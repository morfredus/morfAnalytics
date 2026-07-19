# Choix de conception et raisons

Retour à l'[index de la documentation](README.md).

---

Ce document consigne les décisions structurantes de morfAnalytics et **pourquoi**
elles ont été prises. Il vise à éviter qu'on les redéfasse par inadvertance : la
plupart paraissent arbitraires tant qu'on ignore le problème qu'elles écartent.

Le pendant côté station est documenté dans MeteoHub
(`docs/decisions/analytics-integration.md`).

---

## 1. Le cache est une copie jetable, jamais une base de données

morfAnalytics ne possède **jamais** la vérité des données. Son cache SQLite est
une copie de travail : on peut l'effacer, il se reconstruit intégralement depuis
l'appareil, sans perte.

**Pourquoi c'est structurant.** Cela autorise à privilégier la vitesse sur la
durabilité (`synchronous=NORMAL`), à supprimer le cache lors d'une
désinstallation, et à ne jamais avoir à sauvegarder ce service. À l'inverse, tout
ce qui n'existerait *que* dans le cache trahirait ce principe et devient interdit.

**Sens de circulation.** Le collecteur n'émet que des requêtes `GET` : il ne peut,
par construction, rien modifier sur l'appareil. Aucune évolution ne doit ouvrir
cette porte.

---

## 2. La reprise de collecte se repère par (jour, index), jamais par horodatage

Un horodatage n'est pas un repère fiable : il **recule** lors d'un recalage
d'horloge et **se répète** au passage à l'heure d'hiver. Un collecteur reprenant
« après le dernier horodatage connu » sauterait des mesures dans un cas et en
dupliquerait dans l'autre.

Les fichiers journaliers de l'appareil étant écrits en **ajout seul**, la position
d'une mesure ne change jamais.

**Poussé un cran plus loin** : la position de reprise n'est pas *stockée*, elle
est **déduite** du contenu du cache (`SELECT day_key, MAX(idx)+1 GROUP BY
day_key`). Un compteur tenu à part pourrait se désynchroniser du cache réel ; le
contenu, lui, ne ment pas. Et `MAX(idx)+1` plutôt que `COUNT(*)` garantit qu'un
lot interrompu en son milieu ne saute rien définitivement.

La clé primaire `(day_key, idx)` rend enfin l'import **idempotent** : réimporter
une plage connue ne crée aucun doublon.

---

## 3. Les lots sont limités à 250 mesures

Mesuré sur l'appareil : au-delà d'environ 300 enregistrements, son flux de
réponse asynchrone abandonne et la requête se termine sans renvoyer un octet.

Une journée complète demande donc six requêtes au lieu d'une. C'est le prix à
payer pour ne jamais dépendre d'une taille de réponse que l'appareil ne sait pas
tenir. **Ce défaut n'était pas visible à la compilation** : il n'est apparu qu'au
premier dialogue avec du matériel réel.

---

## 4. Le moteur d'analyse ignore la météo

`IAnalysis` et `Series` ne connaissent que des **canaux nommés** sur un axe de
temps. Aucune notion de pression, de rosée ou de saison n'apparaît dans le moteur
ni dans le cache.

**Pourquoi.** Un autre projet de l'écosystème (consommation électrique, qualité
d'air, supervision) doit pouvoir réutiliser la chaîne complète en enregistrant
son propre jeu d'analyses. C'est l'unique appel à `registerMeteoAnalyses()` qui
spécialise ce service en moteur météo — et rien d'autre.

---

## 5. Les formules météo sont des fonctions pures, isolées

`MeteoMath` ne contient que des fonctions sans état ni accès au cache.

**Pourquoi.** Ce sont les seules parties du code où une erreur reste **plausible
à l'œil** : un point de rosée faux ressemble à un point de rosée juste, et rien
ne le signalera. Les isoler permet de les vérifier contre des valeurs de
référence, seule garantie réelle. Partout ailleurs, une erreur se voit.

---

## 6. Une analyse rend un résultat, jamais un flot de mesures

Tendance, score, classement, quelques valeurs — jamais des milliers de points.
Rapatrier des mesures est le travail de l'API d'historique de l'appareil.

**Corollaire assumé** : une analyse manquant d'historique répond `ok: false` avec
la raison et la profondeur requise, **sans code d'erreur HTTP**. Le service a bien
répondu ; c'est le résultat qui n'est pas calculable. Publier une moyenne calculée
sur trois mesures serait plus trompeur qu'une indisponibilité annoncée.

Chaque analyse porte également ses limites dans son champ `note`, affiché tel
quel : trois grandeurs seulement, ni vent ni ensoleillement.

---

## 7. Le service s'annonce par une capacité, pas par son nom

morfAnalytics déclare la capacité `advanced_analysis` dans son heartbeat
morfBeacon. C'est par elle que MeteoHub le reconnaît.

**Pourquoi.** Le projet est sous licence GPL : chacun peut renommer son service.
Renommer `app_name` en « Mon Analyse Météo » est parfaitement légitime, et une
reconnaissance fondée sur le nom aurait cessé de fonctionner à cet instant.

| Notion | Champ | Nature |
|---|---|---|
| Identité | `app_name` | modifiable, affichée comme libellé |
| Capacité | `capabilities` | stable, sert à la reconnaissance |

Le champ est **facultatif** dans le protocole : un service qui n'en déclare
aucune ne l'émet pas, et les consommateurs écrits avant son introduction ignorent
un champ inconnu. Son ajout n'a donc cassé aucun projet de l'écosystème, ce qui a
été vérifié en recompilant les services concernés.

**Nécessite morfBeacon ≥ 0.2.0.**

---

## 8. La page web est autonome et se construit depuis le catalogue

Aucune ressource externe : ni CDN, ni police distante, ni fichier à déployer à
côté du binaire.

**Pourquoi.** Le service doit rester consultable sur un réseau local **sans accès
Internet**, et s'installer par simple copie d'un exécutable. Une dépendance à un
CDN rendrait la page inutilisable précisément là où ce service a le plus de sens.

La page lit `GET /analyses` et se construit à partir de cette liste : elle ne code
en dur **aucune** analyse. En ajouter une côté serveur la fait apparaître sans
toucher à la page, avec un rendu générique lisible tant qu'aucun rendu dédié n'est
écrit.

Elle affiche enfin un lien **« ← Retour à MeteoHub »** : la station ouvre ce
service dans le *même onglet*, et sans ce lien seul le bouton « précédent » du
navigateur permettrait de revenir.

---

## 9. La mise à jour complète la configuration sans écraser les réglages

Une mise à jour qui remplacerait la configuration détruirait les réglages locaux.
Mais ne jamais y toucher est tout aussi faux : les paramètres apparus depuis
l'installation resteraient absents indéfiniment, et la fonction correspondante ne
s'activerait jamais **sans que rien ne le signale**.

C'est exactement ce qui s'est produit avec `source_url` : introduit après
l'installation, jamais livré, donc aucune collecte et aucun message.

Règle retenue : **ajouter ce qui manque, ne jamais modifier ce qui existe**, lister
ce qui a été ajouté, et sauvegarder avant d'écrire.

`source_url` est **vide** dans la configuration d'exemple, et non renseigné avec
une adresse plausible : une valeur d'exemple injectée dans une installation réelle
produirait une erreur ressemblant à une panne réseau.
