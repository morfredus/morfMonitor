/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/EventMemory.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDateTime>
#include <QHostInfo>
#include <QRandomGenerator>

namespace morfmonitor {

namespace {
// Un tick « normal » (evaluation periodique de MonitorModule) tombe toutes les
// 30 s. Ce qui suit est purement local a ce fichier.
QString severityFor(const QString& event) {
    if (event == QLatin1String("service_crashed"))      return QStringLiteral("error");
    if (event == QLatin1String("service_stuck"))        return QStringLiteral("warning");
    if (event == QLatin1String("heartbeat_lost"))       return QStringLiteral("warning");
    if (event == QLatin1String("restart_failed"))       return QStringLiteral("error");
    if (event == QLatin1String("monitor_gap"))          return QStringLiteral("warning");
    return QStringLiteral("info");
}
} // namespace

// --- Helpers de calendrier (heure LOCALE : « le jour » de Fred) --------------

QString EventMemory::dayKeyOf(qint64 sec) {
    return QDateTime::fromSecsSinceEpoch(sec).date().toString(QStringLiteral("yyyy-MM-dd"));
}

qint64 EventMemory::startOfDaySec(qint64 sec) {
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(sec);
    return QDateTime(dt.date(), QTime(0, 0, 0)).toSecsSinceEpoch();
}

namespace {
// Minuit LOCAL strictement apres `sec` (calcul via addDays pour rester juste les
// jours de changement d'heure : un jour n'y fait pas exactement 86400 s).
qint64 nextMidnightSec(qint64 sec) {
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(sec);
    return QDateTime(dt.date().addDays(1), QTime(0, 0, 0)).toSecsSinceEpoch();
}
} // namespace

// --- Etat compose ------------------------------------------------------------

QString EventMemory::compositeState(const Observation& o) {
    // Machine eteinte : ce n'est pas une panne du service, on n'ouvre pas
    // d'incident par service (une panne machine est un autre sujet, hors v1).
    if (!o.hostOnline)
        return QStringLiteral("host_offline");
    // Crash : systemd declare l'unite en echec. Explication la plus forte.
    if (o.hasLifecycle && o.systemdState == QLatin1String("failed"))
        return QStringLiteral("crash");
    // Heartbeat frais = preuve de vie ET de sante : le service repond, tout va bien.
    if (o.heartbeatOnline)
        return QStringLiteral("available");
    // A partir d'ici, plus de heartbeat frais.
    if (o.hasLifecycle && o.systemdActive)
        return QStringLiteral("stuck");     // vivant (systemd) mais muet
    if (o.hasLifecycle && !o.systemdActive)
        return QStringLiteral("stopped");   // arret propre (inactive/dead)
    // Pas de preuve systemd (service distant, ou sans unite) et silence : silent.
    return QStringLiteral("silent");
}

bool EventMemory::isUnavailable(const QString& state) {
    return state == QLatin1String("crash")
        || state == QLatin1String("stuck")
        || state == QLatin1String("silent");
}

int EventMemory::causeRank(const QString& cause) {
    if (cause == QLatin1String("crash"))  return 3;
    if (cause == QLatin1String("stuck"))  return 2;
    if (cause == QLatin1String("silent")) return 1;
    return 0;
}

namespace {
QString signalEventFor(const QString& state) {
    if (state == QLatin1String("crash"))  return QStringLiteral("service_crashed");
    if (state == QLatin1String("stuck"))  return QStringLiteral("service_stuck");
    return QStringLiteral("heartbeat_lost");   // silent
}
} // namespace

// --- Chargement / persistance ------------------------------------------------

void EventMemory::load(const QString& stateDir, int offlineAfterS) {
    m_offlineAfterS = offlineAfterS;

    if (stateDir.isEmpty()) {
        m_dir.clear();   // persistance desactivee : memoire en RAM seule
    } else {
        m_dir = QDir(stateDir).filePath(QStringLiteral("memory"));
        // Echec de creation (droits, disque) : on continue sans persistance
        // plutot que de refuser de superviser.
        if (!QDir().mkpath(m_dir))
            m_dir.clear();
    }

    loadCursor();
    loadDaily();
    loadOpenEpisodes();

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    loadRawWindow(now);
    pruneRaw(now);

    // Trou d'observation : si le dernier tick connu est ancien, morfMonitor etait
    // eteint/endormi. On le rend visible plutot que de le masquer (sinon la
    // disponibilite mentirait). Puis on marque le (re)demarrage.
    const QString host = QHostInfo::localHostName();
    if (m_lastTickSec > 0 && (now - m_lastTickSec) > kMaxTickGapS) {
        QJsonObject data;
        data["from"] = static_cast<double>(m_lastTickSec);
        data["to"]   = static_cast<double>(now);
        data["gap_s"] = static_cast<double>(now - m_lastTickSec);
        appendEvent(now, QStringLiteral("morfMonitor"), host,
                    QStringLiteral("morfMonitor@") + host,
                    QStringLiteral("monitor_gap"), severityFor(QStringLiteral("monitor_gap")),
                    QStringLiteral("internal"), QString(), QString(), QString(), data);
    }
    appendEvent(now, QStringLiteral("morfMonitor"), host,
                QStringLiteral("morfMonitor@") + host,
                QStringLiteral("monitor_started"), QStringLiteral("info"),
                QStringLiteral("internal"), QString(), QString(), QString());

    // Le prochain observe() partira de ce tick : aucune tranche fantome n'est
    // creditee pour la periode ou morfMonitor ne tournait pas.
    m_lastTickSec = now;
    m_cursorDirty = true;
}

void EventMemory::flush() {
    saveDirtyMonths();
    saveOpenEpisodes();
    saveCursor();
    if (m_lifeDirty) {
        // life.json ne stocke QUE premiere/derniere apparition ; le reste se
        // derive des jours. On le reecrit avec le reste des agregats.
        if (!m_dir.isEmpty()) {
            QJsonObject firsts, lasts;
            for (auto it = m_firstSeen.constBegin(); it != m_firstSeen.constEnd(); ++it)
                firsts[it.key()] = static_cast<double>(it.value());
            for (auto it = m_lastEvent.constBegin(); it != m_lastEvent.constEnd(); ++it)
                lasts[it.key()] = static_cast<double>(it.value());
            QJsonObject root;
            root["first_seen"] = firsts;
            root["last_event"] = lasts;
            QSaveFile f(QDir(m_dir).filePath(QStringLiteral("life.json")));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
                if (f.commit())
                    m_lifeDirty = false;
            }
        } else {
            m_lifeDirty = false;
        }
    }
}

void EventMemory::maybePersist(qint64 nowSec) {
    if (nowSec - m_lastSaveSec < kMinPersistS)
        return;
    m_lastSaveSec = nowSec;
    flush();
}

void EventMemory::loadCursor() {
    m_seq = 0;
    m_lastTickSec = 0;
    if (m_dir.isEmpty())
        return;
    QFile f(QDir(m_dir).filePath(QStringLiteral("cursor.json")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_seq         = static_cast<qint64>(o.value(QStringLiteral("seq")).toDouble());
    m_lastTickSec = static_cast<qint64>(o.value(QStringLiteral("last_tick")).toDouble());
}

void EventMemory::saveCursor() {
    if (m_dir.isEmpty() || !m_cursorDirty)
        return;
    QJsonObject o;
    o["seq"]       = static_cast<double>(m_seq);
    o["last_tick"] = static_cast<double>(m_lastTickSec);
    QSaveFile f(QDir(m_dir).filePath(QStringLiteral("cursor.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    if (f.commit())
        m_cursorDirty = false;
}

QString EventMemory::monthPath(const QString& day) const {
    // day = "yyyy-MM-dd" -> daily/yyyy-MM.json
    return QDir(m_dir).filePath(QStringLiteral("daily/") + day.left(7) + QStringLiteral(".json"));
}

void EventMemory::loadDaily() {
    m_days.clear();
    m_dirtyMonths.clear();
    if (m_dir.isEmpty())
        return;
    QDir d(QDir(m_dir).filePath(QStringLiteral("daily")));
    if (!d.exists())
        return;
    const QStringList files = d.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString& name : files) {
        QFile f(d.filePath(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonObject days = root.value(QStringLiteral("days")).toObject();
        for (auto it = days.constBegin(); it != days.constEnd(); ++it) {
            const QJsonObject dj = it.value().toObject();
            DayStat ds;
            ds.observedSeconds = dj.value(QStringLiteral("observed_seconds")).toDouble();
            const QJsonObject svcs = dj.value(QStringLiteral("services")).toObject();
            for (auto s = svcs.constBegin(); s != svcs.constEnd(); ++s) {
                const QJsonObject ev = s.value().toObject().value(QStringLiteral("events")).toObject();
                const QJsonObject in = s.value().toObject().value(QStringLiteral("incidents")).toObject();
                const QJsonObject bc = in.value(QStringLiteral("by_cause")).toObject();
                const QJsonObject dc = in.value(QStringLiteral("downtime_by_cause")).toObject();
                DayServiceStat st;
                st.starts          = ev.value(QStringLiteral("starts")).toInt();
                st.stops           = ev.value(QStringLiteral("stops")).toInt();
                st.crashes         = ev.value(QStringLiteral("crashes")).toInt();
                st.restarts        = ev.value(QStringLiteral("restarts")).toInt();
                st.restartSuccess  = ev.value(QStringLiteral("restart_success")).toInt();
                st.restartFailed   = ev.value(QStringLiteral("restart_failed")).toInt();
                st.heartbeatLosses = ev.value(QStringLiteral("heartbeat_losses")).toInt();
                st.stuckSignals    = ev.value(QStringLiteral("stuck_signals")).toInt();
                st.incidents       = in.value(QStringLiteral("count")).toInt();
                st.incCrash        = bc.value(QStringLiteral("crash")).toInt();
                st.incStuck        = bc.value(QStringLiteral("stuck")).toInt();
                st.incSilent       = bc.value(QStringLiteral("silent")).toInt();
                st.downtime        = in.value(QStringLiteral("downtime_seconds")).toDouble();
                st.dtCrash         = dc.value(QStringLiteral("crash")).toDouble();
                st.dtStuck         = dc.value(QStringLiteral("stuck")).toDouble();
                st.dtSilent        = dc.value(QStringLiteral("silent")).toDouble();
                st.longestEpisode  = in.value(QStringLiteral("longest_episode_seconds")).toDouble();
                ds.services.insert(s.key(), st);
            }
            m_days.insert(it.key(), ds);
        }
    }

    // life.json : premiere/derniere apparition (le reste se derive des jours).
    QFile lf(QDir(m_dir).filePath(QStringLiteral("life.json")));
    if (lf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonObject root = QJsonDocument::fromJson(lf.readAll()).object();
        const QJsonObject firsts = root.value(QStringLiteral("first_seen")).toObject();
        const QJsonObject lasts  = root.value(QStringLiteral("last_event")).toObject();
        for (auto it = firsts.constBegin(); it != firsts.constEnd(); ++it)
            m_firstSeen.insert(it.key(), static_cast<qint64>(it.value().toDouble()));
        for (auto it = lasts.constBegin(); it != lasts.constEnd(); ++it)
            m_lastEvent.insert(it.key(), static_cast<qint64>(it.value().toDouble()));
    }
}

void EventMemory::saveDirtyMonths() {
    if (m_dir.isEmpty() || m_dirtyMonths.isEmpty())
        return;
    QDir().mkpath(QDir(m_dir).filePath(QStringLiteral("daily")));

    for (const QString& month : m_dirtyMonths) {
        QJsonObject days;
        for (auto it = m_days.constBegin(); it != m_days.constEnd(); ++it) {
            if (it.key().left(7) != month)
                continue;
            const DayStat& ds = it.value();
            QJsonObject svcs;
            for (auto s = ds.services.constBegin(); s != ds.services.constEnd(); ++s) {
                const DayServiceStat& st = s.value();
                QJsonObject ev{
                    {"starts", st.starts}, {"stops", st.stops}, {"crashes", st.crashes},
                    {"restarts", st.restarts}, {"restart_success", st.restartSuccess},
                    {"restart_failed", st.restartFailed},
                    {"heartbeat_losses", st.heartbeatLosses}, {"stuck_signals", st.stuckSignals}};
                QJsonObject in{
                    {"count", st.incidents},
                    {"by_cause", QJsonObject{{"crash", st.incCrash}, {"stuck", st.incStuck},
                                             {"silent", st.incSilent}}},
                    {"downtime_seconds", st.downtime},
                    {"downtime_by_cause", QJsonObject{{"crash", st.dtCrash}, {"stuck", st.dtStuck},
                                                      {"silent", st.dtSilent}}},
                    {"longest_episode_seconds", st.longestEpisode}};
                double avail = -1.0;
                if (ds.observedSeconds > 0)
                    avail = (ds.observedSeconds - st.downtime) / ds.observedSeconds;
                QJsonObject sj{{"events", ev}, {"incidents", in}};
                if (avail >= 0)
                    sj["availability"] = avail;
                svcs[s.key()] = sj;
            }
            QJsonObject dj;
            dj["observed_seconds"] = ds.observedSeconds;
            dj["services"] = svcs;
            days[it.key()] = dj;
        }
        QJsonObject root;
        root["days"] = days;
        QSaveFile f(monthPath(month + QStringLiteral("-01")));   // le jour n'importe pas
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            f.commit();
        }
    }
    m_dirtyMonths.clear();
}

void EventMemory::loadOpenEpisodes() {
    m_openEpisodes.clear();
    if (m_dir.isEmpty())
        return;
    QFile f(QDir(m_dir).filePath(QStringLiteral("open_episodes.json")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).object()
                               .value(QStringLiteral("episodes")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Episode e;
        e.id        = o.value(QStringLiteral("episode_id")).toString();
        e.instance  = o.value(QStringLiteral("instance")).toString();
        e.service   = o.value(QStringLiteral("service")).toString();
        e.host      = o.value(QStringLiteral("host")).toString();
        e.openedAt  = static_cast<qint64>(o.value(QStringLiteral("opened_at")).toDouble());
        e.cause     = o.value(QStringLiteral("cause")).toString();
        for (const QJsonValue& s : o.value(QStringLiteral("signals")).toArray())
            e.signalEvents << s.toString();
        e.restartAttempts = o.value(QStringLiteral("restart_attempts")).toInt();
        e.restartOutcome  = o.value(QStringLiteral("restart_outcome")).toString();
        e.accrued         = o.value(QStringLiteral("accrued_seconds")).toDouble();
        if (e.instance.isEmpty())
            continue;
        m_openEpisodes.insert(e.instance, e);
        // Reprend le suivi : sans cela, un service encore en panne au redemarrage
        // serait vu comme « nouveau » et on OUVRIRAIT un second episode en double.
        Tracked t;
        t.state         = e.cause;   // etat compose = la cause de l'episode ouvert
        t.openEpisodeId = e.id;
        t.nRestarts     = -1;        // inconnu : pas de fausse relance au 1er snapshot
        m_tracked.insert(e.instance, t);
    }
}

void EventMemory::saveOpenEpisodes() {
    if (m_dir.isEmpty())
        return;
    QJsonArray arr;
    for (auto it = m_openEpisodes.constBegin(); it != m_openEpisodes.constEnd(); ++it) {
        const Episode& e = it.value();
        QJsonObject o;
        o["episode_id"]       = e.id;
        o["instance"]         = e.instance;
        o["service"]          = e.service;
        o["host"]             = e.host;
        o["opened_at"]        = static_cast<double>(e.openedAt);
        o["cause"]            = e.cause;
        o["signals"]          = QJsonArray::fromStringList(e.signalEvents);
        o["restart_attempts"] = e.restartAttempts;
        o["restart_outcome"]  = e.restartOutcome;
        o["accrued_seconds"]  = e.accrued;
        arr.append(o);
    }
    QJsonObject root;
    root["episodes"] = arr;
    QSaveFile f(QDir(m_dir).filePath(QStringLiteral("open_episodes.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

// --- Journal brut ------------------------------------------------------------

void EventMemory::appendEvent(qint64 ts, const QString& service, const QString& host,
                              const QString& instance, const QString& event,
                              const QString& severity, const QString& source,
                              const QString& stateFrom, const QString& stateTo,
                              const QString& episodeId, const QJsonObject& data) {
    QJsonObject e;
    e["schema"]   = QStringLiteral("morfevent/1");
    e["ts"]       = static_cast<double>(ts);
    e["seq"]      = static_cast<double>(++m_seq);
    e["host"]     = host;
    e["service"]  = service;
    e["instance"] = instance;
    e["event"]    = event;
    e["severity"] = severity;
    e["source"]   = source;
    if (!stateFrom.isEmpty()) e["state_from"] = stateFrom;
    if (!stateTo.isEmpty())   e["state_to"]   = stateTo;
    if (!episodeId.isEmpty()) e["episode_id"] = episodeId;
    if (!data.isEmpty())      e["data"]       = data;
    m_cursorDirty = true;

    // Ruban en memoire (pour /api/events sans relecture disque).
    m_ring.append(e);
    if (m_ring.size() > kMaxRingEvents)
        m_ring.removeFirst();

    // Derniere apparition (alimente la table de vie).
    if (!instance.isEmpty()) {
        m_lastEvent[instance] = ts;
        m_lifeDirty = true;
    }

    // Append NDJSON : une ligne = un evenement. Une ligne tronquee par une coupure
    // se detecte et se jette a la relecture (JSON invalide), sans corrompre le reste.
    if (m_dir.isEmpty())
        return;
    QFile f(QDir(m_dir).filePath(QStringLiteral("raw-") + dayKeyOf(ts) + QStringLiteral(".ndjson")));
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(QJsonDocument(e).toJson(QJsonDocument::Compact));
        f.write("\n");
    }
}

void EventMemory::loadRawWindow(qint64 nowSec) {
    m_ring.clear();
    if (m_dir.isEmpty())
        return;
    // On relit aujourd'hui + la veille : couvre toujours >= 24 h glissantes.
    const QStringList days{ dayKeyOf(nowSec - 86400), dayKeyOf(nowSec) };
    for (const QString& day : days) {
        QFile f(QDir(m_dir).filePath(QStringLiteral("raw-") + day + QStringLiteral(".ndjson")));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        while (!f.atEnd()) {
            const QByteArray line = f.readLine().trimmed();
            if (line.isEmpty())
                continue;
            QJsonParseError pe{};
            const QJsonDocument doc = QJsonDocument::fromJson(line, &pe);
            if (pe.error != QJsonParseError::NoError || !doc.isObject())
                continue;   // ligne tronquee ou corrompue : ignoree
            m_ring.append(doc.object());
        }
    }
    pruneRingMemory(nowSec);
}

void EventMemory::pruneRingMemory(qint64 nowSec) {
    const qint64 cutoff = nowSec - 2 * 86400;   // 48 h de tampon (fenetre 24 h + marge)
    while (!m_ring.isEmpty()
           && static_cast<qint64>(m_ring.first().value(QStringLiteral("ts")).toDouble()) < cutoff)
        m_ring.removeFirst();
}

void EventMemory::pruneRaw(qint64 nowSec) {
    if (m_dir.isEmpty())
        return;
    const QString keepToday     = dayKeyOf(nowSec);
    const QString keepYesterday = dayKeyOf(nowSec - 86400);
    QDir d(m_dir);
    const QStringList files = d.entryList({QStringLiteral("raw-*.ndjson")}, QDir::Files);
    for (const QString& name : files) {
        // name = "raw-yyyy-MM-dd.ndjson"
        const QString day = name.mid(4, 10);
        if (day != keepToday && day != keepYesterday)
            d.remove(name);
    }
}

// --- Credit du temps (au fil de l'eau, coupe a minuit) -----------------------

EventMemory::DayServiceStat& EventMemory::dayService(const QString& day, const QString& instance) {
    DayStat& ds = m_days[day];
    m_dirtyMonths.insert(day.left(7));
    return ds.services[instance];
}

void EventMemory::creditObserved(qint64 fromSec, qint64 toSec) {
    qint64 cur = fromSec;
    while (cur < toSec) {
        const qint64 end = qMin(toSec, nextMidnightSec(cur));
        DayStat& ds = m_days[dayKeyOf(cur)];
        ds.observedSeconds += static_cast<double>(end - cur);
        m_dirtyMonths.insert(dayKeyOf(cur).left(7));
        cur = end;
    }
}

void EventMemory::creditDowntime(const QString& instance, const QString& cause,
                                 qint64 fromSec, qint64 toSec) {
    const auto ep = m_openEpisodes.find(instance);
    qint64 cur = fromSec;
    while (cur < toSec) {
        const qint64 end = qMin(toSec, nextMidnightSec(cur));
        const double slice = static_cast<double>(end - cur);
        DayServiceStat& st = dayService(dayKeyOf(cur), instance);
        st.downtime += slice;
        if (cause == QLatin1String("crash"))       st.dtCrash  += slice;
        else if (cause == QLatin1String("stuck"))  st.dtStuck  += slice;
        else                                       st.dtSilent += slice;
        if (ep != m_openEpisodes.end()) {
            ep->accrued += slice;
            // longest_episode : plus longue duree d'incident vue ce jour-la.
            if (ep->accrued > st.longestEpisode)
                st.longestEpisode = ep->accrued;
        }
        cur = end;
    }
}

// --- Episodes ----------------------------------------------------------------

QString EventMemory::newEpisodeId() {
    // Unicite parmi les episodes ouverts : horodatage ms + aleatoire court.
    return QString::number(QDateTime::currentMSecsSinceEpoch(), 16)
         + QLatin1Char('-')
         + QString::number(QRandomGenerator::global()->bounded(0x10000), 16);
}

void EventMemory::openEpisode(const Observation& o, const QString& cause,
                              const QString& signalEvent, qint64 openedAt) {
    Episode e;
    e.id       = newEpisodeId();
    e.instance = o.instance;
    e.service  = o.service;
    e.host     = o.host;
    e.openedAt = openedAt;
    e.cause    = cause;
    e.signalEvents << signalEvent;
    m_openEpisodes.insert(o.instance, e);
    m_tracked[o.instance].openEpisodeId = e.id;
    saveOpenEpisodes();
}

void EventMemory::enrichEpisode(const Observation& o, const QString& newCause,
                                const QString& signalEvent, qint64 /*nowSec*/) {
    const auto ep = m_openEpisodes.find(o.instance);
    if (ep == m_openEpisodes.end())
        return;
    // La cause suit la priorite : crash > stuck > silent. Un silence explique par
    // un crash decouvert ensuite devient un crash.
    if (causeRank(newCause) > causeRank(ep->cause))
        ep->cause = newCause;
    if (!ep->signalEvents.contains(signalEvent))
        ep->signalEvents << signalEvent;
    saveOpenEpisodes();
}

void EventMemory::closeEpisode(const QString& instance, qint64 /*closedAt*/) {
    m_openEpisodes.remove(instance);
    m_tracked[instance].openEpisodeId.clear();
    saveOpenEpisodes();
}

// --- Boucle d'observation ----------------------------------------------------

void EventMemory::observe(const QVector<Observation>& snapshot, qint64 nowSec) {
    // 1. Crediter la tranche ECOULEE depuis le tick precedent, AU TITRE DES ETATS
    //    QUI TENAIENT PENDANT cette tranche (episodes deja ouverts), avant
    //    d'appliquer les nouvelles transitions.
    if (m_lastTickSec > 0) {
        const qint64 slice = nowSec - m_lastTickSec;
        if (slice > kMaxTickGapS) {
            // Trou d'observation : morfMonitor etait endormi/arrete. Ni observe ni
            // downtime credites ; on pose un monitor_gap.
            const QString host = QHostInfo::localHostName();
            QJsonObject data;
            data["from"]  = static_cast<double>(m_lastTickSec);
            data["to"]    = static_cast<double>(nowSec);
            data["gap_s"] = static_cast<double>(slice);
            appendEvent(nowSec, QStringLiteral("morfMonitor"), host,
                        QStringLiteral("morfMonitor@") + host,
                        QStringLiteral("monitor_gap"), severityFor(QStringLiteral("monitor_gap")),
                        QStringLiteral("internal"), QString(), QString(), QString(), data);
        } else if (slice > 0) {
            creditObserved(m_lastTickSec, nowSec);
            for (auto it = m_openEpisodes.constBegin(); it != m_openEpisodes.constEnd(); ++it)
                creditDowntime(it.key(), it.value().cause, m_lastTickSec, nowSec);
        }
    }
    m_lastTickSec = nowSec;
    m_cursorDirty = true;

    // 2. Appliquer les transitions du snapshot courant (declares uniquement).
    for (const Observation& o : snapshot) {
        if (!o.declared || o.instance.isEmpty())
            continue;
        applyTransition(o, nowSec);
    }

    // 3. Persistance espacee (usure SD).
    maybePersist(nowSec);
}

void EventMemory::applyTransition(const Observation& o, qint64 nowSec) {
    Tracked& t = m_tracked[o.instance];
    const QString from = t.state;
    const QString to   = compositeState(o);
    const QString day  = dayKeyOf(nowSec);

    // Premiere apparition : evenement dedie + memorisation pour la table de vie.
    if (from.isEmpty()) {
        appendEvent(nowSec, o.service, o.host, o.instance,
                    QStringLiteral("service_first_seen"), QStringLiteral("info"),
                    QStringLiteral("beacon"), QString(), to, QString());
        if (!m_firstSeen.contains(o.instance)) {
            m_firstSeen[o.instance] = nowSec;
            m_lifeDirty = true;
        }
    }

    // Detection des relances (independante de l'etat) : NRestarts a augmente =>
    // systemd a relance le service. On l'attribue a l'episode en cours s'il existe.
    if (o.nRestarts >= 0) {
        if (t.nRestarts >= 0 && o.nRestarts > t.nRestarts) {
            const int delta = static_cast<int>(o.nRestarts - t.nRestarts);
            appendEvent(nowSec, o.service, o.host, o.instance,
                        QStringLiteral("restart_succeeded"), QStringLiteral("info"),
                        QStringLiteral("systemd"), QString(), QString(), t.openEpisodeId);
            DayServiceStat& st = dayService(day, o.instance);
            st.restarts       += delta;
            st.restartSuccess += delta;
            const auto ep = m_openEpisodes.find(o.instance);
            if (ep != m_openEpisodes.end()) {
                ep->restartAttempts += delta;
                ep->restartOutcome = QStringLiteral("succeeded");
                saveOpenEpisodes();
            }
        }
        t.nRestarts = o.nRestarts;
    }

    if (to != from) {
        const bool wasUnavail = isUnavailable(from);
        const bool isUnavail  = isUnavailable(to);

        if (!wasUnavail && isUnavail) {
            // Ouverture d'un episode.
            const QString sig = signalEventFor(to);
            qint64 openedAt = nowSec;
            // Silence : antidate au dernier heartbeat (derniere preuve de vie).
            if (to == QLatin1String("silent") && o.lastSeen > 0 && o.lastSeen < nowSec)
                openedAt = o.lastSeen;
            openEpisode(o, to, sig, openedAt);
            appendEvent(nowSec, o.service, o.host, o.instance, sig, severityFor(sig),
                        (to == QLatin1String("silent")) ? QStringLiteral("heartbeat")
                                                        : QStringLiteral("status"),
                        from, to, m_tracked[o.instance].openEpisodeId);
            DayServiceStat& st = dayService(day, o.instance);
            st.incidents++;
            if (to == QLatin1String("crash"))       { st.crashes++;         st.incCrash++;  }
            else if (to == QLatin1String("stuck"))  { st.stuckSignals++;    st.incStuck++;  }
            else                                    { st.heartbeatLosses++; st.incSilent++; }
        } else if (wasUnavail && isUnavail) {
            // Toujours indisponible, mais la cause change (ex. silent -> crash).
            const QString sig = signalEventFor(to);
            enrichEpisode(o, to, sig, nowSec);
            appendEvent(nowSec, o.service, o.host, o.instance, sig, severityFor(sig),
                        (to == QLatin1String("crash")) ? QStringLiteral("systemd")
                                                       : QStringLiteral("status"),
                        from, to, t.openEpisodeId);
            DayServiceStat& st = dayService(day, o.instance);
            if (to == QLatin1String("crash"))       st.crashes++;
            else if (to == QLatin1String("stuck"))  st.stuckSignals++;
            else                                    st.heartbeatLosses++;
        } else if (wasUnavail && !isUnavail) {
            // Retour a la normale : fermeture de l'episode.
            const auto ep = m_openEpisodes.find(o.instance);
            const QString wasCause = (ep != m_openEpisodes.end()) ? ep->cause : QString();
            const QString closeEvent = (wasCause == QLatin1String("silent"))
                ? QStringLiteral("heartbeat_recovered") : QStringLiteral("service_recovered");
            appendEvent(nowSec, o.service, o.host, o.instance, closeEvent,
                        QStringLiteral("info"),
                        (wasCause == QLatin1String("silent")) ? QStringLiteral("heartbeat")
                                                              : QStringLiteral("status"),
                        from, to, t.openEpisodeId);
            closeEpisode(o.instance, nowSec);
        } else {
            // Transitions entre etats NON incidents (available / stopped / host_offline).
            if (to == QLatin1String("stopped")) {
                appendEvent(nowSec, o.service, o.host, o.instance,
                            QStringLiteral("service_stopped"), QStringLiteral("info"),
                            QStringLiteral("systemd"), from, to, QString());
                dayService(day, o.instance).stops++;
            } else if (to == QLatin1String("available")
                       && (from == QLatin1String("stopped")
                           || from == QLatin1String("host_offline")
                           || from.isEmpty())) {
                appendEvent(nowSec, o.service, o.host, o.instance,
                            QStringLiteral("service_started"), QStringLiteral("info"),
                            QStringLiteral("systemd"), from, to, QString());
                dayService(day, o.instance).starts++;
            }
        }
    }

    t.state = to;
}

// --- API morfhistory/1 -------------------------------------------------------

QJsonObject EventMemory::eventsJson(qint64 sinceSec, qint64 untilSec,
                                    const QString& service) const {
    QJsonArray arr;
    for (const QJsonObject& e : m_ring) {
        const qint64 ts = static_cast<qint64>(e.value(QStringLiteral("ts")).toDouble());
        if (sinceSec > 0 && ts < sinceSec)  continue;
        if (untilSec > 0 && ts > untilSec)  continue;
        if (!service.isEmpty() && e.value(QStringLiteral("service")).toString() != service)
            continue;
        arr.append(e);
    }
    QJsonObject o;
    o["proto"]  = QStringLiteral("morfhistory/1");
    o["events"] = arr;
    o["count"]  = arr.size();
    o["ts"]     = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

QJsonObject EventMemory::dailyJson(const QString& fromDay, const QString& toDay,
                                   const QString& service) const {
    QJsonArray arr;
    for (auto it = m_days.constBegin(); it != m_days.constEnd(); ++it) {
        const QString& day = it.key();
        if (!fromDay.isEmpty() && day < fromDay) continue;
        if (!toDay.isEmpty()   && day > toDay)   continue;
        const DayStat& ds = it.value();
        QJsonObject svcs;
        for (auto s = ds.services.constBegin(); s != ds.services.constEnd(); ++s) {
            if (!service.isEmpty() && s.key() != service)
                continue;   // filtre par instance
            const DayServiceStat& st = s.value();
            QJsonObject in{
                {"count", st.incidents},
                {"by_cause", QJsonObject{{"crash", st.incCrash}, {"stuck", st.incStuck},
                                         {"silent", st.incSilent}}},
                {"downtime_seconds", st.downtime},
                {"longest_episode_seconds", st.longestEpisode}};
            QJsonObject ev{
                {"crashes", st.crashes}, {"restarts", st.restarts},
                {"heartbeat_losses", st.heartbeatLosses}, {"stuck_signals", st.stuckSignals},
                {"starts", st.starts}, {"stops", st.stops}};
            QJsonObject sj{{"events", ev}, {"incidents", in}};
            if (ds.observedSeconds > 0)
                sj["availability"] = (ds.observedSeconds - st.downtime) / ds.observedSeconds;
            svcs[s.key()] = sj;
        }
        QJsonObject dj;
        dj["day"] = day;
        dj["observed_seconds"] = ds.observedSeconds;
        dj["services"] = svcs;
        arr.append(dj);
    }
    QJsonObject o;
    o["proto"] = QStringLiteral("morfhistory/1");
    o["days"]  = arr;
    o["ts"]    = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

QJsonObject EventMemory::lifeJson() const {
    // Tout se DERIVE des jours (source de verite durable) ; on ne lit dans
    // m_firstSeen/m_lastEvent que ce qui n'est pas dans les agregats.
    struct Acc {
        int incidents = 0, incCrash = 0, incStuck = 0, incSilent = 0;
        int crashes = 0, restarts = 0;
        double downtime = 0;
        double bestAvail = -1, worstAvail = -1;
    };
    QHash<QString, Acc> per;
    double gObserved = 0, gDowntime = 0;
    int gIncidents = 0, gCrashes = 0, gRestarts = 0;

    for (auto it = m_days.constBegin(); it != m_days.constEnd(); ++it) {
        const DayStat& ds = it.value();
        gObserved += ds.observedSeconds;
        for (auto s = ds.services.constBegin(); s != ds.services.constEnd(); ++s) {
            const DayServiceStat& st = s.value();
            Acc& a = per[s.key()];
            a.incidents += st.incidents; a.incCrash += st.incCrash;
            a.incStuck += st.incStuck;   a.incSilent += st.incSilent;
            a.crashes += st.crashes;     a.restarts += st.restarts;
            a.downtime += st.downtime;
            gDowntime += st.downtime; gIncidents += st.incidents;
            gCrashes += st.crashes;   gRestarts += st.restarts;
            if (ds.observedSeconds > 0) {
                const double av = (ds.observedSeconds - st.downtime) / ds.observedSeconds;
                if (a.bestAvail < 0 || av > a.bestAvail)   a.bestAvail = av;
                if (a.worstAvail < 0 || av < a.worstAvail) a.worstAvail = av;
            }
        }
    }

    QJsonObject services;
    // Union des services connus (jours + premiere apparition memorisee).
    QSet<QString> known;
    for (auto it = per.constBegin(); it != per.constEnd(); ++it) known.insert(it.key());
    for (auto it = m_firstSeen.constBegin(); it != m_firstSeen.constEnd(); ++it) known.insert(it.key());
    for (const QString& inst : known) {
        const Acc a = per.value(inst);
        QJsonObject sj;
        if (m_firstSeen.contains(inst)) sj["first_seen"] = static_cast<double>(m_firstSeen.value(inst));
        if (m_lastEvent.contains(inst)) sj["last_event"] = static_cast<double>(m_lastEvent.value(inst));
        sj["total_incidents"] = a.incidents;
        sj["by_cause"] = QJsonObject{{"crash", a.incCrash}, {"stuck", a.incStuck},
                                     {"silent", a.incSilent}};
        sj["total_crashes"] = a.crashes;
        sj["total_restarts"] = a.restarts;
        sj["total_downtime_seconds"] = a.downtime;
        if (a.bestAvail >= 0)  sj["best_daily_availability"]  = a.bestAvail;
        if (a.worstAvail >= 0) sj["worst_daily_availability"] = a.worstAvail;
        services[inst] = sj;
    }

    QJsonObject global{
        {"total_incidents", gIncidents}, {"total_crashes", gCrashes},
        {"total_restarts", gRestarts}, {"total_downtime_seconds", gDowntime},
        {"observed_seconds", gObserved}};
    if (gObserved > 0)
        global["availability"] = (gObserved - gDowntime) / gObserved;

    // « since » : la premiere apparition la plus ancienne connue.
    qint64 since = 0;
    for (auto it = m_firstSeen.constBegin(); it != m_firstSeen.constEnd(); ++it)
        if (since == 0 || it.value() < since) since = it.value();

    QJsonObject o;
    o["proto"] = QStringLiteral("morfhistory/1");
    if (since > 0) o["since"] = static_cast<double>(since);
    o["global"]   = global;
    o["services"] = services;
    o["ts"]       = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

QJsonObject EventMemory::summaryJson() const {
    QJsonObject o;
    o["events_ring"]   = m_ring.size();
    o["open_episodes"] = m_openEpisodes.size();
    o["days_tracked"]  = m_days.size();
    o["seq"]           = static_cast<double>(m_seq);
    o["persistent"]    = !m_dir.isEmpty();
    return o;
}

} // namespace morfmonitor
