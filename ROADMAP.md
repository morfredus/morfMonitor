# Roadmap — morfMonitor

morfMonitor supervise la machine hôte et expose son état en JSON. Il reste un
service **sans interface**, léger, qui doit tourner en continu sur un Raspberry
Pi sans peser sur les ressources qu'il mesure.

## Pistes

- **Tests** du serveur HTTP, du registre de modules et des collecteurs.
- **CI** (`.github/workflows`) : build multi-plateforme.
- **`scripts/windows/update-service.ps1`** (pendant Windows de `update-service.sh`).
- **Option d'authentification** du serveur HTTP (jeton).
- **Rechargement de configuration** (SIGHUP) sans redémarrage du service.
- **Historisation courte** des métriques, pour exposer une tendance et pas
  seulement un instantané.

## Non-objectifs

- Pas d'interface graphique ni de stockage long terme : l'affichage est le rôle
  de RaspberryDashboard, l'analyse celui de morfAnalytics.
- Pas d'alerte ni de notification : c'est le rôle de morfNotify.
- Pas de dépendance externe au-delà de Qt (morfBeacon reste vendoré).
