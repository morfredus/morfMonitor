# morfMonitor

*Read in another language: **English** (this document) · [Français](README.fr.md).*

[![Version](https://img.shields.io/badge/version-0.3.6-blue)](CHANGELOG.md)
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
like any other client. RaspberryDashboard and the browser read the same routes.

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

## Shared configuration

morfMonitor and RaspberryDashboard read **the same file**,
`/etc/morfsystem/morfsystem.json`. Adding a supervised component requires
editing that file only — no code change in either program. JSON was chosen
precisely so that neither C++ nor Python is privileged.

It replaces the `SERVICE_LABELS`, `NETWORK_SERVICES` and `BEACON_APPS`
structures previously hard-coded in the Dashboard.

## Deploying the configurations

There are **two files**, and they do not go to the same place:

| Repository file | Destination | Contents | Read by |
|---|---|---|---|
| `config/morfmonitor.json` | `/opt/morfmonitor/` | port, bind address, modules | morfMonitor |
| `config/morfsystem.json` | `/etc/morfsystem/` | what is **supervised** | morfMonitor **and** RaspberryDashboard |

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
| Add new keys **without** touching my settings | `update-service.sh` |
| Find out why the service collects nothing | `config-tool.sh check` |
| Compare deployed against repository | `config-tool.sh diff` |

`check` asks the binary itself which module types are valid (`--list-types`), so
the diagnosis stays correct as the factory evolves, and `update-service.sh` runs
it after every update.

From morfTools, `./morfTools/config.sh deploy morfMonitor` calls the very same
script — useful to drive several projects from one place, pointless if you are
already inside morfMonitor.

### Linux and Windows parity

Every script in `scripts/linux/` has a counterpart in `scripts/windows/`
(`install-service`, `update-service`, `deploy-config`, `config-tool`).

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
