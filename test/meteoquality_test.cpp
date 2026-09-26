/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Test de MeteoQuality : qualification des mesures (bornes, pic isole,
 * exclusion manuelle), sans destruction. Scenarios construits a la main, dont
 * les cas a NE PAS ecarter (vraie transition meteo, trou d'acquisition).
 *
 * Compile via l'option CMake MA_BUILD_TESTS. Retourne 0 si tout passe.
 */

#include <cstdio>
#include <cmath>
#include <string>
#include <QVector>

#include "morfanalytics/analysis/MeteoQuality.h"

using namespace morfanalytics;
using namespace morfanalytics::meteo;

static int failures = 0;

static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

// Serie reguliere (5 min) a partir de valeurs.
static void mk(const QVector<double>& vals, QVector<qint64>& ts, QVector<double>& v,
               qint64 step = 300) {
    ts.clear(); v.clear();
    for (int i = 0; i < vals.size(); ++i) { ts.push_back(1000 + i * step); v.push_back(vals[i]); }
}

static int countFlag(const QVector<quint8>& f, quint8 flag) {
    int n = 0;
    for (quint8 x : f) if (x & flag) ++n;
    return n;
}

int main() {
    const double NaN = std::nan("");
    QVector<qint64> ts; QVector<double> v;

    // --- Pic isole de pression (cas reel du 26/09 : +7 hPa au flash) -----------
    mk({1022.0, 1022.1, 1029.6, 1022.0, 1021.9}, ts, v);
    {
        const auto f = qualifyChannel("pres", ts, v, {});
        check(f[2] & QualitySpike, "pic de pression +7 hPa ecarte");
        check(countFlag(f, QualitySpike) == 1, "un seul point ecarte (les voisins restent)");
    }

    // --- Vraie variation : une marche qui PERSISTE n'est pas un pic ------------
    mk({20.0, 20.1, 16.0, 15.8, 15.7}, ts, v);   // front d'orage : -4 °C qui dure
    check(countFlag(qualifyChannel("temp", ts, v, {}), QualitySpike) == 0,
          "chute de temperature persistante conservee");

    // --- Pic sous le seuil : conserve (seuils prudents) ------------------------
    mk({20.0, 20.1, 22.5, 20.0, 20.1}, ts, v);   // +2,4 °C < 3 °C
    check(countFlag(qualifyChannel("temp", ts, v, {}), QualitySpike) == 0,
          "ecart de 2,4 C sous le seuil conserve");

    // --- Humidite qui plonge puis revient : pic -------------------------------
    mk({60.0, 61.0, 35.0, 60.0, 59.0}, ts, v);
    check(qualifyChannel("hum", ts, v, {})[2] & QualitySpike, "creux d'humidite isole ecarte");

    // --- Voisins en desaccord : vraie transition, pas un aller-retour ---------
    mk({1020.0, 1024.0, 1028.0}, ts, v);          // montee reguliere forte
    check(countFlag(qualifyChannel("pres", ts, v, {}), QualitySpike) == 0,
          "transition monotone jamais prise pour un pic");

    // --- Trou d'acquisition : pas de faux pic ----------------------------------
    {
        QVector<qint64> t{1000, 1300, 5000, 5300};   // 1 h de trou avant le 3e point
        QVector<double> x{20.0, 20.0, 26.0, 20.0};
        check(countFlag(qualifyChannel("temp", t, x, {}), QualitySpike) == 0,
              "voisin trop lointain (trou) : pas de pic");
    }

    // --- Bords de serie : pas de jugement sans les deux voisins ---------------
    mk({30.0, 20.0, 20.0}, ts, v);
    check(countFlag(qualifyChannel("temp", ts, v, {}), QualitySpike) == 0,
          "premier point sans voisin gauche : non juge");

    // --- Bornes physiques, et la valeur hors bornes ne sert pas de voisin ------
    mk({20.0, 20.0, -99.0, 20.0, 20.0}, ts, v);
    {
        const auto f = qualifyChannel("temp", ts, v, {});
        check(f[2] & QualityBounds, "-99 C hors bornes");
        check(countFlag(f, QualitySpike) == 0, "voisins d'une valeur aberrante non accuses");
    }

    // --- Trous (NaN) ignores ---------------------------------------------------
    mk({20.0, NaN, 20.1, NaN, 20.2}, ts, v);
    check(countFlag(qualifyChannel("temp", ts, v, {}), 0xFF) == 0, "NaN jamais qualifie");

    // --- Exclusion manuelle (sonde rentree a l'interieur) ---------------------
    mk({20.0, 26.0, 26.1, 26.0, 19.0}, ts, v);
    {
        const QVector<TimeRange> ex{{1300, 1900}};  // points 1..3
        const auto f = qualifyChannel("temp", ts, v, ex);
        check((f[1] & QualityExcluded) && (f[2] & QualityExcluded) && (f[3] & QualityExcluded),
              "periode annotee ecartee");
        check(f[0] == QualityOk && f[4] == QualityOk, "hors periode : conserve");
    }

    // --- applyQuality : NaN a la place, valeur d'origine rapportee ------------
    {
        Series s(QStringList{"temp", "pres"});
        const double pres[5] = {1022.0, 1022.1, 1029.6, 1022.0, 1021.9};
        for (int i = 0; i < 5; ++i)
            s.append(1000 + i * 300, {{"temp", 20.0 + i * 0.1}, {"pres", pres[i]}});
        const auto flagged = applyQuality(s, {});
        check(flagged.size() == 1 && flagged[0].channel == "pres"
              && std::fabs(flagged[0].value - 1029.6) < 1e-9,
              "point ecarte rapporte avec sa valeur d'origine");
        check(!Series::isValid((*s.channel("pres"))[2]), "valeur ecartee devenue NaN");
        check(Series::isValid((*s.channel("temp"))[2]), "autres canaux intacts");
        check(std::string(qualityReasonCode(flagged[0].flags)) == "pic", "motif = pic");
    }

    // --- Series::slice ---------------------------------------------------------
    {
        Series s(QStringList{"temp"});
        for (int i = 0; i < 6; ++i) s.append(1000 + i * 300, {{"temp", double(i)}});
        const Series c = s.slice(1300, 1900);
        check(c.size() == 3 && c.timestamps().first() == 1300 && (*c.channel("temp"))[2] == 3.0,
              "slice garde [from, to] inclus");
    }

    std::printf(failures ? "\n%d echec(s)\n" : "\nTout passe.\n", failures);
    return failures ? 1 : 0;
}
