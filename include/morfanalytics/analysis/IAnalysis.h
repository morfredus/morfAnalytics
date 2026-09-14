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

// Contexte météo d'une analyse : quelle source de vérité elle interroge.
//   In   = confort intérieur ; Out = météo extérieure ; Both = les deux
//          (modèles étudiant la relation intérieur/extérieur).
// Chaque analyse porte un défaut INTRINSÈQUE à sa nature ; il reste
// surchargeable explicitement (params["ctx"] = "in"|"out"|"both"), mais n'est
// jamais obligatoire ni propagé de force dans le code appelant.
enum class MeteoCtx : quint8 { In, Out, Both };

inline const char* meteoCtxName(MeteoCtx c) {
    switch (c) {
        case MeteoCtx::Out:  return "out";
        case MeteoCtx::Both: return "both";
        default:             return "in";
    }
}

// Analyse un libellé "in"/"out"/"both". Renvoie `fallback` si non reconnu.
inline MeteoCtx meteoCtxFromName(const QString& s, MeteoCtx fallback) {
    if (s == QLatin1String("out"))  return MeteoCtx::Out;
    if (s == QLatin1String("in"))   return MeteoCtx::In;
    if (s == QLatin1String("both")) return MeteoCtx::Both;
    return fallback;
}

// -----------------------------------------------------------------------------
// Contexte fourni a une analyse. Les stores sont CONST : une analyse lit, elle
// n'ecrit jamais — ni dans le cache, ni a plus forte raison sur l'appareil.
//
// `store` est la source PRIMAIRE, résolue par le registre selon le contexte
// effectif de l'analyse (défaut intrinsèque ou surcharge params["ctx"]). Les
// analyses mono-contexte lisent `store` sans rien connaître d'IN/OUT. Celles qui
// étudient la RELATION intérieur/extérieur (ctx Both) accèdent explicitement à
// `storeIn` et `storeOut`. AUCUN repli croisé : si la source primaire est vide,
// l'analyse est déclarée indisponible, jamais recalculée sur l'autre contexte.
// -----------------------------------------------------------------------------
struct AnalysisContext {
    const SampleStore* store = nullptr;     // source primaire résolue
    const SampleStore* storeIn = nullptr;   // cache intérieur (confort)
    const SampleStore* storeOut = nullptr;  // cache extérieur (météo)
    MeteoCtx resolvedCtx = MeteoCtx::In;     // contexte effectivement retenu
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

    // Contexte météo intrinsèque de l'analyse (OUT pour la météo, IN pour le
    // confort, Both pour la relation). Défaut Out : la plupart des analyses de
    // ce dépôt décrivent la météo extérieure.
    virtual MeteoCtx defaultContext() const { return MeteoCtx::Out; }

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
                     qint64 minimumSpanSeconds, Fn fn, MeteoCtx ctx = MeteoCtx::Out)
        : m_id(std::move(id)), m_title(std::move(title)), m_group(std::move(group)),
          m_minSpan(minimumSpanSeconds), m_fn(std::move(fn)), m_ctx(ctx) {}

    QString id() const override { return m_id; }
    QString title() const override { return m_title; }
    QString group() const override { return m_group; }
    MeteoCtx defaultContext() const override { return m_ctx; }
    qint64 minimumSpanSeconds() const override { return m_minSpan; }

    QJsonObject run(const AnalysisContext& ctx, const QJsonObject& params) const override {
        return m_fn(ctx, params);
    }

private:
    QString  m_id, m_title, m_group;
    qint64   m_minSpan;
    Fn       m_fn;
    MeteoCtx m_ctx;
};

} // namespace morfanalytics
