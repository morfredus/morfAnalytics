/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/SiteWatchInsights.h"

#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace morfanalytics {
namespace sitewatch {

namespace {

using Daily = QMap<QDate, double>;

// Series suivies : (nom court, cle dans report.stats, seuil absolu d'anomalie).
// Le seuil evite qu'un ecart statistique sur de tres petits nombres fasse du bruit.
struct SeriesDef { const char* name; const char* key; double minAbs; };
const SeriesDef kSeries[] = {
    {"humans",  "daily_humans",  20},
    {"bots",    "daily_bots",    50},
    {"ai",      "daily_ai",      20},
    {"seo",     "daily_seo",     20},
    {"e404",    "daily_404",     30},
    {"e403",    "daily_403",     20},
    {"e500",    "daily_500",      3},
    {"attacks", "daily_attacks", 10},
    {"normal",  "daily_normal",  50},
};

double minAbsOf(const QString& name) {
    for (const SeriesDef& s : kSeries)
        if (name == QLatin1String(s.name)) return s.minAbs;
    return 10;
}

Daily parseDaily(const QJsonObject& stats, const char* key) {
    Daily out;
    const QJsonObject obj = stats.value(QLatin1String(key)).toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QDate d = QDate::fromString(it.key(), Qt::ISODate);
        if (d.isValid()) out.insert(d, it.value().toDouble());
    }
    return out;
}

struct Model {
    QMap<QString, Daily> series;   // nom court -> jours
    QDate first, last;             // bornes des donnees
    QString siteId, siteLabel;
};

Model buildModel(const QJsonObject& report) {
    Model m;
    m.siteId = report.value(QStringLiteral("site_id")).toString();
    m.siteLabel = report.value(QStringLiteral("site_label")).toString(m.siteId);
    const QJsonObject stats = report.value(QStringLiteral("stats")).toObject();
    for (const SeriesDef& s : kSeries) {
        Daily d = parseDaily(stats, s.key);
        for (auto it = d.begin(); it != d.end(); ++it) {
            if (!m.first.isValid() || it.key() < m.first) m.first = it.key();
            if (!m.last.isValid() || it.key() > m.last) m.last = it.key();
        }
        m.series.insert(QLatin1String(s.name), d);
    }
    // Le dernier jour de donnees annonce par SiteWatch peut depasser les series (jour
    // sans trafic) : on le prefere s'il est plus recent.
    const QDate up = QDate::fromString(report.value(QStringLiteral("source_up_to")).toString(), Qt::ISODate);
    if (up.isValid() && (!m.last.isValid() || up > m.last)) m.last = up;
    return m;
}

double at(const Daily& d, const QDate& day) { return d.value(day, 0.0); }

double sumRange(const Daily& d, const QDate& from, const QDate& to) {
    double s = 0;
    for (QDate x = from; x <= to; x = x.addDays(1)) s += at(d, x);
    return s;
}

double median(QVector<double> v) {
    if (v.isEmpty()) return 0;
    std::sort(v.begin(), v.end());
    const int n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// Reference de normalite d'une serie : mediane et echelle robuste (MAD). La moyenne et
// l'ecart-type sont fausses par les pics qu'on cherche justement a reperer.
struct Baseline { double med = 0, scale = 1; bool ok = false; };

Baseline baselineOf(const Daily& d, const QDate& first, const QDate& last) {
    Baseline b;
    if (!first.isValid() || !last.isValid()) return b;
    QVector<double> v;
    for (QDate x = first; x <= last; x = x.addDays(1)) v << at(d, x);
    if (v.size() < 14) return b;   // trop peu d'historique pour juger
    b.med = median(v);
    QVector<double> dev;
    for (double x : v) dev << std::fabs(x - b.med);
    const double mad = median(dev);
    b.scale = 1.4826 * mad;
    if (b.scale < 1e-9) b.scale = std::max(1.0, 0.25 * b.med);   // serie quasi constante
    b.ok = true;
    return b;
}

double score(const Baseline& b, double x) { return (x - b.med) / b.scale; }

QString dayStr(const QDate& d) { return d.toString(Qt::ISODate); }

QJsonObject kpi(double value, double prev, bool pct = true) {
    QJsonObject o{{"value", value}, {"prev", prev}};
    if (pct && prev > 0) o.insert(QStringLiteral("delta_pct"), (value - prev) * 100.0 / prev);
    return o;
}

QJsonArray rankedList(const QJsonObject& obj, bool withCategory = false) {
    QVector<QPair<QString, double>> r;
    for (auto it = obj.begin(); it != obj.end(); ++it) r.append({it.key(), it.value().toDouble()});
    std::sort(r.begin(), r.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    QJsonArray out;
    for (const auto& p : r) {
        QJsonObject e{{"name", p.first}, {"count", p.second}};
        if (withCategory) e.insert(QStringLiteral("category"), botCategory(p.first));
        out.append(e);
    }
    return out;
}

QVector<double> hourly(const QJsonObject& stats, const char* key) {
    QVector<double> v(24, 0.0);
    const QJsonArray a = stats.value(QStringLiteral("hourly")).toObject().value(QLatin1String(key)).toArray();
    for (int i = 0; i < 24 && i < a.size(); ++i) v[i] = a[i].toDouble();
    return v;
}

// Pages (ou robots) de la derniere semaine face a la precedente : entrants, sortants,
// hausses et baisses. `minCount` ecarte le bruit des petits nombres.
QJsonObject movers(const QJsonObject& recent, const QJsonObject& prior, double minCount) {
    struct M { QString name; double r, p; };
    QVector<M> all;
    QSet<QString> seen;
    for (auto it = recent.begin(); it != recent.end(); ++it) {
        all.append({it.key(), it.value().toDouble(), prior.value(it.key()).toDouble()});
        seen.insert(it.key());
    }
    for (auto it = prior.begin(); it != prior.end(); ++it)
        if (!seen.contains(it.key())) all.append({it.key(), 0, it.value().toDouble()});

    auto toJson = [](const QVector<M>& v) {
        QJsonArray a;
        for (int i = 0; i < v.size() && i < 10; ++i)
            a.append(QJsonObject{{"name", v[i].name}, {"recent", v[i].r}, {"prior", v[i].p},
                                 {"delta", v[i].r - v[i].p}});
        return a;
    };
    QVector<M> up, down;
    for (const M& m : all) {
        if (m.r - m.p >= minCount && m.r >= minCount) up.append(m);
        if (m.p - m.r >= minCount && m.p >= minCount) down.append(m);
    }
    std::sort(up.begin(), up.end(), [](const M& a, const M& b) { return a.r - a.p > b.r - b.p; });
    std::sort(down.begin(), down.end(), [](const M& a, const M& b) { return a.p - a.r > b.p - b.r; });
    return QJsonObject{{"rising", toJson(up)}, {"falling", toJson(down)}};
}

const QSet<QString>& validLevels() {
    static const QSet<QString> s{"info", "warning", "error"};
    return s;
}

} // namespace

// -- Configuration ----------------------------------------------------------

bool AlertConfig::isMuted(const QString& id, const QDate& today) const {
    for (const QString& k : {id, QStringLiteral("*")}) {
        const QDate until = QDate::fromString(muted.value(k), Qt::ISODate);
        if (until.isValid() && until >= today) return true;
    }
    return false;
}

QVector<RuleInfo> ruleCatalog() {
    return {
        {"stale_data", "SiteWatch ne publie plus",
         "Aucune donnée récente : l'application est fermée ou la publication planifiée ne tourne plus.", "warning"},
        {"e500", "Erreurs serveur (500)", "Au moins une erreur HTTP 500 dans la journée : le site est en panne partielle.", "warning"},
        {"attack_spike", "Pic de tentatives d'attaque", "Journée nettement au-dessus de l'habitude du site.", "warning"},
        {"e404_surge", "Pic d'erreurs 404", "Beaucoup de pages introuvables : lien cassé largement diffusé ou scan.", "warning"},
        {"e403_surge", "Pic d'accès refusés (403)", "Beaucoup d'accès interdits : scan ou blocage mal réglé.", "warning"},
        {"traffic_drop", "Chute du trafic", "Visites humaines très inférieures à l'habitude : le site est peut-être injoignable.", "warning"},
        {"traffic_surge", "Pic de trafic", "Visites humaines très supérieures à l'habitude.", "info"},
        {"bot_share", "Part des robots en hausse", "Les robots prennent nettement le dessus sur 7 jours.", "warning"},
        {"ai_growth", "Robots IA en forte hausse", "Les robots d'IA se multiplient d'une semaine à l'autre.", "info"},
        {"new_bot", "Nouveau robot actif", "Un robot absent la période précédente est maintenant actif.", "info"},
    };
}

QStringList sanitizeTargets(const QJsonArray& a) {
    QStringList out;
    for (const QJsonValue& v : a) {
        const QString t = v.toString().trimmed().left(64);
        if (!t.isEmpty() && !out.contains(t) && out.size() < 20) out.append(t);
    }
    return out;
}

AlertConfig alertConfigFromJson(const QJsonObject& o) {
    AlertConfig c;
    auto num = [&](const char* k, double def, double lo, double hi) {
        const QJsonValue v = o.value(QLatin1String(k));
        return v.isDouble() ? std::max(lo, std::min(hi, v.toDouble())) : def;
    };
    c.sensitivity = num("sensitivity", c.sensitivity, 2.0, 10.0);
    c.recentDays = static_cast<int>(num("recent_days", c.recentDays, 1, 7));
    c.staleWarnDays = static_cast<int>(num("stale_warn_days", c.staleWarnDays, 1, 30));
    c.staleErrorDays = static_cast<int>(num("stale_error_days", c.staleErrorDays, 1, 60));
    if (c.staleErrorDays < c.staleWarnDays) c.staleErrorDays = c.staleWarnDays;
    c.botSharePoints = num("bot_share_points", c.botSharePoints, 5, 60);
    c.aiGrowthFactor = num("ai_growth_factor", c.aiGrowthFactor, 1.2, 20);
    c.e500ErrorAt = static_cast<int>(num("e500_error_at", c.e500ErrorAt, 1, 1000));
    const QJsonObject rules = o.value(QStringLiteral("rules")).toObject();
    for (auto it = rules.begin(); it != rules.end(); ++it) {
        const QJsonObject r = it.value().toObject();
        RuleSetting s;
        s.enabled = r.value(QStringLiteral("enabled")).toBool(true);
        s.notify = r.value(QStringLiteral("notify")).toBool(true);
        const QString lvl = r.value(QStringLiteral("level")).toString();
        s.level = validLevels().contains(lvl) ? lvl : QString();
        s.targets = sanitizeTargets(r.value(QStringLiteral("targets")).toArray());
        c.rules.insert(it.key(), s);
    }
    const QJsonObject muted = o.value(QStringLiteral("muted")).toObject();
    for (auto it = muted.begin(); it != muted.end(); ++it)
        if (QDate::fromString(it.value().toString(), Qt::ISODate).isValid())
            c.muted.insert(it.key(), it.value().toString());
    return c;
}

QJsonObject alertConfigToJson(const AlertConfig& c) {
    QJsonObject rules;
    for (auto it = c.rules.begin(); it != c.rules.end(); ++it)
        rules.insert(it.key(), QJsonObject{{"enabled", it.value().enabled}, {"level", it.value().level},
                                           {"notify", it.value().notify},
                                           {"targets", QJsonArray::fromStringList(it.value().targets)}});
    QJsonObject muted;
    for (auto it = c.muted.begin(); it != c.muted.end(); ++it) muted.insert(it.key(), it.value());
    return QJsonObject{{"sensitivity", c.sensitivity}, {"recent_days", c.recentDays},
        {"stale_warn_days", c.staleWarnDays}, {"stale_error_days", c.staleErrorDays},
        {"bot_share_points", c.botSharePoints}, {"ai_growth_factor", c.aiGrowthFactor},
        {"e500_error_at", c.e500ErrorAt}, {"rules", rules}, {"muted", muted}};
}

AlertConfig mergeSiteConfig(const AlertConfig& global, const QJsonObject& site) {
    QJsonObject g = alertConfigToJson(global);
    for (auto it = site.begin(); it != site.end(); ++it) {
        if (it.key() == QLatin1String("rules") || it.key() == QLatin1String("muted")) {
            QJsonObject merged = g.value(it.key()).toObject();
            const QJsonObject over = it.value().toObject();
            for (auto o = over.begin(); o != over.end(); ++o) merged.insert(o.key(), o.value());
            g.insert(it.key(), merged);
        } else {
            g.insert(it.key(), it.value());
        }
    }
    return alertConfigFromJson(g);
}

// -- Analyse ----------------------------------------------------------------

QString botCategory(const QString& engine) {
    static const QSet<QString> ia{"Claude", "OpenAI", "Perplexity", "Gemini", "Anthropic", "ChatGPT",
                                  "Bytespider", "Meta-AI", "Amazonbot", "Mistral", "Cohere"};
    static const QSet<QString> seo{"Ahrefs", "MJ12", "DotBot", "Semrush", "SEMrush", "Majestic",
                                   "Moz", "Screaming Frog", "Serpstat"};
    static const QSet<QString> moteur{"Google", "Bing", "DuckDuckGo", "Yandex", "Baidu", "Qwant",
                                      "Yahoo", "Ecosia", "Brave"};
    static const QSet<QString> social{"Facebook", "Twitter", "LinkedIn", "Pinterest", "Telegram",
                                      "WhatsApp", "Discord", "Slack"};
    if (ia.contains(engine)) return QStringLiteral("ia");
    if (seo.contains(engine)) return QStringLiteral("seo");
    if (moteur.contains(engine)) return QStringLiteral("moteur");
    if (social.contains(engine)) return QStringLiteral("social");
    return QStringLiteral("autre");
}

QJsonObject insights(const QJsonObject& report, const QDate& fromIn, const QDate& toIn,
                     const QDate& today, const AlertConfig& cfg) {
    const Model m = buildModel(report);
    QJsonObject out;
    out.insert(QStringLiteral("site_id"), m.siteId);
    out.insert(QStringLiteral("site_label"), m.siteLabel);
    out.insert(QStringLiteral("has_data"), m.first.isValid());
    if (!m.first.isValid()) return out;

    // Fenetre : par defaut les 30 derniers jours de donnees.
    QDate to = toIn.isValid() ? toIn : m.last;
    QDate from = fromIn.isValid() ? fromIn : to.addDays(-29);
    if (from > to) std::swap(from, to);
    if (from < m.first) from = m.first;   // Tout : pas de jours avant les donnees
    const int len = static_cast<int>(from.daysTo(to)) + 1;
    const QDate pFrom = from.addDays(-len), pTo = from.addDays(-1);
    out.insert(QStringLiteral("window"), QJsonObject{{"from", dayStr(from)}, {"to", dayStr(to)}, {"days", len}});
    out.insert(QStringLiteral("previous"), QJsonObject{{"from", dayStr(pFrom)}, {"to", dayStr(pTo)}});
    out.insert(QStringLiteral("data_range"), QJsonObject{{"from", dayStr(m.first)}, {"to", dayStr(m.last)}});

    // Fraicheur : la donnee la plus recente face a aujourd'hui.
    const int lag = static_cast<int>(m.last.daysTo(today));
    out.insert(QStringLiteral("freshness"), QJsonObject{
        {"up_to", dayStr(m.last)}, {"lag_days", std::max(0, lag)},
        {"consolidated_at", report.value(QStringLiteral("consolidated_at")).toDouble()},
        {"received_at", report.value(QStringLiteral("received_at")).toDouble()}});

    // Series alignees sur la fenetre (un tableau par serie, 0 pour un jour sans valeur).
    QJsonArray dates;
    for (QDate x = from; x <= to; x = x.addDays(1)) dates.append(dayStr(x));
    QJsonObject series{{"dates", dates}};
    QJsonObject kpis;
    QJsonObject peaks;
    for (const SeriesDef& s : kSeries) {
        const Daily& d = m.series[QLatin1String(s.name)];
        QJsonArray vals;
        double peak = -1; QDate peakDay;
        for (QDate x = from; x <= to; x = x.addDays(1)) {
            const double v = at(d, x);
            vals.append(v);
            if (v > peak) { peak = v; peakDay = x; }
        }
        series.insert(QLatin1String(s.name), vals);
        kpis.insert(QLatin1String(s.name), kpi(sumRange(d, from, to), sumRange(d, pFrom, pTo)));
        if (peak > 0) peaks.insert(QLatin1String(s.name), QJsonObject{{"day", dayStr(peakDay)}, {"value", peak}});
    }
    out.insert(QStringLiteral("series"), series);
    out.insert(QStringLiteral("peaks"), peaks);

    // Indicateurs derives. Le denominateur « requetes » = humains + robots.
    auto sumOf = [&](const char* n, const QDate& a, const QDate& b) {
        return sumRange(m.series[QLatin1String(n)], a, b);
    };
    const double req = sumOf("humans", from, to) + sumOf("bots", from, to);
    const double reqP = sumOf("humans", pFrom, pTo) + sumOf("bots", pFrom, pTo);
    kpis.insert(QStringLiteral("requests"), kpi(req, reqP));
    const double err = sumOf("e404", from, to) + sumOf("e403", from, to) + sumOf("e500", from, to);
    const double errP = sumOf("e404", pFrom, pTo) + sumOf("e403", pFrom, pTo) + sumOf("e500", pFrom, pTo);
    // Parts exprimees en POINTS de pourcentage : un delta relatif sur une part est trompeur.
    kpis.insert(QStringLiteral("bot_share_pct"), kpi(req > 0 ? sumOf("bots", from, to) * 100.0 / req : 0,
                                                   reqP > 0 ? sumOf("bots", pFrom, pTo) * 100.0 / reqP : 0, false));
    kpis.insert(QStringLiteral("error_rate_pct"), kpi(req > 0 ? err * 100.0 / req : 0,
                                                    reqP > 0 ? errP * 100.0 / reqP : 0, false));
    kpis.insert(QStringLiteral("ai_share_of_bots_pct"), kpi(sumOf("bots", from, to) > 0 ? sumOf("ai", from, to) * 100.0 / sumOf("bots", from, to) : 0,
                                                          sumOf("bots", pFrom, pTo) > 0 ? sumOf("ai", pFrom, pTo) * 100.0 / sumOf("bots", pFrom, pTo) : 0, false));
    out.insert(QStringLiteral("kpis"), kpis);

    // Anomalies : jours de la fenetre qui s'ecartent nettement de la normale du site.
    struct Anom { QString series, day, direction; double value, baseline, score; };
    QVector<Anom> anoms;
    for (const SeriesDef& s : kSeries) {
        const Daily& d = m.series[QLatin1String(s.name)];
        const Baseline b = baselineOf(d, m.first, m.last);
        if (!b.ok) continue;
        for (QDate x = from; x <= to; x = x.addDays(1)) {
            if (x > m.last || x >= today) continue;   // le jour en cours est partiel
            const double v = at(d, x), sc = score(b, v);
            if (sc >= cfg.sensitivity && v >= s.minAbs)
                anoms.append({QLatin1String(s.name), dayStr(x), QStringLiteral("hausse"), v, b.med, sc});
            else if (sc <= -cfg.sensitivity && b.med >= s.minAbs && std::string(s.name) == "humans")
                anoms.append({QLatin1String(s.name), dayStr(x), QStringLiteral("baisse"), v, b.med, sc});
        }
    }
    std::sort(anoms.begin(), anoms.end(), [](const Anom& a, const Anom& b) { return std::fabs(a.score) > std::fabs(b.score); });
    QJsonArray anomalies;
    for (int i = 0; i < anoms.size() && i < 40; ++i)
        anomalies.append(QJsonObject{{"series", anoms[i].series}, {"day", anoms[i].day},
            {"direction", anoms[i].direction}, {"value", anoms[i].value},
            {"baseline", anoms[i].baseline}, {"score", anoms[i].score}});
    out.insert(QStringLiteral("anomalies"), anomalies);

    // Profil hebdomadaire (lundi = 1) : moyenne par jour de la semaine sur la fenetre.
    QJsonArray weekday;
    for (int wd = 1; wd <= 7; ++wd) {
        double h = 0, b = 0; int n = 0;
        for (QDate x = from; x <= to; x = x.addDays(1))
            if (x.dayOfWeek() == wd) { h += at(m.series["humans"], x); b += at(m.series["bots"], x); ++n; }
        weekday.append(QJsonObject{{"dow", wd}, {"humans", n ? h / n : 0}, {"bots", n ? b / n : 0}, {"days", n}});
    }
    out.insert(QStringLiteral("weekday"), weekday);

    // Classements du rapport (periode du rapport, pas de la fenetre).
    const QJsonObject stats = report.value(QStringLiteral("stats")).toObject();
    QJsonObject bots{{"list", rankedList(stats.value(QStringLiteral("bot_counts")).toObject(), true)}};
    QJsonObject byCat;
    for (const QJsonValue& v : bots.value(QStringLiteral("list")).toArray()) {
        const QJsonObject e = v.toObject();
        const QString c = e.value(QStringLiteral("category")).toString();
        byCat.insert(c, byCat.value(c).toDouble() + e.value(QStringLiteral("count")).toDouble());
    }
    bots.insert(QStringLiteral("by_category"), byCat);
    out.insert(QStringLiteral("report_period"), QJsonObject{
        {"from", report.value(QStringLiteral("from")).toString()}, {"to", report.value(QStringLiteral("to")).toString()}});
    out.insert(QStringLiteral("tops"), QJsonObject{
        {"pages", rankedList(stats.value(QStringLiteral("top_pages")).toObject())},
        {"attacked", rankedList(stats.value(QStringLiteral("top_attacked")).toObject())},
        {"broken", rankedList(stats.value(QStringLiteral("top_404")).toObject())},
        {"bots", bots},
        {"referers", rankedList(stats.value(QStringLiteral("referers")).toObject())},
        {"attack_activity", rankedList(stats.value(QStringLiteral("attack_activity")).toObject())},
        {"normal_activity", rankedList(stats.value(QStringLiteral("normal_activity")).toObject())}});
    out.insert(QStringLiteral("status_classes"), stats.value(QStringLiteral("status_classes")).toObject());

    // Profil horaire : quand les humains viennent, quand les robots et les attaques frappent.
    const QVector<double> hh = hourly(stats, "humans"), hb = hourly(stats, "bots"), ha = hourly(stats, "attacks");
    QJsonArray hours;
    for (int h = 0; h < 24; ++h) hours.append(QJsonObject{{"hour", h}, {"humans", hh[h]}, {"bots", hb[h]}, {"attacks", ha[h]}});
    out.insert(QStringLiteral("hourly"), hours);

    // Pages en hausse ou en baisse, et robots nouveaux : 7 derniers jours contre les 7 precedents.
    out.insert(QStringLiteral("page_movers"), movers(stats.value(QStringLiteral("pages_recent")).toObject(),
                                                     stats.value(QStringLiteral("pages_prior")).toObject(), 20));
    QJsonArray newBots;
    const QJsonObject botsRecent = stats.value(QStringLiteral("bots_recent")).toObject();
    const QJsonObject botsPrior = stats.value(QStringLiteral("bots_prior")).toObject();
    if (!botsPrior.isEmpty())
        for (const QJsonValue& v : rankedList(botsRecent, true))
            if (!botsPrior.contains(v.toObject().value(QStringLiteral("name")).toString())
                && v.toObject().value(QStringLiteral("count")).toDouble() >= 20)
                newBots.append(v);
    out.insert(QStringLiteral("new_bots"), newBots);
    out.insert(QStringLiteral("bot_movers"), movers(botsRecent, botsPrior, 50));

    QJsonArray alerts;
    for (const Alert& a : evaluateAlerts(report, today, cfg))
        alerts.append(QJsonObject{{"key", a.key}, {"rule", a.rule}, {"level", a.level}, {"day", a.day},
                                  {"title", a.title}, {"message", a.message}, {"muted", a.muted}});
    out.insert(QStringLiteral("alerts"), alerts);
    return out;
}

QVector<Alert> evaluateAlerts(const QJsonObject& report, const QDate& today, const AlertConfig& cfg) {
    QVector<Alert> out;
    const Model m = buildModel(report);
    if (!m.first.isValid()) return out;
    const QString site = m.siteLabel;

    // Ajoute une alerte si la regle est active ; le niveau configure remplace celui par defaut.
    auto add = [&](const QString& rule, const QString& defLevel, const QDate& day, const QString& title,
                   const QString& message, const QString& keyExtra = QString()) {
        const RuleSetting rs = cfg.rule(rule);
        if (!rs.enabled) return;
        Alert a;
        a.key = QStringLiteral("%1|%2|%3").arg(rule, m.siteId, keyExtra.isEmpty() ? dayStr(day) : keyExtra);
        a.rule = rule;
        a.level = rs.level.isEmpty() ? defLevel : rs.level;
        a.day = dayStr(day);
        a.title = title;
        a.message = message;
        a.notify = rs.notify;
        a.targets = rs.targets;
        a.muted = cfg.isMuted(rule, today);
        out.append(a);
    };

    // 1. Silence de SiteWatch : c'est la question de depart (appli fermee des jours).
    const int lag = static_cast<int>(m.last.daysTo(today));
    if (lag >= cfg.staleWarnDays)
        add(QStringLiteral("stale_data"), lag >= cfg.staleErrorDays ? QStringLiteral("error") : QStringLiteral("warning"), today,
            QStringLiteral("SiteWatch %1 : données en retard").arg(site),
            QStringLiteral("Dernières données du %1, soit %2 jours de retard. SiteWatch ne publie plus : "
                           "lancer l'application ou planifier sitewatch-publish.").arg(dayStr(m.last)).arg(lag));

    // Jours juges : les derniers jours COMPLETS (le jour en cours est partiel).
    const QDate ref = m.last >= today ? today.addDays(-1) : m.last;
    for (int back = 0; back < cfg.recentDays; ++back) {
        const QDate day = ref.addDays(-back);
        if (day < m.first) break;

        // 2. Erreurs serveur 500 : indispensable, un site qui renvoie des 500 est casse.
        const double e500 = at(m.series["e500"], day);
        if (e500 >= 1)
            add(QStringLiteral("e500"), e500 >= cfg.e500ErrorAt ? QStringLiteral("error") : QStringLiteral("warning"), day,
                QStringLiteral("%1 : erreurs serveur").arg(site),
                QStringLiteral("%1 erreur(s) HTTP 500 le %2.").arg(static_cast<long>(e500)).arg(dayStr(day)));

        struct Rule { const char* series; const char* rule; const char* label; };
        const Rule rules[] = {
            {"attacks", "attack_spike", "tentatives d'attaque"},
            {"e404",    "e404_surge",   "erreurs 404"},
            {"e403",    "e403_surge",   "accès refusés (403)"},
        };
        for (const Rule& r : rules) {
            const Daily& d = m.series[QLatin1String(r.series)];
            const Baseline b = baselineOf(d, m.first, m.last);
            const double v = at(d, day);
            if (b.ok && score(b, v) >= cfg.sensitivity && v >= minAbsOf(QLatin1String(r.series)))
                add(QLatin1String(r.rule), QStringLiteral("warning"), day,
                    QStringLiteral("%1 : pic de %2").arg(site, QLatin1String(r.label)),
                    QStringLiteral("%1 %2 le %3 (habituellement environ %4).")
                        .arg(static_cast<long>(v)).arg(QLatin1String(r.label)).arg(dayStr(day)).arg(static_cast<long>(b.med)));
        }

        // Trafic humain : chute ou pic inhabituel.
        const Daily& h = m.series["humans"];
        const Baseline bh = baselineOf(h, m.first, m.last);
        const double hv = at(h, day);
        if (bh.ok && score(bh, hv) <= -cfg.sensitivity && bh.med >= 20)
            add(QStringLiteral("traffic_drop"), QStringLiteral("warning"), day,
                QStringLiteral("%1 : chute du trafic").arg(site),
                QStringLiteral("%1 visites humaines le %2 (habituellement environ %3). Site injoignable ?")
                    .arg(static_cast<long>(hv)).arg(dayStr(day)).arg(static_cast<long>(bh.med)));
        else if (bh.ok && score(bh, hv) >= cfg.sensitivity && hv >= 20)
            add(QStringLiteral("traffic_surge"), QStringLiteral("info"), day,
                QStringLiteral("%1 : pic de trafic").arg(site),
                QStringLiteral("%1 visites humaines le %2 (habituellement environ %3).")
                    .arg(static_cast<long>(hv)).arg(dayStr(day)).arg(static_cast<long>(bh.med)));
    }

    // 3. Tendances sur 7 jours : part des robots et robots IA.
    if (ref.addDays(-13) >= m.first) {
        const QDate a = ref.addDays(-6), pa = ref.addDays(-13), pb = ref.addDays(-7);
        const double bots = sumRange(m.series["bots"], a, ref), botsP = sumRange(m.series["bots"], pa, pb);
        const double hum = sumRange(m.series["humans"], a, ref), humP = sumRange(m.series["humans"], pa, pb);
        const double share = bots + hum > 0 ? bots * 100.0 / (bots + hum) : 0;
        const double shareP = botsP + humP > 0 ? botsP * 100.0 / (botsP + humP) : 0;
        if (bots >= 200 && share - shareP >= cfg.botSharePoints)
            add(QStringLiteral("bot_share"), QStringLiteral("warning"), ref,
                QStringLiteral("%1 : les robots prennent le dessus").arg(site),
                QStringLiteral("Part des robots sur 7 jours : %1 % (contre %2 % la semaine précédente).")
                    .arg(QString::number(share, 'f', 1), QString::number(shareP, 'f', 1)));
        const double ai = sumRange(m.series["ai"], a, ref), aiP = sumRange(m.series["ai"], pa, pb);
        if (ai >= 100 && aiP > 0 && ai >= cfg.aiGrowthFactor * aiP)
            add(QStringLiteral("ai_growth"), QStringLiteral("info"), ref,
                QStringLiteral("%1 : robots IA en forte hausse").arg(site),
                QStringLiteral("%1 requêtes de robots IA sur 7 jours, contre %2 la semaine précédente.")
                    .arg(static_cast<long>(ai)).arg(static_cast<long>(aiP)));
    }

    // 4. Nouveaux robots (une seule alerte par robot et par site, pas une par jour).
    const QJsonObject stats = report.value(QStringLiteral("stats")).toObject();
    const QJsonObject botsRecent = stats.value(QStringLiteral("bots_recent")).toObject();
    const QJsonObject botsPrior = stats.value(QStringLiteral("bots_prior")).toObject();
    if (!botsPrior.isEmpty())
        for (auto it = botsRecent.begin(); it != botsRecent.end(); ++it)
            if (!botsPrior.contains(it.key()) && it.value().toDouble() >= 20)
                add(QStringLiteral("new_bot"), QStringLiteral("info"), ref,
                    QStringLiteral("%1 : nouveau robot %2").arg(site, it.key()),
                    QStringLiteral("Robot « %1 » (famille %2) : %3 requêtes sur 7 jours, absent la période précédente.")
                        .arg(it.key(), botCategory(it.key())).arg(static_cast<long>(it.value().toDouble())),
                    it.key());
    return out;
}

} // namespace sitewatch
} // namespace morfanalytics
