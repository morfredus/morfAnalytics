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

namespace morfanalytics {

// Position d'un collecteur dans l'historique d'une source. C'est un couple
// (jour, index) et NON un horodatage : voir SampleStore pour le pourquoi.
struct Cursor {
    quint32 dayKey = 0; // AAAAMMJJ
    quint32 index  = 0; // position du prochain enregistrement a lire dans ce jour
    bool isNull() const { return dayKey == 0; }
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
// comme curseur — au passage a l'heure d'hiver, une heure entiere se REPETE, et
// un recalage NTP peut faire RECULER l'horloge de l'ESP32. Un curseur temporel
// sauterait alors des mesures ou en importerait deux fois.
//
// La cle primaire (day_key, idx) rend l'import IDEMPOTENT : re-demander une
// plage deja importee ne cree aucun doublon. Le curseur peut donc etre perdu ou
// remis a zero sans danger — au pire on relit, jamais on ne duplique.
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
    bool insertBatch(quint32 dayKey, quint32 firstIndex,
                     const QVector<qint64>& timestamps,
                     const QVector<QHash<QString, double>>& values);

    // Nombre d'enregistrements deja importes pour chaque jour, soit MAX(idx)+1.
    // Le cache est ainsi SON PROPRE curseur : la position de reprise se deduit
    // du contenu reellement present, pas d'un compteur tenu a part qui pourrait
    // se desynchroniser. Consequence utile : si un jour passe recoit une mesure
    // tardive (horloge de la source recalee en arriere), l'ecart avec le nombre
    // annonce par la source se voit immediatement et le trou est comble.
    QHash<quint32, quint32> importedPerDay() const;

    // Curseur explicite : conserve pour l'affichage d'etat et le diagnostic.
    // La reprise, elle, s'appuie sur importedPerDay().
    Cursor cursor(const QString& source) const;
    bool setCursor(const QString& source, const Cursor& c);

    // --- Lecture (analyses) --------------------------------------------------
    // Toutes les mesures de [fromTs, toTs] triees par horodatage croissant.
    Series range(qint64 fromTs, qint64 toTs) const;

    // Nombre total de mesures en cache, et bornes temporelles couvertes.
    qint64 count() const;
    bool bounds(qint64& firstTs, qint64& lastTs) const;

private:
    QString column(const QString& channel) const;

    QString      m_dbPath;
    QStringList  m_channels;
    QString      m_connectionName;
    QString      m_lastError;
    QSqlDatabase m_db;
};

} // namespace morfanalytics
