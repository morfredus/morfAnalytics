# Journal des versions - morfAnalytics

Le format s'inspire de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/)
et du [versionnage sémantique](https://semver.org/lang/fr/).

## [0.29.4] - 2026-08-18

### Corrigé

- **Compilation cassée sous un Qt 6 plus ancien** (Linux Mint) : le correctif
  d'avertissement de `0.29.3` employait `QTimeZone::UTC`, qui n'existe qu'à partir
  de **Qt 6.5** (`'UTC' is not a member of 'QTimeZone'`). `MeteoSyncPublisher`
  choisit désormais l'API selon la version via un garde `QT_VERSION` :
  `QTimeZone::UTC` sur Qt ≥ 6.5 (sans avertissement), `Qt::UTC` sur Qt < 6.5 (où
  cette surcharge n'est pas encore dépréciée). Compile proprement des deux côtés.

## [0.29.3] - 2026-08-17

### Corrigé

- **Avertissement de compilation supprimé** (Qt 6) : la surcharge `Qt::UTC` de
  `QDateTime::fromSecsSinceEpoch` est dépréciée ; remplacée par `QTimeZone::UTC`. Aucun
  changement de comportement, mais une compilation propre - un warning peut faire douter.

## [0.29.2] - 2026-08-17

### Corrigé

- **Les sections ne se referment plus toute seules.** Modifier un croisement dans
  **Analyses croisées** ou cliquer un réglage (focales, ISO...) rendait de nouveau la
  page et refermait la section qu'on était en train de consulter : il fallait recliquer
  sur la flèche pour la rouvrir. L'état déplié/replié de chaque section est désormais
  conservé d'un rendu à l'autre.

### Modifié

- **Analyses croisées** passe avant **Dossiers** dans l'ordre des sections.
- Derniers tirets cadratin de la page remplacés par le tiret simple.

## [0.29.1] - 2026-08-17

### Modifié

- **Exploration réorganisée en sections repliables**, pour une lecture progressive (la
  page devenait longue et dense). L'essentiel est ouvert d'emblée - **Vue d'ensemble**,
  **Quand** (années/mois), **Avec quoi** (matériel) - et l'avancé est replié :
  **Réglages** (focales, ISO, ouvertures, vitesses, type), **Dossiers**, **Analyses
  croisées** (matrice, comparaisons). Un non-initié voit d'abord l'essentiel et déplie le
  reste à la demande.

## [0.29.0] - 2026-08-17

### Ajouté

- **Onglets Exploration / Configuration** sur `/photo`. Le réglage rarement touché du
  matériel quitte la page d'analyse : l'exploration reste lisible, la configuration a sa
  place à part.
- **Filtre « Boîtiers possédés » (liste blanche).** Changement d'approche : au lieu
  d'exclure les boîtiers qu'on n'a pas, on **choisit explicitement son matériel** dans
  l'onglet Configuration, depuis la liste proposée par l'indexation. Aucune coche = tous
  les boîtiers ; sinon seules ces photos comptent (smartphone, boîtier emprunté, photos
  d'autres photographes… écartés ; les photos sans boîtier identifié passent toujours).
  Mémorisé et **enregistré dans les vues**.
- La liste des boîtiers est une **vraie liste de cases à cocher défilable** (deux
  colonnes), plus le `<select multiple>` qui « remontait » à chaque clic : cocher une case
  ne re-rend plus le panneau, le défilement reste en place.

### Modifié

- Le résumé de périmètre (onglet Exploration) affiche « N boîtiers possédés sur M » avec
  un lien direct vers Configuration. Le ∅ d'exclusion par barre est retiré (remplacé par
  le choix explicite du matériel).

## [0.28.2] - 2026-08-17

### Corrigé

- **Dédoublonnage qui amputait par excès.** Le dédup s'appliquait AUSSI à l'intérieur
  d'une même source, alors que morfPhoto garantit déjà des chemins uniques par poste :
  sur une base sans EXIF (dates nulles), des fichiers **distincts** partageant nom+taille
  se voyaient supprimés. Symptôme : « 15720 doublons écartés » sur une seule source.
  Le dédup est désormais **strictement inter-postes** (une empreinte revue depuis un
  AUTRE poste seulement est un doublon). Vérifié en réel : pi4fred seul → 15720 photos,
  **0 doublon**.
- **Postes en double dans la liste.** Le morfPhoto local apparaissait deux fois
  (`127.0.0.1` en config ET son nom via le beacon sur l'IP du LAN), et une machine
  multi-domiciliée autant de fois que d'interfaces. La liste est **dédoublonnée par
  machine** (une entrée, une URL — loopback préféré pour la locale). Le poste local est
  étiqueté **« <nom> (local) »** (ou « base locale ») au lieu de `127.0.0.1`.
- **Rafraîchissement aléatoire au changement de postes.** Cocher/décocher vite lançait
  des requêtes dont les réponses revenaient dans le désordre. Un garde de séquence ne rend
  plus que la réponse de la **dernière** requête émise.

## [0.28.1] - 2026-08-17

### Ajouté

- **Éditeur d'exclusion de boîtiers** sur `/photo` : une liste multi-sélection de tous
  les boîtiers présents (sélectionnés = exclus des stats), plus rapide que le ∅ des
  barres pour retirer d'un coup smartphones et boîtiers jamais possédés. Persisté comme
  périmètre courant et **enregistré dans les vues** (appliquer une vue réapplique ses
  exclusions).

### Modifié

- **Postes affichés par leur nom, plus par leur IP.** La ligne « Postes analysés » et les
  préfixes de dossiers fusionnés utilisent désormais le **hostname** annoncé par le
  beacon (pi4fred, macbooklinux…) plutôt que l'adresse IP, moins lisible. Repli sur
  l'hôte de l'URL si le nom n'est pas connu.

## [0.28.0] - 2026-08-17

### Ajouté

- **Analyse Photo MULTI-SOURCES.** La page `/photo` découvre les morfPhoto du parc par
  beacon (capacité `photo_index`, jamais par nom) et propose leurs postes en **cases à
  cocher** (pi4fred, pi4dev, macbooklinux, Windows…). On inclut **1, 2, 3+ postes** dans
  l'analyse ; changer la sélection **recharge en temps réel**, tous les filtres
  d'exploration restant présents. Les postes déclarés en config restent un filet ; le
  poste passé par PhotoHub (`?source=`) est pré-coché. Nouveau bloc `photo.discovery`
  (`enabled`, `udp_port`), endpoints `GET /photo/sources` et `GET /photo/data?sources=`.
- **Dédoublonnage inter-postes.** À la fusion, une photo indexée sur plusieurs postes
  (même CD gravé, dossier partagé) n'est **comptée qu'une fois**, via l'empreinte
  `fingerprint` du dataset (morfPhoto ≥ 0.7.0). Le nombre de doublons écartés et les
  postes injoignables sont affichés sous le sélecteur. Dictionnaires unifiés et dossiers
  préfixés du poste (« hôte · dossier ») pour ne jamais mélanger deux postes.
- **Vues de filtres enregistrées.** Mémoriser un jeu de filtres nommé et le réappliquer
  d'un clic (ex. « ma pratique réelle » qui exclut smartphones et boîtiers jamais
  possédés). Enregistrées **par valeur** (libellés, pas index) : une vue survit au
  changement de postes analysés. Enregistrer / appliquer / supprimer, persistées dans le
  navigateur.

## [0.27.0] - 2026-08-17

### Ajouté

- **Découverte automatique des morfMonitor par beacon.** Le domaine Monitor écoute
  désormais le beacon du parc et intègre seul tout morfMonitor qui s'annonce (capacité
  `system_monitor`, jamais par nom, morfMonitor ≥ 0.10.0). Une nouvelle machine est
  historisée sans aucune déclaration manuelle, exactement comme morfMonitor apprend les
  machines. Les sources déclarées en configuration restent un filet (machines sans
  beacon, périmètre figé) : les deux ensembles fusionnent. Nouveau bloc de config
  `monitor.discovery` (`enabled`, `udp_port`).
- **Oubli d'une machine (« Oublier cette machine »).** Suppression **définitive** d'une
  machine et de tout son historique (relevés machine et service, activités), par la page
  `/monitor` ou `POST /api/monitor/forget`. Geste explicite, réservé à une machine
  **déconnectée** (le bouton est inactif tant qu'elle est en ligne, sinon elle serait
  réintégrée aussitôt). Les données sont sinon **conservées** indéfiniment, machine
  connectée ou non.
- **`/status` du module monitor** expose l'état de découverte (`discovery` : écoute
  active, port, morfMonitor appris) en plus des sources déclarées.

## [0.26.3] - 2026-08-16

### Retiré

- **`scripts/linux/deploy-config.sh` supprimé, redondant.** Le déploiement de config
  est centralisé dans morfdeploy (`service.py config push --force`, ou `morf config
  deploy morfAnalytics`) : même effet (remplace la config déployée depuis le dépôt,
  sauvegarde horodatée, redémarrage), mais **multi-plateforme** (Linux et Windows) et
  sans script bash par projet. README (FR + EN) repointés. (Le script avait été ajouté
  en 0.26.2 ; l'équivalent centralisé existait déjà dans morfdeploy.)

## [0.26.2] - 2026-08-16

### Ajouté

- **Script `scripts/linux/deploy-config.sh`** (il manquait, comme pour morfPhoto). Il
  copie `config/morfanalytics.json` (ou l'exemple à défaut) vers
  `/etc/morfsystem/morfanalytics/morfanalytics.json`, en **sauvegardant** l'ancien
  fichier (`.bak-<date>`) et en **affichant le diff**, puis redémarre le service.
  Contrairement à `service.py update` (qui n'ajoute que les clés manquantes), il
  **remplace** la config déployée — la bonne commande après avoir changé une source
  (module `monitor`, `photo`, `analytics`). Options `--no-restart`, `--if-absent`,
  `--help` ; testable sans root via `MORF_SUDO`/`MORF_CONFIG_DIR`.

## [0.26.1] - 2026-08-16

### Corrigé

- **Monitor : slash de fin toléré dans une source.** `http://pi4fred:8790/` construisait
  `…8790//api/all` ; les slashes terminaux sont désormais retirés avant d'ajouter le
  chemin. La config d'exemple et réelle historise pi4fred et pi4dev par défaut.

## [0.26.0] - 2026-08-16

### Ajouté

- **Monitor — activités & compilations.** Le domaine gagne un modèle d'activité
  générique et la vue Builds, le cas d'usage central de la spec.
  - **Endpoint d'ingestion** `POST /api/monitor/activity` : un composant qui connaît
    son activité (compilation, indexation…) la signale ; elle est historisée dans une
    table `activity` **indépendante des samples** (elle survit à la purge du brut). On
    ne devine jamais une activité d'un pic CPU.
  - **Vue Builds** sur `/monitor` : compilations par projet (nombre, réussites/échecs,
    temps total, durée moyenne/min/max — sur les builds réussis pour ne pas fausser la
    moyenne), et liste des dernières activités avec le **coût système** mesuré sur la
    fenêtre exacte de chacune (`MonitorStore::windowStats`).
  - Correctif : les builds/activités sont renvoyés même quand aucune machine n'est
    encore historisée (ils ne dépendent pas des samples).

## [0.25.0] - 2026-08-15

### Ajouté

- **Monitor — vue « qui consomme quoi » (par service).** La page `/monitor` gagne une
  section Services : classement des services par CPU et par mémoire (moyenne sur la
  période) en barres, et tableau détaillé (CPU moy/max, RAM moy/max, nombre de relevés)
  par service. Les relevés par service étaient déjà collectés depuis 0.24.0 ; ils sont
  maintenant agrégés (`MonitorStore::serviceStats`) et exposés dans `/monitor/data`.
  C'est la partie que la spec appelle essentielle : passer de « la machine travaille » à
  « ce service coûte tant ».
- **Rétention des relevés bruts** (`retention_days`, défaut 90 ; `0` = illimité). Le
  module purge une fois par jour les relevés machine et service antérieurs à l'horizon,
  pour borner la base sur une machine modeste. Étape simple avant la compaction par
  paliers (rollups + mémoire remarquable) prévue ensuite. Exposée dans `/modules`.

## [0.24.0] - 2026-08-15

### Ajouté

- **Domaine Monitor — page `/monitor` (premier incrément).** morfAnalytics historise
  désormais les métriques des machines du parc remontées par morfMonitor et les
  représente dans le temps ; c'est le point où morfSystem acquiert une mémoire de son
  propre fonctionnement.
  - **Module `monitor`** : interroge périodiquement un ou plusieurs morfMonitor
    (`/api/all`), extrait métriques machine et par service, et les écrit dans un
    historique SQLite local. Frontière tenue : morfMonitor reste la sonde, ce module
    ne fait qu'échantillonner, stocker et représenter.
  - **`MonitorStore`** : schéma **hybride** (table large `sample_machine` +
    `sample_service`), déjà taillé pour les rollups/rétention/purge à venir. **NULL
    préservés** : une valeur absente n'est jamais un 0 (température sous Windows, source
    hors ligne…).
  - **Sous-échantillonnage côté serveur** : la série renvoyée par `/monitor/data` suit
    la période demandée (~300 points), jamais le brut ; les trous restent des trous.
  - **Page `/monitor`** : vue d'ensemble (CPU, RAM, température, charge, stockage,
    services actifs, uptime) + séries temporelles CPU / RAM / température / charge, avec
    sélecteur de machine et de période (1 h → 30 j), filtres persistants (localStorage),
    badge de version. Lien ajouté au portail. Configurée via le module `monitor`
    (`sources`, `interval_ms`, `db_path`).
  - Détail par service, activités (compilations…), baseline et anomalies : incréments
    suivants.

## [0.23.3] - 2026-08-14

### Modifié

- **Titre de la page MeteoHub conforme aux pages sœurs.** Le `<h1>` affichait
  « morfAnalytics » ; il devient **« Analyse de la météo »**, dans la même famille
  que « Analyse de la photothèque » et « Analyse des sites ». Le `<title>` d'onglet
  passe à « morfAnalytics - Météo » (aligné sur « morfAnalytics - Photo »).
- Tiret cadratin résiduel corrigé dans le `<title>` de la page SiteWatch
  (« — » -> « - »), conforme à la constitution.

## [0.23.2] - 2026-08-14

### Ajouté

- **Badge de version sur les pages Photo et SiteWatch**, comme sur la page MeteoHub.
  La page `/photo` lit `/status` côté navigateur et affiche `v<version>` à côté du
  titre ; la page `/sitewatch` (rendue côté serveur, sans JS) reçoit la version
  injectée directement via `morfanalytics::version()`. Les trois pages exposent
  désormais la version du service de façon cohérente.

## [0.23.1] - 2026-08-14

### Corrigé

- Resynchronisation de la copie vendorée de **morfBeacon**
  (`third_party/morf/beacon`) en 0.6.1, qui corrige la troncature des grandes réponses
  `/status` dans `StatusServer` (fermeture sans drainage du tampon d'écriture). Même
  classe de bug que le correctif déjà appliqué au `HttpServer` de morfAnalytics pour la
  page `/photo`. On attend désormais que `bytesToWrite()` retombe à zéro avant de fermer.

## [0.23.0] - 2026-08-14

### Ajouté

- **Moteur de filtres unifié et croisements analytiques.** La page Photo devient un
  vrai outil pour poser des questions au corpus, pas seulement réduire l'affichage.
  - **Nouvelles dimensions filtrables** : type de fichier, dossiers (libellés exposés
    par morfPhoto ≥ 0.5.1) et **période libre** (fenêtre d'années de … à …), qui
    s'ajoutent aux boîtiers, objectifs, années, mois, focales, ouvertures, ISO,
    vitesses. Toutes combinables (OR interne, AND entre dimensions), toutes cliquables
    depuis leur graphique.
  - **Filtres actifs restructurés** : le **périmètre de pratique** (boîtiers exclus,
    persistant) est affiché à part ; les **filtres analytiques** sont groupés par
    dimension, chaque valeur retirable individuellement ; « réinitialiser les filtres »
    n'efface plus le périmètre.
  - **Matrice analytique** : croisement libre Lignes × Colonnes (deux dimensions au
    choix) avec une mesure (nombre de photos, focale/ouverture/ISO/vitesse médiane).
    Répond à « quelles focales avec quel boîtier ? », « quels ISO par année ? »… sans
    multiplier les graphiques figés.
  - **Comparaison de groupes de filtres** : capturer le sous-ensemble courant comme
    Groupe A, puis un autre comme Groupe B, et comparer (photos, médianes, top focales,
    objectifs les plus utilisés). Plus puissant qu'un simple « boîtier A contre B ».
  - **Correctif serveur** : `reply()` vidait mal son tampon d'écriture ; les réponses
    dépassant ~20 Ko (la page Photo enrichie) étaient tronquées. Le corps est désormais
    entièrement drainé avant fermeture.

## [0.22.0] - 2026-08-14

### Ajouté

- **Filtres multi-critères** sur la page Photo. Chaque dimension accepte désormais
  PLUSIEURS valeurs : plusieurs boîtiers, plusieurs objectifs, plusieurs plages de
  focales, d'ISO, d'ouvertures, de vitesses, plusieurs années/mois. La logique est
  **OR à l'intérieur d'une dimension** (une photo passe si elle correspond à au moins
  un critère de cette dimension) et **AND entre dimensions**. Un clic ajoute un
  critère, un reclic le retire ; chaque valeur active a son propre chip, retirable
  individuellement, et « tout réinitialiser » les efface tous. Exemple : deux boîtiers
  + deux plages ISO restreint aux photos de l'un OU l'autre boîtier, à l'une OU l'autre
  sensibilité.

## [0.21.0] - 2026-08-14

### Ajouté

- **Analyses approfondies** sur la page Photo (Phase 2) :
  - **Tendances (médianes)** de focale, ouverture, ISO et vitesse, avec le nombre de
    valeurs connues.
  - **Focales — détail** : focales exactes (au mm) les plus fréquentes, qui révèlent
    les positions réellement utilisées d'un zoom sans les présupposer.
  - **Vitesses d'obturation** : distribution par plages, filtrable et croisable comme
    les autres dimensions.
  - **Boîtiers — chronologie** : années d'usage et période (première → dernière photo)
    par boîtier ; fait apparaître naturellement les changements de matériel.
  - **Objectifs** : boîtiers associés listés quand un objectif est filtré.
- **Comparaison de deux sous-ensembles** (par année, boîtier ou objectif) : colonnes
  A/B côte à côte (photos, médianes focale/ouverture/ISO/vitesse, top focales) pour
  visualiser des différences de pratique, dans le périmètre filtré courant.
- **Périmètre de pratique côté service** (`exclude_cameras`) : des boîtiers peuvent être
  exclus par défaut des analyses (matériel emprunté, fichiers importés d'autrui). La
  page fusionne ces exclusions de politique avec celles choisies dans le navigateur.
  Corpus ≠ pratique : la présence d'un fichier n'implique ni appartenance ni usage.
- **Passage de contexte depuis PhotoHub** : `GET /photo?source=<baseUrl morfPhoto>`
  ouvre les analyses de CETTE photothèque. Nouveau comportement de `GET /photo/data` :
  si `?source=` est fourni, morfAnalytics rapatrie cette instance à la demande (fetch
  synchrone borné) au lieu de sa source périodique configurée.

## [0.20.0] - 2026-08-14

### Ajouté

- **Page Photo transformée en interface d'exploration.** Jusqu'ici la page `/photo`
  n'affichait que quelques compteurs statiques rendus côté serveur. Elle devient une
  véritable exploration de la pratique photographique : on part d'une vue générale
  puis on descend dans les données via un **système de filtres croisés unique** qui
  pilote toute la page (année, mois, boîtier, objectif, focale, ISO, ouverture,
  combinables ; les graphiques eux-mêmes servent de filtres). Le calcul se fait
  désormais **côté navigateur**, sans dépendance externe.
- **Principe corpus ≠ pratique.** La présence d'un fichier n'implique ni appartenance
  ni usage personnel. Les boîtiers peuvent être **inclus ou exclus** pour délimiter le
  périmètre réel de sa pratique ; les exclusions sont **mémorisées localement**
  (localStorage). Exclure un boîtier est une interprétation : elle vit dans
  morfAnalytics, jamais dans morfPhoto (souverain).
- **Qualité des métadonnées rendue visible.** Chaque statistique affiche son
  dénominateur (« calculé sur 18 000 / 23 000 photos »), sans donner de précision
  artificielle à des données partielles.
- **Pull du nouvel export compact de morfPhoto** (`GET /api/v1/photos/dataset`,
  morfPhoto ≥ 0.5.0) : morfAnalytics rapatrie le jeu de données colonnaire et fait
  toute l'agrégation. Nouveau point d'entrée **`GET /photo/data`** (JSON) que la page
  récupère ; les données restent séparées de la présentation.

### Note

- Phase 1 du chantier « morfAnalytics Photo ». À venir : approfondissements par
  boîtier/objectif (timelines, focales réelles d'un zoom), vitesses d'obturation,
  comparaison de deux sous-ensembles, et passage de contexte depuis PhotoHub.

## [0.19.6] - 2026-08-14

### Corrigé

- README (FR/EN) : lien de la vue d'ensemble repointé vers
  `../morfSystem/docs/ARCHITECTURE.md`. L'ancien `MORFSYSTEM_ARCHITECTURE.md`
  (racine du parc) a été déplacé dans le dépôt morfSystem ; la référence était
  devenue un lien mort.

## [0.19.5] - 2026-08-14

### Documentation

- **README (FR/EN) : spécialisation Photo documentée.** L'espace `/photo`
  consommant **morfPhoto** (boîtiers, objectifs, focales regroupées, années) et
  son module de configuration `photo` (`source_url`) étaient absents des README
  depuis leur ajout en 0.19.0. FR : nouvelle entrée dans « Espaces Web » + liste
  des pages complétée ; EN : section « Photo library (morfPhoto) ».

## [0.19.4] - 2026-08-14

### Modifié

- **`config/morfanalytics.example.json`** : le `source_url` du module `photo`
  vaut désormais `http://127.0.0.1:8793` par défaut (morfPhoto sur la même
  machine, cas courant du parc) au lieu d'une chaîne vide. Une installation neuve
  affiche donc la page `/photo` sans étape de configuration manuelle ; mettre une
  autre IP si morfPhoto tourne ailleurs, ou vider pour désactiver la
  spécialisation Photo. Les configs déjà déployées dans `/etc` ne sont pas
  touchées (à ajuster à la main).

## [0.19.3] - 2026-08-14

### Corrigé

- Description de l'unité systemd : remplacement du tiret cadratin par un tiret
  simple, conformément à la règle de ponctuation du parc.

## [0.19.2] - 2026-08-14

### Modifié

- Resynchronisation de la copie vendorée de **morfBeacon**
  (`third_party/morf/beacon`) en 0.6.0, alignée sur le dépôt source
  (`IMetricsProvider.h`, `StatusServer.cpp`). Aucun changement de comportement.
- Ajout du marqueur de version manquant à la copie vendorée de **morfdeploy**
  (`third_party/morf/morfdeploy/VERSION` = 0.1.0) ; le code Python était déjà à
  jour. `morf doctor` de nouveau vert sur les copies vendorées.

## [0.19.1] - 2026-08-11

### Ajouté

- **Capacité morfBeacon `photo_analytics`** (ajout additif à `advanced_analysis`).
  Permet à PhotoHub de découvrir morfAnalytics par capacité et de proposer un lien
  vers la page `/photo`. Aucune dépendance forte : simple enrichissement optionnel.

## [0.19.0] - 2026-08-11

### Ajouté

- **Spécialisation Photo** : nouvelle page `/photo` et module `photo`. morfAnalytics
  lit les agrégats de morfPhoto (source de vérité de la photothèque) via son API
  `/api/v1`, n'émet que des GET, ne touche jamais au disque ni à sa base. Modèle
  « pull » comme la collecte Meteo : rafraîchissement périodique vers un instantané
  en mémoire, lu par la page sans bloquer le serveur.
- **L'interprétation vit ici** : regroupement des focales BRUTES en focales usuelles
  (`focal_buckets` configurable, jeu par défaut fourni), les valeurs brutes restant
  souveraines dans morfPhoto. La page affiche photos présentes, boîtiers, objectifs,
  répartition par année et focales regroupées.
- Entrée « Analyses Photo » ajoutée au portail racine ; type de module `photo` dans
  la fabrique ; configuration d'exemple (`source_url`, `refresh_ms`, `focal_buckets`).

## [0.18.2] - 2026-08-11

### Corrigé

- **L'interface web annoncée pointait sur `/meteohub` au lieu du portail racine.**
  morfAnalytics est un service multi-domaines (Meteo, SiteWatch, Photo à venir) ;
  son `web_ui` doit désigner la racine `/` qui liste les spécialisations, pas l'une
  d'elles. Un consommateur comme morfMonitor liait donc vers `:8799/meteohub` au
  lieu de `:8799`. Corrigé dans le point unique `fillAnnouncedDetail`
  (`SelfDescription.h`) : `webUiPath = "/"`.
- **Badge de version des README réaligné sur `VERSION`** (0.18.2) : il était resté
  à 0.17.0. `morf doctor` vérifie cette cohérence.

## [0.18.1] - 2026-08-01

### Fixed

- Les sources des pages compilées sont déplacées de `src/web/` vers `src/pages/`
  afin de ne plus être concernées par la règle historique d'exclusion `web/`
  lors des déploiements. La compilation distante retrouve ainsi tous les fichiers.

## [0.18.0] - 2026-08-01

### Changed

- Fondation du rendu Web modulaire : le portail, MeteoHub et SiteWatch possèdent
  désormais chacun leur unité de page compilée sous `src/web/`. `HttpServer` se
  limite progressivement au routage et à la fourniture des données.
- Cette structure est extensible : une future source, telle que GatewayLab,
  pourra ajouter sa page sans modifier les espaces existants.

## [0.17.0] - 2026-08-01

### Ajouté

- **Analyses SiteWatch approfondies.** morfAnalytics compare les synthèses
  successives d'un site, détecte les variations inhabituelles de trafic, les
  pics quotidiens, les nouveaux robots, la répétition des tentatives sensibles
  et l'évolution du taux d'erreurs HTTP.
- Une barre d'apprentissage indique clairement lorsqu'une seconde synthèse est
  encore nécessaire pour établir les comparaisons dans le temps.

## [0.16.0] - 2026-07-31

### Changed

- **Page SiteWatch rendue côté serveur.** `/sitewatch` lit maintenant les
  synthèses dans SQLite et produit directement le HTML des cartes. Elle ne
  dépend plus de JavaScript ni d'un appel asynchrone du navigateur.
- La page se recharge automatiquement toutes les 30 secondes pour intégrer les
  nouvelles synthèses sans intervention.

### Verified

- Test de bout en bout : publication SiteWatch, écriture SQLite puis rendu de
  l'URL, des compteurs et des pages sensibles dans `/sitewatch`.

## [0.15.2] - 2026-07-31

### Fixed

- Correction d'une erreur de syntaxe JavaScript dans la page `/sitewatch` qui
  empêchait entièrement son chargement et laissait « En attente de données »
  affiché, même lorsque SQLite contenait des synthèses valides.
- Le script de la page est désormais contrôlé syntaxiquement lors de la
  vérification locale du service.

## [0.15.1] - 2026-07-31

### Fixed

- `/sitewatch/reports` relit désormais directement SQLite à chaque demande. Les
  synthèses restent ainsi affichées après le redémarrage de morfAnalytics.
- Le rendu Web utilise une référence DOM explicite, ce qui évite le conflit avec
  la propriété navigateur `content` qui laissait la page sur son message d'attente.

## [0.15.0] - 2026-07-31

### Ajouté

- **Historique SiteWatch SQLite.** Chaque synthèse reçue est désormais ajoutée à
  `/opt/morfanalytics/cache/sitewatch-history.sqlite`, ce qui permet de bâtir des
  analyses sur une période étendue. Seules les synthèses calculées sont stockées :
  les journaux d'accès demeurent souverains dans SiteWatch.
- Migration automatique de la dernière synthèse autrefois conservée dans
  QSettings, sans perdre l'état déjà reçu.

## [0.14.3] - 2026-07-31

### Fixed

- La limite de réception des rapports SiteWatch est portée à 1 Mio. Les rapports
  enrichis d'une version déjà installée de SiteWatch ne sont donc plus rejetés
  lorsque leurs listes de pages ou de robots sont importantes.

### Ajouté

- L'espace SiteWatch signale les journées de pic d'erreurs 404, de robots et de
  tentatives sensibles, en complément des classements principaux.

## [0.14.2] - 2026-07-31

### Fixed

- La page `/sitewatch` actualise désormais automatiquement les synthèses reçues.
  Elle ne reste plus bloquée sur « En attente de données » lorsque SiteWatch
  publie son rapport juste après l'ouverture de la page.

### Ajouté

- Les synthèses SiteWatch mettent en évidence le taux d'erreurs HTTP, les pages
  les plus consultées ou touchées, ainsi que les robots les plus actifs.

## [0.14.1] - 2026-07-31

### Fixed

- Réception SiteWatch compatible avec la publication JSON explicite de
  SiteWatch 1.11.1.

## [0.14.0] - 2026-07-31

### Modifié

- **Intégration SiteWatch finalisée.** SiteWatch découvre morfAnalytics sur le
  réseau, lui transmet automatiquement sa synthèse après chaque analyse et
  ouvre sa page dédiée. Le portail liste les deux espaces disponibles et la
  date de la dernière synthèse SiteWatch reçue.

## [0.13.0] - 2026-07-31

### Ajouté

- **Espace SiteWatch.** La page `/sitewatch` reçoit les synthèses publiées par
  SiteWatch et présente, par site, les requêtes analysées, erreurs HTTP, robots,
  tentatives sensibles et une conclusion lisible. Les synthèses sont conservées
  localement après le redémarrage du service.

### Modifié

- **Entrées Web séparées.** La météo est désormais accessible par `/meteohub` ;
  la racine présente les analyses disponibles. L'annonce réseau dirige MeteoHub
  directement vers son espace dédié.

## [0.12.0] - 2026-07-31

### Modifié

- **Page d'analyse réorganisée pour la lecture.** Elle présente désormais la
  situation actuelle, les conditions locales, l'historique et les repères, puis
  les analyses approfondies. Les informations de service et les outils de
  maintenance n'interrompent plus la consultation météo ; ils restent
  accessibles dans leurs sections repliées. Les cartes sont disposées par rangées
  cohérentes afin d'éviter les vides qui rendaient la page difficile à parcourir.
- **Analyses avancées rendues lisibles.** Les noms internes de l'API ne sont plus
  présentés comme des libellés utilisateur ; les seuils et détails de diagnostic
  sont expliqués et repliables. Les relevés atypiques sont résumés plutôt que
  présentés minute par minute.
- **Cycle journalier moyen gradué.** La courbe affiche maintenant sa température
  minimale, sa température maximale et les heures 0 h, 12 h et 23 h ; sa forme
  peut ainsi être interprétée directement.

## [0.11.0] - 2026-07-29

### Ajouté

- **Décomposition tendance / saison (`decomposition`)**, qui achève la vague 3.
  Décompose un canal en **tendance** (moyenne mobile centrée sur 24 h),
  **composante saisonnière** journalière (profil horaire moyen, centré) et
  **résidu**. Sortie synthétique : pente de tendance par jour, amplitude
  saisonnière et profil 24 h, écart-type du résidu, et **force** de la tendance et
  de la saison (diagnostics à la STL, `1 - Var(résidu)/Var(composante+résidu)`).
  Distincte de `daily_cycle` : elle isole la tendance de fond du cycle journalier.
  Couverte par `test/analyses_test.cpp`.

## [0.10.0] - 2026-07-29

### Ajouté

- **Vague 3 d'analyses (avancées).** Trois nouvelles analyses enfichables, groupe
  `avancé`, toutes à sortie synthétique :
  - **`anomalies`** : détection par **z-score robuste (MAD)**, insensible aux
    valeurs aberrantes qu'elle cherche justement (médiane + écart absolu médian,
    score d'Iglewicz-Hoaglin). Ne signale que les points les plus extrêmes,
    plafonnés, avec médiane/MAD/comptage.
  - **`correlations`** : corrélations à **décalage temporel** entre grandeurs
    (sur grille horaire), pour repérer qu'une grandeur en **précède** une autre ;
    renvoie le décalage qui maximise la corrélation par paire.
  - **`episodes`** : **segmentation d'épisodes** (canicule, coup de froid) -
    suites de jours consécutifs au-delà d'un seuil, avec durée et valeur extrême.
  - Couvertes par `test/analyses_test.cpp` (données synthétiques à propriétés
    connues : pics injectés, temp/hum anticorrélées, canicule prolongée).

## [0.9.1] - 2026-07-29

### Ajouté

- **Tests unitaires des formules météo (`MeteoMath`).** `MeteoMath` est la seule
  partie du moteur où une erreur est silencieuse (un résultat faux reste
  plausible) : `test/meteomath_test.cpp` vérifie point de rosée, humidité absolue,
  humidex, déficit de pression de vapeur et pression réduite au niveau de la mer
  contre des **valeurs de référence** issues de tables météorologiques, plus des
  **invariants** robustes (saturation, monotonie, altitude nulle, seuil humidex).
  Désactivés par défaut ; activer avec `-DMA_BUILD_TESTS=ON` puis `ctest`. Les 16
  vérifications passent (l'implémentation colle aux références à moins de 0,1
  d'écart typique).

## [0.9.0] - 2026-07-29

### Ajouté

- **Publication des synthèses journalières vers morfSync.** Les résultats
  d'analyse n'étaient jusqu'ici consultables que sur ce service. morfAnalytics
  publie désormais **une synthèse par jour** (min/max/moyenne + première/dernière
  valeur par canal) dans le journal morfSync `meteohub`, ce qui les rend lisibles
  par le reste du parc (morfDashboard, SiteWatch...). Écriture SEULE, à sens
  unique : jamais vers l'appareil (la source reste souveraine), morfSync n'est
  qu'un aval. Un jour = un enregistrement d'identité stable (`meteohub-<AAAAMMJJ>`)
  dont la révision est le nombre d'échantillons du jour : elle croît quand la
  journée se remplit, ce qui rend la publication naturellement **idempotente** (un
  jour inchangé n'est pas republié, et le hub reconnaît un `(id, rev)` déjà vu).
  La série temporelle brute reste dans le cache SQLite ; le journal JSON ne porte
  que le résultat (~365 enregistrements/an), il ne devient jamais un entrepôt de
  mesures.
  - Nouveau paramètre de module `morfsync_url` (+ `morfsync_token` facultatif) :
    vide, aucune publication n'est faite. Voir `config/morfanalytics.example.json`.
  - Nouvelle classe `MeteoSyncPublisher` (`src/publish/`), lecture par jour
    `SampleStore::rangeForDay()`, statut exposé sous `publisher` dans `/status`.

## [0.8.0] - 2026-07-28

### Modifié

- **Cache déplacé sous `/var/lib/morfsystem/morfanalytics`** (doctrine du parc,
  voir `docs/fr/FILESYSTEM.md`). Le cache SQLite des échantillons est de l'**état
  persistant** généré par le service : quand `cache_dir` n'est pas fixé, il ne
  retombe plus sur le dossier courant (`/opt/morfanalytics`) mais sur l'état,
  distinct du programme (`/opt`) et de la config admin (`/etc`). L'unité systemd
  déclare `StateDirectory=morfsystem/morfanalytics`, que systemd crée possédé par
  l'utilisateur du service et expose via `$STATE_DIRECTORY`. Le paramètre
  `cache_dir` d'un module reste prioritaire.
  - **Migration** : déplacer un cache existant (`meteohub-cache.sqlite`) depuis
    `/opt/morfanalytics` vers `/var/lib/morfsystem/morfanalytics` avant de
    démarrer, ou fixer `cache_dir` sur l'ancien emplacement.

## [0.7.0] - 2026-07-28

### Modifié

- **Configuration regroupée sous `/etc/morfsystem/<service>`.** Tout le parc
  partage désormais un point d'entrée UNIQUE dans `/etc` (`/etc/morfsystem/`),
  qui contient le fichier partagé `morfsystem.json` et un sous-dossier par
  service, au lieu d'un `/etc/<service>` par service à la racine de `/etc`. Sous
  Windows : `%ProgramData%\morfsystem\<service>`. Les données restent sous
  `/opt/<service>`. L'ancien `/etc/<service>` est adopté à l'installation
  (`migrate_from`).

## [0.6.5] - 2026-07-27

### Ajouté

- **`/status` déclare l'API métier** (`GET /analyses`, `POST /analyze`,
  `POST /data/cleanup`), en plus de l'interface web déjà annoncée. Un observateur
  découvre ainsi non seulement que morfAnalytics existe et où l'ouvrir, mais quoi
  l'appeler - le dernier élément du principe « chaque service annonce ce qu'il
  sait faire ET comment on l'appelle » (morfBeacon 0.5.0).

### Modifié

- **Le détail annoncé (interface web + API) est défini en un seul point**
  (`SelfDescription.h`, `fillAnnouncedDetail`), appelé par le heartbeat
  (`Service`) **et** par `/status` (`HttpServer`, via `describeService` de
  morfBeacon). Le bloc `web_ui` était jusqu'ici écrit **à la main aux deux
  endroits**, mêmes valeurs recopiées : la moindre modification pouvait diverger.
  Un observateur voyait potentiellement une interface annoncée par le heartbeat
  et décrite différemment par `/status`.

### Vérifié

`/status` d'un morfAnalytics réel émet `web_ui` et `api` identiques à ce que le
heartbeat annonce, depuis la source unique.

## [0.6.4] - 2026-07-26

### Modifié

- **Page d'analyses réorganisée autour de l'interprétation.** Une synthèse de
  la situation locale ouvre désormais la page (température, tendances, alertes
  locales et prévision). Les analyses qui construisent encore leur historique
  affichent une progression « En apprentissage » avec les jours déjà collectés
  et le seuil restant, au lieu de ressembler à une erreur. Les opérations sur le
  cache sont regroupées sous « Maintenance avancée », repliée par défaut.

## [0.6.3] - 2026-07-26

### Ajouté

- **Chaleur et humidex.** Niveau de chaleur locale déduit de la température et
  de l'humidité, avec humidex lisible dans le cartouche.
- **Sécheresse atmosphérique.** Déficit de pression de vapeur (VPD), qui mesure
  le pouvoir asséchant de l'air. Il est explicitement présenté comme un
  indicateur local et non comme un danger officiel de feu : pluie, vent et état
  de la végétation ne sont pas mesurés par la station.

## [0.6.2] - 2026-07-25

### Corrigé

- **Valeurs longues dans les cartouches d'analyses.** Elles restent alignées à
  droite sans couper les mots. Lorsqu'une valeur ne peut pas cohabiter avec son
  libellé - par exemple une tendance descriptive - elle est placée sur la ligne
  suivante, toujours alignée à droite, sans chevauchement.

## [0.6.1] - 2026-07-25

### Corrigé

- **Cartouches de résultats de la page d'analyses.** Les valeurs longues, dont
  une tendance de température descriptive, reviennent désormais à la ligne dans
  leur cartouche au lieu de déborder de celui-ci.

## [0.6.0] - 2026-07-23

### Ajouté

- **Nettoyage du cache (`POST /data/cleanup`) et section « Gestion du cache »
  sur la page.** Trois opérations, qui n'agissent **que sur la copie locale** -
  les mesures d'origine, sur l'appareil, ne sont jamais touchées, le collecteur
  n'émettant que des GET :
  - *Mesures aberrantes* : recherche puis neutralisation des relevés portant une
    pression physiquement impossible (capteur en panne : `0 hPa`, `0 °C`). La
    ligne entière est neutralisée, ces zéros n'étant pas des mesures.
  - *Neutralisation d'une plage* : les valeurs d'une période (et éventuellement
    d'un seul canal) sont marquées manquantes, avec comptage préalable
    (`dry_run`).
  - *Purge totale* : cache vidé (mesures + curseurs), reconstruit intégralement
    depuis l'appareil au cycle de collecte suivant.

  Le nettoyage partiel **neutralise** (valeurs mises à `NULL`) au lieu de
  supprimer les lignes : la reprise de collecte se déduit de `MAX(idx)` par
  jour, et des lignes supprimées seraient re-téléchargées depuis l'ESP32 au
  cycle suivant. Une ligne neutralisée reste en place, et l'import en
  `OR IGNORE` ne la réécrit jamais.

- **Filtre de plausibilité à l'import.** Le collecteur marque manquante toute
  valeur physiquement impossible (température hors ±60 °C, humidité hors
  0-100 %, pression hors 300-1200 hPa) ; une pression hors bornes invalide le
  relevé entier, signature d'un capteur hors service. Les pannes futures
  n'entrent donc plus dans le cache - le nettoyage manuel rattrape l'historique
  déjà importé.

- **Analyse « Tendance de température » (`temp_trend`).** Variations sur 1 h et
  3 h, écart avec la veille à la même heure (pour séparer un changement de
  masse d'air du cycle jour/nuit), et libellé de tendance. Pendant côté
  thermomètre de la tendance barométrique.

### Modifié

- **Page d'analyses.** Heure de la mesure affichée sous les conditions
  actuelles ; heures restantes avant l'aube sur le risque de gelée ; période
  couverte sous les records ; horodatage « Analyses actualisées » et bouton
  *Actualiser* dans l'en-tête.

### Corrigé

- **`daily_cycle.days_counted` annonçait la largeur de la fenêtre demandée**,
  pas le nombre de journées réellement observées : sur un historique plus court
  que la fenêtre, les deux divergent. Le compte porte désormais sur les jours
  effectivement vus.

## [0.5.1] - 2026-07-21

### Corrigé

- **Le cache de travail était versionné.** `cache/meteohub-cache.sqlite` et ses
  fichiers `-shm` / `-wal` étaient suivis par Git. SQLite réécrivant ces deux
  derniers à chaque ouverture, le dépôt affichait un fichier modifié **à chaque
  exécution du service** - un bruit permanent sans aucune information.

  Plus gênant : un clone neuf héritait du cache d'une autre machine au lieu de
  partir vide. Cela contredit le principe du projet, qui pose que morfAnalytics
  ne possède jamais la vérité des données et travaille sur une copie
  reconstructible depuis l'équipement.

  Les fichiers restent sur disque, ne sont plus suivis, et `cache/` est ignoré.
  Le service crée le dossier au démarrage (`QDir::mkpath`) : vérifié en
  démarrant depuis un emplacement sans cache - dossier et base créés seuls,
  `/status` répond.

## [0.5.0] - 2026-07-21

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

## [0.6.6] - 2026-07-27
### Ajouté

- **La version s'affiche dans l'en-tête de la page** (pastille à côté du titre),
  en plus de la carte Service, pour la lire d'un coup d'œil comme dans les
  autres services du parc.

### Modifié

- **Plus aucun tiret cadratin dans l'interface** : tous remplacés par le trait
  d'union simple, conformément à la convention de rédaction du parc.


## [0.5.3] - 2026-07-22

### Corrigé

- **Un module qui ne démarre pas se voit dans le journal.** L'échec d'ouverture
  du cache SQLite restait muet : le service affichait « 1 module(s) », l'interface
  renvoyait vers `source_url`, et la vraie cause - un dossier `/opt` possédé par
  root où le cache était incréable - ne figurait nulle part. Le module logue
  désormais le chemin et la raison (`qCritical`), et le registre signale tout
  `start()` en échec. L'enquête d'aujourd'hui aurait tenu en une ligne de
  `journalctl`.
## [0.5.2] - 2026-07-22
### Modifié

- **Installation, mise à jour et désinstallation par `./service.py`** - point
  d'entrée unique multiplateforme (morfdeploy), en remplacement des scripts
  `install-service.sh`/`.ps1`. Le binaire de ce service est inchangé ; seul son
  mode de déploiement évolue.
- **La configuration vit désormais dans `/etc/morfanalytics`** (convention Linux),
  séparée du binaire dans `/opt/morfanalytics`. Le déplacement est déclaré : la config
  existante est adoptée, jamais écrasée.
- **Enrichissement à la mise à jour** : une clé introduite par une nouvelle
  version est ajoutée avec sa valeur par défaut, sans jamais toucher vos réglages.

## [0.4.1] - 2026-07-20

### Corrigé

- **`docs/fr/ARCHITECTURE.md` décrivait le template et non morfAnalytics** :
  le schéma citait `ExampleModule` (inexistant ici) et les routes HTTP étaient
  données « à titre d'exemple ». Le document décrit désormais `AnalyticsModule`
  (type `analytics`) et les routes réelles `GET /analyses` et `POST /analyze`.

## [0.4.0] - 2026-07-19

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
  utilisée est celle de la source collectée (`source_url`) - la seule que ce
  service connaisse.

### Compatibilité

- Un MeteoHub à partir de la version 1.12.0 ne détecte que les services
  annonçant `advanced_analysis` : les versions de morfAnalytics antérieures à
  0.4.0 ne sont plus découvertes automatiquement. Elles restent joignables en
  saisissant leur adresse dans la page Système de MeteoHub.

## [0.3.3] - 2026-07-19

### Corrigé

- **La mise à jour ne livrait jamais les nouveaux paramètres de configuration.**
  `update-service.sh` ne recopiait que le binaire et laissait
  `/opt/morfanalytics/morfanalytics.json` intact, par souci de préserver les
  réglages locaux. Conséquence : les paramètres apparus depuis l'installation -
  `source_url` et `altitude_m`, introduits en 0.2.0 et 0.3.x - restaient absents
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

## [0.3.2] - 2026-07-19

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

## [0.3.1] - 2026-07-19

### Corrigé

- **Une altitude nulle n'est plus confondue avec une altitude non renseignée.**
  L'absence du paramètre était détectée en testant `altitude_m ≈ 0`, si bien
  qu'une station réellement au niveau de la mer - une valeur parfaitement
  légitime - était signalée à tort comme mal configurée dans les analyses de
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

## [0.3.0] - 2026-07-19

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
- **Six analyses climatologiques** - celles que l'appareil ne peut pas calculer
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

## [0.2.0] - 2026-07-19

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

## [0.1.0] - Amorçage

### Ajouté

- **Amorçage de morfAnalytics** à partir de `morfTemplateService` : le moteur
  d'analyse de l'écosystème morfSystem. Service prêt à tourner (API HTTP, config
  JSON, annonce LAN via morfBeacon sous l'app `morfAnalytics` sur le port 45454).
- **Module `analytics`** (`AnalyticsModule`, remplace le module d'exemple) :
  squelette du moteur d'analyse - maintient un *cache de travail* (copie
  synchronisée en lecture seule, via morfSync) et exécute les analyses **à la
  demande** (`analyze()`). Les algorithmes réels (tendances, corrélations,
  anomalies…) et la synchro du cache sont des `TODO`.
- **Route `POST /analyze`** (stub) en remplacement de la route de démonstration.

### Principes (voir `../MORFSYSTEM_ARCHITECTURE.md`)

- morfAnalytics ne possède jamais la vérité des données ; MeteoHub (et les autres
  équipements) restent la source de vérité. L'équipement écrit, morfAnalytics lit.
- Présence optionnelle : aucune dépendance pour le fonctionnement nominal des
  équipements.

## [0.1.0] - 2026-07-16

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
