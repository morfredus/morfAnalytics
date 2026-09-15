# Interface web

morfAnalytics sert ses analyses dans une interface web, à ouvrir dans un
navigateur sur son port HTTP (`http://<hôte>:8799`). Chaque domaine d'analyse a
sa page ; le portail les liste.

morfAnalytics ne possède aucune donnée : il lit une **copie** de ce que les
équipements et services du parc exposent (l'équipement reste la source de vérité),
puis l'agrège, l'historise et la représente.

> Les captures ci-dessous utilisent des **données d'exemple anonymisées** :
> valeurs, hôtes, dépôts et photothèque sont fictifs et ne servent qu'à illustrer
> l'interface.

## Portail

Point d'entrée : la liste des analyses disponibles (météo, journaux web, GitHub,
photothèque, machines du parc).

![Portail morfAnalytics (données d'exemple)](pictures/interface-portail.png)

## Météo

Le domaine météo (données recopiées depuis MeteoHub) a **deux onglets**, reliés par
une barre en haut de page :

- **Analyses** - « qu'est-ce que ça veut dire ? » : une carte par analyse (prévision
  locale, tendances, chaleur/humidex, sécheresse, brouillard, gelée, normales, cycle
  journalier, records, confort intérieur, comportement thermique du bâtiment, modèle
  d'inertie, prévu vs observé, complétude…). Chaque analyse a un **contexte** propre
  (Extérieur pour la météo, Intérieur pour le confort, les Deux pour les relations),
  rappelé par un badge ; un sélecteur global permet de forcer le contexte. Deux
  boutons : **Collecter et actualiser** (lance une vraie collecte depuis l'appareil)
  et **Rafraîchir l'affichage** (recalcule sur le cache déjà collecté). L'en-tête et
  les filtres restent **collants** en haut de la page : accessibles quel que soit le
  défilement, et changer un filtre ne renvoie plus tout en haut.

- **Graphiques** - « montre-moi ce qui s'est réellement passé » : l'évolution d'une
  grandeur (ou de **toutes**) dans le temps, source **Intérieur / Extérieur / les
  deux** sur le même axe de temps, période de 6 h à 30 j. Échelles de valeurs à
  gauche et à droite (dynamiques), infobulle au survol donnant la valeur de chaque
  courbe à l'instant pointé. Les points sont reliés (une coupure = un vrai silence du
  capteur, jamais un simple espacement de cadence). En-tête et filtres **collants**,
  comme la page Analyse.
  - **Événements détectés** : sous le graphique, un encart chronologique. Les
    événements sont calculés côté serveur par une **source commune** (`MeteoEvents`),
    partagée avec la page Analyse et l'endpoint `/meteohub/events` (jamais recalculés
    différemment). Trois familles :
    - **Croisement de valeurs** : quand l'Intérieur ET l'Extérieur d'une même grandeur
      sont tracés, l'instant où les deux deviennent égales (changement de signe de
      `D(t) = Extérieur(t) - Intérieur(t)`). Heure et valeur interpolées entre les
      deux mesures encadrantes (une estimation, pas une mesure), jamais au travers
      d'un vrai trou ; une bande morte par grandeur écarte le bruit. Matérialisé par
      un guide vertical et un losange à la valeur estimée.
    - **Changement de régime** : plusieurs tendances qui basculent dans une fenêtre
      rapprochée, **même entre grandeurs de natures différentes** (relation TEMPORELLE,
      jamais déduite d'une proximité graphique). Matérialisé par un repère vertical.
    - Chaque marqueur porte un numéro qui le relie à sa ligne dans l'encart.
    Aucun croisement n'est jamais généré entre grandeurs différentes. C'est une
    première timeline analytique tirée directement des relations entre séries.

## Analyse des machines

L'historique du parc dans le temps, à partir des relevés de morfMonitor. morfMonitor
dit « maintenant » ; morfAnalytics regarde la **durée** : vue d'ensemble, séries
CPU / mémoire / température / charge (les trous d'une source hors ligne restent
visibles, jamais comblés par des zéros), consommation par service, et l'historique
des activités et compilations.

![Analyse des machines (données d'exemple)](pictures/interface-machines.png)

## Analyses GitHub

Mémoire des métriques publiées par SiteWatch : vues, clones, téléchargements,
évolution quotidienne et classement des dépôts.

![Analyses GitHub (données d'exemple)](pictures/interface-github.png)

## Analyse de la photothèque

Une vraie interface d'exploration du corpus (morfPhoto reste la source) : croiser
boîtiers, focales, ISO, ouvertures, vitesses et périodes. Vue d'ensemble et
médianes, répartition temporelle (par année / par mois), matériel (boîtiers,
objectifs, chronologie), réglages, analyses croisées et dossiers.

![Analyse de la photothèque (données d'exemple)](pictures/interface-photo.png)
