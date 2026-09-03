# Journal des versions - morfMonitor

Le format s'inspire de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/)
et du [versionnage sémantique](https://semver.org/lang/fr/). 

## [0.16.1] - 2026-09-03

### Changed

- Re-vendored morfDeploy 0.20.5 (opt-in arm64 cross-packaging and the sysroot
  `.shlibs` Depends resolution). The VERSION is bumped so the source tag matches
  the rebuilt artifact after this vendored-tooling update; without it the release
  provenance check rejects a package built past the previous tag.

## [0.16.0] - 2026-08-24

### Modifié

- **Mise à jour d'un service : plus aucun popup.** La confirmation, l'avancement
  et le résultat s'affichent désormais en ligne, dans la cellule « Mise à jour »
  du service concerné. Cliquer « Mettre à jour » pose la question sur place
  (« Passer à X.Y.Z ? » avec « Confirmer » / « Annuler ») au lieu d'ouvrir une
  boîte de dialogue.
- **Avancement matérialisé.** Pendant l'opération, une barre indéterminée défile
  sous le service, accompagnée de l'étape courante et de son rang
  (« Vérification (2/5)… ») en suivant les états de l'agent morfUpdate
  (téléchargement, vérification, installation, redémarrage, contrôle de santé).
  Le succès reste affiché quelques secondes puis s'efface ; un échec reste
  visible jusqu'à « Masquer », avec un bouton « Réessayer ».

### Corrigé

- **Message d'échec explicite pour une demande lancée trop tôt.** Si la mise à
  jour est demandée pendant la publication de la release (le `.deb` est déjà en
  ligne, donc le bouton apparaît, mais `manifest.json` n'est pas encore attaché),
  l'agent échouait sur « release has no manifest.json » sans que la cause soit
  lisible. Ce cas est maintenant reconnu et affiché en clair : release
  incomplète, attendre une minute, « Vérifier les versions », puis relancer.

## [0.15.0] - 2026-08-24

### Ajouté

- Section **« Activités en cours »** (page État) : affiche en temps réel ce que
  chaque service du parc est en train de faire (indexation, compilation, collecte,
  synchronisation...), de façon générique, selon le contrat `activity/1`
  (`morfSystem/docs/CONTRAT-ACTIVITE.md`). Une ligne par service en ligne
  déclarant une activité : service, type, progression, durée, détail. « Aucune
  activité en cours » sinon. morfMonitor observe, il n'agit jamais dessus.
- Re-sondage périodique du `/status` des services en ligne (toutes les ~5 s,
  seulement quand un client regarde) pour capter le champ volatile `activity`.

### Corrigé

- **État matériel incohérent à distance.** L'état matériel d'un service (bloc
  `hardware` de `/status`) n'était lu qu'une seule fois par version, comme un
  détail statique. Or il est volatile (un capteur s'initialise après le boot, se
  branche/débranche, se dégrade). Un morfMonitor distant ayant sondé morfSensor
  pendant son démarrage figeait « capteur absent » alors que le morfMonitor local,
  sondé plus tard, voyait « présent ». L'état matériel est désormais rafraîchi au
  fil du re-sondage périodique, il suit donc le service dans le temps.

## [0.14.11] - 2026-08-23

### Corrigé

- `morfsystem.json` déclarait morfUpdate dans `beacon_apps`, alors que morfUpdate
  n'émet pas de heartbeat morfbeacon (il est supervisé en local via
  `systemd_services` + lecture `/status`). La vue Écosystème le marquait donc
  éternellement « introuvable » alors que l'onglet Services le montrait actif.
  morfUpdate retiré de `beacon_apps` (conservé dans `systemd_services`), dans le
  fichier déployé et l'exemple, avec une note expliquant pourquoi ne pas l'y
  remettre.

## [0.14.10] - 2026-08-23

### Corrigé

- Version locale de morfUpdate : si `/status` ne répond pas, lecture de
  `/opt/morfupdate/VERSION`.

## [0.14.9] - 2026-08-23

### Corrigé

- Version de morfUpdate : lecture de `GET http://127.0.0.1:8794/status` (pas de
  beacon) ; dernière release via les tags `vX.Y.Z`, pas `/releases/latest`.

## [0.14.8] - 2026-08-23

### Corrigé

- Suivi d'une mise à jour : si `fetch` tombe (dpkg arrête morfMonitor), l'UI
  retente ~3 min au lieu d'un pop-up « Failed to fetch » et d'abandonner.

## [0.14.7] - 2026-08-23

### Corrigé

- morfPackages : les tags `vX.Y.Z` de l'outil se lisent via
  `git/matching-refs/tags/v`. La première page de `/tags` ne contient plus que
  les index `projet-v…` ; `/releases/latest` n'est pas non plus la version de
  l'outil. Le dépôt `morfPackages` utilise ce mode même sans champ `release`
  dans la config déployée.

## [0.14.6] - 2026-08-23

### Corrigé

- morfPackages : la dernière version de l'outil se lit sur les tags `vX.Y.Z`,
  pas sur `/releases/latest` (ce dépôt publie les paquets des autres projets).
- morfTools / morfPackages : version locale lue dans `~/Codage/morfSystem` (et
  le workspace de développement), plus seulement `~/morfSystem`.

## [0.14.5] - 2026-08-23

### Corrigé

- Compilation MinGW : `qEnvironmentVariable` attend un `const char *`, pas un
  `QString`.

## [0.14.4] - 2026-08-23

### Ajouté

- Unité `morfupdate` dans les services systemd (pas de bouton : l'agent ne se
  met pas à jour lui-même).
- Section « Bibliothèques et outils » : morfBeacon, morfDeploy, morfPackages,
  morfTools (pas morfSystem). Comparaison avec la dernière release GitHub.

## [0.14.3] - 2026-08-22

### Corrigé

- Le bouton « Mettre à jour » envoie le dépôt GitHub (`project` / `repo`), plus le libellé affiché. `DashBoard` n'est pas une cible morfUpdate ; `morfDashboard` l'est. Message d'erreur si le projet n'est pas dans `targets` ou si l'agent ne répond pas.

## [0.14.2] - 2026-08-21

### Ajouté

- Enregistrement des compilations au niveau CMake (record_compile) : la durée de compile est signalée à morfAnalytics quel que soit le déclencheur (cmake --build direct, morf upgrade, déploiement morfDeploy).

## [0.14.1] - 2026-08-21

### Modifié

- Resynchroniser les copies vendorées de morfdeploy (0.17.3) et morfUpdate (0.4.1).

## [0.14.0] - 2026-08-20

### Modifié

- Le flux de mise à jour LAN ne dépend plus d'un jeton partagé. L'interface suit
  désormais l'opération asynchrone jusqu'à son succès ou son diagnostic d'échec,
  au lieu de confondre l'acceptation de la demande avec son résultat.

## [0.13.1] - 2026-08-20

### Corrigé

- Le bouton de mise à jour fonctionne depuis l'interface Web consultée sur le
  LAN. Il délègue toujours exclusivement vers l'agent local `127.0.0.1` et ne
  peut cibler que les services de cette machine.

## [0.13.0] - 2026-08-20

### Ajouté

- Bouton local « Mettre à jour » dans les services systemd lorsqu'une release
  plus récente est connue. La demande passe par le backend de morfMonitor vers
  l'agent local morfUpdate, sans jamais sélectionner une machine distante.
- Configuration `update_agent` pour désigner le jeton local partagé, sans jamais
  l'exposer au navigateur ou aux réponses d'API.

## [0.12.0] - 2026-08-20

### Ajouté

- Mise à jour de la copie vendorée de morfDeploy 0.14.0 pour le packaging avec
  provenance vérifiée.

## [0.11.1] - 2026-08-19

### Corrigé

- **Version exécutée : priorité à l'hôte local** dans l'onglet « Services
  systemd ». La jointure prenait le heartbeat le plus récent du parc : un même
  service tournant sur un autre Pi pouvait afficher SA version (ex. morfCollector
  0.4.5 vu depuis pi4fred) dans le tableau local de pi4dev, alors que la machine y
  tourne 0.5.1. On privilégie désormais la version annoncée par CETTE machine ; à
  défaut, la plus récente vue ailleurs est affichée **avec son hôte annonceur**
  (badge + info-bulle), pour ne jamais montrer un numéro sans provenance.

## [0.11.0] - 2026-08-19

### Ajouté

- **Versions des services dans l'onglet « Services morfSystem ».** Chaque service
  affiche désormais sa **version exécutée**, la **dernière release publiée** et un
  **état de comparaison** (`À jour`, `Mise à jour disponible`, `Version locale plus
  récente`, `Version inconnue`, `Vérification impossible`), sans quitter la vue
  opérationnelle.
  - La version exécutée réutilise le chemin existant : le champ `version` du
    heartbeat morfBeacon (joint par le nom d'application). Elle reste visible même
    hors ligne ; une absence de réseau n'est **jamais** interprétée comme « à jour ».
  - La dernière release vient de **morfUpdate** (embarqué, cœur sans UI) : aucune
    logique GitHub propre à morfMonitor, aucun rôle pour Doctor. Comparaison
    sémantique (`Version::compare`), release **stable** uniquement.
  - **Correspondance service → dépôt** : unique table dans `morfsystem.json`
    (champ `repo` par service, `owner` par défaut `morfredus`), consommée par
    morfUpdate. `repo` absent → « Version inconnue » côté release, sans deviner.
    Champ `app` facultatif quand le nom beacon diffère du label affiché.
  - **Cache et déclenchement** : résultat persisté (affiché immédiatement à
    l'ouverture) ; une entrée de moins de 6 h n'est pas revérifiée ; une entrée
    expirée est revérifiée en arrière-plan (jamais bloquant) ; le bouton
    **« Vérifier les versions »** force un contrôle frais. Un échec réseau
    n'efface pas la dernière release connue (info-bulle : dernier succès, dépôt,
    raison). La vérification distante ne part jamais sur la boucle de métriques.
  - Route `POST /api/versions/check` (déclenchement) ; bloc `versions` dans
    `/api/all` et `/api/services`.

### Dépendance

- **morfUpdate embarqué** (`third_party/morf/morfupdate`, cœur Qt Core+Network
  sans Widgets), synchronisé par `scripts/sync-morf.(sh|ps1)`.

## [0.10.0] - 2026-08-17

### Ajouté

- **Capacité annoncée `system_monitor`.** morfMonitor déclare désormais cette
  capacité dans son heartbeat morfBeacon. Un consommateur peut ainsi le reconnaître
  **par capacité, jamais par nom** : morfAnalytics (≥ 0.27.0) s'en sert pour découvrir
  seul les morfMonitor du parc et historiser leur `/api/all`, sans aucune déclaration
  manuelle. Aucun changement de comportement pour l'existant ; c'est un champ additif
  du heartbeat.

## [0.9.0] - 2026-08-16

### Ajouté

- **Machines du parc, apprises seules.** morfMonitor mémorise les machines
  (rôle `host`) découvertes par morfBeacon, sans aucune déclaration manuelle. Un
  registre persistant (`/var/lib/morfmonitor/known-machines.json`, via
  `StateDirectory`) leur survit : une machine éteinte reste connue, puis passe en
  `archivée` après une longue absence (`beacon.archive_after_days`, 30 par défaut)
  sans être supprimée. États dérivés : `active` / `offline` / `archived`. Seul un
  `POST /api/machines/forget` (bouton « Oublier ») retire une machine. Les machines
  sont exposées dans `services.machines`.
- **Vue par machine dans l'onglet Écosystème.** Une carte « Machines du parc »
  liste chaque machine et son état ; un poste entièrement éteint y tient en une
  seule ligne (« pi4dev — éteinte — vue il y a 3 h ») au lieu de faire clignoter en
  rouge chacun de ses services, désormais masqués du tableau beacon.

### Modifié

- **Rôle et présence par hôte.** Le champ `role` (`host`/`device`) du contrat
  morfBeacon 0.7.0 est lu sur chaque heartbeat ; chaque entrée beacon expose son
  `role` et un `host_online`. Un poste est en ligne tant qu'un de ses services
  `host` émet, si bien que sa disparition est UN fait (« machine hors ligne »),
  pas N pannes de service.
- **Anomalies plus justes.** Un service tombé sur une machine vivante est une
  panne attribuable (rouge) ; un service connu seulement sur des machines éteintes
  n'en est pas une (la carte des machines le montre) ; un service attendu et
  entendu nulle part est « introuvable » (rouge).
- **Attente de placement.** `beacon_apps` accepte un `host` facultatif : attente
  de *présence* (attendu quelque part dans le parc, insensible à un déménagement de
  machine) sans lui, de *placement* (attendu sur CETTE machine) avec lui.

## [0.8.4] - 2026-08-15

### Modifié

- **Tout le parc déclaré dans `beacon_apps`** (config partagée `morfsystem.json` et son
  exemple), pour que les services n'apparaissent plus « non déclaré » dans l'onglet
  Écosystème.
  - Services permanents à `enabled: true` (pleinement supervisés, absence = anomalie) :
    `morfMonitor`, `morfPhoto`, `morfAnalytics`, `morfCollector`, `morfNotify`,
    `morfSensor`, `morfSync`, `morfDashboard`.
  - Applications de bureau à `enabled: false` (déclarées mais non alertées à la
    fermeture) : `ComponentHub`, `SiteWatch`, et désormais **`PhotoHub`** (qui s'annonce
    depuis PhotoHub 0.6.3).
  - Le champ `app` reprend exactement le nom annoncé dans le heartbeat. Rappel : ce
    changement de config ne prend effet sur le Pi qu'après `deploy-config.sh --shared`
    (qui écrase le fichier déployé) puis redémarrage de morfMonitor.

## [0.8.3] - 2026-08-15

### Ajouté

- **Équivalents Windows (moins détaillés) des champs bruts de la phase 0.** Les
  ajouts 0.8.2 reposaient sur `systemctl` et `/sys` (Linux). Windows est désormais
  couvert par l'API Win32, avec le même contrat de sortie et la même règle « donnée
  absente = champ omis, jamais un 0 ».
  - **Réseau (`/api/network`)** : compteurs `rx_bytes`, `tx_bytes`, `rx_errors`,
    `tx_errors` par interface via `GetIfTable` (iphlpapi), appariés par index
    d'interface. Compteurs 32 bits (contre 64 bits sous Linux).
  - **Par service (`/api/services`)** : faute de systemd, chaque unité configurée est
    associée à un processus dont l'image est `<unit>.exe` (énumération via
    `CreateToolhelp32Snapshot`). On en tire `state`, `active`, `pid`, `uptime_s`
    (`GetProcessTimes`) et `resources` {`cpu_percent`, `cpu_time_usec`, `memory_bytes`
    avec `memory_source = "working_set"`}. Un service = son processus principal
    (moins détaillé que le cgroup complet sous Linux).
- Liaison conditionnelle des bibliothèques Windows `iphlpapi` et `psapi`.

## [0.8.2] - 2026-08-15

### Ajouté

- **Champs bruts additifs pour l'historisation par morfAnalytics Monitor** (phase 0).
  Tous rétrocompatibles, sans déplacer aucun calcul dans morfMonitor.
  - **Par service (`/api/services`)** : `pid` (MainPID), `uptime_s` (déduit de
    `ActiveEnterTimestampMonotonic` et de `/proc/uptime`) et `restarts` (`NRestarts`).
    Obtenus dans l'appel `systemctl show` **déjà existant** (propriétés ajoutées à la
    même commande) : aucun processus supplémentaire. `pid`/`uptime_s` uniquement pour
    une unité active ; `restarts` exposé même à l'arrêt. Champs omis, jamais faux,
    quand la donnée manque.
  - **Réseau (`/api/network`)** : compteurs cumulés bruts par interface `rx_bytes`,
    `tx_bytes`, `rx_errors`, `tx_errors`, lus dans `/sys/class/net/<itf>/statistics/`.
    morfMonitor ne calcule aucun débit : un débit est un delta, laissé à morfAnalytics.
    Omis hors Linux.

## [0.8.1] - 2026-08-14

### Corrigé

- **Troncature des grandes réponses HTTP** dans `HttpServer::reply()`. Resynchronisation
  du correctif issu du patron `morfTemplateService`. morfMonitor sert des pages HTML
  (tableau de bord) qui dépassent le tampon socket (~20 Ko) : la connexion était fermée
  sans drainer le tampon d'écriture et la fin de page arrivait coupée côté client. On
  attend désormais que `bytesToWrite()` retombe à zéro avant `disconnectFromHost()`.
- Resynchronisation de la copie vendorée de **morfBeacon** (`third_party/morf/beacon`)
  en 0.6.1 : même classe de bug corrigée dans son `StatusServer` (grande réponse
  `/status` coupée faute de drainage du tampon d'écriture).

## [0.8.0] - 2026-08-14

### Ajouté

- **Consommation par service** dans la supervision systemd. Chaque unité active
  expose désormais un bloc `resources` dans `/status` :
  - `cpu_percent` : taux CPU instantané, déduit de la différence de temps CPU
    entre deux relevés (peut dépasser 100 % sur plusieurs cœurs) ;
  - `cpu_time_usec` : temps CPU cumulé depuis le démarrage du service, compteur
    stable destiné à morfAnalytics ;
  - `memory_bytes` : mémoire réellement utilisée, en octets bruts ;
  - `memory_source` : provenance de la mémoire, `cgroup` (mesure systemd) ou
    `pss_sum` (repli, voir ci-dessous) ;
  - `tasks` : nombre de processus/threads de l'unité.
  Les mesures portent sur le **cgroup complet** de l'unité (via `MemoryCurrent`,
  `CPUUsageNSec`, `TasksCurrent` de systemd), pas sur le seul PID principal, et
  sont récoltées dans l'appel `systemctl show` déjà existant, donc sans
  processus supplémentaire.
- **Repli mémoire par somme de PSS.** Le contrôleur cgroup `memory` est
  désactivé par défaut sur Raspberry Pi OS (`cgroup_disable=memory` dans la ligne
  de commande du noyau) : systemd renvoie alors `MemoryCurrent=[not set]` et la
  mémoire par service resterait vide. Quand la mesure systemd manque, morfMonitor
  somme désormais le **PSS** (Proportional Set Size) des processus du cgroup de
  l'unité (`cgroup.procs` → `/proc/<pid>/smaps_rollup`). Le PSS, et non le RSS :
  tous les services du parc étant des binaires Qt qui partagent leurs
  bibliothèques, le RSS compterait ces pages partagées en entier dans chaque
  service. La lecture n'exige aucun privilège tant que les services tournent sous
  le même compte (cas du parc) ; sinon la valeur est simplement omise, jamais
  fausse. `memory_source` indique laquelle des deux sources a servi. Un service arrêté ou désactivé n'expose **pas** de
  bloc `resources` : « inactif + non applicable » reste distinct de « actif + 0 ».
  L'ajout est rétrocompatible (nouvelles clés, aucune modification des champs
  existants). L'onglet « Services morfSystem » affiche deux nouvelles colonnes
  CPU et Mémoire. L'historisation reste du ressort de morfAnalytics.

## [0.7.2] - 2026-08-14

### Corrigé

- Description de l'unité systemd : suppression du reste de patron « (a adapter) »
  et remplacement du tiret cadratin par un tiret simple.

## [0.7.1] - 2026-08-14

### Corrigé

- Ajout de **morfPhoto** à la liste des services systemd supervisés
  (`config/morfsystem.json`) : le service tournait mais n'apparaissait pas dans
  l'onglet « Services morfSystem », faute d'être déclaré dans la source de vérité
  partagée. Le déploiement de la config (`scripts/linux/deploy-config.sh`) reste
  nécessaire pour que le `/etc/morfsystem/morfsystem.json` du Pi prenne en compte
  l'ajout.

## [0.7.0] - 2026-08-13

### Ajouté

- **État matériel dans la vue Écosystème.** morfMonitor lit le bloc `hardware`
  publié par chaque service (contrat morfBeacon) et l'affiche dans une colonne
  « Matériel » (`/api/services` porte aussi ce bloc). Il ne déduit JAMAIS la
  présence du matériel : il restitue tel quel ce que le service déclare. Un
  service sans matériel n'affiche rien ; « aucun capteur » (none) est neutre, pas
  une alerte ; seul « capteur absent » (degraded) est signalé. Permet de
  distinguer `pi4fred · morfSensor · ok · capteur présent` de
  `pi4dev · morfSensor · ok · aucun capteur`.

### Modifié

- Rafraîchissement de la dépendance vendorée morfBeacon (0.6.0, contrat `hardware`).

## [0.6.2] - 2026-07-28

### Ajouté

- **Colonne « Port » dans la page Écosystème.** Le tableau des services découverts
  via morfBeacon affiche désormais le port du service (champ `status_port` annoncé
  dans le heartbeat) entre les colonnes « Adresse » et « Version ». L'information
  transitait déjà dans le JSON (`addReachability`) mais n'était pas visible ;
  distinguer deux services d'une même machine (morfMonitor 8790, morfNotify 8789...)
  demandait jusqu'ici d'ouvrir le lien d'interface. La cellule affiche « - » pour
  une application déclarée mais hors ligne, dont aucune instance n'a été entendue.
  Modification d'affichage uniquement : aucun changement du contrat morfBeacon ni
  du JSON servi.

### Documentation

- **Guide d'accès distant par WireGuard** ([`docs/fr/ACCES-DISTANT.md`](docs/fr/ACCES-DISTANT.md)).
  Comment atteindre morfMonitor (et le reste du parc) hors du réseau local par un
  tunnel VPN hébergé sur le Pi, sans authentification propre au service et sans
  intervenant externe. Applique la doctrine « l'accès distant est un composant
  dédié » : la confiance est arbitrée à l'entrée du tunnel, les services gardent
  leur interface LAN inchangée.

## [0.6.1] - 2026-07-28

### Modifié

- **Copie vendorée de morfdeploy resynchronisée** depuis morfTools : prise en
  charge de l'état persistant `/var/lib` dans le déploiement (`manifest.state_dir()`,
  substitution `__STATE_DIR__`, chemin d'état affiché à l'installation). Aucun
  changement de comportement du service lui-même.

## [0.6.0] - 2026-07-28

### Modifié

- **Configuration regroupée sous `/etc/morfsystem/<service>`.** Tout le parc
  partage désormais un point d'entrée UNIQUE dans `/etc` (`/etc/morfsystem/`),
  qui contient le fichier partagé `morfsystem.json` et un sous-dossier par
  service, au lieu d'un `/etc/<service>` par service à la racine de `/etc`. Sous
  Windows : `%ProgramData%\morfsystem\<service>`. Les données restent sous
  `/opt/<service>`. L'ancien `/etc/<service>` est adopté à l'installation
  (`migrate_from`).


### Corrigé

- **Casse du chemin Windows de la configuration partagée alignée sur le
  déploiement.** `SharedConfig` cherchait `%ProgramData%/morfSystem/morfsystem.json`
  (S majuscule) alors que morfdeploy l'installe dans `%ProgramData%/morfsystem/`
  (minuscule, comme tous les dossiers de service du parc). Sans effet sur NTFS
  (insensible à la casse), mais divergent de la convention et fragile sur un
  montage sensible à la casse. Désormais en minuscule, cohérent avec morfDashboard
  et le reste du parc. Linux (`/etc/morfsystem/morfsystem.json`) était déjà correct.

## [0.5.9] - 2026-07-26
### Corrigé

- **Les routes API dépliées restaient ouvertes au rafraîchissement.** La page
  Écosystème reconstruit tout son tableau à chaque cycle ; l'état ouvert/fermé
  des `<details>` de la colonne « API » était donc perdu, refermant ce que
  l'utilisateur venait d'ouvrir. Il est maintenant relevé avant la reconstruction
  et restauré à l'identique, par identité d'instance (stable d'un rendu à l'autre).

- **Back-off sur le « pull detail » d'un pair qui échoue.** Le `/status` d'un
  pair injoignable était re-sondé à chaque heartbeat (toutes les 15 s), et par
  chaque instance de morfMonitor. Sur une cible contrainte (ESP32 en plein scan
  réseau), cet afflux de connexions entrantes concurrentes pouvait épuiser ses
  sockets et la faire planter, ce qui relançait le cycle. Les tentatives qui
  échouent sont désormais espacées (30 s, 60 s, 120 s... plafonné à 10 min) ;
  un pair sain répond du premier coup, sans délai. Observer ne doit jamais nuire,
  quel que soit le nombre d'observateurs.

## [0.5.8] - 2026-07-26
### Ajouté

- **Cartographie de l'API des pairs.** morfMonitor lit désormais le champ `api`
  du `/status` des services découverts (en plus de `web_ui`) et l'expose dans
  `/api/services`, chaque entrée complétée d'un `base_url` joignable. Une
  colonne « API » apparaît dans la page Écosystème : les routes annoncées par
  chaque service s'y consultent, repliées par défaut. La tour de contrôle voit
  maintenant *ce que chaque service offre*, pas seulement qu'il est en vie.

### Modifié

- **Le « pull detail » n'est plus réservé aux services `web_ui`.** L'API n'étant
  pas une capacité annoncée par le heartbeat (qui reste maigre), elle ne se
  découvre que via `/status` ; morfMonitor interroge donc `/status` une fois par
  version pour **tout** service annonçant un port, et non plus seulement ceux qui
  déclarent une interface. Toujours borné (un appel par version), toujours en
  lecture : morfMonitor observe, ne relaie rien.

## [0.5.7] - 2026-07-26

### Ajouté

- **`/status` déclare l'API d'observation** (`GET /api/system`, `/api/resources`,
  `/api/network`, `/api/services`, `/api/reboot`, `/api/config`, `/api/all`, sous
  la base `/api`), en plus de l'interface web déjà annoncée. Un observateur - un
  autre morfMonitor, un futur portail - découvre ainsi non seulement l'existence
  et l'interface de ce service, mais comment l'interroger. Toutes les routes sont
  en lecture : morfMonitor observe, n'agit jamais (morfBeacon 0.5.0).

### Modifié

- **Le détail annoncé (interface web + API) est défini en un seul point**
  (`SelfDescription.h`, `fillAnnouncedDetail`), appelé par le heartbeat
  (`Service`) **et** par `/status` (`HttpServer`, via `describeService` de
  morfBeacon). Le bloc `web_ui` était écrit à la main aux deux endroits, mêmes
  valeurs : la moindre modification pouvait diverger entre ce que le heartbeat
  annonce et ce que `/status` décrit. Un observatoire qui s'exempterait de ses
  propres règles de découverte n'aurait aucune raison d'être cru sur les autres.


## [0.5.6] - 2026-07-23

### Corrigé

- **L'adresse retenue pour un émetteur multi-domicilié est celle du vrai réseau
  local.** Un service diffuse sur *toutes* les interfaces de sa machine ; un
  poste Windows avec WSL ou Hyper-V, un portable sous VPN, en ont plusieurs - et
  le dernier datagramme reçu gagnait. morfMonitor affichait donc « 172.24.224.1 »
  (réseau virtuel) pour une machine joignable en « 192.168.1.14 », que les
  autres superviseurs voyaient d'ailleurs correctement. L'adresse restait exacte
  au sens de la couche réseau, mais le lien affiché était inutilisable depuis
  toute autre machine.

  morfMonitor conserve désormais la **meilleure** adresse entendue, pas la
  dernière : celle qui appartient au même réseau que lui. Son propre réseau est
  déterminé par l'interface portant la route par défaut - obtenue en
  « connectant » une socket UDP vers une adresse réservée à la documentation
  (RFC 5737), ce qui n'émet **aucun paquet** et ne fait que révéler la route.
  La présence continue d'être rafraîchie à chaque heartbeat ; seule l'adresse
  résiste. Sans route par défaut, aucune préférence n'est appliquée plutôt
  qu'une préférence au hasard.

  Vérifié sur le parc réel : PC-Fred, vu depuis lui-même, est enfin annoncé en
  192.168.1.14. Cette piste, ouverte dans la roadmap, en est retirée.

## [0.5.5] - 2026-07-23

### Ajouté

- **Processeur et mémoire collectés sous Windows.** La page Ressources ne
  restait plus vide que du CPU et de la RAM sur un poste Windows, faute de
  `/proc`. Ils viennent désormais de l'API Win32 - `GetSystemTimes` (taux
  d'occupation, même calcul de delta que `/proc/stat`) et `GlobalMemoryStatusEx`
  (mémoire physique) - au même format JSON que sous Linux, si bien que
  l'interface ne connaît toujours pas la plateforme. L'onglet État général
  gagne aussi l'OS, le noyau et l'uptime (`QSysInfo`, `GetTickCount64`). Toute
  cette connaissance reste confinée aux collecteurs (`#ifdef` de plus bas
  niveau), le reste du parc n'en sait rien.

### Notes

- **La charge moyenne n'est pas simulée sous Windows.** Le *load average* est
  une notion Unix sans équivalent fidèle : plutôt que d'en fabriquer une, la
  carte « Métriques indisponibles » explique que le taux CPU répond à la même
  question. De même, le fichier d'échange n'est pas mesuré (les champs
  `GlobalMemoryStatusEx` ne l'isolent pas ; une première tentative affichait un
  alarmant « 100 % » - la fausse alerte que morfMonitor existe pour éviter), et
  la température, qui demanderait WMI, est omise. Rien d'inventé.

## [0.5.4] - 2026-07-23

### Corrigé

- **Le stockage s'affiche de nouveau sous Windows.** La supervision multi-volumes
  (0.5.2) filtrait les volumes par un périphérique commençant par `/dev/` - ce
  qui écartait tmpfs et les squashfs des snaps sous Linux, mais aussi **tous**
  les volumes Windows (`C:`, `D:`…), dont le périphérique ne suit pas cette
  convention. La page Ressources restait vide de tout stockage sur une machine
  Windows. Le tri se fait désormais par **type de système de fichiers**
  (exclusion portable de tmpfs, squashfs, overlay, proc, sysfs…) : `QStorageInfo`
  est portable, le filtre l'est redevenu. Inchangé sous Linux ; sous Windows,
  chaque disque réel apparaît à nouveau, jauge et anomalie comprises.

## [0.5.3] - 2026-07-22

### Modifié

- **La roadmap devient un document de vision.** Philosophie d'interface
  (« afficher l'information utile plutôt que toute l'information
  disponible »), origine des données, seuils et tendance du stockage, type de
  machine dans l'Écosystème, et l'évolution vers une console d'état de
  morfSystem - point d'entrée **en lecture** de l'administration. Trois
  pistes sont explicitement bornées par les arbitrages existants : les
  journaux attendent l'authentification et R5, les alertes restent émises par
  morfNotify, et commander (redémarrer, mettre à jour) reste le rôle de
  morfTools.

## [0.5.2] - 2026-07-22

### Corrigé

- **Le stockage couvre tous les volumes montés, plus la seule racine.** Sur
  une machine où `/home` est une partition séparée - installation Linux
  classique sur un portable - la page Ressources pouvait afficher « / » à
  90 % et affoler alors que les données avaient ailleurs toute la place, ou à
  l'inverse taire un `/home` plein. L'API expose désormais `disks` : un objet
  par système de fichiers adossé à un périphérique (`/dev/…`), pseudo-montages
  écartés (tmpfs, squashfs des snaps - en lecture seule, toujours « pleins » à
  100 %), montages bind dédupliqués, tri par point de montage. `disk` (la
  seule racine) est conservé pour les consommateurs existants.

### Modifié

- **Une carte Stockage par volume** dans la page Ressources, et **une jauge
  d'anomalie par volume** (« Stockage /home » à 92 % est signalé pour
  lui-même, avec son point de montage dans le libellé).

## [0.5.1] - 2026-07-22

### Modifié

- **Le texte explicatif de l'onglet Écosystème est réécrit, un paragraphe par
  idée** : découverte automatique, interrogation unique des interfaces Web,
  sens de « non déclaré », une ligne par machine, délai de mise hors ligne.
  Le pavé d'un seul tenant se consultait mal. Le délai reste tiré de la
  configuration (`beacon.offline_after_s`), jamais écrit en dur.

## [0.5.0] - 2026-07-22

### Corrigé

- **Deux machines faisant tourner le même service ne s'écrasent plus l'une
  l'autre.** La découverte était indexée par le nom `app` : deux instances du
  même service (un morfMonitor sur le Pi, un autre sur un portable) se
  partageaient une seule entrée, et l'affichage alternait entre les hôtes à
  chaque heartbeat. La découverte est désormais indexée par l'**identité
  d'instance** - le champ `instance` (`app@host`) que PROTOCOL.md avait prévu
  précisément pour ça, ou `app@ip` pour un émetteur qui ne l'annonce pas.

### Modifié

- **Une ligne par machine dans l'onglet Écosystème**, avec une colonne
  *Machine* donnant le nom d'hôte annoncé (joignable en général en lui
  ajoutant `.local`). L'API `/api/services` émet une entrée `beacon` par
  instance, chacune portant `instance`, `host`, `ip` et son propre lien
  d'interface Web.
- **Une anomalie « hors ligne » ne se déclenche que si AUCUNE instance ne
  répond** : la déclaration promet le service, pas chacune de ses machines.
  Une machine d'essai éteinte ne met plus en panne un service qui tourne
  ailleurs.
- **Les instances des applications déclarées sont purgées comme les autres**
  après une heure de silence : c'est la déclaration elle-même qui garantit la
  ligne « hors ligne », plus l'entrée entendue. Une machine retirée du parc
  finit donc par disparaître de la liste au lieu d'y rester en panne
  perpétuelle.

## [0.4.1] - 2026-07-22

### Modifié

- **L'onglet Écosystème affiche le nom que chaque service ANNONCE**, plus un
  libellé défini côté morfMonitor. Un service renommé s'affiche désormais
  correctement de lui-même : la configuration n'est plus une seconde source de
  vérité qui pouvait mentir. Seul le préfixe « morf » est normalisé - minuscule,
  lettre suivante en majuscule - de sorte que « morfdashboard » et
  « morfDashboard » se lisent pareil, les majuscules internes
  (« morfTemplateService ») étant conservées. Un nom sans préfixe morf
  (ComponentHub, MeteoHub) est affiché tel qu'annoncé.

## [0.4.0] - 2026-07-22

### Corrigé

- **`online` dit désormais ce qu'on entend, pas ce qu'on a décidé d'écouter.**
  ComponentHub s'affichait « désactivé » avec un heartbeat de neuf secondes.
  L'API forçait `online` à faux dès que `enabled` valait faux, si bien qu'une
  décision de supervision rendait invisible un service qui émettait.

  Ce sont deux faits indépendants. « Est-ce que ça tourne ? » s'observe et ne se
  configure pas. « Dois-je être alerté si ça s'arrête ? » se décide dans
  `morfsystem.json` et ne dit rien de l'état réel. Les confondre empêchait
  l'onglet Écosystème de répondre à la seule question pour laquelle il existe.

  RaspberryDashboard calculait déjà son propre `online` sans consulter
  `enabled` : morfMonitor était le seul des deux à les mélanger.

- **L'interface reflète la même séparation.** Le fait déclaratif passe en
  pastille près du nom - « non supervisé », à côté du « non déclaré » déjà
  présent - et la colonne État montre l'état observé. Un service hors ligne
  n'est en rouge que si quelqu'un a promis le contraire.

## [0.3.9] - 2026-07-21

### Documentation

- **Section « Le système ne fait pas ce que j'attends »**, dans les deux README.
  Un tableau part du **symptôme** et non du concept, parce que quelqu'un de
  perdu arrive avec un symptôme : routes en 503, listes vides, entrée ajoutée
  qui n'apparaît pas, application signalée en permanence, équipement absent de
  l'écosystème.

  Chaque ligne donne la cause et la commande. Les neuf cas listés sont ceux qui
  ont réellement fait perdre du temps pendant le développement.

- **L'avertissement est aussi dans les fichiers que l'on édite.** L'en-tête des
  deux exemples dit désormais que le fichier n'est **pas** lu tel quel, où il
  est déployé, et que le modifier ne change rien tant que le déploiement n'a pas
  été lancé - la cause la plus fréquente de « j'ai pourtant corrigé ça ».

  Celui de `morfmonitor.example.json` était **trompeur** : il citait
  `/etc/morfmonitor/` sans jamais mentionner `/opt/morfmonitor/`, l'emplacement
  où le fichier finit réellement.

- Les deux règles qui expliquent la plupart des surprises sont énoncées
  explicitement : **déclarer, c'est s'attendre** (`enabled: true` transforme une
  absence en anomalie) et **le fichier réel gagne sur l'exemple**.

## [0.3.8] - 2026-07-21

### Corrigé

- **`install` et `update` ignoraient votre configuration réelle.** Tous deux
  codaient `morfmonitor.example.json` en dur, alors que `deploy` préférait déjà
  `config/morfmonitor.json`. Une mise à jour comparait donc votre installation à
  un **modèle** plutôt qu'à votre propre référence. Les trois appliquent
  désormais la même règle : le fichier réel du dépôt s'il existe, l'exemple
  sinon.

  `install` ne recopie plus la configuration lui-même, il **délègue** à
  `deploy-config.sh --if-absent`. Une seule implémentation de la règle, au lieu
  de trois copies qui auraient fini par diverger.

- **`install` et `update` ne traitaient pas `morfsystem.json` du tout.** Une
  installation neuve plaçait la configuration du service mais pas celle du parc :
  morfMonitor démarrait et ne supervisait **rien**, sans raison apparente. Et un
  paramètre apparu dans la description du parc n'atteignait jamais une
  installation existante - exactement le défaut que `update` corrigeait déjà
  pour l'autre fichier.

  Les deux configurations sont désormais traitées par les trois commandes.

### Ajouté

- `deploy-config.sh --if-absent` : ne place que les fichiers manquants, sans
  jamais écraser. C'est ce dont l'installation a besoin - produire un système
  qui fonctionne sans effacer les réglages d'une installation précédente.

### Note

`update` ajoute les **clés** nouvelles, jamais les **entrées de liste** : un
service ajouté à `systemd_services` ou une application ajoutée à `beacon_apps`
n'arrive pas par cette voie, car ce serait activer une surveillance non
demandée. `deploy-config.sh` écrase et les apporte.

## [0.3.7] - 2026-07-21

### Modifié

- **Les applications de bureau passent à `enabled: false` dans
  `morfsystem.example.json`.** `enabled` signifie « je m'attends à ce que cette
  application tourne » : depuis la 0.3.2, une application déclarée, activée et
  absente devient une **anomalie**.

  ComponentHub et SiteWatch sont lancées de temps en temps sur un poste de
  bureau. Les marquer attendues les faisait signaler en panne dès leur
  fermeture - un bruit permanent qui finit par masquer les vraies pannes,
  exactement ce que la correction de la 0.3.2 cherchait à éviter.

  Un commentaire explique désormais quand mettre `true` : ce qui tourne en
  permanence, et rien d'autre. MeteoHub reste à `true` - un capteur météo qui
  s'arrête est une panne.

## [0.3.6] - 2026-07-21

### Ajouté

- **L'adresse IP de la machine figure sur la page État général.** Elle ne vivait
  que dans l'onglet Réseau. Or c'est la première chose que l'on cherche quand un
  client externe - SSH, un client FTP, un signet de navigateur - cesse de se
  connecter après un changement de bail DHCP : la faire chercher dans un second
  onglet transforme une question de trois secondes en enquête.

  Seules les interfaces **réellement actives** sont listées, avec leur nom
  (`192.168.1.105 (wlan0)`). Une interface montée mais sans lien - un `eth0`
  dont le câble est débranché - n'apparaît pas : afficher une adresse qui ne
  porte aucun trafic serait pire que ne rien afficher.

## [0.3.5] - 2026-07-21

### Modifié

- **`deploy-config.sh` déploie désormais les DEUX configurations.** Il ne
  poussait que `morfmonitor.json` vers `/opt` ; la configuration partagée
  passait par un autre script, dans un autre dépôt, sous un autre nom. Cinq
  points d'entrée coexistaient pour une seule opération - `config-tool`,
  `shared-config`, `config shared`, `config deploy`, `deploy-config` - et rien
  ne permettait de deviner lequel faisait quoi.

  Une commande suffit maintenant :

  ```sh
  ./scripts/linux/deploy-config.sh
  ```

  Elle pousse `morfmonitor.json` vers `/opt/morfmonitor/` **et**
  `morfsystem.json` vers `/etc/morfsystem/`, sauvegarde chaque fichier
  existant, affiche les différences appliquées, puis redémarre `morfmonitor`
  **et** `morfdashboard` - la configuration partagée étant lue par les deux,
  ne relancer que l'un laisserait l'autre sur l'ancienne description du parc.

  `--service` et `--shared` restreignent à l'une des deux.

- **Le script est enfin vérifiable.** L'élévation passe par une variable
  (`MT_SUDO`), vide quand on est déjà root et surchargeable pour déployer vers
  un emplacement accessible sans privilèges.

  Sans cela il n'était testable que sur une machine réelle - et le `sudo` que
  fournit Windows 11, qui **renvoie 0 quoi qu'il arrive**, faisait passer un
  test pour concluant alors qu'il ne vérifiait rien : le script annonçait
  « identique » sur deux fichiers différents et une sauvegarde sur un dossier
  inexistant.

- Documentation reprise pour un lecteur qui découvre le projet : un tableau dit
  quel fichier va où et qui le lit, puis une commande. Les autres outils
  (`update-service`, `config-tool`) sont présentés par le besoin auquel ils
  répondent, pas par leur nom.

## [0.3.4] - 2026-07-21

### Modifié

- **`deploy-config` n'exige plus d'être préfixé par `sudo`** : il élève
  lui-même les seules écritures système, comme le fait `config.sh shared`. Les
  deux sous-commandes du point d'entrée unifié demandaient jusqu'ici l'inverse
  l'une de l'autre - une incohérence introduite en les unifiant. La règle est
  désormais unique : **aucune commande `config` ne se préfixe par `sudo`**.

  Lancer tout un script en root pour quelques écritures faisait aussi tourner
  la lecture, la comparaison et l'affichage avec des droits dont ils n'ont
  aucun besoin.

  Côté Windows, il n'existe pas d'équivalent : un script ne peut pas élever une
  seule écriture. Plutôt qu'exiger l'administrateur d'emblée - inutile quand
  `-AppDir` vise un dossier accessible - l'échec d'écriture est intercepté et
  explique précisément la cause. Un « accès refusé » brut enverrait chercher un
  problème de fichier là où il s'agit de droits.

- **Le message affiché quand aucune sonde réseau n'est déclarée était devenu
  faux.** Il invitait à déclarer les ESP32 dans `network_services`, alors que
  MeteoHub et GatewayLab s'annoncent désormais eux-mêmes. Une liste vide n'est
  plus un manque à combler mais l'aboutissement de la migration : le texte
  l'explique, et présente `network_services` comme le dernier recours pour un
  équipement qui ne s'annonce pas.

## [0.3.3] - 2026-07-21

### Corrigé

- **Les liens de l'interface étaient illisibles sur le fond sombre.** Seuls ceux
  du pied de page étaient stylés ; ceux des tableaux gardaient le bleu-violet
  par défaut du navigateur, quasi invisible sur `#1e293b`. Ils reprennent
  désormais la couleur d'accent, avec `:visited` explicite - sans quoi un lien
  déjà ouvert repassait en violet, ce qui touchait précisément les liens vers
  les interfaces, ceux qu'on ouvre le plus souvent.

  Contraste mesuré : **6.83:1**, au-delà du seuil AA (4.5:1).

## [0.3.2] - 2026-07-21

### Corrigé

- **Une application simplement entendue puis arrêtée était signalée comme
  anomalie indéfiniment.** Déclarer, c'est dire « je m'attends à ce service » ;
  une application jamais déclarée n'a été promise à personne. Un outil de bureau
  lancé une fois puis fermé remontait pourtant en anomalie pour toujours, ce qui
  aurait fini par noyer les vraies pannes. Seules les applications **déclarées**
  justifient désormais une alerte.

- **`m_beaconSeen` n'était jamais purgé** - aucun `remove`, `erase` ni `clear`.
  Une application entendue une seule fois y restait à vie, et la table ne
  pouvait que croître. Les entrées **non déclarées** sont désormais oubliées
  après une heure sans annonce : assez long pour qu'une découverte reste
  exploitable, assez court pour qu'une présence ancienne ne se fasse pas passer
  pour une panne actuelle. Les entrées déclarées ne sont jamais purgées : leur
  absence est précisément ce qu'on veut voir.

- **La configuration d'exemple suggérait une structure que le code ne lit pas.**
  Elle plaçait `config_path` dans un sous-objet `"params"`, alors que
  `ServiceConfig::fromJson` affecte l'objet **entier** du module à `params` :
  les paramètres se lisent donc à plat. La configuration partagée n'était jamais
  chargée, sans le moindre message. Le commentaire d'en-tête de `ModuleDef`,
  qui décrivait aussi un sous-objet, est corrigé.

### Modifié

- **MeteoHub passe de `network_services` à `beacon_apps`** dans
  `morfsystem.example.json`. La sonde TCP existait parce que MeteoHub n'était pas
  découvrable ; il l'est depuis son firmware 1.13.0. La sonde suppose de
  connaître une adresse à l'avance - l'inverse d'une découverte - et le
  commentaire la présente désormais comme le mécanisme de dernier recours.

  Le déplacement n'est pas qu'un nettoyage : une application **déclarée** est
  toujours listée, même jamais entendue, donc son absence se voit. Non déclarée,
  elle n'apparaît que si elle s'annonce - si elle ne démarre jamais, personne ne
  l'apprend.

## [0.3.1] - 2026-07-21

### Corrigé

- **morfMonitor annonçait la capacité `web_ui` sans en publier le détail.**
  Il sert une interface Web mais ne la déclarait pas, et sa propre entrée dans
  la page Écosystème n'affichait donc aucun lien.

  La déclaration a révélé un second défaut : morfMonitor sert son **propre**
  `/status` au lieu d'utiliser le `StatusServer` de morfBeacon, et ne
  connaissait donc pas les champs `webUi`. La capacité partait bien dans le
  heartbeat, mais le détail restait introuvable - un observateur ne pouvait pas
  construire le lien.

  Les deux sont corrigés : morfMonitor se déclare comme n'importe quel autre
  service et publie le bloc `web_ui` dans son `/status`. Un observatoire qui
  s'exempterait de ses propres règles n'aurait aucune raison d'être cru sur les
  autres.

  **Tout service réimplémentant `/status` contracte la même obligation** :
  déclarer une capacité sans en servir le détail annonce une interface que
  personne ne saura ouvrir.

  La déclaration est conditionnée à `web_enabled` : annoncer une interface
  désactivée produirait un lien mort.

## [0.3.0] - 2026-07-21

Première étape de l'**observatoire** : morfMonitor ne se contente plus de dire
quels services vivent, il permet d'atteindre ceux qui exposent une interface.
Sans rien connaître d'eux, et sans jamais se mettre sur le chemin.

### Ajouté

- **Le listener beacon conserve l'adresse de l'émetteur et son `status_port`.**
  Le datagramme transportait déjà ce dernier et l'adresse était disponible à la
  réception, mais `BeaconSeen` ne retenait que `lastSeen`, `version`, `host` et
  `state` : morfMonitor savait qu'un service vivait, pas où le joindre. Aucune
  navigation n'était possible.

  L'adresse vient de la **couche réseau**, pas du datagramme : c'est la seule
  dont on soit sûr qu'elle permette de joindre l'émetteur. `host` est un nom
  annoncé, qui ne résout pas forcément depuis la machine qui observe. Les
  adresses IPv4 mappées en IPv6 (`::ffff:192.168.1.55`) sont normalisées, sans
  quoi le lien serait inutilisable.

- **Découverte des interfaces Web déclarées.** Un service annonçant la capacité
  `web_ui` voit son `/status` interrogé **une fois** pour obtenir le détail
  (chemin, libellé, port), puis la page Écosystème propose un lien vers lui.

  C'est le « pull detail » du protocole, pas une sonde périodique : le détail
  n'est redemandé que si la version du service change. Un échec est sans
  conséquence - le service reste supervisé, simplement sans lien.

- `/api/services` expose désormais, par entrée beacon : `ip`, `status_port`,
  `capabilities` et, le cas échéant, `web_ui` complété d'une `url` prête à
  l'emploi. Un consommateur n'a pas à recomposer l'adresse lui-même.

- **Le lien est un `href` ordinaire** vers l'adresse propre du service, ouvert
  dans un nouvel onglet avec `rel="noopener"`. morfMonitor n'est pas sur le
  chemin de la requête : il ne relaie rien, n'ouvre aucune session,
  n'authentifie personne. Couper morfMonitor laisse ces adresses joignables ;
  seule la commodité de les trouver disparaît. C'est l'invariant
  « observatoire, pas portail », et il est vérifiable.

  Ajouter un service à l'écosystème ne demande **aucune modification ici**.

### Limite connue

Sur une machine **multi-domiciliée** (WSL, Hyper-V, VPN), un émetteur diffuse
son heartbeat sur toutes ses interfaces et morfMonitor retient l'adresse du
dernier datagramme reçu. Celle-ci peut appartenir à un réseau virtuel, donc être
injoignable depuis le navigateur d'une autre machine.

Le cas ne se produit pas sur la cible de production (un Raspberry Pi avec une
seule interface active) et n'affecte que le lien, jamais la supervision. Une
sélection préférant l'interface portant la route du réseau local reste à faire.

## [0.2.0] - 2026-07-21

Cette version ajoute une interface Web et corrige une série de défauts qui
partagent tous une même cause : **du code écrit contre un schéma supposé plutôt
que contre le schéma réel**. Les métriques Linux (`/proc`, `/sys`) n'existant pas
sous Windows, les pages concernées n'avaient jamais été rendues avec de vraies
données pendant leur développement. Le dump complet de l'API d'un Raspberry Pi
en production a servi de référence pour tout reprendre.

### Ajouté

- **Prise en charge de `HEAD`.** Le serveur répondait 405 à toute requête HEAD.
  Un service de supervision est précisément ce que l'on sonde : une sonde
  externe en HEAD concluait que morfMonitor était en panne alors qu'il
  répondait parfaitement. HEAD suit désormais le même routage que GET, renvoie
  les mêmes en-têtes - `Content-Length` compris, comme l'exige HTTP - sans le
  corps. Les réponses 405 portent un en-tête `Allow`.

- **`Cache-Control: no-store` sur toutes les réponses.** Une réponse `/api/` en
  cache afficherait un état périmé dans un outil de supervision, soit le
  contraire de sa raison d'être ; et un asset en cache fait survivre l'ancienne
  interface à une mise à jour du binaire. Ce second cas s'est produit pendant la
  vérification.

- **Interface Web, servie à la racine sur le même port que l'API.** L'ajout de
  services à l'écosystème dépasse ce qu'un écran embarqué peut montrer :
  RaspberryDashboard reste la vue synthétique, l'interface Web devient la vue
  détaillée. Six pages organisées par domaine - état général, ressources,
  réseau, services morfSystem, écosystème, diagnostic - plutôt qu'une liste de
  métriques sans structure.

  L'écran OLED répond à « est-ce que tout va bien ? ». L'interface Web répond à
  « pourquoi ? ».

- **La vue Web est un client de l'API publique, pas un initié.** Les pages sont
  servies comme des fichiers inertes : aucun gabarit, aucune donnée injectée
  côté serveur. Elles lisent `/api/all` et `/status` exactement comme le fait
  RaspberryDashboard.

  Cette contrainte n'est pas cosmétique. morfMonitor annonce « il n'affiche
  rien » : sa responsabilité est de collecter et d'exposer. Tant que la vue Web
  reste cliente de l'API, elle n'est qu'une **seconde vue** - extractible vers
  un projet séparé sans réécriture, si elle devait un jour le devenir. Le jour
  où elle lirait `MonitorModule` directement, cette propriété serait perdue en
  silence.

- Drapeau `web_enabled` (défaut `true`). À `false`, seules les routes JSON
  répondent : le service redevient une API pure sans autre changement.

- Les métriques absentes d'une plateforme (CPU, mémoire, charge et température
  viennent de `/proc` et `/sys`, donc de Linux) affichent un message nommant
  **ce qui manque et pourquoi**, au lieu d'une case vide ou d'un `0` qui se
  lirait comme une mesure.

- **Parité complète des scripts Linux et Windows.** `scripts/windows/` n'avait
  qu'`install-service.ps1` ; chaque script de `scripts/linux/` a désormais son
  homologue (`update-service.ps1`, `deploy-config.ps1`, `config-tool.ps1`).

  La logique JSON **n'est pas convertie en shell** : Python est le seul des
  trois langages de l'écosystème qui tourne à l'identique sous Windows, Linux
  et Raspberry Pi. Les `.ps1` appellent les mêmes `.py`. Réécrire une fusion
  JSON récursive en Bash *et* en PowerShell donnerait deux implémentations
  libres de diverger, à propos du fichier qui décide si le service fonctionne -
  même raison que `morfTools/scripts/ecosystem-check.py`, partagé par `morf.sh`
  et `morf.ps1`.

  `check-config.py` accepte `--hint-style sh|ps1` : l'appelant déclare
  l'outillage à citer dans ses conseils. `os.name` ne suffisait pas - il décrit
  l'interpréteur, pas le shell appelant.

- **`scripts/linux/deploy-config.sh` et son équivalent PowerShell.** La voie
  directe : copier la configuration du dépôt par-dessus celle du service, sans
  fusion et sans Python. La source est `config/morfmonitor.json` si ce fichier
  existe, l'exemple sinon. Sauvegarde datée et aperçu plafonné des différences
  avant écrasement - écraser sans montrer quoi serait une mauvaise façon de
  simplifier.

- **`scripts/linux/config-tool.sh` : gestion à la demande de la configuration
  déployée.** L'installation et la mise à jour ne remplacent jamais
  `morfmonitor.json` - il porte des réglages locaux irrécupérables. C'est la
  bonne règle, mais elle laissait un angle mort : `merge-config.py` ajoute les
  clés apparues depuis l'installation, il ne corrige pas une valeur **déjà
  présente devenue invalide**.

  C'est précisément ce qui s'est produit : la configuration déployée déclarait
  encore un module `example`, la clé `modules` existait donc la fusion n'y
  touchait pas, et le service tournait en répondant 503 partout. Le nouveau
  binaire était bien copié ; la configuration, elle, restait figée.

  Réconcilier une valeur existante ne peut pas être automatique - seul
  l'utilisateur sait si une valeur est un réglage voulu ou un résidu. L'outil
  rend donc l'opération explicite : `status`, `check`, `diff`, `merge`
  (ajout seul), `reset` (remplacement, confirmation requise). Toute écriture est
  précédée d'une sauvegarde datée. Vocabulaire aligné sur
  `morfTools/shared-config.sh`.

- **`scripts/linux/check-config.py` : diagnostic d'une configuration déployée.**
  Il interroge le binaire lui-même (`--list-types`) plutôt que de coder en dur
  les types valides : la vérification reste juste quand la fabrique évolue. Il
  signale un type de module inconnu, une absence totale de module exploitable,
  les clés manquantes et l'exposition réseau. Il ne modifie rien.

  `update-service.sh` l'exécute après chaque mise à jour : une configuration
  périmée s'annonce désormais au lieu d'être découverte par un service muet.

### Corrigé

- **La page Ressources était calée sur un schéma erroné.** `cpu_percent` et
  `cpu_freq_mhz` sont **à plat**, pas dans un objet `cpu` : la carte Processeur
  ne s'affichait donc jamais, et un message annonçait à tort que le CPU n'était
  pas collecté sur cette plateforme. `load` est un **tableau**
  `[1 min, 5 min, 15 min]`, lu comme un objet : la carte Charge affichait trois
  tirets sur une machine dont la charge était parfaitement mesurée.

  `temperature` et `throttling` n'étaient pas affichés du tout. C'est le plus
  coûteux des trois oublis : le collecteur écrit lui-même que le bridage « est
  le diagnostic le plus utile d'un Pi instable, et il n'apparaît nulle part
  ailleurs ». Une sous-tension corrompt la carte SD et fige des services sans
  rien écrire dans les journaux. Le bridage a désormais sa carte, distingue
  « maintenant » de « depuis le démarrage », et remonte au premier rang des
  anomalies avec la température CPU (seuils 70 / 80 °C).

  Le champ `model` (modèle de la machine), collecté mais jamais affiché, est
  ajouté à la carte Machine.

- **`reboot.confidence` est une fraction, pas un pourcentage.** Un diagnostic
  fiable à 70 % s'affichait « 0.7 % », donc comme une quasi-certitude d'erreur.

- **Les entrées beacon désactivées étaient peintes en rouge « hors ligne ».**
  Une application volontairement désactivée (`enabled: false`) n'est pas en
  panne - même confusion que celle corrigée pour les sondes réseau.

- **La carte « Configuration partagée » annonçait « non chargée » en
  permanence.** Elle lisait `all.monitor.config_loaded`, or `/api/all` n'expose
  que `system`, `resources`, `network`, `services` et `reboot` : il n'existe
  aucune clé `monitor`. Elle lit désormais `/api/config`, dont les clés sont
  `loaded` et `path`, et résume ce que le fichier déclare.

- **État des interfaces réseau clarifié.** Une interface `up` sans `running`
  est montée administrativement mais sans porteuse (câble débranché, WiFi non
  associé) : l'état affiché est « sans lien » et non « montée ». Les adresses
  IPv6 sont listées au lieu d'être masquées derrière un compteur, sur la page
  dont le rôle est justement le détail.

- **L'interface Web contredisait la réalité sur la page « Services
  morfSystem ».** Les six services affichaient un badge « arrêté » à côté d'une
  colonne indiquant « active » - une contradiction dans la même ligne - et
  MeteoHub était noté « injoignable » alors qu'il répondait.

  Cause : l'interface avait été écrite contre un schéma JSON **supposé**. Sous
  Windows, `systemd` et `network` sont vides par construction ; les tableaux
  n'ont donc jamais été rendus avec des données réelles, et les noms de champs
  ont été devinés. Le booléen des unités est `active`, pas `running` ; le
  sous-état est `sub_state`, pas `sub`. `u.running` valant toujours
  `undefined`, chaque service était déclaré arrêté.

- **Plus grave que des noms de champs : l'interface écrasait des états que le
  service prend soin de distinguer.** `Supervisor` renvoie quatre états de
  sonde - `online`, `offline`, `pending`, `disabled` - avec ce commentaire
  explicite : « *On ne ment pas : « pas encore sondé » n'est pas « hors
  ligne »* ». Pendant le délai de grâce mDNS du démarrage, une sonde est
  `pending` ; l'affichage binaire la déclarait injoignable, annulant
  exactement la précaution du collecteur. Idem pour systemd, dont les états
  (`active`, `inactive`, `failed`, `activating`, `disabled`) étaient réduits à
  deux.

  L'interface rend désormais l'état réel, ajoute la latence ou le message
  d'erreur des sondes, et signale le délai de grâce quand il s'applique. Les
  anomalies excluent les unités volontairement désactivées et les sondes en
  attente : les signaler noierait les vraies pannes sous du bruit prévisible.

- **Les avertissements et les erreurs n'atteignaient jamais le journal.**
  `err()` était un `QTextStream` sur `stderr` qui n'était jamais vidé - seul
  `out()` l'était. Un démon systemd ne se terminant pas, tout ce qui passait
  par `err()` restait dans le tampon : type de module inconnu, configuration
  introuvable, échec d'écoute du port. `journalctl -u morfmonitor` ne montrait
  rien, et le service paraissait sain alors qu'il annonçait sa panne.

  C'est ce silence qui rendait les deux défauts ci-dessous indiagnosticables :
  ils étaient signalés, mais personne ne pouvait le voir. Les écritures passent
  désormais par `errLine()`, qui vide le flux à chaque appel.

- **Sans configuration, le service démarrait avec un module `example` inconnu
  de sa propre fabrique** - donc 0 module actif et 503 sur toutes les routes
  `/api/`. Le repli produisait un service qui *avait l'air* vivant : il
  démarrait, écoutait, annonçait sa présence sur le LAN, mais ne supervisait
  rien. Il déclare maintenant un module `monitor`, la configuration partagée
  étant facultative : la machine reste supervisée même sans `morfsystem.json`.

- **L'interface Web confondait « injoignable » et « joignable mais sans
  données ».** Sur un 503, elle levait une exception avant de lire le corps de
  la réponse, si bien que le message explicatif prévu pour ce cas était
  inatteignable. Elle affichait « injoignable » pour un service qui répondait
  parfaitement - envoyant chercher une panne réseau là où il s'agissait d'une
  configuration. Les deux cas sont désormais distingués, et le corps de la
  réponse 503 est lu et affiché avec la marche à suivre.

- **La configuration d'exemple déclarait un module `example`, inconnu de la
  fabrique de morfMonitor** - un résidu du gabarit, jamais adapté au clonage.
  Seul le type `monitor` est reconnu. Toute personne copiant
  `morfmonitor.example.json` obtenait donc un service qui démarre normalement,
  annonce sa présence sur le LAN, mais dont **chaque route `/api/` répond 503
  « aucun module de supervision actif »**. Le défaut se manifestait à
  l'exécution seulement, et le service paraissait sain vu de l'extérieur.

### Modifié

- **Le plan d'adressage du parc quitte ce projet.** Le champ `_comment_port` de
  `config/morfmonitor.example.json` était le seul endroit de l'écosystème où le
  plan des ports était écrit - alors que morfMonitor n'a aucune autorité sur les
  autres composants. Cette copie partielle d'un fait valable pour tout le parc
  était déjà incomplète : elle omettait 8789 (morfNotify) et 8787 (défaut du
  serveur de statut morfBeacon). Un développeur la consultant pour choisir un
  port libre obtenait une information fausse sans aucun moyen de le savoir.

  Le registre vit désormais dans `ports.allocations` de
  `morfTools/ecosystem.json`, seul artefact ayant autorité sur l'ensemble, et
  `morf doctor` vérifie que la valeur déclarée ici lui correspond. Le port
  d'écoute de morfMonitor (8790) est inchangé ; seul le commentaire l'est.

## [0.1.1] - 2026-07-20

### Corrigé

- **La documentation décrivait morfTemplateService, pas morfMonitor.** L'index
  présentait le dépôt comme un « squelette réutilisable », `ARCHITECTURE.md`
  documentait `ExampleModule` (inexistant) en affirmant que le projet ne
  contenait aucun métier, et `CONTRIBUTING.md` annonçait aux contributeurs
  qu'ils modifiaient le template commun. Ces documents décrivent maintenant
  `MonitorModule`, les collecteurs et l'API `/api/…` réelle.
- **`README.md` ne référençait jamais son dossier `docs/`** : la documentation
  existait mais restait inatteignable. Une section Documentation a été ajoutée.
- `ROADMAP.md` réécrit pour morfMonitor (il énonçait la feuille de route du
  template et, en non-objectif, l'absence de toute logique métier).

### Supprimé

- `docs/fr/INTEGRATION.md` et `scripts/new-service.{sh,ps1}` : artefacts de
  morfTemplateService hérités à la création du dépôt. morfMonitor n'est pas un
  template ; ces fichiers proposaient de le cloner en un « nouveau service ».
  L'entrée « Guide de création de service mis à jour » de cette même section
  devient sans objet et disparaît avec eux.

## [0.1.0] - 2026-07-20

### Corrigé

- **Collision de port avec morfAnalytics.** Le clone avait hérité du port
  **8799** du modèle, déjà attribué à morfAnalytics. Une fois morfMonitor
  installé en service, il prenait le port au démarrage et morfAnalytics ne
  pouvait plus écouter : il sortait en erreur et systemd le relançait en
  boucle - **249 redémarrages** constatés. morfMonitor écoute désormais sur
  **8790**, et le fichier d'exemple rappelle l'attribution des ports du parc
  (8080 morfSync, 8788 morfSensor, 8790 morfMonitor, 8799 morfAnalytics).
- **Réglages fantômes dans la configuration partagée.** `monitor.http_port`,
  `monitor.bind_address` et `monitor.cache_ttl_ms` y étaient lus mais **jamais
  utilisés** : le port réel vient de la configuration propre au service. Ils
  affichaient 8790 alors que le service écoutait sur 8799, ce qui a masqué la
  collision. Un réglage qui ne règle rien est pire qu'un réglage absent : ils
  sont supprimés, et le fichier partagé décrit désormais uniquement **ce qui est
  supervisé**, pas la manière dont chaque service écoute.
- **La configuration n'était lue qu'au démarrage.** Un service lancé avant que
  `/etc/morfsystem/morfsystem.json` existe - ordre de démarrage, installation en
  cours - restait aveugle jusqu'à son prochain redémarrage : il répondait
  correctement, mais ne supervisait rien, ce qui est le pire des deux mondes.
  Le chargement est désormais retenté tant qu'il n'a pas abouti, et les caches
  bâtis sur une configuration vide sont invalidés dès qu'elle arrive.

  Constaté en conditions réelles : le service avait démarré à 04:00, le fichier
  partagé a été créé à 04:13, et morfMonitor ne l'a jamais vu.



- **Première version de morfMonitor**, la source unique de vérité sur l'état
  d'une machine. Il collecte, maintient en cache et expose en JSON ; il
  n'affiche rien. Créé à partir de morfTemplateService, dont il ne subsiste
  aucune référence.
- **Collecte système** (`GET /api/system`) : nom d'hôte, OS, noyau,
  architecture, modèle de machine, uptime, heure de démarrage.
- **Collecte des ressources** (`GET /api/resources`) : taux et fréquence CPU,
  charge, mémoire, swap, disque, températures CPU et GPU, et **bits de bridage**
  du Raspberry Pi (sous-tension, limite thermique) - le diagnostic le plus utile
  d'une machine instable, absent partout ailleurs.
- **Collecte réseau** (`GET /api/network`) : interfaces, IPv4, IPv6, adresse
  MAC, état. Les adresses lien-local sont écartées : elles encombrent sans
  informer.
- **Supervision** (`GET /api/services`) : services systemd, sondes TCP des
  équipements non-systemd (un ESP32 ne répond pas à `systemctl`), et
  applications découvertes par heartbeat morfBeacon. Les applications entendues
  mais **non déclarées** sont listées et marquées comme telles : c'est un outil
  de découverte qui indique quoi ajouter à la configuration.
- **Cause du dernier redémarrage** (`GET /api/reboot`) : distingue redémarrage
  demandé, mise à jour, coupure d'alimentation, panique noyau, chien de garde et
  démarrage propre, en croisant les traces d'arrêt du journal précédent, le
  journal de paquets et les messages du noyau. Chaque réponse porte un degré de
  **confiance** et l'**indice** retenu ; quand rien ne tranche, la réponse est
  `unknown` plutôt qu'un « démarrage normal » affirmé par défaut, qui masquerait
  une coupure.
- **Configuration partagée** `/etc/morfsystem/morfsystem.json`, lue par
  morfMonitor (C++) **et** RaspberryDashboard (Python). Source unique de vérité
  des composants supervisés : ajouter un service, une sonde ou une application
  ne demande que d'éditer ce fichier. Remplace `SERVICE_LABELS`,
  `NETWORK_SERVICES` et `BEACON_APPS`, autrefois codés dans le Dashboard.
- **Cache par catégorie**, avec une fraîcheur propre à chacune. Lire
  `/proc/meminfo` est instantané, lancer `systemctl` coûte un processus, sonder
  un ESP32 peut prendre une seconde : leur imposer la même cadence gaspillerait
  d'un côté et ferait attendre de l'autre. Sans cache, dix clients rafraîchissant
  chaque seconde provoqueraient dix lectures système par seconde - l'inverse du
  but recherché, qui est de soulager la machine en centralisant.

### Choix de conception

- **Le service démarre même sans configuration.** Un superviseur qui refuse de
  démarrer parce que son fichier manque est inutile au moment précis où on en a
  besoin. Il sert alors ce qu'il peut et signale le problème dans `/status`.
- **Aucun collecteur n'échoue bruyamment.** Une donnée indisponible (capteur
  absent, commande manquante) est omise ; le reste continue d'être servi.
- **La mesure CPU est amorcée au démarrage.** `/proc/stat` ne donne que des
  compteurs cumulés : sans une première lecture d'amorçage, la toute première
  requête renverrait un CPU absent, que les clients afficheraient comme 0 % -
  une valeur fausse, et non « inconnue ».

### Vérifié sur le matériel

Compilé et exécuté sur le Raspberry Pi 4 cible (Debian 13, noyau 6.18, aarch64,
Qt 6) : les sept routes répondent avec des valeurs correctes, la sonde réseau
atteint MeteoHub en 194 ms, les cinq services systemd sont correctement
rapportés, et la découverte beacon a repéré morfNotify, morfAnalytics et
morfSensor comme non déclarés.

### Limitations connues

- Le service **n'est pas encore installé en unité systemd** sur la machine
  cible : il a été validé en exécution directe.
- L'écart de comptage entre les deux modes du Dashboard (9 pastilles via
  morfMonitor, 8 en mode local) n'est pas résorbé : en mode local, une sonde
  réseau n'apparaît que si sa clé figure aussi dans `SERVICE_LABELS`, contrainte
  héritée de l'implémentation d'origine.
- Les notifications de redémarrage enrichies sont câblées **côté Dashboard**,
  qui enrichit son envoi existant avec la cause fournie par `/api/reboot`.
  Contrepartie assumée : morfMonitor n'émet lui-même aucune notification, donc
  si le Dashboard est arrêté, aucune notification de redémarrage ne part. Un
  second émetteur dans morfMonitor en aurait produit deux par redémarrage.
- L'indicateur visuel de source (« ✓ morfMonitor » / « ⚠ mode local ») n'est pas
  encore dessiné à l'écran : la donnée est disponible (`info["source"]`), son
  affichage reste à faire.
