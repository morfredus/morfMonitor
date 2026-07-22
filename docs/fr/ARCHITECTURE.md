# Architecture — morfMonitor

Retour à l'[index de la documentation](README.md).

---

Service Qt (Core + Network), sans interface. Le métier — la supervision de la
machine — vit dans un `IModule`, `MonitorModule`, qui s'appuie sur les
collecteurs de `Collectors.h`.

## Les pièces

```
Service (façade : câble tout à partir d'une ServiceConfig)
├── ModuleRegistry     -> collectionne les IModule, agrège leur état
│     └── IModule (interface, QObject)   ◀── POINT D'EXTENSION
│            └── MonitorModule  (supervision système ; via HostCollectors)
├── HttpServer         -> API HTTP (GET /api/system /api/resources /api/network
│                         /api/services /api/reboot /api/config /api/all,
│                         plus /status /healthz /modules ; GET et HEAD)
│     └── assets Web inertes (:/web via Qt resource) -> /  /styles.css  /app.js
└── morfbeacon::Heartbeat -> annonce UDP (découverte LAN)
        ▲ IMetricsProvider
        └── ModuleRegistry expose un résumé (nombre de modules, ...)
```

### `ServiceConfig` / `ModuleDef`

Chargées depuis un fichier JSON. `ServiceConfig` porte les réglages globaux
(`httpPort`, `bindAddress`, `beacon`) et la liste des modules. Un `ModuleDef`
(`type`, `id`, `params`) décrit un module à instancier. La configuration
partagée `/etc/morfsystem/morfsystem.json` est lue par `SharedConfig`.

### `IModule` (interface, QObject) — le point d'extension

C'est **ici** que vit le métier. Une sous-classe implémente `start()`, `stop()`
et `statusJson()` (état exposé dans `/modules`), et peut émettre `updated()`.
`id()`/`type()` l'identifient. Dans morfMonitor, l'unique implémentation est
`MonitorModule` (type `monitor`, seul type rendu par `knownTypes()`).

### `ModuleFactory`

Fabrique un `IModule` à partir d'un `ModuleDef`. Point d'extension **compile-time** :
une branche par type ; `knownTypes()` les liste.

### `ModuleRegistry` (QObject + `morfbeacon::IMetricsProvider`)

Détient les modules, les démarre/arrête, agrège leur `statusJson()` pour `/modules`
et fournit un résumé à `/status` (via `IMetricsProvider`).

### `HttpServer` (QObject)

Serveur HTTP/1.1 minimal gérant **GET et POST** (lecture du corps via
`Content-Length`). Il expose l'API de supervision (`/api/…`) ainsi que les
routes de service `/status`, `/healthz` et `/modules`.

### `Service` (façade)

L'unique objet manipulé par le démon : construit les modules (via la fabrique),
démarre le serveur HTTP puis le heartbeat morfBeacon.

## Fil d'exécution

Tout tourne sur **le thread principal Qt**. Les modules travaillent de façon
événementielle (timers, sockets) et exposent un instantané via `statusJson()` ;
le serveur HTTP répond sans bloquer. Un module lent doit rester asynchrone et ne
publier qu'un instantané.

## Dépendance morfBeacon (embarquée)

morfBeacon est vendoré dans `third_party/morf/beacon` (lié statiquement) : build
autonome, sans dépôt externe. Resynchroniser avec `scripts/sync-morf.(sh|ps1)`.

## Portabilité

Aucun code spécifique à une plateforme hors des collecteurs. Comportement identique
Windows / Linux x64 / Raspberry Pi (ARM64). Installation en service fournie pour
systemd (Linux) et Planificateur de tâches (Windows).

Les collecteurs sont le seul endroit où les plateformes divergent, et cette
divergence est **visible depuis l'extérieur** : sous Windows, `cpu_percent`,
`memory`, `load`, `temperature` et `throttling` ne sont pas renseignés, car ils
proviennent de `/proc` et `/sys`. Un consommateur doit traiter leur absence
comme une absence de mesure, jamais comme une valeur nulle.

## Interface Web

L'interface Web n'est **pas une pièce d'architecture supplémentaire**. C'est
une seconde vue des mêmes données, servie par le `HttpServer` existant sur le
même port, sous forme de fichiers inertes embarqués dans le binaire (ressource
Qt `:/web`, aucune dépendance Qt Widgets).

La contrainte qui la définit tient en une phrase : **elle est cliente de
l'API publique, pas une initiée.** Elle lit `/api/all`, `/status` et
`/api/config` comme morfDashboard le fait. Aucun gabarit, aucune donnée
injectée côté serveur.

Ce n'est pas cosmétique. morfMonitor annonce « il n'affiche rien » : sa
responsabilité est de collecter et d'exposer, pas de présenter. Tant que la vue
reste cliente de l'API, elle n'est qu'un affichage — extractible vers un projet
séparé sans réécriture si elle devait un jour le devenir. Le jour où elle lirait
`MonitorModule` directement, cette propriété serait perdue en silence, et
« seconde vue » deviendrait faux sans que rien ne le signale.

Les sources vivent dans `web_src/` (convention de l'écosystème : `web_src/`
contient ce qui est écrit, `web/` ce qui est généré — ce dernier est d'ailleurs
exclu par `.gitignore`).
