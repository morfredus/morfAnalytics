/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

namespace morfanalytics {
namespace meteo {

// -----------------------------------------------------------------------------
// MeteoDiagnosis : interprete des series EXTERIEURES en une « situation ».
//
// Fonction PURE (comme MeteoMath et MeteoEvents) : series brutes en entree
// (ts croissants, NaN = manquant), structure en sortie. Le JSON est construit
// ailleurs. Principe : jamais de conclusion tiree d'une seule mesure, mais des
// SIGNAUX qui convergent, et un diagnostic prudent (« conditions favorables »,
// jamais « il va pleuvoir »). Tout est explicable : chaque signal porte sa valeur.
//
// Limite assumee de la v1 : pas de comparaison au comportement habituel de
// l'heure (etape 3) et pas de vent.
// -----------------------------------------------------------------------------

// Un signal physique, oriente vers l'humidification de l'air.
struct DiagSignal {
    QString key;       // "dew_point" | "abs_humidity" | "humidity" | "spread" | "pressure"
    QString label;     // libelle affichable
    double  delta = 0; // variation sur la fenetre (6 h), unite de la grandeur
    int     dir   = 0; // +1 va vers « plus humide », -1 vers « plus sec », 0 neutre
    bool    measured = false; // false : donnee manquante, exclu du total
};

struct Diagnosis {
    bool    valid = false;
    qint64  ts = 0;
    // Valeurs courantes (NaN si inconnues).
    double temp = 0, hum = 0, pres = 0, dewPoint = 0, spread = 0, absHum = 0;
    // Evolution du point de rosee a 1/3/6/12 h (NaN si inconnue).
    double dewTrend[4] = {0, 0, 0, 0};
    double spreadDelta6h = 0, tempDelta3h = 0;

    QStringList situations;   // cles, la plus pertinente en premier
    QString primary;          // "stable" par defaut
    // Niveaux qualitatifs : "none" | "low" | "moderate" | "high".
    QString precipitation, fog, frost;
    // "none" | "possible" | "probable".
    QString airMassChange;

    QVector<DiagSignal> signalsList;
    int agree = 0, total = 0; // convergence : signaux dans le sens dominant / mesures
    QStringList why;          // phrases chiffrees, pour « Pourquoi cette analyse ? »
};

// `localHour` (0-23) sert au brouillard (nuit/aube). Fenetre utile : 13 h avant `now`.
Diagnosis diagnose(const QVector<qint64>& ts, const QVector<double>& temp,
                   const QVector<double>& hum, const QVector<double>& pres,
                   qint64 now, int localHour);

} // namespace meteo
} // namespace morfanalytics
