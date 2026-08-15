/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/Collectors.h"

#include <QProcess>
#include <QTcpSocket>
#include <QDateTime>
#include <QJsonArray>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <limits>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <tlhelp32.h>   // CreateToolhelp32Snapshot : enumeration des processus
#  include <psapi.h>      // GetProcessMemoryInfo : working set d'un processus
#endif

namespace morfmonitor {

namespace {

QString runCommand(const QString& program, const QStringList& args, int timeoutMs = 3000) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(200);
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

} // namespace

// =============================================================================
//  Services systemd
// =============================================================================

namespace {

// systemd note les compteurs indisponibles par une valeur sentinelle (le champ
// n'est pas compte, l'unite est inactive, l'accounting est desactive) : soit la
// chaine vide ou « [not set] », soit UINT64_MAX. La distinguer d'une vraie
// valeur evite de publier un « 0 octet » trompeur la ou il n'y a simplement rien
// a mesurer.
bool parseSystemdU64(const QString& raw, quint64& out) {
    const QString v = raw.trimmed();
    if (v.isEmpty() || v == QLatin1String("[not set]"))
        return false;
    bool ok = false;
    const quint64 n = v.toULongLong(&ok);
    if (!ok || n == std::numeric_limits<quint64>::max())
        return false;
    out = n;
    return true;
}

// Repli memoire quand systemd ne donne pas MemoryCurrent (contrôleur cgroup
// « memory » desactive au boot, cas par defaut de Raspberry Pi OS : le noyau
// demarre avec `cgroup_disable=memory`, si bien que le fichier memory.current
// n'existe meme pas). On somme alors la memoire des processus du cgroup de
// l'unite, un par un.
//
// On additionne le PSS (Proportional Set Size), PAS le RSS : tous les services
// du parc sont des binaires Qt qui PARTAGENT les memes bibliotheques. Le RSS
// compterait ces pages partagees en entier dans CHAQUE service, gonflant chaque
// mesure de plusieurs dizaines de Mio. Le PSS repartit une page partagee entre
// ses N utilisateurs : la somme sur le cgroup est alors fidele a ce que le
// service coute vraiment.
//
// smaps_rollup n'est lisible que pour un processus du meme utilisateur (ou avec
// CAP_SYS_PTRACE). Dans le parc morfSystem, tous les services tournent sous le
// meme compte : morfMonitor lit donc sans privilege particulier. Sur une machine
// ou ce ne serait pas le cas, la lecture echoue silencieusement et la valeur est
// simplement omise, jamais un chiffre faux.
//
// `cgPath` est le ControlGroup rapporte par systemd, p. ex.
// « /system.slice/morfphoto.service ». Renvoie -1 si rien n'a pu etre lu.
qint64 memoryPssFromCgroup(const QString& cgPath) {
    if (cgPath.isEmpty())
        return -1;

    QFile procs(QStringLiteral("/sys/fs/cgroup") + cgPath + QStringLiteral("/cgroup.procs"));
    if (!procs.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;

    qint64 totalKb = 0;
    bool   anyRead = false;
    const QStringList pids = QString::fromUtf8(procs.readAll())
                                 .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& pid : pids) {
        QFile roll(QStringLiteral("/proc/") + pid.trimmed() + QStringLiteral("/smaps_rollup"));
        if (!roll.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;   // processus disparu entre-temps, ou illisible : on l'ignore
        for (const QString& line : QString::fromUtf8(roll.readAll())
                                       .split(QLatin1Char('\n'))) {
            // La ligne totale est « Pss: ». Ne PAS confondre avec « Pss_Anon: »,
            // « Pss_File: », « Pss_Shmem: » qui la detaillent et la doubleraient.
            if (!line.startsWith(QLatin1String("Pss:")))
                continue;
            const QStringList f = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                             Qt::SkipEmptyParts);
            if (f.size() >= 2) {
                totalKb += f.at(1).toLongLong();   // valeur en kB
                anyRead = true;
            }
            break;   // une seule ligne « Pss: » par rollup
        }
    }

    return anyRead ? totalKb * 1024 : -1;
}

} // namespace

QJsonObject Supervisor::collectSystemd() {
#ifdef _WIN32
    // Pas de systemd sous Windows : on sert l'equivalent par processus.
    return collectWindowsServices();
#else
    QJsonObject o;
    QJsonArray arr;
    if (!m_config) {
        o["services"] = arr;
        return o;
    }

    // Un seul appel a systemctl pour TOUTES les unites, plutot qu'un appel par
    // service : lancer un processus coute cher, et le faire six fois toutes les
    // cinq secondes se verrait sur un Raspberry Pi. Les proprietes de
    // consommation (memoire, temps CPU, taches) sont demandees dans CE MEME
    // appel : elles ne coutent donc rien de plus qu'un etat seul.
    QStringList units;
    for (const SystemdServiceDef& d : m_config->systemdServices()) {
        if (d.enabled)
            units << (d.unit + QStringLiteral(".service"));
    }

    QHash<QString, QString> activeState;
    QHash<QString, QString> subState;
    // Compteurs de consommation, par unite. systemd agrege ces valeurs sur le
    // CGROUP COMPLET de l'unite, pas sur son seul PID principal : c'est
    // exactement ce que l'on veut mesurer : « combien consomme le service »,
    // pas « combien consomme son processus principal », mesure qui resterait
    // juste meme si l'implementation interne du service evoluait.
    QHash<QString, quint64> memBytes;   // MemoryCurrent
    QHash<QString, quint64> cpuNsec;    // CPUUsageNSec (temps CPU cumule)
    QHash<QString, quint64> tasks;      // TasksCurrent
    QHash<QString, QString> ctrlGroup;  // ControlGroup (chemin du cgroup de l'unite)
    // Identite et vie de l'unite, demandees dans le MEME appel (cout nul) : le PID
    // principal, l'instant d'activation (monotone, pour en deduire l'uptime) et le
    // nombre de redemarrages. Utiles a morfAnalytics pour distinguer un service
    // stable d'un service qui redemarre en boucle.
    QHash<QString, quint64> mainPid;         // MainPID
    QHash<QString, quint64> activeEnterMono; // ActiveEnterTimestampMonotonic (usec depuis boot)
    QHash<QString, quint64> nRestarts;       // NRestarts (compteur cumule)
    if (!units.isEmpty()) {
        QStringList args{QStringLiteral("show"),
                         QStringLiteral("--property=Id,ActiveState,SubState,"
                                        "MemoryCurrent,CPUUsageNSec,TasksCurrent,ControlGroup,"
                                        "MainPID,ActiveEnterTimestampMonotonic,NRestarts"),
                         QStringLiteral("--no-pager")};
        args << units;
        const QString out = runCommand(QStringLiteral("systemctl"), args);

        // `systemctl show` sépare les unités par une ligne vide.
        QString id, active, sub, mem, cpu, tsk, cg, pid, aet, nr;
        const QStringList lines = out.split(QLatin1Char('\n'));
        for (int i = 0; i <= lines.size(); ++i) {
            const QString line = (i < lines.size()) ? lines.at(i).trimmed() : QString();
            if (line.isEmpty()) {
                if (!id.isEmpty()) {
                    activeState.insert(id, active);
                    subState.insert(id, sub);
                    quint64 v = 0;
                    if (parseSystemdU64(mem, v)) memBytes.insert(id, v);
                    if (parseSystemdU64(cpu, v)) cpuNsec.insert(id, v);
                    if (parseSystemdU64(tsk, v)) tasks.insert(id, v);
                    if (parseSystemdU64(pid, v)) mainPid.insert(id, v);
                    if (parseSystemdU64(aet, v)) activeEnterMono.insert(id, v);
                    if (parseSystemdU64(nr,  v)) nRestarts.insert(id, v);
                    if (!cg.isEmpty()) ctrlGroup.insert(id, cg);
                }
                id.clear(); active.clear(); sub.clear();
                mem.clear(); cpu.clear(); tsk.clear(); cg.clear();
                pid.clear(); aet.clear(); nr.clear();
                continue;
            }
            if (line.startsWith(QLatin1String("Id=")))                 id = line.mid(3);
            else if (line.startsWith(QLatin1String("ActiveState=")))   active = line.mid(12);
            else if (line.startsWith(QLatin1String("SubState=")))      sub = line.mid(9);
            else if (line.startsWith(QLatin1String("MemoryCurrent="))) mem = line.mid(14);
            else if (line.startsWith(QLatin1String("CPUUsageNSec=")))  cpu = line.mid(13);
            else if (line.startsWith(QLatin1String("TasksCurrent=")))  tsk = line.mid(13);
            else if (line.startsWith(QLatin1String("ControlGroup=")))  cg  = line.mid(13);
            else if (line.startsWith(QLatin1String("MainPID=")))       pid = line.mid(8);
            // Le prefixe le plus long d'abord : "ActiveEnterTimestampMonotonic="
            // contient "ActiveEnterTimestamp=" en sous-chaine.
            else if (line.startsWith(QLatin1String("ActiveEnterTimestampMonotonic="))) aet = line.mid(30);
            else if (line.startsWith(QLatin1String("NRestarts=")))     nr  = line.mid(10);
        }
    }

    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> seenUnits;

    // Uptime systeme (horloge monotone, secondes depuis le boot). systemd donne
    // l'instant d'activation de chaque unite en microsecondes monotones : l'uptime
    // du service en est la difference. On lit /proc/uptime une seule fois. Absent
    // hors Linux : l'uptime par service est alors simplement omis.
    double sysUptimeSec = -1.0;
    {
        QFile up(QStringLiteral("/proc/uptime"));
        if (up.open(QIODevice::ReadOnly)) {
            bool ok = false;
            const double v = QString::fromUtf8(up.readAll())
                                 .section(QLatin1Char(' '), 0, 0).toDouble(&ok);
            if (ok) sysUptimeSec = v;
        }
    }

    for (const SystemdServiceDef& d : m_config->systemdServices()) {
        QJsonObject s;
        s["unit"]    = d.unit;
        s["label"]   = d.label;
        s["enabled"] = d.enabled;
        if (!d.enabled) {
            s["state"] = QStringLiteral("disabled");
            s["active"] = false;
            arr.append(s);
            continue;
        }
        const QString key = d.unit + QStringLiteral(".service");
        const QString state = activeState.value(key);
        const bool active = (state == QLatin1String("active"));
        s["state"]     = state.isEmpty() ? QStringLiteral("unknown") : state;
        s["sub_state"] = subState.value(key);
        s["active"]    = active;

        // Nombre de redemarrages : exploitable meme unite inactive. Un service qui
        // redemarre en boucle est un signal fort ; morfAnalytics historisera ce
        // compteur pour reperer les periodes d'instabilite.
        if (nRestarts.contains(key))
            s["restarts"] = static_cast<double>(nRestarts.value(key));

        // Consommation par service. On ne l'expose QUE pour une unite active :
        // un service arrete n'a pas une consommation nulle, il n'a PAS de
        // consommation. Distinguer « actif + 0 » de « inactif + non applicable »
        // est un choix explicite du contrat : l'absence du bloc `resources` dit
        // « non applicable », un `resources` present avec des zeros dirait « rien
        // ne tourne, et je l'ai mesure ».
        if (active) {
            seenUnits.insert(d.unit);

            // PID principal et uptime du service : identite « vivante » de l'unite,
            // au niveau du service (pas dans `resources`, qui ne porte que la
            // consommation). Omis si non disponibles plutot que faux.
            if (mainPid.contains(key) && mainPid.value(key) > 0)
                s["pid"] = static_cast<double>(mainPid.value(key));
            if (sysUptimeSec >= 0 && activeEnterMono.contains(key)) {
                const double sinceBootSec = static_cast<double>(activeEnterMono.value(key)) / 1e6;
                const double up = sysUptimeSec - sinceBootSec;
                if (up >= 0)
                    s["uptime_s"] = qRound(up);
            }

            QJsonObject res;

            // Memoire : d'abord la mesure systemd (memory.current du cgroup v2),
            // la plus juste car elle dedoublonne les pages partagees et inclut ce
            // que le noyau impute au cgroup. A defaut (contrôleur « memory »
            // desactive), on somme le PSS des processus du cgroup. `memory_source`
            // dit laquelle : un consommateur comme morfAnalytics ne doit pas
            // comparer deux mesures de nature differente au fil d'un changement de
            // configuration de la machine.
            if (memBytes.contains(key)) {
                res["memory_bytes"]  = static_cast<double>(memBytes.value(key));
                res["memory_source"] = QStringLiteral("cgroup");
            } else {
                const qint64 pss = memoryPssFromCgroup(ctrlGroup.value(key));
                if (pss >= 0) {
                    res["memory_bytes"]  = static_cast<double>(pss);
                    res["memory_source"] = QStringLiteral("pss_sum");
                }
            }

            if (tasks.contains(key))
                res["tasks"] = static_cast<double>(tasks.value(key));

            if (cpuNsec.contains(key)) {
                const quint64 nsec = cpuNsec.value(key);
                // Temps CPU cumule : compteur stable, directement exploitable par
                // morfAnalytics qui en fera la difference entre deux mesures. On
                // le publie en microsecondes, unite du contrat.
                res["cpu_time_usec"] = static_cast<double>(nsec / 1000);

                // Taux instantane : deduit du delta depuis le releve precedent de
                // CETTE unite. Sans precedent (premier passage, unite qui vient de
                // demarrer), on omet : un 0 % affiche serait faux, « inconnu » est
                // honnete.
                const auto prev = m_cpuSamples.constFind(d.unit);
                if (prev != m_cpuSamples.constEnd() && nowMsec > prev->atMsec
                        && nsec >= prev->cpuNsec) {
                    const double dNsec = static_cast<double>(nsec - prev->cpuNsec);
                    const double dMsec = static_cast<double>(nowMsec - prev->atMsec);
                    // dNsec/1e6 ms de CPU sur dMsec ms de temps reel => *100 pour
                    // un pourcentage. Peut depasser 100 % sur plusieurs coeurs :
                    // c'est correct et voulu (un service sur deux coeurs = 200 %),
                    // exactement comme `top`.
                    res["cpu_percent"] = qRound(dNsec / (dMsec * 1e4) * 10.0) / 10.0;
                }
                m_cpuSamples.insert(d.unit, CpuSample{nsec, nowMsec});
            }

            if (!res.isEmpty())
                s["resources"] = res;
        }

        arr.append(s);
    }

    // Oublie l'historique CPU des unites qui ne sont plus actives (arretees,
    // desactivees, retirees de la configuration) : sinon un service redemarre
    // apres une longue pause verrait son premier taux calcule sur un delta enorme
    // et un compteur CPU remis a zero, produisant une valeur absurde.
    for (auto it = m_cpuSamples.begin(); it != m_cpuSamples.end();) {
        if (!seenUnits.contains(it.key()))
            it = m_cpuSamples.erase(it);
        else
            ++it;
    }

    o["services"] = arr;
    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
#endif // !_WIN32
}

// -----------------------------------------------------------------------------
//  Equivalent Windows de la supervision par service.
//
// Windows n'a pas systemd : on associe chaque service configure (nom d'unite) a
// un processus dont l'image est <unit>.exe. C'est volontairement MOINS detaille
// que sous Linux (un service = son processus principal, pas son cgroup complet),
// mais le contrat de sortie est le meme : state, active, pid, uptime_s et un bloc
// resources {cpu_percent, cpu_time_usec, memory_bytes, memory_source}. Une donnee
// indisponible est omise, jamais remplacee par un 0 trompeur.
// -----------------------------------------------------------------------------
QJsonObject Supervisor::collectWindowsServices() {
    QJsonObject o;
    QJsonArray arr;
    if (!m_config) {
        o["services"] = arr;
        return o;
    }

#ifdef _WIN32
    // Un seul instantane des processus pour toutes les unites : image -> PID.
    QHash<QString, DWORD> pidByImage;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                QString img = QString::fromWCharArray(pe.szExeFile);
                if (img.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
                    img.chop(4);
                // Premier processus vu pour un nom donne : suffisant pour un service
                // a processus unique, cas visé par cet equivalent.
                pidByImage.insert(img.toLower(), pe.th32ProcessID);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    const qint64 nowMsec = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> seenUnits;

    for (const SystemdServiceDef& d : m_config->systemdServices()) {
        QJsonObject s;
        s["unit"]    = d.unit;
        s["label"]   = d.label;
        s["enabled"] = d.enabled;
        if (!d.enabled) {
            s["state"]  = QStringLiteral("disabled");
            s["active"] = false;
            arr.append(s);
            continue;
        }

        const DWORD pid = pidByImage.value(d.unit.toLower(), 0);
        const bool active = (pid != 0);
        // Windows n'a pas la granularite d'etat de systemd : présent = actif,
        // absent = inactif. On ne prétend pas distinguer failed/activating.
        s["state"]  = active ? QStringLiteral("active") : QStringLiteral("inactive");
        s["active"] = active;

        if (active) {
            seenUnits.insert(d.unit);
            s["pid"] = static_cast<double>(pid);

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (h) {
                QJsonObject res;
                FILETIME ftCreate, ftExit, ftKernel, ftUser;
                if (GetProcessTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                    // Uptime : maintenant - instant de creation. FILETIME compte des
                    // intervalles de 100 ns depuis 1601 ; l'ecart avec l'epoch Unix
                    // est 11644473600 s.
                    ULARGE_INTEGER cre;
                    cre.LowPart  = ftCreate.dwLowDateTime;
                    cre.HighPart = ftCreate.dwHighDateTime;
                    const qint64 createMs =
                        static_cast<qint64>(cre.QuadPart / 10000ULL) - 11644473600000LL;
                    const qint64 up = (nowMsec - createMs) / 1000;
                    if (up >= 0)
                        s["uptime_s"] = static_cast<double>(up);

                    // Temps CPU cumule (noyau + utilisateur), en unites de 100 ns.
                    ULARGE_INTEGER k, u;
                    k.LowPart = ftKernel.dwLowDateTime; k.HighPart = ftKernel.dwHighDateTime;
                    u.LowPart = ftUser.dwLowDateTime;   u.HighPart = ftUser.dwHighDateTime;
                    const quint64 cpu100ns = k.QuadPart + u.QuadPart;
                    res["cpu_time_usec"] = static_cast<double>(cpu100ns / 10ULL);

                    // Taux instantane : delta depuis le releve precedent de CETTE
                    // unite (meme logique que sous Linux). Omis au premier passage.
                    const auto prev = m_cpuSamples.constFind(d.unit);
                    if (prev != m_cpuSamples.constEnd() && nowMsec > prev->atMsec
                            && cpu100ns >= prev->cpuNsec) {
                        const double dCpuMs = static_cast<double>(cpu100ns - prev->cpuNsec) / 10000.0;
                        const double dMsec  = static_cast<double>(nowMsec - prev->atMsec);
                        res["cpu_percent"] = qRound(dCpuMs / dMsec * 1000.0) / 10.0;
                    }
                    m_cpuSamples.insert(d.unit, CpuSample{cpu100ns, nowMsec});
                }

                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
                    // Working set : la mesure Windows la plus proche de « RAM du
                    // service ». Nature differente du cgroup Linux, d'ou un
                    // memory_source distinct que morfAnalytics ne comparera pas a lui.
                    res["memory_bytes"]  = static_cast<double>(pmc.WorkingSetSize);
                    res["memory_source"] = QStringLiteral("working_set");
                }

                if (!res.isEmpty())
                    s["resources"] = res;
                CloseHandle(h);
            }
        }
        arr.append(s);
    }

    // Oublie l'historique CPU des unites qui ne tournent plus (meme raison que
    // sous Linux : eviter un delta absurde apres une longue absence).
    for (auto it = m_cpuSamples.begin(); it != m_cpuSamples.end();) {
        if (!seenUnits.contains(it.key()))
            it = m_cpuSamples.erase(it);
        else
            ++it;
    }
#endif // _WIN32

    o["services"] = arr;
    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

// =============================================================================
//  Sondes réseau
// =============================================================================

QJsonObject Supervisor::collectProbes(qint64 uptimeSeconds) const {
    QJsonObject o;
    QJsonArray arr;
    if (!m_config) {
        o["probes"] = arr;
        return o;
    }

    // Délai de grâce : une sonde résout un nom mDNS par multicast. Sonder trop
    // tôt après le démarrage perturbe l'association WiFi en cours, et donne de
    // toute façon un faux « hors ligne ».
    const bool inGrace = uptimeSeconds < m_config->networkProbeGraceS();
    o["grace"] = inGrace;

    for (const NetworkServiceDef& d : m_config->networkServices()) {
        QJsonObject p;
        p["name"]    = d.name;
        p["label"]   = d.label;
        p["host"]    = d.host;
        p["port"]    = d.port;
        p["enabled"] = d.enabled;

        if (!d.enabled) {
            p["state"] = QStringLiteral("disabled");
            p["online"] = false;
        } else if (inGrace) {
            // On ne ment pas : « pas encore sondé » n'est pas « hors ligne ».
            p["state"] = QStringLiteral("pending");
            p["online"] = false;
        } else {
            QTcpSocket sock;
            QElapsedTimer timer;
            timer.start();
            sock.connectToHost(d.host, static_cast<quint16>(d.port));
            const bool ok = sock.waitForConnected(d.timeoutMs);
            p["online"]     = ok;
            p["state"]      = ok ? QStringLiteral("online") : QStringLiteral("offline");
            p["latency_ms"] = static_cast<double>(timer.elapsed());
            if (!ok)
                p["error"] = sock.errorString();
            sock.abort();
        }
        arr.append(p);
    }

    o["probes"] = arr;
    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

// =============================================================================
//  Cause du dernier redémarrage
// =============================================================================

QJsonObject RebootCauseDetector::detect() {
    if (m_done)
        return m_cached;

    QJsonObject o;
    QString cause = QStringLiteral("unknown");
    QString label = QStringLiteral("Cause du redémarrage inconnue");
    QString evidence;
    double confidence = 0.0;

    // --- Indice 1 : le journal du boot précédent s'est-il terminé proprement ?
    //
    // C'est l'indice le plus fiable. Un arrêt propre laisse des traces
    // « systemd-shutdown » ; une coupure d'alimentation ou un plantage coupe le
    // journal net, sans rien écrire. L'absence de trace est donc informative.
    const QString prevTail = runCommand(
        QStringLiteral("journalctl"),
        {QStringLiteral("-b"), QStringLiteral("-1"), QStringLiteral("-n"), QStringLiteral("40"),
         QStringLiteral("--no-pager"), QStringLiteral("-o"), QStringLiteral("short")},
        5000);

    const bool haveJournal = !prevTail.trimmed().isEmpty();
    const bool cleanShutdown = prevTail.contains(QStringLiteral("systemd-shutdown"))
                            || prevTail.contains(QStringLiteral("Reached target"), Qt::CaseInsensitive)
                            || prevTail.contains(QStringLiteral("Shutting down"), Qt::CaseInsensitive);

    if (!haveJournal) {
        // Pas de boot précédent enregistré : première mise en service, ou
        // journal non persistant. On ne peut rien conclure, et on le dit.
        cause = QStringLiteral("unknown");
        label = QStringLiteral("Cause du redémarrage inconnue (aucun journal du démarrage précédent)");
        evidence = QStringLiteral("journalctl -b -1 vide : journal non persistant ou premier démarrage");
        confidence = 0.0;
    } else if (cleanShutdown) {
        // Arrêt propre. Reste à distinguer redémarrage volontaire, mise à jour
        // et extinction franche.
        const bool rebooted = prevTail.contains(QStringLiteral("reboot"), Qt::CaseInsensitive)
                           || prevTail.contains(QStringLiteral("Rebooting"), Qt::CaseInsensitive);
        const bool poweroff = prevTail.contains(QStringLiteral("Power-Off"), Qt::CaseInsensitive)
                           || prevTail.contains(QStringLiteral("poweroff"), Qt::CaseInsensitive);

        // Une mise à jour de paquets juste avant l'arrêt oriente fortement.
        const QString dpkgTail = [] {
            QFile f(QStringLiteral("/var/log/dpkg.log"));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                return QString();
            const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
            return lines.mid(qMax(0, lines.size() - 60)).join(QLatin1Char('\n'));
        }();
        const bool recentUpgrade = dpkgTail.contains(QStringLiteral(" upgrade "));

        if (recentUpgrade) {
            cause = QStringLiteral("update");
            label = QStringLiteral("Redémarrage après mise à jour");
            evidence = QStringLiteral("arrêt propre, et mise à jour de paquets récente dans dpkg.log");
            confidence = 0.6;
        } else if (poweroff && !rebooted) {
            cause = QStringLiteral("clean_boot");
            label = QStringLiteral("Démarrage normal après extinction propre");
            evidence = QStringLiteral("extinction propre enregistrée dans le journal précédent");
            confidence = 0.8;
        } else {
            cause = QStringLiteral("user_requested");
            label = QStringLiteral("Redémarrage demandé par l'utilisateur");
            evidence = QStringLiteral("arrêt propre (traces systemd-shutdown) sans indice de mise à jour");
            confidence = 0.7;
        }
    } else {
        // Journal présent mais fin abrupte : la machine s'est arrêtée sans
        // prévenir. Reste à distinguer coupure, panique noyau et watchdog.
        const bool kernelPanic = prevTail.contains(QStringLiteral("Kernel panic"), Qt::CaseInsensitive)
                              || prevTail.contains(QStringLiteral("Oops"), Qt::CaseSensitive);
        const bool watchdogTrace = prevTail.contains(QStringLiteral("watchdog"), Qt::CaseInsensitive)
                                && prevTail.contains(QStringLiteral("did not stop"), Qt::CaseInsensitive);

        if (kernelPanic) {
            cause = QStringLiteral("kernel_panic");
            label = QStringLiteral("Redémarrage suite à un plantage système");
            evidence = QStringLiteral("trace de panique noyau en fin de journal précédent");
            confidence = 0.9;
        } else if (watchdogTrace) {
            cause = QStringLiteral("watchdog");
            label = QStringLiteral("Redémarrage provoqué par un chien de garde");
            evidence = QStringLiteral("trace de watchdog en fin de journal précédent");
            confidence = 0.7;
        } else {
            // Fin abrupte sans explication : coupure d'alimentation dans
            // l'immense majorité des cas. La confiance reste modérée car un gel
            // matériel produit exactement la même absence de trace.
            cause = QStringLiteral("power_loss");
            label = QStringLiteral("Redémarrage après coupure d'alimentation");
            evidence = QStringLiteral("journal précédent interrompu net, sans trace d'arrêt");
            confidence = 0.6;
        }
    }

    o["cause"]      = cause;
    o["label"]      = label;
    o["evidence"]   = evidence;
    o["confidence"] = confidence;

    // La confiance est publiée volontairement : un consommateur (morfNotify)
    // peut choisir de formuler prudemment une cause peu sûre plutôt que
    // d'affirmer. Une supervision qui affirme à tort est pire qu'une qui doute.
    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());

    m_cached = o;
    m_done = true;
    return o;
}

} // namespace morfmonitor
