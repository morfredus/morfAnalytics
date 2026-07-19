/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/MeteoMath.h"

#include <QStringList>
#include <cmath>
#include <algorithm>

namespace morfanalytics {
namespace meteo {

namespace {
// Coefficients de Magnus, jeu de Sonntag (1990), valable de -45 a +60 degres.
constexpr double kMagnusA = 17.62;
constexpr double kMagnusB = 243.12;
} // namespace

double dewPoint(double tempC, double humidityPct) {
    // Un logarithme d'une humidite nulle divergerait : on borne au ras du zero.
    const double rh = std::clamp(humidityPct, 0.1, 100.0);
    const double gamma = std::log(rh / 100.0) + (kMagnusA * tempC) / (kMagnusB + tempC);
    return (kMagnusB * gamma) / (kMagnusA - gamma);
}

double absoluteHumidity(double tempC, double humidityPct) {
    const double rh = std::clamp(humidityPct, 0.0, 100.0);
    // Pression de vapeur saturante (Magnus), puis loi des gaz parfaits appliquee
    // a la vapeur d'eau. Le facteur 2.1674 regroupe masse molaire et constante.
    const double saturation = 6.112 * std::exp((17.67 * tempC) / (tempC + 243.5));
    return (saturation * rh * 2.1674) / (273.15 + tempC);
}

double humidex(double tempC, double humidityPct) {
    // Sous ~20 degres, la formule n'a pas de sens physique (elle renverrait une
    // valeur inferieure a la temperature reelle). On rend la temperature brute.
    if (tempC < 20.0)
        return tempC;
    const double td = dewPoint(tempC, humidityPct);
    const double kelvin = 273.15 + td;
    if (kelvin <= 0.0)
        return tempC;
    const double vapor = 6.11 * std::exp(5417.7530 * ((1.0 / 273.16) - (1.0 / kelvin)));
    const double result = tempC + 0.5555 * (vapor - 10.0);
    // L'humidex ne descend jamais sous la temperature seche.
    return std::max(result, tempC);
}

double seaLevelPressure(double pressureHpa, double tempC, double altitudeM) {
    if (std::abs(altitudeM) < 0.5)
        return pressureHpa; // station au niveau de la mer : rien a corriger
    const double denominator = tempC + 0.0065 * altitudeM + 273.15;
    if (denominator <= 0.0)
        return pressureHpa;
    return pressureHpa * std::pow(1.0 - (0.0065 * altitudeM) / denominator, -5.257);
}

QString pressureTendencyLabel(double deltaHpa3h) {
    const double d = std::abs(deltaHpa3h);
    // Seuils de la classification OMM des tendances barometriques.
    if (d < 0.1)
        return QStringLiteral("stable");
    const QString direction = deltaHpa3h > 0 ? QStringLiteral("hausse")
                                             : QStringLiteral("baisse");
    if (d < 1.5) return direction + QStringLiteral(" lente");
    if (d < 3.5) return direction + QStringLiteral(" modérée");
    if (d < 6.0) return direction + QStringLiteral(" rapide");
    return direction + QStringLiteral(" très rapide");
}

QString zambrettiForecast(double seaLevelHpa, double delta3h, int month) {
    // Les 26 previsions de l'algorithme, de la plus stable a la plus perturbee.
    static const QStringList kForecasts = {
        QStringLiteral("Temps stable et beau"),
        QStringLiteral("Beau temps"),
        QStringLiteral("Amélioration vers le beau"),
        QStringLiteral("Beau, devenant plus variable"),
        QStringLiteral("Beau, averses possibles"),
        QStringLiteral("Assez beau, en amélioration"),
        QStringLiteral("Assez beau, averses possibles en début de période"),
        QStringLiteral("Assez beau, averses plus tard"),
        QStringLiteral("Averses en début de période, puis amélioration"),
        QStringLiteral("Variable, en amélioration"),
        QStringLiteral("Assez beau, averses probables"),
        QStringLiteral("Plutôt instable, éclaircies plus tard"),
        QStringLiteral("Instable, probablement en amélioration"),
        QStringLiteral("Averses et éclaircies"),
        QStringLiteral("Averses, devenant plus instable"),
        QStringLiteral("Variable, quelques pluies"),
        QStringLiteral("Instable, courtes éclaircies"),
        QStringLiteral("Instable, pluie plus tard"),
        QStringLiteral("Instable, pluies par moments"),
        QStringLiteral("Très instable, accalmies par moments"),
        QStringLiteral("Pluie par moments, se dégradant"),
        QStringLiteral("Pluie par moments, devenant très instable"),
        QStringLiteral("Pluie fréquente"),
        QStringLiteral("Très instable, pluie"),
        QStringLiteral("Tempétueux, possible amélioration"),
        QStringLiteral("Tempétueux, pluie abondante")
    };

    // Seuil de 1.6 hPa sur 3 h : en deca, la pression est consideree stationnaire.
    const bool rising  = delta3h > 1.6;
    const bool falling = delta3h < -1.6;

    double z = 0.0;
    if (rising)
        z = 185.0 - 0.16 * seaLevelHpa;
    else if (falling)
        z = 127.0 - 0.12 * seaLevelHpa;
    else
        z = 144.0 - 0.13 * seaLevelHpa;

    // Correction saisonniere : a pression egale, une hausse est plus favorable en
    // ete et une baisse plus penalisante en hiver.
    const bool summer = (month >= 4 && month <= 9);
    if (summer && rising)  z += 1.0;
    if (!summer && falling) z -= 1.0;

    const int index = std::clamp(static_cast<int>(std::lround(z)), 1,
                                 static_cast<int>(kForecasts.size()));
    return kForecasts.at(index - 1);
}

} // namespace meteo
} // namespace morfanalytics
