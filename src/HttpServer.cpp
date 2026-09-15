/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/HttpServer.h"
#include "morfmonitor/ModuleRegistry.h"
#include "morfmonitor/MonitorModule.h"
#include "morfmonitor/Version.h"
#include "morfmonitor/SelfDescription.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDateTime>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QPointer>
#include <QRegularExpression>

#include <utility>

// Les ressources Qt embarquees dans une bibliotheque STATIQUE ne s'enregistrent
// pas toutes seules : l'editeur de liens ecarte l'initialiseur global de
// qrc_web.cpp puisque rien ne le reference. Sans cet appel explicite, le binaire
// compile et demarre normalement, mais ":/web/index.html" reste introuvable et
// l'interface Web repond 500. L'appel doit vivre hors de tout namespace projet.
static void morfmonitorInitWebResources() {
    Q_INIT_RESOURCE(web);
}

namespace morfmonitor {

namespace {
constexpr int kMaxRequestBytes = 65536;

QByteArray toJson(const QJsonObject& o) {
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

int contentLength(const QByteArray& headerBlock) {
    for (const QByteArray& line : headerBlock.split('\n')) {
        const QByteArray l = line.trimmed();
        if (l.toLower().startsWith("content-length:"))
            return l.mid(l.indexOf(':') + 1).trimmed().toInt();
    }
    return 0;
}

// Valeur d'un parametre de query string (« a=1&b=2 »), decodee. Chaine vide si
// absent. Sert aux routes de la memoire temporelle (fenetre, bornes de jours,
// filtre par service), les seules a exploiter la query -- le reste de l'API n'en
// a pas besoin.
QString queryParam(const QByteArray& query, const char* key) {
    const QByteArray k = QByteArray(key) + "=";
    for (const QByteArray& part : query.split('&')) {
        if (part.startsWith(k))
            return QUrl::fromPercentEncoding(part.mid(k.size()));
    }
    return QString();
}
} // namespace

HttpServer::HttpServer(ServiceConfig config, ModuleRegistry* registry, QObject* parent)
    : QObject(parent),
      m_config(std::move(config)),
      m_registry(registry),
      m_server(new QTcpServer(this)) {
    morfmonitorInitWebResources();
    connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

HttpServer::~HttpServer() = default;

bool HttpServer::start() {
    if (m_config.httpPort == 0)
        return false;
    m_uptime.start();
    QHostAddress addr(m_config.bindAddress);
    if (addr.isNull())
        addr = QHostAddress(QHostAddress::AnyIPv4);
    return m_server->listen(addr, m_config.httpPort);
}

void HttpServer::stop()            { m_server->close(); }
bool HttpServer::isListening() const { return m_server->isListening(); }
quint16 HttpServer::port() const   { return m_server->isListening() ? m_server->serverPort() : 0; }

void HttpServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { onSocketReadyRead(sock); });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void HttpServer::onSocketReadyRead(QTcpSocket* sock) {
    QByteArray buf = sock->property("buf").toByteArray();
    buf += sock->readAll();

    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buf.size() > kMaxRequestBytes) { sock->abort(); return; }
        sock->setProperty("buf", buf);
        return;
    }

    const QByteArray headerBlock = buf.left(headerEnd);
    const int needed = contentLength(headerBlock);
    const int bodyStart = headerEnd + 4;
    if (buf.size() - bodyStart < needed) {
        if (buf.size() > kMaxRequestBytes) { sock->abort(); return; }
        sock->setProperty("buf", buf);
        return;
    }

    const int lineEnd = buf.indexOf("\r\n");
    const QList<QByteArray> parts = buf.left(lineEnd).split(' ');
    const QByteArray method = parts.value(0);
    const QByteArray path   = parts.value(1);
    const QByteArray body   = buf.mid(bodyStart, needed);

    sock->setProperty("buf", QByteArray());
    handleRequest(sock, method, path, body);
}

void HttpServer::handleRequest(QTcpSocket* sock, const QByteArray& method,
                               const QByteArray& rawPath, const QByteArray& body) {
    const int qMark = rawPath.indexOf('?');
    const QByteArray path  = rawPath.left(qMark < 0 ? rawPath.size() : qMark);
    const QByteArray query = qMark < 0 ? QByteArray() : rawPath.mid(qMark + 1);
    int        code   = 200;
    QByteArray reason = "OK";
    QByteArray out;

    // HEAD = GET sans corps. Un service de supervision est precisement ce qu'on
    // sonde : repondre 405 a une sonde HEAD la ferait conclure que le service
    // est en panne alors qu'il repond parfaitement. Le routage est donc commun,
    // et seul l'envoi du corps est supprime (les en-tetes, Content-Length
    // compris, restent ceux qu'aurait produits le GET, comme l'exige HTTP).
    const bool isHead = (method == "HEAD");
    const QByteArray verb = isHead ? QByteArray("GET") : method;
    sock->setProperty("head", isHead);

    // ---- Route POST : oublier une machine du registre --------------------
    // Geste explicite de l'utilisateur (« Oublier cette machine »), le seul moyen
    // de retirer une machine reellement partie du parc. Corps : {"host":"pi4dev"}.
    if (path == "/api/machines/forget") {
        if (verb != "POST") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use POST /api/machines/forget\",\"allow\":\"POST\"}";
        } else {
            out = handleForgetMachine(body, code, reason);
        }
    }
    // ---- Route POST : forcer une verification des versions ----------------
    // Bouton « Verifier les versions » : declenche une verification FRAICHE (meme
    // cache valide). Non bloquant : les resultats arrivent en arriere-plan et
    // seront visibles au prochain /api/all. Le corps est vide.
    else if (path == "/api/versions/check") {
        if (verb != "POST") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use POST /api/versions/check\",\"allow\":\"POST\"}";
        } else {
            auto* mon = m_registry
                ? qobject_cast<MonitorModule*>(m_registry->firstOfType(QStringLiteral("monitor")))
                : nullptr;
            if (!mon) {
                code = 503; reason = "Service Unavailable";
                out = "{\"error\":\"aucun module de supervision actif\"}";
            } else {
                mon->triggerVersionCheck();
                out = "{\"status\":\"checking\"}";
            }
        }
    }
    // The browser never talks to the privileged agent and never receives its
    // token. A request is accepted only from this machine, then proxied to the
    // fixed loopback endpoint with the protected local credential.
    else if (path == "/api/updates") {
        if (verb != "POST") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use POST /api/updates\",\"allow\":\"POST\"}";
        } else {
            // Le navigateur peut consulter cette interface depuis le LAN. La
            // cible reste pourtant locale : morfMonitor délègue seulement à
            // l'agent lié à 127.0.0.1 et ne reçoit ni hôte ni URL à choisir.
            // L'adresse du navigateur ne définit donc pas la portée de la
            // mise à jour et ne doit pas bloquer cette délégation locale.
            // Relais ASYNC : le handler possede la reponse (envoyee dans le
            // callback reseau), on ne retombe donc pas sur le reply() final.
            handleLocalUpdate(sock, body);
            return;
        }
    }
    else if (path.startsWith("/api/updates/")) {
        if (verb != "GET") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use GET /api/updates/<id>\",\"allow\":\"GET\"}";
        } else {
            handleLocalUpdateStatus(sock, path.mid(QByteArray("/api/updates/").size()));
            return;
        }
    }
    // Relance manuelle d'un service bloqué : même délégation locale que les mises
    // à jour. Le navigateur ne parle jamais à l'agent privilégié ; morfMonitor
    // relaie vers l'agent lié à 127.0.0.1. Le statut se suit ensuite par
    // /api/updates/<id> (journal d'opérations commun côté morfUpdate).
    else if (path == "/api/restart") {
        if (verb != "POST") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use POST /api/restart\",\"allow\":\"POST\"}";
        } else {
            handleLocalRestart(sock, body);
            return;
        }
    }
    // ---- Routes GET (et HEAD) --------------------------------------------
    else if (verb != "GET") {
        code = 405; reason = "Method Not Allowed";
        out = "{\"error\":\"method not allowed\",\"allow\":\"GET, HEAD\"}";
    } else if (path.startsWith("/api/")) {
        // API de supervision : la raison d'etre du service. Toutes les routes
        // renvoient du JSON et sont utilisables par n'importe quel client —
        // Dashboard local, navigateur, application Qt, ESP32 — sans qu'aucun
        // n'ait besoin de lire /proc ni d'appeler systemctl lui-meme.
        auto* mon = m_registry
            ? qobject_cast<MonitorModule*>(m_registry->firstOfType(QStringLiteral("monitor")))
            : nullptr;
        if (!mon) {
            code = 503; reason = "Service Unavailable";
            out = "{\"error\":\"aucun module de supervision actif\"}";
        } else if (path == "/api/system") {
            out = toJson(mon->systemJson());
        } else if (path == "/api/resources") {
            out = toJson(mon->resourcesJson());
        } else if (path == "/api/network") {
            out = toJson(mon->networkJson());
        } else if (path == "/api/services") {
            out = toJson(mon->servicesJson());
        } else if (path == "/api/reboot") {
            out = toJson(mon->rebootJson());
        } else if (path == "/api/config") {
            out = toJson(mon->configJson());
        } else if (path == "/api/all") {
            out = toJson(mon->allJson());
        }
        // ---- Memoire temporelle (contrat morfhistory/1, lecture seule) -------
        // Ruban brut de la fenetre 24 h : since/until en secondes epoch (0 = borne
        // ouverte), service = filtre par nom d'application (optionnel).
        else if (path == "/api/events") {
            const qint64 since = queryParam(query, "since").toLongLong();
            const qint64 until = queryParam(query, "until").toLongLong();
            const QString svc  = queryParam(query, "service");
            out = toJson(mon->eventsJson(since, until, svc));
        }
        // Statistiques journalieres : from/to au format « yyyy-MM-dd » (bornes
        // incluses, vides = tout), service = filtre par instance (optionnel).
        else if (path == "/api/stats/daily") {
            const QString from = queryParam(query, "from");
            const QString to   = queryParam(query, "to");
            const QString svc  = queryParam(query, "service");
            out = toJson(mon->dailyStatsJson(from, to, svc));
        }
        // Roll-ups longs, derives des jours : trimestres et annees.
        else if (path == "/api/stats/quarterly") {
            out = toJson(mon->quarterlyStatsJson());
        } else if (path == "/api/stats/annual") {
            out = toJson(mon->annualStatsJson());
        }
        // Table de vie : totaux depuis le debut, derives des jours.
        else if (path == "/api/stats/life") {
            out = toJson(mon->lifeJson());
        }
        // Historique de sante FIFO (48 h) : uptime + heap par service, releve a
        // chaque /status. Diagnostic d'un figeage (heap declinante, instant de
        // rupture). service=<nom> filtre (optionnel).
        else if (path == "/api/health/history") {
            const QString svc = queryParam(query, "service");
            out = toJson(mon->healthHistoryJson(svc));
        } else {
            code = 404; reason = "Not Found";
            out = "{\"error\":\"route inconnue\",\"routes\":[\"/api/system\","
                  "\"/api/resources\",\"/api/network\",\"/api/services\","
                  "\"/api/reboot\",\"/api/config\",\"/api/all\","
                  "\"/api/events\",\"/api/stats/daily\",\"/api/stats/quarterly\","
                  "\"/api/stats/annual\",\"/api/stats/life\",\"/api/health/history\"]}";
        }
    } else if (path == "/healthz") {
        out = "{\"status\":\"ok\"}";
    } else if (path == "/status") {
        out = buildStatusJson();
    } else if (path == "/modules") {
        QJsonObject o;
        o["modules"] = m_registry ? m_registry->modulesJson() : QJsonArray{};
        o["count"]   = m_registry ? m_registry->count() : 0;
        o["ts"]      = static_cast<double>(QDateTime::currentSecsSinceEpoch());
        out = toJson(o);
    } else if (path.startsWith("/modules/")) {
        const QString id = QUrl::fromPercentEncoding(path.mid(9));
        bool found = false;
        const QJsonObject o = m_registry ? m_registry->moduleJson(id, &found) : QJsonObject{};
        if (found) { out = toJson(o); }
        else { code = 404; reason = "Not Found"; out = "{\"error\":\"module not found\"}"; }
    } else if (serveWebAsset(sock, path)) {
        // Interface Web servie : la reponse est deja partie.
        return;
    } else {
        code = 404; reason = "Not Found";
        out = "{\"error\":\"not found\"}";
    }

    reply(sock, code, reason, out);
}

QByteArray HttpServer::handleForgetMachine(const QByteArray& body, int& code, QByteArray& reason) {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        code = 400; reason = "Bad Request";
        return "{\"error\":\"corps JSON invalide\"}";
    }
    const QString host = doc.object().value(QStringLiteral("host")).toString();
    if (host.isEmpty()) {
        code = 400; reason = "Bad Request";
        return "{\"error\":\"champ 'host' requis\"}";
    }
    auto* mon = m_registry
        ? qobject_cast<MonitorModule*>(m_registry->firstOfType(QStringLiteral("monitor")))
        : nullptr;
    if (!mon) {
        code = 503; reason = "Service Unavailable";
        return "{\"error\":\"aucun module de supervision actif\"}";
    }
    const bool removed = mon->forgetMachine(host);
    if (!removed) {
        // Machine inconnue : rien a oublier. 404 plutot qu'une fausse reussite,
        // pour que l'interface ne pretende pas avoir agi sur du vide.
        code = 404; reason = "Not Found";
        return toJson(QJsonObject{{"error", QStringLiteral("machine inconnue")},
                                  {"host", host}});
    }
    return toJson(QJsonObject{{"forgotten", host}, {"ok", true}});
}

void HttpServer::handleLocalUpdate(QTcpSocket* sock, const QByteArray& body) {
    if (!m_config.updateAgentEnabled) {
        reply(sock, 503, "Service Unavailable",
              "{\"error\":\"agent de mise à jour indisponible\"}");
        return;
    }
    const QJsonDocument request = QJsonDocument::fromJson(body);
    const QJsonObject object = request.object();
    static const QRegularExpression identifier(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    const QString project = object.value("project").toString();
    QString version = object.value("version").toString();
    if (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V')))
        version.remove(0, 1);
    if (!request.isObject() || !identifier.match(project).hasMatch()
        || !identifier.match(version).hasMatch()) {
        reply(sock, 400, "Bad Request", "{\"error\":\"projet et version déclarés requis\"}");
        return;
    }
    const QJsonObject payload{{"project", project}, {"version", version}};
    relayToAgent(sock, "POST", QStringLiteral("http://127.0.0.1:8794/api/v1/updates"),
                 QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void HttpServer::handleLocalRestart(QTcpSocket* sock, const QByteArray& body) {
    if (!m_config.updateAgentEnabled) {
        reply(sock, 503, "Service Unavailable", "{\"error\":\"agent local indisponible\"}");
        return;
    }
    const QJsonDocument request = QJsonDocument::fromJson(body);
    const QJsonObject object = request.object();
    static const QRegularExpression identifier(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    const QString project = object.value("project").toString();
    if (!request.isObject() || !identifier.match(project).hasMatch()) {
        reply(sock, 400, "Bad Request", "{\"error\":\"projet déclaré requis\"}");
        return;
    }
    // Le client ne fournit QUE le projet (cle morfUpdate.targets). Le service
    // systemd reel est resolu par l'agent, jamais recu ni execute tel quel.
    const QJsonObject payload{{"project", project}};
    relayToAgent(sock, "POST", QStringLiteral("http://127.0.0.1:8794/api/v1/restart"),
                 QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void HttpServer::handleLocalUpdateStatus(QTcpSocket* sock, const QByteArray& id) {
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9-]{1,128}$"));
    if (!m_config.updateAgentEnabled || !identifier.match(QString::fromUtf8(id)).hasMatch()) {
        reply(sock, 400, "Bad Request", "{\"error\":\"identifiant d’opération invalide\"}");
        return;
    }
    relayToAgent(sock, "GET",
                 QStringLiteral("http://127.0.0.1:8794/api/v1/updates/") + QString::fromUtf8(id),
                 QByteArray());
}

// Cœur commun du relais async vers l'agent morfUpdate local (voir HttpServer.h).
void HttpServer::relayToAgent(QTcpSocket* sock, const QByteArray& method,
                              const QString& url, const QByteArray& body) {
    if (!m_relay)
        m_relay = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(url)};
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    // Borne les 5 s SANS boucle d'evenements imbriquee ni QTimer : Qt annule la
    // requete au-dela et emet finished avec une erreur (status restera 0).
    req.setTransferTimeout(5000);
    QNetworkReply* rep = (method == "POST") ? m_relay->post(req, body) : m_relay->get(req);

    // Le client attend, sa socket reste ouverte. QPointer : s'il se deconnecte
    // (onglet ferme, coupure reseau) avant la reponse de l'agent, la socket est
    // detruite (disconnected -> deleteLater) et le pointeur s'annule -> on n'ecrit
    // alors rien. L'operation, elle, vit cote agent : elle ne depend pas du client.
    QPointer<QTcpSocket> guard(sock);
    connect(rep, &QNetworkReply::finished, this, [this, rep, guard]() {
        const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray response = rep->readAll();
        const QString relayErr = rep->errorString();
        rep->deleteLater();
        if (!guard)
            return;
        // status == 0 : AUCUNE reponse HTTP = vrai injoignable (agent eteint, refus,
        // timeout). Sinon morfUpdate a REPONDU : on propage code + corps tels quels
        // (QNetworkReply::error() est non-nul pour tout 4xx/5xx : s'y fier masquait
        // les erreurs applicatives legitimes derriere un 503 trompeur).
        if (status == 0) {
            reply(guard, 503, "Service Unavailable",
                  toJson(QJsonObject{
                      {"error", QStringLiteral("agent injoignable (127.0.0.1:8794)")},
                      {"detail", relayErr}}));
            return;
        }
        reply(guard, status, reasonForStatus(status),
              response.isEmpty() ? QByteArray("{\"error\":\"réponse d’agent invalide\"}")
                                 : response);
    });
}

QByteArray HttpServer::reasonForStatus(int status) {
    switch (status) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return status < 400 ? "OK" : "Error";
    }
}

QByteArray HttpServer::buildStatusJson() const {
    QJsonObject o;
    o["app"]      = m_config.appName;
    o["host"]     = QHostInfo::localHostName();
    o["version"]  = morfmonitor::version();
    o["proto"]    = QString::fromLatin1(morfmonitor::kProtocol);
    o["state"]    = m_registry ? m_registry->state() : QStringLiteral("ok");
    o["uptime_s"] = static_cast<double>(m_uptime.isValid() ? m_uptime.elapsed() / 1000 : 0);
    o["ts"]       = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    o["metrics"]  = m_registry ? m_registry->metrics() : QJsonObject{};

    // Detail annonce (interface web + API). morfMonitor sert son PROPRE /status
    // plutot que le StatusServer de morfBeacon ; il appelle donc le MEME point
    // unique (fillAnnouncedDetail + describeService) pour que son /status et son
    // heartbeat ne puissent pas diverger. Tout service reimplementant /status
    // contracte la meme obligation.
    morfbeacon::PresenceConfig self;
    fillAnnouncedDetail(self, m_config.webEnabled);
    const quint16 uiPort = m_server->isListening() ? m_server->serverPort()
                                                   : m_config.httpPort;
    const QJsonObject detail = morfbeacon::describeService(self, uiPort);
    for (auto it = detail.constBegin(); it != detail.constEnd(); ++it)
        o[it.key()] = it.value();

    return toJson(o);
}

bool HttpServer::serveWebAsset(QTcpSocket* sock, const QByteArray& path) {
    if (!m_config.webEnabled)
        return false;

    // Table close : seuls ces trois chemins sont servis. Pas de traversee de
    // repertoire possible, puisque rien n'est construit a partir de l'URL.
    struct Asset { const char* route; const char* file; const char* type; };
    static const Asset kAssets[] = {
        { "/",           ":/web/index.html", "text/html; charset=utf-8" },
        { "/index.html", ":/web/index.html", "text/html; charset=utf-8" },
        { "/styles.css", ":/web/styles.css", "text/css; charset=utf-8" },
        { "/app.js",     ":/web/app.js",     "application/javascript; charset=utf-8" },
    };

    for (const Asset& a : kAssets) {
        if (path != a.route)
            continue;
        QFile f(QString::fromLatin1(a.file));
        if (!f.open(QIODevice::ReadOnly)) {
            reply(sock, 500, "Internal Server Error",
                  "{\"error\":\"asset embarque illisible\"}");
            return true;
        }
        reply(sock, 200, "OK", f.readAll(), a.type);
        return true;
    }
    return false;
}

void HttpServer::reply(QTcpSocket* sock, int code, const QByteArray& reason, const QByteArray& body,
                       const QByteArray& contentType) {
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    resp += "Content-Type: " + contentType + "\r\n";
    // Rien de ce que sert ce service ne doit etre mis en cache. Une reponse
    // /api/ en cache afficherait un etat perime dans un outil de supervision --
    // le contraire de sa raison d'etre. Et un asset en cache fait survivre
    // l'ancienne interface a une mise a jour du binaire, panne d'autant plus
    // deroutante que le service, lui, a bien ete mis a jour.
    resp += "Cache-Control: no-store\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Access-Control-Allow-Origin: *\r\n";
    if (code == 405)
        resp += "Allow: GET, HEAD\r\n";
    resp += "Connection: close\r\n\r\n";
    // Content-Length annonce la taille qu'aurait le corps ; sur HEAD, le corps
    // lui-meme n'est pas envoye.
    if (!sock->property("head").toBool())
        resp += body;
    sock->write(resp);
    sock->flush();
    // Fermeture ASYNCHRONE, jamais bloquante. disconnectFromHost() passe la socket en
    // ClosingState et Qt draine le tampon restant EN ARRIERE-PLAN avant de fermer
    // (disconnected -> deleteLater). L'ancienne boucle waitForBytesWritten(2000)
    // bloquait ici le THREAD PRINCIPAL le temps qu'un client lent absorbe une grosse
    // reponse (/api/all fait ~20 Ko) ; sur un lien Wi-Fi degrade vers un observateur
    // distant, ce blocage durait, et pendant ce temps le QTimer du heartbeat morfBeacon
    // (MEME event-loop) ne pouvait plus emettre : morfMonitor « disparaissait » du parc
    // alors qu'il tournait, declenchant de fausses alertes de panne fonctionnelle. On
    // ne bloque donc plus jamais l'event-loop sur l'ecriture reseau.
    sock->disconnectFromHost();
    // Garde-fou : un client reellement mort (Wi-Fi coupe en plein envoi) laisserait la
    // socket en ClosingState. On la coupe apres 10 s pour ne pas accumuler de sockets
    // pendant une degradation reseau -- large pour un client vivant, meme lent. `sock`
    // en objet-contexte : si la socket est deja detruite (disconnected -> deleteLater),
    // le timer est annule, donc aucun pointeur pendouillant.
    QTimer::singleShot(10000, sock, [sock]() {
        if (sock->state() != QAbstractSocket::UnconnectedState)
            sock->abort();
    });
}

} // namespace morfmonitor
