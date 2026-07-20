/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QJsonObject>
#include <QString>
#include <QElapsedTimer>
#include <QHash>
#include "morfmonitor/SharedConfig.h"

namespace morfmonitor {

// -----------------------------------------------------------------------------
// Les collecteurs : la seule partie du parc qui parle au systeme d'exploitation.
//
// L'objectif de morfMonitor est que plus AUCUNE autre application n'aille lire
// /proc, appeler systemctl ou sonder le reseau elle-meme. Toute cette
// connaissance est concentree ici, et le reste de l'ecosysteme consomme du JSON.
//
// Chaque collecteur suit la meme regle : ne jamais echouer bruyamment. Une
// donnee indisponible (fichier absent, commande manquante, capteur absent) est
// simplement omise ou marquee, et le reste continue d'etre servi. Un superviseur
// qui s'arrete parce qu'un thermometre manque ne supervise plus rien.
// -----------------------------------------------------------------------------

// --- Informations figees : lues une fois, elles ne changent pas ---------------
class SystemCollector {
public:
    // `boot` inclus car il derive de l'uptime, qui bouge : recalcule a chaque appel.
    QJsonObject collect();

private:
    // Le nom d'hote, l'OS, le noyau et l'architecture ne changent pas en cours
    // d'execution : les relire a chaque requete serait du gaspillage.
    QJsonObject m_static;
    bool m_staticDone = false;
};

// --- Ressources : ce qui bouge en permanence ---------------------------------
class ResourceCollector {
public:
    QJsonObject collect();

private:
    // Le pourcentage CPU n'est PAS lisible directement : /proc/stat donne des
    // compteurs cumules depuis le demarrage. Le taux se deduit de la difference
    // entre deux lectures, d'ou la memorisation de la precedente.
    quint64 m_prevIdle = 0;
    quint64 m_prevTotal = 0;
    bool    m_havePrev = false;
};

class NetworkCollector {
public:
    QJsonObject collect();
};

// --- Supervision : etat des composants declares dans la configuration --------
class Supervisor {
public:
    explicit Supervisor(const SharedConfig* config) : m_config(config) {}

    QJsonObject collectSystemd() const;

    // Sonde TCP. Lente par nature (resolution mDNS + connexion), donc appelee a
    // une cadence bien plus basse que les ressources.
    QJsonObject collectProbes(qint64 uptimeSeconds) const;

private:
    const SharedConfig* m_config;
};

// --- Cause du dernier redemarrage --------------------------------------------
//
// Tous les redemarrages ne se valent pas : une coupure de courant n'appelle pas
// la meme reaction qu'une mise a jour. Aujourd'hui toutes produisent la meme
// notification, ce qui rend l'information inexploitable.
//
// La determination est FAILLIBLE par nature : il n'existe aucune source unique
// et fiable. On croise plusieurs indices, et quand rien ne tranche, on le dit —
// « cause inconnue » est une reponse honnete, contrairement a un « demarrage
// normal » affirme par defaut, qui masquerait une coupure.
class RebootCauseDetector {
public:
    // Determine une fois au demarrage du service : la cause ne change pas
    // pendant la vie du processus.
    QJsonObject detect();

private:
    QJsonObject m_cached;
    bool m_done = false;
};

} // namespace morfmonitor
