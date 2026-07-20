# Documentation de morfMonitor (français)

Service de supervision de la machine hôte : système, ressources, réseau,
services et cause du dernier redémarrage, exposés en JSON sur une API HTTP.
Il s'annonce sur le LAN via morfBeacon et s'installe en service systemd.

> 🇬🇧 English documentation: [`docs/en/`](../en/README.md) *(index, in progress)*.
> Retour au [README (français)](../../README.fr.md).

## Comprendre et utiliser

| Document | Contenu |
|---|---|
| [Architecture](ARCHITECTURE.md) | Les classes (`IModule`, `MonitorModule`, `ModuleRegistry`, `HttpServer`, `Service`) et le fil d'exécution. |

## À la racine du projet

| Document | Contenu |
|---|---|
| [README](../../README.md) | Présentation générale (anglais). |
| [README (français)](../../README.fr.md) | Présentation générale (français). |
| [Journal des versions](../../CHANGELOG.md) | Historique des versions. |
| [Roadmap](../../ROADMAP.md) | Évolutions envisagées de morfMonitor. |
| [Contribuer](../../CONTRIBUTING.md) | Guide de contribution à morfMonitor. |
