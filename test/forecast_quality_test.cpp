/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Test de la qualite de prevision (« prevu vs observe »).
 *
 * Deux etages :
 *   1) Les fonctions PURES de ForecastQuality (biais, MAE, RMSE, indice,
 *      libelle), verifiees sur des cas ou l'on SAIT le resultat attendu :
 *      prevision parfaite, erreur constante +/-2 C, erreurs qui s'annulent,
 *      journees manquantes, historique insuffisant, valeurs invalides.
 *   2) L'analyse complete analyzeForecastVsObserved de bout en bout, sur un
 *      ForecastStore + SampleStore synthetiques a erreur connue, pour verifier
 *      la COHERENCE entre ce que calcule le backend et ce que lira l'interface
 *      (fenetres, en-tete, biais/MAE/indice).
 *
 * Compile via l'option CMake MA_BUILD_TESTS. Retourne 0 si tout passe.
 */

#include <cstdio>
#include <cmath>
#include <limits>
#include <vector>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

#include "morfanalytics/analysis/ForecastQuality.h"
#include "morfanalytics/analysis/AnalysisRegistry.h"
#include "morfanalytics/data/SampleStore.h"
#include "morfanalytics/data/ForecastStore.h"

using namespace morfanalytics;

static int failures = 0;

static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

static void checkNear(double got, double expected, double tol, const char* msg) {
    const bool ok = std::fabs(got - expected) <= tol;
    if (ok) std::printf("ok  : %s (%.3f ~ %.3f)\n", msg, got, expected);
    else { std::printf("FAIL: %s (obtenu %.3f, attendu %.3f +/- %.3f)\n",
                       msg, got, expected, tol); ++failures; }
}

// ---------------------------------------------------------------------------
// Etage 1 : fonctions pures
// ---------------------------------------------------------------------------
static void testPureFunctions() {
    using namespace forecastq;
    const double NaN = std::numeric_limits<double>::quiet_NaN();

    // 1) Prevision parfaitement exacte : tous les ecarts nuls.
    {
        const ErrorStats s = accumulate({0.0, 0.0, 0.0, 0.0});
        check(s.count == 4, "parfait : 4 couples comptes");
        checkNear(s.bias, 0.0, 1e-9, "parfait : biais nul");
        checkNear(s.mae, 0.0, 1e-9, "parfait : MAE nulle");
        checkNear(s.rmse, 0.0, 1e-9, "parfait : RMSE nulle");
        checkNear(reliabilityIndex(s.mae), 100.0, 1e-9, "parfait : indice 100");
        check(reliabilityLabel(s.mae, s.count) == QStringLiteral("Fiabilité élevée"),
              "parfait : libelle « Fiabilité élevée »");
    }

    // 2) Erreur constante de +2 C (observe plus chaud que prevu).
    {
        const ErrorStats s = accumulate({2.0, 2.0, 2.0});
        checkNear(s.bias, 2.0, 1e-9, "+2 : biais +2");
        checkNear(s.mae, 2.0, 1e-9, "+2 : MAE 2");
        checkNear(s.rmse, 2.0, 1e-9, "+2 : RMSE 2");
        // index = 100 * (1 - 2/6) = 66.67 -> arrondi 67
        checkNear(reliabilityIndex(s.mae), 67.0, 0.5, "+2 : indice ~67");
    }

    // 3) Erreur constante de -2 C : meme MAE, biais oppose.
    {
        const ErrorStats s = accumulate({-2.0, -2.0, -2.0, -2.0});
        checkNear(s.bias, -2.0, 1e-9, "-2 : biais -2");
        checkNear(s.mae, 2.0, 1e-9, "-2 : MAE 2 (identique au cas +2)");
    }

    // 4) Erreurs +/- qui s'annulent dans le biais, mais pas dans la MAE.
    {
        const ErrorStats s = accumulate({2.0, -2.0, 2.0, -2.0});
        checkNear(s.bias, 0.0, 1e-9, "compensation : biais nul");
        checkNear(s.mae, 2.0, 1e-9, "compensation : MAE reste 2 (les ecarts existent)");
        check(s.mae > std::fabs(s.bias),
              "compensation : MAE > |biais| quand les erreurs se compensent");
    }

    // 5) MAE correctement calculee sur des ecarts inegaux.
    {
        const ErrorStats s = accumulate({1.0, -3.0, 2.0}); // |.|=1,3,2 -> MAE 2
        checkNear(s.mae, 2.0, 1e-9, "MAE : moyenne des valeurs absolues");
        checkNear(s.bias, 0.0, 1e-9, "MAE : biais = (1-3+2)/3 = 0");
        // RMSE = sqrt((1+9+4)/3) = sqrt(4.667) ~ 2.16, > MAE : les gros ecarts pesent plus
        check(s.rmse > s.mae, "RMSE : superieure a la MAE en presence d'un gros ecart");
    }

    // 6) Journees manquantes : les ecarts non finis sont ignores, pas comptes 0.
    {
        const ErrorStats s = accumulate({2.0, NaN, 2.0, NaN});
        check(s.count == 2, "manquantes : seules 2 valeurs finies comptees");
        checkNear(s.mae, 2.0, 1e-9, "manquantes : MAE non diluee par les trous");
    }

    // 7) Historique insuffisant : sous le minimum, pas de qualification.
    {
        check(reliabilityLabel(0.5, kMinDaysForIndex - 1)
                  == QStringLiteral("Données insuffisantes"),
              "insuffisant : « Données insuffisantes » sous le minimum de jours");
        check(reliabilityLabel(0.5, kMinDaysForIndex) != QStringLiteral("Données insuffisantes"),
              "suffisant : au minimum de jours, on qualifie");
    }

    // 8) Valeurs nulles / invalides.
    {
        const ErrorStats empty = accumulate({});
        check(!empty.valid(), "vide : valid() faux, aucun couple");
        const ErrorStats onlyNan = accumulate({NaN, NaN});
        check(!onlyNan.valid(), "que des NaN : aucun couple valide");
        checkNear(reliabilityIndex(NaN), 0.0, 1e-9, "indice(NaN) = 0 (pas de fuite)");
        checkNear(reliabilityIndex(-1.0), 0.0, 1e-9, "indice(MAE negative) = 0");
    }

    // 9) Stabilite / bornes de l'indice et paliers qualitatifs documentes.
    {
        checkNear(reliabilityIndex(0.0), 100.0, 1e-9, "indice : borne haute 100 a MAE 0");
        checkNear(reliabilityIndex(kDefaultMaxMae), 0.0, 1e-9, "indice : 0 a la MAE plafond");
        checkNear(reliabilityIndex(kDefaultMaxMae * 2.0), 0.0, 1e-9,
                  "indice : reste borne a 0 au-dela du plafond");
        // Monotonie : plus l'erreur grandit, plus l'indice baisse.
        check(reliabilityIndex(1.0) > reliabilityIndex(3.0),
              "indice : decroissant avec la MAE");
        // Paliers
        check(reliabilityLabel(0.8, 10) == QStringLiteral("Fiabilité élevée"),
              "palier : MAE 0.8 -> elevee");
        check(reliabilityLabel(1.6, 10) == QStringLiteral("Fiabilité correcte"),
              "palier : MAE 1.6 -> correcte");
        check(reliabilityLabel(3.0, 10) == QStringLiteral("Fiabilité variable"),
              "palier : MAE 3.0 -> variable");
        check(reliabilityLabel(5.0, 10) == QStringLiteral("Fiabilité faible"),
              "palier : MAE 5.0 -> faible");
    }
}

// ---------------------------------------------------------------------------
// Etage 2 : analyse de bout en bout (coherence backend)
// ---------------------------------------------------------------------------
static void testEndToEnd() {
    const QString obsDb = QDir(QDir::tempPath()).filePath("morfanalytics_fc_obs.sqlite");
    const QString fcDb  = QDir(QDir::tempPath()).filePath("morfanalytics_fc_cache.sqlite");
    QDir().remove(obsDb);
    QDir().remove(fcDb);

    SampleStore store(obsDb, {"temp", "hum", "pres"});
    ForecastStore fcStore(fcDb);
    if (!store.open() || !fcStore.open()) {
        std::printf("FAIL: ouverture des caches\n"); ++failures; return;
    }

    // 20 jours consecutifs finissant aujourd'hui. Chaque jour : observations dont
    // les extremes sont connus (min 10, max 20), et une prevision decalee d'un
    // ecart CONSTANT de +2 C (observe plus chaud que prevu) sur min et max.
    constexpr int kDays = 20;
    const QDate today = QDate::currentDate();
    qint64 nowTs = 0;
    for (int i = 0; i < kDays; ++i) {
        const QDate date = today.addDays(-(kDays - 1 - i));
        const quint32 dayKey =
            quint32(date.year() * 10000 + date.month() * 100 + date.day());
        const qint64 dayStart = QDateTime(date, QTime(0, 0)).toSecsSinceEpoch();

        // Releves de la journee : min=10, max=20 (par construction).
        const QVector<double> temps = {10.0, 13.0, 16.0, 20.0, 15.0, 11.0};
        QVector<qint64> ts;
        QVector<QHash<QString, double>> vals;
        for (int h = 0; h < temps.size(); ++h) {
            const qint64 t = dayStart + qint64(h) * 4 * 3600; // etale sur la journee
            ts.push_back(t);
            vals.push_back({{"temp", temps[h]}, {"hum", 60.0}, {"pres", 1013.0}});
            nowTs = t;
        }
        if (!store.insertBatch(dayKey, 0, ts, vals)) {
            std::printf("FAIL: insertion obs (%s)\n",
                        store.lastError().toUtf8().constData()); ++failures; return;
        }

        // Prevision : fcMax = 18 (eMax = 20-18 = +2), fcMin = 8 (eMin = 10-8 = +2).
        ForecastEntry fe;
        fe.targetDay = dayKey;
        fe.issuedTs  = dayStart - 86400;
        fe.tempMin   = 8.0;
        fe.tempMax   = 18.0;
        fe.description = QStringLiteral("ciel variable");
        if (!fcStore.upsert(fe)) {
            std::printf("FAIL: upsert prevision (%s)\n",
                        fcStore.lastError().toUtf8().constData()); ++failures; return;
        }
    }

    AnalysisRegistry reg;
    registerMeteoAnalyses(reg);

    AnalysisContext ctx;
    ctx.store         = &store;
    ctx.storeIn       = &store;
    ctx.storeOut      = &store;   // analyse OUT : la source primaire est le cache exterieur
    ctx.forecastStore = &fcStore;
    ctx.now           = nowTs;

    const QJsonObject r = reg.run("forecast_vs_observed", ctx, {});
    check(r.value("ok").toBool(), "e2e : analyse disponible (ok)");

    const QJsonArray windows = r.value("windows").toArray();
    check(windows.size() == 4, "e2e : quatre fenetres (3/7/14/30)");

    // En-tete : fenetre de reference suffisante, MAE ~2, biais ~+2, indice ~67.
    const QJsonObject head = r.value("headline").toObject();
    check(head.value("sufficient").toBool(), "e2e : en-tete suffisant (assez de jours)");
    checkNear(head.value("mae").toDouble(), 2.0, 0.05, "e2e : MAE globale ~2 C");
    checkNear(head.value("index").toDouble(), 67.0, 1.5, "e2e : indice global ~67");
    check(head.value("quality").toString() == QStringLiteral("Fiabilité correcte"),
          "e2e : qualite « Fiabilité correcte » (MAE 2 C)");

    // Coherence min/max sur une fenetre : biais signe +2, moyennes coherentes.
    bool checkedWindow = false;
    for (const QJsonValue& v : windows) {
        const QJsonObject w = v.toObject();
        if (w.value("days_requested").toInt() != 7) continue;
        checkedWindow = true;
        check(w.value("sufficient").toBool(), "e2e/7j : fenetre suffisante");
        const QJsonObject tmax = w.value("tmax").toObject();
        const QJsonObject tmin = w.value("tmin").toObject();
        checkNear(tmax.value("bias").toDouble(), 2.0, 0.05, "e2e/7j : biais T max +2");
        checkNear(tmax.value("mae").toDouble(), 2.0, 0.05, "e2e/7j : MAE T max 2");
        checkNear(tmax.value("observed_mean").toDouble(), 20.0, 0.05,
                  "e2e/7j : observe max moyen = 20");
        checkNear(tmax.value("forecast_mean").toDouble(), 18.0, 0.05,
                  "e2e/7j : prevu max moyen = 18");
        checkNear(tmin.value("bias").toDouble(), 2.0, 0.05, "e2e/7j : biais T min +2");
        checkNear(tmin.value("observed_mean").toDouble(), 10.0, 0.05,
                  "e2e/7j : observe min moyen = 10");
    }
    check(checkedWindow, "e2e : fenetre 7 jours presente et verifiee");

    store.close();
    fcStore.close();
    QDir().remove(obsDb);
    QDir().remove(fcDb);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);  // QSqlDatabase a besoin d'une boucle d'application

    testPureFunctions();
    testEndToEnd();

    std::printf("\n%s (%d echec%s)\n",
                failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ECHOUENT",
                failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
