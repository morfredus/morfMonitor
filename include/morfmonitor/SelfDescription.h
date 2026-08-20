/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfbeacon/PresenceConfig.h"

namespace morfmonitor {

// -----------------------------------------------------------------------------
// fillAnnouncedDetail : renseigne le DETAIL annonce du service -- interface web
// et liste d'API -- dans un PresenceConfig.
//
// Point UNIQUE, appele des deux cotes :
//   - Service.cpp, en construisant la config du heartbeat morfBeacon ;
//   - HttpServer::buildStatusJson, en servant /status (via describeService).
//
// Avant ce point unique, le bloc web_ui etait ecrit A LA MAIN aux deux endroits,
// mots pour mots : une modification d'un cote pouvait diverger de l'autre. Ici,
// l'interface et l'API qu'un observateur decouvre sont definies une seule fois.
//
// `webEnabled` conditionne l'interface web : declarer une interface desactivee
// produirait un lien mort. L'API, elle, est toujours servie.
//
// En-tete (inline) : aucun fichier source ni entree CMake supplementaires.
inline void fillAnnouncedDetail(morfbeacon::PresenceConfig& pc, bool webEnabled) {
    if (webEnabled) {
        pc.webUiPath        = QStringLiteral("/");
        pc.webUiLabel       = QStringLiteral("Supervision");
        pc.webUiDescription = QStringLiteral(
            "Etat de la machine et des services morfSystem.");
    }

    // morfMonitor exposes observations. The sole write route only forwards an
    // explicit local request to the dedicated update agent; it never installs.
    pc.apiBasePath = QStringLiteral("/api");
    pc.api = {
        {QStringLiteral("GET"), QStringLiteral("/api/system"),
         QStringLiteral("identite, OS, noyau, uptime de la machine")},
        {QStringLiteral("GET"), QStringLiteral("/api/resources"),
         QStringLiteral("CPU, memoire, stockage, temperatures, bridage")},
        {QStringLiteral("GET"), QStringLiteral("/api/network"),
         QStringLiteral("interfaces, IPv4/IPv6, MAC, etat du lien")},
        {QStringLiteral("GET"), QStringLiteral("/api/services"),
         QStringLiteral("services systemd, sondes reseau, applications decouvertes")},
        {QStringLiteral("GET"), QStringLiteral("/api/reboot"),
         QStringLiteral("cause du dernier redemarrage")},
        {QStringLiteral("GET"), QStringLiteral("/api/config"),
         QStringLiteral("configuration effective supervisee")},
        {QStringLiteral("GET"), QStringLiteral("/api/all"),
         QStringLiteral("tout l'etat en une seule requete")},
        {QStringLiteral("POST"), QStringLiteral("/api/updates"),
         QStringLiteral("demande locale deleguee a morfUpdate, jamais une installation directe")},
    };
}

} // namespace morfmonitor
