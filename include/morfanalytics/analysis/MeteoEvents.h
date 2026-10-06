/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QVector>

namespace morfanalytics {
namespace meteo {

// -----------------------------------------------------------------------------
// MeteoEvents : la SOURCE COMMUNE des evenements temporels tires des series.
//
// Meme esprit que MeteoMath : fonctions PURES, pas d'etat, pas de cache, pas de
// JSON. Elles prennent des series brutes (ts croissants, NaN = manquant) et
// rendent des structures. Le JSON et l'affichage sont construits ailleurs
// (AnalyticsModule pour l'API, les analyses pour l'enrichissement de leur texte,
// la page Graphiques pour les marqueurs) : ainsi le calcul d'un croisement ou
// d'un changement de regime est ecrit UNE fois et partage par tous les
// consommateurs, jamais reimplemente differemment.
//
// Trois familles d'evenements, a bien distinguer :
//   - Croisement de VALEURS : deux series de MEME grandeur (Interieur vs
//     Exterieur) deviennent egales. C'est un instant physique reel.
//   - Changement de TENDANCE : une serie passe de hausse/stable/baisse a un
//     autre regime. C'est l'analyse de sa variation dans le temps.
//   - Changement de REGIME : plusieurs changements de tendance rapproches dans
//     le temps, meme entre grandeurs de natures differentes. C'est une relation
//     TEMPORELLE, jamais deduite d'une proximite graphique.
// -----------------------------------------------------------------------------

// Un croisement entre l'Exterieur et l'Interieur d'une meme grandeur.
struct Crossing {
    QString metric;            // "temp" | "hum" | "pres"
    qint64  ts = 0;            // instant interpole (secondes Unix)
    double  value = 0.0;       // valeur commune interpolee a l'egalite
    bool    outRising = true;  // D = OUT - IN passe de - a + : l'exterieur passe
                               // au-dessus de l'interieur (false : repasse sous)
};

enum class Trend : quint8 { Down, Flat, Up };
const char* trendName(Trend t);  // "baisse" | "stable" | "hausse"

// Un changement de tendance d'UNE serie.
struct TrendChange {
    QString metric;
    qint64  ts = 0;                 // instant estime du basculement
    Trend   from = Trend::Flat;
    Trend   to   = Trend::Flat;
};

// Un changement de regime : plusieurs changements de tendance rapproches.
struct RegimeChange {
    qint64 ts = 0;                  // instant estime (centre du groupe)
    QVector<TrendChange> parts;     // les changements de tendance impliques
};

// Une variation brutale LOCALE : en un quart d'heure, la temperature et l'humidite
// partent en sens opposes alors que la pression ne bouge pas. C'est un constat,
// jamais une cause : l'ombre soudaine sur le capteur, un nuage dense, une averse
// locale ou un abri mal ventile donnent la meme signature. Le point de rosee
// aide a lire l'evenement (stable : seul l'effet thermique joue ; en hausse : l'air
// lui-meme s'est humidifie) sans pour autant trancher.
struct SuddenChange {
    qint64 ts = 0;          // fin de la variation (instant de la derniere mesure)
    qint64 startTs = 0;     // debut de la fenetre sur laquelle l'ecart est mesure
    double dTemp = 0.0;     // variation de temperature (degC)
    double dHum  = 0.0;     // variation d'humidite relative (points de %)
    double dPres = 0.0;     // variation de pression (hPa)
    double dDew  = 0.0;     // variation du point de rosee (degC)
};

// Bande morte (eps) d'un croisement, dans l'unite de la grandeur : un croisement
// n'est valide que si l'ecart repart franchement de l'autre cote. Sert aussi
// cote client comme repere de tolerance. Grandeur inconnue -> valeur prudente.
double crossingEps(const QString& metric);

// Detecte les croisements Exterieur/Interieur d'une grandeur. Les series peuvent
// contenir des NaN (mesures manquantes) et des trous : aucun croisement n'est
// invente au travers d'un vrai silence de capteur. L'instant et la valeur sont
// interpoles entre les deux mesures qui encadrent l'egalite.
//
// Pas de croisement de PRESSION (renvoie vide) : interieur et exterieur baignent
// dans le meme air, et MeteoHub publie les deux pressions deja ramenees au
// niveau de la mer. Leurs courbes se confondent ; qu'elles se croisent ne
// traduit qu'un ecart entre capteurs, jamais un evenement meteo. Un tel ecart
// releverait d'un controle de coherence des capteurs, pas des evenements.
QVector<Crossing> detectCrossings(const QString& metric,
                                  const QVector<qint64>& tsOut, const QVector<double>& vOut,
                                  const QVector<qint64>& tsIn,  const QVector<double>& vIn);

// Detecte les changements de tendance d'une serie. Travaille sur des MOYENNES
// HORAIRES (lissage) pour ne pas transformer chaque fluctuation du capteur en
// changement de regime. Les segments trop courts sont fusionnes.
QVector<TrendChange> detectTrendChanges(const QString& metric,
                                        const QVector<qint64>& ts, const QVector<double>& v);

// Regroupe des changements de tendance proches dans le temps en changements de
// regime. Un groupe n'est retenu que s'il implique AU MOINS DEUX grandeurs
// distinctes (sinon ce n'est qu'un changement de tendance isole).
QVector<RegimeChange> detectRegimeChanges(const QVector<TrendChange>& changes,
                                          qint64 windowS = 90 * 60);

// Detecte les variations brutales locales d'une serie EXTERIEURE (les trois
// grandeurs partagent les memes instants, NaN = manquant). Seuils fixes (voir le
// .cpp) : volontairement simples, a affiner avec l'experience. Les detections
// voisines (moins de 30 min) sont fusionnees en gardant la plus forte.
QVector<SuddenChange> detectSuddenChanges(const QVector<qint64>& ts, const QVector<double>& temp,
                                          const QVector<double>& hum, const QVector<double>& pres);

} // namespace meteo
} // namespace morfanalytics
