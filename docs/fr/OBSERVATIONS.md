# Observations météo (annotations humaines)

Cette page décrit la couche d'**observations** de morfAnalytics Meteo : des notes
saisies par l'utilisateur pour consigner ce qu'il a *vu*, rattachées à une
période, à côté des mesures que la station enregistre.

C'est la **Phase 1** d'une fonctionnalité plus large (détection d'épisodes
météorologiques). Ici, uniquement la saisie et la conservation des observations ;
aucune détection automatique.

## Pourquoi cette couche

La station MeteoHub mesure trois grandeurs : température, humidité, pression. Elle
ne mesure ni le vent, ni la pluie, ni la grêle, ni les éclairs. Or ce sont
souvent ces phénomènes qui font l'épisode. Une observation humaine comble ce que
les capteurs ne voient pas.

Deux principes en découlent :

- **Une observation n'est pas une mesure.** On ne la fait pas entrer dans les
  canaux temp/hum/pres : c'est un événement distinct, rattaché à une période.
- **Une observation est une donnée originale.** Contrairement au cache des
  mesures (une copie reconstructible de MeteoHub), une observation n'existe nulle
  part ailleurs. Elle est donc stockée à part, dans l'état du service, et survit à
  une purge ou une reconstruction du cache.

## Format de stockage

Fichier JSON unique, dans l'**état persistant** du service (doctrine morfSystem,
voir [FILESYSTEM.md](FILESYSTEM.md)) :

- sous systemd : `$STATE_DIRECTORY/meteo-annotations.json`
  (typiquement `/var/lib/morfsystem/morfanalytics/meteo-annotations.json`) ;
- repli hors systemd : même dossier d'état que le cache, mais **fichier distinct**
  du cache SQLite des mesures.

Le fichier n'est jamais le cache des mesures : une purge du cache ne le touche pas.

Écriture **atomique** (fichier temporaire puis renommage) : une observation n'est
jamais laissée à moitié écrite si le service s'arrête au mauvais moment.

Structure :

```json
{
  "annotations": [
    {
      "id": "2c3dea84-7ea9-4ef6-9a31-3476977f87e9",
      "start": 1756241000,
      "end": 1756246400,
      "types": ["orage", "pluie", "vent_fort", "grele"],
      "description": "Première dégradation, puis seconde cellule depuis la côte…",
      "created_at": "2026-08-26T21:19:32Z",
      "updated_at": "2026-08-26T21:19:43Z"
    }
  ],
  "updated_at": "2026-08-26T21:19:43Z"
}
```

| Champ | Type | Sens |
|---|---|---|
| `id` | chaîne | Identifiant unique (UUID), généré à la création. |
| `start` | entier | Début de la période observée, en **secondes Unix**. |
| `end` | entier | Fin de la période, en secondes Unix (`end >= start`). |
| `types` | tableau | Un ou plusieurs types observés (voir ci-dessous). |
| `description` | chaîne | Texte libre (facultatif). Le récit que les types seuls ne portent pas. |
| `created_at` | chaîne | Horodatage de création (ISO-8601 UTC). |
| `updated_at` | chaîne | Horodatage de dernière modification (ISO-8601 UTC). |

Les bornes sont en secondes Unix, **comme l'axe de temps des mesures**. Le
rapprochement futur « donne-moi les mesures de cette période » se ramène alors à
un simple `WHERE ts BETWEEN start AND end`, sans conversion.

### Types d'événements

Vocabulaire proposé par défaut, **descriptif et non scientifique** (il décrit ce
que l'utilisateur a vu) :

`orage`, `pluie`, `vent_fort`, `grele`, `neige`, `brouillard`, `gel`, `autre`.

La liste guide l'interface (cases à cocher), mais le stockage accepte tout type
non vide : le vocabulaire reste extensible sans changer le code.

## API HTTP

Trois routes, sur le domaine Meteo (`/meteohub/…`). Elles agissent uniquement sur
l'état des observations, jamais sur le cache des mesures.

### `GET /meteohub/annotations`

Liste les observations (plus récentes d'abord, par `start`) et le vocabulaire
proposé :

```json
{
  "annotations": [ … ],
  "known_types": ["orage", "pluie", "vent_fort", "grele", "neige", "brouillard", "gel", "autre"]
}
```

### `POST /meteohub/annotations`

Crée ou met à jour une observation.

- Corps **sans `id`** : création. Un `id` est généré, `created_at` posé.
- Corps **avec un `id` connu** : mise à jour. `created_at` d'origine préservé,
  `updated_at` rafraîchi.
- Corps **avec un `id` inconnu** : `404` (on ne recrée pas silencieusement une
  observation qui a pu être supprimée entre-temps).

Corps attendu :

```json
{ "start": 1756241000, "end": 1756246400,
  "types": ["orage", "pluie"], "description": "…" }
```

Réponses : `200` + l'observation stockée en cas de succès ; `400` si la saisie est
invalide (début/fin manquants ou non entiers, fin avant début, aucun type) ;
`404` pour un `id` inconnu ; `500` si l'écriture échoue.

### `POST /meteohub/annotations/delete`

Supprime une observation par identifiant.

```json
{ "id": "2c3dea84-7ea9-4ef6-9a31-3476977f87e9" }
```

Réponses : `200` `{ "ok": true, "id": … }` ; `404` si l'`id` est inconnu.

## Interface web

Page `/meteohub`, section **Observations météo** : un formulaire (début, fin,
types à cocher, description) pour créer ou modifier une observation, et la liste
des observations enregistrées avec, pour chacune, sa période, ses types et son
texte, plus les actions *Modifier* et *Supprimer*.

## Ce que la Phase 1 ne fait pas

Volontairement hors périmètre ici : détection automatique d'épisodes,
classification, prévision, rapprochement automatique mesures/observation. La
structure est prévue pour les accueillir ensuite (bornes en secondes Unix,
observation distincte des mesures), mais aucune de ces logiques n'est présente.
Voir la note de conception d'ensemble dans `.morfredus_travail/Evolution/`.
