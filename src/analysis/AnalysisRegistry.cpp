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

    if (!ctx.store || !ctx.store->isOpen()) {
        result["ok"]     = false;
        result["reason"] = QStringLiteral("cache indisponible");
        return result;
    }

    // Verification de la profondeur d'historique AVANT de lancer le calcul :
    // une moyenne sur trois mesures se presenterait comme un resultat valide,
    // ce qui est plus trompeur qu'une indisponibilite annoncee.
    qint64 firstTs = 0, lastTs = 0;
    if (!ctx.store->bounds(firstTs, lastTs)) {
        result["ok"]     = false;
        result["reason"] = QStringLiteral("aucune donnée en cache");
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

    QJsonObject body = analysis->run(ctx, params);
    // Une analyse peut declarer elle-meme son echec (donnees trop laconiques sur
    // la fenetre demandee, par exemple) ; on ne l'ecrase pas.
    if (!body.contains(QStringLiteral("ok")))
        body["ok"] = true;

    for (auto it = body.constBegin(); it != body.constEnd(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

} // namespace morfanalytics
