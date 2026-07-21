# Roadmap — morfMonitor

morfMonitor supervise la machine hôte et expose son état en JSON. Il reste un
service léger, qui doit tourner en continu sur un Raspberry Pi sans peser sur
les ressources qu'il mesure.

Depuis la 0.2.0, il sert aussi une interface Web. Cela ne change pas sa
responsabilité : la page est une **seconde vue** des mêmes données, servie comme
des fichiers inertes et cliente des routes `/api/` publiques, au même titre que
RaspberryDashboard. morfMonitor collecte et expose ; il ne présente pas.

## Pistes

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
- **Pas de vue synthétique embarquée** : c'est le rôle de RaspberryDashboard.
  L'écran OLED répond à « est-ce que tout va bien ? », l'interface Web répond à
  « pourquoi ? ». Les deux lisent la même API.
- **Pas de logique de présentation côté serveur.** L'interface Web reste des
  fichiers inertes consommant `/api/`. Le jour où elle lirait `MonitorModule`
  directement, elle cesserait d'être une seconde vue — et deviendrait
  inextricable du service.
- **Pas de visualiseur de journaux.** La page Diagnostic dérive ses anomalies
  des données déjà exposées. Servir la sortie de journald élargirait le profil
  d'exposition bien au-delà de métriques.
- **Pas de dépendance externe au-delà de Qt** (morfBeacon reste vendoré ; les
  assets Web passent par Qt Core, sans Qt Widgets).
