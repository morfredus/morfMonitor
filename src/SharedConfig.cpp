/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/SharedConfig.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QProcessEnvironment>

namespace morfmonitor {

QStringList SharedConfig::searchPaths() {
    QStringList paths;
    const QString env = QProcessEnvironment::systemEnvironment()
                            .value(QStringLiteral("MORFSYSTEM_CONFIG"));
    if (!env.isEmpty())
        paths << env;
    const QString programData = QProcessEnvironment::systemEnvironment()
                                    .value(QStringLiteral("ProgramData"));
    if (!programData.isEmpty())
        paths << QDir(programData).filePath(QStringLiteral("morfsystem/morfsystem.json"));
    paths << QStringLiteral("/etc/morfsystem/morfsystem.json")
          << QStringLiteral("morfsystem.json");
    return paths;
}

bool SharedConfig::load(const QString& path) {
    const QStringList candidates = path.isEmpty() ? searchPaths() : QStringList{path};

    QByteArray raw;
    QString chosen;
    for (const QString& p : candidates) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly)) {
            raw = f.readAll();
            chosen = p;
            break;
        }
    }
    if (chosen.isEmpty()) {
        m_lastError = QStringLiteral("aucune configuration trouvée (%1)")
                          .arg(candidates.join(QStringLiteral(", ")));
        return false;
    }

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        // On garde les valeurs par defaut : mieux vaut un service qui repond en
        // ne supervisant rien qu'un service de supervision qui refuse de demarrer.
        m_lastError = QStringLiteral("%1 : JSON invalide (%2)")
                          .arg(chosen, pe.errorString());
        return false;
    }

    const QJsonObject root = doc.object();

    const QJsonObject refresh = root.value(QStringLiteral("refresh")).toObject();
    m_resourcesMs = refresh.value(QStringLiteral("resources_ms")).toInt(m_resourcesMs);
    m_networkMs   = refresh.value(QStringLiteral("network_ms")).toInt(m_networkMs);
    m_systemdMs   = refresh.value(QStringLiteral("systemd_ms")).toInt(m_systemdMs);
    m_probesMs    = refresh.value(QStringLiteral("probes_ms")).toInt(m_probesMs);

    m_systemd.clear();
    for (const QJsonValue& v : root.value(QStringLiteral("systemd_services")).toArray()) {
        const QJsonObject o = v.toObject();
        SystemdServiceDef d;
        d.unit      = o.value(QStringLiteral("unit")).toString();
        d.label     = o.value(QStringLiteral("label")).toString(d.unit);
        d.enabled   = o.value(QStringLiteral("enabled")).toBool(true);
        d.repo      = o.value(QStringLiteral("repo")).toString();
        d.repoOwner = o.value(QStringLiteral("owner")).toString(QStringLiteral("morfredus"));
        d.app       = o.value(QStringLiteral("app")).toString(d.label);   // defaut = label
        if (!d.unit.isEmpty())
            m_systemd.push_back(d);
    }

    m_network.clear();
    for (const QJsonValue& v : root.value(QStringLiteral("network_services")).toArray()) {
        const QJsonObject o = v.toObject();
        NetworkServiceDef d;
        d.name      = o.value(QStringLiteral("name")).toString();
        d.label     = o.value(QStringLiteral("label")).toString(d.name);
        d.host      = o.value(QStringLiteral("host")).toString();
        d.port      = o.value(QStringLiteral("port")).toInt(80);
        d.timeoutMs = o.value(QStringLiteral("timeout_ms")).toInt(1000);
        d.enabled   = o.value(QStringLiteral("enabled")).toBool(true);
        if (!d.name.isEmpty() && !d.host.isEmpty())
            m_network.push_back(d);
    }

    m_beaconApps.clear();
    for (const QJsonValue& v : root.value(QStringLiteral("beacon_apps")).toArray()) {
        const QJsonObject o = v.toObject();
        BeaconAppDef d;
        d.app     = o.value(QStringLiteral("app")).toString();
        d.label   = o.value(QStringLiteral("label")).toString(d.app);
        d.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        // Machine attendue (facultative) : contrainte de placement si presente.
        d.host    = o.value(QStringLiteral("host")).toString();
        if (!d.app.isEmpty())
            m_beaconApps.push_back(d);
    }

    m_ecosystem.clear();
    for (const QJsonValue& v : root.value(QStringLiteral("ecosystem_projects")).toArray()) {
        const QJsonObject o = v.toObject();
        EcosystemProjectDef d;
        d.label     = o.value(QStringLiteral("label")).toString();
        d.repo      = o.value(QStringLiteral("repo")).toString(d.label);
        d.repoOwner = o.value(QStringLiteral("owner")).toString(QStringLiteral("morfredus"));
        d.kind      = o.value(QStringLiteral("kind")).toString(QStringLiteral("tool"));
        d.local     = o.value(QStringLiteral("local")).toString(QStringLiteral("clone"));
        if (!d.label.isEmpty() && !d.repo.isEmpty())
            m_ecosystem.push_back(d);
    }

    m_probeGraceS = root.value(QStringLiteral("network_probe_grace_s")).toInt(m_probeGraceS);

    const QJsonObject beacon = root.value(QStringLiteral("beacon")).toObject();
    m_beaconPort     = static_cast<quint16>(beacon.value(QStringLiteral("port")).toInt(m_beaconPort));
    m_beaconOfflineS = beacon.value(QStringLiteral("offline_after_s")).toInt(m_beaconOfflineS);
    // Archivage des machines connues : regle en JOURS dans le fichier, converti en
    // secondes. Absent => defaut (30 j). 0 => archivage automatique desactive.
    if (beacon.contains(QStringLiteral("archive_after_days"))) {
        const qint64 days = static_cast<qint64>(beacon.value(QStringLiteral("archive_after_days")).toInt(30));
        m_machineArchiveS = days > 0 ? days * 24 * 3600 : 0;
    }

    m_loaded = true;
    m_loadedPath = chosen;
    m_lastError.clear();
    return true;
}

QJsonObject SharedConfig::toJson() const {
    QJsonObject o;
    o["loaded"] = m_loaded;
    o["path"]   = m_loadedPath;
    if (!m_lastError.isEmpty())
        o["error"] = m_lastError;

    QJsonArray systemd;
    for (const SystemdServiceDef& d : m_systemd) {
        systemd.append(QJsonObject{{"unit", d.unit}, {"label", d.label}, {"enabled", d.enabled}});
    }
    o["systemd_services"] = systemd;

    QJsonArray network;
    for (const NetworkServiceDef& d : m_network) {
        network.append(QJsonObject{{"name", d.name}, {"label", d.label}, {"host", d.host},
                                   {"port", d.port}, {"timeout_ms", d.timeoutMs},
                                   {"enabled", d.enabled}});
    }
    o["network_services"] = network;

    QJsonArray apps;
    for (const BeaconAppDef& d : m_beaconApps) {
        QJsonObject a{{"app", d.app}, {"label", d.label}, {"enabled", d.enabled}};
        // N'expose « host » que s'il est renseigne : une attente de presence
        // (host vide) ne doit pas se lire comme une attente de placement sur « ».
        if (!d.host.isEmpty())
            a["host"] = d.host;
        apps.append(a);
    }
    o["beacon_apps"] = apps;

    QJsonArray eco;
    for (const EcosystemProjectDef& d : m_ecosystem) {
        eco.append(QJsonObject{{"label", d.label}, {"repo", d.repo},
                               {"owner", d.repoOwner}, {"kind", d.kind},
                               {"local", d.local}});
    }
    o["ecosystem_projects"] = eco;

    o["beacon"] = QJsonObject{{"port", m_beaconPort}, {"offline_after_s", m_beaconOfflineS},
                              {"archive_after_days", static_cast<double>(m_machineArchiveS / (24 * 3600))}};
    o["network_probe_grace_s"] = m_probeGraceS;
    o["refresh"] = QJsonObject{{"resources_ms", m_resourcesMs}, {"network_ms", m_networkMs},
                               {"systemd_ms", m_systemdMs}, {"probes_ms", m_probesMs}};
    return o;
}

} // namespace morfmonitor
