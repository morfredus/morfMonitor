/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QByteArray>
#include "morfmonitor/ServiceConfig.h"

class QTcpServer;
class QTcpSocket;

namespace morfmonitor {

class ModuleRegistry;

// -----------------------------------------------------------------------------
// HttpServer : serveur HTTP/1.1 minimal, gerant GET *et* POST (avec corps).
//
// Routes fournies :
//   GET  /status        -> compatible morfBeacon (app, version, uptime, metrics)
//   GET  /healthz       -> { "status": "ok" }
//   GET  /modules       -> etat de tous les modules
//   GET  /modules/{id}  -> etat d'un module
//   GET  /api/...       -> supervision (systeme, ressources, reseau, services...)
//   GET  /  /styles.css  /app.js -> interface Web (assets statiques embarques)
//   POST /example       -> exemple de reception d'un corps JSON (a remplacer)
//
// L'interface Web est servie comme des fichiers INERTES : aucun gabarit, aucune
// donnee injectee cote serveur. Elle consomme les memes routes /api/ que
// n'importe quel autre client, et n'a donc AUCUN acces privilegie a l'etat
// interne du service.
//
// Cette contrainte n'est pas cosmetique. morfMonitor annonce « il n'affiche
// rien » : sa responsabilite est de collecter et d'exposer, pas de presenter.
// Tant que la vue Web reste un client de l'API publique, elle n'est qu'une
// SECONDE VUE des memes donnees — extractible a tout moment vers un projet
// separe sans reecriture. Le jour ou elle lirait MonitorModule directement,
// cette propriete serait perdue en silence.
// -----------------------------------------------------------------------------
class HttpServer : public QObject {
    Q_OBJECT
public:
    HttpServer(ServiceConfig config, ModuleRegistry* registry, QObject* parent = nullptr);
    ~HttpServer() override;

    bool start();
    void stop();
    bool isListening() const;
    quint16 port() const;

private:
    void onNewConnection();
    void onSocketReadyRead(QTcpSocket* sock);
    void handleRequest(QTcpSocket* sock, const QByteArray& method,
                       const QByteArray& path, const QByteArray& body);
    QByteArray handleExamplePost(const QByteArray& body, int& code, QByteArray& reason) const;
    QByteArray buildStatusJson() const;

    // Sert un asset embarque (:/web/...). Renvoie false si le chemin ne
    // correspond a aucun asset : l'appelant poursuit alors son routage.
    bool serveWebAsset(QTcpSocket* sock, const QByteArray& path);

    void reply(QTcpSocket* sock, int code, const QByteArray& reason, const QByteArray& body,
               const QByteArray& contentType = "application/json; charset=utf-8");

    ServiceConfig   m_config;
    ModuleRegistry* m_registry;
    QTcpServer*     m_server;
    QElapsedTimer   m_uptime;
};

} // namespace morfmonitor
