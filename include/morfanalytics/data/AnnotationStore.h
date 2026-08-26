/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

namespace morfanalytics {

// -----------------------------------------------------------------------------
// AnnotationStore : observations meteorologiques HUMAINES, saisies par
// l'utilisateur et rattachees a une periode.
//
// Pourquoi un stockage a part, et non le cache SQLite des mesures :
//   - Les mesures (temp/hum/pres) sont une COPIE reconstructible de MeteoHub :
//     le cache peut etre purge et se refait tout seul au cycle de collecte
//     suivant. Une annotation, elle, est une donnee ORIGINALE : elle n'existe
//     nulle part ailleurs et se perdrait definitivement avec le cache.
//   - Une annotation n'est pas une mesure. La forcer dans les trois canaux
//     temp/hum/pres serait un contresens : c'est un evenement observe, distinct
//     de ce que la station enregistre.
//
// Le fichier vit donc dans l'ETAT du service (StateDirectory, /var/lib sous
// systemd), a cote du cache mais jamais dedans, et survit a sa reconstruction.
//
// Format sur disque : un objet JSON { "annotations": [ ... ], "updated_at": ... }.
// Chaque annotation :
//   {
//     "id":          "uuid sans accolades",
//     "start":       <secondes Unix>,          // debut de la periode observee
//     "end":         <secondes Unix>,          // fin (>= start)
//     "types":       ["orage", "pluie", ...],  // vocabulaire libre, extensible
//     "description": "texte libre",
//     "created_at":  "ISO-8601 UTC",
//     "updated_at":  "ISO-8601 UTC"
//   }
//
// Les bornes sont en SECONDES UNIX, comme l'axe de temps des mesures : le
// rapprochement futur « donne-moi les mesures de cette periode » se ramene alors
// a un simple `WHERE ts BETWEEN start AND end`, sans conversion.
// -----------------------------------------------------------------------------
class AnnotationStore {
public:
    explicit AnnotationStore(QString filePath);

    // Charge le fichier en memoire. Un fichier absent n'est pas une erreur (aucune
    // observation saisie), et renvoie true : le store demarre vide. Renvoie false
    // seulement si un fichier PRESENT est illisible ou corrompu, pour ne pas
    // masquer une perte de donnees derriere un demarrage silencieux.
    bool load();

    // Toutes les annotations, de la plus recente a la plus ancienne (par `start`).
    QJsonArray all() const;

    // Cree ou met a jour une annotation.
    //   - sans "id" dans `in`        -> creation (id genere, created_at pose) ;
    //   - avec un "id" connu         -> mise a jour (created_at preserve) ;
    //   - avec un "id" inconnu       -> echec 404 (on ne ressuscite pas un id).
    // Valide les champs. En cas d'echec, renseigne *error (message pour l'humain)
    // et *code (400 requete invalide, 404 introuvable, 500 ecriture impossible),
    // et renvoie un objet vide. En cas de succes, *code vaut 200 et le retour est
    // l'annotation stockee, telle qu'elle est persistee.
    QJsonObject upsert(const QJsonObject& in, int* code, QString* error);

    // Supprime une annotation par id. Renvoie {"ok":true,"id":...} en cas de
    // succes ; sinon *code/*error renseignes (404 si l'id est inconnu).
    QJsonObject removeById(const QString& id, int* code, QString* error);

    // Vocabulaire de types propose par defaut. Ce ne sont PAS des categories
    // scientifiques : elles decrivent ce que l'utilisateur a reellement observe.
    // La liste guide l'interface ; le stockage accepte tout type non vide, ce qui
    // laisse le vocabulaire extensible sans changer le code.
    static QStringList knownTypes();

    QString filePath() const { return m_path; }
    int count() const { return m_items.size(); }

private:
    // Ecrit l'etat courant sur disque de facon atomique (QSaveFile : ecriture dans
    // un temporaire puis renommage). Un plantage en cours d'ecriture laisse alors
    // l'ancien fichier intact plutot qu'un fichier tronque : la donnee utilisateur
    // n'est jamais a moitie ecrite.
    bool persist(QString* error) const;

    // Nettoie et valide un objet entrant vers une annotation normalisee. Renvoie
    // false + *error si invalide. Ne touche pas a id/created_at (geres par upsert).
    static bool normalize(const QJsonObject& in, QJsonObject* out, QString* error);

    QString                m_path;
    QVector<QJsonObject>   m_items;   // annotations en memoire (source de rendu)
};

} // namespace morfanalytics
