/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Test d'integration des analyses de la VAGUE 3 (anomalies MAD, correlations a
 * decalage, segmentation d'episodes) sur des donnees SYNTHETIQUES aux proprietes
 * connues : quelques valeurs franchement aberrantes, temperature et humidite
 * anticorrelees, et une periode chaude prolongee. Ce sont des maths ou une
 * erreur reste plausible ; on verifie donc le comportement sur un jeu ou l'on
 * SAIT ce qui doit ressortir.
 *
 * Compile via l'option CMake MA_BUILD_TESTS. Retourne 0 si tout passe.
 */

#include <cstdio>
#include <cmath>

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QHash>

#include "morfanalytics/data/SampleStore.h"
#include "morfanalytics/analysis/AnalysisRegistry.h"

using namespace morfanalytics;

static int failures = 0;
static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);  // QSqlDatabase a besoin d'une boucle d'application

    const QString db = QDir(QDir::tempPath()).filePath("morfanalytics_wave3_test.sqlite");
    QDir().remove(db);

    SampleStore store(db, {"temp", "hum", "pres"});
    if (!store.open()) { std::printf("FAIL: ouverture du cache (%s)\n",
                                     store.lastError().toUtf8().constData()); return 1; }

    // --- Jeu synthetique : 11 jours, un point par heure ----------------------
    constexpr qint64 kHour = 3600;
    constexpr int    kDays = 11;
    constexpr int    N     = kDays * 24;
    // Ancre a minuit local il y a 11 jours, pour des journees civiles nettes.
    const qint64 t0 = (QDateTime::currentSecsSinceEpoch() - qint64(kDays) * 86400) / 86400 * 86400;

    QVector<qint64> ts;
    QVector<QHash<QString, double>> vals;
    ts.reserve(N); vals.reserve(N);
    for (int i = 0; i < N; ++i) {
        const int day = i / 24;
        const double cycle = 5.0 * std::sin(double(i % 24) / 24.0 * 2.0 * M_PI);
        double temp = 18.0 + cycle;
        if (day >= 5 && day <= 8) temp += 14.0;      // 4 jours de canicule (Tmax > 30)
        if (i == 50 || i == 130) temp = 60.0;         // deux valeurs franchement aberrantes
        const double hum = std::clamp(95.0 - temp, 5.0, 100.0);  // anticorrelee a la temperature
        ts.push_back(t0 + qint64(i) * kHour);
        vals.push_back({{"temp", temp}, {"hum", hum}, {"pres", 1013.0}});
    }
    if (!store.insertBatch(20260101u, 0u, ts, vals)) {
        std::printf("FAIL: insertion (%s)\n", store.lastError().toUtf8().constData()); return 1;
    }

    AnalysisRegistry reg;
    registerMeteoAnalyses(reg);

    AnalysisContext ctx;
    ctx.store = &store;
    ctx.now   = ts.last();

    // --- Anomalies : les deux pics a 60 doivent ressortir (vers le haut) ------
    {
        const QJsonObject r = reg.run("anomalies", ctx, {});
        const QJsonObject temp = r.value("channels").toObject().value("temp").toObject();
        const int count = temp.value("anomalies_count").toInt();
        check(count >= 2, "anomalies : au moins les 2 pics de temperature detectes");
        const QJsonArray a = temp.value("anomalies").toArray();
        bool up = !a.isEmpty() && a.first().toObject().value("direction").toString() == "haut";
        check(up, "anomalies : le pic le plus extreme est oriente vers le haut");
        check(temp.value("median").toDouble() < 40.0,
              "anomalies : la mediane robuste ignore les aberrations (~20, pas ~60)");
    }

    // --- Correlations : temp~hum fortement negative --------------------------
    {
        const QJsonObject r = reg.run("correlations", ctx, {});
        double tempHum = 2.0;
        for (const QJsonValue& v : r.value("pairs").toArray())
            if (v.toObject().value("pair").toString() == "temp~hum")
                tempHum = v.toObject().value("best_r").toDouble();
        check(tempHum < -0.7, "correlations : temp~hum fortement negative (anticorrelees)");
    }

    // --- Episodes : une canicule d'au moins 3 jours --------------------------
    {
        const QJsonObject r = reg.run("episodes", ctx, {});
        int heatDays = 0;
        for (const QJsonValue& v : r.value("episodes").toArray()) {
            const QJsonObject e = v.toObject();
            if (e.value("type").toString() == "canicule")
                heatDays = std::max(heatDays, e.value("days").toInt());
        }
        check(heatDays >= 3, "episodes : une canicule d'au moins 3 jours consecutifs");
    }

    store.close();
    QDir().remove(db);

    std::printf("\n%s (%d echec%s)\n",
                failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ECHOUENT",
                failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
