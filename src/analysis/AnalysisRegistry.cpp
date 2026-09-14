/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/AnalysisRegistry.h"
#include "morfanalytics/data/SampleStore.h"

#include <QDateTime>

namespace morfanalytics {

void AnalysisRegistry::add(std::unique_ptr<IAnalysis> analysis) {
    if (analysis)
        m_analyses.push_back(std::move(analysis));
}

const IAnalysis* AnalysisRegistry::find(const QString& id) const {
    for (const auto& a : m_analyses) {
        if (a->id() == id)
            return a.get();
    }
    return nullptr;
}

QJsonArray AnalysisRegistry::catalogJson() const {
    QJsonArray out;
    for (const auto& a : m_analyses) {
        QJsonObject o;
        o["id"]       = a->id();
        o["title"]    = a->title();
        o["group"]    = a->group();
        o["ctx"]      = QString::fromLatin1(meteoCtxName(a->defaultContext()));
        o["min_span_s"] = static_cast<double>(a->minimumSpanSeconds());
        out.append(o);
    }
    return out;
}

QJsonObject AnalysisRegistry::run(const QString& id, const AnalysisContext& ctx,
                                  const QJsonObject& params) const {
    QJsonObject result;
    result["id"] = id;

    const IAnalysis* analysis = find(id);
    if (!analysis) {
        result["ok"]     = false;
        result["reason"] = QStringLiteral("analyse inconnue");
        return result;
    }

    result["title"] = analysis->title();
    result["group"] = analysis->group();

    // Contexte effectif : défaut intrinsèque de l'analyse, surchargeable par
    // params["ctx"] ("in"|"out"|"both") quand c'est pertinent.
    const MeteoCtx eff = meteoCtxFromName(params.value(QStringLiteral("ctx")).toString(),
                                          analysis->defaultContext());
    result["ctx"] = QString::fromLatin1(meteoCtxName(eff));

    // Résolution de la source primaire SANS repli croisé : une analyse météo (OUT)
    // dont le cache extérieur est vide est déclarée indisponible, jamais recalculée
    // en douce sur l'intérieur (et réciproquement). storeIn/storeOut restent
    // exposés pour les analyses de relation (Both).
    const SampleStore* inStore  = ctx.storeIn  ? ctx.storeIn  : ctx.store;
    const SampleStore* outStore = ctx.storeOut;
    AnalysisContext local = ctx;
    local.storeIn  = inStore;
    local.storeOut = outStore;
    local.resolvedCtx = eff;
    switch (eff) {
        case MeteoCtx::Out:  local.store = outStore; break;
        case MeteoCtx::In:   local.store = inStore;  break;
        case MeteoCtx::Both: local.store = outStore ? outStore : inStore; break; // primaire = météo
    }

    if (!local.store || !local.store->isOpen()) {
        result["ok"]     = false;
        result["reason"] = (eff == MeteoCtx::Out)
                               ? QStringLiteral("données extérieures (OUT) indisponibles")
                               : QStringLiteral("cache indisponible");
        return result;
    }

    // Verification de la profondeur d'historique AVANT de lancer le calcul :
    // une moyenne sur trois mesures se presenterait comme un resultat valide,
    // ce qui est plus trompeur qu'une indisponibilite annoncee.
    qint64 firstTs = 0, lastTs = 0;
    if (!local.store->bounds(firstTs, lastTs)) {
        result["ok"]     = false;
        result["reason"] = (eff == MeteoCtx::Out)
                               ? QStringLiteral("aucune donnée extérieure en cache")
                               : QStringLiteral("aucune donnée en cache");
        return result;
    }

    const qint64 span = lastTs - firstTs;
    if (span < analysis->minimumSpanSeconds()) {
        result["ok"]             = false;
        result["reason"]         = QStringLiteral("historique insuffisant");
        result["span_s"]         = static_cast<double>(span);
        result["required_span_s"] = static_cast<double>(analysis->minimumSpanSeconds());
        return result;
    }

    QJsonObject body = analysis->run(local, params);
    // Une analyse peut declarer elle-meme son echec (donnees trop laconiques sur
    // la fenetre demandee, par exemple) ; on ne l'ecrase pas.
    if (!body.contains(QStringLiteral("ok")))
        body["ok"] = true;

    for (auto it = body.constBegin(); it != body.constEnd(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

} // namespace morfanalytics
