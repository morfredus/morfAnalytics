/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QVector>
#include <QSqlDatabase>

namespace morfanalytics {

// Une prévision « day-ahead » archivée par MeteoHub pour un jour cible, collectée
// ici pour l'analyse « prévu vs observé » (étape 9). Volume faible : ~1 par jour.
struct ForecastEntry {
    quint32 targetDay = 0;   // AAAAMMJJ : le jour PRÉVU
    qint64  issuedTs  = 0;   // horodatage Unix d'émission de la prévision
    double  tempMin   = 0.0;
    double  tempMax   = 0.0;
    QString description;
};

// -----------------------------------------------------------------------------
// ForecastStore : cache de travail local (SQLite) des prévisions archivées.
//
// Comme SampleStore, c'est une COPIE reconstructible depuis l'appareil (source de
// vérité) : le collecteur n'émet que des GET, les analyses ne font que lire.
//
// Différence clé avec SampleStore : l'écriture est un UPSERT par jour cible
// (INSERT OR REPLACE sur la clé primaire target_day). MeteoHub RÉÉCRIT la
// prévision d'un jour au fil de la veille (elle converge vers la « day-ahead ») ;
// le cache doit donc refléter la DERNIÈRE version, pas la première importée. C'est
// l'inverse de SampleStore, dont l'historique de mesures est en ajout seul et
// idempotent par (jour, index).
// -----------------------------------------------------------------------------
class ForecastStore {
public:
    explicit ForecastStore(QString dbPath);
    ~ForecastStore();

    bool open();
    void close();
    bool isOpen() const;
    QString lastError() const { return m_lastError; }

    // Écriture (collecteur uniquement) : remplace la prévision du jour cible.
    bool upsert(const ForecastEntry& e);

    // Lecture (analyses) : prévisions dont le jour cible tombe dans [dayFrom,
    // dayTo] (bornes AAAAMMJJ incluses), triées par jour croissant.
    QVector<ForecastEntry> range(quint32 dayFrom, quint32 dayTo) const;

    qint64 count() const;

    // Vide le cache : reconstruit depuis l'appareil au prochain cycle de collecte.
    bool purgeAll();

private:
    QString      m_dbPath;
    QString      m_connectionName;
    QString      m_lastError;
    QSqlDatabase m_db;
};

} // namespace morfanalytics
