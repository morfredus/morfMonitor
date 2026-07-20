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
#include <memory>

class QUdpSocket;

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
    QUdpSocket* m_beaconSocket = nullptr;
    struct BeaconSeen {
        qint64  lastSeen = 0;   // secondes Unix
        QString version;
        QString host;
        QString state;
    };
    QHash<QString, BeaconSeen> m_beaconSeen;  // clé = nom annoncé (« app »)
};

} // namespace morfmonitor
