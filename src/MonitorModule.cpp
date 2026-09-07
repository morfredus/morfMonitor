/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/MonitorModule.h"

#include <QUdpSocket>
#include <QStandardPaths>
#include <QDir>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHostInfo>
#include <QFile>
#include <QSet>
#include <QTimer>
#include <QRegularExpression>
#include <QCoreApplication>

#ifndef MORFBEACON_VENDORED_VERSION
#  define MORFBEACON_VENDORED_VERSION ""
#endif
#ifndef MORFDEPLOY_VENDORED_VERSION
#  define MORFDEPLOY_VENDORED_VERSION ""
#endif

namespace morfmonitor {

namespace {

QString firstVersionLine(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

// morfUpdate n'emet pas de beacon : sa version executee se lit sur son /status.
//
// IMPORTANT : cette lecture est ASYNCHRONE, jamais bloquante. Une version
// precedente faisait ici un GET synchrone avec une QEventLoop imbriquee
// (loop.exec()). Or cette fonction est appelee depuis servicesJson(), en plein
// parcours de m_beaconSeen : la boucle imbriquee re-entrait dans l'event loop,
// laissait passer un datagramme beacon (onBeaconDatagram -> m_beaconSeen.insert),
// et l'insertion d'une NOUVELLE cle rehashait la table en invalidant l'iterateur
// du parcours en cours -> plantage (SIGSEGV). C'est pourquoi le service crashait
// en rafale au demarrage (nouvelles cles a chaque service qui s'annonce) puis se
// stabilisait (cles connues : insert = simple remplacement, sans rehash).
//
// On interroge donc /status en arriere-plan et on renvoie la derniere valeur
// connue ; a defaut, la version installee lue dans /opt/morfupdate/VERSION
// (immediate, sans reseau).
QString cachedMorfUpdateVersion() {
    static QString ver;
    static QElapsedTimer age;
    static bool armed    = false;
    static bool inFlight = false;

    // Rafraichissement en tache de fond quand le cache est vide ou perime (15 s),
    // sans jamais attendre la reponse : le parcours appelant n'est pas suspendu.
    const bool stale = !armed || age.elapsed() > 15000;
    if (stale && !inFlight) {
        inFlight = true;
        // Un seul QNetworkAccessManager pour toute la vie du process (thread
        // principal) : sa reutilisation evite de recreer une pile reseau a chaque
        // sonde. Les statics de la fonction sont accessibles depuis le lambda sans
        // capture (duree de vie statique) ; seul `reply`, local, est capture.
        static QNetworkAccessManager nam;
        QNetworkRequest req{QUrl(QStringLiteral("http://127.0.0.1:8794/status"))};
        req.setTransferTimeout(800);   // borne la requete SANS boucle imbriquee
        QNetworkReply* reply = nam.get(req);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply]() {
            reply->deleteLater();
            inFlight = false;
            armed    = true;
            age.restart();
            // On ne lit le corps QU'EN cas de succes : lire une reply en erreur
            // (ou abandonnee) declenche « QIODevice::read: device not open ».
            if (reply->error() == QNetworkReply::NoError
                && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
                const QString v = QJsonDocument::fromJson(reply->readAll())
                                      .object().value(QStringLiteral("version")).toString();
                if (!v.isEmpty())
                    ver = v;
            }
        });
    }

    if (!ver.isEmpty())
        return ver;
    // Repli immediat tant que la sonde n'a pas encore repondu : version installee.
    return firstVersionLine(QStringLiteral("/opt/morfupdate/VERSION"));
}

// Clone voisin : dossier du dépôt, ou même nom suivi d'un suffixe (copie de travail).
QString cloneVersion(const QString& repo) {
    if (repo.isEmpty())
        return {};
    QStringList roots;
    const QString env = qEnvironmentVariable("MORFSYSTEM_ROOT");
    if (!env.isEmpty())
        roots << env;
    roots << QDir::home().filePath(QStringLiteral("morfSystem"));
    roots << QDir::home().filePath(QStringLiteral("Codage/morfSystem"));
    roots << QDir::home().filePath(QStringLiteral("Codage/01-Travail"));
    QDir walk(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        roots << walk.absolutePath();
        if (!walk.cdUp())
            break;
    }
    const QString prefix = repo + QLatin1Char('_');
    for (const QString& root : roots) {
        QDir d(root);
        if (!d.exists())
            continue;
        QString v = firstVersionLine(d.filePath(repo + QStringLiteral("/VERSION")));
        if (!v.isEmpty())
            return v;
        for (const QString& name : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (name != repo && !name.startsWith(prefix))
                continue;
            v = firstVersionLine(d.filePath(name + QStringLiteral("/VERSION")));
            if (!v.isEmpty())
                return v;
        }
    }
    return {};
}

QString localEcosystemVersion(const EcosystemProjectDef& p) {
    if (p.local == QLatin1String("vendor_beacon"))
        return QString::fromUtf8(MORFBEACON_VENDORED_VERSION);
    if (p.local == QLatin1String("vendor_deploy"))
        return QString::fromUtf8(MORFDEPLOY_VENDORED_VERSION);
    return cloneVersion(p.repo);
}

} // namespace

MonitorModule::MonitorModule(const QString& id, QString configPath, QObject* parent)
    : IModule(id, QStringLiteral("monitor"), parent),
      m_configPath(std::move(configPath)) {}

MonitorModule::~MonitorModule() = default;

bool MonitorModule::start() {
    // Un échec de chargement n'empêche PAS le démarrage : le service doit
    // répondre et servir ce qu'il peut (système, ressources, réseau), même sans
    // liste de composants à superviser. Un superviseur qui refuse de démarrer
    // parce que sa configuration manque est un superviseur inutile au moment
    // précis où on en a le plus besoin.
    m_config.load(m_configPath);
    m_supervisor = std::make_unique<Supervisor>(&m_config);

    // Memoire persistante des machines : chargee au demarrage pour que le parc
    // deja connu reapparaisse immediatement, meme si aucune machine n'emet encore.
    m_machines.load(resolveStateDir());

    // Versions de services : reutilise morfUpdate (owner/repo -> derniere release).
    // Le cache persiste dans le dossier d'etat, si bien qu'a l'ouverture on affiche
    // immediatement le dernier resultat connu. TTL 6 h ; les cibles sont (re)lues
    // de la config dans servicesJson (elle peut arriver apres le demarrage).
    m_versions = std::make_unique<VersionMonitor>(resolveStateDir(), 6 * 3600 * 1000, this);

    // Memoire temporelle : chargee au demarrage (rattrapage, purge du brut > 48 h,
    // monitor_started/monitor_gap). Elle vit sous le meme dossier d'etat editable
    // que le registre des machines (/var/lib/morfmonitor via StateDirectory).
    m_memory.load(resolveStateDir(), m_config.beaconOfflineAfterS());

    m_beaconSocket = new QUdpSocket(this);
    // ShareAddress : d'autres programmes de la machine (le Dashboard en mode
    // dégradé, par exemple) écoutent le même port de diffusion.
    if (m_beaconSocket->bind(QHostAddress::AnyIPv4, m_config.beaconPort(),
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        connect(m_beaconSocket, &QUdpSocket::readyRead,
                this, &MonitorModule::onBeaconDatagram);
    }

    // Amorce la mesure CPU : /proc/stat ne donne que des compteurs cumules, si
    // bien que la premiere lecture ne peut produire aucun taux. Sans cette
    // amorce, la toute premiere requete a l'API renverrait un CPU absent — que
    // les clients afficheraient comme 0 %, une valeur FAUSSE et non « inconnue ».
    m_resources.collect();

    // Alerte de panne fonctionnelle : evaluation periodique (en memoire, non
    // bloquante). 30 s suffisent — l'anti-rebond (voir evaluateFunctionalAlerts)
    // exige de toute facon une panne SOUTENUE avant d'alerter.
    m_alertTimer = new QTimer(this);
    m_alertTimer->setInterval(30 * 1000);
    connect(m_alertTimer, &QTimer::timeout, this, &MonitorModule::evaluateFunctionalAlerts);
    // Meme cadence pour alimenter la memoire temporelle : les transitions d'etat
    // du parc y deviennent des evenements structures, sans aucune sonde nouvelle.
    connect(m_alertTimer, &QTimer::timeout, this, &MonitorModule::feedMemory);
    m_alertTimer->start();

    m_running = true;
    return true;
}

void MonitorModule::stop() {
    m_running = false;
    if (m_alertTimer) {
        m_alertTimer->stop();
        m_alertTimer = nullptr;   // detruit par l'arbre QObject
    }
    // Persiste la derniere annonce de chaque machine avant de partir : au
    // prochain demarrage, « vu il y a ... » repart d'une base fraiche.
    m_machines.flush();
    // Persiste la memoire temporelle (agregats, curseur, episodes ouverts) : un
    // incident en cours et l'histoire consolidee survivent a l'arret.
    m_memory.flush();
    if (m_beaconSocket) {
        m_beaconSocket->close();
        m_beaconSocket = nullptr; // détruit par l'arbre QObject
    }
}

// Dossier d'etat inscriptible. Sous systemd, `StateDirectory=morfmonitor` fournit
// /var/lib/morfmonitor et exporte STATE_DIRECTORY : on l'honore en priorite, dans
// le respect de la doctrine du parc (config en lecture seule sous /etc, etat
// editable sous /var/lib). A defaut (execution hors service, developpement,
// Windows), on retombe sur l'emplacement de donnees applicatif standard.
QString MonitorModule::resolveStateDir() const {
    const QByteArray env = qgetenv("STATE_DIRECTORY");
    if (!env.isEmpty()) {
        // systemd peut en lister plusieurs, separes par « : » ; on prend le premier.
        const QString first = QString::fromLocal8Bit(env).split(QLatin1Char(':')).value(0);
        if (!first.isEmpty())
            return first;
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return QString();   // aucun emplacement : persistance desactivee, sans echec
    return QDir(base).filePath(QStringLiteral("morfmonitor"));
}

bool MonitorModule::forgetMachine(const QString& host) {
    return m_machines.forget(host);
}

qint64 MonitorModule::uptimeSeconds() const {
    QFile f(QStringLiteral("/proc/uptime"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    return static_cast<qint64>(QString::fromUtf8(f.readAll())
                                   .split(QLatin1Char(' ')).value(0).toDouble());
}

bool MonitorModule::isFresh(const Cached& c, int maxAgeMs) const {
    return c.valid && c.age.isValid() && c.age.elapsed() < maxAgeMs;
}

// --- Heartbeats morfBeacon ---------------------------------------------------

void MonitorModule::fetchDetailIfNeeded(const QString& key) {
    const auto it = m_beaconSeen.find(key);
    if (it == m_beaconSeen.end() || it->detailFetched)
        return;
    if (it->sourceIp.isEmpty() || it->statusPort == 0)
        return;                       // pas de /status a joindre : rien a demander

    // Back-off : tant que le delai depuis le dernier echec n'est pas ecoule, on
    // ne retente pas. Un pair sain repond du premier coup (detailTries reste 0,
    // aucun delai) ; seul un pair en difficulte est espace, ce qui le soulage au
    // lieu de l'achever.
    if (QDateTime::currentMSecsSinceEpoch() < it->detailRetryAfter)
        return;

    // Marque avant l'envoi : sans cela, chaque heartbeat relancerait une requete
    // tant que la premiere n'a pas repondu.
    it->detailFetched = true;

    if (!m_http)
        m_http = new QNetworkAccessManager(this);

    const QUrl url(QStringLiteral("http://%1:%2/status").arg(it->sourceIp).arg(it->statusPort));
    QNetworkRequest req(url);
    req.setTransferTimeout(3000);     // un service lent ne doit pas retenir la supervision
    QNetworkReply* reply = m_http->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, key, reply]() {
        reply->deleteLater();
        const auto entry = m_beaconSeen.find(key);
        if (entry == m_beaconSeen.end())
            return;
        if (reply->error() != QNetworkReply::NoError) {
            // Echec : on reautorise une tentative, mais plus tard et de plus en
            // plus tard. 30 s, 60 s, 120 s ... plafonne a 10 min. Sans cet
            // espacement, deux observateurs re-sondaient un pair defaillant
            // toutes les 15 s chacun, entretenant sa panne.
            entry->detailFetched = false;
            entry->detailTries  += 1;
            const qint64 base  = 30000;   // 30 s
            const int    shift = qMin(entry->detailTries - 1, 5);
            const qint64 delay = qMin(base << shift, static_cast<qint64>(600000));
            entry->detailRetryAfter = QDateTime::currentMSecsSinceEpoch() + delay;
            return;
        }
        // Succes : on efface le back-off et on lit le detail. Le meme /status
        // porte les deux : l'interface (si le service en a une) et l'API. Un
        // service sans interface laisse simplement web_ui vide.
        entry->detailTries      = 0;
        entry->detailRetryAfter = 0;
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        entry->webUi    = o.value(QStringLiteral("web_ui")).toObject();
        entry->api      = o.value(QStringLiteral("api")).toObject();
        entry->hardware = o.value(QStringLiteral("hardware")).toObject();
    });
}

void MonitorModule::fetchActivityIfStale(const QString& key) {
    const auto it = m_beaconSeen.find(key);
    if (it == m_beaconSeen.end())
        return;
    if (it->sourceIp.isEmpty() || it->statusPort == 0)
        return;                       // pas de /status a joindre

    // On n'observe l'activite que d'un service EN LIGNE : une activite d'un pair
    // disparu n'a pas de sens, et cela evite de solliciter une cible absente.
    const qint64 nowS = QDateTime::currentSecsSinceEpoch();
    if (nowS - it->lastSeen > m_config.beaconOfflineAfterS())
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs < it->activityRetryAfter)
        return;                       // back-off apres un echec recent
    if (it->activityAt != 0 && nowMs - it->activityAt < 5000)
        return;                       // deja frais : au plus une fois toutes les 5 s
    it->activityAt = nowMs;           // marque avant l'envoi : pas de requetes en double

    if (!m_http)
        m_http = new QNetworkAccessManager(this);

    const QUrl url(QStringLiteral("http://%1:%2/status").arg(it->sourceIp).arg(it->statusPort));
    QNetworkRequest req(url);
    req.setTransferTimeout(3000);
    QNetworkReply* reply = m_http->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, key, reply]() {
        reply->deleteLater();
        const auto entry = m_beaconSeen.find(key);
        if (entry == m_beaconSeen.end())
            return;
        if (reply->error() != QNetworkReply::NoError) {
            // Echec : on garde la derniere activite connue et on espace la reprise.
            entry->activityRetryAfter = QDateTime::currentMSecsSinceEpoch() + 30000;
            return;
        }
        // `activity` absent ou state:"idle" => rien en cours : on stocke l'objet
        // tel quel (vide ou idle) et activitiesJson n'affichera pas de ligne.
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        entry->activity           = o.value(QStringLiteral("activity")).toObject();
        entry->activityRetryAfter = 0;
        entry->activityAt         = QDateTime::currentMSecsSinceEpoch();

        // Rafraichir aussi l'ETAT MATERIEL : il est VOLATILE (un capteur s'initialise
        // apres le boot, se branche/debranche, se degrade), contrairement a
        // l'interface web et a la liste d'API qui sont stables. Le pull unique par
        // version (fetchDetailIfNeeded) pouvait figer un « absent » capte pendant le
        // demarrage du service : un morfMonitor distant montrait alors « capteur
        // absent » quand le local, sonde plus tard, voyait « present ». En le
        // relisant a chaque re-sondage, l'etat materiel suit le service dans le temps.
        // morfMonitor n'infere jamais cet etat : il recopie ce que le service declare.
        if (o.contains(QStringLiteral("hardware")))
            entry->hardware = o.value(QStringLiteral("hardware")).toObject();
    });
}

QJsonArray MonitorModule::activitiesJson(qint64 nowSecs) const {
    // Contrat generique `activity/1` : une ligne par service EN LIGNE qui declare
    // une activite en cours. Indexation morfPhoto, compilation morfDeploy, collecte
    // morfCollector... la representation est la meme. morfMonitor n'ajoute que de
    // quoi identifier la source (service, host) ; le reste vient du service.
    QJsonArray arr;
    for (auto it = m_beaconSeen.constBegin(); it != m_beaconSeen.constEnd(); ++it) {
        const BeaconSeen& s = it.value();
        if (s.activity.isEmpty())
            continue;
        if (s.activity.value(QStringLiteral("state")).toString() == QLatin1String("idle"))
            continue;
        if (nowSecs - s.lastSeen > m_config.beaconOfflineAfterS())
            continue;                 // service hors ligne : pas d'activite fantome
        QJsonObject a = s.activity;   // type, state, started_at, current, total, progress_percent, detail
        a["service"] = s.app;
        a["host"]    = s.host;
        arr.append(a);
    }
    return arr;
}

// --- Choix de l'adresse d'un emetteur multi-domicilie ------------------------
//
// Un service diffuse sur TOUTES les interfaces de sa machine. Un poste Windows
// avec WSL ou Hyper-V, un portable sous VPN, en ont plusieurs — et le dernier
// datagramme recu gagnait, si bien que morfMonitor retenait volontiers
// « 172.24.224.1 » (reseau virtuel) pour une machine joignable en
// « 192.168.1.14 ». L'adresse restait exacte du point de vue de la couche
// reseau, mais le lien affiche etait inutilisable depuis toute autre machine.
//
// L'etalon : l'adresse par laquelle NOUS sortons, c'est-a-dire l'interface qui
// porte la route par defaut. Une adresse d'emetteur situee dans ce meme reseau
// est celle par laquelle on peut reellement le joindre.
void MonitorModule::refreshPrimaryAddress() {
    // Recalculee de loin en loin : une adresse DHCP change, une interface
    // apparait. Assez rare pour ne rien couter, assez frequent pour suivre.
    if (m_primaryPrefix >= 0 && m_primaryAge.isValid() && m_primaryAge.elapsed() < 60000)
        return;

    // « Connecter » une socket UDP n'emet AUCUN paquet : cela ne fait que fixer
    // la route, apres quoi localAddress() revele l'adresse que le systeme
    // utiliserait pour sortir. C'est le seul moyen portable de distinguer le
    // vrai LAN d'un reseau virtuel, que rien d'autre ne differencie.
    // La cible est une adresse reservee a la documentation (RFC 5737), jamais
    // routee : rien ne quitte la machine, ici comme ailleurs.
    QUdpSocket probe;
    probe.connectToHost(QHostAddress(QStringLiteral("192.0.2.1")), 9,
                        QIODevice::ReadOnly);
    const QHostAddress local = probe.localAddress();
    probe.close();
    if (local.isNull() || local.protocol() != QAbstractSocket::IPv4Protocol)
        return;

    for (const QNetworkInterface& itf : QNetworkInterface::allInterfaces()) {
        for (const QNetworkAddressEntry& entry : itf.addressEntries()) {
            if (entry.ip() == local && entry.prefixLength() > 0) {
                m_primaryAddress = local;
                m_primaryPrefix  = entry.prefixLength();
                m_primaryAge.restart();
                return;
            }
        }
    }
}

int MonitorModule::addressScore(const QHostAddress& candidate) {
    refreshPrimaryAddress();
    // Sans etalon (machine sans route par defaut), toutes les adresses se
    // valent : on ne prefere rien plutot que de preferer au hasard.
    if (m_primaryPrefix < 0 || candidate.isNull())
        return 1;
    return candidate.isInSubnet(m_primaryAddress, m_primaryPrefix) ? 2 : 1;
}

void MonitorModule::onBeaconDatagram() {
    while (m_beaconSocket && m_beaconSocket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_beaconSocket->receiveDatagram();
        const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        if (o.value(QStringLiteral("proto")).toString() != QLatin1String("morfbeacon/1"))
            continue;

        const QString app = o.value(QStringLiteral("app")).toString();
        if (app.isEmpty())
            continue;

        // On enregistre TOUTES les applications entendues, même absentes de la
        // configuration : l'API peut ainsi révéler ce qui tourne réellement sur
        // le réseau, ce qui aide à peupler la configuration.
        BeaconSeen s;
        s.lastSeen   = QDateTime::currentSecsSinceEpoch();
        s.app        = app;
        s.instance   = o.value(QStringLiteral("instance")).toString();
        s.version    = o.value(QStringLiteral("version")).toString();
        s.host       = o.value(QStringLiteral("host")).toString();
        // Role de l'emetteur. Absent (annonce d'avant morfBeacon 0.7.0) => "host",
        // le defaut historique : toute annonce etait un service sur une machine.
        s.role       = o.value(QStringLiteral("role")).toString(QStringLiteral("host"));
        s.state      = o.value(QStringLiteral("state")).toString();
        s.statusPort = static_cast<quint16>(o.value(QStringLiteral("status_port")).toInt());

        // L'adresse de l'emetteur vient de la COUCHE RESEAU, pas du datagramme :
        // c'est la seule valeur dont on soit sur qu'elle permette de le joindre
        // depuis ici.
        s.sourceIp = dg.senderAddress().toString();
        // Qt prefixe les adresses IPv4 mappees en IPv6 (« ::ffff:192.168.1.55 ») ;
        // un lien construit tel quel serait inutilisable.
        if (const int i = s.sourceIp.lastIndexOf(QLatin1Char(':')); i >= 0 &&
            s.sourceIp.startsWith(QLatin1String("::ffff:")))
            s.sourceIp = s.sourceIp.mid(i + 1);
        s.addressScore = addressScore(QHostAddress(s.sourceIp));

        for (const QJsonValue& c : o.value(QStringLiteral("capabilities")).toArray())
            s.capabilities << c.toString();

        // La clef est l'IDENTITE D'INSTANCE, pas le nom : deux machines qui font
        // tourner le meme service sont deux entrees, et chacune vit sa vie. Un
        // emetteur qui n'annonce pas `instance` (version anterieure du
        // protocole) en recoit une derivee de son adresse — moins stable qu'un
        // nom d'hote, mais qui distingue tout autant les machines.
        const QString key = s.instance.isEmpty()
            ? app + QLatin1Char('@') + s.sourceIp : s.instance;

        // Une entree deja connue conserve le detail deja recupere : inutile de
        // reinterroger /status a chaque heartbeat. C'est le sens de
        // « push presence / pull detail » — la presence est bavarde, le detail
        // ne se demande qu'une fois.
        if (const auto it = m_beaconSeen.constFind(key); it != m_beaconSeen.constEnd()) {
            s.webUi         = it->webUi;
            s.api           = it->api;
            s.hardware      = it->hardware;
            const bool sameVersion = (it->version == s.version);
            s.detailFetched = it->detailFetched && sameVersion;
            // On conserve le back-off tant que la version ne change pas. Une
            // nouvelle version est un service a redecouvrir : on repart a zero,
            // sans le delai herite de l'ancienne.
            s.detailTries      = sameVersion ? it->detailTries      : 0;
            s.detailRetryAfter = sameVersion ? it->detailRetryAfter : 0;

            // Emetteur multi-domicilie : on garde la MEILLEURE adresse entendue,
            // pas la derniere. Les diffusions arrivent par chaque interface, et
            // celle du reseau virtuel arrivant apres celle du LAN suffisait a
            // faire afficher une adresse que personne d'autre ne peut joindre.
            // La presence, elle, est bien rafraichie : seule l'adresse resiste.
            if (it->addressScore > s.addressScore && !it->sourceIp.isEmpty()) {
                s.sourceIp     = it->sourceIp;
                s.addressScore = it->addressScore;
            }
        }
        m_beaconSeen.insert(key, s);

        // Apprentissage du parc : un heartbeat de POSTE (role « host ») memorise
        // sa machine dans le registre persistant. Les equipements (« device ») ne
        // sont pas des machines et n'y entrent pas. Aucune declaration manuelle :
        // le parc se construit a partir de ce qui s'annonce reellement.
        if (s.role != QLatin1String("device") && !s.host.isEmpty())
            m_machines.observe(s.host, s.lastSeen);

        fetchDetailIfNeeded(key);
    }
    pruneStaleBeacons();
}

// Les instances entendues sont conservees pour la DECOUVERTE : brancher un
// service et le voir apparaitre indique quoi ajouter a la configuration. Passe
// un certain temps, cet interet disparait et l'entree devient du bruit — une
// application lancee une fois puis fermee serait listee « hors ligne »
// indefiniment, et la table ne cesserait de croitre.
//
// Les instances des applications DECLAREES sont purgees comme les autres : ce
// n'est plus l'entree entendue qui garantit la visibilite d'une absence, c'est
// la DECLARATION elle-meme — beaconAppsJson emet une ligne « hors ligne » pour
// toute application declaree dont aucune instance ne se fait entendre. Une
// machine d'essai eteinte pour de bon finit donc par disparaitre de la liste,
// au lieu d'y rester en panne perpetuelle.
void MonitorModule::pruneStaleBeacons() {
    // Assez long pour qu'une decouverte reste visible le temps de l'exploiter,
    // assez court pour qu'une presence ancienne ne se fasse pas passer pour une
    // panne actuelle.
    constexpr qint64 kKeepStaleS = 3600;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = m_beaconSeen.begin(); it != m_beaconSeen.end();) {
        if ((now - it->lastSeen) > kKeepStaleS)
            it = m_beaconSeen.erase(it);
        else
            ++it;
    }
}

void MonitorModule::addReachability(QJsonObject& a, const BeaconSeen& s) {
    if (!s.sourceIp.isEmpty())  a["ip"] = s.sourceIp;
    if (s.statusPort != 0)      a["status_port"] = static_cast<int>(s.statusPort);
    if (!s.capabilities.isEmpty()) {
        QJsonArray caps;
        for (const QString& c : s.capabilities) caps.append(c);
        a["capabilities"] = caps;
    }
    // Detail de l'interface Web, complete de l'adresse a laquelle l'ouvrir :
    // le consommateur ne doit pas avoir a recomposer une URL lui-meme.
    if (!s.webUi.isEmpty() && !s.sourceIp.isEmpty()) {
        QJsonObject ui = s.webUi;
        const int port = ui.value("port").toInt(s.statusPort);
        ui["url"] = QStringLiteral("http://%1:%2%3")
                        .arg(s.sourceIp).arg(port)
                        .arg(ui.value("path").toString(QStringLiteral("/")));
        a["web_ui"] = ui;
    }
    // API annoncee, completee de la base a laquelle l'atteindre (meme principe
    // que web_ui : referencer, sans recomposer). Les chemins des endpoints
    // restent relatifs ; `base_url` donne le prefixe joignable depuis ici.
    if (!s.api.isEmpty() && !s.sourceIp.isEmpty()) {
        QJsonObject api = s.api;
        api["base_url"] = QStringLiteral("http://%1:%2")
                              .arg(s.sourceIp).arg(s.statusPort);
        a["api"] = api;
    }
    // Etat du materiel rapporte par le service (present/none/degraded + label),
    // relaye TEL QUEL. morfMonitor ne deduit jamais la presence du materiel :
    // absence de bloc => rien a afficher, ce n'est pas une anomalie.
    if (!s.hardware.isEmpty())
        a["hardware"] = s.hardware;
}

QJsonObject MonitorModule::beaconAppsJson() const {
    QJsonObject o;
    QJsonArray arr;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int offlineAfter = m_config.beaconOfflineAfterS();

    // Presence par hote : un poste generaliste ("host") est considere en ligne
    // tant qu'au moins un de ses services host emet un heartbeat frais. Un poste
    // eteint voit donc TOUS ses services disparaitre ensemble, ce qui permet a un
    // consommateur de le presenter comme UNE machine hors ligne plutot que N
    // pannes independantes. Les equipements ("device") ne rendent pas un hote
    // present : un ESP32 vit sa propre presence, il n'est pas une machine hote.
    QSet<QString> onlineHosts;
    for (auto it = m_beaconSeen.constBegin(); it != m_beaconSeen.constEnd(); ++it) {
        if (it->role == QLatin1String("device"))
            continue;
        if (!it->host.isEmpty() && (now - it->lastSeen) < offlineAfter)
            onlineHosts.insert(it->host);
    }

    // Une entree par INSTANCE entendue : le meme service tournant sur deux
    // machines produit deux lignes, chacune avec son hote, son adresse et son
    // heartbeat. Regroupees sous un seul nom, elles s'ecrasaient l'une l'autre
    // et l'affichage alternait entre les machines a chaque annonce.
    const auto fill = [&](QJsonObject& a, const BeaconSeen& s) {
        const qint64 age = now - s.lastSeen;
        // « online » dit ce qu'on ENTEND, jamais ce qu'on a décidé d'écouter.
        // Le `enabled &&` qui figurait ici rendait invisible un service qui
        // émettait : ComponentHub s'affichait « désactivé » avec un heartbeat
        // de neuf secondes. `enabled` dit si une absence doit alerter, et
        // c'est le consommateur qui combine les deux faits.
        a["online"]      = age < offlineAfter;
        a["last_seen_s"] = static_cast<double>(age);
        a["instance"]    = s.instance.isEmpty()
                               ? s.app + QLatin1Char('@') + s.sourceIp : s.instance;
        a["version"]     = s.version;
        a["host"]        = s.host;
        a["role"]        = s.role.isEmpty() ? QStringLiteral("host") : s.role;
        // host_online : l'hote de CETTE entree est-il present ? Il distingue
        // « service en panne sur une machine vivante » (vraie anomalie) de
        // « machine entierement eteinte » (un seul fait, a presenter comme tel).
        // Pour un device, l'hote est l'equipement lui-meme : la notion se confond
        // alors avec sa propre presence.
        a["host_online"] = (s.role == QLatin1String("device"))
                               ? (age < offlineAfter)
                               : onlineHosts.contains(s.host);
        a["state"]       = s.state;
        addReachability(a, s);
    };

    // Resout la declaration qui COUVRE une instance : un PLACEMENT exact (meme app
    // ET meme hote) l'emporte sur une PRESENCE (meme app, hote non precise). Sans
    // declaration, l'instance est « non declaree » (outil de decouverte).
    const auto declFor = [&](const QString& app,
                             const QString& host) -> const BeaconAppDef* {
        const BeaconAppDef* presence = nullptr;
        for (const BeaconAppDef& d : m_config.beaconApps()) {
            if (d.app != app)
                continue;
            if (!d.host.isEmpty()) {
                if (d.host == host)
                    return &d;          // placement exact : la meilleure couverture
            } else if (!presence) {
                presence = &d;          // presence : couverture de repli
            }
        }
        return presence;
    };

    // 1. Une ligne par INSTANCE entendue. Chaque instance porte l'etat declaratif
    //    (enabled/declared) de la declaration qui la couvre. Le meme service sur
    //    deux machines fait deux lignes, chacune avec son hote et sa presence.
    for (auto it = m_beaconSeen.constBegin(); it != m_beaconSeen.constEnd(); ++it) {
        const BeaconAppDef* d = declFor(it->app, it->host);
        QJsonObject a;
        a["app"]      = it->app;
        a["label"]    = d ? d->label : it->app;
        a["declared"] = (d != nullptr);
        if (d)
            a["enabled"] = d->enabled;
        fill(a, *it);
        arr.append(a);
    }

    // 2. Les declarations INSATISFAITES : attendues, mais qu'aucune instance ne
    //    couvre. La declaration a elle seule produit alors une ligne « hors ligne »,
    //    pour que l'absence se voie au lieu de disparaitre en silence.
    //      - PRESENCE (host vide) : insatisfaite si AUCUNE instance de l'app n'est
    //        entendue, ou qu'elle soit -> ligne sans hote (« introuvable »).
    //      - PLACEMENT (host precis) : insatisfaite si aucune instance de l'app sur
    //        CET hote -> ligne portant l'hote et sa presence, pour distinguer
    //        « absent d'une machine vivante » (anomalie) de « machine eteinte ».
    for (const BeaconAppDef& d : m_config.beaconApps()) {
        bool satisfied = false;
        for (auto it = m_beaconSeen.constBegin(); it != m_beaconSeen.constEnd(); ++it) {
            if (it->app != d.app)
                continue;
            if (d.host.isEmpty() || d.host == it->host) {
                satisfied = true;
                break;
            }
        }
        if (satisfied)
            continue;
        QJsonObject a;
        a["app"]      = d.app;
        a["label"]    = d.label;
        a["enabled"]  = d.enabled;
        a["declared"] = true;
        a["online"]   = false;
        if (!d.host.isEmpty()) {
            a["host"]        = d.host;
            a["role"]        = QStringLiteral("host");
            a["host_online"] = onlineHosts.contains(d.host);
        }
        arr.append(a);
    }

    o["apps"] = arr;
    o["offline_after_s"] = offlineAfter;
    return o;
}

// --- Alerte de panne fonctionnelle -------------------------------------------

namespace {
// Panne SOUTENUE avant d'alerter : un service qui redemarre vite (mise a jour,
// reboot) ne doit pas declencher d'alerte. Deux minutes = large au-dela d'un
// redemarrage normal, sous la barre d'une vraie indisponibilite.
constexpr qint64 kMinFailureDurationS = 120;
// Ne pas repeter la meme alerte avant ce delai (une panne longue = un cri, pas cent).
constexpr qint64 kAlertCooldownS = 6 * 3600;
} // namespace

QStringList MonitorModule::alertTargets() const {
    const QString env = qEnvironmentVariable("MORF_ALERT_TARGETS").trimmed();
    if (!env.isEmpty()) {
        QStringList t;
        for (const QString& s : env.split(QLatin1Char(','), Qt::SkipEmptyParts))
            t << s.trimmed();
        if (!t.isEmpty())
            return t;
    }
    QFile f(QStringLiteral("/etc/morfsystem/alert-targets"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QStringList t;
        const QString body = QString::fromUtf8(f.readAll());
        for (const QString& s : body.split(QRegularExpression(QStringLiteral("[,\\n]")),
                                           Qt::SkipEmptyParts))
            if (!s.trimmed().isEmpty())
                t << s.trimmed();
        if (!t.isEmpty())
            return t;
    }
    return {QStringLiteral("telegram")};   // repli raisonnable (canal de Fred)
}

void MonitorModule::pushNotification(const QString& title, const QString& message,
                                     const QString& level) {
    if (!m_http)
        m_http = new QNetworkAccessManager(this);

    QJsonObject payload;
    payload["title"]   = title;
    payload["message"] = message;
    payload["level"]   = level;
    payload["targets"] = QJsonArray::fromStringList(alertTargets());

    // morfNotify ecoute en local (port 8789 du parc) ; surchargeable pour un cas
    // particulier. POST asynchrone, best-effort : on ne bloque JAMAIS la boucle
    // d'evenements (une notification ratee ne doit pas nuire a la supervision).
    QString url = qEnvironmentVariable("MORFNOTIFY_URL").trimmed();
    if (url.isEmpty())
        url = QStringLiteral("http://127.0.0.1:8789/notify");

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    req.setTransferTimeout(5000);
    QNetworkReply* reply = m_http->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void MonitorModule::evaluateFunctionalAlerts() {
    const qint64 nowS = QDateTime::currentSecsSinceEpoch();

    // Etat courant du parc, tel que morfMonitor le calcule deja pour l'affichage :
    // on ne reinvente aucune logique de sante, on la CONSOMME (coherence garantie).
    const QJsonArray apps = beaconAppsJson().value(QStringLiteral("apps")).toArray();

    QSet<QString> failingNow;
    for (const QJsonValue& v : apps) {
        const QJsonObject a = v.toObject();
        // Panne FONCTIONNELLE = service DECLARE (donc attendu), absent des annonces,
        // alors que sa MACHINE est en ligne. Un poste entier hors ligne (host_online
        // faux) n'est PAS traite ici service par service : ce serait une panne
        // machine, pas un dysfonctionnement de service. Un equipement (role device)
        // absent a host_online faux : ecarte aussi, conforme a la doctrine materiel.
        const bool declared    = a.value(QStringLiteral("declared")).toBool();
        const bool online      = a.value(QStringLiteral("online")).toBool();
        const bool hostOnline  = a.value(QStringLiteral("host_online")).toBool();
        if (!(declared && !online && hostOnline))
            continue;

        const QString label = a.value(QStringLiteral("label")).toString(
            a.value(QStringLiteral("app")).toString());
        const QString host  = a.value(QStringLiteral("host")).toString();
        const QString inst  = a.value(QStringLiteral("instance")).toString(
            label + QLatin1Char('@') + host);
        failingNow.insert(inst);

        FailureState& st = m_failureState[inst];
        if (st.sinceS == 0) {
            st.sinceS = nowS;   // premiere fois vu en panne : demarre l'anti-rebond
            st.label  = label;
            st.host   = host;
        }
        const bool sustained   = (nowS - st.sinceS) >= kMinFailureDurationS;
        const bool cooldownOk  = !st.notified || (nowS - st.notifiedAtS) >= kAlertCooldownS;
        if (sustained && cooldownOk) {
            pushNotification(
                QStringLiteral("morfSystem"),
                QStringLiteral("%1 ne repond plus sur %2 : le service n'annonce plus "
                               "sa presence alors que la machine est en ligne "
                               "(bloque ou arrete). Panne fonctionnelle detectee par "
                               "morfMonitor.").arg(label, host.isEmpty() ? QStringLiteral("?") : host),
                QStringLiteral("error"));
            st.notified    = true;
            st.notifiedAtS = nowS;
        }
    }

    // Retour a la normale : un service qui reapparait apres avoir ete signale.
    for (auto it = m_failureState.begin(); it != m_failureState.end(); ) {
        if (failingNow.contains(it.key())) {
            ++it;
            continue;
        }
        if (it->notified) {
            pushNotification(
                QStringLiteral("morfSystem"),
                QStringLiteral("%1 est de nouveau en ligne sur %2.")
                    .arg(it->label, it->host.isEmpty() ? QStringLiteral("?") : it->host),
                QStringLiteral("success"));
        }
        it = m_failureState.erase(it);
    }
}

// --- Alimentation de la memoire temporelle -----------------------------------

void MonitorModule::feedMemory() {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int offlineAfter = m_config.beaconOfflineAfterS();

    // Etat systemd LOCAL (preuve de vie/cycle de vie OS). Reutilise le cache ; ne
    // relance systemctl qu'a l'expiration, exactement comme servicesJson.
    if (!isFresh(m_cSystemd, m_config.systemdRefreshMs())) {
        m_cSystemd.value = m_supervisor ? m_supervisor->collectSystemd() : QJsonObject{};
        m_cSystemd.age.restart();
        m_cSystemd.valid = true;
    }
    const QJsonArray sysArr = m_cSystemd.value.value(QStringLiteral("services")).toArray();
    const QHash<QString, qint64> beaconAge = beaconAgeByLocalApp(now);

    // Index systemd par application (unite -> app), enrichi de l'etat « muet »
    // (actif mais heartbeat tu) exactement comme le calcule servicesJson : pur
    // croisement d'observations deja en main.
    struct Sys { bool active = false; QString state; bool stuck = false; qint64 restarts = -1; };
    QHash<QString, QString> appByUnit;
    for (const SystemdServiceDef& s : m_config.systemdServices())
        appByUnit.insert(s.unit, s.app.isEmpty() ? s.label : s.app);
    QHash<QString, Sys> sysByApp;
    for (const QJsonValue& v : sysArr) {
        const QJsonObject o = v.toObject();
        const QString app = appByUnit.value(o.value(QStringLiteral("unit")).toString());
        if (app.isEmpty())
            continue;
        Sys si;
        si.active = o.value(QStringLiteral("active")).toBool();
        si.state  = o.value(QStringLiteral("state")).toString();
        if (o.contains(QStringLiteral("restarts")))
            si.restarts = static_cast<qint64>(o.value(QStringLiteral("restarts")).toDouble());
        if (si.active && beaconAge.contains(app))
            si.stuck = beaconAge.value(app) >= offlineAfter;   // actif mais muet
        sysByApp.insert(app, si);
    }

    // Instantane par service DECLARE, a partir de la vue beacon deja assemblee
    // (une ligne par instance, plus les declarations insatisfaites en « hors ligne »).
    const QString localHost = QHostInfo::localHostName();
    QVector<EventMemory::Observation> obs;
    const QJsonArray apps = beaconAppsJson().value(QStringLiteral("apps")).toArray();
    for (const QJsonValue& v : apps) {
        const QJsonObject a = v.toObject();
        if (!a.value(QStringLiteral("declared")).toBool())
            continue;                       // seul un service ATTENDU fait incident
        EventMemory::Observation ob;
        ob.service         = a.value(QStringLiteral("app")).toString();
        ob.host            = a.value(QStringLiteral("host")).toString();
        ob.instance        = a.value(QStringLiteral("instance")).toString();
        if (ob.instance.isEmpty())
            ob.instance = ob.host.isEmpty() ? ob.service
                                            : ob.service + QLatin1Char('@') + ob.host;
        ob.declared        = true;
        ob.heartbeatOnline = a.value(QStringLiteral("online")).toBool();
        // host_online absent (declaration « presence » sans hote) => on suppose la
        // machine en ligne : sans hote, on ne peut pas affirmer qu'elle est eteinte.
        ob.hostOnline      = a.value(QStringLiteral("host_online")).toBool(true);
        if (a.contains(QStringLiteral("last_seen_s")))
            ob.lastSeen = now - static_cast<qint64>(a.value(QStringLiteral("last_seen_s")).toDouble());

        // Chaque morfMonitor n'est l'observateur QUE de son hote : on ne suit que
        // les services de CETTE machine. Ainsi pas de double comptage entre les
        // morfMonitor du parc (chacun tient la memoire de son hote, morfAnalytics
        // les reunira), et les applications itinerantes sans hote (PhotoHub,
        // ComponentHub, SiteWatch...) -- qui ne sont pas des daemons -- ne
        // generent aucun incident de disponibilite.
        const bool isLocal = !ob.host.isEmpty()
            && ob.host.compare(localHost, Qt::CaseInsensitive) == 0;
        if (!isLocal)
            continue;

        // Enrichissement cycle de vie OS (systemd), disponible pour cet hote local.
        if (sysByApp.contains(ob.service)) {
            const Sys& si = sysByApp.value(ob.service);
            ob.hasLifecycle  = true;
            ob.systemdActive = si.active;
            ob.systemdState  = si.state;
            ob.stuck         = si.stuck;
            ob.nRestarts     = si.restarts;
        }
        obs.push_back(ob);
    }

    m_memory.observe(obs, now);
}

QJsonObject MonitorModule::eventsJson(qint64 sinceSec, qint64 untilSec,
                                      const QString& service) const {
    return m_memory.eventsJson(sinceSec, untilSec, service);
}

QJsonObject MonitorModule::dailyStatsJson(const QString& fromDay, const QString& toDay,
                                          const QString& service) const {
    return m_memory.dailyJson(fromDay, toDay, service);
}

QJsonObject MonitorModule::quarterlyStatsJson() const {
    return m_memory.quarterlyJson();
}

QJsonObject MonitorModule::annualStatsJson() const {
    return m_memory.annualJson();
}

QJsonObject MonitorModule::lifeJson() const {
    return m_memory.lifeJson();
}

// --- Sections de l'API -------------------------------------------------------

QJsonObject MonitorModule::systemJson() {
    // Presque entièrement statique : aucun cache nécessaire.
    return m_system.collect();
}

QJsonObject MonitorModule::resourcesJson() {
    if (isFresh(m_cResources, m_config.resourcesRefreshMs()))
        return m_cResources.value;
    m_cResources.value = m_resources.collect();
    m_cResources.age.restart();
    m_cResources.valid = true;
    return m_cResources.value;
}

QJsonObject MonitorModule::networkJson() {
    if (isFresh(m_cNetwork, m_config.networkRefreshMs()))
        return m_cNetwork.value;
    m_cNetwork.value = m_network.collect();
    m_cNetwork.age.restart();
    m_cNetwork.valid = true;
    return m_cNetwork.value;
}

QJsonObject MonitorModule::servicesJson() {
    QJsonObject o;

    // Nouvelle tentative de chargement si la configuration manquait au
    // demarrage. Un service lance avant que le fichier partage existe (ordre de
    // demarrage, installation en cours) restait sinon aveugle jusqu'a son
    // prochain redemarrage — en repondant correctement, mais sans rien
    // superviser, ce qui est le pire des deux mondes.
    if (!m_config.isLoaded()) {
        if (m_config.load(m_configPath)) {
            // La configuration vient d'arriver : les caches batis sur l'ancienne
            // (vide) n'ont plus de sens.
            m_cSystemd.valid = false;
            m_cProbes.valid = false;
        }
    }

    if (!isFresh(m_cSystemd, m_config.systemdRefreshMs())) {
        m_cSystemd.value = m_supervisor ? m_supervisor->collectSystemd() : QJsonObject{};
        m_cSystemd.age.restart();
        m_cSystemd.valid = true;
    }
    // Croise l'etat systemd (preuve de VIE) avec la fraicheur du heartbeat beacon
    // (preuve de SANTE). Un service que systemd voit « active » mais dont le beacon
    // s'est TU depuis plus de offline_after_s est vivant mais bloque (« muet ») :
    // le cas SIGSTOP/deadlock qu'ActiveState ne voit pas, et que la sonde TCP rate
    // aussi (le noyau accepte la connexion sans l'application). On ne juge que les
    // services DEJA entendus : une app absente de la table n'emet pas de beacon,
    // son silence ne prouve rien. Pur croisement d'observations deja en main --
    // aucune sonde nouvelle, aucune dependance, aucun privilege. Fonctionne aussi
    // sous Windows : le beacon ne depend pas de l'OS. Calcule a chaque requete
    // (non mis en cache) pour refleter l'age courant entre deux releves systemd.
    {
        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
        const qint64 offlineAfter = m_config.beaconOfflineAfterS();
        const QHash<QString, qint64> beaconAge = beaconAgeByLocalApp(nowSec);
        QHash<QString, QString> appByUnit;
        for (const SystemdServiceDef& s : m_config.systemdServices())
            appByUnit.insert(s.unit, s.app.isEmpty() ? s.label : s.app);

        QJsonArray services = m_cSystemd.value.value(QStringLiteral("services")).toArray();
        for (int i = 0; i < services.size(); ++i) {
            QJsonObject svc = services.at(i).toObject();
            if (!svc.value(QStringLiteral("active")).toBool())
                continue;               // seul un service actif peut etre « muet »
            const QString app = appByUnit.value(svc.value(QStringLiteral("unit")).toString());
            if (app.isEmpty() || !beaconAge.contains(app))
                continue;               // service qui n'emet pas de beacon : on ne conclut rien
            const qint64 age = beaconAge.value(app);
            svc[QStringLiteral("heartbeat_age_s")] = static_cast<double>(age);
            const bool online = age < offlineAfter;
            svc[QStringLiteral("heartbeat_online")] = online;
            svc[QStringLiteral("stuck")] = !online;   // actif mais muet => bloque
            services.replace(i, svc);
        }
        o["systemd"] = services;
    }

    if (!isFresh(m_cProbes, m_config.probesRefreshMs())) {
        m_cProbes.value = m_supervisor ? m_supervisor->collectProbes(uptimeSeconds())
                                       : QJsonObject{};
        m_cProbes.age.restart();
        m_cProbes.valid = true;
    }
    o["network"] = m_cProbes.value.value(QStringLiteral("probes"));
    o["network_grace"] = m_cProbes.value.value(QStringLiteral("grace"));

    const QJsonObject beacon = beaconAppsJson();
    o["beacon"] = beacon.value(QStringLiteral("apps"));
    o["beacon_offline_after_s"] = beacon.value(QStringLiteral("offline_after_s"));

    // Machines connues du parc (apprises par beacon, persistantes). Elles portent
    // l'etat par MACHINE : un poste entierement eteint est UNE ligne « hors ligne »,
    // pas la disparition de chacun de ses services. L'archivage automatique range
    // hors de la vue une machine absente depuis longtemps, sans la supprimer.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    o["machines"] = m_machines.machinesJson(now, m_config.beaconOfflineAfterS(),
                                            m_config.machineArchiveAfterS());

    // Activites EN COURS declarees par les services (contrat generique `activity/1`).
    // On re-sonde le /status des services en ligne pour capter ce champ VOLATILE,
    // mais seulement ici, quand un client regarde : pas de sonde de fond permanente.
    // Puis on assemble la section. morfMonitor OBSERVE : il affiche, il n'agit pas.
    // Parcours sur une COPIE des cles : iterer m_beaconSeen en direct pendant qu'un
    // effet de bord (ou une reentrance) l'insere/purge invaliderait l'iterateur, ce
    // qui plantait le service. La copie rend ce parcours insensible a toute mutation
    // de la table -- ceinture et bretelles, en plus de la sonde morfUpdate rendue
    // asynchrone qui supprimait la cause premiere de cette reentrance.
    const QList<QString> beaconKeys = m_beaconSeen.keys();
    for (const QString& key : beaconKeys)
        fetchActivityIfStale(key);
    o["activities"] = activitiesJson(now);

    // Versions de services : partie DISTANTE (release) en cache, jointe ici a la
    // version EXECUTEE du beacon. On tente un rafraichissement des entrees expirees
    // en arriere-plan (checkNow(false)) : jamais bloquant, et sans rien envoyer si
    // le cache est frais (< TTL). L'etat est calcule dans VersionMonitor.
    if (m_versions) {
        QVector<VersionMonitor::Target> targets;
        for (const SystemdServiceDef& s : m_config.systemdServices()) {
            VersionMonitor::Target t{ s.label, s.app, s.repoOwner, s.repo };
            t.updatable = (s.repo != QLatin1String("morfUpdate"));
            // /releases/latest du depot morfUpdate n'est pas toujours une
            // release de l'outil (tags vX.Y.Z).
            if (s.repo.compare(QLatin1String("morfUpdate"), Qt::CaseInsensitive) == 0)
                t.releaseMode = QStringLiteral("semver_tags");
            targets.push_back(t);
        }
        for (const EcosystemProjectDef& p : m_config.ecosystemProjects()) {
            VersionMonitor::Target t;
            t.label = p.label;
            t.app = p.label;
            t.owner = p.repoOwner;
            t.repo = p.repo;
            t.group = QStringLiteral("ecosystem");
            t.updatable = false;
            t.kind = p.kind;
            if (!p.release.isEmpty())
                t.releaseMode = p.release;
            else if (p.repo.compare(QLatin1String("morfPackages"), Qt::CaseInsensitive) == 0)
                t.releaseMode = QStringLiteral("semver_tags");
            else
                t.releaseMode = QStringLiteral("github_latest");
            targets.push_back(t);
        }
        m_versions->setTargets(targets);
        m_versions->checkNow(/*force=*/false);
        QHash<QString, VersionMonitor::Running> running = runningVersionsByApp();
        const QString localHost = QHostInfo::localHostName();
        for (const SystemdServiceDef& s : m_config.systemdServices()) {
            const QString app = s.app.isEmpty() ? s.label : s.app;
            if (running.contains(app) && !running.value(app).version.isEmpty())
                continue;
            if (s.repo.compare(QLatin1String("morfUpdate"), Qt::CaseInsensitive) != 0)
                continue;
            const QString ver = cachedMorfUpdateVersion();
            if (!ver.isEmpty())
                running.insert(app, { ver, localHost });
        }
        for (const EcosystemProjectDef& p : m_config.ecosystemProjects()) {
            const QString ver = localEcosystemVersion(p);
            if (ver.isEmpty())
                continue;
            running.insert(p.label, { ver, localHost });
        }
        o["versions"] = m_versions->toJson(running);
    }

    o["ts"] = static_cast<double>(now);
    return o;
}

// Version executee par nom d'application (= label systemd), depuis les heartbeats
// beacon deja recus. L'onglet « Services systemd » decrit les unites de CETTE
// machine : on privilegie donc la version annoncee par l'hote LOCAL. A defaut
// (service local qui n'annonce pas, ou vu seulement ailleurs), on prend la plus
// recente vue sur une autre machine, en conservant l'hote annonceur pour que le
// frontend puisse le signaler -- sinon « morfCollector 0.4.5 (pi4fred) » afficherait
// 0.4.5 dans l'onglet local de pi4dev sans qu'on sache d'ou vient ce numero.
QHash<QString, VersionMonitor::Running> MonitorModule::runningVersionsByApp() const {
    const QString local = QHostInfo::localHostName();
    QHash<QString, VersionMonitor::Running> byApp;
    QHash<QString, qint64> seenAt;
    QHash<QString, bool>   pickedLocal;
    for (const BeaconSeen& s : m_beaconSeen) {
        if (s.app.isEmpty() || s.version.isEmpty())
            continue;
        const bool isLocal = !s.host.isEmpty()
            && s.host.compare(local, Qt::CaseInsensitive) == 0;
        const bool have = byApp.contains(s.app);
        // Une entree LOCALE l'emporte toujours ; entre deux entrees de meme
        // « localite », la plus recente gagne.
        const bool better = !have
            || (isLocal && !pickedLocal.value(s.app))
            || (isLocal == pickedLocal.value(s.app) && s.lastSeen > seenAt.value(s.app));
        if (better) {
            byApp[s.app]      = { s.version, s.host };
            seenAt[s.app]     = s.lastSeen;
            pickedLocal[s.app] = isLocal;
        }
    }
    return byApp;
}

// Meme jointure que runningVersionsByApp, mais pour la FRAICHEUR. On privilegie
// l'instance de l'hote LOCAL, meme PERIMEE : c'est celle de l'onglet « Services
// systemd » de CETTE machine. Un service local gele laisse son entree vieillir
// (les entrees periment au bout d'1 h, pas a offline_after_s) ; on veut cet age
// stale, pas celui d'une instance fraiche du meme service sur une autre machine,
// qui masquerait le blocage local. A localite egale, la plus fraiche gagne.
QHash<QString, qint64> MonitorModule::beaconAgeByLocalApp(qint64 nowSecs) const {
    const QString local = QHostInfo::localHostName();
    QHash<QString, qint64> ageByApp;
    QHash<QString, bool>   pickedLocal;
    for (const BeaconSeen& s : m_beaconSeen) {
        if (s.app.isEmpty())
            continue;
        const qint64 age = nowSecs - s.lastSeen;
        const bool isLocal = !s.host.isEmpty()
            && s.host.compare(local, Qt::CaseInsensitive) == 0;
        const bool have = ageByApp.contains(s.app);
        const bool better = !have
            || (isLocal && !pickedLocal.value(s.app))
            || (isLocal == pickedLocal.value(s.app) && age < ageByApp.value(s.app));
        if (better) {
            ageByApp[s.app]    = age;
            pickedLocal[s.app] = isLocal;
        }
    }
    return ageByApp;
}

void MonitorModule::triggerVersionCheck() {
    if (m_versions)
        m_versions->checkNow(/*force=*/true);
}

QJsonObject MonitorModule::rebootJson() {
    return m_reboot.detect();
}

QJsonObject MonitorModule::allJson() {
    QJsonObject o;
    o["system"]    = systemJson();
    o["resources"] = resourcesJson();
    o["network"]   = networkJson();
    o["services"]  = servicesJson();
    o["reboot"]    = rebootJson();
    o["ts"]        = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

QJsonObject MonitorModule::statusJson() const {
    QJsonObject o;
    o["running"]       = m_running;
    o["config_loaded"] = m_config.isLoaded();
    o["config_path"]   = m_config.loadedPath();
    if (!m_config.isLoaded())
        o["config_error"] = m_config.lastError();
    o["supervised"] = QJsonObject{
        {"systemd", m_config.systemdServices().size()},
        {"network", m_config.networkServices().size()},
        {"beacon",  m_config.beaconApps().size()},
        {"ecosystem", m_config.ecosystemProjects().size()}};
    o["beacon_heard"] = m_beaconSeen.size();
    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

} // namespace morfmonitor
