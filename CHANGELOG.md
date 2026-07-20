# Journal des versions — morfMonitor

Le format s'inspire de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/)
et du [versionnage sémantique](https://semver.org/lang/fr/).

## [Non publié]

### Ajouté

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

### Corrigé

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

### Ajouté

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
