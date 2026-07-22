/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/Collectors.h"

#include <QFile>
#include <QDir>
#include <QHostInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QProcess>
#include <QNetworkInterface>
#include <QStorageInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <vector>

namespace morfmonitor {

namespace {

// Lit un fichier texte court (/proc, /sys). Renvoie une chaine vide si absent :
// tous ces chemins sont optionnels selon la machine et le noyau.
QString readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

double readDouble(const QString& path, double fallback = -1.0) {
    bool ok = false;
    const double v = readAll(path).trimmed().toDouble(&ok);
    return ok ? v : fallback;
}

// Execute une commande courte et renvoie sa sortie. Utilise avec parcimonie :
// lancer un processus coute infiniment plus cher que lire un fichier, d'ou la
// preference systematique pour /proc et /sys quand l'information s'y trouve.
QString runCommand(const QString& program, const QStringList& args, int timeoutMs = 2000) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(200);
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

// /etc/os-release : format « CLE="valeur" », guillemets facultatifs.
QString osReleaseValue(const QString& content, const QString& key) {
    for (const QString& line : content.split(QLatin1Char('\n'))) {
        if (!line.startsWith(key + QLatin1Char('=')))
            continue;
        QString v = line.mid(key.size() + 1).trimmed();
        if (v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
            v = v.mid(1, v.size() - 2);
        return v;
    }
    return QString();
}

} // namespace

// =============================================================================
//  Informations systeme
// =============================================================================

QJsonObject SystemCollector::collect() {
    if (!m_staticDone) {
        QJsonObject s;
        s["hostname"] = QHostInfo::localHostName();

        const QString osrel = readAll(QStringLiteral("/etc/os-release"));
        s["os"] = osReleaseValue(osrel, QStringLiteral("PRETTY_NAME"));
        s["os_id"] = osReleaseValue(osrel, QStringLiteral("ID"));

        s["kernel"] = readAll(QStringLiteral("/proc/sys/kernel/osrelease")).trimmed();
        s["arch"] = QSysInfo::currentCpuArchitecture();

        // Propre au Raspberry Pi ; absent ailleurs, et c'est sans conséquence.
        const QString model = readAll(QStringLiteral("/proc/device-tree/model"));
        if (!model.isEmpty()) {
            // Le device-tree termine ses chaînes par un octet nul, qui polluerait
            // le JSON s'il était recopié tel quel.
            s["model"] = QString(model).remove(QChar(u'\0')).trimmed();
        }

        m_static = s;
        m_staticDone = true;
    }

    QJsonObject o = m_static;

    // L'uptime bouge : toujours relu.
    const QString uptimeRaw = readAll(QStringLiteral("/proc/uptime"));
    const double uptime = uptimeRaw.split(QLatin1Char(' ')).value(0).toDouble();
    o["uptime_s"] = uptime;
    o["boot_time"] = QDateTime::currentDateTimeUtc()
                         .addSecs(-static_cast<qint64>(uptime))
                         .toString(Qt::ISODate);
    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

// =============================================================================
//  Ressources
// =============================================================================

QJsonObject ResourceCollector::collect() {
    QJsonObject o;

    // --- CPU : taux d'occupation ---------------------------------------------
    // /proc/stat ne donne PAS un pourcentage mais des compteurs cumules depuis
    // le demarrage. Le taux est la part de temps non-inactif entre DEUX
    // lectures ; la premiere ne peut donc rien produire.
    {
        const QString stat = readAll(QStringLiteral("/proc/stat"));
        const QString first = stat.section(QLatin1Char('\n'), 0, 0);
        const QStringList f = first.split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts);
        if (f.size() >= 5 && f.first() == QLatin1String("cpu")) {
            quint64 total = 0;
            for (int i = 1; i < f.size(); ++i)
                total += f.at(i).toULongLong();
            const quint64 idle = f.at(4).toULongLong()
                               + (f.size() > 5 ? f.at(5).toULongLong() : 0); // idle + iowait

            if (m_havePrev && total > m_prevTotal) {
                const double dTotal = static_cast<double>(total - m_prevTotal);
                const double dIdle  = static_cast<double>(idle - m_prevIdle);
                o["cpu_percent"] = qRound((1.0 - dIdle / dTotal) * 1000.0) / 10.0;
            }
            m_prevTotal = total;
            m_prevIdle = idle;
            m_havePrev = true;
        }
    }

    // Fréquence courante du cœur 0, exprimée en kHz par le noyau.
    const double khz = readDouble(
        QStringLiteral("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"));
    if (khz > 0)
        o["cpu_freq_mhz"] = qRound(khz / 1000.0);

    // --- Charge système ------------------------------------------------------
    {
        const QStringList l = readAll(QStringLiteral("/proc/loadavg"))
                                  .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (l.size() >= 3) {
            o["load"] = QJsonArray{l.at(0).toDouble(), l.at(1).toDouble(), l.at(2).toDouble()};
        }
    }

    // --- Mémoire -------------------------------------------------------------
    // MemAvailable, et non MemFree : le noyau compte comme disponible le cache
    // qu'il sait pouvoir libérer. MemFree seul donne une impression de saturation
    // trompeuse sur une machine qui tourne depuis longtemps.
    {
        QHash<QString, quint64> mem;
        for (const QString& line : readAll(QStringLiteral("/proc/meminfo"))
                                       .split(QLatin1Char('\n'))) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon <= 0)
                continue;
            mem.insert(line.left(colon),
                       line.mid(colon + 1).remove(QStringLiteral("kB")).trimmed().toULongLong());
        }
        auto kb = [&](const char* k) { return static_cast<double>(mem.value(QLatin1String(k))) * 1024.0; };

        const double total = kb("MemTotal");
        const double avail = kb("MemAvailable");
        if (total > 0) {
            QJsonObject m;
            m["total_b"]     = total;
            m["available_b"] = avail;
            m["used_b"]      = total - avail;
            m["percent"]     = qRound((1.0 - avail / total) * 1000.0) / 10.0;
            o["memory"] = m;
        }
        const double swapTotal = kb("SwapTotal");
        if (swapTotal > 0) {
            QJsonObject s;
            s["total_b"] = swapTotal;
            s["free_b"]  = kb("SwapFree");
            s["used_b"]  = swapTotal - kb("SwapFree");
            s["percent"] = qRound((1.0 - kb("SwapFree") / swapTotal) * 1000.0) / 10.0;
            o["swap"] = s;
        } else {
            o["swap"] = QJsonObject{{"total_b", 0}, {"percent", 0}};
        }
    }

    // --- Disque : tous les volumes REELS montés -----------------------------
    // La racine seule mentait dès que /home est une partition séparée —
    // installation Linux classique sur un portable : « / » à 90 % affole alors
    // que les données ont ailleurs toute la place, et inversement un /home
    // plein restait invisible. Plutôt que d'ajouter /home en dur (qui n'est
    // pas un montage séparé sur un Raspberry Pi), on liste chaque système de
    // fichiers adossé à un périphérique (/dev/…), en écartant les
    // pseudo-montages : tmpfs, et les squashfs des snaps — en lecture seule,
    // toujours « pleins » à 100 %, la fausse alerte assurée.
    {
        struct Vol { QString mount; QJsonObject d; };
        std::vector<Vol> vols;
        QSet<QString> devices;
        for (const QStorageInfo& v : QStorageInfo::mountedVolumes()) {
            if (!v.isValid() || !v.isReady() || v.isReadOnly() || v.bytesTotal() <= 0)
                continue;
            const QString device = QString::fromUtf8(v.device());
            if (!device.startsWith(QLatin1String("/dev/")))
                continue;
            if (devices.contains(device))
                continue;   // montage bind : le même volume sous un autre chemin
            devices.insert(device);

            QJsonObject d;
            d["mount"]   = v.rootPath();
            d["total_b"] = static_cast<double>(v.bytesTotal());
            d["free_b"]  = static_cast<double>(v.bytesAvailable());
            d["used_b"]  = static_cast<double>(v.bytesTotal() - v.bytesAvailable());
            d["percent"] = qRound((1.0 - static_cast<double>(v.bytesAvailable())
                                   / static_cast<double>(v.bytesTotal())) * 1000.0) / 10.0;
            vols.push_back({v.rootPath(), d});
        }
        // Tri par point de montage : « / » d'abord, puis /boot, /home… L'ordre
        // de mountedVolumes() est celui du montage, qui varie d'un boot à
        // l'autre — un affichage qui change de place à chaque redémarrage se
        // lit comme une panne.
        std::sort(vols.begin(), vols.end(),
                  [](const Vol& a, const Vol& b) { return a.mount < b.mount; });

        QJsonArray disks;
        for (const Vol& v : vols) {
            disks.append(v.d);
            if (v.mount == QLatin1String("/"))
                o["disk"] = v.d;   // compat : consommateurs d'avant `disks`
        }
        if (!disks.isEmpty())
            o["disks"] = disks;
    }

    // --- Températures --------------------------------------------------------
    // CPU depuis le noyau, en millidegrés. Disponible sur toute machine Linux
    // dotée d'une sonde ; simplement omis ailleurs.
    {
        QJsonObject t;
        const double cpuMilli = readDouble(
            QStringLiteral("/sys/class/thermal/thermal_zone0/temp"));
        if (cpuMilli > 0)
            t["cpu_c"] = qRound(cpuMilli / 100.0) / 10.0;

        // Le GPU n'est lisible que par vcgencmd, propre au Raspberry Pi. On ne
        // paie ce lancement de processus que si l'outil existe.
        static const bool hasVcgencmd = QFile::exists(QStringLiteral("/usr/bin/vcgencmd"));
        if (hasVcgencmd) {
            const QString out = runCommand(QStringLiteral("/usr/bin/vcgencmd"),
                                           {QStringLiteral("measure_temp")});
            // Format : temp=42.8'C
            const QRegularExpression re(QStringLiteral("temp=([0-9.]+)"));
            const auto m = re.match(out);
            if (m.hasMatch())
                t["gpu_c"] = m.captured(1).toDouble();

            // Bits de bridage : sous-tension, limite thermique... 0x0 = tout va
            // bien. C'est le diagnostic le plus utile d'un Pi instable, et il
            // n'apparait nulle part ailleurs.
            const QString thr = runCommand(QStringLiteral("/usr/bin/vcgencmd"),
                                           {QStringLiteral("get_throttled")});
            const QRegularExpression rt(QStringLiteral("throttled=0x([0-9a-fA-F]+)"));
            const auto mt = rt.match(thr);
            if (mt.hasMatch()) {
                bool ok = false;
                const quint32 bits = mt.captured(1).toUInt(&ok, 16);
                if (ok) {
                    QJsonObject th;
                    th["raw"] = static_cast<double>(bits);
                    th["undervoltage_now"]     = (bits & 0x1) != 0;
                    th["throttled_now"]        = (bits & 0x4) != 0;
                    th["undervoltage_since_boot"] = (bits & 0x10000) != 0;
                    th["throttled_since_boot"]    = (bits & 0x40000) != 0;
                    o["throttling"] = th;
                }
            }
        }
        if (!t.isEmpty())
            o["temperature"] = t;
    }

    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

// =============================================================================
//  Réseau
// =============================================================================

QJsonObject NetworkCollector::collect() {
    QJsonObject o;
    QJsonArray interfaces;

    for (const QNetworkInterface& itf : QNetworkInterface::allInterfaces()) {
        // La boucle locale n'apprend rien sur la connectivité de la machine.
        if (itf.flags().testFlag(QNetworkInterface::IsLoopBack))
            continue;

        QJsonObject i;
        i["name"] = itf.humanReadableName();
        i["mac"]  = itf.hardwareAddress();
        i["up"]   = itf.flags().testFlag(QNetworkInterface::IsUp);
        i["running"] = itf.flags().testFlag(QNetworkInterface::IsRunning);

        QJsonArray v4, v6;
        for (const QNetworkAddressEntry& e : itf.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() == QAbstractSocket::IPv4Protocol)
                v4.append(a.toString());
            else if (a.protocol() == QAbstractSocket::IPv6Protocol)
                // Les adresses lien-local (fe80::) ne servent qu'au voisinage
                // immédiat : les publier encombrerait sans informer.
                if (!a.isLinkLocal())
                    v6.append(a.toString());
        }
        i["ipv4"] = v4;
        i["ipv6"] = v6;
        interfaces.append(i);
    }

    o["interfaces"] = interfaces;
    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

} // namespace morfmonitor
