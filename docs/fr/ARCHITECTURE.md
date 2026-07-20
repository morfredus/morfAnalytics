# Architecture — morfAnalytics

Retour à l'[index de la documentation](README.md).

---

Service Qt (Core, Network, Sql), sans interface graphique. Le socle ne contient
**aucun métier** : ce qui est propre à une activité donnée vit dans des `IModule`
(ici `AnalyticsModule`) et, pour les calculs, dans des `IAnalysis`.

## Les pièces

```
Service (façade : câble tout à partir d'une ServiceConfig)
├── ModuleRegistry     -> collectionne les IModule, agrège leur état
│     └── IModule (interface, QObject)   ◀── POINT D'EXTENSION
│            └── AnalyticsModule  (type « analytics » : collecte et analyses)
├── HttpServer         -> API HTTP (GET /status /healthz /modules /analyses ;
│                         POST /analyze)
└── morfbeacon::Heartbeat -> annonce UDP (découverte LAN)
        ▲ IMetricsProvider
        └── ModuleRegistry expose un résumé (nombre de modules, ...)
```

### `ServiceConfig` / `ModuleDef`

Chargées depuis un fichier JSON. `ServiceConfig` porte les réglages globaux
(`httpPort`, `bindAddress`, `beacon`) et la liste des modules. Un `ModuleDef`
(`type`, `id`, `params`) décrit un module à instancier. **À enrichir** avec vos
réglages propres.

### `IModule` (interface, QObject) — le point d'extension

C'est **ici** que vit le métier. Une sous-classe implémente `start()`, `stop()`
et `statusJson()` (état exposé dans `/modules`), et peut émettre `updated()`.
`id()`/`type()` l'identifient. Voir `AnalyticsModule` : il détient le cache de
travail, pilote le collecteur et expose le registre d'analyses.

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
