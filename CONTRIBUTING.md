# Contribuer à morfAnalytics

morfAnalytics est le moteur d'analyse de l'écosystème morfSystem. Il lit une
copie locale des mesures d'un équipement et en tire des résultats synthétiques.

## 1. Philosophie

- **L'équipement possède la vérité, pas ce service.** Le collecteur n'émet que
  des requêtes `GET` : par construction, morfAnalytics ne peut rien modifier sur
  un appareil. Aucune contribution ne doit ouvrir cette porte.
- **Le cache est jetable.** C'est une copie de travail, supprimable et
  reconstructible sans perte. Rien ne doit y être stocké qui n'existe pas déjà
  sur l'équipement.
- **Le moteur ignore le métier.** `IAnalysis` et `Series` ne connaissent que des
  canaux nommés. Une notion météo (pression, rosée, saison) n'a rien à faire
  dans le moteur ni dans le cache — elle vit dans les analyses.
- **Une analyse rend un résultat, pas des données.** Tendance, score, classement,
  quelques valeurs. Jamais un flot de mesures.
- **Présence optionnelle.** Les équipements fonctionnent sans ce service ; seules
  les analyses avancées deviennent indisponibles.
- **Qt Core + Network + Sql uniquement.** morfBeacon reste vendoré. Portable
  Windows / Linux x64 / Raspberry Pi.

## 2. Ajouter une analyse

Les analyses météo sont enregistrées dans `src/analysis/MeteoAnalyses.cpp`. En
ajouter une tient en deux gestes :

1. écrire une fonction `QJsonObject(const AnalysisContext&, const QJsonObject&)` ;
2. la déclarer dans `registerMeteoAnalyses()` avec son identifiant, son titre,
   son groupe et la profondeur d'historique minimale qu'elle exige.

Elle apparaît alors automatiquement dans `GET /analyses` et dans la page web,
sans toucher ni au moteur ni à l'interface. Un rendu dédié dans la page (objet
`RENDERERS` de `HttpServer::landingPage()`) est facultatif : sans lui, un rendu
générique lisible s'applique.

Deux exigences pour toute nouvelle analyse :

- **Déclarer honnêtement sa profondeur minimale.** Une moyenne calculée sur trois
  mesures se présente comme un résultat valide, ce qui est plus trompeur qu'une
  indisponibilité annoncée.
- **Assumer ses limites dans le résultat.** Les mesures se réduisent à trois
  grandeurs, sans vent ni ensoleillement. Une analyse qui extrapole doit le dire
  dans son champ `note`, affiché tel quel dans la page.

## 3. Modifier les formules

`src/analysis/MeteoMath.cpp` regroupe les formules météorologiques en fonctions
**pures**, sans état ni accès au cache. C'est délibéré : ce sont les seules
parties du code où une erreur reste plausible à l'œil — un point de rosée faux
ressemble à un point de rosée juste. Toute modification doit être vérifiée contre
des valeurs de référence, jamais contre l'intuition.

## 4. Style

- C++17, en-tête de licence SPDX, namespace `morfanalytics`.
- Commentaires en français expliquant le **pourquoi**, pas le quoi. Un commentaire
  qui paraphrase la ligne suivante est du bruit ; un commentaire qui explique le
  piège évité vaut de l'or.
- Fins de ligne : voir `.gitattributes` (LF dans le dépôt ; `.ps1` en CRLF).

## 5. Vérifier avant de proposer

```sh
cmake --preset linux && cmake --build --preset linux
./build/service/morfanalytics --config config/morfanalytics.example.json
curl http://127.0.0.1:8799/analyses
```

Les pièges rencontrés côté équipement (ordre des routes, taille des réponses,
parcours de carte SD) ne se voient **jamais** à la compilation. Toute
modification touchant au dialogue avec un appareil doit être vérifiée contre du
matériel réel avant d'être proposée.
