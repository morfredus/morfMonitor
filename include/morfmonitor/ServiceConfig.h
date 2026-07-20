/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QtGlobal>

namespace morfmonitor {

// -----------------------------------------------------------------------------
// ModuleDef : declaration d'UN module a activer (lue depuis la config JSON).
//   type   : identifiant de fabrique ("example", ...) -> ModuleFactory
//   id     : identifiant unique dans le service
//   params : objet JSON libre, propre au type (>>> vos parametres metier <<<)
// -----------------------------------------------------------------------------
struct ModuleDef {
    QString     id;
    QString     type;
    QJsonObject params;
};

// -----------------------------------------------------------------------------
// ServiceConfig : configuration complete du service. Charge depuis un fichier
// JSON (voir config/morfmonitor.example.json).
//
// >>> A ADAPTER : ajoutez ici les reglages globaux propres a votre service. <<<
// -----------------------------------------------------------------------------
struct ServiceConfig {
    QString appName    = QStringLiteral("morfMonitor");
    QString instanceId;                              // defaut = appName@hostname

    // 8790 : port attribue a morfMonitor dans le parc. 8799 appartient a
    // morfAnalytics — le clone du modele en avait herite, ce qui empechait
    // les deux services de tourner ensemble.
    quint16 httpPort    = 8790;                      // 0 => pas de serveur HTTP
    QString bindAddress = QStringLiteral("0.0.0.0");

    // Annonce de presence sur le LAN via morfBeacon.
    bool    beaconEnabled    = true;
    quint16 beaconUdpPort    = 45454;                // port du parc morfSystem
    int     beaconIntervalMs = 15000;

    // Modules a activer.
    QVector<ModuleDef> modules;

    static ServiceConfig fromJson(const QJsonObject& root) {
        ServiceConfig c;
        if (root.contains("app_name"))     c.appName     = root.value("app_name").toString(c.appName);
        if (root.contains("instance_id"))  c.instanceId  = root.value("instance_id").toString();
        if (root.contains("http_port"))    c.httpPort    = static_cast<quint16>(root.value("http_port").toInt(c.httpPort));
        if (root.contains("bind_address")) c.bindAddress = root.value("bind_address").toString(c.bindAddress);

        const QJsonObject beacon = root.value("beacon").toObject();
        if (beacon.contains("enabled"))     c.beaconEnabled    = beacon.value("enabled").toBool(c.beaconEnabled);
        if (beacon.contains("udp_port"))    c.beaconUdpPort    = static_cast<quint16>(beacon.value("udp_port").toInt(c.beaconUdpPort));
        if (beacon.contains("interval_ms")) c.beaconIntervalMs = beacon.value("interval_ms").toInt(c.beaconIntervalMs);

        const QJsonArray modules = root.value("modules").toArray();
        for (const QJsonValue& v : modules) {
            const QJsonObject o = v.toObject();
            const QString type = o.value("type").toString();
            if (type.isEmpty())
                continue;
            ModuleDef d;
            d.type   = type;
            d.id     = o.value("id").toString(type);
            d.params = o;
            c.modules.push_back(d);
        }
        return c;
    }
};

} // namespace morfmonitor
