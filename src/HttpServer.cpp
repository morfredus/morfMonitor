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
    const QByteArray path = rawPath.left(rawPath.indexOf('?') < 0 ? rawPath.size()
                                                                  : rawPath.indexOf('?'));
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
        } else if (!sock->peerAddress().isLoopback()) {
            code = 403; reason = "Forbidden";
            out = "{\"error\":\"remote update requests are unavailable\"}";
        } else {
            out = handleLocalUpdate(body, code, reason);
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
        } else {
            code = 404; reason = "Not Found";
            out = "{\"error\":\"route inconnue\",\"routes\":[\"/api/system\","
                  "\"/api/resources\",\"/api/network\",\"/api/services\","
                  "\"/api/reboot\",\"/api/config\",\"/api/all\"]}";
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

QByteArray HttpServer::handleLocalUpdate(const QByteArray& body, int& code, QByteArray& reason) {
    if (!m_config.updateAgentEnabled || m_config.updateAgentTokenFile.isEmpty()) {
        code = 503; reason = "Service Unavailable";
        return "{\"error\":\"agent de mise à jour indisponible\"}";
    }
    const QJsonDocument request = QJsonDocument::fromJson(body);
    const QJsonObject object = request.object();
    static const QRegularExpression identifier(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    const QString project = object.value("project").toString();
    const QString version = object.value("version").toString();
    if (!request.isObject() || !identifier.match(project).hasMatch()
        || !identifier.match(version).hasMatch()) {
        code = 400; reason = "Bad Request";
        return "{\"error\":\"projet et version déclarés requis\"}";
    }
    QString tokenPath = m_config.updateAgentTokenFile;
#ifdef Q_OS_WIN
    tokenPath.replace(QStringLiteral("@morfupdate-state@"),
                      QDir(qEnvironmentVariable("ProgramData")).filePath(
                          QStringLiteral("morfsystem/morfupdate/state")));
#else
    tokenPath.replace(QStringLiteral("@morfupdate-state@"),
                      QStringLiteral("/var/lib/morfsystem/morfupdate"));
#endif
    QFile tokenFile(tokenPath);
    if (!tokenFile.open(QIODevice::ReadOnly)) {
        code = 503; reason = "Service Unavailable";
        return "{\"error\":\"jeton local de mise à jour inaccessible\"}";
    }
    const QByteArray token = tokenFile.readAll().trimmed();
    if (token.size() < 32) {
        code = 503; reason = "Service Unavailable";
        return "{\"error\":\"jeton local de mise à jour invalide\"}";
    }
    QNetworkAccessManager manager;
    QNetworkRequest agent(QUrl(QStringLiteral("http://127.0.0.1:8794/api/v1/updates")));
    agent.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    agent.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply* reply = manager.post(agent, QJsonDocument(object).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(5000);
    loop.exec();
    const QByteArray response = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool failed = reply->error() != QNetworkReply::NoError;
    reply->deleteLater();
    if (failed || status == 0) {
        code = 503; reason = "Service Unavailable";
        return "{\"error\":\"agent de mise à jour indisponible\"}";
    }
    code = status;
    reason = status == 202 ? "Accepted" : (status == 409 ? "Conflict" : "Bad Request");
    return response.isEmpty() ? "{\"error\":\"réponse d’agent invalide\"}" : response;
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
    // Vider le tampon d'écriture AVANT de fermer : sur une grande réponse (page HTML,
    // /status volumineux), le corps déborde du tampon socket (~20 Ko constaté) et
    // `disconnectFromHost` seul en tronque la fin côté client. On draine jusqu'à ce
    // qu'il ne reste rien à écrire, avec un délai de garde pour ne jamais bloquer.
    while (sock->bytesToWrite() > 0)
        if (!sock->waitForBytesWritten(2000))
            break;
    sock->disconnectFromHost();
}

} // namespace morfmonitor
