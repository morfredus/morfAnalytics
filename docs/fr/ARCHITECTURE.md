# Architecture - morfAnalytics

Retour à l'[index de la documentation](README.md).

---

## Pages Web modulaires

Le rendu Web est organisé sous `src/pages/` : `PortalPage`, `MeteoHubPage` et
`SiteWatchPage` correspondent respectivement aux routes `/`, `/meteohub` et
`/sitewatch`. `HttpServer` ne choisit que la route et fournit les données ; une
nouvelle source doit ajouter sa propre page, sans modifier les autres espaces.

> **Convention de nommage :** ne pas créer de dossier source nommé `web/`.
> Dans le parc morfSystem, ce nom est historiquement réservé aux projets ESP32
> dont les pages Web sont générées ou minifiées au moment de la compilation.
> Cette règle d'exclusion peut donc empêcher leur déploiement et provoquer des
> propositions de commit après chaque compilation. Les pages C++ de
> morfAnalytics vivent sous `src/pages/` et `include/morfanalytics/pages/`.

Service Qt (Core, Network, Sql), sans interface graphique. Le socle ne contient
**aucun métier** : ce qui est propre à une activité donnée vit dans des `IModule`
(ici `AnalyticsModule`) et, pour les calculs, dans des `IAnalysis`.

## Les pièces

```
Service (façade : câble tout à partir d'une ServiceConfig)
├── ModuleRegistry     -> collectionne les IModule, agrège leur état
│     └── IModule (interface, QObject)   ◀── POINT D'EXTENSION
│            └── AnalyticsModule  (type « analytics » : collecte et analyses)
├── HttpServer         -> API HTTP (GET /status /healthz /modules /analyses
│                         /meteohub/series /meteohub/events ; POST /analyze)
└── morfbeacon::Heartbeat -> annonce UDP (découverte LAN)
        ▲ IMetricsProvider
        └── ModuleRegistry expose un résumé (nombre de modules, ...)
```

### `ServiceConfig` / `ModuleDef`

Chargées depuis un fichier JSON. `ServiceConfig` porte les réglages globaux
(`httpPort`, `bindAddress`, `beacon`) et la liste des modules. Un `ModuleDef`
(`type`, `id`, `params`) décrit un module à instancier. **À enrichir** avec vos
réglages propres.

### `IModule` (interface, QObject) - le point d'extension

C'est **ici** que vit le métier. Une sous-classe implémente `start()`, `stop()`
et `statusJson()` (état exposé dans `/modules`), et peut émettre `updated()`.
`id()`/`type()` l'identifient. Voir `AnalyticsModule` (domaine météo) : il détient
les caches de travail (intérieur, extérieur, prévisions), pilote leurs collecteurs
et expose le registre d'analyses (chacune avec son contexte IN / OUT / les deux).

### `ModuleFactory`

Fabrique un `IModule` à partir d'un `ModuleDef`. Point d'extension **compile-time** :
une branche par type ; `knownTypes()` les liste.

### `ModuleRegistry` (QObject + `morfbeacon::IMetricsProvider`)

Détient les modules, les démarre/arrête, agrège leur `statusJson()` pour `/modules`
et fournit un résumé à `/status` (via `IMetricsProvider`).

### `HttpServer` (QObject)

Serveur HTTP/1.1 minimal gérant **GET et POST** (lecture du corps via
`Content-Length`). Il expose `GET /analyses` (analyses disponibles) et
`POST /analyze`, ainsi que les routes de service `/status`, `/healthz` et
`/modules`.

### Moteur d'analyse et d'événements (pur)

Deux briques PURES (pas d'état, pas de cache, pas de JSON), testables isolément :
- `MeteoMath` : les formules météo (point de rosée, humidex, Zambretti…).
- `MeteoEvents` : la **source commune** des événements temporels tirés des séries
  (croisements Intérieur/Extérieur d'une même grandeur, changements de tendance,
  changements de régime). Le même calcul alimente l'endpoint `/meteohub/events`
  (consommé par la page Graphiques pour ses marqueurs et son encart) ET les
  analyses de `MeteoAnalyses` (qui enrichissent leur texte : « changement de
  tendance vers HH:MM », « dernier croisement… »). Écrit une fois, jamais
  réimplémenté différemment d'un consommateur à l'autre.

`MeteoAnalyses` enregistre le jeu d'analyses météo dans `AnalysisRegistry`
(générique) ; `AnalyticsModule::eventsJson()` assemble le JSON des événements.

### `Service` (façade)

L'unique objet manipulé par le démon : construit les modules (via la fabrique),
démarre le serveur HTTP puis le heartbeat morfBeacon.

## Fil d'exécution

Tout tourne sur **le thread principal Qt**. Les modules travaillent de façon
événementielle (timers, sockets) et exposent un instantané via `statusJson()` ;
le serveur HTTP répond sans bloquer. Un module lent doit rester asynchrone et ne
publier qu'un instantané.

## Dépendance morfBeacon (embarquée)

morfBeacon est vendoré dans `third_party/morf/beacon` (lié statiquement) : build
autonome, sans dépôt externe. Resynchroniser avec `scripts/sync-morf.(sh|ps1)`.

## Portabilité

Aucun code spécifique à une plateforme. Comportement identique
Windows / Linux x64 / Raspberry Pi (ARM64). Installation en service fournie pour
systemd (Linux) et Planificateur de tâches (Windows).
