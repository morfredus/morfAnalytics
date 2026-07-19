# morfAnalytics

*Read in another language: **English** (this document) · [Français](README.fr.md).*

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![Build](https://img.shields.io/badge/CMake-3.21+-064F8C?logo=cmake)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**morfAnalytics — the analysis engine of the morfSystem ecosystem.** It offloads
heavy analytics (long-period statistics, sensor correlations, anomaly detection,
seasonal trends, cross-device comparisons, reports…) from the embedded devices.

**morfAnalytics never owns the truth of the data.** It works only on a synchronized,
read-only *working cache* fed through **morfSync**. The source of truth stays on the
devices (e.g. **MeteoHub**): the device writes, morfAnalytics reads — never the
opposite. Its presence is always **optional**: if no server is available, the devices
keep measuring, storing, charting and exporting exactly as before; only the advanced
analyses become unavailable.

It is bootstrapped from **morfTemplateService** (HTTP API, JSON config, LAN announce
via morfBeacon, systemd/Windows service). Only the analytics business logic is
developed here. See the ecosystem vision in `../MORFSYSTEM_ARCHITECTURE.md`.

> **Status: bootstrap.** The service, its LAN announce (`morfBeacon`, app
> `morfAnalytics`) and the module skeleton are in place; the real algorithms (in
> `AnalyticsModule::analyze()` and the morfSync cache sync) are stubs marked `TODO`.

## What you get out of the box

- **`IModule` extension point** — plug your business logic as one or more modules
  (a sensor, a notifier, a collector…). A working `ExampleModule` ships as a
  starting point.
- **HTTP API** (GET + POST) — `GET /status` (morfBeacon-compatible), `GET /healthz`,
  `GET /modules`, `GET /modules/{id}`, and a `POST /example` showing body parsing.
- **Config** — JSON file with a `modules` list; a factory turns it into modules.
- **LAN announce** — morfBeacon heartbeat (bundled, no external dependency).
- **Service install** — `scripts/linux/` (systemd) and `scripts/windows/`
  (Task Scheduler), copying binary + config to a fixed location.
- **Clone helper** — `scripts/new-service.sh` / `.ps1` scaffolds a renamed project.

## Bootstrap a new service

```sh
scripts/new-service.sh morfwatch morfWatch     # Linux/macOS
# scripts\new-service.ps1 morfwatch morfWatch  # Windows
```

Creates `../morfWatch_travail` with every name replaced (`morfAnalytics` →
`morfWatch`, `morfanalytics` → `morfwatch`, `MORFANALYTICS` → `MORFWATCH`). It
compiles as-is. Then:

1. Code your logic in `src/ExampleModule.*` (rename it) — implement `IModule`.
2. Register your type(s) in `src/ModuleFactory.cpp` + `knownTypes()`.
3. Adapt the HTTP routes (`src/HttpServer.cpp`) and CMake source list.
4. Update the comments / config / docs.

## Build

Only needs **Qt 6** (Core, Network). morfBeacon is vendored under
`third_party/morf/beacon`.

```sh
cmake --preset mingw        # or linux / linux-arm64
cmake --build --preset mingw
```

## Run

```sh
./build-mingw/service/morfanalytics.exe          # 'example' module fallback
curl http://127.0.0.1:8799/modules
```

## Install as a service

```sh
# Linux (systemd)
sudo ./scripts/linux/install-service.sh
# Windows (Task Scheduler, PowerShell Administrateur)
powershell -ExecutionPolicy Bypass -File scripts\windows\install-service.ps1
```

## Documentation

French documentation in [`docs/fr/`](docs/fr/README.md): architecture, and a
step-by-step guide to turn this template into your service.

## License

GPL-3.0-only — © 2026 morfredus (Frédéric Biron).
