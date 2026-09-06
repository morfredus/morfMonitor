/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QStringList>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

namespace morfmonitor {

// -----------------------------------------------------------------------------
// EventMemory : la memoire temporelle de morfMonitor (contrats morfevent/1 et
// morfhistory/1). Voir la note de conception
//   .morfredus_travail/Evolution/morfMonitor - memoire temporelle (...).md
//
// Responsabilite, en une phrase : conserver la memoire des evenements que
// morfMonitor OBSERVE deja (elle n'ouvre AUCUNE sonde nouvelle), sous trois
// formes de profondeur decroissante :
//   - EVENEMENTS bruts : les faits dates, gardes 24-48 h (FIFO), pour la
//     chronologie recente ;
//   - EPISODES : une periode continue d'indisponibilite d'un service ; c'est
//     l'EPISODE qui porte la duree (le downtime), jamais la somme des types
//     d'evenements -> un crash suivi d'une perte de heartbeat = UN incident ;
//   - STATISTIQUES : agregats par JOUR (permanents), d'ou se DERIVE la table de
//     VIE (totaux depuis le debut). Le jour est la seule source de verite
//     durable ; life se recalcule a partir de lui.
//
// morfMonitor OBSERVE : cette classe ne controle rien et ne depend d'aucun autre
// service. morfAnalytics la LIRA (pull) via l'API morfhistory/1 ; son absence ne
// retire que l'analyse, jamais la memoire.
//
// Choix de conception majeurs (traces dans la note) :
//   - Le downtime est CREDITE AU FIL DE L'EAU : a chaque tick, la tranche de
//     temps ecoulee est imputee au jour courant (coupee a minuit). Les episodes
//     a cheval sur plusieurs jours sont ainsi geres sans appariement complexe, et
//     un tick anormalement long (morfMonitor endormi) devient du temps NON
//     OBSERVE, pas du downtime -- l'honnetete de la disponibilite en depend.
//   - Ecriture atomique (QSaveFile) pour les agregats, append pour le brut,
//     exactement comme MachineRegistry. Un stateDir absent desactive la
//     persistance sans jamais faire echouer le service.
// -----------------------------------------------------------------------------
class EventMemory {
public:
    // Instantane d'UN service, tel que morfMonitor le calcule DEJA a chaque tick
    // (croisement beacon + systemd). EventMemory ne lit rien de neuf : elle recoit
    // ce snapshot et en DEDUIT les transitions. C'est le sens de « aucune nouvelle
    // source d'observation ».
    struct Observation {
        QString instance;          // identite « app@host »
        QString service;           // nom d'application
        QString host;
        bool    declared        = false;  // service ATTENDU (seul un attendu fait incident)
        bool    hostOnline      = false;  // sa machine est-elle en ligne ?
        bool    heartbeatOnline = false;  // heartbeat frais (< offline_after_s) ?
        qint64  lastSeen        = 0;      // secs : dernier heartbeat (antidatage du silence)
        bool    hasLifecycle    = false;  // dispose-t-on de l'etat systemd (hote local) ?
        bool    systemdActive   = false;  // ActiveState == active
        QString systemdState;             // active / failed / inactive / ...
        bool    stuck           = false;  // actif mais muet (panne fonctionnelle)
        qint64  nRestarts       = -1;     // NRestarts systemd (-1 = inconnu)
    };

    // Charge l'etat existant sous <stateDir>/memory/ et effectue le rattrapage de
    // demarrage (monitor_started, monitor_gap si un trou d'observation est detecte,
    // purge du brut > 48 h). offlineAfterS n'est conserve que pour information.
    void load(const QString& stateDir, int offlineAfterS);

    // Persiste ce qui a change (agregats + curseur + episodes ouverts). A appeler
    // a l'arret et de loin en loin. Sans effet si rien n'a bouge.
    void flush();

    // A appeler a chaque tick avec l'instantane COMPLET du parc. Credite la tranche
    // de temps ecoulee depuis le tick precedent, puis applique les transitions
    // d'etat (ouverture / enrichissement / fermeture d'episodes, emission des
    // evenements). nowSec = horloge Unix (secondes).
    void observe(const QVector<Observation>& snapshot, qint64 nowSec);

    // --- API morfhistory/1 (lecture seule) ----------------------------------
    // Ruban brut sur une fenetre (chronologie 24 h). service vide = tous.
    QJsonObject eventsJson(qint64 sinceSec, qint64 untilSec, const QString& service) const;
    // Statistiques journalieres, bornes « yyyy-MM-dd » incluses (vides = tout).
    QJsonObject dailyJson(const QString& fromDay, const QString& toDay,
                          const QString& service) const;
    // Table de vie : totaux depuis le debut, DERIVES des jours + premiere/derniere
    // apparition persistees.
    QJsonObject lifeJson() const;

    // Petit resume pour le /status de morfMonitor (metrics). Non couteux.
    QJsonObject summaryJson() const;

private:
    // --- Journal brut (NDJSON append + ruban en memoire) --------------------
    void appendEvent(qint64 ts, const QString& service, const QString& host,
                     const QString& instance, const QString& event,
                     const QString& severity, const QString& source,
                     const QString& stateFrom, const QString& stateTo,
                     const QString& episodeId, const QJsonObject& data = {});
    void loadRawWindow(qint64 nowSec);
    void pruneRaw(qint64 nowSec);     // supprime les fichiers raw-*.ndjson > 48 h
    void pruneRingMemory(qint64 nowSec);

    // --- Etat compose + machine a etats -------------------------------------
    // Renvoie l'un de : "available", "crash", "stuck", "silent", "stopped",
    // "host_offline". Les trois etats problematiques (crash/stuck/silent) ouvrent
    // un episode ; "stopped" (arret propre) et "host_offline" (machine eteinte)
    // n'en ouvrent pas.
    static QString compositeState(const Observation& o);
    static bool    isUnavailable(const QString& state);   // crash/stuck/silent
    static int     causeRank(const QString& cause);       // crash>stuck>silent

    void applyTransition(const Observation& o, qint64 nowSec);

    // --- Credit du temps (au fil de l'eau, coupe a minuit) ------------------
    void creditObserved(qint64 fromSec, qint64 toSec);
    void creditDowntime(const QString& instance, const QString& cause,
                        qint64 fromSec, qint64 toSec);

    // --- Consolidation en memoire (jour = source de verite) -----------------
    struct DayServiceStat {
        // Compteurs d'EVENEMENTS (tallies bruts, informatifs).
        int starts = 0, stops = 0, crashes = 0;
        int restarts = 0, restartSuccess = 0, restartFailed = 0;
        int heartbeatLosses = 0, stuckSignals = 0;
        // Metriques d'EPISODES (les vraies statistiques d'incident).
        int    incidents = 0, incCrash = 0, incStuck = 0, incSilent = 0;
        double downtime = 0, dtCrash = 0, dtStuck = 0, dtSilent = 0;
        double longestEpisode = 0;
    };
    struct DayStat {
        double observedSeconds = 0;                 // temps reellement observe ce jour
        QHash<QString, DayServiceStat> services;    // clef = instance
    };
    DayServiceStat& dayService(const QString& day, const QString& instance);

    // --- Episodes ouverts (persistes hors du brut volatil) ------------------
    struct Episode {
        QString id;
        QString instance, service, host;
        qint64  openedAt = 0;
        QString cause;                 // crash / stuck / silent
        QStringList signalEvents;      // types d'evenements vus (le mot « signals » est reserve par Qt)
        int     restartAttempts = 0;
        QString restartOutcome;        // "" / succeeded / failed
        double  accrued = 0;           // downtime deja credite (pour longest_episode)
    };
    QString newEpisodeId();
    void openEpisode(const Observation& o, const QString& cause,
                     const QString& signalEvent, qint64 openedAt);
    void enrichEpisode(const Observation& o, const QString& newCause,
                       const QString& signalEvent, qint64 nowSec);
    void closeEpisode(const QString& instance, qint64 closedAt);

    // --- Persistance ---------------------------------------------------------
    QString monthPath(const QString& day) const;   // daily/YYYY-MM.json
    void loadDaily();
    void saveDirtyMonths();
    void loadOpenEpisodes();
    void saveOpenEpisodes();
    void loadCursor();
    void saveCursor();
    void maybePersist(qint64 nowSec);               // espace les ecritures disque

    static QString dayKeyOf(qint64 sec);            // "yyyy-MM-dd" (heure locale)
    static qint64  startOfDaySec(qint64 sec);       // minuit local <= sec

    // --- Membres -------------------------------------------------------------
    QString m_dir;                 // <stateDir>/memory (vide => persistance off)
    int     m_offlineAfterS = 30;

    // Ruban brut en memoire (24-48 h) pour servir /api/events sans relire le disque.
    QList<QJsonObject> m_ring;

    // Suivi par instance (en memoire) pour detecter les transitions.
    struct Tracked {
        QString state;             // dernier etat compose connu ("" = jamais vu)
        qint64  nRestarts = -1;    // dernier NRestarts vu (detection des relances)
        QString openEpisodeId;     // episode en cours pour cette instance ("" = aucun)
    };
    QHash<QString, Tracked> m_tracked;

    QHash<QString, Episode> m_openEpisodes;   // clef = instance

    // Statistiques journalieres : source de verite durable. QMap = clefs ordonnees.
    QMap<QString, DayStat> m_days;
    QSet<QString>          m_dirtyMonths;     // "YYYY-MM" a reecrire

    // Vie : on ne persiste que premiere/derniere apparition ; tout le reste se
    // derive de m_days au moment de la requete.
    QHash<QString, qint64> m_firstSeen;       // clef = instance
    QHash<QString, qint64> m_lastEvent;       // clef = instance
    bool m_lifeDirty = false;

    // Curseur : sequence des evenements + dernier tick (pour credit et detection
    // de trou d'observation).
    qint64 m_seq         = 0;
    qint64 m_lastTickSec = 0;
    bool   m_cursorDirty = false;
    qint64 m_lastSaveSec = 0;

    // Un tick plus long que ceci = trou d'observation (morfMonitor endormi/arrete) :
    // on n'en credite NI observe NI downtime, et on pose un monitor_gap. L'evaluation
    // nominale tombe toutes les 30 s ; 90 s laisse passer un retard normal.
    static constexpr qint64 kMaxTickGapS   = 90;
    // Espacement des ecritures disque des agregats (usure SD du Raspberry Pi).
    static constexpr qint64 kMinPersistS   = 60;
    // Borne de securite du ruban en memoire.
    static constexpr int    kMaxRingEvents = 200000;
};

} // namespace morfmonitor
