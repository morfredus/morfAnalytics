# morfAnalytics

*Lire dans une autre langue : [English](README.md) · **Français** (ce document).*

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![Build](https://img.shields.io/badge/CMake-3.21+-064F8C?logo=cmake)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**morfAnalytics — le moteur d'analyse de l'écosystème morfSystem.** Il décharge les
équipements embarqués des traitements lourds (statistiques longues périodes,
corrélations entre capteurs, détection d'anomalies, tendances saisonnières,
comparaisons entre équipements, rapports…).

**morfAnalytics ne possède jamais la vérité des données.** Il travaille uniquement sur
une **copie synchronisée en lecture seule** (cache de travail) alimentée via
**morfSync**. La source de vérité reste sur les équipements (ex. **MeteoHub**) :
l'équipement écrit, morfAnalytics lit — jamais l'inverse. Sa présence est toujours
**optionnelle** : sans serveur, les équipements continuent de mesurer, stocker,
tracer et exporter comme avant ; seules les analyses avancées deviennent
indisponibles.

Il est amorcé à partir de **morfTemplateService** (API HTTP, config JSON, annonce LAN
via morfBeacon, service systemd/Windows). Seule la logique métier d'analyse est
développée ici. Voir la vision d'ensemble dans `../MORFSYSTEM_ARCHITECTURE.md`.

> **État : amorçage.** Le service, son annonce LAN (`morfBeacon`, app `morfAnalytics`)
> et le squelette de module sont en place ; les algorithmes réels (dans
> `AnalyticsModule::analyze()` et la synchro du cache via morfSync) sont des `TODO`.

## Ce qu'on a d'emblée

- **Point d'extension `IModule`** — branchez votre métier en un ou plusieurs
  modules (capteur, notifieur, collecteur…). Un `ExampleModule` fonctionnel sert
  de point de départ.
- **API HTTP** (GET + POST) — `GET /status` (compatible morfBeacon), `/healthz`,
  `/modules`, `/modules/{id}`, et `POST /example` qui montre la lecture d'un corps.
- **Config** — fichier JSON avec une liste `modules` ; une fabrique les instancie.
- **Annonce LAN** — heartbeat morfBeacon (embarqué, aucune dépendance externe).
- **Installation service** — `scripts/linux/` (systemd) et `scripts/windows/`
  (Planificateur de tâches), copie binaire + config dans un dossier fixe.
- **Aide au clonage** — `scripts/new-service.sh` / `.ps1` amorce un projet renommé.

## Amorcer un nouveau service

```sh
scripts/new-service.sh morfwatch morfWatch     # Linux/macOS
# scripts\new-service.ps1 morfwatch morfWatch  # Windows
```

Crée `../morfWatch_travail` avec tous les noms remplacés (`morfAnalytics` →
`morfWatch`, `morfanalytics` → `morfwatch`, `MORFANALYTICS` → `MORFWATCH`). Il
compile tel quel. Ensuite :

1. Codez votre logique dans `src/ExampleModule.*` (renommez-le) — implémentez
   `IModule`.
2. Enregistrez vos types dans `src/ModuleFactory.cpp` + `knownTypes()`.
3. Adaptez les routes HTTP (`src/HttpServer.cpp`) et la liste de sources CMake.
4. Mettez à jour commentaires / config / docs.

## Compiler

Nécessite seulement **Qt 6** (Core, Network). morfBeacon est vendoré dans
`third_party/morf/beacon`.

```sh
cmake --preset mingw        # ou linux / linux-arm64
cmake --build --preset mingw
```

## Lancer

```sh
./build-mingw/service/morfanalytics.exe          # module 'example' de repli
curl http://127.0.0.1:8799/modules
```

## Installer en service

```sh
# Linux (systemd)
sudo ./scripts/linux/install-service.sh
# Windows (Planificateur de tâches, PowerShell Administrateur)
powershell -ExecutionPolicy Bypass -File scripts\windows\install-service.ps1
```

## Documentation

- [Architecture](docs/fr/ARCHITECTURE.md) — les classes et le fil d'exécution.
- [Guide « créer votre service »](docs/fr/INTEGRATION.md) — pas à pas.

## Licence

GPL-3.0-only — © 2026 morfredus (Frédéric Biron).
