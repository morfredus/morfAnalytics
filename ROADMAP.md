# Roadmap — morfAnalytics

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

- **Publication des résultats vers morfSync.** Les analyses restent aujourd'hui
  cantonnées à ce service. Les publier dans le journal `meteohub` les rendrait
  consultables par le reste de l'écosystème (morfDashboard, SiteWatch…).
- **Tests unitaires des formules météo.** `MeteoMath` est la seule partie où une
  erreur passe inaperçue : un point de rosée faux ressemble à un point de rosée
  juste. Des valeurs de référence issues de tables météorologiques seraient la
  seule vraie garantie.
- **Vague 3 d'analyses** — corrélations avec décalage temporel entre grandeurs,
  décomposition tendance/saisonnalité, détection d'anomalies par z-score robuste
  (MAD), segmentation automatique d'épisodes (canicule, coup de froid).

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
