/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QStringList>
#include <QSqlDatabase>
#include <QHash>
#include "morfanalytics/data/Series.h"
#include <functional>

namespace morfanalytics {

// Position d'un collecteur dans l'historique d'une source. C'est un couple
// (jour, index) et NON un horodatage : voir SampleStore pour le pourquoi.
struct Cursor {
    quint32 dayKey = 0; // AAAAMMJJ
    quint32 index  = 0; // position du prochain enregistrement a lire dans ce jour
    bool isNull() const { return dayKey == 0; }
};

// Fichier source d'une journee, tel que le cache le connait. Une journee peut
// venir de PLUSIEURS fichiers successifs sur la source : changement de carte SD
// ou de carte hub en cours de journee (echange prod <-> banc du 2026-09-27). Le
// nouveau fichier repart a l'index 0 : ses positions n'ont rien a voir avec
// celles de l'ancien. Chaque fichier recoit donc sa GENERATION, et les
// positions ne se comparent qu'a l'interieur d'une meme generation.
// `firstTs` (horodatage de l'enregistrement 0, publie par la source dans
// /api/history/days) identifie le fichier : il ne change pas quand le fichier
// grandit, il change quand la source repart d'un fichier neuf.
struct DaySource {
    bool    known   = false; // la journee a deja une generation en cache
    quint32 gen     = 0;
    qint64  firstTs = 0;     // 0 = inconnu (cache migre sans index 0 : a adopter)
};

// -----------------------------------------------------------------------------
// SampleStore : le CACHE DE TRAVAIL local (SQLite), alimente par un collecteur.
//
// morfAnalytics ne possede jamais la verite des donnees. Ce cache est une copie
// de travail : il peut etre efface et reconstruit integralement depuis la source
// (l'ESP32) sans aucune perte. Les analyses le lisent, rien ne l'ecrit sauf le
// collecteur.
//
// --- Pourquoi un curseur (jour, index) et pas un horodatage ------------------
// La source ecrit ses fichiers journaliers en AJOUT SEUL : la position d'une
// mesure dans son fichier ne change jamais. L'horodatage, lui, n'est pas fiable
// comme curseur - au passage a l'heure d'hiver, une heure entiere se REPETE, et
// un recalage NTP peut faire RECULER l'horloge de l'ESP32. Un curseur temporel
// sauterait alors des mesures ou en importerait deux fois.
//
// La cle primaire (day_key, gen, idx) rend l'import IDEMPOTENT : re-demander une
// plage deja importee ne cree aucun doublon. Le curseur peut donc etre perdu ou
// remis a zero sans danger - au pire on relit, jamais on ne duplique. `gen`
// distingue les fichiers successifs d'une meme journee (voir DaySource).
//
// --- Generique ---------------------------------------------------------------
// Les canaux sont donnes a la construction ; la table est creee avec une colonne
// par canal. Le store ignore totalement ce que ces canaux representent, ce qui
// permet de le reutiliser tel quel dans un autre projet.
// -----------------------------------------------------------------------------
class SampleStore {
public:
    SampleStore(QString dbPath, QStringList channels);
    ~SampleStore();

    // Ouvre la base et cree le schema si besoin. false si SQLite est indisponible.
    bool open();
    void close();
    bool isOpen() const;

    QString lastError() const { return m_lastError; }

    // --- Ecriture (collecteur uniquement) ------------------------------------
    // Insere un lot de mesures pour un jour donne, a partir de `firstIndex`.
    // Les doublons sont ignores silencieusement (cf. idempotence ci-dessus).
    // Tout le lot passe dans UNE transaction : sur une carte SD ou une cle USB,
    // valider chaque insertion separement serait des ordres de grandeur plus lent.
    bool insertBatch(quint32 dayKey, quint32 gen, quint32 firstIndex,
                     const QVector<qint64>& timestamps,
                     const QVector<QHash<QString, double>>& values);
    // Generation 0 (tests, sources a fichier unique).
    bool insertBatch(quint32 dayKey, quint32 firstIndex,
                     const QVector<qint64>& timestamps,
                     const QVector<QHash<QString, double>>& values) {
        return insertBatch(dayKey, 0, firstIndex, timestamps, values);
    }

    // Position de reprise dans le fichier (dayKey, gen) : MAX(idx)+1, 0 si rien.
    // Le cache est ainsi SON PROPRE curseur : la position de reprise se deduit
    // du contenu reellement present, pas d'un compteur tenu a part qui pourrait
    // se desynchroniser. Consequence utile : si un jour passe recoit une mesure
    // tardive (horloge de la source recalee en arriere), l'ecart avec le nombre
    // annonce par la source se voit immediatement et le trou est comble.
    quint32 resumeIndex(quint32 dayKey, quint32 gen) const;

    // Generation courante d'une journee (known = false si jamais vue).
    DaySource daySource(quint32 dayKey) const;
    // Enregistre la generation courante d'une journee et son identifiant.
    bool setDaySource(quint32 dayKey, quint32 gen, qint64 firstTs);
    // Passe la journee a une NOUVELLE generation (la source a repris un fichier
    // neuf). Les lignes des generations precedentes datees a partir de
    // `newFirstTs` sont retirees : elles ont ete importees a tort, par position,
    // depuis le nouveau fichier (avant 0.59.0), et vont revenir dans la nouvelle
    // generation. Un ancien fichier ne peut pas contenir de mesure posterieure a
    // la premiere du fichier qui l'a remplace. Renvoie la nouvelle generation,
    // ou -1 en cas d'erreur.
    qint64 startNewGeneration(quint32 dayKey, qint64 newFirstTs);

    // Nombre d'echantillons en cache pour chaque jour (toutes generations). Ne
    // decroit jamais (hors purge) : sert de revision a la publication morfSync.
    QHash<quint32, quint32> samplesPerDay() const;

    // Curseur explicite : conserve pour l'affichage d'etat et le diagnostic.
    // La reprise, elle, s'appuie sur resumeIndex() et daySource().
    Cursor cursor(const QString& source) const;
    bool setCursor(const QString& source, const Cursor& c);

    // --- Nettoyage (cache uniquement - jamais la source) ---------------------
    // Ces operations n'agissent QUE sur la copie locale : la source de verite
    // (l'appareil) n'est jamais touchee, le collecteur n'emettant que des GET.
    //
    // Le nettoyage partiel NEUTRALISE (valeurs mises a NULL) au lieu de
    // supprimer les lignes : la reprise de collecte se deduit de MAX(idx) par
    // jour, et des lignes supprimees seraient re-telechargees depuis l'appareil
    // au cycle suivant. Une ligne neutralisee, elle, reste en place - l'import
    // etant en OR IGNORE, la valeur d'origine ne revient pas tant que le cache
    // n'est pas purge entierement.

    // Neutralise les canaux donnes sur [fromTs, toTs]. Renvoie le nombre de
    // lignes touchees (celles ou au moins un des canaux etait renseigne), ou -1
    // en cas d'erreur. dryRun : compte sans rien modifier.
    qint64 invalidateChannels(qint64 fromTs, qint64 toTs, const QStringList& channels,
                              bool dryRun);

    // Vide integralement le cache (mesures + curseurs). Le prochain cycle de
    // collecte reconstruit tout depuis la source, sans aucune perte : c'est la
    // seule forme de suppression totale qui reste coherente avec la reprise.
    bool purgeAll();

    // --- Qualification (lecture) ---------------------------------------------
    // Fonction appliquee a chaque lecture d'analyse : elle remplace par NaN les
    // points ecartes (bornes, pic isole, exclusion), SANS toucher au cache. Le
    // store lit une fenetre elargie de kQualifyPadS de chaque cote, pour que les
    // points au bord de la periode demandee aient leurs voisins, puis recadre.
    using Qualifier = std::function<void(Series&)>;
    void setQualifier(Qualifier q) { m_qualifier = std::move(q); }
    static constexpr qint64 kQualifyPadS = 1800;

    // --- Lecture (analyses) --------------------------------------------------
    // Toutes les mesures de [fromTs, toTs] triees par horodatage croissant,
    // QUALIFIEES (points ecartes = NaN) si un qualificateur est pose.
    Series range(qint64 fromTs, qint64 toTs) const;

    // Donnee BRUTE de [fromTs, toTs], sans qualification (graphiques : montrer
    // les points ecartes ; bilan qualite).
    Series rangeRaw(qint64 fromTs, qint64 toTs) const;

    // Toutes les mesures d'UN jour (day_key = AAAAMMJJ), triees par ts croissant.
    // Selection par day_key (et non par bornes temporelles) : c'est la source qui
    // decoupe les journees, donc on s'aligne sur SON decoupage sans reconstruire
    // des bornes a partir d'un fuseau, ce qui serait fragile au changement d'heure.
    Series rangeForDay(quint32 dayKey) const;
    Series rangeForDayRaw(quint32 dayKey) const;

    // Nombre total de mesures en cache, et bornes temporelles couvertes.
    qint64 count() const;
    bool bounds(qint64& firstTs, qint64& lastTs) const;

private:
    QString column(const QString& channel) const;
    // Schema anterieur a 0.59.0 (cle (day_key, idx), sans generation) :
    // migration en place vers (day_key, gen, idx), generation 0.
    bool migrateToGenerations();

    QString      m_dbPath;
    QStringList  m_channels;
    QString      m_connectionName;
    QString      m_lastError;
    QSqlDatabase m_db;
    Qualifier    m_qualifier;
};

} // namespace morfanalytics
