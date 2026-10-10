/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Test de SiteWatchInsights : fonctions pures, verifiees sur des rapports construits a
 * la main dont on connait la reponse (fenetre, comparaison, anomalies, alertes).
 *
 * Compile via l'option CMake MA_BUILD_TESTS. Retourne 0 si tout passe.
 */

#include <cstdio>
#include <functional>

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>

#include "morfanalytics/analysis/SiteWatchInsights.h"

using namespace morfanalytics::sitewatch;

static int failures = 0;
static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

static const QDate kToday(2026, 10, 10);

// 40 jours (jusqu'a `last`) de trafic stable ; `tweak(serie, jour, valeur)` permet d'injecter un incident.
static QJsonObject makeReport(const QDate& last,
        const std::function<double(const QString&, const QDate&, double)>& tweak = nullptr,
        int nDays = 40) {
    const struct { const char* key; double base; } defs[] = {
        {"daily_humans", 100}, {"daily_bots", 120}, {"daily_ai", 30}, {"daily_seo", 30},
        {"daily_404", 40}, {"daily_403", 2}, {"daily_500", 0}, {"daily_attacks", 12}, {"daily_normal", 60}};
    QJsonObject stats;
    for (const auto& d : defs) {
        QJsonObject days;
        for (int i = 0; i < nDays; ++i) {
            const QDate day = last.addDays(-i);
            double v = d.base + (d.base > 0 ? (i % 3) : 0);   // un peu de variation (MAD non nul), sauf les 500
            if (tweak) v = tweak(QString::fromLatin1(d.key), day, v);
            days.insert(day.toString(Qt::ISODate), v);
        }
        stats.insert(QLatin1String(d.key), days);
    }
    stats.insert("bot_counts", QJsonObject{{"Claude", 50}, {"Google", 200}, {"Ahrefs", 30}, {"Inconnu", 5}});
    return QJsonObject{{"site_id", "s1"}, {"site_label", "monsite.fr"},
        {"source_up_to", last.toString(Qt::ISODate)}, {"stats", stats}};
}

static bool hasRule(const QVector<Alert>& v, const char* rule) {
    for (const Alert& a : v) if (a.rule == QLatin1String(rule)) return true;
    return false;
}

int main() {
    // Categories de robots.
    check(botCategory("Claude") == "ia", "Claude = robot IA");
    check(botCategory("Ahrefs") == "seo", "Ahrefs = robot SEO");
    check(botCategory("Google") == "moteur", "Google = moteur de recherche");
    check(botCategory("Truc") == "autre", "robot inconnu = autre");

    // Trafic stable et a jour : aucune alerte.
    const QDate yesterday = kToday.addDays(-1);
    const QJsonObject calm = makeReport(yesterday);
    check(evaluateAlerts(calm, kToday).isEmpty(), "trafic stable : aucune alerte");

    // Fenetre et comparaison : 7 jours, periode precedente de meme longueur.
    const QJsonObject ins = insights(calm, kToday.addDays(-7), yesterday, kToday);
    check(ins.value("window").toObject().value("days").toInt() == 7, "fenetre de 7 jours");
    check(ins.value("series").toObject().value("dates").toArray().size() == 7, "7 dates alignees");
    check(ins.value("kpis").toObject().value("humans").toObject().contains("delta_pct"), "variation vs periode precedente");
    check(ins.value("anomalies").toArray().isEmpty(), "pas d'anomalie sur trafic stable");
    check(ins.value("tops").toObject().value("bots").toObject().value("by_category").toObject()
              .value("ia").toDouble() == 50, "robots regroupes par famille");

    // Erreurs 500 hier : alerte erreur, cle stable.
    const QJsonObject broken = makeReport(yesterday, [&](const QString& k, const QDate& d, double v) {
        return (k == "daily_500" && d == yesterday) ? 12.0 : v; });
    QVector<Alert> a = evaluateAlerts(broken, kToday);
    check(hasRule(a, "e500"), "erreurs 500 recentes : alerte");
    bool errLevel = false, keyOk = false;
    for (const Alert& x : a) if (x.rule == "e500") { errLevel = x.level == "error"; keyOk = x.key == "e500|s1|2026-10-09"; }
    check(errLevel, "12 erreurs 500 = niveau erreur");
    check(keyOk, "cle d'alerte stable (regle|site|jour)");

    // Pic d'attaques hier : alerte + anomalie dans la fenetre.
    const QJsonObject attacked = makeReport(yesterday, [&](const QString& k, const QDate& d, double v) {
        return (k == "daily_attacks" && d == yesterday) ? 400.0 : v; });
    check(hasRule(evaluateAlerts(attacked, kToday), "attack_spike"), "pic d'attaques : alerte");
    bool anomalyFound = false;
    for (const QJsonValue& v : insights(attacked, kToday.addDays(-7), yesterday, kToday).value("anomalies").toArray())
        if (v.toObject().value("series").toString() == "attacks") anomalyFound = true;
    check(anomalyFound, "pic d'attaques : anomalie dans l'analyse");

    // Un incident ANCIEN ne declenche rien (seuls les 3 derniers jours comptent).
    const QDate old = kToday.addDays(-20);
    const QJsonObject past = makeReport(yesterday, [&](const QString& k, const QDate& d, double v) {
        return (k == "daily_500" && d == old) ? 50.0 : v; });
    check(!hasRule(evaluateAlerts(past, kToday), "e500"), "incident ancien : pas d'alerte");

    // Chute du trafic humain hier.
    const QJsonObject drop = makeReport(yesterday, [&](const QString& k, const QDate& d, double v) {
        return (k == "daily_humans" && d == yesterday) ? 0.0 : v; });
    check(hasRule(evaluateAlerts(drop, kToday), "traffic_drop"), "chute du trafic : alerte");

    // SiteWatch silencieux depuis 4 jours.
    const QJsonObject stale = makeReport(kToday.addDays(-4));
    a = evaluateAlerts(stale, kToday);
    bool staleErr = false;
    for (const Alert& x : a) if (x.rule == "stale_data") staleErr = x.level == "error";
    check(staleErr, "4 jours sans donnees : alerte erreur");
    check(!hasRule(evaluateAlerts(calm, kToday), "stale_data"), "donnees d'hier : pas de retard");

    // Pas assez d'historique (5 jours) : pas de faux positif meme avec un pic.
    const QJsonObject tiny = makeReport(yesterday, [&](const QString& k, const QDate& d, double v) {
        return (k == "daily_attacks" && d == yesterday) ? 900.0 : v; }, 5);
    check(!hasRule(evaluateAlerts(tiny, kToday), "attack_spike"), "moins de 14 jours d'historique : pas de jugement");

    // ---- Configuration -------------------------------------------------------------
    AlertConfig off;
    off.rules["e500"].enabled = false;
    check(!hasRule(evaluateAlerts(broken, kToday, off), "e500"), "regle desactivee : pas d'alerte");

    const QJsonObject modest = makeReport(yesterday, [&](const QString& k, const QDate& d, double v) {
        return (k == "daily_attacks" && d == yesterday) ? 22.0 : v; });
    AlertConfig lax;
    lax.sensitivity = 10;
    check(hasRule(evaluateAlerts(modest, kToday), "attack_spike"), "ecart modere : alerte a la sensibilite par defaut");
    check(!hasRule(evaluateAlerts(modest, kToday, lax), "attack_spike"), "sensibilite relevee : plus d'alerte");

    AlertConfig lvl;
    lvl.rules["e500"].level = "info";
    lvl.rules["e500"].notify = false;
    for (const Alert& x : evaluateAlerts(broken, kToday, lvl))
        if (x.rule == "e500") {
            check(x.level == "info", "niveau de la regle surcharge");
            check(!x.notify, "envoi desactive pour la regle");
        }

    AlertConfig dest;
    dest.rules["e500"].targets = {"telegram", "mail"};
    for (const Alert& x : evaluateAlerts(broken, kToday, dest))
        if (x.rule == "e500") check(x.targets == QStringList({"telegram", "mail"}), "destinations propres a la regle");
    check(alertConfigFromJson(alertConfigToJson(dest)).rules["e500"].targets == QStringList({"telegram", "mail"}),
          "destinations : aller-retour JSON");
    check(sanitizeTargets(QJsonArray{"a", "a", " ", "b"}) == QStringList({"a", "b"}), "destinations : doublons et vides ecartes");

    AlertConfig quiet;
    quiet.muted["e500"] = kToday.addDays(1).toString(Qt::ISODate);
    bool muted = false, unmuted = true;
    for (const Alert& x : evaluateAlerts(broken, kToday, quiet)) if (x.rule == "e500") muted = x.muted;
    check(muted, "sourdine en cours : alerte marquee muette");
    quiet.muted["e500"] = kToday.addDays(-1).toString(Qt::ISODate);
    for (const Alert& x : evaluateAlerts(broken, kToday, quiet)) if (x.rule == "e500") unmuted = x.muted;
    check(!unmuted, "sourdine expiree : plus muette");

    AlertConfig few;
    few.recentDays = 1;
    check(evaluateAlerts(past, kToday, few).isEmpty(), "un seul jour juge : incident ancien ignore");

    // JSON : bornes, aller-retour, fusion par site.
    check(alertConfigFromJson(QJsonObject{{"sensitivity", 0.1}}).sensitivity == 2.0, "sensibilite bornee par le bas");
    check(alertConfigFromJson(QJsonObject{{"sensitivity", 99}}).sensitivity == 10.0, "sensibilite bornee par le haut");
    AlertConfig rt;
    rt.sensitivity = 5;
    rt.rules["e500"].level = "error";
    rt.muted["*"] = "2030-01-01";
    const AlertConfig back = alertConfigFromJson(alertConfigToJson(rt));
    check(back.sensitivity == 5 && back.rules["e500"].level == "error" && back.muted["*"] == "2030-01-01",
          "configuration : aller-retour JSON");
    check(alertConfigFromJson(QJsonObject{{"rules", QJsonObject{{"x", QJsonObject{{"level", "bidon"}}}}}}).rules["x"].level.isEmpty(),
          "niveau invalide ignore");
    const AlertConfig merged = mergeSiteConfig(rt, QJsonObject{{"sensitivity", 8},
        {"rules", QJsonObject{{"e404_surge", QJsonObject{{"enabled", false}}}}}});
    check(merged.sensitivity == 8 && !merged.rules["e404_surge"].enabled && merged.rules["e500"].level == "error",
          "fusion site : surcharge sans perdre le global");

    // ---- Nouveaux robots, pages en mouvement, profil horaire -----------------------
    QJsonObject withBots = makeReport(yesterday);
    QJsonObject st2 = withBots.value("stats").toObject();
    st2.insert("bots_prior", QJsonObject{{"Google", 100}});
    st2.insert("bots_recent", QJsonObject{{"Google", 120}, {"Nouveau", 80}, {"Petit", 5}});
    st2.insert("pages_recent", QJsonObject{{"/a", 100}, {"/b", 5}});
    st2.insert("pages_prior", QJsonObject{{"/a", 40}, {"/c", 60}});
    st2.insert("hourly", QJsonObject{{"humans", QJsonArray{1, 2, 3}}});
    withBots.insert("stats", st2);
    bool newBotAlert = false;
    for (const Alert& x : evaluateAlerts(withBots, kToday)) if (x.rule == "new_bot") newBotAlert = x.key == "new_bot|s1|Nouveau";
    check(newBotAlert, "nouveau robot : une alerte par robot (cle sans jour), petits volumes ignores");
    const QJsonObject w = insights(withBots, kToday.addDays(-7), yesterday, kToday);
    check(w.value("new_bots").toArray().size() == 1, "nouveaux robots listes dans l'analyse");
    check(w.value("page_movers").toObject().value("rising").toArray().at(0).toObject().value("name") == "/a", "page en hausse");
    check(w.value("page_movers").toObject().value("falling").toArray().at(0).toObject().value("name") == "/c", "page en baisse");
    check(w.value("hourly").toArray().size() == 24, "profil horaire sur 24 heures");
    check(insights(calm, kToday.addDays(-7), yesterday, kToday).value("new_bots").toArray().isEmpty(),
          "sans historique de robots : aucun nouveau robot");

    std::printf(failures ? "ECHEC (%d)\n" : "Tout est conforme.\n", failures);
    return failures ? 1 : 0;
}
