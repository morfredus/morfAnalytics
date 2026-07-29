# Roadmap - morfAnalytics

morfAnalytics est le **moteur d'analyse** de l'écosystème morfSystem. Il déporte
sur un serveur (typiquement un Raspberry Pi) les traitements que les équipements
embarqués ne peuvent pas assumer, faute de puissance ou de profondeur
d'historique.

Deux principes cadrent tout ce qui suit :

- **La vérité des données reste sur l'équipement.** morfAnalytics travaille sur
  une copie locale en lecture seule. Rien de ce qui est listé ici ne doit le
  conduire à écrire vers un appareil.
- **Le moteur ignore le métier.** Les analyses ne manipulent qu'une série
  temporelle à canaux nommés. Les analyses météo sont un jeu enfichable parmi
  d'autres ; ce qui les concerne ne remonte pas dans le moteur.

## Prochaines étapes

- ~~**Publication des résultats vers morfSync.**~~ FAIT (v0.9.0) : une synthèse
  par jour est publiée dans le journal `meteohub` (id stable `meteohub-<AAAAMMJJ>`,
  révision = nombre d'échantillons du jour, idempotent). Écriture seule, à sens
  unique. Reste envisageable plus loin : publier aussi des résultats d'analyse de
  plus haut niveau (tendances, épisodes) une fois les vagues suivantes écrites.
- ~~**Tests unitaires des formules météo.**~~ FAIT (v0.9.1) : `test/meteomath_test.cpp`
  vérifie `MeteoMath` contre des valeurs de référence issues de tables
  météorologiques + des invariants (saturation, monotonie, altitude nulle).
- **Vague 3 d'analyses** - en grande partie FAITE (v0.10.0) : corrélations à
  décalage temporel entre grandeurs (`correlations`), détection d'anomalies par
  z-score robuste MAD (`anomalies`), segmentation d'épisodes canicule/coup de
  froid (`episodes`), toutes couvertes par `test/analyses_test.cpp`. **Reste** la
  décomposition tendance/saisonnalité, plus lourde et à cadrer.

## Envisagé

- **Second équipement suivi.** Le cache et le moteur sont déjà génériques ; il
  manque la gestion de plusieurs sources et la comparaison entre appareils.
- **Rapports périodiques** (synthèse mensuelle, bilan de saison), exportables.
- **Authentification par jeton** du serveur HTTP, pour une exposition hors du
  réseau local.
- **Rechargement de configuration** sans redémarrage (SIGHUP).

## Non-objectifs

- **Devenir une base de données.** Le cache est une copie de travail jetable,
  reconstructible depuis l'équipement. Il n'a pas vocation à devenir un stockage
  de référence.
- **Renvoyer des flots de mesures.** Une analyse produit un résultat synthétique.
  Rapatrier des milliers de points est le travail de l'API d'historique de
  l'équipement.
- **Devenir indispensable.** Les équipements doivent continuer à mesurer,
  stocker, tracer et exporter sans ce service. Sa présence reste optionnelle.
