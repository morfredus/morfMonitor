/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QObject>
#include <QString>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QVector>

namespace morfmonitor {

// -----------------------------------------------------------------------------
// VersionMonitor : compare la version EXÉCUTÉE d'un service (annoncée par
// morfBeacon, cf. MonitorModule) à la dernière RELEASE stable publiée sur GitHub.
//
// Il ne réimplémente AUCUNE logique GitHub : il réutilise morfUpdate (owner/repo
// -> dernière release). Il ne connaît pas Doctor. Il n'écrit rien : purement
// informatif.
//
// Responsabilité stricte : VersionMonitor ne met en cache que la partie DISTANTE
// (la release), la seule qui coûte une requête réseau. La version exécutée reste
// détenue par le beacon ; l'état de comparaison est calculé au moment de bâtir la
// réponse, en joignant les deux (Version::compare, sémantique).
//
// Cache et déclenchement (le réseau ne doit jamais ralentir la supervision) :
//   - à l'ouverture, on renvoie le dernier résultat en cache (persisté) ;
//   - une entrée de moins de `ttlMs` n'est pas revérifiée automatiquement ;
//   - une entrée expirée peut être revérifiée en arrière-plan (non bloquant) ;
//   - `checkNow` force une vérification fraîche, même cache encore valide ;
//   - un échec réseau N'EFFACE PAS la dernière release connue ni sa date de succès.
// -----------------------------------------------------------------------------
class VersionMonitor : public QObject {
    Q_OBJECT
public:
    // Un service à surveiller côté version (issu de morfsystem.json).
    struct Target {
        QString label;   // identité AFFICHÉE (colonne Service)
        QString app;     // nom beacon pour joindre la version exécutée (défaut = label)
        QString owner;   // propriétaire GitHub (défaut morfredus)
        QString repo;    // dépôt ; vide => pas de vérification
        QString group = QStringLiteral("service");  // service | ecosystem
        bool    updatable = true;                   // false : pas de bouton (agent, outils)
        QString kind;                               // library | tool (groupe ecosystem)
        // github_latest : /releases/latest (services, morfTools).
        // semver_tags   : refs/tags/vX.Y.Z (morfPackages : /latest et la
        //                 première page de /tags sont des index projet-v…).
        QString releaseMode = QStringLiteral("github_latest");
    };

    explicit VersionMonitor(QString stateDir, int ttlMs = 6 * 3600 * 1000,
                            QObject* parent = nullptr);

    void setTargets(const QVector<Target>& targets);   // depuis la config

    // Lance une vérification. `force` = true : toutes les cibles, même cache frais
    // (bouton « Vérifier les versions »). `force` = false : seulement les entrées
    // expirées (rafraîchissement d'arrière-plan à l'ouverture).
    void checkNow(bool force);

    // Version exécutée d'un service, avec l'hôte qui l'a annoncée : dans un onglet
    // « Services systemd » local, on veut la version de CETTE machine, et pouvoir
    // signaler le cas où elle provient d'un autre hôte.
    struct Running {
        QString version;
        QString host;
    };

    // Vue JSON par service, prête pour l'API. `runningByApp` = version exécutée
    // connue du beacon (nom d'app -> version+hôte). L'état est calculé ici
    // (Version::compare).
    QJsonArray toJson(const QHash<QString, Running>& runningByApp) const;

private:
    struct Entry {
        QString owner, repo;
        QString latest;         // version de la dernière release stable (ex. "0.5.1")
        QString latestTag;      // tag brut (ex. "v0.5.1")
        QString url;            // page de la release
        QString publishedAt;    // ISO 8601
        qint64  lastCheckMs   = 0;   // dernière tentative (succès ou échec)
        qint64  lastSuccessMs = 0;   // dernier succès (0 = jamais)
        QString error;          // message du dernier échec (vidé au succès)
    };

    void startCheck(const Target& target);
    void startGithubLatest(const Target& target);
    void startSemverTags(const Target& target);
    void save() const;
    void load();

    QString                    m_stateDir;
    int                        m_ttlMs;
    QVector<Target>            m_targets;
    QHash<QString, Entry>      m_entries;    // label -> résultat distant caché
    QSet<QString>              m_inFlight;   // labels en cours de vérification
};

} // namespace morfmonitor
