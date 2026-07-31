# Emplacements des fichiers - morfAnalytics

morfAnalytics suit la doctrine du parc morfSystem (référence complète :
`morfTemplateService/docs/fr/FILESYSTEM.md`). Ce document précise sa disposition.

## Les trois zones

| Zone | Contenu | Emplacement (Linux) | Écriture |
|------|---------|---------------------|----------|
| Programme | binaire `morfanalytics` | `/opt/morfanalytics` | non |
| Config admin | `morfanalytics.json` | `/etc/morfsystem/morfanalytics` | non |
| État persistant | cache SQLite des échantillons | `/var/lib/morfsystem/morfanalytics` | oui |
| Historique SiteWatch | synthèses d'analyse SQLite | `/opt/morfanalytics/cache/sitewatch-history.sqlite` | oui |

Sous Windows, l'état se replie sous
`%ProgramData%\morfsystem\morfanalytics\state`.

## L'état persistant

Le cache `meteohub-cache.sqlite` contient les échantillons collectés et sert de
base aux analyses. C'est de l'**état généré** par le service : le perdre oblige à
re-collecter. Il vit donc sous `/var/lib`, jamais dans le dossier courant
(`/opt`, le programme) ni dans `/etc` (la config admin).

Quand le module ne fixe pas `cache_dir`, le service résout la racine de l'état
via `$STATE_DIRECTORY` (posé par systemd grâce à
`StateDirectory=morfsystem/morfanalytics`), avec repli sur
`/var/lib/morfsystem/morfanalytics` hors systemd.

## Surcharge

Le paramètre `cache_dir` d'un module reste prioritaire sur le défaut : il sert à
placer le cache sur un autre volume, ou à pointer un ancien emplacement lors
d'une migration.

## Migration depuis une version antérieure

Une installation antérieure à 0.8.0 laisse le cache dans le dossier courant
(`/opt/morfanalytics/meteohub-cache.sqlite`). Le déplacer avant de démarrer :

```bash
sudo systemctl stop morfanalytics
sudo mv /opt/morfanalytics/meteohub-cache.sqlite /var/lib/morfsystem/morfanalytics/ 2>/dev/null || true
sudo systemctl start morfanalytics
```

Autre option : fixer `"cache_dir": "/opt/morfanalytics"` dans le module pour
conserver l'ancien emplacement.

## Historique SiteWatch

`/opt/morfanalytics/cache/sitewatch-history.sqlite` conserve une ligne par
analyse reçue de SiteWatch. Il permet d'étendre les comparaisons dans le temps
sans conserver les journaux d'accès eux-mêmes : **SiteWatch reste la source
souveraine** de ces données.

Le chemin est réglable avec `sitewatch_cache_dir` dans `morfanalytics.json`.
La base utilise le mode WAL afin que les consultations ne bloquent pas la
réception d'une nouvelle synthèse. Aucune purge automatique n'est appliquée.

Le compte qui exécute le service doit pouvoir écrire dans ce dossier. Pour le
chemin par défaut, le préparer lors de l'installation :

```bash
sudo install -d -o <utilisateur-du-service> -g <utilisateur-du-service> /opt/morfanalytics/cache
```
