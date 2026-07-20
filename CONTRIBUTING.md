# Contribuer à morfMonitor

morfMonitor supervise la machine hôte et expose son état en JSON. Il est bâti sur
la mécanique commune des services morfSystem (issue de morfTemplateService) :
config, registre de modules, serveur HTTP, annonce LAN.

## 1. Philosophie

- **Le métier vit dans un module.** La supervision est implémentée par
  `MonitorModule` et les collecteurs de `Collectors.h` ; la mécanique commune
  (config, registre, serveur HTTP, annonce, service) reste générique.
- **Fonctionnel du premier coup.** Le dépôt doit compiler et tourner sans
  modification.
- **Qt Core + Network uniquement.** morfBeacon reste vendoré. Portable Windows /
  Linux x64 / Raspberry Pi.
- **Renommable.** Tout nom propre au template doit rester l'un des trois jetons
  gérés par les scripts de clonage : `morfMonitor`, `morfmonitor`,
  `MORFMONITOR`. Ne pas introduire d'autre variante de nom.

## 2. Faire évoluer morfMonitor

Une nouvelle donnée supervisée s'ajoute dans les collecteurs (`Collectors.h`,
`HostCollectors.cpp`) et s'expose via `MonitorModule`. Après modification,
vérifier que le projet compile et que l'API répond :

```sh
cmake --preset mingw && cmake --build --preset mingw
./build-mingw/service/morfmonitor --config config/morfmonitor.example.json
curl http://127.0.0.1:8790/api/all
```

## 3. Style

- C++17, conventions des projets frères : en-tête de licence SPDX, namespace
  `morfmonitor`, commentaires en français expliquant le **pourquoi**, marqueurs
  `>>> A ADAPTER <<<` aux endroits que le développeur doit personnaliser.
- Fins de ligne : voir `.gitattributes` (LF dans le dépôt ; `.ps1` en CRLF).
