/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/MonitorModule.h"

#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>

namespace morfmonitor {

MonitorModule::MonitorModule(const QString& id, QString configPath, QObject* parent)
    : IModule(id, QStringLiteral("monitor"), parent),
      m_configPath(std::move(configPath)) {}

MonitorModule::~MonitorModule() = default;

bool MonitorModule::start() {
    // Un échec de chargement n'empêche PAS le démarrage : le service doit
    // répondre et servir ce qu'il peut (système, ressources, réseau), même sans
    // liste de composants à superviser. Un superviseur qui refuse de démarrer
    // parce que sa configuration manque est un superviseur inutile au moment
    // précis où on en a le plus besoin.
    m_config.load(m_configPath);
    m_supervisor = std::make_unique<Supervisor>(&m_config);

    m_beaconSocket = new QUdpSocket(this);
    // ShareAddress : d'autres programmes de la machine (le Dashboard en mode
    // dégradé, par exemple) écoutent le même port de diffusion.
    if (m_beaconSocket->bind(QHostAddress::AnyIPv4, m_config.beaconPort(),
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        connect(m_beaconSocket, &QUdpSocket::readyRead,
                this, &MonitorModule::onBeaconDatagram);
    }

    // Amorce la mesure CPU : /proc/stat ne donne que des compteurs cumules, si
    // bien que la premiere lecture ne peut produire aucun taux. Sans cette
    // amorce, la toute premiere requete a l'API renverrait un CPU absent — que
    // les clients afficheraient comme 0 %, une valeur FAUSSE et non « inconnue ».
    m_resources.collect();

    m_running = true;
    return true;
}

void MonitorModule::stop() {
    m_running = false;
    if (m_beaconSocket) {
        m_beaconSocket->close();
        m_beaconSocket = nullptr; // détruit par l'arbre QObject
    }
}

qint64 MonitorModule::uptimeSeconds() const {
    QFile f(QStringLiteral("/proc/uptime"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    return static_cast<qint64>(QString::fromUtf8(f.readAll())
                                   .split(QLatin1Char(' ')).value(0).toDouble());
}

bool MonitorModule::isFresh(const Cached& c, int maxAgeMs) const {
    return c.valid && c.age.isValid() && c.age.elapsed() < maxAgeMs;
}

// --- Heartbeats morfBeacon ---------------------------------------------------

void MonitorModule::onBeaconDatagram() {
    while (m_beaconSocket && m_beaconSocket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_beaconSocket->receiveDatagram();
        const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        if (o.value(QStringLiteral("proto")).toString() != QLatin1String("morfbeacon/1"))
            continue;

        const QString app = o.value(QStringLiteral("app")).toString();
        if (app.isEmpty())
            continue;

        // On enregistre TOUTES les applications entendues, même absentes de la
        // configuration : l'API peut ainsi révéler ce qui tourne réellement sur
        // le réseau, ce qui aide à peupler la configuration.
        BeaconSeen s;
        s.lastSeen = QDateTime::currentSecsSinceEpoch();
        s.version  = o.value(QStringLiteral("version")).toString();
        s.host     = o.value(QStringLiteral("host")).toString();
        s.state    = o.value(QStringLiteral("state")).toString();
        m_beaconSeen.insert(app, s);
    }
}

QJsonObject MonitorModule::beaconAppsJson() const {
    QJsonObject o;
    QJsonArray arr;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int offlineAfter = m_config.beaconOfflineAfterS();

    // 1. Les applications déclarées : toujours listées, en ligne ou non. Une
    //    application attendue mais absente est une information ; l'omettre la
    //    ferait disparaître silencieusement de l'affichage.
    QSet<QString> declared;
    for (const BeaconAppDef& d : m_config.beaconApps()) {
        declared.insert(d.app);
        QJsonObject a;
        a["app"]      = d.app;
        a["label"]    = d.label;
        a["enabled"]  = d.enabled;
        a["declared"] = true;

        const auto it = m_beaconSeen.constFind(d.app);
        const bool seen = (it != m_beaconSeen.constEnd());
        const qint64 age = seen ? (now - it->lastSeen) : -1;
        a["online"] = d.enabled && seen && age >= 0 && age < offlineAfter;
        if (seen) {
            a["last_seen_s"] = static_cast<double>(age);
            a["version"] = it->version;
            a["host"]    = it->host;
            a["state"]   = it->state;
        }
        arr.append(a);
    }

    // 2. Les applications entendues mais NON déclarées, marquées comme telles.
    //    C'est un outil de découverte : brancher un nouveau service et le voir
    //    apparaître ici indique quoi ajouter à la configuration.
    for (auto it = m_beaconSeen.constBegin(); it != m_beaconSeen.constEnd(); ++it) {
        if (declared.contains(it.key()))
            continue;
        const qint64 age = now - it->lastSeen;
        QJsonObject a;
        a["app"]         = it.key();
        a["label"]       = it.key();
        a["declared"]    = false;
        a["online"]      = age < offlineAfter;
        a["last_seen_s"] = static_cast<double>(age);
        a["version"]     = it->version;
        a["host"]        = it->host;
        a["state"]       = it->state;
        arr.append(a);
    }

    o["apps"] = arr;
    o["offline_after_s"] = offlineAfter;
    return o;
}

// --- Sections de l'API -------------------------------------------------------

QJsonObject MonitorModule::systemJson() {
    // Presque entièrement statique : aucun cache nécessaire.
    return m_system.collect();
}

QJsonObject MonitorModule::resourcesJson() {
    if (isFresh(m_cResources, m_config.resourcesRefreshMs()))
        return m_cResources.value;
    m_cResources.value = m_resources.collect();
    m_cResources.age.restart();
    m_cResources.valid = true;
    return m_cResources.value;
}

QJsonObject MonitorModule::networkJson() {
    if (isFresh(m_cNetwork, m_config.networkRefreshMs()))
        return m_cNetwork.value;
    m_cNetwork.value = m_network.collect();
    m_cNetwork.age.restart();
    m_cNetwork.valid = true;
    return m_cNetwork.value;
}

QJsonObject MonitorModule::servicesJson() {
    QJsonObject o;

    // Nouvelle tentative de chargement si la configuration manquait au
    // demarrage. Un service lance avant que le fichier partage existe (ordre de
    // demarrage, installation en cours) restait sinon aveugle jusqu'a son
    // prochain redemarrage — en repondant correctement, mais sans rien
    // superviser, ce qui est le pire des deux mondes.
    if (!m_config.isLoaded()) {
        if (m_config.load(m_configPath)) {
            // La configuration vient d'arriver : les caches batis sur l'ancienne
            // (vide) n'ont plus de sens.
            m_cSystemd.valid = false;
            m_cProbes.valid = false;
        }
    }

    if (!isFresh(m_cSystemd, m_config.systemdRefreshMs())) {
        m_cSystemd.value = m_supervisor ? m_supervisor->collectSystemd() : QJsonObject{};
        m_cSystemd.age.restart();
        m_cSystemd.valid = true;
    }
    o["systemd"] = m_cSystemd.value.value(QStringLiteral("services"));

    if (!isFresh(m_cProbes, m_config.probesRefreshMs())) {
        m_cProbes.value = m_supervisor ? m_supervisor->collectProbes(uptimeSeconds())
                                       : QJsonObject{};
        m_cProbes.age.restart();
        m_cProbes.valid = true;
    }
    o["network"] = m_cProbes.value.value(QStringLiteral("probes"));
    o["network_grace"] = m_cProbes.value.value(QStringLiteral("grace"));

    const QJsonObject beacon = beaconAppsJson();
    o["beacon"] = beacon.value(QStringLiteral("apps"));
    o["beacon_offline_after_s"] = beacon.value(QStringLiteral("offline_after_s"));

    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

QJsonObject MonitorModule::rebootJson() {
    return m_reboot.detect();
}

QJsonObject MonitorModule::allJson() {
    QJsonObject o;
    o["system"]    = systemJson();
    o["resources"] = resourcesJson();
    o["network"]   = networkJson();
    o["services"]  = servicesJson();
    o["reboot"]    = rebootJson();
    o["ts"]        = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

QJsonObject MonitorModule::statusJson() const {
    QJsonObject o;
    o["running"]       = m_running;
    o["config_loaded"] = m_config.isLoaded();
    o["config_path"]   = m_config.loadedPath();
    if (!m_config.isLoaded())
        o["config_error"] = m_config.lastError();
    o["supervised"] = QJsonObject{
        {"systemd", m_config.systemdServices().size()},
        {"network", m_config.networkServices().size()},
        {"beacon",  m_config.beaconApps().size()}};
    o["beacon_heard"] = m_beaconSeen.size();
    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

} // namespace morfmonitor
