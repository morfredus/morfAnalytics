# morfAnalytics

*Lire dans une autre langue : [English](README.md) · **Français** (ce document).*

[![Version](https://img.shields.io/badge/version-0.5.0-blue)](CHANGELOG.md)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![Build](https://img.shields.io/badge/CMake-3.21+-064F8C?logo=cmake)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**morfAnalytics — le moteur d'analyse de l'écosystème morfSystem.** Il décharge les
équipements embarqués des traitements lourds (statistiques longues périodes,
corrélations entre capteurs, détection d'anomalies, tendances saisonnières,
comparaisons entre équipements, rapports…).

**morfAnalytics ne possède jamais la vérité des données.** Il travaille uniquement sur
une **copie locale en lecture seule** (cache de travail), recopiée depuis les
équipements. La source de vérité reste sur ceux-ci (ex. **MeteoHub**) :
l'équipement écrit, morfAnalytics lit — jamais l'inverse, ce que garantit le
collecteur, qui n'émet que des requêtes `GET`. Le cache est supprimable et
reconstructible intégralement depuis l'équipement, sans aucune perte. Sa présence
est toujours **optionnelle** : sans serveur, les équipements continuent de mesurer,
stocker, tracer et exporter comme avant ; seules les analyses avancées deviennent
indisponibles.

Voir la vision d'ensemble de l'écosystème dans `../MORFSYSTEM_ARCHITECTURE.md`.

> **État : opérationnel.** Collecte incrémentale, douze analyses météo et page
> web sont en place. Reste à écrire : la publication des résultats vers
> **morfSync**, et les analyses de corrélation et de détection d'anomalies.

> **Note d'architecture.** Le cache n'est pas alimenté via morfSync : l'équipement
> n'en est pas client, l'enveloppe de synchronisation (UUID, révision, origine)
> pesant plus que la mesure elle-même sur un ESP32 qui écrit chaque minute.
> morfSync est destiné à diffuser les **résultats d'analyse** à l'écosystème.

## Ce que fait le service

- **Collecte incrémentale** — recopie de l'historique de l'appareil, sans jamais
  redemander ce qui est déjà en cache.
- **Moteur d'analyse enfichable** — les analyses ne manipulent qu'une série
  temporelle générique à canaux nommés. Les analyses météo ne sont qu'un jeu
  parmi d'autres : un autre projet enregistre les siennes sans toucher au moteur.
- **Page web d'analyses** — servie par le service lui-même, sans ressource
  externe (consultable sur un réseau local sans accès Internet).
- **API HTTP** (GET + POST) — `GET /` (page web), `GET /analyses` (catalogue),
  `GET /status` (compatible morfBeacon), `/healthz`, `/modules`, `/modules/{id}`,
  et `POST /analyze` (analyse à la demande).
- **Config** — fichier JSON avec une liste `modules` ; une fabrique les instancie.
- **Annonce LAN** — heartbeat morfBeacon (embarqué, aucune dépendance externe).
- **Installation service** — `scripts/linux/` (systemd) et `scripts/windows/`
  (Planificateur de tâches), copie binaire + config dans un dossier fixe.

## Configurer la collecte depuis MeteoHub

Renseigner l'adresse de l'appareil et l'altitude de la station dans le module
`analytics` (voir `config/morfanalytics.example.json`) :

```jsonc
{
  "type": "analytics",
  "id": "analytics-1",
  "maintenance_ms": 60000,      // période entre deux cycles de collecte
  "cache_dir": "cache",         // dossier du cache de travail
  "source_url": "http://192.168.1.42",
  "altitude_m": 8               // altitude de la station, en mètres
}
```

- **`source_url`** — sans ce paramètre, aucune collecte n'est lancée : le service
  se contente d'exposer le cache déjà constitué. C'est le mode à utiliser pour
  analyser un historique déjà recopié alors que l'appareil est hors service.
- **`altitude_m`** — sert à ramener la pression au niveau de la mer, seule forme
  comparable aux bulletins météo. Compter environ **0,12 hPa par mètre** : à
  quelques mètres l'écart est négligeable, à quelques centaines il change la
  prévision. Renseigner l'altitude réelle du capteur, pas celle de la commune.
  Une altitude **nulle est une valeur valide** (station au bord de mer) ; c'est
  l'*absence* du paramètre qui est signalée dans les analyses de pression.

Consulter ensuite `http://<adresse-du-serveur>:8799/` pour suivre l'avancement de
la collecte. Le premier cycle recopie l'intégralité de l'historique présent sur la
carte SD et peut donc durer plusieurs minutes ; les cycles suivants ne
transfèrent que les nouvelles mesures.

Le cache est un simple fichier SQLite dans `cache_dir`. Il peut être supprimé à
tout moment : il sera reconstruit depuis l'appareil, sans perte, puisque la
source de vérité reste MeteoHub.

## Les analyses disponibles

Consulter la page `http://<adresse-du-serveur>:8799/`, ou interroger une analyse
directement :

```sh
curl -X POST -H 'Content-Type: application/json' \
     -d '{"type":"zambretti"}' http://localhost:8799/analyze
```

Le catalogue complet est exposé par `GET /analyses`.

### Conditions et prévision locale

| Analyse | `type` | Ce qu'elle apporte |
|---|---|---|
| Conditions actuelles | `current` | Point de rosée, humidité absolue, humidex, pression ramenée au niveau de la mer |
| Tendance barométrique | `pressure_trend` | Variation sur 1 h et 3 h, code OMM, alerte de chute rapide |
| Prévision locale | `zambretti` | Prévision textuelle à 12–24 h déduite de la pression |
| Risque de brouillard | `fog_risk` | Écart au point de rosée et son resserrement |
| Risque de gelée | `frost_risk` | Minimum projeté au petit matin |

### Climatologie

| Analyse | `type` | Ce qu'elle apporte |
|---|---|---|
| Normale du jour | `normals` | Écart du jour à la normale glissante du jour de l'année |
| Degrés-jours | `degree_days` | Chauffage (base 18) et climatisation (base 26), par mois |
| Amplitude diurne | `diurnal_amplitude` | Écart maximum–minimum, moyenne et extrêmes |
| Records | `records` | Minima et maxima absolus datés |
| Jours remarquables | `streaks` | Gel, fortes chaleurs, nuits tropicales, séries consécutives |
| Cycle journalier | `daily_cycle` | Température moyenne par heure, heures extrêmes |
| Complétude | `data_quality` | Journées complètes, partielles et trous de collecte |

Paramètres facultatifs : `days` (profondeur de la fenêtre), `window_days` (demi-
fenêtre des normales), `heating_base` / `cooling_base` (degrés-jours).

Une analyse qui manque d'historique ne renvoie pas d'erreur HTTP : elle répond
`ok: false` avec la raison et la profondeur requise. Le service a bien répondu ;
c'est le résultat qui n'est pas calculable, et il vaut mieux le dire que de
publier une moyenne calculée sur trois mesures.

### Limites assumées

Ces analyses reposent sur trois grandeurs seulement — température, humidité,
pression. Sans vent ni ensoleillement, Zambretti et les risques de brouillard et
de gelée restent des **indications locales**, pas des prévisions. Chaque
résultat porte la note correspondante, affichée telle quelle dans la page.

## Compiler

Nécessite seulement **Qt 6** (Core, Network, Sql). morfBeacon est vendoré dans
`third_party/morf/beacon`.

```sh
cmake --preset mingw        # ou linux / linux-arm64
cmake --build --preset mingw
```

## Lancer

```sh
./build-mingw/service/morfanalytics.exe --config config/morfanalytics.example.json
curl http://127.0.0.1:8799/analyses
```

Sans `--config`, le service cherche une configuration dans le dossier courant, à
côté du binaire, puis dans `/etc/morfanalytics/` ; à défaut il démarre avec un
module `analytics` par défaut, sans source, donc sans collecte.

## Installer en service

```sh
# Linux (systemd)
sudo ./scripts/linux/install-service.sh     # installer
sudo ./scripts/linux/update-service.sh      # mettre à jour (git pull + build)
sudo ./scripts/linux/uninstall-service.sh   # tout supprimer, pour repartir de zéro

# Windows (Planificateur de tâches, PowerShell Administrateur)
powershell -ExecutionPolicy Bypass -File scripts\windows\install-service.ps1
powershell -ExecutionPolicy Bypass -File scripts\windows\uninstall-service.ps1
```

La mise à jour ne remplace jamais les valeurs déjà présentes dans la
configuration, mais y **ajoute les paramètres apparus depuis l'installation** et
les signale. Sans cela, une nouvelle fonction resterait silencieusement inactive
faute de son paramètre. La désinstallation retire le service **et** son dossier
d'installation (binaire, configuration, cache) ; `--keep-config` sauvegarde la
configuration au passage, `--dry-run` montre ce qui serait supprimé. Le clone git
n'est jamais touché.

## Documentation

- [Architecture](docs/fr/ARCHITECTURE.md) — les classes et le fil d'exécution.
- [Choix de conception](docs/fr/DECISIONS.md) — les décisions structurantes et leurs raisons.
- [Journal des versions](CHANGELOG.md) · [Roadmap](ROADMAP.md) · [Contribuer](CONTRIBUTING.md)

## Licence

GPL-3.0-only — © 2026 morfredus (Frédéric Biron).
