/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QJsonObject>
#include <functional>

namespace morfanalytics {

class SampleStore;

// -----------------------------------------------------------------------------
// Contexte fourni a une analyse. Le store est CONST : une analyse lit, elle
// n'ecrit jamais — ni dans le cache, ni a plus forte raison sur l'appareil.
// -----------------------------------------------------------------------------
struct AnalysisContext {
    const SampleStore* store = nullptr;
    double altitudeM = 0.0;  // altitude de la station, en metres
    // Une altitude nulle est une valeur LEGITIME (station au bord de mer). On ne
    // peut donc pas deduire de `altitudeM == 0` que le parametre est absent :
    // le fait qu'il ait ete renseigne est porte separement, sans quoi une
    // station au niveau de la mer serait accusee a tort d'etre mal configuree.
    bool   altitudeKnown = false;
    qint64 now = 0;          // instant de reference (secondes Unix)
};

// -----------------------------------------------------------------------------
// IAnalysis : une analyse enfichable.
//
// Le moteur ne connait aucune analyse en particulier : il les execute par leur
// identifiant. Les analyses meteo de ce depot ne sont qu'un jeu parmi d'autres —
// un projet different enregistre les siennes sans toucher au moteur.
//
// Contrat de sortie : un resultat SYNTHETIQUE. Une analyse renvoie une tendance,
// un score, un classement, une poignee de valeurs — jamais un flot de mesures.
// Rapatrier des milliers de points est le travail de l'API d'historique de
// l'appareil, pas celui d'une analyse.
// -----------------------------------------------------------------------------
class IAnalysis {
public:
    virtual ~IAnalysis() = default;

    virtual QString id() const = 0;
    virtual QString title() const = 0;

    // Regroupement pour l'affichage ("nowcast", "climat"...).
    virtual QString group() const = 0;

    // Duree d'historique en dessous de laquelle l'analyse n'a pas de sens. Le
    // moteur s'en sert pour expliquer une indisponibilite plutot que de rendre
    // un resultat calcule sur trois mesures.
    virtual qint64 minimumSpanSeconds() const = 0;

    virtual QJsonObject run(const AnalysisContext& ctx, const QJsonObject& params) const = 0;
};

// -----------------------------------------------------------------------------
// Adaptateur permettant de declarer une analyse a partir d'une simple fonction,
// sans ecrire une classe complete. La plupart des analyses tiennent en une
// fonction : leur imposer une classe n'ajouterait que du bruit.
// -----------------------------------------------------------------------------
class FunctionAnalysis : public IAnalysis {
public:
    using Fn = std::function<QJsonObject(const AnalysisContext&, const QJsonObject&)>;

    FunctionAnalysis(QString id, QString title, QString group,
                     qint64 minimumSpanSeconds, Fn fn)
        : m_id(std::move(id)), m_title(std::move(title)), m_group(std::move(group)),
          m_minSpan(minimumSpanSeconds), m_fn(std::move(fn)) {}

    QString id() const override { return m_id; }
    QString title() const override { return m_title; }
    QString group() const override { return m_group; }
    qint64 minimumSpanSeconds() const override { return m_minSpan; }

    QJsonObject run(const AnalysisContext& ctx, const QJsonObject& params) const override {
        return m_fn(ctx, params);
    }

private:
    QString m_id, m_title, m_group;
    qint64  m_minSpan;
    Fn      m_fn;
};

} // namespace morfanalytics
