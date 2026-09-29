/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Generations de fichiers source dans le cache (0.59.0). Rejoue le cas reel du
 * 2026-09-27 : le hub est remplace en cours de journee, la nouvelle carte SD
 * repart d'un fichier du jour neuf (index 0). Avant 0.59.0, la reprise par
 * position sautait le debut du nouveau fichier et melangeait ses dernieres
 * mesures a l'ancien. On verifie aussi la migration d'un cache a l'ancien
 * schema (cle (day_key, idx), sans generation).
 *
 * Compile via l'option CMake MA_BUILD_TESTS. Retourne 0 si tout passe.
 */

#include <cstdio>

#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVector>
#include <QHash>

#include "morfanalytics/data/SampleStore.h"

using namespace morfanalytics;

static int failures = 0;
static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

// Cache a l'ANCIEN schema, rempli comme l'a laisse le collecteur 0.58.0 le
// 2026-09-27 : 6 mesures de l'ancien hub (idx 0..5), puis 3 mesures du NOUVEAU
// fichier importees a tort aux positions 6..8.
static bool writeLegacyCache(const QString& path, quint32 day, qint64 oldT0, qint64 newT0) {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("legacy"));
    db.setDatabaseName(path);
    if (!db.open())
        return false;
    bool ok = true;
    {
        QSqlQuery q(db);
        ok &= q.exec(QStringLiteral("CREATE TABLE sample (day_key INTEGER NOT NULL,"
                                    "idx INTEGER NOT NULL, ts INTEGER NOT NULL,"
                                    "ch_temp REAL, ch_hum REAL, ch_pres REAL,"
                                    "PRIMARY KEY (day_key, idx))"));
        for (int i = 0; i < 9; ++i) {
            const qint64 ts = (i < 6) ? oldT0 + i * 300 : newT0 + (i + 1) * 300 + 1500;
            q.prepare(QStringLiteral("INSERT INTO sample VALUES (?, ?, ?, 20.0, 50.0, 1010.0)"));
            q.addBindValue(day);
            q.addBindValue(i);
            q.addBindValue(ts);
            ok &= q.exec();
        }
        // Jour dont l'index 0 manque (lot interrompu) : identifiant inconnu.
        q.prepare(QStringLiteral("INSERT INTO sample VALUES (?, 3, ?, 20.0, 50.0, 1010.0)"));
        q.addBindValue(day - 1);
        q.addBindValue(oldT0 - 86400);
        ok &= q.exec();
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("legacy"));
    return ok;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString path = QDir(QDir::tempPath()).filePath("morfanalytics_generations_test.sqlite");
    QDir().remove(path);

    const quint32 day = 20260927u;
    const qint64 oldT0 = 1790460135; // 00:02, premiere mesure de l'ancien hub
    const qint64 newT0 = 1790469355; // 02:35, premiere mesure du nouveau fichier

    if (!writeLegacyCache(path, day, oldT0, newT0)) {
        std::printf("FAIL: creation du cache a l'ancien schema\n");
        return 1;
    }

    SampleStore store(path, {"temp", "hum", "pres"});
    if (!store.open()) {
        std::printf("FAIL: ouverture/migration (%s)\n", store.lastError().toUtf8().constData());
        return 1;
    }

    // --- Migration ---------------------------------------------------------------
    check(store.count() == 10, "migration : aucune ligne perdue");
    const DaySource legacy = store.daySource(day);
    check(legacy.known && legacy.gen == 0 && legacy.firstTs == oldT0,
          "migration : generation 0 identifiee par l'index 0");
    check(store.resumeIndex(day, 0) == 9, "migration : reprise a MAX(idx)+1 en generation 0");
    const DaySource noIdx0 = store.daySource(day - 1);
    check(noIdx0.known && noIdx0.firstTs == 0,
          "migration : sans index 0, identifiant inconnu (a adopter, pas de faux fichier neuf)");

    // --- Le hub sert un fichier neuf pour ce jour --------------------------------
    const qint64 gen = store.startNewGeneration(day, newT0);
    check(gen == 1, "fichier neuf : generation 1");
    check(store.count() == 7, "fichier neuf : les 3 lignes importees a tort sont retirees");
    check(store.resumeIndex(day, 1) == 0, "fichier neuf : lecture depuis l'index 0");

    QVector<qint64> ts;
    QVector<QHash<QString, double>> vals;
    for (int i = 0; i < 10; ++i) {
        ts.push_back(newT0 + i * 300);
        vals.push_back({{"temp", 17.0}, {"hum", 80.0}, {"pres", 1009.0}});
    }
    check(store.insertBatch(day, 1, 0, ts, vals), "fichier neuf : import des 10 mesures");
    check(store.resumeIndex(day, 1) == 10, "fichier neuf : reprise a 10");
    check(store.samplesPerDay().value(day) == 16, "revision morfSync = 6 anciennes + 10 nouvelles");

    // Journee continue, sans doublon ni trou entre 02:35 et 03:20.
    const Series s = store.rangeForDayRaw(day);
    bool sorted = true, noDup = true;
    for (int i = 1; i < s.size(); ++i) {
        if (s.timestamps()[i] < s.timestamps()[i - 1]) sorted = false;
        if (s.timestamps()[i] == s.timestamps()[i - 1]) noDup = false;
    }
    check(s.size() == 16 && sorted && noDup, "journee : 16 points tries, aucun doublon");
    check(s.timestamps()[6] == newT0, "journee : le nouveau fichier commence bien a 02:35");

    // Reimport idempotent dans la meme generation.
    check(store.insertBatch(day, 1, 0, ts, vals) && store.count() == 17,
          "reimport de la generation 1 : aucun doublon");

    // Marques de la source (0.64.0) : colonne ajoutee a un cache ancien, marque
    // relue a l'identique, mesures sans marque a 0.
    const qint64 coldTs = newT0 + 10 * 300;
    check(store.insertBatch(day, 1, 10, {coldTs}, {{{"temp", 25.0}, {"hum", 60.0}, {"pres", 1010.0}}},
                            {Series::kSourceColdBoot}),
          "marque : import d'une mesure de demarrage a froid");
    const Series f = store.rangeRaw(coldTs - 300, coldTs);
    check(f.size() == 2 && f.sourceFlags(0) == 0 && f.sourceFlags(1) == Series::kSourceColdBoot,
          "marque : relue a l'identique, voisine sans marque");

    store.close();
    QDir().remove(path);
    std::printf(failures ? "\n%d echec(s)\n" : "\nTout passe.\n", failures);
    return failures ? 1 : 0;
}
