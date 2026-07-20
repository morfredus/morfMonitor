/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/ModuleFactory.h"
#include "morfmonitor/IModule.h"
#include "morfmonitor/MonitorModule.h"   // <-- remplacer/completer par vos modules

namespace morfmonitor {
namespace ModuleFactory {

// -----------------------------------------------------------------------------
// POUR AJOUTER UN MODULE METIER :
//   1. ecrire la classe (heriter d'IModule) ;
//   2. ajouter une branche dans create() qui lit ses parametres (def.params) ;
//   3. ajouter son nom dans knownTypes().
// Aucune autre partie du code (registre, serveur HTTP, service) ne change.
// -----------------------------------------------------------------------------

IModule* create(const ModuleDef& def, QString* error, QObject* parent) {
    const QString type = def.type.toLower();

    if (type == QLatin1String("monitor")) {
        // « config_path » est optionnel : sans lui, la configuration partagee
        // est cherchee aux emplacements standards (voir SharedConfig).
        const QString configPath = def.params.value("config_path").toString();
        return new MonitorModule(def.id, configPath, parent);
    }

    if (error)
        *error = QStringLiteral("type de module inconnu : '%1'").arg(def.type);
    return nullptr;
}

QStringList knownTypes() {
    return { QStringLiteral("monitor") };
}

} // namespace ModuleFactory
} // namespace morfmonitor
