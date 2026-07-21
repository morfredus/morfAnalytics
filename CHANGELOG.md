# Journal des versions — morfAnalytics

Le format s'inspire de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/)
et du [versionnage sémantique](https://semver.org/lang/fr/).

## [0.5.0] – 2026-07-21

### Ajouté

- **Déclaration de l'interface Web (capacité `web_ui`).** morfAnalytics sert une
  page d'accueil ; il l'annonce désormais, et un observateur peut proposer un
  lien vers les analyses **sans rien connaître de morfAnalytics**.

  La capacité `advanced_analysis` est conservée : c'est par elle que MeteoHub
  détecte ce service, par capacité et jamais par nom.

- Le bloc `web_ui` est publié dans `/status`. morfAnalytics sert son **propre**
  `/status` plutôt que le `StatusServer` de morfBeacon : il doit donc fournir ce
  détail lui-même, sans quoi il annoncerait une capacité dont le moyen
  d'ouverture resterait introuvable.

## [Non publié]

## [0.4.1] – 2026-07-20

### Corrigé

- **`docs/fr/ARCHITECTURE.md` décrivait le template et non morfAnalytics** :
  le schéma citait `ExampleModule` (inexistant ici) et les routes HTTP étaient
  données « à titre d'exemple ». Le document décrit désormais `AnalyticsModule`
  (type `analytics`) et les routes réelles `GET /analyses` et `POST /analyze`.

## [0.4.0] – 2026-07-19

### Ajouté

- **Annonce de la capacité `advanced_analysis`** dans le heartbeat morfBeacon.
  C'est par elle que MeteoHub reconnaît le service, et non par son nom : celui-ci
  est librement modifiable (licence GPL), et une reconnaissance fondée sur le nom
  cesserait de fonctionner au premier renommage. Renommer `app_name` en « Mon
  Analyse Météo » n'interrompt donc plus l'intégration ; MeteoHub affichera ce
  nom dans son menu. Nécessite morfBeacon ≥ 0.2.0 (copie vendorée resynchronisée).
- **Lien « ← Retour à MeteoHub »** en tête de la page web. MeteoHub ouvre ce
  service dans le **même onglet** pour ne pas accumuler d'onglets ; sans ce lien,
  seul le bouton « précédent » du navigateur permettait de revenir. L'adresse
  utilisée est celle de la source collectée (`source_url`) — la seule que ce
  service connaisse.

### Compatibilité

- Un MeteoHub à partir de la version 1.12.0 ne détecte que les services
  annonçant `advanced_analysis` : les versions de morfAnalytics antérieures à
  0.4.0 ne sont plus découvertes automatiquement. Elles restent joignables en
  saisissant leur adresse dans la page Système de MeteoHub.

## [0.3.3] – 2026-07-19

### Corrigé

- **La mise à jour ne livrait jamais les nouveaux paramètres de configuration.**
  `update-service.sh` ne recopiait que le binaire et laissait
  `/opt/morfanalytics/morfanalytics.json` intact, par souci de préserver les
  réglages locaux. Conséquence : les paramètres apparus depuis l'installation —
  `source_url` et `altitude_m`, introduits en 0.2.0 et 0.3.x — restaient absents
  indéfiniment, et la collecte ne démarrait jamais **sans que rien ne le
  signale**. La mise à jour **complète** désormais la configuration
  (`merge-config.py`) : les valeurs déjà en place ne sont jamais modifiées, les
  clés manquantes sont ajoutées puis listées, et une sauvegarde est prise avant
  toute écriture.
- **L'unité systemd n'était pas rafraîchie non plus.** Une modification du
  fichier `.service` dans le dépôt ne parvenait jamais à `/etc/systemd/system`.
- **`source_url` ne porte plus d'adresse d'exemple dans la configuration type.**
  Elle valait `http://192.168.1.42` : une adresse plausible mais fausse, que la
  fusion aurait injectée telle quelle dans les installations existantes, avec
  une erreur ressemblant à une panne réseau. Le champ est vide par défaut, ce
  que le service interprète déjà comme « aucune collecte ».

### Ajouté

- **Scripts de désinstallation complète** (`scripts/linux/uninstall-service.sh`,
  `scripts/windows/uninstall-service.ps1`), pour repartir d'une installation
  vierge. Le `--uninstall` des installeurs se contente de retirer le service et
  **conserve** le dossier d'installation ; ces scripts retirent aussi le
  binaire, la configuration et le cache. Options `--keep-config` (sauvegarde la
  configuration) et `--dry-run` (montre sans supprimer). Le clone git n'est
  jamais touché, et le cache étant une copie, sa suppression ne perd rien.

### Modifié

- **Le dépôt n'est plus présenté comme un modèle à cloner.** morfAnalytics est un
  service à part entière : les scripts de clonage (`new-service.sh`/`.ps1`), le
  guide « créer votre service » et le vocabulaire de squelette/template ont été
  retirés de la documentation, du CMakeLists, de l'aide en ligne de commande et
  des commentaires. `ROADMAP.md` et `CONTRIBUTING.md` sont réécrits pour ce
  qu'est le projet, `CONTRIBUTING.md` expliquant notamment comment ajouter une
  analyse.

## [0.3.2] – 2026-07-19

### Corrigé

- **La collecte échouait sur l'appareil réel** (« Operation timed out », aucune
  mesure importée). Les lots étaient demandés par 2000 enregistrements, alors que
  MeteoHub abandonne sa réponse au-delà d'environ 300 : la requête se terminait
  sans le moindre octet. Un lot vaut désormais **250 enregistrements**, valeur
  mesurée sûre sur le matériel. Une journée complète demande six requêtes au lieu
  d'une, ce qui est le prix à payer pour ne jamais dépendre d'une taille de
  réponse que l'appareil ne sait pas tenir.
- **Délai d'attente porté de 20 à 45 secondes.** L'ESP32 lit sa carte SD tout en
  servant sa propre interface web ; plusieurs secondes par requête sont normales.

### Validé sur le matériel

Collecte complète depuis un MeteoHub réel (`meteohub.local`, firmware 1.11.2) :
6963 mesures sur 5 journées importées sans erreur, le cycle suivant ne reprenant
que la seule mesure nouvelle. Les cinq analyses de conditions et de prévision
locale produisent des résultats cohérents ; les six analyses climatologiques
annoncent correctement « historique insuffisant » avec la profondeur requise,
5 journées ne suffisant pas à leurs seuils de 7 à 30 jours.

## [0.3.1] – 2026-07-19

### Corrigé

- **Une altitude nulle n'est plus confondue avec une altitude non renseignée.**
  L'absence du paramètre était détectée en testant `altitude_m ≈ 0`, si bien
  qu'une station réellement au niveau de la mer — une valeur parfaitement
  légitime — était signalée à tort comme mal configurée dans les analyses de
  pression. C'est désormais la *présence* de la clé dans la configuration qui
  fait foi (`AnalysisContext::altitudeKnown`).

### Modifié

- L'avertissement sur l'altitude manquante indique ce qui est réellement en jeu
  (environ **0,12 hPa par mètre**) au lieu de laisser entendre que toute
  prévision est faussée : à quelques mètres l'écart est négligeable, il ne le
  devient qu'à partir de quelques dizaines de mètres.
- `GET /modules` expose `altitude_known` à côté de `altitude_m`.
- L'exemple de configuration et les README utilisent 8 m comme valeur
  d'illustration, à adapter à chaque installation.

## [0.3.0] – 2026-07-19

### Ajouté

- **Moteur d'analyse enfichable** (`IAnalysis`, `AnalysisRegistry`). Le moteur
  ignore la météo : les analyses ne manipulent qu'une série temporelle générique
  à canaux nommés (`Series`). Le jeu météo est enregistré par un unique appel à
  `registerMeteoAnalyses()` ; un autre projet enregistre le sien sans toucher au
  moteur.
- **Formules météorologiques** (`MeteoMath`), isolées en fonctions pures :
  point de rosée (Magnus-Tetens), humidité absolue, humidex, pression ramenée au
  niveau de la mer, codes de tendance barométrique de l'OMM, algorithme Zambretti.
- **Cinq analyses de conditions et de prévision locale** : conditions actuelles
  enrichies (`current`), tendance barométrique avec alerte de chute rapide
  (`pressure_trend`), prévision locale Zambretti (`zambretti`), risque de
  brouillard (`fog_risk`), risque de gelée par extrapolation du refroidissement
  nocturne (`frost_risk`).
- **Six analyses climatologiques** — celles que l'appareil ne peut pas calculer
  lui-même, faute de profondeur d'historique : normale glissante du jour de
  l'année et écart du jour (`normals`), degrés-jours de chauffage et de
  climatisation par mois (`degree_days`), amplitude thermique diurne
  (`diurnal_amplitude`), records datés (`records`), jours de gel, fortes
  chaleurs, nuits tropicales et séries consécutives (`streaks`), cycle journalier
  moyen (`daily_cycle`).
- **Analyse de complétude** (`data_quality`) : journées complètes, partielles et
  trous de collecte. Les journées de début et de fin de fenêtre en sont exclues,
  étant tronquées par la fenêtre elle-même et non par un défaut de collecte.
- **Route `GET /analyses`** : catalogue des analyses (identifiant, titre, groupe,
  profondeur d'historique requise). La page web se construit à partir de cette
  liste et n'en code aucune en dur.
- **Page web d'analyses** servie par `GET /`, sans aucune ressource externe :
  cartes par groupe, courbe du cycle journalier, barres des degrés-jours,
  tableaux de records et de séries.

### Modifié

- **`POST /analyze` exécute réellement les analyses** au lieu de répondre
  `not_implemented`.
- Une analyse manquant d'historique répond `ok: false` avec la raison et la
  profondeur requise, sans code d'erreur HTTP : le service a bien répondu, c'est
  le résultat qui n'est pas calculable.

### Limitations connues

- Les analyses reposent sur trois grandeurs seulement (température, humidité,
  pression). Sans vent ni ensoleillement, Zambretti et les risques de brouillard
  et de gelée restent des **indications locales**, pas des prévisions. Chaque
  résultat porte la note correspondante.
- Sans `altitude_m` renseignée, la pression n'est pas réellement ramenée au
  niveau de la mer et Zambretti est faussé dès que la station n'est pas au bord
  de mer. Le cas est signalé dans le résultat et dans la page.
- La publication des résultats vers morfSync reste à écrire.

## [0.2.0] – 2026-07-19

### Ajouté

- **Collecte incrémentale de l'historique de MeteoHub** (`MeteoHubCollector`) :
  le service recopie les mesures de l'appareil dans un cache de travail local et
  ne redemande **jamais** ce qu'il détient déjà. Chaque cycle compare, jour par
  jour, le nombre de mesures annoncé par l'appareil à celui présent en cache, et
  ne télécharge que l'écart. Sans nouveauté, un cycle ne coûte qu'une requête.
  Les requêtes sont séquentielles, afin de ne pas saturer l'ESP32 qui sert par
  ailleurs sa propre interface web.
- **Cache de travail local en SQLite** (`SampleStore`) : une copie, jamais la
  source de vérité. Il peut être supprimé et reconstruit intégralement depuis
  l'appareil sans aucune perte.
- **Reprise de collecte par `(jour, index)` et non par horodatage.** Un
  horodatage ne peut pas servir de repère fiable : il recule lors d'un recalage
  d'horloge et se répète lors du passage à l'heure d'hiver, ce qui ferait sauter
  ou dupliquer des mesures. Les fichiers journaliers de MeteoHub étant écrits en
  ajout seul, la position d'une mesure dans son fichier ne change jamais. La
  clé primaire `(jour, index)` rend de plus l'import idempotent : réimporter une
  plage déjà connue ne crée aucun doublon.
- **Page d'accueil `GET /`** : état du service et de la collecte (source, nombre
  de mesures en cache, période couverte, dernière collecte). C'est la cible du
  lien « Analyse avancée » affiché par MeteoHub lorsqu'il détecte le service.
  Page autonome, sans ressource externe : elle reste consultable sur un réseau
  local sans accès Internet.
- **Série temporelle générique `Series`** : les analyses ne manipulent que des
  canaux nommés, sans aucune notion de météo, afin que le moteur soit
  réutilisable par d'autres projets de l'écosystème.
- **Paramètres de module `source_url` et `altitude_m`** (voir
  `config/morfanalytics.example.json`).

### Modifié

- **Le cache n'est plus alimenté via morfSync.** L'appareil n'est pas client
  morfSync : l'enveloppe de synchronisation (UUID, révision, origine) pèserait
  plus que la mesure elle-même, écrite chaque minute sur un ESP32. Le service
  interroge donc directement l'API de MeteoHub en lecture seule ; morfSync est
  destiné à diffuser les **résultats d'analyse** à l'écosystème.

### Limitations connues

- Les **algorithmes d'analyse ne sont pas encore implémentés** : `POST /analyze`
  répond `not_implemented`. Cette version constitue la chaîne de collecte et son
  socle générique.
- La publication des résultats vers morfSync reste à écrire.

## [0.1.0] – Amorçage

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
