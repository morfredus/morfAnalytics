# Journal des versions — morfAnalytics

Le format s'inspire de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/)
et du [versionnage sémantique](https://semver.org/lang/fr/).

## [Non publié]

### Ajouté

- **Amorçage de morfAnalytics** à partir de `morfTemplateService` : le moteur
  d'analyse de l'écosystème morfSystem. Service prêt à tourner (API HTTP, config
  JSON, annonce LAN via morfBeacon sous l'app `morfAnalytics` sur le port 45454).
- **Module `analytics`** (`AnalyticsModule`, remplace le module d'exemple) :
  squelette du moteur d'analyse — maintient un *cache de travail* (copie
  synchronisée en lecture seule, via morfSync) et exécute les analyses **à la
  demande** (`analyze()`). Les algorithmes réels (tendances, corrélations,
  anomalies…) et la synchro du cache sont des `TODO`.
- **Route `POST /analyze`** (stub) en remplacement de la route de démonstration.

### Principes (voir `../MORFSYSTEM_ARCHITECTURE.md`)

- morfAnalytics ne possède jamais la vérité des données ; MeteoHub (et les autres
  équipements) restent la source de vérité. L'équipement écrit, morfAnalytics lit.
- Présence optionnelle : aucune dépendance pour le fonctionnement nominal des
  équipements.

## [0.1.0] — 2026-07-16

### Ajouté

- **Squelette réutilisable de service morfSystem**, distillé de morfSensor et
  morfNotify : architecture identique, sans code métier.
- **Point d'extension `IModule`** + `ModuleFactory` + `ModuleRegistry` ; module
  de démonstration `ExampleModule` fonctionnel.
- **Serveur HTTP générique** (GET + POST avec corps) : `/status` (compatible
  morfBeacon), `/healthz`, `/modules`, `/modules/{id}`, `POST /example`.
- **Chargeur de configuration JSON** (`ServiceConfig`, liste `modules`).
- **Annonce LAN via morfBeacon** embarqué (vendoré dans `third_party/morf/beacon`).
- **Installation en service** : `scripts/linux/` (systemd) **et**
  `scripts/windows/install-service.ps1` (Planificateur de tâches, sans dépendance).
- **Scripts de clonage** `scripts/new-service.(sh|ps1)` : amorcent un nouveau
  projet en remplaçant tous les noms ; le résultat compile tel quel.
- Documentation FR (architecture, guide de création d'un service).
