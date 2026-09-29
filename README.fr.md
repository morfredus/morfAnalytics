# morfAnalytics

*Lire dans une autre langue : [English](README.md) · **Français** (ce document).*

[![Version](https://img.shields.io/badge/version-0.63.1-blue)](CHANGELOG.md)
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

Il historise le trafic GitHub **consolidé par SiteWatch** (vues, clones,
téléchargements). Il n'interroge ni GitHub ni morfCollector pour ces métriques.

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
- **Nettoyage du cache** - depuis la page ou l'API : neutralisation d'une plage,
  purge totale (les relevés de capteur en panne, `0 hPa`, sont rejetés dès
  l'import). Les points écartés par la qualification se listent et se
  réintègrent un par un (état du service, réversible). N'agit que sur la copie locale ; les mesures d'origine, sur
  l'appareil, ne sont jamais touchées et le cache purgé se reconstruit seul.
- **Config** - fichier JSON avec une liste `modules` ; une fabrique les instancie.
- **Annonce LAN** - heartbeat morfBeacon (embarqué, aucune dépendance externe).
- **Installation service** - `scripts/linux/` (systemd) et `scripts/windows/`
  (Planificateur de tâches), copie binaire + config dans un dossier fixe.

## Configurer la collecte depuis MeteoHub

Renseigner l'adresse de l'appareil dans le module
`analytics` (voir `config/morfanalytics.example.json`) :

```jsonc
{
  "type": "analytics",
  "id": "analytics-1",
  "maintenance_ms": 60000,      // période entre deux cycles de collecte
  "cache_dir": "cache",         // dossier du cache de travail
  "source_url": "http://192.168.1.42"
}
```

- **`source_url`** - sans ce paramètre, aucune collecte n'est lancée : le service
  se contente d'exposer le cache déjà constitué. C'est le mode à utiliser pour
  analyser un historique déjà recopié alors que l'appareil est hors service.
- **Pas d'altitude ici.** MeteoHub publie une pression déjà ramenée au niveau de
  la mer, avec l'altitude de chacun de ses capteurs (page Système du hub).
  morfAnalytics l'analyse telle quelle et ne connaît pas l'installation : déplacer
  la sonde se règle dans MeteoHub seul. Un ancien paramètre `altitude_m` est
  ignoré, avec un avertissement au démarrage.

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
  `/var/lib/morfsystem/morfanalytics/sitewatch-history.sqlite` ; les journaux source ne
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
  boîtiers, objectifs, focales (regroupées en focales usuelles), années, et le
  **contexte photographique** par dossier (`context` et `subject`, deux dimensions
  indépendantes qualifiées dans PhotoHub). Des **presets** croisent ces dimensions -
  « Focale naturelle » = `DECOUVERTE + GENERAL`, animalier spontané vs préparé,
  événements, spectacles - à comparer entre eux ; ce ne sont que des raccourcis de
  filtres, jamais une règle inscrite dans les données.
  morfAnalytics n'interroge que les agrégats de morfPhoto à intervalle régulier
  (jamais la liste des fichiers) et en garde un instantané. Les boîtiers possédés
  s'enregistrent depuis l'onglet Configuration (`POST /photo/practice`) pour
  pouvoir les rappeler et les modifier plus tard. Chaque poste analysé n'apparaît
  qu'une fois, sous son nom d'hôte s'il est connu (l'IP n'est qu'un repli).
  La source se règle
  via le module `photo` de la configuration (`source_url`, p. ex.
  `http://127.0.0.1:8793`) ; si morfPhoto est injoignable ou le module absent, la
  page l'indique explicitement.
- `/monitor` historise les métriques des machines du parc remontées par
  **morfMonitor** (CPU, mémoire, température, charge, services actifs) et les
  représente **dans le temps** : vue d'ensemble, séries CPU / RAM / température /
  charge, et une vue **qui consomme quoi** par service (top CPU et RAM, moyennes et
  maxima par service sur la période), avec sélecteur de machine et de période
  (1 h - 30 j). morfMonitor reste la sonde (« maintenant ») ; ce domaine donne la
  mémoire dans la durée. Une section **Supervision** lit la mémoire temporelle
  propre à morfMonitor (contrat `morfhistory/1`, en read-through mis en cache,
  `GET /monitor/history?machine=`) : disponibilité globale, table du jour par service
  (incidents par cause, indisponibilité), chronologie 24 h, graphes de tendance
  disponibilité/incidents et table des trimestres. Cette mémoire appartient à
  morfMonitor ; morfAnalytics ne fait que la lire et la représenter. Les relevés
  (ressources) sont historisés dans
  `/var/lib/morfsystem/morfanalytics/monitor.sqlite`, avec une rétention configurable des
  relevés bruts (`retention_days`, 90 j par défaut ; `0` = illimité), première étape
  avant la compaction par paliers. Les **activités** sont aussi historisées : tout
  composant qui sait ce qu'il fait en signale une à `POST /api/monitor/activity`. Les
  **compilations** sont le premier cas - morfDeploy en émet une par build (définir
  `MORFANALYTICS_ACTIVITY_URL` sur la machine de build). Les horodatages `start` /
  `end` sont lus même quand JSON les envoie comme entiers (cas Python). La page
  montre les stats de build par projet (nombre, taux de réussite, durée
  totale/moyenne/min/max, y compris les secondes) plus le coût système mesuré
  autour de chaque build. La baseline et les
  anomalies viendront ensuite. Réglé via le module `monitor` (`sources`,
  `interval_ms`, `retention_days`).

## Architecture des pages Web

Les espaces `/`, `/meteohub`, `/sitewatch` et `/photo` sont organisés comme des
pages compilées distinctes dans `src/pages/`. Le serveur HTTP assure les routes et les
pages reçoivent uniquement les données nécessaires à leur rendu. Cette
organisation permet d'ajouter de nouvelles sources d'analyse sans transformer
`HttpServer` en fichier monolithique.

Le thème (palette sombre par défaut, variante claire) et le bandeau de retour sont
communs : `src/pages/Theme.cpp` les injecte dans chaque page à la place de trois
marqueurs (`<!--theme-head-->`, `<!--theme-toggle-->`, `<!--nav-back-->`). Une page
n'écrit aucune couleur en dur : elle emploie les variables CSS de la palette, et ses
graphiques les lisent en JavaScript avec `mfaColor("--nom")`. Le choix du thème est
mémorisé dans le navigateur ; détails dans
[docs/fr/INTERFACE-WEB.md](docs/fr/INTERFACE-WEB.md).

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

### Confort, bâtiment et prévision (analyses contextuelles)

Chaque analyse porte un **contexte** intrinsèque - Extérieur (météo), Intérieur
(confort) ou Les deux (relation intérieur/extérieur) - surchargeable par requête
avec `ctx=in|out|both`, jamais par un repli croisé silencieux. L'intérieur et
l'extérieur sont deux caches séparés ; la prévision a le sien.

| Analyse | `type` | Contexte | Ce qu'elle apporte |
|---|---|---|---|
| Confort intérieur | `indoor_comfort` | Intérieur | Zones de confort température/humidité, point de rosée, repère de moisissure |
| Comportement thermique du bâtiment | `thermal_behaviour` | Les deux | Écart intérieur/extérieur, amortissement, décalage d'inertie |
| Modèle d'inertie intérieure | `indoor_inertia_model` | Les deux | Ajuste l'intérieur à partir de l'extérieur décalé (gain, offset, R²/RMSE), prédit vs réel |
| Prévu vs observé | `forecast_vs_observed` | Extérieur | Prévision « du lendemain » vs min/max observés : biais et erreur moyenne |

Paramètres facultatifs : `days` (profondeur de la fenêtre), `window_days` (demi-
fenêtre des normales), `heating_base` / `cooling_base` (degrés-jours), `ctx`
(forçage du contexte).

Au-delà des analyses, un onglet **Graphiques** (`/meteohub/graphs`) trace les
séries dans le temps - température, humidité, pression, source intérieur /
extérieur / les deux - en complément visuel des analyses. La fenêtre est soit
glissante (6 h à 30 j, jusqu'à maintenant), soit libre : le bouton « Période
libre » fixe un jour + heure de début et de fin, au pas de 5 min, pour revenir sur
un moment passé. `/meteohub/series` et `/meteohub/events` acceptent `from`/`to`
(secondes epoch) en plus de `hours`.

**Qualité des mesures (0.58.0).** Les mesures ne sont jamais modifiées dans le
cache : elles sont **qualifiées à la lecture**. Un point hors bornes physiques,
un pic isolé (écart aux deux voisins, dans le même sens, qui revient ensuite) ou
une mesure extérieure prise pendant une période annotée « sonde hors
conditions » (sonde rentrée à l'intérieur, sur l'établi) est écarté des
analyses et de la détection d'événements. Les Graphiques le montrent en croix
grise, avec son motif au survol et un bilan sous la courbe. Seuils prudents :
mieux vaut laisser passer un petit artefact que d'écarter une vraie variation
rapide.

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

## API HTTP

Chaque page de l'interface est servie par le service lui-même et lit ses données
sur une route JSON. Le tableau les rassemble toutes, pour qu'un script (ou un autre
composant) puisse s'en servir directement.

| Route | Rôle |
|---|---|
| `GET /healthz` · `GET /status` | Vivacité, et rapport riche du contrat morfSystem |
| `GET /modules` · `GET /modules/<nom>` | Modules déclarés et leur état |
| `GET /`, `/meteohub`, `/meteohub/graphs`, `/sitewatch`, `/photo`, `/monitor`, `/github` | Pages HTML (thème sombre / clair commun, mémorisé par le navigateur ; `?back=<url>&back_label=<nom>` optionnel ajoute un retour vers l'application appelante) |
| `GET /analyses` | Catalogue des analyses disponibles (la page se construit à partir de lui) |
| `POST /analyze` | Lancer une analyse à la demande |
| `GET /meteohub/series` · `GET /meteohub/events` | Séries sous-échantillonnées et événements temporels (croisements, tendances, régimes) de l'onglet Graphiques |
| `GET·POST /meteohub/annotations`, `POST /meteohub/annotations/delete` | Observations météo humaines |
| `POST /data/cleanup` | Vider le cache de travail local (jamais les mesures de l'appareil) |
| `POST /sitewatch/ingest` · `GET /sitewatch/reports` | Synthèses SiteWatch : réception, puis historique |
| `POST /github/ingest` · `GET /github/data?repo=&from=&to=` | Trafic GitHub consolidé par SiteWatch |
| `GET /photo/data?sources=` · `GET /photo/sources` · `GET·POST /photo/practice` | Données de la page Photo, postes morfPhoto connus, périmètre des boîtiers possédés |
| `GET /monitor/data` · `GET /monitor/history?machine=` | Relevés des machines et historique de supervision (`morfhistory/1`) |
| `POST /api/monitor/activity` | Signaler une activité (compilation, indexation...) à historiser |
| `POST /api/monitor/forget` | Oublier une machine et tout son historique (`{"machine": "..."}`) |

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
