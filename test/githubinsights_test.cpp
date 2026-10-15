/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Test de GitHubInsights : fonctions pures, verifiees sur des depots construits a la main dont
 * on connait la reponse (chute, pic, retard de publication, release, reglages, sourdine).
 */

#include <cstdio>

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>

#include "morfanalytics/analysis/GitHubInsights.h"

using namespace morfanalytics;
using namespace morfanalytics::github;

static int failures = 0;
static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

static const QDate kToday(2026, 10, 15);

// Depot : vues du jour J-13 a J-1 donnees par `perDay(i)` (i = 0 pour le plus ancien).
template <typename F>
static QJsonObject repoWith(F perDay, const QString& tag = QString(), qint64 importedAt = 0) {
    QJsonArray daily;
    for (int i = 0; i < 14; ++i)
        daily.append(QJsonObject{{"day", kToday.addDays(-14 + i).toString(Qt::ISODate)},
                                 {"metric", "views"}, {"count", perDay(i)}});
    return QJsonObject{{"full_name", "morfredus/Demo"}, {"daily", daily}, {"last_release", tag},
                       {"last_imported_at", static_cast<double>(importedAt)}};
}
static qint64 yesterdayNoon() { return QDateTime(kToday.addDays(-1), QTime(12, 0)).toSecsSinceEpoch(); }
static bool has(const QVector<sitewatch::Alert>& a, const char* rule) {
    for (const auto& x : a) if (x.rule == QLatin1String(rule)) return true;
    return false;
}

int main() {
    const qint64 fresh = yesterdayNoon();

    // Trafic stable : rien a signaler.
    check(evaluateAlerts(repoWith([](int) { return 20; }, QString(), fresh), kToday).isEmpty(),
          "trafic stable : aucune alerte");

    // 7 premiers jours a 40, 7 derniers a 5 : chute de 87 %.
    const auto drop = evaluateAlerts(repoWith([](int i) { return i < 7 ? 40 : 5; }, QString(), fresh), kToday);
    check(has(drop, "views_drop"), "chute des vues detectee");
    check(drop.value(0).key.startsWith("views_drop|github:morfredus/Demo|"), "cle stable par depot et semaine");

    // Petit volume : la meme chute en proportion n'est que du bruit.
    check(!has(evaluateAlerts(repoWith([](int i) { return i < 7 ? 4 : 1; }, QString(), fresh), kToday), "views_drop"),
          "volume plancher : pas de chute sur de tres petits nombres");

    // Pic : 200 vues hier contre ~10 d'habitude.
    const auto spike = evaluateAlerts(repoWith([](int i) { return i == 13 ? 200 : 10; }, QString(), fresh), kToday);
    check(has(spike, "views_spike"), "pic de vues detecte");
    check(!has(spike, "views_drop"), "un pic n'est pas une chute");

    // Retard de publication : derniere publication il y a 5 jours = avertissement, 9 = erreur.
    const qint64 old5 = QDateTime(kToday.addDays(-5), QTime(12, 0)).toSecsSinceEpoch();
    const qint64 old9 = QDateTime(kToday.addDays(-9), QTime(12, 0)).toSecsSinceEpoch();
    const auto st5 = evaluateAlerts(repoWith([](int) { return 20; }, QString(), old5), kToday);
    check(has(st5, "stale_data") && st5.value(0).level == "warning", "retard 5 j : avertissement");
    const auto st9 = evaluateAlerts(repoWith([](int) { return 20; }, QString(), old9), kToday);
    check(has(st9, "stale_data") && st9.value(0).level == "error", "retard 9 j : erreur");

    // Release : la cle porte l'etiquette.
    const auto rel = evaluateAlerts(repoWith([](int) { return 20; }, "v1.2.0", fresh), kToday);
    check(has(rel, "new_release") && rel.value(0).key.endsWith("|v1.2.0"), "nouvelle release, cle = etiquette");

    // Reglages : regle coupee, niveau force, sourdine.
    AlertConfig cfg = alertConfigFromJson(QJsonObject{
        {"rules", QJsonObject{{"views_drop", QJsonObject{{"enabled", false}}},
                              {"new_release", QJsonObject{{"level", "error"}, {"notify", false}}}}},
        {"muted", QJsonObject{{"new_release", "2026-10-20"}}}});
    const auto repoDrop = repoWith([](int i) { return i < 7 ? 40 : 5; }, "v2", fresh);
    const auto custom = evaluateAlerts(repoDrop, kToday, cfg);
    check(!has(custom, "views_drop"), "regle desactivee : pas d'alerte");
    check(has(custom, "new_release") && custom.value(0).level == "error" && !custom.value(0).notify && custom.value(0).muted,
          "niveau force, envoi coupe, sourdine active");
    cfg.muted["new_release"] = "2026-10-01";
    check(!evaluateAlerts(repoDrop, kToday, cfg).value(0).muted, "sourdine expiree");

    // Bornes : une valeur aberrante ne coupe jamais la detection.
    const AlertConfig wild = alertConfigFromJson(QJsonObject{{"drop_pct", 0}, {"spike_factor", 99999}, {"stale_warn_days", 9},
                                                             {"stale_error_days", 2}});
    check(wild.dropPct == 10 && wild.spikeFactor == 20 && wild.staleErrorDays == 9, "valeurs bornees");

    // Aller-retour JSON.
    check(alertConfigFromJson(alertConfigToJson(cfg)).rules.value("new_release").level == "error", "config aller-retour");

    std::printf(failures ? "ECHEC (%d)\n" : "Tout est conforme.\n", failures);
    return failures ? 1 : 0;
}
