# morfAnalytics

*Lire dans une autre langue : [English](README.md) · **Français** (ce document).*

[![Version](https://img.shields.io/badge/version-0.27.0-blue)](CHANGELOG.md)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt)
![Build](https://img.shields.io/badge/CMake-3.21+-064F8C?logo=cmake)
![License](https://img.shields.io/badge/License-GPL--3.0--only-blue)

**morfAnalytics - le moteur d'analyse de l'écosystème morfSystem.** Il décharge les
équipements embarqués des traitements lourds (statistiques longues périodes,
corrélations entre capteurs, détection d'anomalies, tendances saisonnières,
comparaisons entre équipements, rapports…).

**morfAnalytics ne possède jamais la vérité des données.** Il travaille uniquement sur
une **copie locale en lecture seule** (cache de travail), recopiée depuis les
équipements. La source de vérité reste sur ceux-ci (ex. **MeteoHub**) :
l'équipement écrit, morfAnalytics lit - jamais l'inverse, ce que garantit le
collecteur, qui n'émet que des requêtes `GET`. Le cache est supprimable et
reconstructible intégralement depuis l'équipement, sans aucune perte. Sa présence
est toujours **optionnelle** : sans serveur, les équipements continuent de mesurer,
stocker, tracer et exporter comme avant ; seules les analyses avancées deviennent
indisponibles.

Voir la vision d'ensemble de l'écosystème dans `../morfSystem/docs/ARCHITECTURE.md`.

> **État : opérationnel.** Collecte incrémentale, treize analyses météo, nettoyage du cache et page
> web sont en place. Reste à écrire : la publication des résultats vers
> **morfSync**, et les analyses de corrélation et de détection d'anomalies.

> **Note d'architecture.** Le cache n'est pas alimenté via morfSync : l'équipement
> n'en est pas client, l'enveloppe de synchronisation (UUID, révision, origine)
> pesant plus que la mesure elle-même sur un ESP32 qui écrit chaque minute.
> morfSync est destiné à diffuser les **résultats d'analyse** à l'écosystème.

## Ce que fait le service

- **Collecte incrémentale** - recopie de l'historique de l'appareil, sans jamais
  redemander ce qui est déjà en cache.
- **Moteur d'analyse enfichable** - les analyses ne manipulent qu'une série
  temporelle générique à canaux nommés. Les analyses météo ne sont qu'un jeu
  parmi d'autres : un autre projet enregistre les siennes sans toucher au moteur.
- **Page web d'analyses** - servie par le service lui-même, sans ressource
  externe (consultable sur un réseau local sans accès Internet). Elle se lit dans
  l'ordre utile : **situation actuelle**, **conditions locales**, **historique et
  repères**, puis **analyses approfondies**. Les outils de maintenance et le
  détail du service restent accessibles sans encombrer la lecture. Les analyses
  encore incomplètes affichent leur progression d'apprentissage.
- **API HTTP** (GET + POST) - `GET /` (page web), `GET /analyses` (catalogue),
  `GET /status` (compatible morfBeacon), `/healthz`, `/modules`, `/modules/{id}`,
  `POST /analyze` (analyse à la demande) et `POST /data/cleanup` (nettoyage du
  cache local - jamais de la source).
- **Nettoyage du cache** - depuis la page ou l'API : neutralisation des relevés
  de capteur en panne (`0 hPa`, `0 °C`), neutralisation d'une plage, purge
  totale. N'agit que sur la copie locale ; les mesures d'origine, sur
  l'appareil, ne sont jamais touchées et le cache purgé se reconstruit seul.
- **Config** - fichier JSON avec une liste `modules` ; une fabrique les instancie.
- **Annonce LAN** - heartbeat morfBeacon (embarqué, aucune dépendance externe).
- **Installation service** - `scripts/linux/` (systemd) et `scripts/windows/`
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

- **`source_url`** - sans ce paramètre, aucune collecte n'est lancée : le service
  se contente d'exposer le cache déjà constitué. C'est le mode à utiliser pour
  analyser un historique déjà recopié alors que l'appareil est hors service.
- **`altitude_m`** - sert à ramener la pression au niveau de la mer, seule forme
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

### Espaces Web

- `/` présente les espaces d'analyse disponibles.
- `/meteohub` conserve les analyses météorologiques de MeteoHub.
- `/sitewatch` affiche les synthèses reçues automatiquement de SiteWatch.
  SiteWatch publie à la fin de chaque analyse les compteurs de requêtes, erreurs,
  robots et tentatives sensibles ; morfAnalytics les conserve localement. La page
  se met à jour automatiquement dans les secondes qui suivent et met en avant le
  taux d'erreurs, les pages concernées, les robots les plus actifs et les journées
  qui concentrent les erreurs ou les tentatives sensibles.
  Chaque synthèse est historisée dans
  `/opt/morfanalytics/cache/sitewatch-history.sqlite` ; les journaux source ne
  quittent jamais SiteWatch. La page lit directement cette base à chaque
  actualisation, y compris après un redémarrage du service. Si elle ne peut pas
  lire l'API, elle affiche un message explicite au lieu de conserver l'état
  d'attente. L'affichage est produit côté serveur, sans JavaScript, et la page
  se recharge automatiquement toutes les 30 secondes.
  Lorsque plusieurs synthèses sont disponibles, morfAnalytics ajoute des
  comparaisons temporelles, les jours anormaux, les pics, nouveaux robots et
  répétitions de tentatives sensibles : ces analyses n'existent pas dans la
  vue de bureau de SiteWatch.
- `/photo` lit la photothèque indexée par **morfPhoto** (source de vérité) :
  boîtiers, objectifs, focales (regroupées en focales usuelles) et années.
  morfAnalytics n'interroge que les agrégats de morfPhoto à intervalle régulier
  (jamais la liste des fichiers) et en garde un instantané. La source se règle
  via le module `photo` de la configuration (`source_url`, p. ex.
  `http://127.0.0.1:8793`) ; si morfPhoto est injoignable ou le module absent, la
  page l'indique explicitement.
- `/monitor` historise les métriques des machines du parc remontées par
  **morfMonitor** (CPU, mémoire, température, charge, services actifs) et les
  représente **dans le temps** : vue d'ensemble, séries CPU / RAM / température /
  charge, et une vue **qui consomme quoi** par service (top CPU et RAM, moyennes et
  maxima par service sur la période), avec sélecteur de machine et de période
  (1 h - 30 j). morfMonitor reste la sonde (« maintenant ») ; ce domaine donne la
  mémoire dans la durée. Les relevés sont historisés dans
  `/opt/morfanalytics/cache/monitor.sqlite`, avec une rétention configurable des
  relevés bruts (`retention_days`, 90 j par défaut ; `0` = illimité), première étape
  avant la compaction par paliers. Les **activités** sont aussi historisées : tout
  composant qui sait ce qu'il fait en signale une à `POST /api/monitor/activity`. Les
  **compilations** sont le premier cas — morfDeploy en émet une par build (définir
  `MORFANALYTICS_ACTIVITY_URL` sur la machine de build), et la page montre les stats
  de build par projet (nombre, taux de réussite, durée totale/moyenne/min/max) plus le
  coût système mesuré sur la fenêtre exacte de chaque build. La baseline et les
  anomalies viendront ensuite. Réglé via le module `monitor` (`sources`,
  `interval_ms`, `retention_days`).

## Architecture des pages Web

Les espaces `/`, `/meteohub`, `/sitewatch` et `/photo` sont organisés comme des
pages compilées distinctes dans `src/pages/`. Le serveur HTTP assure les routes et les
pages reçoivent uniquement les données nécessaires à leur rendu. Cette
organisation permet d'ajouter de nouvelles sources d'analyse sans transformer
`HttpServer` en fichier monolithique.

Consulter la page `http://<adresse-du-serveur>:8799/`, ou interroger une analyse
directement :

```sh
curl -X POST -H 'Content-Type: application/json' \
     -d '{"type":"zambretti"}' http://localhost:8799/analyze
```

Le catalogue complet est exposé par `GET /analyses`.

### Lire la page d'analyse

La page met d'abord en avant une synthèse de la **situation actuelle**, puis les
**conditions locales** et leur évolution à court terme. Les mesures sont ensuite
replacées dans l'**historique et les repères** du lieu (normales, cycle journalier,
records, degrés-jours). Les traitements statistiques sont regroupés dans
**Analyses approfondies et diagnostic** afin de rester disponibles sans alourdir
la consultation quotidienne.

Le graphique du **cycle journalier moyen** indique désormais ses températures
minimale et maximale, ainsi que les repères 0 h, 12 h et 23 h. Il permet de lire
l'amplitude et le moment de la variation, pas seulement sa forme.

### Conditions et prévision locale

| Analyse | `type` | Ce qu'elle apporte |
|---|---|---|
| Conditions actuelles | `current` | Point de rosée, humidité absolue, humidex, pression ramenée au niveau de la mer |
| Chaleur et humidex | `heat_risk` | Inconfort et risque local liés à la combinaison température-humidité |
| Sécheresse atmosphérique | `dry_air` | Pouvoir asséchant de l'air ; indicateur local, distinct du danger officiel de feu |
| Tendance barométrique | `pressure_trend` | Variation sur 1 h et 3 h, code OMM, alerte de chute rapide |
| Prévision locale | `zambretti` | Prévision textuelle à 12-24 h déduite de la pression |
| Risque de brouillard | `fog_risk` | Écart au point de rosée et son resserrement |
| Risque de gelée | `frost_risk` | Minimum projeté au petit matin |

### Climatologie

| Analyse | `type` | Ce qu'elle apporte |
|---|---|---|
| Normale du jour | `normals` | Écart du jour à la normale glissante du jour de l'année |
| Degrés-jours | `degree_days` | Chauffage (base 18) et climatisation (base 26), par mois |
| Amplitude diurne | `diurnal_amplitude` | Écart maximum-minimum, moyenne et extrêmes |
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

Ces analyses reposent sur trois grandeurs seulement - température, humidité,
pression. Sans vent, pluie ni état de la végétation, Zambretti, les risques de
brouillard et de gelée, et la sécheresse atmosphérique restent des
**indications locales**, pas des prévisions ni un danger officiel de feu. Chaque
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
côté du binaire, puis dans `/etc/morfsystem/morfanalytics/` ; à défaut il démarre avec un
module `analytics` par défaut, sans source, donc sans collecte.

## Installer en service

```sh
# Toutes plateformes : Linux, Windows, Raspberry Pi
sudo ./service.py install      # compile si besoin, installe, demarre
sudo ./service.py update       # recompile, remplace le binaire, redemarre
sudo ./service.py uninstall    # desinscrit, en conservant votre configuration
./service.py status            # ce que le systeme en dit
```

Un seul point d'entree partout. Ce qu'est ce service - son nom, son dossier,
ses configurations - est declare dans `service.json` a cote. Les quatre etapes
d'installation vivent une seule fois pour tout le parc ; seul le gestionnaire
de services change selon la plateforme.

Les anciens scripts `scripts/linux/` et `scripts/windows/` fonctionnent
toujours, inchanges.

Pour **redéployer la configuration** vers `/etc/morfsystem/morfanalytics/` après
l'avoir modifiée (une source MeteoHub, morfPhoto, un morfMonitor du module
`monitor`…), sans tout recompiler, utiliser la commande unifiée du parc (sauvegarde
horodatée, puis redémarre ; Linux comme Windows) :

```sh
sudo ./service.py config push --force     # ou, depuis la racine du parc : morf config deploy morfAnalytics
```

Contrairement à `service.py update` (qui n'ajoute que les clés manquantes, sans jamais
écraser), `config push` **remplace** le fichier déployé par celui du dépôt - garder un
vrai `config/morfanalytics.json` dans le clone, avec vos sources, comme référence
déployée. (Sans mode, `service.py config` n'ajoute que les clés d'une nouvelle version
en gardant vos réglages.)

La mise à jour ne remplace jamais les valeurs déjà présentes dans la
configuration, mais y **ajoute les paramètres apparus depuis l'installation** et
les signale. Sans cela, une nouvelle fonction resterait silencieusement inactive
faute de son paramètre. La désinstallation retire le service **et** son dossier
d'installation (binaire, configuration, cache) ; `--keep-config` sauvegarde la
configuration au passage, `--dry-run` montre ce qui serait supprimé. Le clone git
n'est jamais touché.

## Documentation

- [Architecture](docs/fr/ARCHITECTURE.md) - les classes et le fil d'exécution.
- [Choix de conception](docs/fr/DECISIONS.md) - les décisions structurantes et leurs raisons.
- [Journal des versions](CHANGELOG.md) · [Roadmap](ROADMAP.md) · [Contribuer](CONTRIBUTING.md)

## Licence

GPL-3.0-only - © 2026 morfredus (Frédéric Biron).
