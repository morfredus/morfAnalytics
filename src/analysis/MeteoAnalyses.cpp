/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/AnalysisRegistry.h"
#include "morfanalytics/analysis/MeteoMath.h"
#include "morfanalytics/analysis/ForecastQuality.h"
#include "morfanalytics/analysis/MeteoEvents.h"
#include "morfanalytics/data/SampleStore.h"
#include "morfanalytics/data/ForecastStore.h"
#include "morfanalytics/data/Series.h"

#include <QDateTime>
#include <QDate>
#include <QJsonArray>
#include <QMap>
#include <QHash>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace morfanalytics {

namespace {

constexpr qint64 kHour = 3600;
constexpr qint64 kDay  = 86400;

const QString kTemp = QStringLiteral("temp");
const QString kHum  = QStringLiteral("hum");
const QString kPres = QStringLiteral("pres");

// --- Petits utilitaires de lecture de serie --------------------------------

// Index de la derniere valeur renseignee d'un canal, -1 si le canal est vide.
// Les trous sont normaux (panne capteur, coupure) : on ne suppose jamais que la
// derniere ligne de la serie porte une mesure valide.
int lastValidIndex(const QVector<double>& v) {
    for (int i = v.size() - 1; i >= 0; --i) {
        if (Series::isValid(v[i]))
            return i;
    }
    return -1;
}

// Valeur d'un canal au plus pres d'un instant donne, dans une tolerance. Renvoie
// NaN si aucune mesure valide n'est assez proche - la comparaison "il y a 3 h"
// n'a pas de sens si la mesure la plus proche date de la veille.
double valueNear(const Series& series, const QVector<double>& channel,
                 qint64 target, qint64 tolerance) {
    const QVector<qint64>& ts = series.timestamps();
    double best = Series::missing();
    qint64 bestGap = tolerance + 1;
    for (int i = 0; i < ts.size(); ++i) {
        if (!Series::isValid(channel[i]))
            continue;
        const qint64 gap = std::llabs(ts[i] - target);
        if (gap <= tolerance && gap < bestGap) {
            bestGap = gap;
            best = channel[i];
        }
    }
    return best;
}

// --- Agregation journaliere -------------------------------------------------

struct DayAggregate {
    QDate  date;
    double tMin = 0, tMax = 0; int tCount = 0;

    // Moyenne journaliere au sens meteorologique : (min + max) / 2, et non la
    // moyenne de toutes les mesures. C'est la definition utilisee pour les
    // normales et les degres-jours, sur laquelle sont calibrees les references.
    double tMidRange() const { return tCount ? (tMin + tMax) / 2.0 : std::nan(""); }
};

// Regroupe une serie par journee CIVILE LOCALE. Le decoupage doit suivre les
// jours vecus (minimum de la nuit, maximum de l'apres-midi), pas UTC, sans quoi
// un maximum de fin de journee basculerait dans le jour suivant.
QVector<DayAggregate> aggregateByDay(const Series& series) {
    QMap<QDate, DayAggregate> byDate;

    const QVector<qint64>& ts = series.timestamps();
    const QVector<double>* temp = series.channel(kTemp);

    for (int i = 0; i < ts.size(); ++i) {
        const QDate date = QDateTime::fromSecsSinceEpoch(ts[i]).date();
        DayAggregate& day = byDate[date];
        day.date = date;

        if (temp && Series::isValid((*temp)[i])) {
            const double v = (*temp)[i];
            if (day.tCount == 0) { day.tMin = v; day.tMax = v; }
            else { day.tMin = std::min(day.tMin, v); day.tMax = std::max(day.tMax, v); }
            day.tCount++;
        }
    }

    QVector<DayAggregate> out;
    out.reserve(byDate.size());
    for (auto it = byDate.constBegin(); it != byDate.constEnd(); ++it)
        out.push_back(it.value());
    return out; // QMap itere dans l'ordre des cles : deja trie par date
}

// Fenetre demandee par l'appelant, en jours, bornee pour eviter qu'une requete
// ne charge dix ans de mesures en memoire.
qint64 windowDays(const QJsonObject& params, int fallback) {
    const int d = params.value(QStringLiteral("days")).toInt(fallback);
    return std::clamp(d, 1, 3650);
}

QJsonObject failure(const QString& reason) {
    QJsonObject o;
    o["ok"]     = false;
    o["reason"] = reason;
    return o;
}

// Arrondi d'affichage : inutile de publier une pression a 10 decimales, la
// mesure n'a pas cette precision.
double round1(double v) { return std::round(v * 10.0) / 10.0; }
double round2(double v) { return std::round(v * 100.0) / 100.0; }

// --- Enrichissement par les evenements (source COMMUNE : MeteoEvents) --------
// Les analyses ne recalculent pas ces evenements a leur facon : elles appellent
// exactement les memes fonctions que les Graphiques et la page /meteohub/events.

QString metricNameFr(const QString& metric) {
    if (metric == kTemp) return QStringLiteral("Température");
    if (metric == kHum)  return QStringLiteral("Humidité");
    if (metric == kPres) return QStringLiteral("Pression");
    return metric;
}

// Ajoute le dernier changement de tendance d'une serie (le plus recent de la
// fenetre) sous la cle "trend_shift" : { ts, from, to }.
void addLastTrendShift(QJsonObject& o, const QString& metric, const Series& s) {
    const QVector<double>* ch = s.channel(metric);
    if (!ch) return;
    const auto tc = meteo::detectTrendChanges(metric, s.timestamps(), *ch);
    if (tc.isEmpty()) return;
    const auto& last = tc.last();
    QJsonObject j;
    j["ts"]   = static_cast<double>(last.ts);
    j["from"] = QString::fromUtf8(meteo::trendName(last.from));
    j["to"]   = QString::fromUtf8(meteo::trendName(last.to));
    o["trend_shift"] = j;
}

// Ajoute le dernier changement de regime (bascule rapprochee de >= 2 grandeurs)
// sous la cle "regime_shift" : { ts, parts:[{metric_name, from, to}] }.
void addLastRegimeShift(QJsonObject& o, const Series& s, const QStringList& metrics) {
    QVector<meteo::TrendChange> all;
    for (const QString& m : metrics) {
        const QVector<double>* ch = s.channel(m);
        if (ch) all += meteo::detectTrendChanges(m, s.timestamps(), *ch);
    }
    const auto rc = meteo::detectRegimeChanges(all);
    if (rc.isEmpty()) return;
    const auto& last = rc.last();
    QJsonObject j;
    j["ts"] = static_cast<double>(last.ts);
    QJsonArray parts;
    for (const auto& p : last.parts) {
        QJsonObject pj;
        pj["metric_name"] = metricNameFr(p.metric);
        pj["from"] = QString::fromUtf8(meteo::trendName(p.from));
        pj["to"]   = QString::fromUtf8(meteo::trendName(p.to));
        parts.append(pj);
    }
    j["parts"] = parts;
    o["regime_shift"] = j;
}

// Ajoute le dernier croisement IN/OUT d'une grandeur sous la cle "last_crossing"
// : { ts, value, out_rising }.
void addLastCrossing(QJsonObject& o, const QString& metric,
                     const Series& outS, const Series& inS) {
    const QVector<double>* oc = outS.channel(metric);
    const QVector<double>* ic = inS.channel(metric);
    if (!oc || !ic) return;
    const auto cr = meteo::detectCrossings(metric, outS.timestamps(), *oc,
                                           inS.timestamps(), *ic);
    if (cr.isEmpty()) return;
    const auto& last = cr.last();
    QJsonObject j;
    j["ts"]         = static_cast<double>(last.ts);
    j["value"]      = round1(last.value);
    j["out_rising"] = last.outRising;
    o["last_crossing"] = j;
}

// Mediane d'un ensemble de valeurs (la copie est triee en place). Robuste aux
// valeurs aberrantes, contrairement a la moyenne : c'est le socle des
// statistiques de la vague 3 (anomalies par MAD, notamment).
double median(std::vector<double> v) {
    if (v.empty()) return std::nan("");
    const std::size_t n = v.size();
    std::nth_element(v.begin(), v.begin() + n / 2, v.end());
    const double hi = v[n / 2];
    if (n % 2 == 1) return hi;
    std::nth_element(v.begin(), v.begin() + n / 2 - 1, v.begin() + n / 2);
    return (v[n / 2 - 1] + hi) / 2.0;
}

// ===========================================================================
//  VAGUE 1 - derives instantanes et prevision locale
// ===========================================================================

// Etat courant enrichi : ce que les capteurs ne mesurent pas directement mais
// qui se deduit de leurs mesures (rosee, humidite absolue, ressenti, pression
// reduite au niveau de la mer).
QJsonObject analyzeCurrent(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 6 * kHour, ctx.now);
    const QVector<double>* temp = series.channel(kTemp);
    const QVector<double>* hum  = series.channel(kHum);
    const QVector<double>* pres = series.channel(kPres);
    if (!temp || !hum || !pres)
        return failure(QStringLiteral("canaux manquants"));

    const int i = lastValidIndex(*temp);
    if (i < 0)
        return failure(QStringLiteral("aucune mesure récente"));

    const double t = (*temp)[i];
    const double h = Series::isValid((*hum)[i]) ? (*hum)[i] : std::nan("");
    const double p = Series::isValid((*pres)[i]) ? (*pres)[i] : std::nan("");

    QJsonObject o;
    o["ts"]          = static_cast<double>(series.timestamps()[i]);
    o["temperature"] = round1(t);

    if (!std::isnan(h)) {
        const double td = meteo::dewPoint(t, h);
        o["humidity"]          = round1(h);
        o["dew_point"]         = round1(td);
        o["absolute_humidity"] = round2(meteo::absoluteHumidity(t, h));
        o["humidex"]           = round1(meteo::humidex(t, h));
        // Ecart au point de rosee : plus il est faible, plus l'air est proche de
        // la saturation (buee, brouillard, condensation sur les parois froides).
        o["dew_point_spread"]  = round1(t - td);
    }
    if (!std::isnan(p)) {
        // Pression au niveau de la mer, telle que publiee par MeteoHub.
        o["pressure"] = round1(p);
    }
    return o;
}

// Chaleur ressentie : l'humidex combine temperature et humidite, ce qui rend
// visible un risque que le thermometre seul sous-estime quand l'air est lourd.
QJsonObject analyzeHeatRisk(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 6 * kHour, ctx.now);
    const QVector<double>* temp = series.channel(kTemp);
    const QVector<double>* hum  = series.channel(kHum);
    if (!temp || !hum)
        return failure(QStringLiteral("canaux manquants"));

    const int i = lastValidIndex(*temp);
    if (i < 0 || !Series::isValid((*hum)[i]))
        return failure(QStringLiteral("aucune mesure récente"));

    const double t = (*temp)[i];
    const double h = (*hum)[i];
    const double hx = meteo::humidex(t, h);
    QString level = QStringLiteral("faible");
    if (hx >= 45.0)      level = QStringLiteral("très élevé");
    else if (hx >= 40.0) level = QStringLiteral("élevé");
    else if (hx >= 35.0) level = QStringLiteral("modéré");

    QJsonObject o;
    o["temperature"] = round1(t);
    o["humidity"]    = round1(h);
    o["humidex"]     = round1(hx);
    o["risk"]        = level;
    // Bascule conjointe temperature/humidite sur 12 h : « rechauffement accompagne
    // d'une baisse de l'humidite depuis environ HH:MM » (source commune).
    const Series win = ctx.store->range(ctx.now - 12 * kHour, ctx.now);
    addLastRegimeShift(o, win, {kTemp, kHum});
    o["note"] = QStringLiteral(
        "Indicateur local temperature-humidite. Il ne remplace pas une "
        "vigilance canicule officielle ni un avis medical.");
    return o;
}

// Secheresse de l'air : le deficit de pression de vapeur quantifie le pouvoir
// evaporant de l'air. C'est un precurseur utile pour la vegetation, mais pas
// un indice de danger de feu : pluie, vent et etat des sols manquent ici.
QJsonObject analyzeDryAirRisk(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 6 * kHour, ctx.now);
    const QVector<double>* temp = series.channel(kTemp);
    const QVector<double>* hum  = series.channel(kHum);
    if (!temp || !hum)
        return failure(QStringLiteral("canaux manquants"));

    const int i = lastValidIndex(*temp);
    if (i < 0 || !Series::isValid((*hum)[i]))
        return failure(QStringLiteral("aucune mesure récente"));

    const double t = (*temp)[i];
    const double h = (*hum)[i];
    const double vpd = meteo::vaporPressureDeficit(t, h);
    QString level = QStringLiteral("faible");
    if (vpd >= 2.0)      level = QStringLiteral("très sec");
    else if (vpd >= 1.2) level = QStringLiteral("sec");
    else if (vpd >= 0.7) level = QStringLiteral("modéré");

    QJsonObject o;
    o["temperature"] = round1(t);
    o["humidity"]    = round1(h);
    o["vpd_kpa"]     = round2(vpd);
    o["risk"]        = level;
    o["note"] = QStringLiteral(
        "Secheresse atmospherique locale, pas un niveau officiel de danger de "
        "feu. La pluie recente, le vent et l'etat de la vegetation ne sont pas mesures.");
    return o;
}

// Tendance barometrique sur 3 h, code OMM, et alerte de chute rapide.
QJsonObject analyzePressureTrend(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 12 * kHour, ctx.now);
    const QVector<double>* pres = series.channel(kPres);
    if (!pres)
        return failure(QStringLiteral("canaux manquants"));

    const int i = lastValidIndex(*pres);
    if (i < 0)
        return failure(QStringLiteral("aucune mesure de pression récente"));

    // Pression deja ramenee au niveau de la mer par MeteoHub.
    const qint64 nowTs = series.timestamps()[i];
    const double pNow = (*pres)[i];

    // Tolerance de 30 min autour de la cible : une mesure par minute suffit
    // largement, mais un trou d'acquisition ne doit pas invalider la tendance.
    const double p3 = valueNear(series, *pres, nowTs - 3 * kHour, 30 * 60);
    const double p1 = valueNear(series, *pres, nowTs - 1 * kHour, 30 * 60);

    QJsonObject o;
    o["pressure"] = round1(pNow);
    if (std::isnan(p3))
        return failure(QStringLiteral("historique de pression insuffisant sur 3 h"));

    const double delta3h = pNow - p3;
    o["delta_3h"] = round1(delta3h);
    o["tendency"] = meteo::pressureTendencyLabel(delta3h);

    if (!std::isnan(p1)) {
        const double delta1h = pNow - p1;
        o["delta_1h"] = round1(delta1h);
        // Seuil classique d'alerte : une chute d'environ 1,6 hPa en une heure
        // signale un creux qui se creuse vite (orage, coup de vent).
        o["storm_warning"] = (delta1h <= -1.6);
        if (delta1h <= -1.6)
            o["storm_note"] = QStringLiteral(
                "Chute de pression rapide : risque d'orage ou de coup de vent "
                "dans les heures qui viennent.");
    }
    // Dernier basculement de tendance barometrique sur la fenetre (source commune).
    addLastTrendShift(o, kPres, series);
    return o;
}

// Prevision locale Zambretti.
QJsonObject analyzeZambretti(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 12 * kHour, ctx.now);
    const QVector<double>* pres = series.channel(kPres);
    if (!pres)
        return failure(QStringLiteral("canaux manquants"));

    const int i = lastValidIndex(*pres);
    if (i < 0)
        return failure(QStringLiteral("aucune mesure de pression récente"));

    // Zambretti est calibre sur la pression au niveau de la mer : c'est
    // exactement ce que publie MeteoHub, utilisee telle quelle.
    const qint64 nowTs = series.timestamps()[i];
    const double pNow = (*pres)[i];

    const double p3 = valueNear(series, *pres, nowTs - 3 * kHour, 30 * 60);
    if (std::isnan(p3))
        return failure(QStringLiteral("historique de pression insuffisant sur 3 h"));

    const double delta3h = pNow - p3;
    const int month = QDateTime::fromSecsSinceEpoch(nowTs).date().month();

    QJsonObject o;
    o["forecast"] = meteo::zambrettiForecast(pNow, delta3h, month);
    o["pressure"] = round1(pNow);
    o["delta_3h"] = round1(delta3h);
    o["tendency"] = meteo::pressureTendencyLabel(delta3h);
    o["note"] = QStringLiteral(
        "Prévision locale pour les 12 à 24 heures à venir, déduite de la seule "
        "pression. Le vent n'étant pas mesuré, elle reste indicative.");
    return o;
}

// Tendance de temperature : variations recentes et rythme actuel. Pendant de la
// tendance barometrique, cote thermometre : dire si l'air se rechauffe ou se
// refroidit, et a quelle vitesse, sans attendre le bulletin.
QJsonObject analyzeTempTrend(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 25 * kHour, ctx.now);
    const QVector<double>* temp = series.channel(kTemp);
    if (!temp)
        return failure(QStringLiteral("canal température manquant"));

    const int i = lastValidIndex(*temp);
    if (i < 0)
        return failure(QStringLiteral("aucune mesure récente"));

    const qint64 nowTs = series.timestamps()[i];
    const double t = (*temp)[i];

    const double t1  = valueNear(series, *temp, nowTs - 1 * kHour,  30 * 60);
    const double t3  = valueNear(series, *temp, nowTs - 3 * kHour,  30 * 60);
    const double t24 = valueNear(series, *temp, nowTs - 24 * kHour, 45 * 60);
    if (std::isnan(t3))
        return failure(QStringLiteral("historique de température insuffisant sur 3 h"));

    const double delta3h = t - t3;

    QJsonObject o;
    o["temperature"] = round1(t);
    o["delta_3h"]    = round1(delta3h);
    if (!std::isnan(t1))
        o["delta_1h"] = round1(t - t1);
    // L'ecart a la meme heure hier separe l'evolution du temps (masse d'air) du
    // simple cycle jour/nuit, que delta_3h ne distingue pas.
    if (!std::isnan(t24))
        o["delta_24h"] = round1(t - t24);

    QString label;
    if (delta3h >= 2.0)       label = QStringLiteral("réchauffement rapide");
    else if (delta3h >= 0.5)  label = QStringLiteral("réchauffement");
    else if (delta3h <= -2.0) label = QStringLiteral("refroidissement rapide");
    else if (delta3h <= -0.5) label = QStringLiteral("refroidissement");
    else                      label = QStringLiteral("stable");
    o["tendency"] = label;
    // Repere temporel : le dernier basculement de tendance sur la fenetre (source
    // commune avec les Graphiques). Permet de dire « a change de regime vers HH:MM ».
    addLastTrendShift(o, kTemp, series);
    o["note"] = QStringLiteral(
        "Variation sur 3 h pour la tendance ; l'écart à la même heure la veille "
        "distingue un changement de masse d'air du simple cycle jour/nuit.");
    return o;
}

// Risque de brouillard : ecart au point de rosee faible et qui se resserre.
QJsonObject analyzeFogRisk(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 6 * kHour, ctx.now);
    const QVector<double>* temp = series.channel(kTemp);
    const QVector<double>* hum  = series.channel(kHum);
    if (!temp || !hum)
        return failure(QStringLiteral("canaux manquants"));

    const int i = lastValidIndex(*temp);
    if (i < 0 || !Series::isValid((*hum)[i]))
        return failure(QStringLiteral("aucune mesure récente"));

    const qint64 nowTs = series.timestamps()[i];
    const double t = (*temp)[i];
    const double spread = t - meteo::dewPoint(t, (*hum)[i]);

    // Evolution de l'ecart sur 2 h : un ecart faible mais stable est moins
    // propice qu'un ecart qui se resserre, signe que l'air approche la saturation.
    const double t2 = valueNear(series, *temp, nowTs - 2 * kHour, 45 * 60);
    const double h2 = valueNear(series, *hum,  nowTs - 2 * kHour, 45 * 60);
    double spreadTrend = std::nan("");
    if (!std::isnan(t2) && !std::isnan(h2))
        spreadTrend = spread - (t2 - meteo::dewPoint(t2, h2));

    QString level = QStringLiteral("faible");
    if (spread < 1.0)      level = QStringLiteral("élevé");
    else if (spread < 2.5) level = QStringLiteral("modéré");

    // Un resserrement net rehausse d'un cran un risque encore modere.
    if (!std::isnan(spreadTrend) && spreadTrend < -0.5 && spread < 4.0
        && level == QStringLiteral("faible"))
        level = QStringLiteral("modéré");

    QJsonObject o;
    o["dew_point_spread"] = round1(spread);
    if (!std::isnan(spreadTrend))
        o["spread_trend_2h"] = round1(spreadTrend);
    o["risk"] = level;
    o["note"] = QStringLiteral(
        "Estimé à partir du seul écart au point de rosée. Le vent et la "
        "couverture nuageuse, non mesurés, pèsent aussi sur la formation du "
        "brouillard.");
    return o;
}

// Risque de gelee : extrapolation du refroidissement en cours vers le petit matin.
QJsonObject analyzeFrostRisk(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 12 * kHour, ctx.now);
    const QVector<double>* temp = series.channel(kTemp);
    const QVector<double>* hum  = series.channel(kHum);
    if (!temp || !hum)
        return failure(QStringLiteral("canaux manquants"));

    const int i = lastValidIndex(*temp);
    if (i < 0)
        return failure(QStringLiteral("aucune mesure récente"));

    const qint64 nowTs = series.timestamps()[i];
    const double t = (*temp)[i];
    const double t3 = valueNear(series, *temp, nowTs - 3 * kHour, 45 * 60);
    if (std::isnan(t3))
        return failure(QStringLiteral("historique insuffisant sur 3 h"));

    const QDateTime nowDt = QDateTime::fromSecsSinceEpoch(nowTs);
    const int hour = nowDt.time().hour();
    const double coolingPerHour = (t - t3) / 3.0;

    // Heures restantes jusqu'au minimum, situe grossierement au lever du jour
    // (6 h locale). En journee, l'extrapolation n'a pas de sens : le sol se
    // rechauffe, la nuit n'a pas commence.
    double hoursToDawn = 0;
    if (hour >= 18)      hoursToDawn = (24 - hour) + 6;
    else if (hour < 6)   hoursToDawn = 6 - hour;

    QJsonObject o;
    o["temperature"] = round1(t);
    o["cooling_per_hour"] = round2(coolingPerHour);

    if (hoursToDawn <= 0) {
        o["risk"] = QStringLiteral("hors période");
        o["note"] = QStringLiteral(
            "Estimation disponible seulement en soirée et de nuit : elle "
            "extrapole le refroidissement nocturne en cours.");
        return o;
    }

    // Le refroidissement ralentit en fin de nuit et le point de rosee agit comme
    // un plancher : en atteignant la saturation, la condensation libere de la
    // chaleur et freine la baisse. On borne donc l'extrapolation par la rosee.
    double projected = t + coolingPerHour * hoursToDawn * 0.7;
    if (Series::isValid((*hum)[i])) {
        const double td = meteo::dewPoint(t, (*hum)[i]);
        o["dew_point"] = round1(td);
        projected = std::max(projected, td - 1.0);
    }

    QString level = QStringLiteral("nul");
    if (projected <= 0.0)      level = QStringLiteral("élevé");
    else if (projected <= 2.0) level = QStringLiteral("modéré");
    else if (projected <= 4.0) level = QStringLiteral("faible");

    o["projected_min"] = round1(projected);
    o["hours_to_dawn"] = hoursToDawn;
    o["risk"] = level;
    o["note"] = QStringLiteral(
        "Extrapolation du refroidissement observé, bornée par le point de rosée. "
        "Un ciel se dégageant ou se couvrant modifie sensiblement le résultat.");
    return o;
}

// ===========================================================================
//  VAGUE 2 - climatologie
// ===========================================================================

// Normale glissante du jour de l'annee et ecart du jour a cette normale.
QJsonObject analyzeNormals(const AnalysisContext& ctx, const QJsonObject& params) {
    // Toute la profondeur disponible : c'est le propre d'une normale.
    qint64 first = 0, last = 0;
    ctx.store->bounds(first, last);
    const Series series = ctx.store->range(first, last);
    const QVector<DayAggregate> days = aggregateByDay(series);
    if (days.isEmpty())
        return failure(QStringLiteral("aucune donnée exploitable"));

    const QDate today = QDateTime::fromSecsSinceEpoch(ctx.now).date();
    // Fenetre de +/- N jours autour du jour de l'annee : une normale calculee sur
    // la seule date exacte reposerait sur une poignee de valeurs et sauterait
    // dans tous les sens d'un jour a l'autre.
    const int halfWindow = std::clamp(params.value(QStringLiteral("window_days")).toInt(7), 1, 30);

    double sum = 0; int count = 0;
    double minSeen = 0, maxSeen = 0; bool first_ = true;
    QSet<int> years;

    for (const DayAggregate& d : days) {
        if (d.tCount == 0)
            continue;
        // Distance en jours de l'annee, en tenant compte du passage de decembre
        // a janvier (le 31 decembre est a 2 jours du 2 janvier).
        int diff = std::abs(d.date.dayOfYear() - today.dayOfYear());
        diff = std::min(diff, 365 - diff);
        if (diff > halfWindow)
            continue;
        if (d.date == today)
            continue; // le jour en cours ne participe pas a sa propre normale

        const double mid = d.tMidRange();
        sum += mid; count++;
        years.insert(d.date.year());
        if (first_) { minSeen = maxSeen = mid; first_ = false; }
        else { minSeen = std::min(minSeen, mid); maxSeen = std::max(maxSeen, mid); }
    }

    if (count == 0)
        return failure(QStringLiteral("aucune donnée autour de cette date les années précédentes"));

    const double normal = sum / count;

    QJsonObject o;
    o["normal_temp"]  = round1(normal);
    o["sample_days"]  = count;
    o["years"]        = years.size();
    o["window_days"]  = halfWindow;
    o["normal_min"]   = round1(minSeen);
    o["normal_max"]   = round1(maxSeen);

    // Jour en cours, s'il est deja renseigne.
    for (const DayAggregate& d : days) {
        if (d.date == today && d.tCount > 0) {
            const double mid = d.tMidRange();
            o["today_temp"]  = round1(mid);
            o["anomaly"]     = round1(mid - normal);
            break;
        }
    }

    if (years.size() < 3)
        o["warning"] = QStringLiteral(
            "Normale calculée sur moins de trois années : elle décrit surtout "
            "l'historique disponible, pas encore un climat de référence.");
    return o;
}

// Degres-jours unifies : chauffage (base 18) et climatisation (base 26).
QJsonObject analyzeDegreeDays(const AnalysisContext& ctx, const QJsonObject& params) {
    const qint64 days_n = windowDays(params, 365);
    const Series series = ctx.store->range(ctx.now - days_n * kDay, ctx.now);
    const QVector<DayAggregate> days = aggregateByDay(series);
    if (days.isEmpty())
        return failure(QStringLiteral("aucune donnée exploitable"));

    const double heatingBase = params.value(QStringLiteral("heating_base")).toDouble(18.0);
    const double coolingBase = params.value(QStringLiteral("cooling_base")).toDouble(26.0);

    double heating = 0, cooling = 0;
    int counted = 0;
    QMap<QString, double> heatingByMonth;

    for (const DayAggregate& d : days) {
        if (d.tCount == 0)
            continue;
        const double mid = d.tMidRange();
        const double h = std::max(0.0, heatingBase - mid);
        heating += h;
        cooling += std::max(0.0, mid - coolingBase);
        heatingByMonth[d.date.toString(QStringLiteral("yyyy-MM"))] += h;
        counted++;
    }

    QJsonObject months;
    for (auto it = heatingByMonth.constBegin(); it != heatingByMonth.constEnd(); ++it)
        months.insert(it.key(), round1(it.value()));

    QJsonObject o;
    o["heating_degree_days"] = round1(heating);
    o["cooling_degree_days"] = round1(cooling);
    o["heating_base"] = heatingBase;
    o["cooling_base"] = coolingBase;
    o["days_counted"] = counted;
    o["heating_by_month"] = months;
    o["note"] = QStringLiteral(
        "Méthode des moyennes : (minimum + maximum) / 2 comparé à la base. "
        "Les jours sans mesure ne sont pas comptés, ce qui sous-estime un total "
        "si l'historique comporte des trous.");
    return o;
}

// Amplitude thermique diurne : ecart entre maximum et minimum du jour.
QJsonObject analyzeDiurnalAmplitude(const AnalysisContext& ctx, const QJsonObject& params) {
    const qint64 days_n = windowDays(params, 90);
    const Series series = ctx.store->range(ctx.now - days_n * kDay, ctx.now);
    const QVector<DayAggregate> days = aggregateByDay(series);
    if (days.isEmpty())
        return failure(QStringLiteral("aucune donnée exploitable"));

    double sum = 0; int count = 0;
    double maxAmp = -1e9, minAmp = 1e9;
    QDate maxDate, minDate;

    for (const DayAggregate& d : days) {
        // Une journee tres incomplete donnerait une amplitude trompeuse (par ex.
        // deux mesures prises dans la meme heure).
        if (d.tCount < 100)
            continue;
        const double amp = d.tMax - d.tMin;
        sum += amp; count++;
        if (amp > maxAmp) { maxAmp = amp; maxDate = d.date; }
        if (amp < minAmp) { minAmp = amp; minDate = d.date; }
    }

    if (count == 0)
        return failure(QStringLiteral("aucune journée suffisamment complète"));

    QJsonObject o;
    o["mean_amplitude"] = round1(sum / count);
    o["days_counted"]   = count;
    o["max_amplitude"]  = round1(maxAmp);
    o["max_date"]       = maxDate.toString(Qt::ISODate);
    o["min_amplitude"]  = round1(minAmp);
    o["min_date"]       = minDate.toString(Qt::ISODate);
    o["note"] = QStringLiteral(
        "Seules les journées comptant au moins 100 mesures sont retenues, afin "
        "qu'une journée tronquée ne passe pas pour une journée sans amplitude.");
    return o;
}

// Records absolus sur la profondeur disponible.
QJsonObject analyzeRecords(const AnalysisContext& ctx, const QJsonObject&) {
    qint64 first = 0, last = 0;
    ctx.store->bounds(first, last);
    const Series series = ctx.store->range(first, last);

    const QVector<qint64>& ts = series.timestamps();
    QJsonObject o;

    auto recordsFor = [&](const QString& channel, const QString& label) {
        const QVector<double>* v = series.channel(channel);
        if (!v)
            return;
        double lo = 0, hi = 0; qint64 loTs = 0, hiTs = 0; bool started = false;
        for (int i = 0; i < v->size(); ++i) {
            if (!Series::isValid((*v)[i]))
                continue;
            const double x = (*v)[i];
            if (!started) { lo = hi = x; loTs = hiTs = ts[i]; started = true; continue; }
            if (x < lo) { lo = x; loTs = ts[i]; }
            if (x > hi) { hi = x; hiTs = ts[i]; }
        }
        if (!started)
            return;
        QJsonObject r;
        r["min"] = round1(lo);
        r["min_ts"] = static_cast<double>(loTs);
        r["max"] = round1(hi);
        r["max_ts"] = static_cast<double>(hiTs);
        o[label] = r;
    };

    recordsFor(kTemp, QStringLiteral("temperature"));
    recordsFor(kHum,  QStringLiteral("humidity"));
    recordsFor(kPres, QStringLiteral("pressure"));

    o["from_ts"] = static_cast<double>(first);
    o["to_ts"]   = static_cast<double>(last);
    if (o.isEmpty())
        return failure(QStringLiteral("aucune donnée exploitable"));
    return o;
}

// Series de jours remarquables : gel, forte chaleur, nuits tropicales.
QJsonObject analyzeStreaks(const AnalysisContext& ctx, const QJsonObject& params) {
    const qint64 days_n = windowDays(params, 365);
    const Series series = ctx.store->range(ctx.now - days_n * kDay, ctx.now);
    const QVector<DayAggregate> days = aggregateByDay(series);
    if (days.isEmpty())
        return failure(QStringLiteral("aucune donnée exploitable"));

    struct Counter {
        int total = 0;
        int current = 0;
        int longest = 0;
        void feed(bool hit) {
            if (hit) { total++; current++; longest = std::max(longest, current); }
            else current = 0;
        }
    };
    Counter frost, hot, tropicalNight, veryHot;

    for (const DayAggregate& d : days) {
        if (d.tCount == 0)
            continue;
        frost.feed(d.tMin < 0.0);          // jour de gel
        hot.feed(d.tMax > 25.0);           // journee chaude
        veryHot.feed(d.tMax > 30.0);       // forte chaleur
        tropicalNight.feed(d.tMin > 20.0); // nuit tropicale
    }

    auto pack = [](const Counter& c) {
        QJsonObject o;
        o["days"] = c.total;
        o["longest_streak"] = c.longest;
        return o;
    };

    QJsonObject o;
    o["frost_days"]      = pack(frost);
    o["hot_days"]        = pack(hot);
    o["very_hot_days"]   = pack(veryHot);
    o["tropical_nights"] = pack(tropicalNight);
    o["days_counted"]    = days.size();
    o["thresholds"] = QStringLiteral(
        "Gel : minimum < 0 °C. Journée chaude : maximum > 25 °C. Forte chaleur : "
        "maximum > 30 °C. Nuit tropicale : minimum > 20 °C.");
    return o;
}

// Cycle journalier moyen : temperature moyenne par heure de la journee.
QJsonObject analyzeDailyCycle(const AnalysisContext& ctx, const QJsonObject& params) {
    const qint64 days_n = windowDays(params, 30);
    const Series series = ctx.store->range(ctx.now - days_n * kDay, ctx.now);
    const QVector<double>* temp = series.channel(kTemp);
    if (!temp)
        return failure(QStringLiteral("canal température manquant"));

    double sums[24] = {0};
    int counts[24] = {0};
    QSet<QDate> daysSeen;
    const QVector<qint64>& ts = series.timestamps();
    for (int i = 0; i < ts.size(); ++i) {
        if (!Series::isValid((*temp)[i]))
            continue;
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(ts[i]);
        sums[dt.time().hour()] += (*temp)[i];
        counts[dt.time().hour()]++;
        daysSeen.insert(dt.date());
    }

    QJsonArray hours;
    int hottest = -1, coldest = -1;
    double hottestV = -1e9, coldestV = 1e9;
    for (int h = 0; h < 24; ++h) {
        if (counts[h] == 0) {
            hours.append(QJsonValue::Null);
            continue;
        }
        const double mean = sums[h] / counts[h];
        hours.append(round1(mean));
        if (mean > hottestV) { hottestV = mean; hottest = h; }
        if (mean < coldestV) { coldestV = mean; coldest = h; }
    }

    if (hottest < 0)
        return failure(QStringLiteral("aucune mesure exploitable"));

    QJsonObject o;
    o["hourly_mean"]   = hours;
    o["warmest_hour"]  = hottest;
    o["warmest_temp"]  = round1(hottestV);
    o["coldest_hour"]  = coldest;
    o["coldest_temp"]  = round1(coldestV);
    o["amplitude"]     = round1(hottestV - coldestV);
    // Jours reellement observes, pas la largeur de la fenetre demandee : sur un
    // historique plus court que la fenetre, les deux divergent.
    o["days_counted"]  = daysSeen.size();
    return o;
}

// Completude de la collecte : ce que le cache detient reellement.
QJsonObject analyzeDataQuality(const AnalysisContext& ctx, const QJsonObject& params) {
    const qint64 days_n = windowDays(params, 30);
    const Series series = ctx.store->range(ctx.now - days_n * kDay, ctx.now);
    const QVector<DayAggregate> days = aggregateByDay(series);
    if (days.isEmpty())
        return failure(QStringLiteral("aucune donnée exploitable"));

    // Nombre de mesures attendues par jour, INFERE des donnees et non fige : la
    // sonde est passee de 1 mesure/min (1440/j) a 1 toutes les 5 min (288/j), et
    // la cadence pourra encore changer. On la deduit du delta MEDIAN entre mesures
    // consecutives (robuste aux trous), puis attendues/j = 86400 / cadence.
    const QVector<qint64>& tsv = series.timestamps();
    qint64 medianSec = 60;                       // repli prudent : cadence 1 min
    if (tsv.size() >= 10) {
        QVector<qint64> deltas;
        deltas.reserve(tsv.size());
        for (int i = 1; i < tsv.size(); ++i) {
            const qint64 dt = tsv[i] - tsv[i - 1];
            if (dt > 0) deltas.append(dt);
        }
        if (!deltas.isEmpty()) {
            std::sort(deltas.begin(), deltas.end());
            medianSec = deltas[deltas.size() / 2];
        }
    }
    if (medianSec <= 0) medianSec = 60;
    const int expectedPerDay = qBound(1, static_cast<int>(qRound(86400.0 / medianSec)), 86400);

    int completeDays = 0, partialDays = 0;
    QJsonArray gaps;

    // Les journees des deux extremites sont tronquees par la fenetre elle-meme,
    // pas par un defaut de collecte : les signaler comme incompletes produirait
    // deux fausses alertes a chaque execution.
    const QDate firstDay = days.first().date;
    const QDate lastDay  = days.last().date;

    for (const DayAggregate& d : days) {
        if (d.date == firstDay || d.date == lastDay)
            continue;
        const double ratio = static_cast<double>(d.tCount) / expectedPerDay;
        if (ratio >= 0.95) completeDays++;
        else {
            partialDays++;
            if (gaps.size() < 20) { // on ne noie pas l'interface sous les trous
                QJsonObject g;
                g["date"] = d.date.toString(Qt::ISODate);
                g["measures"] = d.tCount;
                g["completeness"] = round2(ratio * 100.0);
                gaps.append(g);
            }
        }
    }

    QJsonObject o;
    o["days_seen"]      = completeDays + partialDays;
    o["complete_days"]  = completeDays;
    o["partial_days"]   = partialDays;
    o["incomplete"]     = gaps;
    o["expected_per_day"] = expectedPerDay;
    const int cadenceMin = qMax(1, static_cast<int>(qRound(medianSec / 60.0)));
    o["cadence_minutes"] = cadenceMin;
    o["note"] = QStringLiteral(
        "Une journée est dite complète au-delà de 95 % des %1 mesures attendues "
        "(cadence détectée : ~%2 min entre deux mesures). Les journées de début et "
        "de fin de fenêtre sont exclues, étant tronquées par la fenêtre elle-même. "
        "Une journée partielle n'est pas une anomalie : coupure, carte SD absente "
        "ou capteur en défaut suffisent à l'expliquer.")
        .arg(expectedPerDay).arg(cadenceMin);
    return o;
}

} // namespace

// ===========================================================================
//  VAGUE 3 - analyses avancees (anomalies, correlations, episodes)
// ===========================================================================

// Detection d'anomalies par z-score ROBUSTE (MAD). La moyenne et l'ecart-type
// classiques sont eux-memes fausses par les valeurs aberrantes qu'on cherche a
// reperer ; la mediane et l'ecart absolu median (MAD) ne le sont pas. Le score
// d'Iglewicz-Hoaglin, z = 0.6745*(x - mediane)/MAD, depasse ~3.5 pour un point
// franchement atypique.
//
// Sortie SYNTHETIQUE : on ne renvoie PAS un flot de points, seulement les plus
// extremes (plafonnes), accompagnes des statistiques (mediane, MAD, comptage)
// qui permettent de juger. Params : `days` (fenetre, defaut 30), `threshold`
// (seuil |z|, defaut 3.5).
QJsonObject analyzeAnomalies(const AnalysisContext& ctx, const QJsonObject& params) {
    const qint64 days      = windowDays(params, 30);
    const double threshold = std::clamp(params.value(QStringLiteral("threshold")).toDouble(3.5),
                                        1.0, 20.0);
    const Series series = ctx.store->range(ctx.now - days * kDay, ctx.now);
    const QVector<qint64>& ts = series.timestamps();
    if (ts.size() < 20)
        return failure(QStringLiteral("historique insuffisant pour des statistiques robustes"));

    constexpr int kMaxReported = 10;
    const std::vector<QString> channels = {kTemp, kHum, kPres};

    QJsonObject out;
    out["window_days"] = static_cast<int>(days);
    out["threshold"]   = threshold;
    QJsonObject chansJson;
    int totalAnomalies = 0;

    for (const QString& key : channels) {
        const QVector<double>* col = series.channel(key);
        if (!col) continue;

        // Valeurs valides et l'index d'origine (pour retrouver l'horodatage).
        std::vector<double> values;
        std::vector<int> origin;
        for (int i = 0; i < col->size(); ++i)
            if (Series::isValid((*col)[i])) { values.push_back((*col)[i]); origin.push_back(i); }
        if (values.size() < 20) continue;

        const double med = median(values);
        std::vector<double> absdev;
        absdev.reserve(values.size());
        for (double v : values) absdev.push_back(std::fabs(v - med));
        const double mad = median(absdev);

        QJsonObject cj;
        cj["count"]  = static_cast<int>(values.size());
        cj["median"] = round2(med);
        cj["mad"]    = round2(mad);

        QJsonArray anomalies;
        int anomaliesCount = 0;
        // MAD nul = serie (quasi) constante : aucun point n'est "atypique", et
        // diviser par zero n'aurait aucun sens. On le signale honnetement.
        if (mad > 1e-9) {
            std::vector<std::pair<double, int>> flagged;  // (z, index dans values)
            for (std::size_t k = 0; k < values.size(); ++k) {
                const double z = 0.6745 * (values[k] - med) / mad;
                if (std::fabs(z) > threshold) flagged.push_back({z, static_cast<int>(k)});
            }
            anomaliesCount = static_cast<int>(flagged.size());
            std::sort(flagged.begin(), flagged.end(),
                      [](const auto& a, const auto& b) { return std::fabs(a.first) > std::fabs(b.first); });
            for (int n = 0; n < static_cast<int>(flagged.size()) && n < kMaxReported; ++n) {
                const int k = flagged[n].second;
                QJsonObject a;
                a["ts"]        = static_cast<double>(ts[origin[k]]);
                a["value"]     = round2(values[k]);
                a["z"]         = round2(flagged[n].first);
                a["direction"] = flagged[n].first > 0 ? QStringLiteral("haut")
                                                      : QStringLiteral("bas");
                anomalies.append(a);
            }
        } else {
            cj["note"] = QStringLiteral("série constante (MAD nul) : aucune anomalie définissable");
        }
        cj["anomalies_count"]    = anomaliesCount;
        cj["anomalies_reported"] = anomalies.size();
        cj["anomalies"]          = anomalies;
        totalAnomalies += anomaliesCount;
        chansJson[key] = cj;
    }

    if (chansJson.isEmpty())
        return failure(QStringLiteral("aucun canal exploitable"));
    out["channels"]        = chansJson;
    out["total_anomalies"] = totalAnomalies;
    return out;
}

// Reechantillonne une serie sur une grille HORAIRE dense : une case par heure
// entre la premiere et la derniere mesure, contenant la moyenne du canal sur
// l'heure, ou NaN si l'heure n'a aucune mesure. Aligner sur une grille reguliere
// est indispensable a une correlation a decalage : sans elle, un "decalage de N
// heures" n'aurait pas de sens sur des mesures espacees irregulierement.
struct HourlyGrid {
    QVector<double> temp, hum, pres;   // meme longueur, NaN pour une heure vide
    qint64 firstHour = 0;              // numero d'heure Unix de la case 0 (ts/3600)
    int size() const { return temp.size(); }
};
HourlyGrid hourlyGrid(const Series& series) {
    HourlyGrid g;
    const QVector<qint64>& ts = series.timestamps();
    if (ts.isEmpty()) return g;
    const qint64 firstHour = ts.first() / kHour;
    const qint64 lastHour  = ts.last()  / kHour;
    g.firstHour = firstHour;
    const int n = static_cast<int>(lastHour - firstHour) + 1;
    if (n <= 0 || n > 24 * 3660) return g;   // garde-fou memoire

    const QVector<double>* cols[3] = {series.channel(kTemp), series.channel(kHum),
                                      series.channel(kPres)};
    QVector<double>* dst[3] = {&g.temp, &g.hum, &g.pres};
    for (int c = 0; c < 3; ++c) {
        QVector<double> sum(n, 0.0);
        QVector<int>    cnt(n, 0);
        if (cols[c]) {
            for (int i = 0; i < ts.size(); ++i) {
                if (!Series::isValid((*cols[c])[i])) continue;
                const int b = static_cast<int>(ts[i] / kHour - firstHour);
                if (b >= 0 && b < n) { sum[b] += (*cols[c])[i]; cnt[b]++; }
            }
        }
        dst[c]->resize(n);
        for (int b = 0; b < n; ++b)
            (*dst[c])[b] = cnt[b] ? sum[b] / cnt[b] : std::nan("");
    }
    return g;
}

// Correlation de Pearson entre a[i] et b[i+lag], sur les seules cases ou les deux
// sont presentes. Renvoie NaN si trop peu de panneaux valides pour etre credible.
double pearsonLag(const QVector<double>& a, const QVector<double>& b, int lag) {
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    int n = 0;
    for (int i = 0; i < a.size(); ++i) {
        const int j = i + lag;
        if (j < 0 || j >= b.size()) continue;
        if (std::isnan(a[i]) || std::isnan(b[j])) continue;
        sa += a[i]; sb += b[j];
        saa += a[i] * a[i]; sbb += b[j] * b[j]; sab += a[i] * b[j];
        ++n;
    }
    if (n < 12) return std::nan("");
    const double cov = sab - sa * sb / n;
    const double va  = saa - sa * sa / n;
    const double vb  = sbb - sb * sb / n;
    if (va <= 0 || vb <= 0) return std::nan("");
    return cov / std::sqrt(va * vb);
}

// Correlations a decalage temporel entre grandeurs : au-dela du lien instantane,
// on cherche le decalage qui MAXIMISE la correlation (ex. une chute de pression
// qui PRECEDE une hausse d'humidite). Params : `days` (fenetre, defaut 30),
// `max_lag_hours` (defaut 12).
QJsonObject analyzeLaggedCorrelation(const AnalysisContext& ctx, const QJsonObject& params) {
    const qint64 days = windowDays(params, 30);
    const int maxLag  = std::clamp(params.value(QStringLiteral("max_lag_hours")).toInt(12), 1, 72);
    const Series series = ctx.store->range(ctx.now - days * kDay, ctx.now);
    const HourlyGrid g = hourlyGrid(series);
    if (g.size() < 24)
        return failure(QStringLiteral("historique insuffisant (moins d'une journée exploitable)"));

    struct Pair { const char* label; const QVector<double>* a; const QVector<double>* b; };
    const std::vector<Pair> pairs = {
        {"temp~hum",  &g.temp, &g.hum},
        {"temp~pres", &g.temp, &g.pres},
        {"hum~pres",  &g.hum,  &g.pres},
    };

    QJsonArray arr;
    for (const Pair& p : pairs) {
        const double r0 = pearsonLag(*p.a, *p.b, 0);
        double bestR = 0; int bestLag = 0; bool found = false;
        for (int lag = -maxLag; lag <= maxLag; ++lag) {
            const double r = pearsonLag(*p.a, *p.b, lag);
            if (std::isnan(r)) continue;
            if (!found || std::fabs(r) > std::fabs(bestR)) { bestR = r; bestLag = lag; found = true; }
        }
        if (!found) continue;
        QJsonObject o;
        o["pair"]          = QString::fromUtf8(p.label);
        o["r_at_zero"]     = std::isnan(r0) ? QJsonValue() : QJsonValue(round2(r0));
        o["best_r"]        = round2(bestR);
        o["best_lag_hours"] = bestLag;   // >0 : le 2e canal suit le 1er de N heures
        arr.append(o);
    }
    if (arr.isEmpty())
        return failure(QStringLiteral("aucune paire exploitable"));

    QJsonObject out;
    out["window_days"]   = static_cast<int>(days);
    out["max_lag_hours"] = maxLag;
    out["note"]          = QStringLiteral(
        "best_lag_hours > 0 : le second canal suit le premier de N heures "
        "(le premier « précède »). La corrélation n'est pas une causalité.");
    out["pairs"] = arr;
    return out;
}

// Segmentation d'episodes remarquables : suites de jours consecutifs ou une
// grandeur reste au-dela d'un seuil. Canicule (Tmax >= seuil chaud) et coup de
// froid (Tmin <= seuil froid). Params : `days` (fenetre, defaut 90),
// `heat_threshold` (defaut 30), `cold_threshold` (defaut 0), `min_days` (defaut 3).
QJsonObject analyzeEpisodes(const AnalysisContext& ctx, const QJsonObject& params) {
    const qint64 days   = windowDays(params, 90);
    const double heatT  = params.value(QStringLiteral("heat_threshold")).toDouble(30.0);
    const double coldT  = params.value(QStringLiteral("cold_threshold")).toDouble(0.0);
    const int    minDays = std::clamp(params.value(QStringLiteral("min_days")).toInt(3), 1, 60);
    const Series series = ctx.store->range(ctx.now - days * kDay, ctx.now);
    const QVector<DayAggregate> agg = aggregateByDay(series);
    if (agg.size() < minDays)
        return failure(QStringLiteral("historique insuffisant"));

    QJsonArray episodes;
    // Detecte les suites de jours CONSECUTIFS qui satisfont `qualifies`, et emet
    // un episode quand la suite atteint `minDays`. `extreme` suit la valeur la
    // plus marquante de l'episode (max en chaleur, min en froid).
    const auto scan = [&](const char* type,
                          const std::function<bool(const DayAggregate&)>& qualifies,
                          const std::function<double(const DayAggregate&)>& value,
                          bool wantMax) {
        int runStart = -1;
        double extreme = 0;
        auto flush = [&](int endExclusive) {
            if (runStart < 0) return;
            const int len = endExclusive - runStart;
            if (len >= minDays) {
                QJsonObject e;
                e["type"]    = QString::fromUtf8(type);
                e["start"]   = agg[runStart].date.toString(Qt::ISODate);
                e["end"]     = agg[endExclusive - 1].date.toString(Qt::ISODate);
                e["days"]    = len;
                e["extreme"] = round1(extreme);
                episodes.append(e);
            }
            runStart = -1;
        };
        for (int i = 0; i < agg.size(); ++i) {
            const bool ok = agg[i].tCount > 0 && qualifies(agg[i]);
            // Rupture de continuite du calendrier : un trou de jours casse la suite.
            const bool contiguous = runStart >= 0 &&
                agg[i - 1].date.daysTo(agg[i].date) == 1;
            if (ok && runStart >= 0 && !contiguous) flush(i);
            if (ok) {
                if (runStart < 0) { runStart = i; extreme = value(agg[i]); }
                else extreme = wantMax ? std::max(extreme, value(agg[i]))
                                       : std::min(extreme, value(agg[i]));
            } else {
                flush(i);
            }
        }
        flush(agg.size());
    };

    scan("canicule", [&](const DayAggregate& d){ return d.tMax >= heatT; },
         [](const DayAggregate& d){ return d.tMax; }, /*wantMax=*/true);
    scan("froid",    [&](const DayAggregate& d){ return d.tMin <= coldT; },
         [](const DayAggregate& d){ return d.tMin; }, /*wantMax=*/false);

    QJsonObject out;
    out["window_days"]    = static_cast<int>(days);
    out["heat_threshold"] = heatT;
    out["cold_threshold"] = coldT;
    out["min_days"]       = minDays;
    out["episodes"]       = episodes;   // vide = aucun episode sur la periode, ce n'est pas une erreur
    return out;
}

// Decomposition additive tendance / saison / residu d'un canal, sur grille
// horaire, saison journaliere (24 h). Distincte de `daily_cycle` (forme moyenne
// du jour) : ici on SEPARE la tendance multi-jours (rechauffement/refroidissement
// de fond, une fois le cycle journalier retire) du residu (ce qui n'est explique
// ni par la tendance ni par la saison), et on chiffre la FORCE de chaque
// composante (diagnostics a la STL, Hyndman). Params : `days` (defaut 14),
// `channel` (temp/hum/pres, defaut temp).
QJsonObject analyzeDecomposition(const AnalysisContext& ctx, const QJsonObject& params) {
    const qint64 days = windowDays(params, 14);
    const QString channel = params.value(QStringLiteral("channel")).toString(kTemp);
    const Series series = ctx.store->range(ctx.now - days * kDay, ctx.now);
    const HourlyGrid g = hourlyGrid(series);

    const QVector<double>* pv = channel == kHum ? &g.hum
                              : channel == kPres ? &g.pres : &g.temp;
    const QVector<double>& v = *pv;
    const int n = v.size();
    constexpr int P = 24;   // periode saisonniere : le cycle journalier
    if (n < 2 * P) return failure(QStringLiteral("historique insuffisant (moins de deux jours)"));

    // Tendance : moyenne mobile centree sur 24 h, trous ignores ; NaN si trop peu
    // de points valides dans la fenetre.
    QVector<double> trend(n, std::nan(""));
    for (int i = 0; i < n; ++i) {
        double s = 0; int c = 0;
        for (int j = i - P / 2; j <= i + P / 2; ++j)
            if (j >= 0 && j < n && !std::isnan(v[j])) { s += v[j]; ++c; }
        if (c >= P / 2) trend[i] = s / c;
    }

    // Profil saisonnier = moyenne de (v - tendance) par heure du jour (UTC), centre.
    QVector<double> seasonProfile(P, 0.0);
    QVector<int>    seasonCount(P, 0);
    for (int i = 0; i < n; ++i) {
        if (std::isnan(v[i]) || std::isnan(trend[i])) continue;
        const int h = static_cast<int>((g.firstHour + i) % P);
        seasonProfile[h] += v[i] - trend[i];
        seasonCount[h]   += 1;
    }
    double profMean = 0; int profN = 0;
    for (int h = 0; h < P; ++h)
        if (seasonCount[h] > 0) { seasonProfile[h] /= seasonCount[h]; profMean += seasonProfile[h]; ++profN; }
    if (profN > 0) profMean /= profN;
    for (int h = 0; h < P; ++h)
        seasonProfile[h] = seasonCount[h] > 0 ? seasonProfile[h] - profMean : 0.0;

    // Residu, deseasonnalise (tendance+residu) et detrended (saison+residu).
    QVector<double> resid(n, std::nan("")), deseason(n, std::nan("")), detrend(n, std::nan(""));
    for (int i = 0; i < n; ++i) {
        if (std::isnan(v[i])) continue;
        const double s = seasonProfile[static_cast<int>((g.firstHour + i) % P)];
        deseason[i] = v[i] - s;
        if (!std::isnan(trend[i])) { detrend[i] = v[i] - trend[i]; resid[i] = v[i] - trend[i] - s; }
    }
    auto varOf = [](const QVector<double>& x) -> double {
        double m = 0; int c = 0;
        for (double e : x) if (!std::isnan(e)) { m += e; ++c; }
        if (c < 2) return std::nan("");
        m /= c; double s = 0;
        for (double e : x) if (!std::isnan(e)) s += (e - m) * (e - m);
        return s / c;
    };
    const double vr = varOf(resid), vTR = varOf(deseason), vSR = varOf(detrend);
    auto strength = [](double vres, double vcomp) -> double {
        if (std::isnan(vres) || std::isnan(vcomp) || vcomp <= 0) return 0.0;
        return std::clamp(1.0 - vres / vcomp, 0.0, 1.0);
    };

    // Pente de tendance : regression lineaire de trend[i] sur i (heures), *24 -> /jour.
    double sx=0, sy=0, sxx=0, sxy=0; int cnt=0;
    for (int i = 0; i < n; ++i)
        if (!std::isnan(trend[i])) { sx+=i; sy+=trend[i]; sxx+=double(i)*i; sxy+=double(i)*trend[i]; ++cnt; }
    double slopePerDay = std::nan(""), trendStart = std::nan(""), trendEnd = std::nan("");
    if (cnt >= 2) { const double d = cnt*sxx - sx*sx; if (d != 0) slopePerDay = (cnt*sxy - sx*sy) / d * 24.0; }
    for (int i = 0; i < n; ++i)   if (!std::isnan(trend[i])) { trendStart = trend[i]; break; }
    for (int i = n-1; i >= 0; --i) if (!std::isnan(trend[i])) { trendEnd = trend[i]; break; }

    double smin = 1e18, smax = -1e18;
    for (int h = 0; h < P; ++h)
        if (seasonCount[h] > 0) { smin = std::min(smin, seasonProfile[h]); smax = std::max(smax, seasonProfile[h]); }

    QJsonObject out;
    out["channel"]     = channel;
    out["window_days"] = static_cast<int>(days);
    QJsonObject tr;
    if (!std::isnan(slopePerDay)) tr["slope_per_day"] = round2(slopePerDay);
    if (!std::isnan(trendStart))  tr["start"] = round2(trendStart);
    if (!std::isnan(trendEnd))    tr["end"]   = round2(trendEnd);
    if (!std::isnan(trendStart) && !std::isnan(trendEnd)) tr["change"] = round2(trendEnd - trendStart);
    out["trend"] = tr;
    QJsonObject se;
    se["period_hours"] = P;
    se["amplitude"]    = (smax >= smin) ? round2(smax - smin) : 0.0;
    QJsonArray prof;
    for (int h = 0; h < P; ++h) prof.append(round2(seasonProfile[h]));
    se["profile_utc"] = prof;   // 24 decalages, indexes par heure UTC, somme ~ 0
    out["seasonal"] = se;
    out["residual_std"] = std::isnan(vr) ? QJsonValue() : QJsonValue(round2(std::sqrt(vr)));
    QJsonObject st;
    st["trend"]    = round2(strength(vr, vTR));
    st["seasonal"] = round2(strength(vr, vSR));
    out["strength"] = st;
    return out;
}

// Comportement thermique du bâtiment : première analyse de RELATION (contexte
// Both, lit les DEUX caches). Compare l'intérieur (confort) à l'extérieur (météo)
// pour caractériser comment le bâti filtre les variations du dehors :
//   - l'écart intérieur/extérieur courant ;
//   - l'AMORTISSEMENT : amplitude intérieure / amplitude extérieure sur la
//     fenêtre (plus il est faible, plus le bâtiment tamponne) ;
//   - le DÉCALAGE : le retard horaire où l'intérieur suit le mieux l'extérieur
//     (inertie thermique), par corrélation à décalage sur des moyennes horaires.
// Aucun repli croisé : les deux séries gardent leur provenance, on ne mélange
// jamais une mesure intérieure et une mesure extérieure.
QJsonObject analyzeThermalBehaviour(const AnalysisContext& ctx, const QJsonObject& params) {
    if (!ctx.storeIn || !ctx.storeOut || !ctx.storeIn->isOpen() || !ctx.storeOut->isOpen())
        return failure(QStringLiteral("les deux caches (intérieur ET extérieur) sont nécessaires"));

    int hours = params.value(QStringLiteral("hours")).toInt(48);
    if (hours < 12)  hours = 12;
    if (hours > 240) hours = 240;
    const qint64 from = ctx.now - static_cast<qint64>(hours) * kHour;

    const Series outS = ctx.storeOut->range(from, ctx.now);
    const Series inS  = ctx.storeIn->range(from, ctx.now);
    const QVector<double>* outT = outS.channel(kTemp);
    const QVector<double>* inT  = inS.channel(kTemp);
    if (!outT || !inT)
        return failure(QStringLiteral("canal température manquant"));

    const int oi = lastValidIndex(*outT);
    const int ii = lastValidIndex(*inT);
    if (oi < 0) return failure(QStringLiteral("aucune mesure extérieure récente"));
    if (ii < 0) return failure(QStringLiteral("aucune mesure intérieure récente"));

    const double outNow = (*outT)[oi];
    const double inNow  = (*inT)[ii];

    // Amplitudes (max - min des valeurs valides) sur la fenêtre.
    double outMin = 1e9, outMax = -1e9, inMin = 1e9, inMax = -1e9;
    int outN = 0, inN = 0;
    for (double v : *outT) if (Series::isValid(v)) {
        if (v < outMin) outMin = v; if (v > outMax) outMax = v; outN++;
    }
    for (double v : *inT) if (Series::isValid(v)) {
        if (v < inMin) inMin = v; if (v > inMax) inMax = v; inN++;
    }
    if (outN < 3 || inN < 3)
        return failure(QStringLiteral("historique insuffisant sur la fenêtre"));
    const double outAmp = outMax - outMin;
    const double inAmp  = inMax - inMin;

    // Moyennes horaires (indexées par heure Unix) pour la corrélation à décalage.
    auto hourlyMeans = [](const Series& s, const QVector<double>& ch) {
        QHash<qint64, double> sum;
        QHash<qint64, int>    cnt;
        const auto& ts = s.timestamps();
        for (int k = 0; k < ch.size(); ++k) {
            if (!Series::isValid(ch[k])) continue;
            const qint64 h = ts[k] / kHour;
            sum[h] += ch[k];
            cnt[h] += 1;
        }
        QHash<qint64, double> means;
        for (auto it = sum.constBegin(); it != sum.constEnd(); ++it)
            means[it.key()] = it.value() / cnt[it.key()];
        return means;
    };
    const QHash<qint64, double> outH = hourlyMeans(outS, *outT);
    const QHash<qint64, double> inH  = hourlyMeans(inS, *inT);

    // Corrélation de Pearson entre IN[h] et OUT[h - lag], lag de 0 à maxLag heures.
    const int maxLag = 8;
    int bestLag = 0;
    double bestR = -2.0;
    for (int lag = 0; lag <= maxLag; ++lag) {
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        int n = 0;
        for (auto it = inH.constBegin(); it != inH.constEnd(); ++it) {
            const auto oit = outH.constFind(it.key() - lag);
            if (oit == outH.constEnd()) continue;
            const double x = oit.value(), y = it.value();
            sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; ++n;
        }
        if (n < 6) continue;
        const double cov = sxy - sx * sy / n;
        const double vx  = sxx - sx * sx / n;
        const double vy  = syy - sy * sy / n;
        if (vx <= 0 || vy <= 0) continue;
        const double r = cov / std::sqrt(vx * vy);
        if (r > bestR) { bestR = r; bestLag = lag; }
    }

    QJsonObject o;
    o["outdoor_temp"]      = round1(outNow);
    o["indoor_temp"]       = round1(inNow);
    o["gap"]               = round1(inNow - outNow); // >0 : intérieur plus chaud
    o["window_hours"]      = hours;
    o["outdoor_amplitude"] = round1(outAmp);
    o["indoor_amplitude"]  = round1(inAmp);
    if (outAmp > 0.2) {
        const double damping = inAmp / outAmp; // <1 : le bâti amortit
        o["damping"] = round2(damping);
        o["inertia"] = damping < 0.3 ? QStringLiteral("forte inertie (bâtiment tampon)")
                     : damping < 0.6 ? QStringLiteral("inertie moyenne")
                                     : QStringLiteral("faible inertie (suit l'extérieur)");
    }
    if (bestR > -2.0) {
        o["lag_hours"]   = bestLag;
        o["correlation"] = round2(bestR);
    }
    // Croisement IN/OUT le plus recent : l'instant ou l'exterieur rejoint puis
    // depasse (ou repasse sous) l'interieur, repere-cle de l'inertie thermique.
    addLastCrossing(o, kTemp, outS, inS);
    o["note"] = QStringLiteral(
        "Relation intérieur/extérieur. L'amortissement compare l'amplitude "
        "intérieure à l'extérieure (plus il est faible, plus le bâtiment tamponne). "
        "Le décalage est le retard horaire où l'intérieur suit le mieux l'extérieur.");
    return o;
}

// Modèle d'inertie intérieure : analyse de RELATION (contexte Both). Prolonge
// thermal_behaviour en un vrai MODÈLE PRÉDICTIF simple : la température intérieure
// est ajustée comme une fonction linéaire de l'extérieur DÉCALÉ de l'inertie du
// bâti, T_in(h) ≈ offset + gain · T_out(h - lag). Le lag est le décalage de
// meilleure corrélation ; gain et offset sont estimés par régression sur les
// moyennes horaires. On expose la qualité d'ajustement (R², RMSE, MAE) et, à titre
// de repère, la température intérieure PRÉDITE par le modèle « maintenant » face au
// réel. Aucun repli croisé : chaque série garde sa provenance.
QJsonObject analyzeIndoorInertiaModel(const AnalysisContext& ctx, const QJsonObject& params) {
    if (!ctx.storeIn || !ctx.storeOut || !ctx.storeIn->isOpen() || !ctx.storeOut->isOpen())
        return failure(QStringLiteral("les deux caches (intérieur ET extérieur) sont nécessaires"));

    int hours = params.value(QStringLiteral("hours")).toInt(72);
    if (hours < 24)  hours = 24;
    if (hours > 336) hours = 336;
    const qint64 from = ctx.now - static_cast<qint64>(hours) * kHour;

    const Series outS = ctx.storeOut->range(from, ctx.now);
    const Series inS  = ctx.storeIn->range(from, ctx.now);
    const QVector<double>* outT = outS.channel(kTemp);
    const QVector<double>* inT  = inS.channel(kTemp);
    if (!outT || !inT)
        return failure(QStringLiteral("canal température manquant"));

    const int ii = lastValidIndex(*inT);
    if (ii < 0) return failure(QStringLiteral("aucune mesure intérieure récente"));
    const double inNow = (*inT)[ii];

    // Moyennes horaires (indexées par heure Unix), comme thermal_behaviour.
    auto hourlyMeans = [](const Series& s, const QVector<double>& ch) {
        QHash<qint64, double> sum;
        QHash<qint64, int>    cnt;
        const auto& ts = s.timestamps();
        for (int k = 0; k < ch.size(); ++k) {
            if (!Series::isValid(ch[k])) continue;
            const qint64 h = ts[k] / kHour;
            sum[h] += ch[k];
            cnt[h] += 1;
        }
        QHash<qint64, double> means;
        for (auto it = sum.constBegin(); it != sum.constEnd(); ++it)
            means[it.key()] = it.value() / cnt[it.key()];
        return means;
    };
    const QHash<qint64, double> outH = hourlyMeans(outS, *outT);
    const QHash<qint64, double> inH  = hourlyMeans(inS, *inT);

    // 1) Lag = décalage (0..maxLag h) de meilleure corrélation IN[h] vs OUT[h-lag].
    const int maxLag = 12;
    int bestLag = 0;
    double bestR = -2.0;
    for (int lag = 0; lag <= maxLag; ++lag) {
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
        int n = 0;
        for (auto it = inH.constBegin(); it != inH.constEnd(); ++it) {
            const auto oit = outH.constFind(it.key() - lag);
            if (oit == outH.constEnd()) continue;
            const double x = oit.value(), y = it.value();
            sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; ++n;
        }
        if (n < 6) continue;
        const double cov = sxy - sx * sy / n;
        const double vx  = sxx - sx * sx / n;
        const double vy  = syy - sy * sy / n;
        if (vx <= 0 || vy <= 0) continue;
        const double r = cov / std::sqrt(vx * vy);
        if (r > bestR) { bestR = r; bestLag = lag; }
    }
    if (bestR <= -2.0)
        return failure(QStringLiteral("historique horaire insuffisant pour ajuster le modèle"));

    // 2) Régression linéaire IN[h] = offset + gain · OUT[h - bestLag] au lag retenu.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (auto it = inH.constBegin(); it != inH.constEnd(); ++it) {
        const auto oit = outH.constFind(it.key() - bestLag);
        if (oit == outH.constEnd()) continue;
        const double x = oit.value(), y = it.value();
        sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
    }
    const double meanX = sx / n, meanY = sy / n;
    const double vx = sxx - sx * sx / n;
    if (n < 6 || vx <= 0)
        return failure(QStringLiteral("variance extérieure trop faible pour ajuster le modèle"));
    const double gain   = (sxy - sx * sy / n) / vx;
    const double offset = meanY - gain * meanX;

    // 3) Qualité d'ajustement : R², RMSE, MAE des résidus (réel - prédit).
    double ssRes = 0, ssTot = 0, absSum = 0;
    for (auto it = inH.constBegin(); it != inH.constEnd(); ++it) {
        const auto oit = outH.constFind(it.key() - bestLag);
        if (oit == outH.constEnd()) continue;
        const double pred = offset + gain * oit.value();
        const double resid = it.value() - pred;
        ssRes += resid * resid;
        ssTot += (it.value() - meanY) * (it.value() - meanY);
        absSum += std::abs(resid);
    }
    const double r2   = ssTot > 0 ? 1.0 - ssRes / ssTot : 0.0;
    const double rmse = std::sqrt(ssRes / n);
    const double mae  = absSum / n;

    QJsonObject o;
    o["window_hours"] = hours;
    o["lag_hours"]    = bestLag;
    o["gain"]         = round2(gain);     // °C intérieur par °C extérieur (≈ amortissement)
    o["offset"]       = round1(offset);   // apport propre (chauffage / occupation / soleil)
    o["fit_r2"]       = round2(r2);
    o["rmse"]         = round2(rmse);
    o["mae"]          = round2(mae);
    o["indoor_temp"]  = round1(inNow);

    // Prédiction « maintenant » : extérieur d'il y a bestLag heures, si disponible.
    const qint64 nowH = ctx.now / kHour;
    const auto oitNow = outH.constFind(nowH - bestLag);
    if (oitNow != outH.constEnd()) {
        const double pred = offset + gain * oitNow.value();
        o["predicted_indoor"] = round1(pred);
        o["residual"]         = round1(inNow - pred); // >0 : plus chaud que le modèle
    }

    o["model_quality"] = r2 >= 0.75 ? QStringLiteral("modèle fiable")
                       : r2 >= 0.4  ? QStringLiteral("modèle indicatif")
                                    : QStringLiteral("modèle faible (autres facteurs dominants)");
    // Dernier croisement IN/OUT : la transition de phase (Ext < In -> Ext = In ->
    // Ext > In) sert de repere au decalage d'inertie.
    addLastCrossing(o, kTemp, outS, inS);
    o["note"] = QStringLiteral(
        "Modèle d'inertie : la température intérieure est estimée à partir de "
        "l'extérieur décalé du retard d'inertie (T_in ≈ offset + gain × T_ext "
        "décalé). Le gain reflète l'amortissement du bâti, l'offset l'apport propre "
        "(chauffage, occupation, soleil). R²/RMSE/MAE mesurent la qualité du modèle ; "
        "un résidu positif signale un intérieur plus chaud que ce que le dehors seul "
        "expliquerait.");
    return o;
}

// Confort intérieur : première analyse de CONFORT (contexte In par défaut, lit le
// cache intérieur). Zones de température et d'humidité, point de rosée, et un
// repère de risque de condensation/moisissure. Une analyse de confort décrit le
// DEDANS ; elle ne se rabat jamais sur les mesures extérieures.
QJsonObject analyzeIndoorComfort(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 6 * kHour, ctx.now);
    const QVector<double>* temp = series.channel(kTemp);
    const QVector<double>* hum  = series.channel(kHum);
    if (!temp || !hum)
        return failure(QStringLiteral("canaux température/humidité manquants"));

    const int i = lastValidIndex(*temp);
    if (i < 0)
        return failure(QStringLiteral("aucune mesure récente"));
    const double t = (*temp)[i];
    const double h = (*hum)[i];
    if (std::isnan(h))
        return failure(QStringLiteral("humidité manquante"));

    QJsonObject o;
    o["temperature"]       = round1(t);
    o["humidity"]          = round1(h);
    o["dew_point"]         = round1(meteo::dewPoint(t, h));
    o["absolute_humidity"] = round1(meteo::absoluteHumidity(t, h));

    // Zones de confort (repères usuels d'un logement).
    const QString tempZone = t < 16.0 ? QStringLiteral("froid")
                           : t < 19.0 ? QStringLiteral("frais")
                           : t <= 24.0 ? QStringLiteral("confortable")
                           : t <= 27.0 ? QStringLiteral("chaud")
                                       : QStringLiteral("trop chaud");
    const QString humZone = h < 30.0 ? QStringLiteral("trop sec")
                          : h < 40.0 ? QStringLiteral("sec")
                          : h <= 60.0 ? QStringLiteral("confortable")
                          : h <= 70.0 ? QStringLiteral("humide")
                                      : QStringLiteral("trop humide");
    o["temperature_zone"] = tempZone;
    o["humidity_zone"]    = humZone;
    const bool comfyT = (t >= 19.0 && t <= 24.0);
    const bool comfyH = (h >= 40.0 && h <= 60.0);
    o["comfort"] = (comfyT && comfyH) ? QStringLiteral("confortable")
                                      : QStringLiteral("hors zone de confort");

    // Risque de condensation / moisissure : humidité intérieure élevée.
    if (h >= 65.0) {
        o["mold_risk"] = h >= 75.0 ? QStringLiteral("élevé") : QStringLiteral("modéré");
        o["mold_note"] = QStringLiteral(
            "Humidité intérieure élevée : risque de condensation sur les parois "
            "froides (ponts thermiques) et de moisissure. Aérer / ventiler.");
    } else {
        o["mold_risk"] = QStringLiteral("faible");
    }
    o["note"] = QStringLiteral(
        "Confort intérieur (mesures IN) : zones de température et d'humidité, point "
        "de rosée, et repère de risque de condensation.");
    return o;
}

// Prévu vs observé (étape 9, enrichie) : compare, jour par jour, la prévision
// « day-ahead » archivée (émise la veille) aux mesures OUT réellement observées
// ce jour-là, puis résume la qualité des prévisions sur PLUSIEURS fenêtres
// glissantes (3/7/14/30 jours). Source primaire = OUT (les observations) ; les
// prévisions viennent du cache dédié ctx.forecastStore.
//
// On mesure, par fenêtre et par type de température (min/max) : le biais (écart
// moyen signé), la MAE (erreur absolue moyenne, l'indicateur de RÉFÉRENCE), la
// RMSE, et un indice de fiabilité 0..100 DÉRIVÉ de la MAE (voir ForecastQuality,
// où vivent la formule et les seuils). L'indice n'est qu'une relecture de la MAE :
// jamais une probabilité de prévision correcte. Contexte Out ; sans repli croisé.

// Une comparaison d'un jour cible : la prévision archivée face aux extrêmes
// réellement observés. Calculée une seule fois sur la plus large fenêtre, puis
// filtrée par date pour chaque fenêtre (évite de relire le cache à répétition).
struct DayCompare {
    quint32 day = 0;     // jour cible AAAAMMJJ
    double  fcMin = 0, fcMax = 0;   // prévu
    double  obsMin = 0, obsMax = 0; // observé
    double  eMin = 0, eMax = 0;     // écart = observé - prévu
};

// Une température min/max n'a de sens physique que dans une plage raisonnable.
// Au-delà, c'est une valeur aberrante (capteur, parasite) qu'on écarte plutôt
// que de laisser polluer les extrêmes du jour.
static bool plausibleTemp(double v) {
    return std::isfinite(v) && v > -60.0 && v < 60.0;
}

// AAAAMMJJ -> "YYYY-MM-DD" (ISO), pour que l'interface formate la date elle-même.
static QString dayKeyToIso(quint32 key) {
    if (key == 0) return QString();
    const int y = int(key / 10000), m = int((key / 100) % 100), d = int(key % 100);
    return QDate(y, m, d).toString(Qt::ISODate);
}

// Construit le résumé JSON d'une fenêtre de `days` jours se terminant à `dayTo`,
// à partir des comparaisons déjà calculées (`all`).
static QJsonObject buildForecastWindow(int days, const QVector<DayCompare>& all,
                                       quint32 dayFrom, quint32 dayTo) {
    std::vector<double> eMin, eMax, eAll;
    double fcMinSum = 0, obsMinSum = 0, fcMaxSum = 0, obsMaxSum = 0;
    int evaluated = 0;
    quint32 first = 0, last = 0;
    for (const DayCompare& dc : all) {
        if (dc.day < dayFrom || dc.day > dayTo) continue; // clés AAAAMMJJ : ordre = date
        eMin.push_back(dc.eMin);
        eMax.push_back(dc.eMax);
        eAll.push_back(dc.eMin);
        eAll.push_back(dc.eMax);
        fcMinSum += dc.fcMin; obsMinSum += dc.obsMin;
        fcMaxSum += dc.fcMax; obsMaxSum += dc.obsMax;
        if (first == 0 || dc.day < first) first = dc.day;
        if (dc.day > last) last = dc.day;
        ++evaluated;
    }

    QJsonObject w;
    w["days_requested"] = days;
    w["evaluated"]      = evaluated;
    // Couverture : part des jours de la fenêtre pour lesquels une comparaison a
    // pu être faite. En dessous de 100 %, des jours manquent (pas de prévision
    // archivée, ou observations incomplètes) : la lecture doit rester prudente.
    w["coverage_pct"]   = int(std::round(100.0 * evaluated / std::max(1, days)));
    w["date_from"]      = dayKeyToIso(first);
    w["date_to"]        = dayKeyToIso(last);

    const bool sufficient = evaluated >= forecastq::kMinDaysForIndex;
    w["sufficient"] = sufficient;

    const forecastq::ErrorStats sMin = forecastq::accumulate(eMin);
    const forecastq::ErrorStats sMax = forecastq::accumulate(eMax);
    const forecastq::ErrorStats sAll = forecastq::accumulate(eAll);

    auto tempBlock = [&](const forecastq::ErrorStats& s, double fcSum, double obsSum) {
        QJsonObject t;
        if (s.valid()) {
            t["forecast_mean"] = round1(fcSum / s.count);
            t["observed_mean"] = round1(obsSum / s.count);
            t["bias"] = round1(s.bias);   // signé : + = observé plus chaud que prévu
            t["mae"]  = round1(s.mae);    // indicateur de référence
            t["rmse"] = round1(s.rmse);   // secondaire : pèse les gros écarts
            if (sufficient)
                t["index"] = forecastq::reliabilityIndex(s.mae);
        }
        return t;
    };

    if (evaluated > 0) {
        w["tmin"] = tempBlock(sMin, fcMinSum, obsMinSum);
        w["tmax"] = tempBlock(sMax, fcMaxSum, obsMaxSum);
        QJsonObject overall;
        overall["mae"]     = round1(sAll.mae);
        overall["rmse"]    = round1(sAll.rmse);
        overall["quality"] = forecastq::reliabilityLabel(sAll.mae, evaluated);
        if (sufficient)
            overall["index"] = forecastq::reliabilityIndex(sAll.mae);
        w["overall"] = overall;
    } else {
        // Aucune comparaison : état explicite, aucune valeur inventée.
        QJsonObject overall;
        overall["quality"] = QStringLiteral("Données insuffisantes");
        w["overall"] = overall;
    }
    return w;
}

QJsonObject analyzeForecastVsObserved(const AnalysisContext& ctx, const QJsonObject& params) {
    if (!ctx.forecastStore)
        return failure(QStringLiteral("collecte des prévisions inactive (aucun cache prévisions)"));
    if (!ctx.store || !ctx.store->isOpen())
        return failure(QStringLiteral("données extérieures (OUT) indisponibles"));

    // Fenêtres glissantes, de la plus courte à la plus longue. Surchargeables
    // (params["windows"]) pour les tests, sinon les paliers de lecture usuels.
    QVector<int> windows;
    const QJsonArray reqW = params.value(QStringLiteral("windows")).toArray();
    if (!reqW.isEmpty()) {
        for (const QJsonValue& v : reqW) {
            const int d = v.toInt();
            if (d >= 2 && d <= 90) windows.push_back(d);
        }
    }
    if (windows.isEmpty())
        windows = {3, 7, 14, 30};
    std::sort(windows.begin(), windows.end());
    const int widest = windows.last();

    auto dayKeyOf = [](qint64 ts) -> quint32 {
        const QDate d = QDateTime::fromSecsSinceEpoch(ts).date();
        return static_cast<quint32>(d.year() * 10000 + d.month() * 100 + d.day());
    };
    const quint32 dayTo = dayKeyOf(ctx.now);

    // On lit le cache une seule fois, sur la plus large fenêtre. Une fenêtre de N
    // jours = aujourd'hui et les N-1 jours précédents, d'où le (widest - 1).
    const quint32 widestFrom = dayKeyOf(ctx.now - static_cast<qint64>(widest - 1) * 86400);
    const QVector<ForecastEntry> forecasts = ctx.forecastStore->range(widestFrom, dayTo);
    if (forecasts.isEmpty())
        return failure(QStringLiteral("aucune prévision archivée sur la fenêtre"));

    // Comparaisons jour par jour, calculées une seule fois.
    QVector<DayCompare> compares;
    int incompleteObs = 0;  // jours prévus mais pas (encore) assez observés
    quint32 lastDay = 0;
    double lastFcMin = 0, lastFcMax = 0, lastObsMin = 0, lastObsMax = 0;
    QString lastDesc;

    for (const ForecastEntry& fc : forecasts) {
        if (!plausibleTemp(fc.tempMin) || !plausibleTemp(fc.tempMax))
            continue;  // prévision aberrante : on ne compare pas contre du bruit
        // Observations OUT de ce jour cible (même découpage AAAAMMJJ que l'appareil).
        const Series obs = ctx.store->rangeForDay(fc.targetDay);
        const QVector<double>* obsT = obs.channel(kTemp);
        if (!obsT) { incompleteObs++; continue; }
        double obsMin = 1e9, obsMax = -1e9; int obsN = 0;
        for (double v : *obsT) if (Series::isValid(v) && plausibleTemp(v)) {
            if (v < obsMin) obsMin = v;
            if (v > obsMax) obsMax = v;
            obsN++;
        }
        if (obsN < 3) { incompleteObs++; continue; } // journée pas (encore) assez observée

        DayCompare dc;
        dc.day = fc.targetDay;
        dc.fcMin = fc.tempMin; dc.fcMax = fc.tempMax;
        dc.obsMin = obsMin;    dc.obsMax = obsMax;
        dc.eMin = obsMin - fc.tempMin; // >0 : observé plus chaud que prévu
        dc.eMax = obsMax - fc.tempMax;
        compares.push_back(dc);

        if (fc.targetDay >= lastDay) {
            lastDay = fc.targetDay;
            lastFcMin = fc.tempMin; lastFcMax = fc.tempMax;
            lastObsMin = obsMin; lastObsMax = obsMax;
            lastDesc = fc.description;
        }
    }

    if (compares.isEmpty())
        return failure(QStringLiteral(
            "prévisions archivées mais aucun jour encore observé pour comparer "
            "(laisser passer au moins une journée complète)"));

    QJsonObject o;
    o["forecasts_in_cache"] = forecasts.size();
    o["incomplete_days"]    = incompleteObs;

    // Résumé par fenêtre (l'évolution de la fiabilité dans le temps se lit en
    // parcourant ce tableau, de la plus courte à la plus large).
    QJsonArray windowsJson;
    QJsonObject reference;  // la plus large fenêtre SUFFISANTE : le titre du cartouche
    int referenceDays = 0;
    for (int days : windows) {
        const quint32 from = dayKeyOf(ctx.now - static_cast<qint64>(days - 1) * 86400);
        const QJsonObject w = buildForecastWindow(days, compares, from, dayTo);
        windowsJson.append(w);
        if (w.value("sufficient").toBool()) {
            reference = w;
            referenceDays = days;
        }
    }
    o["windows"] = windowsJson;

    // En-tête : indice + MAE + qualité de la fenêtre de référence. Si AUCUNE
    // fenêtre n'atteint le minimum de jours, on l'annonce sans inventer d'indice.
    QJsonObject headline;
    if (referenceDays > 0) {
        const QJsonObject ov = reference.value("overall").toObject();
        headline["sufficient"]  = true;
        headline["window_days"] = referenceDays;
        headline["evaluated"]   = reference.value("evaluated").toInt();
        headline["mae"]         = ov.value("mae").toDouble();
        headline["index"]       = ov.value("index").toInt();
        headline["quality"]     = ov.value("quality").toString();
    } else {
        headline["sufficient"] = false;
        headline["evaluated"]  = compares.size();
        headline["quality"]    = QStringLiteral("Données insuffisantes");
    }
    o["headline"] = headline;

    if (lastDay > 0) {
        o["last_day"]          = static_cast<double>(lastDay);
        o["last_forecast_min"] = round1(lastFcMin);
        o["last_forecast_max"] = round1(lastFcMax);
        o["last_observed_min"] = round1(lastObsMin);
        o["last_observed_max"] = round1(lastObsMax);
        if (!lastDesc.isEmpty())
            o["last_forecast_desc"] = lastDesc;
    }

    o["note"] = QStringLiteral(
        "Prévu vs observé : compare la prévision archivée la veille (day-ahead) aux "
        "mesures extérieures réellement relevées. La MAE (erreur absolue moyenne, en "
        "°C) est l'indicateur de référence ; le biais est l'écart moyen signé "
        "(positif = observé plus chaud que prévu). L'indice sur 100 n'est qu'une "
        "relecture de la MAE sur l'historique observé, jamais une garantie pour les "
        "prévisions à venir.");
    return o;
}

void registerMeteoAnalyses(AnalysisRegistry& registry) {
    using A = FunctionAnalysis;
    // `ctx` = contexte météo par défaut de l'analyse (intrinsèque à sa nature).
    // OUT pour toute la météo/climatologie de ce dépôt ; passer MeteoCtx::In pour
    // une future analyse de confort intérieur, MeteoCtx::Both pour un modèle de
    // relation intérieur/extérieur. Toujours surchargeable via params["ctx"].
    auto add = [&](const char* id, const char* title, const char* group,
                   qint64 minSpan, A::Fn fn, MeteoCtx ctx = MeteoCtx::Out) {
        registry.add(std::make_unique<A>(QString::fromUtf8(id), QString::fromUtf8(title),
                                         QString::fromUtf8(group), minSpan, std::move(fn), ctx));
    };

    // --- Vague 1 : etat courant et prevision locale -------------------------
    add("current", "Conditions actuelles", "nowcast", 0, analyzeCurrent);
    add("heat_risk", "Chaleur et humidex", "nowcast", 0, analyzeHeatRisk);
    add("dry_air", "Sécheresse atmosphérique", "nowcast", 0, analyzeDryAirRisk);
    add("pressure_trend", "Tendance barométrique", "nowcast", 3 * kHour, analyzePressureTrend);
    add("temp_trend", "Tendance de température", "nowcast", 3 * kHour, analyzeTempTrend);
    add("zambretti", "Prévision locale (Zambretti)", "nowcast", 3 * kHour, analyzeZambretti);
    add("fog_risk", "Risque de brouillard", "nowcast", 2 * kHour, analyzeFogRisk);
    add("frost_risk", "Risque de gelée", "nowcast", 3 * kHour, analyzeFrostRisk);

    // --- Vague 2 : climatologie ---------------------------------------------
    add("normals", "Normale du jour et écart", "climat", 30 * kDay, analyzeNormals);
    add("degree_days", "Degrés-jours (chauffage / climatisation)", "climat", 7 * kDay, analyzeDegreeDays);
    add("diurnal_amplitude", "Amplitude thermique diurne", "climat", 7 * kDay, analyzeDiurnalAmplitude);
    add("records", "Records", "climat", 7 * kDay, analyzeRecords);
    add("streaks", "Jours remarquables et séries", "climat", 30 * kDay, analyzeStreaks);
    add("daily_cycle", "Cycle journalier moyen", "climat", 7 * kDay, analyzeDailyCycle);

    // Vague 3 : analyses avancees.
    add("anomalies", "Anomalies (z-score robuste)", "avancé", 3 * kDay, analyzeAnomalies);
    add("correlations", "Corrélations à décalage", "avancé", 2 * kDay, analyzeLaggedCorrelation);
    add("episodes", "Épisodes (canicule, coup de froid)", "avancé", 3 * kDay, analyzeEpisodes);
    add("decomposition", "Décomposition tendance / saison", "avancé", 3 * kDay, analyzeDecomposition);

    // --- Confort interieur (contexte In) -------------------------------------
    add("indoor_comfort", "Confort intérieur", "confort", 0, analyzeIndoorComfort,
        MeteoCtx::In);

    // --- Relation interieur / exterieur (contexte Both) ----------------------
    add("thermal_behaviour", "Comportement thermique du bâtiment", "relation",
        1 * kDay, analyzeThermalBehaviour, MeteoCtx::Both);
    add("indoor_inertia_model", "Réactivité du bâtiment", "relation",
        1 * kDay, analyzeIndoorInertiaModel, MeteoCtx::Both);

    // --- Prevision (prevu vs observe) ----------------------------------------
    add("forecast_vs_observed", "Prévu vs observé", "prévision",
        1 * kDay, analyzeForecastVsObserved, MeteoCtx::Out);

    // --- Qualite -------------------------------------------------------------
    add("data_quality", "Complétude des données", "qualite", 2 * kDay, analyzeDataQuality);
}

} // namespace morfanalytics
