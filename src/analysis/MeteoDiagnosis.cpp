/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/MeteoDiagnosis.h"
#include "morfanalytics/analysis/MeteoMath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace morfanalytics {
namespace meteo {

namespace {

constexpr qint64 kHour = 3600;
constexpr qint64 kHalfWin = 20 * 60; // demi-fenetre de moyenne autour d'un instant

bool ok(double v) { return !std::isnan(v); }

// Moyenne des points valides dans [t - kHalfWin, t + kHalfWin] (lisse le bruit
// capteur) ; NaN s'il n'y en a aucun : on n'invente pas au travers d'un trou.
// Bornes trouvees par dichotomie : l'historique peut compter des dizaines de
// milliers de points.
double avgAt(const QVector<qint64>& ts, const QVector<double>& v, qint64 t) {
    const int hi = int(std::upper_bound(ts.begin(), ts.end(), t + kHalfWin) - ts.begin());
    int i = int(std::lower_bound(ts.begin(), ts.end(), t - kHalfWin) - ts.begin());
    double s = 0; int n = 0;
    for (; i < hi && i < v.size(); ++i) {
        if (!ok(v[i])) continue;
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

// Series derivees point par point (T et HR du MEME instant) : point de rosee,
// humidite absolue, ecart a la saturation.
struct Derived { QVector<qint64> ts; QVector<double> dew, ah, spr; };

Derived derive(const QVector<qint64>& ts, const QVector<double>& temp, const QVector<double>& hum) {
    Derived r;
    const int n = std::min({ts.size(), temp.size(), hum.size()});
    for (int i = 0; i < n; ++i) {
        if (!ok(temp[i]) || !ok(hum[i]) || hum[i] <= 0) continue;
        const double td = dewPoint(temp[i], hum[i]);
        r.ts.push_back(ts[i]);
        r.dew.push_back(td);
        r.ah.push_back(absoluteHumidity(temp[i], hum[i]));
        r.spr.push_back(temp[i] - td);
    }
    return r;
}

int hourOf(qint64 t, int off) { return int(((t + off) / kHour % 24 + 24) % 24); }

double median(QVector<double> v) {
    std::sort(v.begin(), v.end());
    const int n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Variations sur 6 h observees a la meme heure locale (+-1 h), au plus une par
// demi-heure : distribution de reference d'UNE grandeur.
QVector<double> sameHourDeltas(const QVector<qint64>& ts, const QVector<double>& v,
                               int hNow, int off) {
    QVector<double> out;
    qint64 last = 0;
    for (int i = 0; i < ts.size() && i < v.size(); ++i) {
        if (!ok(v[i]) || ts[i] - last < 1800) continue;
        const int dh = std::abs(hourOf(ts[i], off) - hNow);
        if (std::min(dh, 24 - dh) > 1) continue;
        // Mesure la plus proche de t - 6 h (tolerance 20 min).
        const qint64 target = ts[i] - 6 * kHour;
        const int j = int(std::lower_bound(ts.begin(), ts.end(), target) - ts.begin());
        int best = -1; qint64 bd = kHalfWin + 1;
        for (int k = std::max(0, j - 1); k <= std::min<int>(ts.size() - 1, j); ++k) {
            const qint64 dd = std::llabs(ts[k] - target);
            if (dd < bd && k < v.size() && ok(v[k])) { bd = dd; best = k; }
        }
        if (best < 0) continue;
        out.push_back(v[i] - v[best]);
        last = ts[i];
    }
    return out;
}

} // namespace

Baseline buildBaseline(const QVector<qint64>& ts, const QVector<double>& temp,
                       const QVector<double>& hum, const QVector<double>& pres,
                       qint64 now, int utcOffsetS) {
    Baseline b;
    const Derived dv = derive(ts, temp, hum);
    const int hNow = hourOf(now, utcOffsetS);
    // Planchers de sigma : sous le bruit capteur, un « ecart inhabituel » n'a pas de sens.
    static const double floors[5] = {0.5, 0.3, 2.0, 0.5, 0.5};
    const QVector<double> deltas[5] = {
        sameHourDeltas(dv.ts, dv.dew, hNow, utcOffsetS), sameHourDeltas(dv.ts, dv.ah, hNow, utcOffsetS),
        sameHourDeltas(ts, hum, hNow, utcOffsetS),       sameHourDeltas(dv.ts, dv.spr, hNow, utcOffsetS),
        sameHourDeltas(ts, pres, hNow, utcOffsetS)};
    int minN = 1 << 30;
    for (int k = 0; k < 5; ++k) {
        b.n[k] = deltas[k].size();
        minN = std::min(minN, b.n[k]);
        if (deltas[k].size() < 2) { b.sig[k] = floors[k]; continue; }
        b.med[k] = median(deltas[k]);
        QVector<double> dev;
        for (double x : deltas[k]) dev.push_back(std::fabs(x - b.med[k]));
        b.sig[k] = std::max(floors[k], 1.4826 * median(dev));
    }
    // ~15 jours d'historique a cette heure (un cas par demi-heure, +-1 h) au minimum.
    b.ok = minN >= 40;
    return b;
}

Diagnosis diagnose(const QVector<qint64>& ts, const QVector<double>& temp,
                   const QVector<double>& hum, const QVector<double>& pres,
                   qint64 now, int localHour, const Baseline* base) {
    Diagnosis d;
    d.ts = now;
    d.primary = QStringLiteral("stable");
    d.precipitation = d.fog = d.frost = QStringLiteral("none");
    d.airMassChange = QStringLiteral("none");
    const double nan = std::nan("");
    d.temp = d.hum = d.pres = d.dewPoint = d.spread = d.absHum = nan;
    for (double& x : d.dewTrend) x = nan;
    d.spreadDelta6h = d.tempDelta3h = nan;

    const Derived dvd = derive(ts, temp, hum);
    const QVector<qint64>& dts = dvd.ts;
    const QVector<double>& dew = dvd.dew;
    const QVector<double>& ah = dvd.ah;
    const QVector<double>& spr = dvd.spr;

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
    // de l'humidification, d'ou flip = -1.
    // Avec une Baseline, le signal mesure l'ecart a l'HABITUDE de l'heure :
    // z = (variation - mediane de l'heure) / sigma robuste, actif des |z| >= 1,
    // « inhabituel » des |z| >= 2. Une HR qui monte de 8 points a 19 h ne compte
    // pas si c'est l'ordinaire. Sans Baseline : seuils fixes de repli.
    const bool useBase = base && base->ok;
    d.adaptive = useBase;
    struct Def { const char* key; const char* label; double delta; int flip; double eps; bool meas; };
    const Def defs[5] = {
        {"dew_point",    "Point de rosée",        dTd,             +1, 1.5, ok(dTd)},
        {"abs_humidity", "Humidité absolue",      dAh,             +1, 1.0, ok(dAh)},
        {"humidity",     "Humidité relative",     dHum,            +1, 8.0, ok(dHum)},
        {"spread",       "Écart à la saturation", d.spreadDelta6h, -1, 1.5, ok(d.spreadDelta6h)},
        {"pressure",     "Pression",              dP,              -1, 1.5, ok(dP)},
    };
    for (int k = 0; k < 5; ++k) {
        const Def& f = defs[k];
        DiagSignal s;
        s.key = QString::fromLatin1(f.key);
        s.label = QString::fromUtf8(f.label);
        s.delta = f.delta;
        s.measured = f.meas;
        if (f.meas) {
            if (useBase) {
                s.adaptive = true;
                s.usual = base->med[k];
                s.z = (f.delta - base->med[k]) / base->sig[k];
                s.dir = f.flip * (s.z >= 1.0 ? 1 : s.z <= -1.0 ? -1 : 0);
                s.unusual = std::fabs(s.z) >= 2.0;
            } else {
                s.dir = f.flip * sign(f.delta, f.eps);
            }
        }
        d.signalsList.push_back(s);
    }
    int up = 0, down = 0;
    for (const DiagSignal& s : d.signalsList) {
        if (!s.measured) continue;
        ++d.total;
        if (s.dir > 0) ++up; else if (s.dir < 0) ++down;
        if (s.unusual) ++d.unusualCount;
    }
    d.agree = std::max(up, down);
    const DiagSignal& sDew = d.signalsList[0];
    const DiagSignal& sAh = d.signalsList[1];
    const DiagSignal& sSpr = d.signalsList[3];
    const DiagSignal& sPres = d.signalsList[4];

    // Vraie humidification : le point de rosee ET l'humidite absolue montent. HR
    // seule qui monte pendant que T baisse = simple refroidissement.
    const bool humidifying = sDew.measured && sAh.measured && sDew.dir > 0 && sAh.dir > 0;
    const bool drying = sDew.measured && sAh.measured && sDew.dir < 0 && sAh.dir < 0;
    // dir de la pression : +1 = elle baisse (vers l'humidification), -1 = elle monte.
    const bool presDown = sPres.measured && sPres.dir > 0;
    const bool presUp = sPres.measured && sPres.dir < 0;
    const bool spreadShrinks = sSpr.measured && sSpr.dir > 0;
    const bool spreadGrows = sSpr.measured && sSpr.dir < 0;
    // La temperature suit un cycle quotidien : seuil fixe (le refroidissement du
    // soir est une situation en soi, pas une anomalie).
    const bool cooling = ok(d.tempDelta3h) && d.tempDelta3h <= -1.5;
    const bool warming = ok(d.tempDelta3h) && d.tempDelta3h >= 1.5;
    const bool saturated = d.spread <= 1.0 && d.hum >= 93;

    // Ordre = pertinence : la premiere situation devient `primary`.
    if (saturated) d.situations << "saturation";
    if (humidifying) d.situations << "humidification";
    if (drying) d.situations << "dessechement";
    if (presDown && (humidifying || spreadShrinks)) d.situations << "degradation";
    if (presUp && (drying || spreadGrows)) d.situations << "amelioration";
    if (cooling) d.situations << "refroidissement";
    if (warming) d.situations << "rechauffement";
    // Au moins deux grandeurs s'ecartent nettement de l'habitude de l'heure.
    if (d.unusualCount >= 2) d.situations << "inhabituelle";
    if (d.situations.isEmpty()) d.situations << "stable";
    d.primary = d.situations.first();

    // Precipitations : 5 indices, dont la proximite de la saturation. Niveau
    // qualitatif seulement (pas de probabilite sans historique calibre).
    int pr = 0;
    if (humidifying) ++pr;
    if (sDew.measured && sDew.dir > 0) ++pr;
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
    // humidite absolue tous en forte variation. Avec historique : evolution
    // INHABITUELLE pour l'heure (|z| >= 2) ; sinon seuils fixes. Jamais « certain ».
    auto big = [](const DiagSignal& s, double fixedEps) {
        return s.measured && (s.adaptive ? s.unusual : std::fabs(s.delta) >= fixedEps);
    };
    int am = 0;
    if (big(sPres, 3.0)) ++am;
    if (big(sDew, 3.0)) ++am;
    if (big(sAh, 2.0)) ++am;
    d.airMassChange = am >= 3 ? "probable" : am == 2 ? "possible" : "none";

    // Explication chiffree : chaque phrase renvoie aux valeurs mesurees.
    d.why << QStringLiteral("Température %1 °C, humidité %2 %, point de rosée %3 °C, écart à la saturation %4 °C.")
                 .arg(f1(d.temp)).arg(qRound(d.hum)).arg(f1(d.dewPoint)).arg(f1(d.spread));
    for (const DiagSignal& s : d.signalsList) {
        if (!s.measured) continue;
        const QString unit = s.key == "humidity" ? " %" : s.key == "pressure" ? " hPa"
                           : s.key == "abs_humidity" ? " g/m³" : " °C";
        const QString sens = s.dir > 0 ? "vers plus humide" : s.dir < 0 ? "vers plus sec" : "dans la norme";
        QString line = QStringLiteral("%1 : %2%3 sur 6 h (%4).")
                           .arg(s.label, QString(s.delta > 0 ? "+" : "") + f1(s.delta), unit, sens);
        if (s.adaptive) {
            line += QStringLiteral(" Habituellement à cette heure : %1%2%3.")
                        .arg(s.usual > 0 ? "+" : "", f1(s.usual), unit);
            if (s.unusual) line += QStringLiteral(" Évolution inhabituelle pour cette heure.");
        }
        d.why << line;
    }
    if (!useBase)
        d.why << QStringLiteral("Historique insuffisant pour cette heure : seuils fixes utilisés.");
    if (cooling && !humidifying && ok(d.dewTrend[1]) && std::fabs(d.dewTrend[1]) < 1.0)
        d.why << QStringLiteral("La température baisse alors que le point de rosée reste stable : la hausse d'humidité relative vient du refroidissement, pas d'un air plus humide.");
    return d;
}

} // namespace meteo
} // namespace morfanalytics
