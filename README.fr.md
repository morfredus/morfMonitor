# morfMonitor

*Lire dans une autre langue : [English](README.md) · **Français** (ce document).*

[![Version](https://img.shields.io/badge/version-0.3.1-blue)](CHANGELOG.md)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![Build](https://img.shields.io/badge/CMake-3.21+-064F8C?logo=cmake)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**morfMonitor — la source unique de vérité sur l'état d'une machine.**

Il collecte les informations système, les tient à jour, et les expose en JSON.
C'est tout. **Il n'affiche rien.**

L'objectif : plus aucune application de l'écosystème morfSystem ne doit
interroger le système d'exploitation elle-même. Toutes lisent morfMonitor.

## Pourquoi

RaspberryDashboard collectait lui-même ses informations : `psutil`, `systemctl`,
`/proc`, commandes shell, sondes réseau, écoute morfBeacon. Cette logique était
soudée à son interface graphique.

Conséquence : une deuxième interface (dashboard web, application Qt, ESP32…)
aurait dû tout réimplémenter, avec la certitude que les deux finiraient par
diverger. En déplaçant la collecte dans un service, plusieurs interfaces
affichent les mêmes données sans dupliquer une ligne.

## Ce qu'il expose

| Route | Contenu |
|---|---|
| `GET /api/system` | nom d'hôte, OS, noyau, architecture, modèle, uptime, heure de démarrage |
| `GET /api/resources` | CPU (%, fréquence), charge, mémoire, swap, disque, températures, bridage |
| `GET /api/network` | interfaces, IPv4, IPv6, adresse MAC, état |
| `GET /api/services` | services systemd, sondes réseau, applications découvertes par morfBeacon |
| `GET /api/reboot` | cause du dernier redémarrage, avec son degré de confiance |
| `GET /api/config` | configuration effective (ce qui est supervisé) |
| `GET /api/all` | tout, en une seule requête |

S'y ajoutent les routes du socle : `GET /status` (compatible morfBeacon),
`/healthz`, `/modules`.

L'API est volontairement indépendante de toute interface : elle sert aussi bien
un écran TFT, un navigateur, une application Qt qu'un ESP32.

### Un exemple

```sh
curl http://localhost:8790/api/resources
```

```json
{
  "cpu_percent": 3.2,
  "cpu_freq_mhz": 1800,
  "load": [2.87, 2.29, 1.11],
  "memory": { "total_b": 948654080, "available_b": 684109824, "percent": 27.9 },
  "disk": { "mount": "/", "percent": 13.1, "free_b": 91773427712 },
  "temperature": { "cpu_c": 36, "gpu_c": 36 },
  "throttling": { "raw": 0, "undervoltage_now": false, "throttled_now": false }
}
```

`throttling` mérite un mot : ce sont les bits de bridage du Raspberry Pi
(sous-tension, limite thermique). C'est le diagnostic le plus utile d'un Pi
instable, et il n'apparaît nulle part ailleurs.

## Interface Web

morfMonitor sert une interface Web à la racine, **sur le même port que l'API**.

Ce n'est pas un second service, ni une seconde collecte : c'est une **seconde
vue des mêmes données**. La page est servie comme des fichiers inertes — aucun
gabarit, aucune valeur injectée côté serveur — et interroge `/api/all`,
`/status` et `/api/config` exactement comme n'importe quel autre client.
RaspberryDashboard et le navigateur lisent les mêmes routes.

| Page | Question à laquelle elle répond |
|---|---|
| État général | Identité de la machine, uptime, santé du service, résumé des anomalies |
| Ressources | CPU, mémoire, charge, swap, stockage, températures, **bridage** |
| Réseau | Interfaces, IPv4/IPv6, MAC, état du lien |
| Services morfSystem | Unités systemd et sondes réseau supervisées |
| Écosystème | Services découverts par morfBeacon, version, dernier heartbeat et lien vers l'interface Web qu'ils déclarent |
| Diagnostic | Anomalies détectées, cause du dernier redémarrage, configuration partagée |

Les deux interfaces répondent à des questions différentes, et c'est pourquoi
les deux existent :

> L'écran OLED répond à **« est-ce que tout va bien ? »**. L'interface Web
> répond à **« pourquoi ? »**, et donne accès à tout ce qu'il faut pour
> diagnostiquer.

Mettre `"web_enabled": false` ne sert plus que les routes JSON.

**Ce que l'interface ne fait pas.** Elle n'écrase jamais un état par une
approximation : une unité désactivée n'est pas affichée « arrêtée », une sonde
en attente pendant le délai de grâce n'est pas affichée « injoignable », et une
métrique absente de la plateforme est nommée comme telle plutôt que montrée
à zéro. Une case vide se lit comme une mesure ; une absence de mesure doit se
dire.

**Exposition.** L'interface hérite de `bind_address`, à `0.0.0.0` par défaut,
donc toutes les interfaces réseau de la machine. Sur un poste multi-domicilié
ou exposé, indiquer l'adresse du LAN. Il n'y a aucune authentification : le
modèle de confiance est le réseau local, conformément au principe de
l'écosystème.

La page Diagnostic n'expose **délibérément aucun visualiseur de journaux**. Elle
dérive ses anomalies des données que l'API renvoie déjà. Exposer la sortie de
journald élargirait le profil d'exposition bien au-delà de métriques : une ligne
de log peut citer des chemins, des valeurs de configuration et, dans un message
d'erreur, des identifiants manipulés par d'autres services. C'est une décision
séparée.

## Configuration partagée

morfMonitor et RaspberryDashboard lisent **le même fichier** :
`/etc/morfsystem/morfsystem.json` (voir `config/morfsystem.example.json`).

C'est la source unique de vérité des composants supervisés. Ajouter un service,
une sonde ou une application ne demande **que** d'éditer ce fichier — aucune
modification de code, dans aucun des deux programmes.

Le format est du JSON, et non un fichier Python ou un en-tête C++, précisément
pour qu'aucun des deux langages ne soit privilégié.

Il remplace les listes `SERVICE_LABELS`, `NETWORK_SERVICES` et `BEACON_APPS`
autrefois codées dans le Dashboard.

### Découverte

`GET /api/services` liste aussi les applications **entendues mais non
déclarées**, marquées `"declared": false`. Brancher un nouveau service et le voir
apparaître indique exactement quoi ajouter à la configuration.

## Cause du redémarrage

Tous les redémarrages ne se valent pas : une coupure de courant n'appelle pas la
même réaction qu'une mise à jour. morfMonitor croise plusieurs indices — traces
d'arrêt dans le journal du démarrage précédent, journal de paquets, traces de
panique noyau ou de chien de garde — pour distinguer :

| Cause | Signification |
|---|---|
| `user_requested` | Redémarrage demandé (`reboot`, `systemctl reboot`) |
| `update` | Redémarrage après mise à jour de paquets |
| `power_loss` | Coupure d'alimentation |
| `kernel_panic` | Plantage système |
| `watchdog` | Chien de garde matériel ou logiciel |
| `clean_boot` | Démarrage normal après extinction propre |
| `unknown` | Indéterminable |

**La détermination est faillible par nature** : il n'existe aucune source unique
et fiable. Chaque réponse porte donc un champ `confidence` et un champ
`evidence` décrivant l'indice retenu. Quand rien ne tranche, morfMonitor répond
`unknown` — une réponse honnête, contrairement à un « démarrage normal » affirmé
par défaut, qui masquerait une coupure.

## Mode dégradé du Dashboard

RaspberryDashboard privilégie morfMonitor, mais **ne dépend pas** de lui : un
écran de supervision qui s'éteint parce que le superviseur est arrêté est un
contresens.

Si morfMonitor est arrêté, en cours de démarrage ou injoignable, le Dashboard
reprend automatiquement sa collecte locale. Il revient tout aussi
automatiquement au mode normal dès que le service répond — **aucun redémarrage
n'est nécessaire**, ce qui compte, puisque c'est pendant un incident qu'on
regarde l'écran.

Le champ `source` de `get_system_info()` vaut `morfMonitor` ou `local`, ce qui
permet d'afficher discrètement l'origine des données.

## Compiler

Nécessite **Qt 6** (Core, Network). morfBeacon est vendoré.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Lancer

```sh
./build/service/morfmonitor --config config/morfmonitor.example.json
curl http://127.0.0.1:8790/api/all
```

## Installer en service

```sh
sudo ./scripts/linux/install-service.sh     # installer
sudo ./scripts/linux/update-service.sh      # mettre à jour
```

Chaque script a son équivalent Windows dans `scripts/windows/` (tâche
planifiée) :

| Tâche | Linux | Windows |
|---|---|---|
| Installer | `install-service.sh` | `install-service.ps1` |
| Mettre à jour | `update-service.sh` | `update-service.ps1` |
| Déployer la config du dépôt | `deploy-config.sh` | `deploy-config.ps1` |
| Gérer la config déployée | `config-tool.sh` | `config-tool.ps1` |

La logique JSON reste en Python (`merge-config.py`, `check-config.py`),
appelée telle quelle des deux côtés : c'est le seul des trois langages de
l'écosystème qui tourne à l'identique sous Windows, Linux et Raspberry Pi.
La réécrire en Bash **et** en PowerShell donnerait deux implémentations libres
de diverger, à propos du fichier qui décide si le service fonctionne.

## Gérer la configuration déployée

L'installation et la mise à jour **ne remplacent jamais** `morfmonitor.json` :
il porte des réglages locaux irrécupérables. `update-service.sh` ajoute les
clés apparues depuis l'installation sans toucher aux valeurs existantes.

Cette règle laisse un angle mort : une valeur **déjà présente mais devenue
invalide** n'est jamais corrigée. Un module dont le type a disparu de la
fabrique reste en place, et le service démarre alors, écoute, annonce sa
présence sur le LAN — sans rien superviser, chaque route `/api/` répondant 503.

```sh
./scripts/linux/config-tool.sh status      # où elle est, est-elle exploitable
./scripts/linux/config-tool.sh check       # diagnostic détaillé
./scripts/linux/config-tool.sh diff        # écart avec l'exemple du dépôt
sudo ./scripts/linux/config-tool.sh merge  # ajouter les clés manquantes
sudo ./scripts/linux/config-tool.sh reset  # remplacer (confirmation requise)

sudo ./scripts/linux/deploy-config.sh      # écraser par la config du dépôt
```

`check` demande les types valides **au binaire** (`--list-types`) : le
diagnostic reste juste quand la fabrique évolue. Il constate sans rien modifier,
et `update-service.sh` l'exécute après chaque mise à jour — une configuration
périmée s'annonce au lieu d'être découverte par un service silencieux.

`deploy-config.sh` est la voie directe : il copie
`config/morfmonitor.json` (ou l'exemple à défaut) par-dessus la configuration
déployée, sans fusion et sans Python. Toute écriture est précédée d'une
sauvegarde datée.

## Philosophie

morfMonitor collecte, supervise, centralise, maintient et expose. Il n'affiche
rien. Toutes les interfaces graphiques deviennent de simples consommateurs de
son API.

## Licence

GPL-3.0-only — © 2026 morfredus (Frédéric Biron).
