/*
 * morfMonitor
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfmonitor/MonitorModule.h"

#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QSet>

namespace morfmonitor {

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
    if (m_beaconSocket) {
        m_beaconSocket->close();
        m_beaconSocket = nullptr; // détruit par l'arbre QObject
    }
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

void MonitorModule::fetchWebUiIfNeeded(const QString& key) {
    const auto it = m_beaconSeen.find(key);
    if (it == m_beaconSeen.end() || it->webUiFetched)
        return;
    if (!it->capabilities.contains(QLatin1String("web_ui")))
        return;                       // pas d'interface annoncee : rien a demander
    if (it->sourceIp.isEmpty() || it->statusPort == 0)
        return;                       // rien pour construire l'URL

    // Marque avant l'envoi : sans cela, chaque heartbeat relancerait une requete
    // tant que la premiere n'a pas repondu.
    it->webUiFetched = true;

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
            // Echec sans consequence : le service reste supervise, simplement
            // sans lien. On autorise un nouvel essai au prochain heartbeat.
            entry->webUiFetched = false;
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        entry->webUi = o.value(QStringLiteral("web_ui")).toObject();
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
            s.webUi        = it->webUi;
            s.webUiFetched = it->webUiFetched && it->version == s.version;

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
        fetchWebUiIfNeeded(key);
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
}

QJsonObject MonitorModule::beaconAppsJson() const {
    QJsonObject o;
    QJsonArray arr;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int offlineAfter = m_config.beaconOfflineAfterS();

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
        a["state"]       = s.state;
        addReachability(a, s);
    };

    // 1. Les applications déclarées : toujours listées, en ligne ou non. Une
    //    application attendue mais absente est une information ; l'omettre la
    //    ferait disparaître silencieusement de l'affichage. Quand plusieurs
    //    instances s'annoncent, chacune a sa ligne ; quand aucune ne se fait
    //    entendre, la déclaration à elle seule produit la ligne « hors ligne ».
    QSet<QString> declared;
    for (const BeaconAppDef& d : m_config.beaconApps()) {
        declared.insert(d.app);
        bool heard = false;
        for (auto it = m_beaconSeen.constBegin(); it != m_beaconSeen.constEnd(); ++it) {
            if (it->app != d.app)
                continue;
            heard = true;
            QJsonObject a;
            a["app"]      = d.app;
            a["label"]    = d.label;
            a["enabled"]  = d.enabled;
            a["declared"] = true;
            fill(a, *it);
            arr.append(a);
        }
        if (!heard) {
            QJsonObject a;
            a["app"]      = d.app;
            a["label"]    = d.label;
            a["enabled"]  = d.enabled;
            a["declared"] = true;
            a["online"]   = false;
            arr.append(a);
        }
    }

    // 2. Les instances entendues d'applications NON déclarées, marquées comme
    //    telles. C'est un outil de découverte : brancher un nouveau service et
    //    le voir apparaître ici indique quoi ajouter à la configuration.
    for (auto it = m_beaconSeen.constBegin(); it != m_beaconSeen.constEnd(); ++it) {
        if (declared.contains(it->app))
            continue;
        QJsonObject a;
        a["app"]      = it->app;
        a["label"]    = it->app;
        a["declared"] = false;
        fill(a, *it);
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

    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
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
        {"beacon",  m_config.beaconApps().size()}};
    o["beacon_heard"] = m_beaconSeen.size();
    o["ts"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

} // namespace morfmonitor
