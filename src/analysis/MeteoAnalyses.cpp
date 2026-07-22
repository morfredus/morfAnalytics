/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/AnalysisRegistry.h"
#include "morfanalytics/analysis/MeteoMath.h"
#include "morfanalytics/data/SampleStore.h"
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
// NaN si aucune mesure valide n'est assez proche — la comparaison "il y a 3 h"
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
    double tMin = 0, tMax = 0, tSum = 0; int tCount = 0;
    double hSum = 0; int hCount = 0;
    double pSum = 0; int pCount = 0;

    double tMean() const { return tCount ? tSum / tCount : std::nan(""); }
    double hMean() const { return hCount ? hSum / hCount : std::nan(""); }
    double pMean() const { return pCount ? pSum / pCount : std::nan(""); }
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
    const QVector<double>* hum  = series.channel(kHum);
    const QVector<double>* pres = series.channel(kPres);

    for (int i = 0; i < ts.size(); ++i) {
        const QDate date = QDateTime::fromSecsSinceEpoch(ts[i]).date();
        DayAggregate& day = byDate[date];
        day.date = date;

        if (temp && Series::isValid((*temp)[i])) {
            const double v = (*temp)[i];
            if (day.tCount == 0) { day.tMin = v; day.tMax = v; }
            else { day.tMin = std::min(day.tMin, v); day.tMax = std::max(day.tMax, v); }
            day.tSum += v;
            day.tCount++;
        }
        if (hum && Series::isValid((*hum)[i]))  { day.hSum += (*hum)[i];  day.hCount++; }
        if (pres && Series::isValid((*pres)[i])) { day.pSum += (*pres)[i]; day.pCount++; }
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

// ===========================================================================
//  VAGUE 1 — derives instantanes et prevision locale
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
        o["pressure"] = round1(p);
        o["pressure_sea_level"] = round1(meteo::seaLevelPressure(p, t, ctx.altitudeM));
        o["altitude_m"] = ctx.altitudeM;
        // Sans altitude renseignee, la pression reduite vaut la pression brute :
        // on le dit, plutot que de laisser croire a une valeur comparable aux
        // bulletins meteo.
        if (!ctx.altitudeKnown)
            o["note"] = QStringLiteral(
                "Altitude non renseignée (paramètre altitude_m) : la pression au "
                "niveau de la mer est identique à la pression mesurée et n'est "
                "pas comparable aux bulletins météo.");
    }
    return o;
}

// Tendance barometrique sur 3 h, code OMM, et alerte de chute rapide.
QJsonObject analyzePressureTrend(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 12 * kHour, ctx.now);
    const QVector<double>* pres = series.channel(kPres);
    const QVector<double>* temp = series.channel(kTemp);
    if (!pres || !temp)
        return failure(QStringLiteral("canaux manquants"));

    const int i = lastValidIndex(*pres);
    if (i < 0)
        return failure(QStringLiteral("aucune mesure de pression récente"));

    const qint64 nowTs = series.timestamps()[i];
    const double t = Series::isValid((*temp)[i]) ? (*temp)[i] : 15.0;
    const double pNow = meteo::seaLevelPressure((*pres)[i], t, ctx.altitudeM);

    // Tolerance de 30 min autour de la cible : une mesure par minute suffit
    // largement, mais un trou d'acquisition ne doit pas invalider la tendance.
    auto reduced = [&](qint64 target) {
        const double raw = valueNear(series, *pres, target, 30 * 60);
        return std::isnan(raw) ? std::nan("")
                               : meteo::seaLevelPressure(raw, t, ctx.altitudeM);
    };

    const double p3 = reduced(nowTs - 3 * kHour);
    const double p1 = reduced(nowTs - 1 * kHour);

    QJsonObject o;
    o["pressure_sea_level"] = round1(pNow);
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
    return o;
}

// Prevision locale Zambretti.
QJsonObject analyzeZambretti(const AnalysisContext& ctx, const QJsonObject&) {
    const Series series = ctx.store->range(ctx.now - 12 * kHour, ctx.now);
    const QVector<double>* pres = series.channel(kPres);
    const QVector<double>* temp = series.channel(kTemp);
    if (!pres || !temp)
        return failure(QStringLiteral("canaux manquants"));

    const int i = lastValidIndex(*pres);
    if (i < 0)
        return failure(QStringLiteral("aucune mesure de pression récente"));

    const qint64 nowTs = series.timestamps()[i];
    const double t = Series::isValid((*temp)[i]) ? (*temp)[i] : 15.0;
    const double pNow = meteo::seaLevelPressure((*pres)[i], t, ctx.altitudeM);

    const double raw3 = valueNear(series, *pres, nowTs - 3 * kHour, 30 * 60);
    if (std::isnan(raw3))
        return failure(QStringLiteral("historique de pression insuffisant sur 3 h"));
    const double p3 = meteo::seaLevelPressure(raw3, t, ctx.altitudeM);

    const double delta3h = pNow - p3;
    const int month = QDateTime::fromSecsSinceEpoch(nowTs).date().month();

    QJsonObject o;
    o["forecast"] = meteo::zambrettiForecast(pNow, delta3h, month);
    o["pressure_sea_level"] = round1(pNow);
    o["delta_3h"] = round1(delta3h);
    o["tendency"] = meteo::pressureTendencyLabel(delta3h);
    o["note"] = QStringLiteral(
        "Prévision locale pour les 12 à 24 heures à venir, déduite de la seule "
        "pression. Le vent n'étant pas mesuré, elle reste indicative.");
    // L'avertissement ne se declenche que si le parametre est absent, et il dit
    // ce qui est reellement en jeu : environ 0,12 hPa par metre d'altitude, soit
    // un decalage negligeable pres du niveau de la mer et franc en altitude.
    if (!ctx.altitudeKnown)
        o["warning"] = QStringLiteral(
            "Altitude non renseignée (paramètre altitude_m) : Zambretti est "
            "calibré sur la pression ramenée au niveau de la mer. L'écart vaut "
            "environ 0,12 hPa par mètre — négligeable près du niveau de la mer, "
            "il change la prévision dès quelques dizaines de mètres.");
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
//  VAGUE 2 — climatologie
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

    // MeteoHub enregistre une mesure par minute, soit 1440 par journee pleine.
    constexpr int kExpectedPerDay = 1440;
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
        const double ratio = static_cast<double>(d.tCount) / kExpectedPerDay;
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
    o["expected_per_day"] = kExpectedPerDay;
    o["note"] = QStringLiteral(
        "Une journée est dite complète au-delà de 95 % des 1440 mesures "
        "attendues. Les journées de début et de fin de fenêtre sont exclues, "
        "étant tronquées par la fenêtre elle-même. Une journée partielle n'est "
        "pas une anomalie : coupure, carte SD absente ou capteur en défaut "
        "suffisent à l'expliquer.");
    return o;
}

} // namespace

void registerMeteoAnalyses(AnalysisRegistry& registry) {
    using A = FunctionAnalysis;
    auto add = [&](const char* id, const char* title, const char* group,
                   qint64 minSpan, A::Fn fn) {
        registry.add(std::make_unique<A>(QString::fromUtf8(id), QString::fromUtf8(title),
                                         QString::fromUtf8(group), minSpan, std::move(fn)));
    };

    // --- Vague 1 : etat courant et prevision locale -------------------------
    add("current", "Conditions actuelles", "nowcast", 0, analyzeCurrent);
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

    // --- Qualite -------------------------------------------------------------
    add("data_quality", "Complétude des données", "qualite", 2 * kDay, analyzeDataQuality);
}

} // namespace morfanalytics
