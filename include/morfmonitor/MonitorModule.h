/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfmonitor/IModule.h"
#include "morfmonitor/SharedConfig.h"
#include "morfmonitor/Collectors.h"

#include <QHash>
#include <QElapsedTimer>
#include <QHostAddress>
#include <memory>

class QUdpSocket;
class QNetworkAccessManager;

namespace morfmonitor {

// -----------------------------------------------------------------------------
// MonitorModule : le cœur de morfMonitor.
//
// Il détient la configuration partagée, les collecteurs, et le cache. Toute
// l'API HTTP se sert ici.
//
// --- Pourquoi un cache ---------------------------------------------------
// L'API est faite pour être interrogée souvent, et par plusieurs clients à la
// fois (le Dashboard local, un futur dashboard web, une application Qt…). Sans
// cache, dix clients rafraîchissant chacun toutes les secondes provoqueraient
// dix lectures de /proc et dix lancements de systemctl par seconde — l'inverse
// exact du but recherché, qui est de SOULAGER la machine en centralisant.
//
// Chaque catégorie a sa propre fraîcheur, parce que leurs coûts et leurs
// rythmes n'ont rien à voir : lire /proc/meminfo est instantané, lancer
// systemctl coûte un processus, et sonder un ESP32 par le réseau peut prendre
// une seconde entière.
// -----------------------------------------------------------------------------
class MonitorModule : public IModule {
    Q_OBJECT
public:
    MonitorModule(const QString& id, QString configPath = QString(),
                  QObject* parent = nullptr);
    ~MonitorModule() override;

    bool start() override;
    void stop() override;
    QJsonObject statusJson() const override;

    // --- Sections exposées par l'API ----------------------------------------
    QJsonObject systemJson();
    QJsonObject resourcesJson();
    QJsonObject networkJson();
    QJsonObject servicesJson();   // systemd + sondes réseau + applications beacon
    QJsonObject rebootJson();
    QJsonObject configJson() const { return m_config.toJson(); }

    // Vue complète, en une seule requête. Un client qui affiche un tableau de
    // bord veut tout à la fois : lui imposer cinq requêtes multiplierait les
    // allers-retours sans rien apporter.
    QJsonObject allJson();

    const SharedConfig& config() const { return m_config; }

private:
    void onBeaconDatagram();

    // Interroge /status d'un service qui annonce « web_ui », une seule fois par
    // version vue. Sans reponse, le service reste simplement sans lien : une
    // interface indisponible ne doit pas degrader la supervision.
    void fetchWebUiIfNeeded(const QString& key);

    // Oublie les applications simplement ENTENDUES qui ne s'annoncent plus
    // depuis longtemps. Les applications declarees sont conservees : leur
    // absence est justement ce qu'on veut voir.
    void pruneStaleBeacons();

    // Adresse de CETTE machine sur l'interface portant la route par defaut,
    // avec sa longueur de prefixe. Sert d'etalon pour juger si l'adresse d'un
    // emetteur appartient au vrai reseau local ou a un reseau virtuel.
    void refreshPrimaryAddress();
    int  addressScore(const QHostAddress& candidate);

    QHostAddress  m_primaryAddress;
    int           m_primaryPrefix = -1;
    QElapsedTimer m_primaryAge;

    struct BeaconSeen;
    // Ajoute a une entree ce qui permet de la JOINDRE, et rien de plus :
    // morfMonitor publie une adresse, il n'ouvre aucune connexion pour le compte
    // de l'utilisateur. C'est la difference entre referencer et mediatiser.
    static void addReachability(QJsonObject& entry, const BeaconSeen& seen);

    QJsonObject beaconAppsJson() const;
    qint64 uptimeSeconds() const;

    QString      m_configPath;
    SharedConfig m_config;
    bool         m_running = false;

    SystemCollector     m_system;
    ResourceCollector   m_resources;
    NetworkCollector    m_network;
    RebootCauseDetector m_reboot;
    std::unique_ptr<Supervisor> m_supervisor;

    // Cache : valeur + âge, par catégorie.
    struct Cached {
        QJsonObject value;
        QElapsedTimer age;
        bool valid = false;
    };
    Cached m_cResources, m_cNetwork, m_cSystemd, m_cProbes;

    bool isFresh(const Cached& c, int maxAgeMs) const;

    // --- Écoute des heartbeats morfBeacon ------------------------------------
    // On ÉCOUTE, on ne sonde pas : les applications de bureau annoncent leur
    // présence en broadcast, donc aucune adresse à connaître et aucune requête
    // à émettre.
    //
    // Seule exception, et elle est bornée : quand un service annonce la capacité
    // « web_ui », son /status est interrogé UNE fois pour obtenir le détail de
    // son interface. C'est le « pull detail » du protocole, pas une sonde
    // périodique — et cela ne fait de morfMonitor l'intermédiaire de rien : il
    // lit une description, il ne relaie aucun trafic.
    QUdpSocket*            m_beaconSocket = nullptr;
    QNetworkAccessManager* m_http = nullptr;
    struct BeaconSeen {
        qint64  lastSeen = 0;   // secondes Unix
        QString app;            // nom annonce — plusieurs entrees peuvent le partager
        QString instance;       // identite « app@host » du protocole, si annoncee
        QString version;
        QString host;
        QString state;

        // Adresse REELLE de l'emetteur, relevee a la reception du datagramme, et
        // port de son serveur /status. Sans ces deux valeurs, morfMonitor sait
        // qu'un service vit mais pas ou le joindre : aucune navigation n'est
        // possible. `host` ne suffit pas — c'est un nom annonce, qui ne resout
        // pas forcement depuis la machine qui observe.
        QString sourceIp;
        quint16 statusPort = 0;

        // Qualite de `sourceIp` : 2 quand elle appartient au meme reseau que
        // nous, 1 sinon. Un emetteur multi-domicilie diffuse sur TOUTES ses
        // interfaces ; sans ce classement, le dernier datagramme recu gagnait,
        // souvent celui d'un reseau virtuel (WSL, Hyper-V, VPN) — une adresse
        // injoignable depuis n'importe quelle autre machine.
        int addressScore = 0;

        // Capacites annoncees (protocole morfbeacon/1). Un consommateur
        // s'appuie sur elles, jamais sur le nom de l'application.
        QStringList capabilities;

        // Detail de l'interface Web, obtenu en interrogeant /status une fois la
        // capacite « web_ui » reperee. Vide tant qu'elle ne l'est pas : le
        // heartbeat annonce, HTTP detaille.
        QJsonObject webUi;
        bool        webUiFetched = false;
    };
    // Clé = identité d'INSTANCE (champ « instance » du protocole, ou app@ip à
    // défaut), jamais le seul nom « app » : le même service tournant sur deux
    // machines est deux instances, et les indexer par nom les faisait s'écraser
    // l'une l'autre à chaque heartbeat — l'affichage alternait entre les hôtes
    // toutes les quinze secondes. PROTOCOL.md avait prévu le champ pour ça.
    QHash<QString, BeaconSeen> m_beaconSeen;
};

} // namespace morfmonitor
