# Analyses SiteWatch

Pages `/sitewatch` (analyses) et `/sitewatch/settings` (configuration).

morfAnalytics reçoit de SiteWatch, pour chaque site, des séries quotidiennes consolidées
sur tout l'historique en cache, plus des classements portant sur la période du rapport.
Toutes les analyses se calculent à partir de ces données : aucun journal brut n'est
conservé ici (ils restent souverains dans SiteWatch).

## La page d'analyse

- **Vue d'ensemble** (choix « Tous les sites ») : une ligne par site avec fraîcheur des
  données, visites humaines et leur variation, part des robots, erreurs 500, attaques,
  taux d'erreur et alertes actives. Un clic ouvre l'analyse détaillée.
- **Filtres** : site, période (7, 30, 60, 90, 180 jours, tout, ou dates libres), séries
  du graphique, famille de robots, filtre texte sur les classements. Les filtres vivent
  dans l'adresse : une vue se partage par simple lien.
- **Indicateurs** avec variation par rapport à la **période précédente de même durée** :
  requêtes, visites humaines, robots (dont IA et SEO), erreurs 404, 403 et 500,
  tentatives d'attaque, part des robots, taux d'erreur, part de l'IA parmi les robots.
  Les parts se comparent en points de pourcentage, pas en pourcentage relatif.
- **Graphique quotidien** avec infobulle par jour et cercles sur les jours anormaux.
- **Jours anormaux** : écart robuste à la médiane de l'historique du site (médiane et MAD,
  insensibles aux pics que l'on cherche à repérer). Un seuil absolu par série évite le
  bruit sur de petits nombres, et moins de 14 jours d'historique ne donnent aucun verdict.
- **Profil de la semaine** et **profil horaire** (humains, robots, attaques : à quelle
  heure ils arrivent).
- **Codes HTTP** (2xx à 5xx) et **classements** : pages, URL attaquées, robots par famille
  (IA, SEO, moteurs, réseaux, autres), provenance, types d'attaque, activité WordPress.
- **Liens cassés probables** : URL en 404 qui ne sont pas des scans, à corriger ou rediriger.
- **Pages en mouvement** : les 7 derniers jours contre les 7 précédents (hausses, baisses).
- **Nouveaux robots** : robots actifs cette semaine et absents la période précédente.
- **Export** CSV ou JSON des séries quotidiennes de la fenêtre.

## Alertes

Règles évaluées sur les derniers jours **complets** (3 par défaut) : un incident ancien ne
déclenche rien. Chaque règle peut être désactivée, changée de niveau, mise en sourdine ou
dirigée vers des destinations précises depuis la page de configuration.

| Règle | Niveau par défaut | Condition |
|---|---|---|
| `stale_data` | avertissement, erreur dès 3 jours | SiteWatch ne publie plus depuis 2 jours ou plus |
| `e500` | avertissement, erreur dès 10 | au moins une erreur HTTP 500 dans la journée |
| `attack_spike` | avertissement | pic de tentatives d'attaque |
| `e404_surge`, `e403_surge` | avertissement | pic d'erreurs 404 ou d'accès refusés |
| `traffic_drop` | avertissement | chute des visites humaines (site injoignable ?) |
| `traffic_surge` | information | pic inhabituel de visites |
| `bot_share` | avertissement | part des robots en hausse de 15 points ou plus sur 7 jours |
| `ai_growth` | information | robots IA multipliés par 2 ou plus sur 7 jours |
| `new_bot` | information | robot absent la période précédente, actif cette semaine |

### Envoi à morfNotify

- Chaque situation a une **clé stable** (`règle|site|jour`, ou `règle|site|robot` pour les
  nouveaux robots) : elle n'est enregistrée et envoyée qu'**une seule fois**.
- L'envoi à **morfNotify** (`POST /notify`) est **suivi** : l'état réel est enregistré
  (`envoyée`, `échec`, `en attente`, `sans envoi`, `en sourdine`). Un échec (morfNotify arrêté,
  réseau coupé) est **retenté toutes les heures**, 5 essais au plus, pendant 3 jours. Il
  reste visible dans l'historique, avec la dernière erreur.
- **Destinations** : morfNotify dispatche vers Telegram, mail, etc. La page de
  configuration charge la liste réelle depuis `GET /targets` de morfNotify. On choisit des
  destinations **globales** et, au besoin, **par règle** (par exemple les erreurs 500 vers
  le mail et Telegram, le reste vers Telegram seul). Aucune destination choisie : morfNotify
  applique ses destinations par défaut.
- Un **niveau minimal** d'envoi se règle (par défaut : avertissement) ; les alertes en
  dessous restent enregistrées.
- **Première évaluation d'un site** : état des lieux silencieux, pour ne pas inonder
  l'utilisateur au premier déploiement. Seules les situations nouvelles sont ensuite envoyées.
- Une minuterie évalue toutes les heures : c'est la seule façon de voir le **silence** de
  SiteWatch, qui ne produit alors aucun rapport.
- **Sourdine** : une règle en sourdine (par défaut 7 jours, depuis l'alerte ou la
  configuration) reste enregistrée et visible, mais n'est jamais envoyée.

## Configuration (`/sitewatch/settings`)

Aucun fichier JSON à éditer : les réglages sont enregistrés par le service dans son dossier
d'état (SQLite) et s'appliquent immédiatement. Toutes les valeurs sont bornées à
l'enregistrement.

- **Notifications** : envoi actif ou non, adresse de morfNotify (sinon `MORFNOTIFY_URL`,
  sinon `http://127.0.0.1:8789/notify`), niveau minimal, destinations, bouton de **test**.
- **Détection** : sensibilité, jours jugés, seuils de retard, seuil des erreurs 500, hausse de
  la part des robots, facteur des robots IA, rapports conservés par site.
- **Règles** : active, niveau, envoi, destinations et sourdine pour chacune.
- **Par site** : sensibilité et jours jugés propres, règles désactivées, sourdines.

## API

| Route | Rôle |
|---|---|
| `GET /sitewatch/insights` | Liste des sites. Avec `site=ID&days=30` (ou `all`, ou `from`+`to`) : analyse complète. |
| `GET /sitewatch/overview` | Vue d'ensemble de tous les sites. |
| `GET /sitewatch/export?site=ID&format=csv` | Séries quotidiennes (`csv` ou `json`). |
| `GET /sitewatch/alerts?site=ID&limit=100` | Historique des alertes avec état d'envoi. |
| `GET /sitewatch/config` | Réglages, valeurs par défaut, règles et réglages par site. |
| `POST /sitewatch/config` | `{scope, settings}` : `scope` vaut `global` ou l'identifiant d'un site. |
| `POST /sitewatch/config/mute` | `{site_id?, rule, days}` : sourdine (`days` 0 la lève, `rule` `*` = toutes). |
| `GET /sitewatch/config/targets` | Destinations de morfNotify (nom et type). |
| `POST /sitewatch/config/test` | Notification de test (`{targets?}`). |
| `GET /sitewatch/reports` | Derniers rapports bruts reçus. |

Comme les autres routes de ce service, les routes d'écriture n'ont pas d'authentification :
elles supposent le réseau local de confiance du parc (l'authentification du serveur HTTP est
un sujet distinct, voir la feuille de route du parc).

## Indicateurs pour morfMonitor (`/status`)

`sitewatch_sites`, `sitewatch_max_lag_days` (retard du site le plus en retard),
`sitewatch_alerts_24h` et `sitewatch_alert_delivery_backlog` (alertes en attente ou en
échec d'envoi).

## Limites

- Les classements (pages, robots, référents, liens cassés) portent sur la **période du
  rapport**, pas sur la fenêtre choisie.
- Un jour sans aucune ligne de log n'apparaît pas dans les séries et vaut zéro.
- Les comparaisons de pages et de robots portent sur 7 jours contre 7 jours, ancrés sur la
  dernière date de données.
