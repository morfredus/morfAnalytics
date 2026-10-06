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
// Seuils : relatifs a l'HABITUDE du lieu a la meme heure (Baseline) des que
// l'historique le permet ; seuils fixes de repli sinon. Pas de vent.
// -----------------------------------------------------------------------------

// Un signal physique, oriente vers l'humidification de l'air.
struct DiagSignal {
    QString key;       // "dew_point" | "abs_humidity" | "humidity" | "spread" | "pressure"
    QString label;     // libelle affichable
    double  delta = 0; // variation sur la fenetre (6 h), unite de la grandeur
    int     dir   = 0; // +1 va vers « plus humide », -1 vers « plus sec », 0 neutre
    bool    measured = false; // false : donnee manquante, exclu du total
    bool    adaptive = false; // seuil tire de l'historique (sinon seuil fixe de repli)
    double  z = 0;            // ecart a l'habitude de l'heure, en sigmas robustes
    double  usual = 0;        // variation HABITUELLE a cette heure (mediane historique)
    bool    unusual = false;  // |z| >= 2 : evolution inhabituelle pour cette heure
};

// Comportement habituel du lieu : distribution des variations sur 6 h observees
// a la MEME heure locale (+-1 h) dans l'historique. Ordre : point de rosee,
// humidite absolue, HR, ecart a la saturation, pression.
struct Baseline {
    bool   ok = false;     // assez de cas pour s'y fier
    int    n[5] = {0, 0, 0, 0, 0};
    double med[5] = {0, 0, 0, 0, 0};
    double sig[5] = {0, 0, 0, 0, 0}; // ecart-type robuste (MAD x 1,4826), avec plancher
};

// Construit la Baseline a partir d'un long historique (plusieurs semaines).
// `utcOffsetS` : decalage horaire local, pour comparer des heures locales.
Baseline buildBaseline(const QVector<qint64>& ts, const QVector<double>& temp,
                       const QVector<double>& hum, const QVector<double>& pres,
                       qint64 now, int utcOffsetS);

// Un critere verifiable derriere un niveau (« humidite >= 93 % : oui/non »). Rend le
// diagnostic falsifiable : on montre les faits observes, pas un score.
struct Factor {
    QString label;      // phrase chiffree (valeur mesuree + seuil)
    bool    met = false;
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

    // Criteres observes derriere chaque niveau (memes seuils que le calcul ci-dessous).
    QVector<Factor> precipFactors, fogFactors, frostFactors;

    QVector<DiagSignal> signalsList;
    bool adaptive = false;    // seuils issus de l'historique du lieu
    int unusualCount = 0;     // signaux inhabituels pour l'heure
    int agree = 0, total = 0; // convergence : signaux dans le sens dominant / mesures
    QStringList why;          // phrases chiffrees, pour « Pourquoi cette analyse ? »
};

// `localHour` (0-23) sert au brouillard (nuit/aube). `base` (optionnel) remplace les
// seuils fixes par ceux du lieu ; null ou !ok -> seuils fixes de repli.
Diagnosis diagnose(const QVector<qint64>& ts, const QVector<double>& temp,
                   const QVector<double>& hum, const QVector<double>& pres,
                   qint64 now, int localHour, const Baseline* base = nullptr);

} // namespace meteo
} // namespace morfanalytics
