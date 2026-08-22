/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/VersionMonitor.h"

#include "morfupdate/UpdateChecker.h"
#include "morfupdate/ReleaseInfo.h"
#include "morfupdate/Version.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>

namespace morfmonitor {

namespace {
qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
}

VersionMonitor::VersionMonitor(QString stateDir, int ttlMs, QObject* parent)
    : QObject(parent), m_stateDir(std::move(stateDir)), m_ttlMs(ttlMs) {
    load();
}

void VersionMonitor::setTargets(const QVector<Target>& targets) {
    m_targets.clear();
    for (const Target& t : targets)
        if (!t.repo.isEmpty())           // sans dépôt : aucune vérification distante
            m_targets.push_back(t);
}

void VersionMonitor::checkNow(bool force) {
    const qint64 now = nowMs();
    for (const Target& t : m_targets) {
        const Entry cached = m_entries.value(t.label);
        const bool never   = cached.lastCheckMs == 0;
        const bool expired = (now - cached.lastCheckMs) > m_ttlMs;
        if (!force && !never && !expired)
            continue;                    // cache encore frais : on ne refrappe pas GitHub
        Entry target;
        target.owner = t.owner.isEmpty() ? QStringLiteral("morfredus") : t.owner;
        target.repo  = t.repo;
        startCheck(t.label, target);
    }
}

void VersionMonitor::startCheck(const QString& label, const Entry& target) {
    if (m_inFlight.contains(label))
        return;                          // une vérification de ce service est en cours
    m_inFlight.insert(label);

    morfupdate::morfUpdateConfig cfg;
    cfg.owner              = target.owner;
    cfg.repo               = target.repo;
    cfg.currentVersion     = QStringLiteral("0.0.0"); // on ne veut que la dernière release
    cfg.includePrereleases = false;                   // release STABLE uniquement

    auto* checker = new morfupdate::UpdateChecker(cfg, this);

    // updateAvailable et upToDate portent tous deux la ReleaseInfo : dans les deux
    // cas on ne retient que la dernière release publiée (la comparaison à la
    // version EXÉCUTÉE se fait ailleurs, avec la vraie version du beacon).
    auto onLatest = [this, label, checker](const morfupdate::ReleaseInfo& info) {
        Entry& e = m_entries[label];
        e.latest        = info.version.toString();
        e.latestTag     = info.tag;
        e.url           = info.htmlUrl.toString();
        e.publishedAt   = info.publishedAt;
        e.lastCheckMs   = nowMs();
        e.lastSuccessMs = e.lastCheckMs;
        e.error.clear();
        // owner/repo mémorisés pour l'affichage même hors config rechargée.
        e.owner = checker->property("mu_owner").toString();
        e.repo  = checker->property("mu_repo").toString();
        m_inFlight.remove(label);
        checker->deleteLater();
        save();
    };
    connect(checker, &morfupdate::UpdateChecker::updateAvailable, this, onLatest);
    connect(checker, &morfupdate::UpdateChecker::upToDate,        this, onLatest);
    connect(checker, &morfupdate::UpdateChecker::checkFailed, this,
            [this, label, checker](const QString& err) {
        // Un échec NE DÉTRUIT PAS la dernière info connue : on garde `latest` et
        // `lastSuccessMs`, on note seulement l'échec courant et sa date.
        Entry& e = m_entries[label];
        e.lastCheckMs = nowMs();
        e.error       = err;
        m_inFlight.remove(label);
        checker->deleteLater();
        save();
    });

    // On range owner/repo sur l'objet pour les récupérer au succès (l'entrée peut
    // ne pas encore exister dans m_entries).
    checker->setProperty("mu_owner", target.owner);
    checker->setProperty("mu_repo",  target.repo);
    checker->checkForUpdates();
}

QJsonArray VersionMonitor::toJson(const QHash<QString, Running>& runningByApp) const {
    using morfupdate::Version;
    QJsonArray arr;
    for (const Target& t : m_targets) {
        const Entry e = m_entries.value(t.label);
        const QString joinApp = t.app.isEmpty() ? t.label : t.app;
        const Running run = runningByApp.value(joinApp);
        const QString running = run.version;
        const bool runningKnown = !running.isEmpty() && Version::parse(running).valid;
        const bool hasLatest = !e.latest.isEmpty();

        QString state;
        if (!hasLatest) {
            // Jamais de release obtenue : « non vérifié » tant qu'aucune tentative,
            // sinon « Vérification impossible ». Jamais « à jour » hors ligne.
            state = (e.lastCheckMs == 0) ? QStringLiteral("—")
                                         : QStringLiteral("Vérification impossible");
        } else if (!runningKnown) {
            state = QStringLiteral("Version inconnue");
        } else {
            const int c = Version::parse(running).compare(Version::parse(e.latest));
            state = (c < 0) ? QStringLiteral("Mise à jour disponible")
                  : (c > 0) ? QStringLiteral("Version locale plus récente")
                            : QStringLiteral("À jour");
        }
        // Échec courant alors qu'on avait déjà une release : résultat « périmé »,
        // à signaler (l'état reste calculé sur la dernière release connue).
        const bool stale = !e.error.isEmpty() && e.lastSuccessMs > 0;

        QJsonObject o;
        o["service"]        = t.label;
        o["project"]        = t.repo;
        o["repo"]           = t.repo;
        o["owner"]          = t.owner.isEmpty() ? QStringLiteral("morfredus") : t.owner;
        o["running"]        = running.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(running);
        o["running_host"]   = run.host.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(run.host);
        o["latest"]         = hasLatest ? QJsonValue(e.latest) : QJsonValue(QJsonValue::Null);
        o["latest_tag"]     = e.latestTag;
        o["url"]            = e.url;
        o["published_at"]   = e.publishedAt;
        o["state"]          = state;
        o["last_check_s"]   = static_cast<double>(e.lastCheckMs / 1000);
        o["last_success_s"] = static_cast<double>(e.lastSuccessMs / 1000);
        o["error"]          = e.error.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(e.error);
        o["stale"]          = stale;
        o["group"]          = t.group.isEmpty() ? QStringLiteral("service") : t.group;
        o["updatable"]      = t.updatable;
        if (!t.kind.isEmpty())
            o["kind"] = t.kind;
        arr.append(o);
    }
    return arr;
}

// --- Persistance : le dernier résultat survit au redémarrage (affichage immédiat
//     à l'ouverture, sans requête). -------------------------------------------

QString versionsFile(const QString& dir) { return QDir(dir).filePath(QStringLiteral("versions.json")); }

void VersionMonitor::save() const {
    if (m_stateDir.isEmpty())
        return;
    QDir().mkpath(m_stateDir);
    QJsonObject root;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        const Entry& e = it.value();
        root[it.key()] = QJsonObject{
            {"owner", e.owner}, {"repo", e.repo},
            {"latest", e.latest}, {"latest_tag", e.latestTag},
            {"url", e.url}, {"published_at", e.publishedAt},
            {"last_check_ms", static_cast<double>(e.lastCheckMs)},
            {"last_success_ms", static_cast<double>(e.lastSuccessMs)},
            {"error", e.error},
        };
    }
    QFile f(versionsFile(m_stateDir));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void VersionMonitor::load() {
    QFile f(versionsFile(m_stateDir));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.owner         = o.value("owner").toString();
        e.repo          = o.value("repo").toString();
        e.latest        = o.value("latest").toString();
        e.latestTag     = o.value("latest_tag").toString();
        e.url           = o.value("url").toString();
        e.publishedAt   = o.value("published_at").toString();
        e.lastCheckMs   = static_cast<qint64>(o.value("last_check_ms").toDouble());
        e.lastSuccessMs = static_cast<qint64>(o.value("last_success_ms").toDouble());
        e.error         = o.value("error").toString();
        m_entries.insert(it.key(), e);
    }
}

} // namespace morfmonitor
