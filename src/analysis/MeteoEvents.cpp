/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/MeteoEvents.h"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace morfanalytics {
namespace meteo {

namespace {

constexpr qint64 kHour = 3600;
constexpr qint64 kConnectMin = 20 * 60; // plancher du seuil de trou (comme le web)

bool isValid(double v) { return !std::isnan(v); }

// Compacte une serie (ts, v) en ne gardant que les points valides, dans l'ordre.
void compact(const QVector<qint64>& ts, const QVector<double>& v,
             QVector<qint64>& outTs, QVector<double>& outV) {
    const int n = std::min(ts.size(), v.size());
    outTs.clear(); outV.clear();
    outTs.reserve(n); outV.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (!isValid(v[i])) continue;
        outTs.push_back(ts[i]);
        outV.push_back(v[i]);
    }
}

// Seuil de trou : au-dela, deux points ne sont plus relies (vrai silence du
// capteur). Deduit de l'espacement median des mesures, avec un plancher de 20 min.
qint64 gapMaxFor(const QVector<qint64>& ts) {
    if (ts.size() < 2) return kConnectMin;
    QVector<qint64> d;
    d.reserve(ts.size() - 1);
    for (int i = 1; i < ts.size(); ++i) {
        const qint64 dt = ts[i] - ts[i - 1];
        if (dt > 0) d.push_back(dt);
    }
    if (d.isEmpty()) return kConnectMin;
    std::sort(d.begin(), d.end());
    const qint64 median = d[d.size() / 2];
    const qint64 g = static_cast<qint64>(median * 2.5);
    return std::max(g, kConnectMin);
}

// Interpolation lineaire d'une serie compactee a l'instant t, en gardant un
// curseur qui n'avance que : appele sur une horloge croissante, cela reste
// lineaire (pas de recherche a chaque appel). Renvoie NaN hors plage ou en
// travers d'un trou (> gapMax).
struct Interp {
    const QVector<qint64>& ts;
    const QVector<double>& v;
    qint64 gapMax;
    int j = 0;
    double at(qint64 t) {
        const int n = ts.size();
        if (n == 0) return std::nan("");
        if (t < ts[0] || t > ts[n - 1]) return std::nan("");
        while (j < n - 1 && ts[j + 1] <= t) ++j;
        if (ts[j] == t) return v[j];
        if (j >= n - 1) return std::nan("");
        const qint64 t0 = ts[j], t1 = ts[j + 1];
        if (t1 - t0 > gapMax) return std::nan(""); // trou : on n'invente pas
        return v[j] + (v[j + 1] - v[j]) * double(t - t0) / double(t1 - t0);
    }
};

// Pente locale (unite/heure) sous laquelle une serie est dite « stable ».
double slopeEps(const QString& metric) {
    if (metric == QLatin1String("temp")) return 0.25; // °C/h
    if (metric == QLatin1String("hum"))  return 1.0;  // %/h
    if (metric == QLatin1String("pres")) return 0.15; // hPa/h
    return 0.1;
}

} // namespace

const char* trendName(Trend t) {
    switch (t) {
        case Trend::Up:   return "hausse";
        case Trend::Down: return "baisse";
        default:          return "stable";
    }
}

double crossingEps(const QString& metric) {
    if (metric == QLatin1String("temp")) return 0.2; // °C
    if (metric == QLatin1String("hum"))  return 1.0; // %
    if (metric == QLatin1String("pres")) return 0.3; // hPa
    return 0.2;
}

QVector<Crossing> detectCrossings(const QString& metric,
                                  const QVector<qint64>& tsOut, const QVector<double>& vOut,
                                  const QVector<qint64>& tsIn,  const QVector<double>& vIn) {
    QVector<Crossing> out;
    QVector<qint64> oTs, iTs; QVector<double> oV, iV;
    compact(tsOut, vOut, oTs, oV);
    compact(tsIn,  vIn,  iTs, iV);
    if (oTs.size() < 2 || iTs.size() < 2) return out;

    Interp io{oTs, oV, gapMaxFor(oTs)};
    Interp ii{iTs, iV, gapMaxFor(iTs)};

    // Horloge commune = union triee des instants valides des deux series.
    QVector<qint64> T;
    T.reserve(oTs.size() + iTs.size());
    std::merge(oTs.begin(), oTs.end(), iTs.begin(), iTs.end(), std::back_inserter(T));
    T.erase(std::unique(T.begin(), T.end()), T.end());

    const double eps = crossingEps(metric);
    // On suit le dernier point a ecart NON nul : le changement de signe se lit
    // entre deux tels points, correct meme si une mesure tombe pile sur l'egalite.
    int sign = 0;
    bool hasCand = false;
    Crossing cand;
    bool haveLast = false;
    qint64 lT = 0; double lD = 0.0, lA = 0.0;

    for (qint64 t : T) {
        const double a = io.at(t);
        const double b = ii.at(t);
        if (!isValid(a) || !isValid(b)) { haveLast = false; continue; } // trou
        const double d = a - b;
        if (d != 0.0) {
            if (haveLast && lD * d < 0.0) {
                const double tc = lT + double(t - lT) * lD / (lD - d);
                Crossing c;
                c.metric = metric;
                c.ts = static_cast<qint64>(std::llround(tc));
                c.value = lA + (a - lA) * (tc - lT) / double(t - lT);
                c.outRising = (d > 0.0); // D passe de - a + : exterieur passe au-dessus
                cand = c; hasCand = true;
            }
            haveLast = true; lT = t; lD = d; lA = a;
        }
        if (std::fabs(d) >= eps) {
            const int ns = d > 0.0 ? 1 : -1;
            if (ns != sign && sign != 0 && hasCand) out.push_back(cand);
            if (ns != sign) sign = ns;
            hasCand = false; // oscillation sous eps oubliee
        }
    }
    return out;
}

QVector<TrendChange> detectTrendChanges(const QString& metric,
                                        const QVector<qint64>& ts, const QVector<double>& v) {
    QVector<TrendChange> out;
    QVector<qint64> cTs; QVector<double> cV;
    compact(ts, v, cTs, cV);
    if (cTs.size() < 6) return out;

    // Moyennes horaires (cle = heure Unix) : lissage qui evite de prendre chaque
    // fluctuation pour un changement de regime.
    QVector<qint64> hourKey;                 // heures presentes, triees
    QVector<double> hourMean;
    {
        // Les ts sont deja croissants apres compact() : on agrege par paliers.
        qint64 curH = cTs[0] / kHour;
        double sum = 0; int cnt = 0;
        auto flush = [&]() {
            if (cnt > 0) { hourKey.push_back(curH); hourMean.push_back(sum / cnt); }
        };
        for (int i = 0; i < cTs.size(); ++i) {
            const qint64 h = cTs[i] / kHour;
            if (h != curH) { flush(); curH = h; sum = 0; cnt = 0; }
            sum += cV[i]; ++cnt;
        }
        flush();
    }
    const int m = hourKey.size();
    if (m < 4) return out;

    // Classe chaque heure : pente centrale (unite/heure) contre la bande morte.
    const double eps = slopeEps(metric);
    QVector<Trend> lab(m, Trend::Flat);
    for (int i = 0; i < m; ++i) {
        int lo = std::max(0, i - 1), hi = std::min(m - 1, i + 1);
        const double dh = double(hourKey[hi] - hourKey[lo]);
        double slope = 0.0;
        if (dh > 0) slope = (hourMean[hi] - hourMean[lo]) / dh;
        lab[i] = slope > eps ? Trend::Up : slope < -eps ? Trend::Down : Trend::Flat;
    }

    // Encode en segments, puis fusionne les segments trop courts (< minRun heures)
    // dans leur voisin, pour ne garder que des tendances reellement etablies.
    struct Run { Trend label; int start; int end; }; // indices dans hourKey
    auto buildRuns = [&](const QVector<Trend>& L) {
        QVector<Run> r;
        for (int i = 0; i < m; ++i) {
            if (!r.isEmpty() && r.last().label == L[i]) r.last().end = i;
            else r.push_back({L[i], i, i});
        }
        return r;
    };
    const int minRun = 3; // au moins 3 heures pour une tendance credible
    QVector<Trend> L = lab;
    for (;;) {
        QVector<Run> runs = buildRuns(L);
        if (runs.size() < 2) break;
        int shortest = -1, shortestLen = minRun;
        for (int k = 0; k < runs.size(); ++k) {
            const int len = runs[k].end - runs[k].start + 1;
            if (len < shortestLen) { shortestLen = len; shortest = k; }
        }
        if (shortest < 0) break;
        // Fusion : relabelise le segment court avec l'etiquette du voisin le plus
        // long (ou le precedent en cas d'egalite / de bord).
        Trend into;
        const Run& s = runs[shortest];
        if (shortest == 0) into = runs[1].label;
        else if (shortest == runs.size() - 1) into = runs[shortest - 1].label;
        else {
            const int lenPrev = runs[shortest - 1].end - runs[shortest - 1].start + 1;
            const int lenNext = runs[shortest + 1].end - runs[shortest + 1].start + 1;
            into = lenPrev >= lenNext ? runs[shortest - 1].label : runs[shortest + 1].label;
        }
        for (int i = s.start; i <= s.end; ++i) L[i] = into;
    }

    // Emet un changement a chaque frontiere entre segments finaux retenus.
    QVector<Run> runs = buildRuns(L);
    for (int k = 1; k < runs.size(); ++k) {
        const qint64 tPrev = hourKey[runs[k - 1].end] * kHour;
        const qint64 tNext = hourKey[runs[k].start] * kHour;
        TrendChange tc;
        tc.metric = metric;
        tc.ts = (tPrev + tNext) / 2 + kHour / 2; // milieu du basculement estime
        tc.from = runs[k - 1].label;
        tc.to = runs[k].label;
        out.push_back(tc);
    }
    return out;
}

QVector<RegimeChange> detectRegimeChanges(const QVector<TrendChange>& changes, qint64 windowS) {
    QVector<RegimeChange> out;
    if (changes.isEmpty()) return out;
    QVector<TrendChange> sorted = changes;
    std::sort(sorted.begin(), sorted.end(),
              [](const TrendChange& a, const TrendChange& b) { return a.ts < b.ts; });

    int i = 0;
    while (i < sorted.size()) {
        const qint64 start = sorted[i].ts;
        QVector<TrendChange> group;
        group.push_back(sorted[i]);
        int j = i + 1;
        while (j < sorted.size() && sorted[j].ts - start <= windowS) {
            group.push_back(sorted[j]);
            ++j;
        }
        // Un regime n'existe que si AU MOINS DEUX grandeurs distinctes basculent
        // dans la meme fenetre : sinon c'est un simple changement de tendance.
        QSet<QString> metrics;
        for (const TrendChange& c : group) metrics.insert(c.metric);
        if (metrics.size() >= 2) {
            RegimeChange rc;
            qint64 sum = 0;
            for (const TrendChange& c : group) sum += c.ts;
            rc.ts = sum / group.size();
            rc.parts = group;
            out.push_back(rc);
        }
        i = j;
    }
    return out;
}

} // namespace meteo
} // namespace morfanalytics
