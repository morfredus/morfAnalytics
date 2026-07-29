/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Test des formules meteorologiques (MeteoMath) contre des valeurs de reference
 * et des invariants physiques. Ce sont les seules parties du moteur ou une erreur
 * est SILENCIEUSE (un resultat faux reste plausible), donc les seules a verifier
 * isolement -- exactement ce que dit l'en-tete de MeteoMath.h.
 *
 * Deux familles de verifications :
 *   - valeurs de reference issues de tables meteorologiques (tolerance large,
 *     pour ne pas dependre d'une variante de formule) ;
 *   - INVARIANTS qui doivent tenir pour toute implementation correcte, quelle que
 *     soit la variante : saturation, monotonie, altitude nulle, seuil humidex.
 *
 * Compile via l'option CMake MA_BUILD_TESTS. Retourne 0 si tout passe.
 */

#include <cstdio>
#include <cmath>

#include "morfanalytics/analysis/MeteoMath.h"

using namespace morfanalytics::meteo;

static int failures = 0;

static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

// Egalite a une tolerance pres, avec impression de l'ecart en cas d'echec.
static void checkNear(double got, double expected, double tol, const char* msg) {
    const bool ok = std::fabs(got - expected) <= tol;
    if (ok) {
        std::printf("ok  : %s (%.3f ~ %.3f)\n", msg, got, expected);
    } else {
        std::printf("FAIL: %s (obtenu %.3f, attendu %.3f +/- %.3f)\n",
                    msg, got, expected, tol);
        ++failures;
    }
}

int main() {
    // --- Point de rosee (Magnus-Tetens) --------------------------------------
    // Valeurs de reference publiees ; l'invariant fort est la saturation.
    checkNear(dewPoint(20.0, 50.0), 9.3,  0.5, "dewPoint(20,50) ~ 9.3 C");
    checkNear(dewPoint(30.0, 80.0), 26.2, 0.6, "dewPoint(30,80) ~ 26.2 C");
    checkNear(dewPoint(22.0, 100.0), 22.0, 0.2,
              "dewPoint a 100% RH == temperature (saturation)");
    check(dewPoint(20.0, 80.0) > dewPoint(20.0, 40.0),
          "point de rosee croissant avec l'humidite");

    // --- Humidite absolue (g/m3) ---------------------------------------------
    checkNear(absoluteHumidity(20.0, 50.0),  8.6,  0.7, "absoluteHumidity(20,50) ~ 8.6 g/m3");
    checkNear(absoluteHumidity(20.0, 100.0), 17.3, 0.9, "absoluteHumidity(20,100) ~ 17.3 g/m3");
    check(absoluteHumidity(30.0, 50.0) > absoluteHumidity(20.0, 50.0),
          "a RH egale, air plus chaud = plus d'eau en absolu");

    // --- Humidex (Environnement Canada) --------------------------------------
    checkNear(humidex(30.0, 70.0), 41.0, 2.0, "humidex(30,70) ~ 41");
    // Invariant : sous ~20 C, l'humidex n'a pas de sens et renvoie la temperature.
    checkNear(humidex(10.0, 90.0), 10.0, 0.01, "humidex sous le seuil == temperature");
    check(humidex(32.0, 80.0) > 32.0, "humidex > temperature en air chaud et humide");

    // --- Deficit de pression de vapeur (kPa) ---------------------------------
    checkNear(vaporPressureDeficit(20.0, 50.0), 1.17, 0.20, "VPD(20,50) ~ 1.17 kPa");
    checkNear(vaporPressureDeficit(20.0, 100.0), 0.0, 0.02, "VPD == 0 a saturation");
    check(vaporPressureDeficit(30.0, 30.0) > vaporPressureDeficit(20.0, 30.0),
          "VPD croissant avec la chaleur a RH egale");

    // --- Pression reduite au niveau de la mer --------------------------------
    // Invariant : a altitude nulle, aucune reduction.
    checkNear(seaLevelPressure(1013.0, 15.0, 0.0), 1013.0, 0.5,
              "seaLevelPressure a altitude 0 == pression mesuree");
    checkNear(seaLevelPressure(1000.0, 15.0, 100.0), 1012.0, 2.0,
              "seaLevelPressure(1000,15,100m) ~ 1012 hPa");
    check(seaLevelPressure(1000.0, 15.0, 200.0) > 1000.0,
          "la reduction augmente la pression avec l'altitude");

    std::printf("\n%s (%d echec%s)\n",
                failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ECHOUENT",
                failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
