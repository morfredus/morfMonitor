# Roadmap — morfMonitor

morfMonitor supervise la machine hôte et expose son état en JSON. Il reste un
service léger, qui doit tourner en continu sur un Raspberry Pi sans peser sur
les ressources qu'il mesure.

Depuis la 0.2.0, il sert aussi une interface Web. Cela ne change pas sa
responsabilité : la page est une **seconde vue** des mêmes données, servie comme
des fichiers inertes et cliente des routes `/api/` publiques, au même titre que
morfDashboard. morfMonitor collecte et expose ; il ne présente pas.

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

## Pistes

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
- **Option d'authentification** du serveur HTTP (jeton). À arbitrer avec la
  page Diagnostic : le modèle de confiance actuel est le réseau local, ce qui
  reste cohérent tant qu'aucun journal n'est exposé.
- **Rechargement de configuration** (SIGHUP) sans redémarrage du service.
- **Historisation courte** des métriques, pour exposer une tendance et pas
  seulement un instantané.

## Non-objectifs

- **Pas de stockage long terme ni d'analyse** : c'est le rôle de morfAnalytics.
- **Pas d'alerte ni de notification** : c'est le rôle de morfNotify.
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
- **Pas de visualiseur de journaux.** La page Diagnostic dérive ses anomalies
  des données déjà exposées. Servir la sortie de journald élargirait le profil
  d'exposition bien au-delà de métriques.
- **Pas de dépendance externe au-delà de Qt** (morfBeacon reste vendoré ; les
  assets Web passent par Qt Core, sans Qt Widgets).
