/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/MachineRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace morfmonitor {

void MachineRegistry::load(const QString& stateDir) {
    m_machines.clear();
    m_dirty = false;

    if (stateDir.isEmpty()) {
        m_path.clear();   // persistance desactivee : registre en memoire seule
        return;
    }

    // Cree le dossier d'etat au besoin. En cas d'echec (droits, disque), on
    // continue sans persistance plutot que de refuser de superviser.
    QDir().mkpath(stateDir);
    m_path = QDir(stateDir).filePath(QStringLiteral("known-machines.json"));

    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;   // premier demarrage, ou fichier illisible : on repart a vide

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray arr = root.value(QStringLiteral("machines")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        const QString host = o.value(QStringLiteral("host")).toString();
        if (host.isEmpty())
            continue;
        Machine m;
        m.firstSeen = static_cast<qint64>(o.value(QStringLiteral("first_seen")).toDouble());
        m.lastSeen  = static_cast<qint64>(o.value(QStringLiteral("last_seen")).toDouble());
        m_machines.insert(host, m);
    }
}

bool MachineRegistry::observe(const QString& host, qint64 nowSec) {
    if (host.isEmpty())
        return false;

    const auto it = m_machines.find(host);
    if (it == m_machines.end()) {
        // Machine inconnue : on l'apprend. Evenement rare et important -> on
        // persiste aussitot, pour qu'une decouverte survive a un arret immediat.
        Machine m;
        m.firstSeen = nowSec;
        m.lastSeen  = nowSec;
        m_machines.insert(host, m);
        m_dirty = true;
        save();
        return true;
    }

    // Machine deja connue : on rafraichit sa derniere annonce. On ne persiste pas
    // a chaque heartbeat (usure SD) ; save() n'ecrira qu'au plus une fois par
    // kMinSaveIntervalS.
    it->lastSeen = nowSec;
    m_dirty = true;
    if (nowSec - m_lastSaveSec >= kMinSaveIntervalS)
        save();
    return false;
}

bool MachineRegistry::forget(const QString& host) {
    if (m_machines.remove(host) == 0)
        return false;
    m_dirty = true;
    save();   // geste explicite de l'utilisateur : on materialise sans attendre
    return true;
}

QJsonArray MachineRegistry::machinesJson(qint64 nowSec, int offlineAfterS,
                                         qint64 archiveAfterS) const {
    QJsonArray arr;
    for (auto it = m_machines.constBegin(); it != m_machines.constEnd(); ++it) {
        const qint64 age = nowSec - it->lastSeen;
        const bool online = age < offlineAfterS;

        QString state;
        if (online)
            state = QStringLiteral("active");
        else if (archiveAfterS > 0 && age >= archiveAfterS)
            state = QStringLiteral("archived");
        else
            state = QStringLiteral("offline");

        QJsonObject o;
        o["host"]        = it.key();
        o["state"]       = state;
        o["online"]      = online;
        o["first_seen"]  = static_cast<double>(it->firstSeen);
        o["last_seen"]   = static_cast<double>(it->lastSeen);
        o["last_seen_s"] = static_cast<double>(age);
        arr.append(o);
    }
    return arr;
}

void MachineRegistry::flush() {
    if (m_dirty)
        save();
}

void MachineRegistry::save() {
    if (m_path.isEmpty())
        return;   // persistance desactivee

    QJsonArray arr;
    for (auto it = m_machines.constBegin(); it != m_machines.constEnd(); ++it) {
        QJsonObject o;
        o["host"]       = it.key();
        o["first_seen"] = static_cast<double>(it->firstSeen);
        o["last_seen"]  = static_cast<double>(it->lastSeen);
        arr.append(o);
    }
    QJsonObject root;
    root["machines"] = arr;

    // Ecriture atomique : QSaveFile ecrit dans un temporaire puis renomme, si
    // bien qu'une coupure au mauvais moment ne laisse jamais un fichier a moitie
    // ecrit -- le registre precedent reste intact.
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;   // non inscriptible : on garde l'etat en memoire, sans echouer
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (f.commit()) {
        m_dirty = false;
        // Horodate le dernier enregistrement reussi a partir de la machine la plus
        // recemment vue : suffit a espacer les ecritures sans lire l'horloge ici.
        for (auto it = m_machines.constBegin(); it != m_machines.constEnd(); ++it)
            m_lastSaveSec = qMax(m_lastSaveSec, it->lastSeen);
    }
}

} // namespace morfmonitor
