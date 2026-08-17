/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/Service.h"
#include "morfmonitor/ModuleRegistry.h"
#include "morfmonitor/HttpServer.h"
#include "morfmonitor/ModuleFactory.h"
#include "morfmonitor/IModule.h"
#include "morfmonitor/Version.h"
#include "morfmonitor/SelfDescription.h"

#include "morfbeacon/Heartbeat.h"
#include "morfbeacon/PresenceConfig.h"

#include <utility>

namespace morfmonitor {

Service::Service(ServiceConfig config, QObject* parent)
    : QObject(parent),
      m_config(std::move(config)),
      m_registry(new ModuleRegistry(this)),
      m_http(nullptr) {

    // Construit les modules declares. Une erreur sur l'un (type inconnu,
    // parametre manquant) n'empeche pas les autres : on la note.
    for (const ModuleDef& def : m_config.modules) {
        QString error;
        IModule* m = ModuleFactory::create(def, &error);
        if (!m) {
            m_warnings << error;
            continue;
        }
        m_registry->add(m);
    }

    m_http = new HttpServer(m_config, m_registry, this);
}

Service::~Service() = default;

bool Service::start() {
    m_registry->startAll();
    const bool httpOk = (m_config.httpPort == 0) ? true : m_http->start();

    if (m_config.beaconEnabled) {
        morfbeacon::PresenceConfig pc;
        pc.appName             = m_config.appName;
        pc.version             = morfmonitor::version();
        pc.instanceId          = m_config.instanceId;
        pc.udpPort             = m_config.beaconUdpPort;
        pc.broadcastIntervalMs = m_config.beaconIntervalMs;
        pc.statusPort          = m_http ? m_http->port() : 0;
        pc.statusBindAddress   = m_config.bindAddress;

        // Capacite annoncee : « system_monitor ». C'est par elle qu'un consommateur
        // reconnait un morfMonitor, jamais par son nom (que l'utilisateur peut
        // changer). morfAnalytics s'en sert pour DECOUVRIR seul les morfMonitor du
        // parc et historiser leur /api/all : une nouvelle machine qui s'annonce est
        // integree sans aucune declaration manuelle, exactement comme morfMonitor
        // apprend les machines depuis les heartbeats.
        pc.capabilities        = {QStringLiteral("system_monitor")};

        // Detail annonce (interface web + API) : renseigne par le point unique
        // fillAnnouncedDetail, le meme qu'utilise /status, pour que le heartbeat
        // et /status ne puissent pas diverger. Un observatoire qui s'exempterait
        // de ses propres regles de decouverte n'aurait aucune raison d'etre cru
        // sur les autres.
        fillAnnouncedDetail(pc, m_config.webEnabled);

        m_heartbeat = new morfbeacon::Heartbeat(pc, m_registry, this);
        m_heartbeat->start();
    }

    return httpOk;
}

void Service::stop() {
    if (m_heartbeat)
        m_heartbeat->stop();
    if (m_http)
        m_http->stop();
    m_registry->stopAll();
}

int Service::moduleCount() const   { return m_registry->count(); }
quint16 Service::httpPort() const  { return m_http ? m_http->port() : 0; }
QStringList Service::warnings() const { return m_warnings; }
ModuleRegistry* Service::registry() const { return m_registry; }

} // namespace morfmonitor
