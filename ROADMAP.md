# Roadmap — morfMonitor

morfMonitor supervise la machine hôte et expose son état en JSON. Il reste un
service léger, qui doit tourner en continu sur un Raspberry Pi sans peser sur
les ressources qu'il mesure.

Depuis la 0.2.0, il sert aussi une interface Web. Cela ne change pas sa
responsabilité : la page est une **seconde vue** des mêmes données, servie comme
des fichiers inertes et cliente des routes `/api/` publiques, au même titre que
morfDashboard. morfMonitor collecte et expose ; il ne présente pas.

## Une interface qui privilégie l'information utile

Depuis les premières versions, l'objectif de morfMonitor n'est pas de
reproduire un moniteur système classique ni d'afficher toutes les informations
disponibles. L'objectif est de présenter uniquement celles qui permettent de
comprendre rapidement l'état d'une machine ou de l'écosystème.

Quelques choix de conception illustrent cette philosophie :

- seules les interfaces réseau réellement actives sont affichées (une
  interface Ethernet débranchée n'apporte aucune information utile) ;
- les services sont regroupés par mécanisme (services systemd, découverte
  morfBeacon, sondes réseau) afin de mieux représenter leur rôle ;
- les anomalies sont isolées dans des vues dédiées plutôt que dispersées dans
  l'interface ;
- les informations secondaires sont volontairement masquées pour conserver une
  lecture immédiate.

Le principe est simple :

> **Afficher l'information utile plutôt que toute l'information disponible.**

## Invariant : observatoire, pas portail

morfMonitor peut **découvrir, référencer et présenter** les services de
l'écosystème. Il ne doit **jamais médiatiser leur accès** : pas de proxy HTTP,
pas de relais de requêtes, pas de session, pas de courtier d'authentification,
pas d'agrégation de trafic. Les liens de l'interface Web restent de simples
`href` vers l'adresse propre du service.

Le test qui tranche : **si morfMonitor disparaît, les services continuent de
fonctionner à l'identique — seule la facilité de navigation disparaît.** Cette
propriété doit rester vraie, et elle se vérifie en coupant le service.

Le mot compte. Un portail concentre progressivement sessions, authentification,
proxys et tableaux de bord agrégés, et finit en point de passage obligatoire. Un
observatoire observe et propose un lien. La dérive ne viendra pas d'une décision
de principe mais d'une demande ponctuelle et raisonnable : c'est à ce
moment-là qu'il faut relire ce paragraphe.

Référence complète, valable pour tout le parc :
`docs/ECOSYSTEM-PRINCIPLES.md`, dans le dépôt **morfTools**.

## Évolutions envisagées

### Identifier l'origine des informations

Certaines données proviennent de sources différentes. À terme, il pourrait
être intéressant de rendre cette origine visible afin de faciliter le
développement et le diagnostic.

| Élément | Source |
|---|---|
| Utilisation CPU | live (`/proc`) |
| Mémoire | live (`/proc`) |
| Services | systemd |
| Écosystème | morfBeacon |
| Configuration | `morfsystem.json` |

Cette indication resterait discrète mais permettrait de comprendre
immédiatement d'où provient une information.

### Évolution de la supervision du stockage

L'un des premiers effets inattendus de morfMonitor a été la découverte d'un
disque système presque saturé. L'analyse a montré que l'espace était consommé
par les instantanés Timeshift. Cette expérience montre qu'un simple indicateur
d'occupation est déjà très utile — mais qu'il pourrait évoluer.

Des seuils lisibles (la page Diagnostic applique aujourd'hui 75 % / 90 % ;
la cible envisagée) :

- occupation inférieure à 80 % : état normal ;
- entre 80 % et 90 % : surveillance conseillée ;
- au-delà de 90 % : anomalie.

Une évolution plus intéressante encore serait de suivre la **tendance** :

```
/            91 %    (+8 % sur 7 jours)
/home        43 %    (stable)
```

Le but n'est plus seulement de constater qu'un disque est plein, mais
d'**anticiper** qu'il le deviendra. La frontière avec morfAnalytics reste
nette : une historisation **courte**, au service d'une tendance affichée, est
de la supervision ; le stockage long terme et l'analyse restent le rôle de
morfAnalytics.

### Distinguer le type de machine

La page Écosystème affiche naturellement plusieurs instances d'un même
service : morfMonitor sur Raspberry Pi, sur PC Linux, sur MacBook ; GatewayLab
et MeteoHub sur ESP32. À terme, une colonne ou un badge pourrait préciser la
nature de chaque équipement :

| Application | Machine | Type |
|---|---|---|
| GatewayLab | esp32… | ESP32 |
| MeteoHub | esp32… | ESP32 |
| morfAnalytics | pi4fred | Service |
| morfMonitor | pi4fred | Superviseur |
| morfNotify | MacBook | Desktop |

Cette information permettrait d'identifier immédiatement le rôle d'un
équipement sans connaître l'ensemble de l'écosystème.

### Vers un point d'entrée de l'administration

Aujourd'hui, morfMonitor est principalement un outil de supervision. À terme,
il pourrait devenir le **point d'entrée en lecture** de l'administration de
morfSystem. Sans changer sa philosophie actuelle, il pourrait progressivement
présenter :

- l'état des sauvegardes ;
- la disponibilité des mises à jour ;
- la compatibilité des versions des services entre elles ;
- la santé des SSD (SMART) ;
- les alertes remontées par les différents services ;
- l'état des certificats HTTPS ;
- les tendances de consommation des ressources ;
- la consultation centralisée des journaux.

L'objectif n'est pas de transformer morfMonitor en usine à gaz, mais de
proposer un point d'entrée unique permettant de comprendre rapidement l'état
de l'ensemble de l'écosystème.

Trois de ces pistes touchent des arbitrages notés plus bas, et s'y plient :

- **journaux** : les servir élargit le profil d'exposition bien au-delà de
  métriques ; cette piste est suspendue à une authentification du serveur HTTP
  et à l'arbitrage R5 (modèle de confiance du parc) ;
- **alertes** : morfNotify reste l'émetteur ; morfMonitor en présenterait
  l'état, il ne déciderait ni n'enverrait rien ;
- **administrer n'est pas commander** : le point d'entrée est une **vue**.
  Déclencher — redémarrer un service, lancer une mise à jour — reste le rôle
  de morfTools, et l'exécution reste distribuée et autonome.

## Vision

Au fil de son développement, morfMonitor évolue progressivement d'un simple
tableau de supervision vers une véritable **console d'état** de morfSystem.

La supervision consiste à afficher des métriques. L'administration consiste à
fournir une vue cohérente permettant de comprendre une machine, ses services
et l'ensemble de l'écosystème.

Cette évolution se fait sans perdre le principe fondateur du projet :

> **Une interface sobre, qui privilégie les informations réellement utiles
> plutôt que l'accumulation de données techniques.**

## Pistes techniques

- **Choisir l'adresse d'un émetteur multi-domicilié.** Un service diffusant sur
  plusieurs interfaces (WSL, Hyper-V, VPN) fait retenir à morfMonitor l'adresse
  du dernier datagramme reçu, qui peut appartenir à un réseau virtuel et donc
  être injoignable depuis le navigateur d'une autre machine. Le cas ne se
  produit pas sur la cible de production, où une seule interface est active, et
  n'affecte que le lien — jamais la supervision. Préférer l'interface portant la
  route du réseau local reste à faire.
- **Tests de contrat** du serveur HTTP et des collecteurs. Prioritaire : la
  version 0.2.0 a corrigé huit défauts d'affichage qui partageaient une seule
  cause — une interface écrite contre un schéma supposé. Un test comparant les
  clés que produit `MonitorModule` à celles que lit `app.js` les aurait tous
  attrapés au premier commit.
- **CI** (`.github/workflows`) : build multi-plateforme.
- **Option d'authentification** du serveur HTTP (jeton). Préalable à toute
  consultation de journaux ; à arbitrer avec R5 — le modèle de confiance
  actuel est le réseau local, ce qui reste cohérent tant qu'aucun journal
  n'est exposé.
- **Rechargement de configuration** (SIGHUP) sans redémarrage du service.
- **Historisation courte** des métriques, socle des tendances ci-dessus.

## Non-objectifs

- **Pas de stockage long terme ni d'analyse** : c'est le rôle de morfAnalytics.
  Une tendance courte affichée par morfMonitor s'appuie sur une fenêtre
  glissante, jamais sur une base historique.
- **Pas d'émission d'alerte ni de notification** : c'est le rôle de morfNotify.
  Afficher l'état des alertes n'est pas les émettre.
- **Pas de commande** : redémarrer, mettre à jour, installer relèvent de
  morfTools. morfMonitor montre, il ne déclenche pas.
- **Pas de vue synthétique embarquée** : c'est le rôle de morfDashboard.
  L'écran OLED répond à « est-ce que tout va bien ? », l'interface Web répond à
  « pourquoi ? ». Les deux lisent la même API.
- **Pas de logique de présentation côté serveur.** L'interface Web reste des
  fichiers inertes consommant `/api/`. Le jour où elle lirait `MonitorModule`
  directement, elle cesserait d'être une seconde vue — et deviendrait
  inextricable du service.
- **Pas de médiation d'accès** : ni proxy, ni relais, ni session, ni
  authentification pour le compte d'un autre service (voir l'invariant
  ci-dessus). L'accès distant relèvera d'un composant dédié, qui n'imposera
  aucune modification aux services existants — ceux-ci continueront d'ignorer
  qu'ils sont consultés depuis l'extérieur.
- **Pas de journaux sans authentification.** La page Diagnostic dérive ses
  anomalies des données déjà exposées ; servir la sortie de journald attendra
  l'authentification et l'arbitrage R5.
- **Pas de dépendance externe au-delà de Qt** (morfBeacon reste vendoré ; les
  assets Web passent par Qt Core, sans Qt Widgets).
