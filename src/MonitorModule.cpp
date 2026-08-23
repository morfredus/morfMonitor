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
#include <QHostInfo>
#include <QFile>
#include <QSet>
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

    m_running = true;
    return true;
}

void MonitorModule::stop() {
    m_running = false;
    // Persiste la derniere annonce de chaque machine avant de partir : au
    // prochain demarrage, « vu il y a ... » repart d'une base fraiche.
    m_machines.flush();
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
    o["systemd"] = m_cSystemd.value.value(QStringLiteral("services"));

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

    // Versions de services : partie DISTANTE (release) en cache, jointe ici a la
    // version EXECUTEE du beacon. On tente un rafraichissement des entrees expirees
    // en arriere-plan (checkNow(false)) : jamais bloquant, et sans rien envoyer si
    // le cache est frais (< TTL). L'etat est calcule dans VersionMonitor.
    if (m_versions) {
        QVector<VersionMonitor::Target> targets;
        for (const SystemdServiceDef& s : m_config.systemdServices()) {
            VersionMonitor::Target t{ s.label, s.app, s.repoOwner, s.repo };
            t.updatable = (s.repo != QLatin1String("morfUpdate"));
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
