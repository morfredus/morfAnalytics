/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Test de SiteWatchAlertStore sur une base SQLite en memoire : etat des lieux initial
 * silencieux, une seule notification par situation, nouvelle tentative apres un echec
 * d'envoi, sourdine, reglages et retention.
 */

#include <cstdio>

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "morfanalytics/data/SiteWatchAlertStore.h"

using namespace morfanalytics;
using sitewatch::Alert;

static int failures = 0;
static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

static Alert mk(const char* key, const char* level, bool muted = false, bool notify = true) {
    Alert a;
    a.key = key; a.rule = "e500"; a.level = level; a.day = "2026-10-09";
    a.title = "titre"; a.message = "message"; a.muted = muted; a.notify = notify;
    return a;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "test");
    db.setDatabaseName(":memory:");
    check(db.open(), "base en memoire");
    SiteWatchAlertStore store(db);
    check(store.ensureSchema(), "schema cree");
    check(store.ensureSchema(), "schema idempotent (migration relancee)");

    // Etat des lieux : premiere evaluation silencieuse.
    auto first = store.record("s1", {mk("a|s1|d1", "error")}, "warning", true);
    check(first.isEmpty(), "premiere evaluation du site : rien n'est envoye");
    // Nouvelle situation : envoyee, une seule fois.
    auto second = store.record("s1", {mk("a|s1|d1", "error"), mk("b|s1|d2", "error")}, "warning", true);
    check(second.size() == 1 && second[0].key == "b|s1|d2", "seule la situation nouvelle est a envoyer");
    check(store.record("s1", {mk("b|s1|d2", "error")}, "warning", true).isEmpty(), "meme situation : pas de doublon");

    // Echec d'envoi : retente ; succes : plus rien a envoyer.
    check(store.due().size() == 1, "alerte en attente d'envoi");
    store.markDelivery("b|s1|d2", false, "morfNotify injoignable");
    check(store.due().size() == 1, "apres un echec : a retenter");
    store.markDelivery("b|s1|d2", true, QString());
    check(store.due().isEmpty(), "apres un succes : plus rien a envoyer");

    // Plafond d'essais.
    store.record("s1", {mk("c|s1|d3", "error")}, "warning", true);
    for (int i = 0; i < 5; ++i) store.markDelivery("c|s1|d3", false, "x");
    check(store.due().isEmpty(), "apres 5 echecs : abandon (visible dans l'historique)");
    check(store.failedCount() == 1, "echecs comptes pour /status");

    // Destinations : conservees avec l'alerte, y compris pour une nouvelle tentative.
    Alert withTargets = mk("t|s1|d", "error");
    withTargets.targets = {"telegram", "mail"};
    auto sent = store.record("s1", {withTargets}, "warning", true);
    check(sent.size() == 1 && sent[0].targets == QStringList({"telegram", "mail"}), "destinations transmises a l'envoi");
    store.markDelivery("t|s1|d", false, "x");
    bool found = false;
    for (const auto& p : store.due()) if (p.key == "t|s1|d") found = p.targets == QStringList({"telegram", "mail"});
    check(found, "destinations conservees pour la nouvelle tentative");
    store.markDelivery("t|s1|d", true, QString());

    // Seuil de niveau, envoi coupe, regle muette, regle sans envoi.
    check(store.record("s1", {mk("d|s1|d", "info")}, "warning", true).isEmpty(), "niveau sous le seuil : non envoye");
    check(store.record("s1", {mk("e|s1|d", "error")}, "warning", false).isEmpty(), "envoi coupe globalement : non envoye");
    check(store.record("s1", {mk("f|s1|d", "error", true)}, "warning", true).isEmpty(), "regle en sourdine : non envoyee");
    check(store.record("s1", {mk("g|s1|d", "error", false, false)}, "warning", true).isEmpty(), "regle sans envoi : non envoyee");
    QJsonArray hist = store.history("s1", 100);
    QString states;
    for (const QJsonValue& v : hist) states += v.toObject().value("key").toString() + "=" + v.toObject().value("delivery").toString() + ";";
    check(states.contains("f|s1|d=muted") && states.contains("d|s1|d=skipped") && states.contains("b|s1|d2=sent")
          && states.contains("c|s1|d3=failed"), "historique : etat d'envoi de chaque alerte");
    check(store.history("autre", 10).isEmpty(), "historique filtre par site");

    // Reglages.
    check(store.settings("global").isEmpty(), "pas de reglage au depart");
    store.saveSettings("global", QJsonObject{{"sensitivity", 5.0}});
    store.saveSettings("site:s1", QJsonObject{{"sensitivity", 7.0}});
    check(store.settings("global").value("sensitivity").toDouble() == 5.0, "reglage global relu");
    check(store.settings("site:s1").value("sensitivity").toDouble() == 7.0, "reglage de site relu");

    // Retention des rapports.
    QSqlQuery q(db);
    for (int i = 0; i < 6; ++i)
        q.exec(QString("INSERT INTO sitewatch_report(site_id, site_label, received_at, payload) VALUES('s1','x',%1,'{}')").arg(i));
    q.exec("INSERT INTO sitewatch_report(site_id, site_label, received_at, payload) VALUES('s2','x',1,'{}')");
    store.pruneReports("s1", 3);
    q.exec("SELECT COUNT(*) FROM sitewatch_report WHERE site_id='s1'"); q.next();
    check(q.value(0).toInt() == 3, "retention : 3 rapports conserves pour le site");
    q.exec("SELECT COUNT(*) FROM sitewatch_report WHERE site_id='s2'"); q.next();
    check(q.value(0).toInt() == 1, "retention : les autres sites ne sont pas touches");

    std::printf(failures ? "ECHEC (%d)\n" : "Tout est conforme.\n", failures);
    return failures ? 1 : 0;
}
