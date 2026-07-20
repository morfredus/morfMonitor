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
│                         plus /status /healthz /modules)
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
