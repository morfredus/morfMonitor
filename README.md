# morfMonitor

*Read in another language: **English** (this document) · [Français](README.fr.md).*

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
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

## Shared configuration

morfMonitor and RaspberryDashboard read **the same file**,
`/etc/morfsystem/morfsystem.json`. Adding a supervised component requires
editing that file only — no code change in either program. JSON was chosen
precisely so that neither C++ nor Python is privileged.

It replaces the `SERVICE_LABELS`, `NETWORK_SERVICES` and `BEACON_APPS`
structures previously hard-coded in the Dashboard.

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
