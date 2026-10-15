/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/analysis/GitHubInsights.h"

#include <QDateTime>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace morfanalytics {
namespace github {

namespace {

using Daily = QMap<QDate, double>;

QString dayStr(const QDate& d) { return d.toString(Qt::ISODate); }

// Serie quotidienne d'une metrique (« views » | « clones ») lue dans repo.daily.
Daily seriesOf(const QJsonObject& repo, const QString& metric) {
    Daily out;
    for (const QJsonValue& v : repo.value(QStringLiteral("daily")).toArray()) {
        const QJsonObject d = v.toObject();
        if (d.value(QStringLiteral("metric")).toString() != metric) continue;
        const QDate day = QDate::fromString(d.value(QStringLiteral("day")).toString(), Qt::ISODate);
        if (day.isValid()) out[day] += d.value(QStringLiteral("count")).toDouble();
    }
    return out;
}

double sumBetween(const Daily& d, const QDate& from, const QDate& to) {
    double s = 0;
    for (QDate x = from; x <= to; x = x.addDays(1)) s += d.value(x, 0);
    return s;
}

// Mediane des jours [from, to] ; un jour absent compte pour 0 (GitHub n'ecrit pas les jours sans visite).
double medianBetween(const Daily& d, const QDate& from, const QDate& to) {
    QVector<double> v;
    for (QDate x = from; x <= to; x = x.addDays(1)) v.append(d.value(x, 0));
    std::sort(v.begin(), v.end());
    if (v.isEmpty()) return 0;
    const int n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

const QSet<QString>& validLevels() {
    static const QSet<QString> s{QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("error")};
    return s;
}

} // namespace

bool AlertConfig::isMuted(const QString& id, const QDate& today) const {
    for (const QString& k : {id, QStringLiteral("*")}) {
        const QDate until = QDate::fromString(muted.value(k), Qt::ISODate);
        if (until.isValid() && until >= today) return true;
    }
    return false;
}

QVector<sitewatch::RuleInfo> ruleCatalog() {
    return {
        {"stale_data", "Données GitHub en retard",
         "Aucune donnée récente pour ce dépôt : la publication de SiteWatch ne tourne plus.", "warning"},
        {"views_drop", "Chute des vues",
         "Les vues des 7 derniers jours sont très inférieures à celles des 7 jours précédents.", "warning"},
        {"views_spike", "Pic de vues",
         "Une journée très au-dessus de l'habitude du dépôt (article, réseau social, mention).", "info"},
        {"clones_spike", "Pic de clones",
         "Beaucoup de clones en une journée : succès, intégration continue ou robot.", "info"},
        {"new_release", "Nouvelle release", "Une nouvelle version est publiée sur le dépôt.", "info"},
    };
}

AlertConfig alertConfigFromJson(const QJsonObject& o) {
    AlertConfig c;
    auto num = [&](const char* k, double def, double lo, double hi) {
        const QJsonValue v = o.value(QLatin1String(k));
        return v.isDouble() ? std::max(lo, std::min(hi, v.toDouble())) : def;
    };
    c.dropPct = num("drop_pct", c.dropPct, 10, 95);
    c.spikeFactor = num("spike_factor", c.spikeFactor, 1.5, 20);
    c.minViews = static_cast<int>(num("min_views", c.minViews, 5, 1000));
    c.staleWarnDays = static_cast<int>(num("stale_warn_days", c.staleWarnDays, 1, 30));
    c.staleErrorDays = static_cast<int>(num("stale_error_days", c.staleErrorDays, 1, 60));
    if (c.staleErrorDays < c.staleWarnDays) c.staleErrorDays = c.staleWarnDays;
    const QJsonObject rules = o.value(QStringLiteral("rules")).toObject();
    for (auto it = rules.begin(); it != rules.end(); ++it) {
        const QJsonObject r = it.value().toObject();
        sitewatch::RuleSetting s;
        s.enabled = r.value(QStringLiteral("enabled")).toBool(true);
        s.notify = r.value(QStringLiteral("notify")).toBool(true);
        const QString lvl = r.value(QStringLiteral("level")).toString();
        s.level = validLevels().contains(lvl) ? lvl : QString();
        s.targets = sitewatch::sanitizeTargets(r.value(QStringLiteral("targets")).toArray());
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
    return QJsonObject{{"drop_pct", c.dropPct}, {"spike_factor", c.spikeFactor}, {"min_views", c.minViews},
                       {"stale_warn_days", c.staleWarnDays}, {"stale_error_days", c.staleErrorDays},
                       {"rules", rules}, {"muted", muted}};
}

QVector<sitewatch::Alert> evaluateAlerts(const QJsonObject& repo, const QDate& today, const AlertConfig& cfg) {
    QVector<sitewatch::Alert> out;
    const QString full = repo.value(QStringLiteral("full_name")).toString();
    const Daily views = seriesOf(repo, QStringLiteral("views"));
    const Daily clones = seriesOf(repo, QStringLiteral("clones"));
    if (full.isEmpty()) return out;

    auto add = [&](const QString& rule, const QString& defLevel, const QString& keyPart, const QString& title,
                   const QString& message) {
        const sitewatch::RuleSetting rs = cfg.rule(rule);
        if (!rs.enabled) return;
        sitewatch::Alert a;
        a.key = QStringLiteral("%1|github:%2|%3").arg(rule, full, keyPart);
        a.rule = rule;
        a.level = rs.level.isEmpty() ? defLevel : rs.level;
        a.day = dayStr(today);
        a.title = title;
        a.message = message;
        a.notify = rs.notify;
        a.targets = rs.targets;
        a.muted = cfg.isMuted(rule, today);
        out.append(a);
    };

    // Dernier jour connu (vues ou clones) : base du controle de fraicheur et des fenetres.
    QDate last;
    for (const Daily* d : {&views, &clones})
        if (!d->isEmpty() && (!last.isValid() || d->lastKey() > last)) last = d->lastKey();

    // 1. Silence de la publication : on mesure la derniere PUBLICATION de SiteWatch (champ
    //    `last_imported_at`), pas le dernier jour de trafic, car GitHub n'ecrit pas les jours sans
    //    visite : un depot calme n'est pas un depot en panne. Repli sur le dernier jour connu.
    QDate published = QDateTime::fromSecsSinceEpoch(
        static_cast<qint64>(repo.value(QStringLiteral("last_imported_at")).toDouble())).date();
    if (repo.value(QStringLiteral("last_imported_at")).toDouble() <= 0) published = last;
    if (published.isValid()) {
        const int lag = static_cast<int>(published.daysTo(today));
        if (lag >= cfg.staleWarnDays)
            add(QStringLiteral("stale_data"),
                lag >= cfg.staleErrorDays ? QStringLiteral("error") : QStringLiteral("warning"), dayStr(published),
                QStringLiteral("%1 : données en retard").arg(full),
                QStringLiteral("Dernière publication de SiteWatch le %1, soit %2 jours de retard. "
                               "Lancer l'application ou planifier la publication.").arg(dayStr(published)).arg(lag));
    }

    // Fenetres jugees : les jours COMPLETS, jusqu'a la veille (aujourd'hui est partiel).
    const QDate ref = today.addDays(-1);
    if (last.isValid() && last.daysTo(today) <= 14) {
        // 2. Chute des vues : 7 derniers jours contre les 7 precedents. Une cle par semaine ISO :
        //    la situation n'est annoncee qu'une fois par semaine, pas chaque jour.
        const double recent = sumBetween(views, ref.addDays(-6), ref);
        const double prior = sumBetween(views, ref.addDays(-13), ref.addDays(-7));
        if (prior >= cfg.minViews && recent <= prior * (1 - cfg.dropPct / 100.0))
            add(QStringLiteral("views_drop"), QStringLiteral("warning"),
                QStringLiteral("%1-W%2").arg(ref.year()).arg(ref.weekNumber()),
                QStringLiteral("%1 : chute des vues").arg(full),
                QStringLiteral("%1 vues sur les 7 derniers jours contre %2 sur les 7 précédents (%3 %%).")
                    .arg(static_cast<long>(recent)).arg(static_cast<long>(prior))
                    .arg(static_cast<long>(std::lround((recent - prior) * 100.0 / prior))));

        // 3 et 4. Pics du dernier jour complet par rapport a la mediane des 14 jours d'avant.
        struct Spike { const Daily* d; const char* rule; const char* label; };
        const Spike spikes[] = {{&views, "views_spike", "vues"}, {&clones, "clones_spike", "clones"}};
        for (const Spike& s : spikes) {
            const double v = s.d->value(ref, 0);
            const double med = medianBetween(*s.d, ref.addDays(-14), ref.addDays(-1));
            if (v >= cfg.minViews && v >= std::max(1.0, med) * cfg.spikeFactor)
                add(QLatin1String(s.rule), QStringLiteral("info"), dayStr(ref),
                    QStringLiteral("%1 : pic de %2").arg(full, QLatin1String(s.label)),
                    QStringLiteral("%1 %2 le %3 (habituellement environ %4).")
                        .arg(static_cast<long>(v)).arg(QLatin1String(s.label)).arg(dayStr(ref)).arg(static_cast<long>(med)));
        }
    }

    // 5. Nouvelle release : la cle porte l'etiquette, donc une release deja connue ne se repete jamais
    //    (le premier passage sur un depot est un etat des lieux silencieux, voir SiteWatchAlertStore).
    const QString tag = repo.value(QStringLiteral("last_release")).toString();
    if (!tag.isEmpty())
        add(QStringLiteral("new_release"), QStringLiteral("info"), tag,
            QStringLiteral("%1 : nouvelle release %2").arg(full, tag),
            QStringLiteral("La release %1 est publiée sur %2.").arg(tag, full));

    return out;
}

} // namespace github
} // namespace morfanalytics
