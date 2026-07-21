/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/HttpServer.h"
#include "morfmonitor/ModuleRegistry.h"
#include "morfmonitor/MonitorModule.h"
#include "morfmonitor/Version.h"

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

    // ---- Exemple de route POST (a remplacer par vos endpoints metier) ----
    if (path == "/example") {
        if (verb != "POST") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use POST /example\",\"allow\":\"POST\"}";
        } else {
            out = handleExamplePost(body, code, reason);
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

QByteArray HttpServer::handleExamplePost(const QByteArray& body, int& code, QByteArray& reason) const {
    // >>> EXEMPLE : parse un corps JSON et repond. A remplacer par votre metier.
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        code = 400; reason = "Bad Request";
        return "{\"error\":\"corps JSON invalide\"}";
    }
    QJsonObject o;
    o["received"] = doc.object();
    o["ts"]       = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return toJson(o);
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

    // Detail de l'interface Web, attendu par tout consommateur qui a vu passer
    // la capacite « web_ui » dans le heartbeat.
    //
    // morfMonitor sert son PROPRE /status au lieu d'utiliser le StatusServer de
    // morfBeacon : il doit donc publier ce bloc lui-meme. Sans cela, il
    // annoncerait une capacite dont le detail resterait introuvable, et un
    // observateur ne pourrait pas construire le lien. Tout service qui
    // reimplemente /status contracte la meme obligation.
    if (m_config.webEnabled) {
        QJsonObject ui;
        ui["path"]        = QStringLiteral("/");
        ui["label"]       = QStringLiteral("Supervision");
        ui["port"]        = static_cast<int>(m_server->isListening() ? m_server->serverPort()
                                                                     : m_config.httpPort);
        ui["description"] = QStringLiteral("Etat de la machine et des services morfSystem.");
        o["web_ui"] = ui;
    }
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
    sock->disconnectFromHost();
}

} // namespace morfmonitor
