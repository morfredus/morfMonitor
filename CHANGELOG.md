# Journal des versions — morfMonitor

Le format s'inspire de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/)
et du [versionnage sémantique](https://semver.org/lang/fr/).

## [Non publié]

## [0.5.2] – 2026-07-22

### Corrigé

- **Le stockage couvre tous les volumes montés, plus la seule racine.** Sur
  une machine où `/home` est une partition séparée — installation Linux
  classique sur un portable — la page Ressources pouvait afficher « / » à
  90 % et affoler alors que les données avaient ailleurs toute la place, ou à
  l'inverse taire un `/home` plein. L'API expose désormais `disks` : un objet
  par système de fichiers adossé à un périphérique (`/dev/…`), pseudo-montages
  écartés (tmpfs, squashfs des snaps — en lecture seule, toujours « pleins » à
  100 %), montages bind dédupliqués, tri par point de montage. `disk` (la
  seule racine) est conservé pour les consommateurs existants.

### Modifié

- **Une carte Stockage par volume** dans la page Ressources, et **une jauge
  d'anomalie par volume** (« Stockage /home » à 92 % est signalé pour
  lui-même, avec son point de montage dans le libellé).

## [0.5.1] – 2026-07-22

### Modifié

- **Le texte explicatif de l'onglet Écosystème est réécrit, un paragraphe par
  idée** : découverte automatique, interrogation unique des interfaces Web,
  sens de « non déclaré », une ligne par machine, délai de mise hors ligne.
  Le pavé d'un seul tenant se consultait mal. Le délai reste tiré de la
  configuration (`beacon.offline_after_s`), jamais écrit en dur.

## [0.5.0] – 2026-07-22

### Corrigé

- **Deux machines faisant tourner le même service ne s'écrasent plus l'une
  l'autre.** La découverte était indexée par le nom `app` : deux instances du
  même service (un morfMonitor sur le Pi, un autre sur un portable) se
  partageaient une seule entrée, et l'affichage alternait entre les hôtes à
  chaque heartbeat. La découverte est désormais indexée par l'**identité
  d'instance** — le champ `instance` (`app@host`) que PROTOCOL.md avait prévu
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

## [0.4.1] – 2026-07-22

### Modifié

- **L'onglet Écosystème affiche le nom que chaque service ANNONCE**, plus un
  libellé défini côté morfMonitor. Un service renommé s'affiche désormais
  correctement de lui-même : la configuration n'est plus une seconde source de
  vérité qui pouvait mentir. Seul le préfixe « morf » est normalisé — minuscule,
  lettre suivante en majuscule — de sorte que « morfdashboard » et
  « morfDashboard » se lisent pareil, les majuscules internes
  (« morfTemplateService ») étant conservées. Un nom sans préfixe morf
  (ComponentHub, MeteoHub) est affiché tel qu'annoncé.

## [0.4.0] – 2026-07-22

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
  pastille près du nom — « non supervisé », à côté du « non déclaré » déjà
  présent — et la colonne État montre l'état observé. Un service hors ligne
  n'est en rouge que si quelqu'un a promis le contraire.

## [0.3.9] – 2026-07-21

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
  été lancé — la cause la plus fréquente de « j'ai pourtant corrigé ça ».

  Celui de `morfmonitor.example.json` était **trompeur** : il citait
  `/etc/morfmonitor/` sans jamais mentionner `/opt/morfmonitor/`, l'emplacement
  où le fichier finit réellement.

- Les deux règles qui expliquent la plupart des surprises sont énoncées
  explicitement : **déclarer, c'est s'attendre** (`enabled: true` transforme une
  absence en anomalie) et **le fichier réel gagne sur l'exemple**.

## [0.3.8] – 2026-07-21

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
  installation existante — exactement le défaut que `update` corrigeait déjà
  pour l'autre fichier.

  Les deux configurations sont désormais traitées par les trois commandes.

### Ajouté

- `deploy-config.sh --if-absent` : ne place que les fichiers manquants, sans
  jamais écraser. C'est ce dont l'installation a besoin — produire un système
  qui fonctionne sans effacer les réglages d'une installation précédente.

### Note

`update` ajoute les **clés** nouvelles, jamais les **entrées de liste** : un
service ajouté à `systemd_services` ou une application ajoutée à `beacon_apps`
n'arrive pas par cette voie, car ce serait activer une surveillance non
demandée. `deploy-config.sh` écrase et les apporte.

## [0.3.7] – 2026-07-21

### Modifié

- **Les applications de bureau passent à `enabled: false` dans
  `morfsystem.example.json`.** `enabled` signifie « je m'attends à ce que cette
  application tourne » : depuis la 0.3.2, une application déclarée, activée et
  absente devient une **anomalie**.

  ComponentHub et SiteWatch sont lancées de temps en temps sur un poste de
  bureau. Les marquer attendues les faisait signaler en panne dès leur
  fermeture — un bruit permanent qui finit par masquer les vraies pannes,
  exactement ce que la correction de la 0.3.2 cherchait à éviter.

  Un commentaire explique désormais quand mettre `true` : ce qui tourne en
  permanence, et rien d'autre. MeteoHub reste à `true` — un capteur météo qui
  s'arrête est une panne.

## [0.3.6] – 2026-07-21

### Ajouté

- **L'adresse IP de la machine figure sur la page État général.** Elle ne vivait
  que dans l'onglet Réseau. Or c'est la première chose que l'on cherche quand un
  client externe — SSH, un client FTP, un signet de navigateur — cesse de se
  connecter après un changement de bail DHCP : la faire chercher dans un second
  onglet transforme une question de trois secondes en enquête.

  Seules les interfaces **réellement actives** sont listées, avec leur nom
  (`192.168.1.105 (wlan0)`). Une interface montée mais sans lien — un `eth0`
  dont le câble est débranché — n'apparaît pas : afficher une adresse qui ne
  porte aucun trafic serait pire que ne rien afficher.

## [0.3.5] – 2026-07-21

### Modifié

- **`deploy-config.sh` déploie désormais les DEUX configurations.** Il ne
  poussait que `morfmonitor.json` vers `/opt` ; la configuration partagée
  passait par un autre script, dans un autre dépôt, sous un autre nom. Cinq
  points d'entrée coexistaient pour une seule opération — `config-tool`,
  `shared-config`, `config shared`, `config deploy`, `deploy-config` — et rien
  ne permettait de deviner lequel faisait quoi.

  Une commande suffit maintenant :

  ```sh
  ./scripts/linux/deploy-config.sh
  ```

  Elle pousse `morfmonitor.json` vers `/opt/morfmonitor/` **et**
  `morfsystem.json` vers `/etc/morfsystem/`, sauvegarde chaque fichier
  existant, affiche les différences appliquées, puis redémarre `morfmonitor`
  **et** `morfdashboard` — la configuration partagée étant lue par les deux,
  ne relancer que l'un laisserait l'autre sur l'ancienne description du parc.

  `--service` et `--shared` restreignent à l'une des deux.

- **Le script est enfin vérifiable.** L'élévation passe par une variable
  (`MT_SUDO`), vide quand on est déjà root et surchargeable pour déployer vers
  un emplacement accessible sans privilèges.

  Sans cela il n'était testable que sur une machine réelle — et le `sudo` que
  fournit Windows 11, qui **renvoie 0 quoi qu'il arrive**, faisait passer un
  test pour concluant alors qu'il ne vérifiait rien : le script annonçait
  « identique » sur deux fichiers différents et une sauvegarde sur un dossier
  inexistant.

- Documentation reprise pour un lecteur qui découvre le projet : un tableau dit
  quel fichier va où et qui le lit, puis une commande. Les autres outils
  (`update-service`, `config-tool`) sont présentés par le besoin auquel ils
  répondent, pas par leur nom.

## [0.3.4] – 2026-07-21

### Modifié

- **`deploy-config` n'exige plus d'être préfixé par `sudo`** : il élève
  lui-même les seules écritures système, comme le fait `config.sh shared`. Les
  deux sous-commandes du point d'entrée unifié demandaient jusqu'ici l'inverse
  l'une de l'autre — une incohérence introduite en les unifiant. La règle est
  désormais unique : **aucune commande `config` ne se préfixe par `sudo`**.

  Lancer tout un script en root pour quelques écritures faisait aussi tourner
  la lecture, la comparaison et l'affichage avec des droits dont ils n'ont
  aucun besoin.

  Côté Windows, il n'existe pas d'équivalent : un script ne peut pas élever une
  seule écriture. Plutôt qu'exiger l'administrateur d'emblée — inutile quand
  `-AppDir` vise un dossier accessible — l'échec d'écriture est intercepté et
  explique précisément la cause. Un « accès refusé » brut enverrait chercher un
  problème de fichier là où il s'agit de droits.

- **Le message affiché quand aucune sonde réseau n'est déclarée était devenu
  faux.** Il invitait à déclarer les ESP32 dans `network_services`, alors que
  MeteoHub et GatewayLab s'annoncent désormais eux-mêmes. Une liste vide n'est
  plus un manque à combler mais l'aboutissement de la migration : le texte
  l'explique, et présente `network_services` comme le dernier recours pour un
  équipement qui ne s'annonce pas.

## [0.3.3] – 2026-07-21

### Corrigé

- **Les liens de l'interface étaient illisibles sur le fond sombre.** Seuls ceux
  du pied de page étaient stylés ; ceux des tableaux gardaient le bleu-violet
  par défaut du navigateur, quasi invisible sur `#1e293b`. Ils reprennent
  désormais la couleur d'accent, avec `:visited` explicite — sans quoi un lien
  déjà ouvert repassait en violet, ce qui touchait précisément les liens vers
  les interfaces, ceux qu'on ouvre le plus souvent.

  Contraste mesuré : **6.83:1**, au-delà du seuil AA (4.5:1).

## [0.3.2] – 2026-07-21

### Corrigé

- **Une application simplement entendue puis arrêtée était signalée comme
  anomalie indéfiniment.** Déclarer, c'est dire « je m'attends à ce service » ;
  une application jamais déclarée n'a été promise à personne. Un outil de bureau
  lancé une fois puis fermé remontait pourtant en anomalie pour toujours, ce qui
  aurait fini par noyer les vraies pannes. Seules les applications **déclarées**
  justifient désormais une alerte.

- **`m_beaconSeen` n'était jamais purgé** — aucun `remove`, `erase` ni `clear`.
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
  connaître une adresse à l'avance — l'inverse d'une découverte — et le
  commentaire la présente désormais comme le mécanisme de dernier recours.

  Le déplacement n'est pas qu'un nettoyage : une application **déclarée** est
  toujours listée, même jamais entendue, donc son absence se voit. Non déclarée,
  elle n'apparaît que si elle s'annonce — si elle ne démarre jamais, personne ne
  l'apprend.

## [0.3.1] – 2026-07-21

### Corrigé

- **morfMonitor annonçait la capacité `web_ui` sans en publier le détail.**
  Il sert une interface Web mais ne la déclarait pas, et sa propre entrée dans
  la page Écosystème n'affichait donc aucun lien.

  La déclaration a révélé un second défaut : morfMonitor sert son **propre**
  `/status` au lieu d'utiliser le `StatusServer` de morfBeacon, et ne
  connaissait donc pas les champs `webUi`. La capacité partait bien dans le
  heartbeat, mais le détail restait introuvable — un observateur ne pouvait pas
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

## [0.3.0] – 2026-07-21

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
  conséquence — le service reste supervisé, simplement sans lien.

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

## [0.2.0] – 2026-07-21

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
  les mêmes en-têtes — `Content-Length` compris, comme l'exige HTTP — sans le
  corps. Les réponses 405 portent un en-tête `Allow`.

- **`Cache-Control: no-store` sur toutes les réponses.** Une réponse `/api/` en
  cache afficherait un état périmé dans un outil de supervision, soit le
  contraire de sa raison d'être ; et un asset en cache fait survivre l'ancienne
  interface à une mise à jour du binaire. Ce second cas s'est produit pendant la
  vérification.

- **Interface Web, servie à la racine sur le même port que l'API.** L'ajout de
  services à l'écosystème dépasse ce qu'un écran embarqué peut montrer :
  RaspberryDashboard reste la vue synthétique, l'interface Web devient la vue
  détaillée. Six pages organisées par domaine — état général, ressources,
  réseau, services morfSystem, écosystème, diagnostic — plutôt qu'une liste de
  métriques sans structure.

  L'écran OLED répond à « est-ce que tout va bien ? ». L'interface Web répond à
  « pourquoi ? ».

- **La vue Web est un client de l'API publique, pas un initié.** Les pages sont
  servies comme des fichiers inertes : aucun gabarit, aucune donnée injectée
  côté serveur. Elles lisent `/api/all` et `/status` exactement comme le fait
  RaspberryDashboard.

  Cette contrainte n'est pas cosmétique. morfMonitor annonce « il n'affiche
  rien » : sa responsabilité est de collecter et d'exposer. Tant que la vue Web
  reste cliente de l'API, elle n'est qu'une **seconde vue** — extractible vers
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
  libres de diverger, à propos du fichier qui décide si le service fonctionne —
  même raison que `morfTools/scripts/ecosystem-check.py`, partagé par `morf.sh`
  et `morf.ps1`.

  `check-config.py` accepte `--hint-style sh|ps1` : l'appelant déclare
  l'outillage à citer dans ses conseils. `os.name` ne suffisait pas — il décrit
  l'interpréteur, pas le shell appelant.

- **`scripts/linux/deploy-config.sh` et son équivalent PowerShell.** La voie
  directe : copier la configuration du dépôt par-dessus celle du service, sans
  fusion et sans Python. La source est `config/morfmonitor.json` si ce fichier
  existe, l'exemple sinon. Sauvegarde datée et aperçu plafonné des différences
  avant écrasement — écraser sans montrer quoi serait une mauvaise façon de
  simplifier.

- **`scripts/linux/config-tool.sh` : gestion à la demande de la configuration
  déployée.** L'installation et la mise à jour ne remplacent jamais
  `morfmonitor.json` — il porte des réglages locaux irrécupérables. C'est la
  bonne règle, mais elle laissait un angle mort : `merge-config.py` ajoute les
  clés apparues depuis l'installation, il ne corrige pas une valeur **déjà
  présente devenue invalide**.

  C'est précisément ce qui s'est produit : la configuration déployée déclarait
  encore un module `example`, la clé `modules` existait donc la fusion n'y
  touchait pas, et le service tournait en répondant 503 partout. Le nouveau
  binaire était bien copié ; la configuration, elle, restait figée.

  Réconcilier une valeur existante ne peut pas être automatique — seul
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
  panne — même confusion que celle corrigée pour les sondes réseau.

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
  colonne indiquant « active » — une contradiction dans la même ligne — et
  MeteoHub était noté « injoignable » alors qu'il répondait.

  Cause : l'interface avait été écrite contre un schéma JSON **supposé**. Sous
  Windows, `systemd` et `network` sont vides par construction ; les tableaux
  n'ont donc jamais été rendus avec des données réelles, et les noms de champs
  ont été devinés. Le booléen des unités est `active`, pas `running` ; le
  sous-état est `sub_state`, pas `sub`. `u.running` valant toujours
  `undefined`, chaque service était déclaré arrêté.

- **Plus grave que des noms de champs : l'interface écrasait des états que le
  service prend soin de distinguer.** `Supervisor` renvoie quatre états de
  sonde — `online`, `offline`, `pending`, `disabled` — avec ce commentaire
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
  `err()` était un `QTextStream` sur `stderr` qui n'était jamais vidé — seul
  `out()` l'était. Un démon systemd ne se terminant pas, tout ce qui passait
  par `err()` restait dans le tampon : type de module inconnu, configuration
  introuvable, échec d'écoute du port. `journalctl -u morfmonitor` ne montrait
  rien, et le service paraissait sain alors qu'il annonçait sa panne.

  C'est ce silence qui rendait les deux défauts ci-dessous indiagnosticables :
  ils étaient signalés, mais personne ne pouvait le voir. Les écritures passent
  désormais par `errLine()`, qui vide le flux à chaque appel.

- **Sans configuration, le service démarrait avec un module `example` inconnu
  de sa propre fabrique** — donc 0 module actif et 503 sur toutes les routes
  `/api/`. Le repli produisait un service qui *avait l'air* vivant : il
  démarrait, écoutait, annonçait sa présence sur le LAN, mais ne supervisait
  rien. Il déclare maintenant un module `monitor`, la configuration partagée
  étant facultative : la machine reste supervisée même sans `morfsystem.json`.

- **L'interface Web confondait « injoignable » et « joignable mais sans
  données ».** Sur un 503, elle levait une exception avant de lire le corps de
  la réponse, si bien que le message explicatif prévu pour ce cas était
  inatteignable. Elle affichait « injoignable » pour un service qui répondait
  parfaitement — envoyant chercher une panne réseau là où il s'agissait d'une
  configuration. Les deux cas sont désormais distingués, et le corps de la
  réponse 503 est lu et affiché avec la marche à suivre.

- **La configuration d'exemple déclarait un module `example`, inconnu de la
  fabrique de morfMonitor** — un résidu du gabarit, jamais adapté au clonage.
  Seul le type `monitor` est reconnu. Toute personne copiant
  `morfmonitor.example.json` obtenait donc un service qui démarre normalement,
  annonce sa présence sur le LAN, mais dont **chaque route `/api/` répond 503
  « aucun module de supervision actif »**. Le défaut se manifestait à
  l'exécution seulement, et le service paraissait sain vu de l'extérieur.

### Modifié

- **Le plan d'adressage du parc quitte ce projet.** Le champ `_comment_port` de
  `config/morfmonitor.example.json` était le seul endroit de l'écosystème où le
  plan des ports était écrit — alors que morfMonitor n'a aucune autorité sur les
  autres composants. Cette copie partielle d'un fait valable pour tout le parc
  était déjà incomplète : elle omettait 8789 (morfNotify) et 8787 (défaut du
  serveur de statut morfBeacon). Un développeur la consultant pour choisir un
  port libre obtenait une information fausse sans aucun moyen de le savoir.

  Le registre vit désormais dans `ports.allocations` de
  `morfTools/ecosystem.json`, seul artefact ayant autorité sur l'ensemble, et
  `morf doctor` vérifie que la valeur déclarée ici lui correspond. Le port
  d'écoute de morfMonitor (8790) est inchangé ; seul le commentaire l'est.

## [0.1.1] – 2026-07-20

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

## [0.1.0] – 2026-07-20

### Corrigé

- **Collision de port avec morfAnalytics.** Le clone avait hérité du port
  **8799** du modèle, déjà attribué à morfAnalytics. Une fois morfMonitor
  installé en service, il prenait le port au démarrage et morfAnalytics ne
  pouvait plus écouter : il sortait en erreur et systemd le relançait en
  boucle — **249 redémarrages** constatés. morfMonitor écoute désormais sur
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
  `/etc/morfsystem/morfsystem.json` existe — ordre de démarrage, installation en
  cours — restait aveugle jusqu'à son prochain redémarrage : il répondait
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
  du Raspberry Pi (sous-tension, limite thermique) — le diagnostic le plus utile
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
  chaque seconde provoqueraient dix lectures système par seconde — l'inverse du
  but recherché, qui est de soulager la machine en centralisant.

### Choix de conception

- **Le service démarre même sans configuration.** Un superviseur qui refuse de
  démarrer parce que son fichier manque est inutile au moment précis où on en a
  besoin. Il sert alors ce qu'il peut et signale le problème dans `/status`.
- **Aucun collecteur n'échoue bruyamment.** Une donnée indisponible (capteur
  absent, commande manquante) est omise ; le reste continue d'être servi.
- **La mesure CPU est amorcée au démarrage.** `/proc/stat` ne donne que des
  compteurs cumulés : sans une première lecture d'amorçage, la toute première
  requête renverrait un CPU absent, que les clients afficheraient comme 0 % —
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
