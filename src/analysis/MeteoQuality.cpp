/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/MeteoQuality.h"

#include <cmath>

namespace morfanalytics {
namespace meteo {

namespace {

struct ChannelSpec {
    bool known = false;
    double lo = 0.0, hi = 0.0;   // bornes physiques (exterieur France, larges)
    double spike = 0.0;          // seuil de pic isole
};

ChannelSpec specFor(const QString& ch, const QualityRules& r) {
    if (ch == QLatin1String("temp")) return {true, -40.0, 60.0, r.spikeTemp};
    if (ch == QLatin1String("hum"))  return {true, 0.0, 100.0, r.spikeHum};
    if (ch == QLatin1String("pres")) return {true, 870.0, 1085.0, r.spikePres};
    return {};
}

bool inExclusion(qint64 t, const QVector<TimeRange>& ex) {
    for (const TimeRange& r : ex)
        if (t >= r.from && t <= r.to) return true;
    return false;
}

} // namespace

QVector<quint8> qualifyChannel(const QString& channel, const QVector<qint64>& ts,
                               const QVector<double>& values,
                               const QVector<TimeRange>& exclusions,
                               const QualityRules& rules,
                               const QSet<qint64>& keptTs,
                               const QVector<quint32>& sourceFlags) {
    const int n = qMin(ts.size(), values.size());
    QVector<quint8> flags(n, QualityOk);
    const ChannelSpec spec = specFor(channel, rules);

    // 1) Exclusion manuelle et bornes : jugement point par point.
    for (int i = 0; i < n; ++i) {
        if (!Series::isValid(values[i])) continue;
        if (keptTs.contains(ts[i])) continue; // reintegre : la regle ne s'applique plus
        if (inExclusion(ts[i], exclusions)) flags[i] |= QualityExcluded;
        if (i < sourceFlags.size() && (sourceFlags[i] & Series::kSourceColdBoot))
            flags[i] |= QualityColdBoot;
        if (spec.known && (values[i] < spec.lo || values[i] > spec.hi)) flags[i] |= QualityBounds;
    }
    if (!spec.known) return flags;

    // 2) Pic isole. Les voisins retenus sont les points valides les plus proches
    //    qui ne sont pas deja ecartes (une valeur hors bornes ou exclue ne sert
    //    pas de reference). Un point est un pic si :
    //      - ses deux voisins existent, a moins de maxNeighbourGapS ;
    //      - il s'en ecarte DANS LE MEME SENS de plus du seuil, des deux cotes ;
    //      - les deux voisins s'accordent entre eux (ecart < seuil) : sinon c'est
    //        une vraie transition (front, averse), pas un aller-retour.
    auto usable = [&](int k) { return Series::isValid(values[k]) && flags[k] == QualityOk; };
    for (int i = 0; i < n; ++i) {
        if (!usable(i) || keptTs.contains(ts[i])) continue;
        int p = i - 1;
        while (p >= 0 && !usable(p)) --p;
        int q = i + 1;
        while (q < n && !usable(q)) ++q;
        if (p < 0 || q >= n) continue;
        if (ts[i] - ts[p] > rules.maxNeighbourGapS || ts[q] - ts[i] > rules.maxNeighbourGapS)
            continue;
        const double d1 = values[i] - values[p];
        const double d2 = values[i] - values[q];
        const bool sameSide = (d1 > 0 && d2 > 0) || (d1 < 0 && d2 < 0);
        if (!sameSide) continue;
        if (std::fabs(d1) <= spec.spike || std::fabs(d2) <= spec.spike) continue;
        if (std::fabs(values[p] - values[q]) >= spec.spike) continue;
        flags[i] |= QualitySpike;
    }
    return flags;
}

QVector<FlaggedPoint> applyQuality(Series& s, const QVector<TimeRange>& exclusions,
                                   const QualityRules& rules, const KeptPoints& kept) {
    QVector<FlaggedPoint> out;
    const QVector<qint64>& ts = s.timestamps();
    QVector<quint32> src(ts.size());
    for (int i = 0; i < ts.size(); ++i) src[i] = s.sourceFlags(i);
    for (const QString& ch : s.channelNames()) {
        QVector<double>* col = s.mutableChannel(ch);
        if (!col) continue;
        const QVector<quint8> flags = qualifyChannel(ch, ts, *col, exclusions, rules, kept.value(ch), src);
        for (int i = 0; i < flags.size(); ++i) {
            if (flags[i] == QualityOk) continue;
            out.push_back(FlaggedPoint{ts[i], ch, (*col)[i], flags[i]});
            (*col)[i] = Series::missing();
        }
    }
    return out;
}

const char* qualityReasonCode(quint8 flags) {
    if (flags & QualityExcluded) return "exclusion";
    if (flags & QualityColdBoot) return "demarrage";
    if (flags & QualityBounds)   return "bornes";
    if (flags & QualitySpike)    return "pic";
    return "ok";
}

} // namespace meteo
} // namespace morfanalytics
