# morfMonitor

*Read in another language: **English** (this document) · [Français](README.fr.md).*

[![Version](https://img.shields.io/badge/version-0.5.6-blue)](CHANGELOG.md)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**morfMonitor — the single source of truth about a machine's state.**

It collects system information, keeps it fresh, and exposes it as JSON. That is
all. **It displays nothing.**

The goal: no application in the morfSystem ecosystem should query the operating
system itself. They all read morfMonitor instead.

## HTTP API

| Route | Contents |
|---|---|
| `GET /api/system` | hostname, OS, kernel, architecture, model, uptime, boot time |
| `GET /api/resources` | CPU (%, frequency), load, memory, swap, disk, temperatures, throttling |
| `GET /api/network` | interfaces, IPv4, IPv6, MAC address, state |
| `GET /api/services` | systemd services, network probes, morfBeacon-discovered apps |
| `GET /api/reboot` | cause of the last reboot, with a confidence level |
| `GET /api/config` | effective configuration (what is supervised) |
| `GET /api/all` | everything, in a single request |

Plus the framework routes: `GET /status` (morfBeacon-compatible), `/healthz`,
`/modules`.

The API is deliberately independent of any interface: it serves a TFT screen, a
browser, a Qt application and an ESP32 equally well.

## Web interface

morfMonitor serves a web interface at `/`, on the **same port as the API**.

It is not a second service, and not a second collection: it is a **second view
of the same data**. The page is served as inert assets — no server-side
templating, no injected values — and fetches `/api/all` and `/status` exactly
like any other client. morfDashboard and the browser read the same routes.

| Page | Question it answers |
|---|---|
| État général | Machine identity, uptime, service health, anomaly summary |
| Ressources | CPU, memory, load, swap, storage, processes |
| Réseau | Interfaces, IPv4/IPv6, MAC, link state |
| Services morfSystem | systemd units and network probes being supervised |
| Écosystème | Services discovered over morfBeacon, with version, last heartbeat and a link to any web interface they declare |
| Diagnostic | Detected anomalies, last reboot cause, shared configuration state |

The two interfaces answer different questions, which is why both exist:

> The OLED screen answers **"is everything alright?"**. The web interface
> answers **"why?"**, and gives access to everything needed to diagnose it.

Set `"web_enabled": false` to serve the JSON routes only.

**Exposure.** The interface inherits `bind_address`, which defaults to
`0.0.0.0` — every network interface on the machine. On a multi-homed or exposed
host, set the LAN address instead. There is no authentication: the trust model
is the local network, consistent with the ecosystem's LAN-only design.

The diagnostic page deliberately exposes **no raw log viewer**. It reports
anomalies derived from data the API already returns. Surfacing journald output
would broaden the exposure profile well beyond metrics — log lines can quote
paths, configuration values and, in error messages, credentials handled by other
services. That remains a separate decision.

## The system does not do what I expect

Nearly every surprise comes from the **same misunderstanding**: the service never
reads the repository files. It reads the **deployed** ones.

```
    config/morfmonitor.json   ──deploy──>   /etc/morfmonitor/morfmonitor.json   ← read
    config/morfsystem.json    ──deploy──>   /etc/morfsystem/morfsystem.json     ← read
```

Editing a repository file changes **nothing** until `deploy-config.sh` has run.
That is the most common cause of "but I already fixed that".

| What I see | Why | What to do |
|---|---|---|
| I edited a file in `config/` and nothing changed | The service reads `/opt` and `/etc` | `./scripts/linux/deploy-config.sh` |
| Every `/api/` route answers **503** | No module of type `monitor` is declared | `./scripts/linux/config-tool.sh check` |
| The services / probes / apps lists are **empty** | `morfsystem.json` is not deployed | `./scripts/linux/deploy-config.sh --shared` |
| I added an entry to `systemd_services` or `beacon_apps` and it does not show | `update` adds **keys**, never **list entries** | `./scripts/linux/deploy-config.sh` (it overwrites) |
| I edited the `.example.json` but the other one is deployed | The **real** file wins | Edit `config/morfsystem.json` |
| An application is permanently **flagged** | `enabled: true` on an app that runs occasionally | Set it to `false` |
| An application shows **"désactivé"** | `enabled: false` | Set it to `true` if its absence should alert |
| A device never appears under **Écosystème** | It emits no heartbeat, or UDP broadcast is filtered | `python3 tools/check-protocol.py` from morfBeacon |
| A **stopped** service raised no alert | It was not **declared**: nobody promised it would run | Add it to `beacon_apps` with `enabled: true` |

### The two rules to remember

**Declaring means expecting.** An application in `beacon_apps` with
`enabled: true` is *expected*: its absence becomes an anomaly, in red, on the
screen and in the diagnosis. An undeclared application that stops triggers
nothing — nobody said it should be running. Reserve `true` for what runs
continuously.

**The real file wins over the example.** `install`, `update` and `deploy` use
`config/morfsystem.json` when it exists, `config/morfsystem.example.json`
otherwise. The examples hold a complete, working configuration: if it suits you,
do not create a real file. If you do create one, **it** is what gets deployed and
the example stops being consulted.

## Shared configuration

morfMonitor and morfDashboard read **the same file**,
`/etc/morfsystem/morfsystem.json`. Adding a supervised component requires
editing that file only — no code change in either program. JSON was chosen
precisely so that neither C++ nor Python is privileged.

It replaces the `SERVICE_LABELS`, `NETWORK_SERVICES` and `BEACON_APPS`
structures previously hard-coded in the Dashboard.

## Deploying the configurations

There are **two files**, and they do not go to the same place:

| Repository file | Destination | Contents | Read by |
|---|---|---|---|
| `config/morfmonitor.json` | `/etc/morfmonitor/` | port, bind address, modules | morfMonitor |
| `config/morfsystem.json` | `/etc/morfsystem/` | what is **supervised** | morfMonitor **and** morfDashboard |

One command pushes both:

```bash
./scripts/linux/deploy-config.sh
```

That is all. It backs up each existing file, shows the differences it applies,
copies, then restarts `morfmonitor` and `morfdashboard`.

**Do not prefix with `sudo`**: the script elevates only the system writes.

To push just one:

```bash
./scripts/linux/deploy-config.sh --service      # /opt only
./scripts/linux/deploy-config.sh --shared       # /etc only
./scripts/linux/deploy-config.sh --no-restart   # without restarting
```

The source is your real file (`config/morfsystem.json`) when it exists, and the
example otherwise — so keeping a real file in the clone makes it the reference
that gets deployed.

### The other tools, and when they help

`deploy-config.sh` **overwrites**, which is not always what you want:

| Need | Tool |
|---|---|
| Push my files as they are | `deploy-config.sh` ← the common case |
| Add new keys **without** touching my settings | `service.py update` |
| Find out why the service collects nothing | `config-tool.sh check` |
| Compare deployed against repository | `config-tool.sh diff` |

`check` asks the binary itself which module types are valid (`--list-types`), so
the diagnosis stays correct as the factory evolves, and `service.py update` runs
it after every update.

**`install` and `update` follow the same source rule** as `deploy`: your real
file when it exists, the example otherwise. All three now handle **both**
configurations — `install` used to place only the service one, so a fresh
install started up supervising nothing.

`install` never replaces an existing file: it only puts down what is missing.

**One limit worth knowing**: `update` adds new **keys**, never new **list
entries**. A service added to `systemd_services`, or an application added to
`beacon_apps`, will not arrive through `update` — that would switch on
monitoring you never asked for. Use `deploy-config.sh`, which overwrites.

From morfTools, `python3 ./morfTools/config.py deploy morfMonitor` calls the very same
script — useful to drive several projects from one place, pointless if you are
already inside morfMonitor.

### Linux and Windows parity

Service installation no longer needs a counterpart: `./service.py` is one
implementation for Linux, Windows and the Raspberry Pi, and only the service
manager it drives differs. The remaining `scripts/linux/` and `scripts/windows/`
pairs (`deploy-config`, `config-tool`) still work and are unchanged.

The **JSON logic stays in Python** (`merge-config.py`, `check-config.py`), called
unchanged by both sides. Python is the only one of the three languages in this
ecosystem that runs identically on Windows, Linux and the Raspberry Pi;
reimplementing a recursive JSON merge in both Bash and PowerShell would create
two implementations free to disagree — about the file that decides whether the
service works at all.

## Reboot cause

Not all reboots are alike: a power cut does not call for the same reaction as an
update. morfMonitor cross-references several clues to tell them apart. The
determination is **fallible by nature** — no single reliable source exists — so
every answer carries a `confidence` level and the `evidence` used. When nothing
settles it, the answer is `unknown` rather than a default "normal boot", which
would hide a power cut.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/service/morfmonitor --config config/morfmonitor.example.json
curl http://127.0.0.1:8790/api/all
```

## Documentation

| Document | What it covers |
| --- | --- |
| [docs/en/README.md](docs/en/README.md) | English documentation index. |
| [docs/fr/README.md](docs/fr/README.md) | Index de la documentation (français). |
| [docs/fr/ARCHITECTURE.md](docs/fr/ARCHITECTURE.md) *(FR)* | Classes, module `monitor`, HTTP routes and threading. |
| [README.fr.md](README.fr.md) *(FR)* | Reboot-cause detection, Dashboard fallback mode, design rationale. |

The in-depth documentation is currently **French only**; translations are
welcome (see [CONTRIBUTING.md](CONTRIBUTING.md)).

## License

GPL-3.0-only — © 2026 morfredus (Frédéric Biron).
