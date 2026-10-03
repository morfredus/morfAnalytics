/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/MeteoDiagnosis.h"
#include "morfanalytics/analysis/MeteoMath.h"

#include <algorithm>
#include <cmath>

namespace morfanalytics {
namespace meteo {

namespace {

constexpr qint64 kHour = 3600;
constexpr qint64 kHalfWin = 20 * 60; // demi-fenetre de moyenne autour d'un instant

bool ok(double v) { return !std::isnan(v); }

// Moyenne des points valides dans [t - kHalfWin, t + kHalfWin] (lisse le bruit
// capteur) ; NaN s'il n'y en a aucun : on n'invente pas au travers d'un trou.
double avgAt(const QVector<qint64>& ts, const QVector<double>& v, qint64 t) {
    double s = 0; int n = 0;
    for (int i = 0; i < ts.size() && i < v.size(); ++i) {
        if (ts[i] < t - kHalfWin || ts[i] > t + kHalfWin || !ok(v[i])) continue;
        s += v[i]; ++n;
    }
    return n ? s / n : std::nan("");
}

// Variation sur h heures : valeur(now) - valeur(now - h), NaN si l'un manque.
double deltaOver(const QVector<qint64>& ts, const QVector<double>& v, qint64 now, int h) {
    const double a = avgAt(ts, v, now), b = avgAt(ts, v, now - h * kHour);
    return (ok(a) && ok(b)) ? a - b : std::nan("");
}

// Signe seuille : +1 / -1 au-dela de +-eps, 0 sinon (NaN -> 0).
int sign(double d, double eps) { return d >= eps ? 1 : (d <= -eps ? -1 : 0); }

QString f1(double v) { return QString::number(v, 'f', 1).replace('.', ','); }

} // namespace

Diagnosis diagnose(const QVector<qint64>& ts, const QVector<double>& temp,
                   const QVector<double>& hum, const QVector<double>& pres,
                   qint64 now, int localHour) {
    Diagnosis d;
    d.ts = now;
    d.primary = QStringLiteral("stable");
    d.precipitation = d.fog = d.frost = QStringLiteral("none");
    d.airMassChange = QStringLiteral("none");
    const double nan = std::nan("");
    d.temp = d.hum = d.pres = d.dewPoint = d.spread = d.absHum = nan;
    for (double& x : d.dewTrend) x = nan;
    d.spreadDelta6h = d.tempDelta3h = nan;

    // Series derivees point par point (T et HR du MEME instant).
    const int n = std::min({ts.size(), temp.size(), hum.size()});
    QVector<qint64> dts; QVector<double> dew, ah, spr;
    for (int i = 0; i < n; ++i) {
        if (!ok(temp[i]) || !ok(hum[i]) || hum[i] <= 0) continue;
        const double td = dewPoint(temp[i], hum[i]);
        dts.push_back(ts[i]);
        dew.push_back(td);
        ah.push_back(absoluteHumidity(temp[i], hum[i]));
        spr.push_back(temp[i] - td);
    }

    d.temp = avgAt(ts, temp, now);
    d.hum = avgAt(ts, hum, now);
    d.pres = avgAt(ts, pres, now);
    d.dewPoint = avgAt(dts, dew, now);
    d.absHum = avgAt(dts, ah, now);
    d.spread = avgAt(dts, spr, now);
    if (!ok(d.temp) || !ok(d.hum) || !ok(d.dewPoint)) return d; // rien d'interpretable
    d.valid = true;

    static const int hours[4] = {1, 3, 6, 12};
    for (int i = 0; i < 4; ++i) d.dewTrend[i] = deltaOver(dts, dew, now, hours[i]);
    d.tempDelta3h = deltaOver(ts, temp, now, 3);
    d.spreadDelta6h = deltaOver(dts, spr, now, 6);
    const double dAh = deltaOver(dts, ah, now, 6);
    const double dHum = deltaOver(ts, hum, now, 6);
    const double dP = deltaOver(ts, pres, now, 6);
    const double dTd = d.dewTrend[2], dT6 = deltaOver(ts, temp, now, 6);

    // Signaux : tous orientes vers « plus humide » (+1) ou « plus sec » (-1), sur
    // 6 h. Un ecart a la saturation ou une pression qui BAISSENT vont dans le sens
    // de l'humidification, d'ou le signe inverse.
    d.signalsList = {
        {"dew_point",    QStringLiteral("Point de rosée"),        dTd,             sign(dTd, 1.5),                ok(dTd)},
        {"abs_humidity", QStringLiteral("Humidité absolue"),      dAh,             sign(dAh, 1.0),                ok(dAh)},
        {"humidity",     QStringLiteral("Humidité relative"),     dHum,            sign(dHum, 8.0),               ok(dHum)},
        {"spread",       QStringLiteral("Écart à la saturation"), d.spreadDelta6h, -sign(d.spreadDelta6h, 1.5),   ok(d.spreadDelta6h)},
        {"pressure",     QStringLiteral("Pression"),              dP,              -sign(dP, 1.5),                ok(dP)},
    };
    int up = 0, down = 0;
    for (const DiagSignal& s : d.signalsList) {
        if (!s.measured) continue;
        ++d.total;
        if (s.dir > 0) ++up; else if (s.dir < 0) ++down;
    }
    d.agree = std::max(up, down);

    // Vraie humidification : le point de rosee ET l'humidite absolue montent. HR
    // seule qui monte pendant que T baisse = simple refroidissement.
    const bool humidifying = ok(dTd) && ok(dAh) && dTd >= 1.5 && dAh >= 1.0;
    const bool drying = ok(dTd) && ok(dAh) && dTd <= -1.5 && dAh <= -1.0;
    const bool presDown = ok(dP) && dP <= -1.5, presUp = ok(dP) && dP >= 1.5;
    const bool cooling = ok(d.tempDelta3h) && d.tempDelta3h <= -1.5;
    const bool warming = ok(d.tempDelta3h) && d.tempDelta3h >= 1.5;
    const bool saturated = d.spread <= 1.0 && d.hum >= 93;
    const bool spreadShrinks = ok(d.spreadDelta6h) && d.spreadDelta6h <= -1.5;
    const bool spreadGrows = ok(d.spreadDelta6h) && d.spreadDelta6h >= 1.5;

    // Ordre = pertinence : la premiere situation devient `primary`.
    if (saturated) d.situations << "saturation";
    if (humidifying) d.situations << "humidification";
    if (drying) d.situations << "dessechement";
    if (presDown && (humidifying || spreadShrinks)) d.situations << "degradation";
    if (presUp && (drying || spreadGrows)) d.situations << "amelioration";
    if (cooling) d.situations << "refroidissement";
    if (warming) d.situations << "rechauffement";
    if (d.situations.isEmpty()) d.situations << "stable";
    d.primary = d.situations.first();

    // Precipitations : 5 indices, dont la proximite de la saturation. Niveau
    // qualitatif seulement (pas de probabilite sans historique calibre).
    int pr = 0;
    if (humidifying) ++pr;
    if (ok(dTd) && dTd >= 1.5) ++pr;
    if (d.spread <= 2.5) ++pr;
    if (d.hum >= 85) ++pr;
    if (presDown) ++pr;
    if (d.spread > 4.0 || d.hum < 70) pr = std::min(pr, 1); // air trop sec : pas favorable
    d.precipitation = pr >= 4 ? "high" : pr == 3 ? "moderate" : pr == 2 ? "low" : "none";

    // Brouillard : air proche de la condensation ET refroidissement ou nuit/aube.
    // « Air humide » seul ne suffit pas.
    const bool night = localHour >= 20 || localHour <= 8;
    if (d.hum >= 88 && d.spread <= 3.5) {
        if (d.spread <= 1.5 && d.hum >= 93 && (cooling || night)) d.fog = "high";
        else if (d.spread <= 2.5 && (cooling || night)) d.fog = "moderate";
        else d.fog = "low";
    }

    // Gel : temperature REELLE proche de 0 et en baisse, Td proche de T.
    if (d.temp <= 5.0 && (cooling || (ok(dT6) && dT6 <= -1.0) || d.temp <= 0.5)) {
        if (d.temp <= 1.0 && d.spread <= 4.0) d.frost = "high";
        else if (d.temp <= 3.0) d.frost = "moderate";
        else d.frost = "low";
    }

    // Changement probable des caracteristiques de l'air : pression, Td et
    // humidite absolue tous en forte variation. L'ecart au cycle habituel
    // (etape 3) manque encore : jamais « certain ».
    int am = 0;
    if (ok(dP) && std::fabs(dP) >= 3.0) ++am;
    if (ok(dTd) && std::fabs(dTd) >= 3.0) ++am;
    if (ok(dAh) && std::fabs(dAh) >= 2.0) ++am;
    d.airMassChange = am >= 3 ? "probable" : am == 2 ? "possible" : "none";

    // Explication chiffree : chaque phrase renvoie aux valeurs mesurees.
    d.why << QStringLiteral("Température %1 °C, humidité %2 %, point de rosée %3 °C, écart à la saturation %4 °C.")
                 .arg(f1(d.temp)).arg(qRound(d.hum)).arg(f1(d.dewPoint)).arg(f1(d.spread));
    for (const DiagSignal& s : d.signalsList) {
        if (!s.measured) continue;
        const QString unit = s.key == "humidity" ? " %" : s.key == "pressure" ? " hPa"
                           : s.key == "abs_humidity" ? " g/m³" : " °C";
        const QString sens = s.dir > 0 ? "vers plus humide" : s.dir < 0 ? "vers plus sec" : "stable";
        d.why << QStringLiteral("%1 : %2%3 sur 6 h (%4).")
                     .arg(s.label, QString(s.delta > 0 ? "+" : "") + f1(s.delta), unit, sens);
    }
    if (cooling && !humidifying && ok(d.dewTrend[1]) && std::fabs(d.dewTrend[1]) < 1.0)
        d.why << QStringLiteral("La température baisse alors que le point de rosée reste stable : la hausse d'humidité relative vient du refroidissement, pas d'un air plus humide.");
    return d;
}

} // namespace meteo
} // namespace morfanalytics
