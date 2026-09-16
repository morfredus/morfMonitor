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
class QNetworkAccessManager;

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
    QByteArray handleForgetMachine(const QByteArray& body, int& code, QByteArray& reason);

    // --- Relais vers l'agent local morfUpdate (127.0.0.1:8794) ---------------
    // ASYNCHRONES : ces handlers valident la requete de façon synchrone (agent
    // active ? projet/identifiant valide ?) puis, si tout est bon, lancent la
    // requete reseau vers morfUpdate SANS bloquer -- la reponse au client est
    // ecrite plus tard, dans le callback Qt (voir relayToAgent). Aucune boucle
    // d'evenements imbriquee, aucune attente bloquante : l'event-loop de
    // morfMonitor n'est jamais perturbe pendant que morfUpdate travaille. En cas
    // d'erreur de validation, le handler repond lui-meme (synchrone) et n'ouvre
    // aucune requete. Chacun possede donc entierement sa reponse (comme
    // serveWebAsset) : handleRequest les appelle puis `return`.
    void handleLocalUpdate(QTcpSocket* sock, const QByteArray& body);
    void handleLocalUpdateStatus(QTcpSocket* sock, const QByteArray& id);
    void handleLocalRestart(QTcpSocket* sock, const QByteArray& body);

    // Cœur commun du relais async. Emet `method` (GET/POST) vers `url` avec `body`
    // (vide pour un GET), timeout de transfert 5 s (borne sans boucle imbriquee),
    // et, a la reponse : distingue transport (status == 0 => 503 injoignable +
    // detail) et reponse applicative (status != 0 => propage code + corps de
    // morfUpdate tels quels). Sûr si le client se deconnecte entre-temps (QPointer).
    void relayToAgent(QTcpSocket* sock, const QByteArray& method,
                      const QString& url, const QByteArray& body);
    static QByteArray reasonForStatus(int status);

    QByteArray buildStatusJson() const;

    // Sert un asset embarque (:/web/...). Renvoie false si le chemin ne
    // correspond a aucun asset : l'appelant poursuit alors son routage.
    bool serveWebAsset(QTcpSocket* sock, const QByteArray& path);

    void reply(QTcpSocket* sock, int code, const QByteArray& reason, const QByteArray& body,
               const QByteArray& contentType = "application/json; charset=utf-8",
               const QByteArray& extraHeaders = QByteArray());

    ServiceConfig   m_config;
    ModuleRegistry* m_registry;
    QTcpServer*     m_server;
    QElapsedTimer   m_uptime;
    // QNAM persistant pour le relais async vers morfUpdate (cree a la demande).
    // Membre (pas sur la pile) : la requete survit au retour du handler.
    QNetworkAccessManager* m_relay = nullptr;
};

} // namespace morfmonitor
