# morfAnalytics

*Read in another language: **English** (this document) · [Français](README.fr.md).*

[![Version](https://img.shields.io/badge/version-0.30.0-blue)](CHANGELOG.md)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![Build](https://img.shields.io/badge/CMake-3.21+-064F8C?logo=cmake)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**morfAnalytics - the analysis engine of the morfSystem ecosystem.** It offloads
heavy analytics (long-period statistics, sensor correlations, anomaly detection,
seasonal trends, cross-device comparisons, reports…) from the embedded devices.

**morfAnalytics never owns the truth of the data.** It works only on a local,
read-only *working cache* copied from the devices. The source of truth stays on the
devices (e.g. **MeteoHub**): the device writes, morfAnalytics reads - never the
opposite, which the collector guarantees by issuing `GET` requests only. The cache
can be deleted and rebuilt in full from the device without any loss. Its presence is
always **optional**: if no server is available, the devices keep measuring, storing,
charting and exporting exactly as before; only the advanced analyses become
unavailable.

See the ecosystem vision in `../morfSystem/docs/ARCHITECTURE.md`.

> **Status: operational.** Incremental collection, thirteen weather analyses, cache cleanup and a
> web page are in place. Still to be written: publishing results to **morfSync**,
> and the correlation and anomaly-detection analyses.

> **Architecture note.** The cache is not fed through morfSync: the device is not
> a morfSync client, as the sync envelope (UUID, revision, origin) would weigh
> more than the measurement itself on an ESP32 writing one every minute. morfSync
> is meant to distribute the **analysis results** to the ecosystem.

## What the service does

- **Incremental collection** - copies the device history, never re-requesting
  what the cache already holds.
- **Pluggable analysis engine** - analyses only ever handle a generic time series
  with named channels. The weather analyses are just one set: another project
  registers its own without touching the engine.
- **Analysis web page** - served by the service itself, with no external asset
  (usable on a LAN with no Internet access).
- **HTTP API** (GET + POST) - `GET /` (web page), `GET /analyses` (catalog),
  `GET /status` (morfBeacon-compatible), `GET /healthz`, `GET /modules`,
  `GET /modules/{id}`, `POST /analyze` (on-demand analysis) and
  `POST /data/cleanup` (local-cache cleanup - never the source).
- **Cache cleanup** - from the page or the API: neutralise failed-sensor
  readings (`0 hPa`, `0 °C`), neutralise a time range, or purge everything.
  Acts on the local copy only; the original measurements on the device are
  never touched, and a purged cache rebuilds itself.
- **Config** - JSON file with a `modules` list; a factory turns it into modules.
- **LAN announce** - morfBeacon heartbeat (bundled, no external dependency).
- **Service install** - `scripts/linux/` (systemd) and `scripts/windows/`
  (Task Scheduler), copying binary + config to a fixed location.

## Configure collection from MeteoHub

Set the device address and the station altitude in the `analytics` module (see
`config/morfanalytics.example.json`):

```jsonc
{
  "type": "analytics",
  "id": "analytics-1",
  "maintenance_ms": 60000,      // delay between two collection cycles
  "cache_dir": "cache",         // working cache directory
  "source_url": "http://192.168.1.42",
  "altitude_m": 8               // station altitude, in metres
}
```

- **`source_url`** - without it, no collection runs: the service only exposes the
  cache it already holds. Use that mode to analyse an already-copied history while
  the device is offline.
- **`altitude_m`** - used to reduce pressure to sea level, the only form
  comparable to weather bulletins. Reckon about **0.12 hPa per metre**: at a few
  metres the difference is negligible, at a few hundred it changes the forecast.
  Enter the actual altitude of the sensor, not the one of the town. A **zero
  altitude is a valid value** (seaside station); it is the *absence* of the
  parameter that pressure analyses flag.

Then open `http://<server-address>:8799/` to follow the collection. The first
cycle copies the whole history stored on the SD card and may take several
minutes; later cycles only transfer new measurements.

The cache is a plain SQLite file in `cache_dir`. It can be deleted at any time: it
gets rebuilt from the device without loss, since MeteoHub remains the source of
truth.

SiteWatch summaries are stored separately in
`/opt/morfanalytics/cache/sitewatch-history.sqlite`. This historical database
contains only calculated summaries: SiteWatch remains the sovereign owner of
the source access logs.

## Photo library (morfPhoto)

The `/photo` space reads the photo library indexed by **morfPhoto** (the source
of truth): camera bodies, lenses, focal lengths (bucketed into usual values) and
years. morfAnalytics only polls morfPhoto's aggregate endpoints at a regular
interval (never the file list) and keeps a snapshot. Configure the source through
the `photo` module of the configuration (`source_url`, e.g.
`http://127.0.0.1:8793`); if morfPhoto is unreachable or the module is missing,
the page says so explicitly.

## Machine history (morfMonitor)

The `/monitor` space historises the machine metrics reported by **morfMonitor**
(CPU, memory, temperature, load, active services) and charts them **over time**: an
overview, CPU / RAM / temperature / load series, and a **who-consumes-what**
per-service breakdown (top CPU and RAM consumers, plus per-service averages and
maxima over the period), with a machine and period selector (1 h - 30 d).
morfMonitor stays the probe ("now"); this domain provides the memory over time.
Samples are stored in `/opt/morfanalytics/cache/monitor.sqlite`, with a
configurable raw-sample retention (`retention_days`, 90 by default; `0` = keep
forever) as a first step before tiered compaction. **Activities** are historised
too: any component that knows what it is doing posts one to
`POST /api/monitor/activity`. **Compilations** are the first case — morfDeploy emits
one per build (set `MORFANALYTICS_ACTIVITY_URL` on the build machine), and the page
shows per-project build stats (count, success rate, total/average/min/max duration)
plus the system cost measured over each build's exact window. Baseline and anomalies
come next. Configure it through the `monitor` module (`sources`, one entry per
morfMonitor to poll, plus `interval_ms` and `retention_days`).

## Available analyses

Open `http://<server-address>:8799/`, or query one analysis directly:

```sh
curl -X POST -H 'Content-Type: application/json'      -d '{"type":"zambretti"}' http://localhost:8799/analyze
```

`GET /analyses` exposes the full catalog.

### Conditions and local forecast

| Analysis | `type` | What it gives |
|---|---|---|
| Current conditions | `current` | Dew point, absolute humidity, humidex, sea-level pressure |
| Pressure tendency | `pressure_trend` | 1 h and 3 h change, WMO code, rapid-fall warning |
| Local forecast | `zambretti` | 12-24 h text forecast derived from pressure |
| Fog risk | `fog_risk` | Dew-point spread and how fast it narrows |
| Frost risk | `frost_risk` | Projected minimum at dawn |

### Climatology

| Analysis | `type` | What it gives |
|---|---|---|
| Daily normal | `normals` | Departure from the rolling normal for the day of year |
| Degree days | `degree_days` | Heating (base 18) and cooling (base 26), per month |
| Diurnal range | `diurnal_amplitude` | Max-min spread, mean and extremes |
| Records | `records` | Dated absolute minima and maxima |
| Notable days | `streaks` | Frost, heat, tropical nights, consecutive runs |
| Daily cycle | `daily_cycle` | Mean temperature per hour, extreme hours |
| Completeness | `data_quality` | Complete days, partial days and collection gaps |

Optional parameters: `days` (window depth), `window_days` (normals half-window),
`heating_base` / `cooling_base` (degree days).

An analysis short of history does not return an HTTP error: it answers
`ok: false` with the reason and the required depth. The service did answer; it is
the result that cannot be computed - better said plainly than published as an
average over three measurements.

### Acknowledged limits

These analyses rest on three quantities only - temperature, humidity, pressure.
With no wind or sunshine data, Zambretti and the fog and frost risks remain
**local indications**, not forecasts. Each result carries the matching note,
displayed as-is on the page.

## Build

Only needs **Qt 6** (Core, Network, Sql). morfBeacon is vendored under
`third_party/morf/beacon`.

```sh
cmake --preset mingw        # or linux / linux-arm64
cmake --build --preset mingw
```

## Run

```sh
./build-mingw/service/morfanalytics.exe --config config/morfanalytics.example.json
curl http://127.0.0.1:8799/analyses
```

Without `--config`, the service looks for a configuration in the current
directory, next to the binary, then in `/etc/morfsystem/morfanalytics/`; failing that it
starts with a default `analytics` module, with no source, hence no collection.

## Install as a service

```sh
# Any platform: Linux, Windows, Raspberry Pi
sudo ./service.py install      # build if needed, install, start
sudo ./service.py update       # rebuild, replace the binary, restart
sudo ./service.py uninstall    # deregister, keeping your configuration
./service.py status            # what the system says about it
```

One entry point everywhere. What this service is - its name, its directory,
its configurations - is declared in `service.json` beside it. The four install
steps live once for the whole parc; only the service manager differs by
platform.

The former `scripts/linux/` and `scripts/windows/` scripts still work,
unchanged.

To **redeploy the configuration** to `/etc/morfsystem/morfanalytics/` after editing
it (a MeteoHub / morfPhoto / morfMonitor source of the `monitor` module...), without
a full rebuild, use the parc's unified command (timestamped backup, then restarts;
Linux and Windows alike):

```sh
sudo ./service.py config push --force     # or, from the parc root: morf config deploy morfAnalytics
```

Unlike `service.py update` (which only adds missing keys, never overwrites), `config
push` replaces the deployed file with the one from the repo - keep a real
`config/morfanalytics.json` in your clone, with your sources, as the deployed
reference. (Use `service.py config` with no mode to only add a new version's keys
while keeping your settings.)

Updating never replaces values already present in the configuration, but **adds
the parameters introduced since installation** and reports them. Without that, a
new feature would stay silently inactive for want of its parameter. Uninstalling
removes the service **and** its installation directory (binary, configuration,
cache); `--keep-config` backs the configuration up on the way out, `--dry-run`
shows what would be removed. The git clone is never touched.

## Documentation

French documentation in [`docs/fr/`](docs/fr/README.md): architecture,
execution flow, and the [design decisions](docs/fr/DECISIONS.md) with their
rationale. See also [CHANGELOG](CHANGELOG.md) · [ROADMAP](ROADMAP.md) ·
[CONTRIBUTING](CONTRIBUTING.md) (including how to add an analysis).

## License

GPL-3.0-only - © 2026 morfredus (Frédéric Biron).
