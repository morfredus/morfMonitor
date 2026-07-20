/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>

namespace morfmonitor {

// -----------------------------------------------------------------------------
// SharedConfig : la configuration PARTAGEE de l'ecosysteme morfSystem.
//
// Un seul fichier (/etc/morfsystem/morfsystem.json) decrit les composants
// supervises, et il est lu par DEUX programmes ecrits dans deux langages :
// morfMonitor (C++), qui collecte, et RaspberryDashboard (Python), qui affiche.
//
// C'est la raison d'etre de ce fichier : tant que chacun portait sa propre
// liste, ajouter un service demandait de modifier du code a deux endroits, avec
// la garantie qu'ils divergeraient. Desormais, ajouter un composant ne demande
// QUE d'editer ce fichier.
//
// Le format est du JSON, et non un fichier Python ou un en-tete C++, precisement
// parce qu'aucun des deux langages n'y est privilegie.
// -----------------------------------------------------------------------------

// Un service systemd local, interroge par « systemctl is-active ».
struct SystemdServiceDef {
    QString unit;      // nom systemd, sans « .service »
    QString label;     // texte affiche
    bool    enabled = true;
};

// Un equipement surveille par sonde reseau. Un ESP32 ne repond pas a systemctl :
// on teste l'ouverture de son port TCP.
struct NetworkServiceDef {
    QString name;
    QString label;
    QString host;      // nom mDNS ou IP fixe
    int     port = 80;
    int     timeoutMs = 1000;
    bool    enabled = true;
};

// Une application annoncant sa presence par heartbeat morfBeacon.
struct BeaconAppDef {
    QString app;       // nom annonce dans le champ « app » du heartbeat
    QString label;
    bool    enabled = true;
};

class SharedConfig {
public:
    // Chemins cherches, dans l'ordre. Le premier trouve gagne :
    //   1. $MORFSYSTEM_CONFIG (surcharge explicite, pratique pour les tests)
    //   2. /etc/morfsystem/morfsystem.json (emplacement d'installation)
    //   3. ./morfsystem.json (execution depuis un clone, sans installation)
    static QStringList searchPaths();

    // Charge la configuration. En cas d'absence ou d'erreur de syntaxe, renvoie
    // false et laisse les valeurs par defaut : le service doit demarrer et
    // repondre meme mal configure, quitte a ne rien superviser. Un service de
    // supervision qui refuse de demarrer est le pire des cas.
    bool load(const QString& path = QString());

    QString loadedPath() const { return m_loadedPath; }
    QString lastError() const { return m_lastError; }
    bool isLoaded() const { return m_loaded; }

    // --- Cadences ------------------------------------------------------------
    int resourcesRefreshMs() const { return m_resourcesMs; }
    int networkRefreshMs() const { return m_networkMs; }
    int systemdRefreshMs() const { return m_systemdMs; }
    int probesRefreshMs() const { return m_probesMs; }

    // --- Composants supervises -----------------------------------------------
    const QVector<SystemdServiceDef>& systemdServices() const { return m_systemd; }
    const QVector<NetworkServiceDef>& networkServices() const { return m_network; }
    const QVector<BeaconAppDef>& beaconApps() const { return m_beaconApps; }

    int networkProbeGraceS() const { return m_probeGraceS; }
    quint16 beaconPort() const { return m_beaconPort; }
    int beaconOfflineAfterS() const { return m_beaconOfflineS; }

    // NOTE : le port d'ecoute et l'adresse de bind ne sont volontairement PAS
    // ici. Ils appartiennent a la configuration propre du service
    // (morfmonitor.json). Ils ont figure dans ce fichier partage sans jamais
    // etre utilises, ce qui a fait croire que le port etait regle alors que le
    // service ecoutait ailleurs — et l'a mis en collision avec morfAnalytics.
    // Un reglage qui ne regle rien est pire qu'un reglage absent.

    // Vue JSON de la configuration effective, exposee par l'API. Elle permet a
    // un client de savoir ce qui est supervise sans lire le fichier lui-meme —
    // utile pour un client qui n'a pas acces au disque de la machine (ESP32,
    // navigateur, application distante).
    QJsonObject toJson() const;

private:
    bool    m_loaded = false;
    QString m_loadedPath;
    QString m_lastError;

    int m_resourcesMs = 2000;
    int m_networkMs   = 10000;
    int m_systemdMs   = 5000;
    int m_probesMs    = 15000;

    QVector<SystemdServiceDef> m_systemd;
    QVector<NetworkServiceDef> m_network;
    QVector<BeaconAppDef>      m_beaconApps;

    int     m_probeGraceS   = 45;
    quint16 m_beaconPort    = 45454;
    int     m_beaconOfflineS = 60;
};

} // namespace morfmonitor
