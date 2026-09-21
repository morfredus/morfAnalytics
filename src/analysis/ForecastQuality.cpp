/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/ForecastQuality.h"

#include <cmath>
#include <algorithm>

namespace morfanalytics {
namespace forecastq {

ErrorStats accumulate(const std::vector<double>& signedErrors) {
    ErrorStats s;
    double sum = 0.0, absSum = 0.0, sqSum = 0.0;
    for (double e : signedErrors) {
        // Un ecart non fini (journee sans couple exploitable) est ignore, pas
        // compte comme zero : le compter fausserait a la baisse la MAE et le biais.
        if (!std::isfinite(e))
            continue;
        sum    += e;
        absSum += std::abs(e);
        sqSum  += e * e;
        ++s.count;
    }
    if (s.count == 0)
        return s;  // tout a zero, valid() == false : rien de comparable

    const double n = static_cast<double>(s.count);
    s.bias = sum / n;
    s.mae  = absSum / n;
    s.rmse = std::sqrt(sqSum / n);
    return s;
}

double reliabilityIndex(double mae, double maxMae) {
    // Garde-fous : une entree invalide ne doit pas produire un indice trompeur.
    if (!std::isfinite(mae) || mae < 0.0 || maxMae <= 0.0)
        return 0.0;
    const double fraction = 1.0 - mae / maxMae;      // 1 a MAE nulle, 0 a maxMae
    const double index    = 100.0 * std::max(0.0, fraction);
    return std::round(index);
}

QString reliabilityLabel(double mae, int evaluated) {
    // On ne qualifie jamais une fiabilite sur trop peu de points, ni sur une MAE
    // non calculable : l'etat explicite prime sur une etiquette rassurante.
    if (evaluated < kMinDaysForIndex || !std::isfinite(mae))
        return QStringLiteral("Données insuffisantes");
    if (mae <= kMaeHigh)     return QStringLiteral("Fiabilité élevée");
    if (mae <= kMaeCorrect)  return QStringLiteral("Fiabilité correcte");
    if (mae <= kMaeVariable) return QStringLiteral("Fiabilité variable");
    return QStringLiteral("Fiabilité faible");
}

} // namespace forecastq
} // namespace morfanalytics
