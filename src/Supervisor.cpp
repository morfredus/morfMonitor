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

QJsonObject Supervisor::collectSystemd() const {
    QJsonObject o;
    QJsonArray arr;
    if (!m_config) {
        o["services"] = arr;
        return o;
    }

    // Un seul appel a systemctl pour TOUTES les unites, plutot qu'un appel par
    // service : lancer un processus coute cher, et le faire six fois toutes les
    // cinq secondes se verrait sur un Raspberry Pi.
    QStringList units;
    for (const SystemdServiceDef& d : m_config->systemdServices()) {
        if (d.enabled)
            units << (d.unit + QStringLiteral(".service"));
    }

    QHash<QString, QString> activeState;
    QHash<QString, QString> subState;
    if (!units.isEmpty()) {
        QStringList args{QStringLiteral("show"),
                         QStringLiteral("--property=Id,ActiveState,SubState"),
                         QStringLiteral("--no-pager")};
        args << units;
        const QString out = runCommand(QStringLiteral("systemctl"), args);

        // `systemctl show` sépare les unités par une ligne vide.
        QString id, active, sub;
        const QStringList lines = out.split(QLatin1Char('\n'));
        for (int i = 0; i <= lines.size(); ++i) {
            const QString line = (i < lines.size()) ? lines.at(i).trimmed() : QString();
            if (line.isEmpty()) {
                if (!id.isEmpty()) {
                    activeState.insert(id, active);
                    subState.insert(id, sub);
                }
                id.clear(); active.clear(); sub.clear();
                continue;
            }
            if (line.startsWith(QLatin1String("Id=")))               id = line.mid(3);
            else if (line.startsWith(QLatin1String("ActiveState="))) active = line.mid(12);
            else if (line.startsWith(QLatin1String("SubState=")))    sub = line.mid(9);
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
        s["state"]     = state.isEmpty() ? QStringLiteral("unknown") : state;
        s["sub_state"] = subState.value(key);
        s["active"]    = (state == QLatin1String("active"));
        arr.append(s);
    }

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
