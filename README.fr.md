# morfMonitor

*Lire dans une autre langue : [English](README.md) · **Français** (ce document).*

[![Version](https://img.shields.io/badge/version-0.5.3-blue)](CHANGELOG.md)
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

morfDashboard collectait lui-même ses informations : `psutil`, `systemctl`,
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
morfDashboard et le navigateur lisent les mêmes routes.

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

## Le système ne fait pas ce que j'attends

Presque toutes les surprises viennent du **même malentendu** : le service ne lit
jamais les fichiers du dépôt. Il lit ceux qui ont été **déployés**.

```
    config/morfmonitor.json   ──déploiement──>   /etc/morfmonitor/morfmonitor.json   ← lu
    config/morfsystem.json    ──déploiement──>   /etc/morfsystem/morfsystem.json     ← lu
```

Modifier un fichier du dépôt ne change **rien** tant que `deploy-config.sh` n'a
pas été lancé. C'est la cause la plus fréquente de « j'ai pourtant corrigé ça ».

| Ce que je constate | Pourquoi | Quoi faire |
|---|---|---|
| J'ai modifié un fichier de `config/` et rien ne change | Le service lit `/opt` et `/etc` | `./scripts/linux/deploy-config.sh` |
| Toutes les routes `/api/` répondent **503** | Aucun module de type `monitor` déclaré | `./scripts/linux/config-tool.sh check` |
| Les listes services / sondes / applications sont **vides** | `morfsystem.json` n'est pas déployé | `./scripts/linux/deploy-config.sh --shared` |
| J'ai ajouté une entrée à `systemd_services` ou `beacon_apps`, elle n'apparaît pas | `update` ajoute les **clés**, jamais les **entrées de liste** | `./scripts/linux/deploy-config.sh` (il écrase) |
| J'ai édité le `.example.json`, c'est l'autre qui part | Le fichier **réel** est prioritaire | Éditer `config/morfsystem.json` |
| Une application est en **anomalie permanente** | `enabled: true` sur une application lancée par intermittence | La passer à `false` |
| Une application affiche **« désactivé »** | `enabled: false` | La passer à `true` si son absence doit alerter |
| Un équipement ne remonte pas dans **Écosystème** | Il n'émet pas de heartbeat, ou la diffusion UDP est filtrée | `python3 tools/check-protocol.py` depuis morfBeacon |
| Un service **arrêté** n'a levé aucune alerte | Il n'était pas **déclaré** : il n'a été promis à personne | L'ajouter à `beacon_apps` avec `enabled: true` |

### Les deux règles à retenir

**Déclarer, c'est s'attendre.** Une application dans `beacon_apps` avec
`enabled: true` est *attendue* : son absence devient une anomalie, en rouge, sur
l'écran et dans le diagnostic. Une application non déclarée qui s'arrête ne
déclenche rien — personne n'avait dit qu'elle devait tourner. Réservez `true` à
ce qui tourne en permanence.

**Le fichier réel gagne sur l'exemple.** `install`, `update` et `deploy`
utilisent `config/morfsystem.json` s'il existe, `config/morfsystem.example.json`
sinon. Les exemples contiennent une configuration complète et fonctionnelle : si
elle vous convient, ne créez pas de fichier réel. Si vous en créez un, c'est
**lui** qui sera déployé, et l'exemple cessera d'être consulté.

## Configuration partagée

morfMonitor et morfDashboard lisent **le même fichier** :
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

morfDashboard privilégie morfMonitor, mais **ne dépend pas** de lui : un
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
# Toutes plateformes : Linux, Windows, Raspberry Pi
sudo ./service.py install      # compile si besoin, installe, demarre
sudo ./service.py update       # recompile, remplace le binaire, redemarre
sudo ./service.py uninstall    # desinscrit, en conservant votre configuration
./service.py status            # ce que le systeme en dit
```

Un seul point d'entree partout. Ce qu'est ce service — son nom, son dossier,
ses configurations — est declare dans `service.json` a cote. Les quatre etapes
d'installation vivent une seule fois pour tout le parc ; seul le gestionnaire
de services change selon la plateforme.

Les anciens scripts `scripts/linux/` et `scripts/windows/` fonctionnent
toujours, inchanges.

Chaque script a son équivalent Windows dans `scripts/windows/` (tâche
planifiée) :

| Tâche | Linux | Windows |
|---|---|---|
| Installer | `service.py install` | `service.py install` |
| Mettre à jour | `service.py update` | `service.py update` |
| Déployer la config du dépôt | `deploy-config.sh` | `deploy-config.ps1` |
| Gérer la config déployée | `config-tool.sh` | `config-tool.ps1` |

La logique JSON reste en Python (`merge-config.py`, `check-config.py`),
appelée telle quelle des deux côtés : c'est le seul des trois langages de
l'écosystème qui tourne à l'identique sous Windows, Linux et Raspberry Pi.
La réécrire en Bash **et** en PowerShell donnerait deux implémentations libres
de diverger, à propos du fichier qui décide si le service fonctionne.

## Déployer les configurations

Il y a **deux fichiers**, et ils ne vont pas au même endroit :

| Fichier du dépôt | Destination | Contenu | Lu par |
|---|---|---|---|
| `config/morfmonitor.json` | `/etc/morfmonitor/` | port, adresse d'écoute, modules | morfMonitor |
| `config/morfsystem.json` | `/etc/morfsystem/` | ce qui est **supervisé** | morfMonitor **et** morfDashboard |

Une seule commande pousse les deux :

```sh
./scripts/linux/deploy-config.sh
```

C'est tout. Elle sauvegarde chaque fichier existant, affiche les différences
appliquées, copie, puis redémarre `morfmonitor` et `morfdashboard`.

**Ne pas préfixer par `sudo`** : le script n'élève que les écritures système.

Pour n'en pousser qu'un :

```sh
./scripts/linux/deploy-config.sh --service      # seulement /opt
./scripts/linux/deploy-config.sh --shared       # seulement /etc
./scripts/linux/deploy-config.sh --no-restart   # sans redémarrer
```

La source est votre fichier réel (`config/morfsystem.json`) s'il existe, sinon
l'exemple (`config/morfsystem.example.json`). Garder un vrai fichier dans le
clone en fait donc la référence déployée.

### Les autres outils, et quand ils servent

`deploy-config.sh` **écrase**. Ce n'est pas toujours ce qu'on veut :

| Besoin | Outil |
|---|---|
| Pousser mes fichiers tels quels | `deploy-config.sh` ← le cas courant |
| Ajouter les clés nouvelles **sans** toucher à mes réglages | `service.py update` |
| Savoir pourquoi le service ne collecte rien | `config-tool.sh check` |
| Comparer déployé et dépôt | `config-tool.sh diff` |

**`install` et `update` suivent la même règle de source** que `deploy` : votre
fichier réel s'il existe, l'exemple sinon. Et tous trois traitent désormais les
**deux** configurations — `install` ne plaçait que celle du service, si bien
qu'une installation neuve démarrait sans rien superviser.

`install` ne remplace jamais un fichier existant : il ne pose que ce qui manque.

**Une limite à connaître** : `update` ajoute les **clés** nouvelles, jamais les
**entrées de liste**. Un service ajouté à `systemd_services` ou une application
ajoutée à `beacon_apps` n'arrivera donc pas par `update` — ce serait activer une
surveillance que vous n'avez pas demandée. Pour les récupérer, utiliser
`deploy-config.sh`, qui écrase.

## Philosophie

morfMonitor collecte, supervise, centralise, maintient et expose. Il n'affiche
rien. Toutes les interfaces graphiques deviennent de simples consommateurs de
son API.

## Licence

GPL-3.0-only — © 2026 morfredus (Frédéric Biron).
