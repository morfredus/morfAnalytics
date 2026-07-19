/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include <vector>
#include "morfanalytics/analysis/IAnalysis.h"

namespace morfanalytics {

// -----------------------------------------------------------------------------
// AnalysisRegistry : catalogue des analyses disponibles.
//
// Le registre est generique. Les analyses meteo y sont ajoutees par
// registerMeteoAnalyses() (voir MeteoAnalyses.cpp) ; un autre projet appelle sa
// propre fonction d'enregistrement et reutilise tout le reste tel quel.
// -----------------------------------------------------------------------------
class AnalysisRegistry {
public:
    void add(std::unique_ptr<IAnalysis> analysis);

    // Catalogue au format JSON (id, titre, groupe, historique minimal requis),
    // pour que l'interface se construise sans connaitre les analyses a l'avance.
    QJsonArray catalogJson() const;

    // Execute l'analyse `id`. En cas d'identifiant inconnu ou d'historique
    // insuffisant, renvoie un objet decrivant la raison plutot qu'une erreur
    // muette : une analyse indisponible doit s'expliquer.
    QJsonObject run(const QString& id, const AnalysisContext& ctx,
                    const QJsonObject& params) const;

    int count() const { return static_cast<int>(m_analyses.size()); }

private:
    const IAnalysis* find(const QString& id) const;

    // std::vector et non QVector : les conteneurs Qt exigent un type copiable,
    // ce qu'un unique_ptr n'est pas.
    std::vector<std::unique_ptr<IAnalysis>> m_analyses;
};

// Enregistre le jeu d'analyses meteo (derives, prevision locale, climatologie).
void registerMeteoAnalyses(AnalysisRegistry& registry);

} // namespace morfanalytics
