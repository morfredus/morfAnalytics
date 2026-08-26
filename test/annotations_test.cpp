/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Tests de la couche d'observations meteo humaines (AnnotationStore, Phase 1).
 * Verifie le CRUD, la validation, l'ordre de restitution et surtout la
 * PERSISTANCE : une observation saisie doit survivre a un redemarrage (rouverte
 * depuis un store neuf pointant le meme fichier) et rester independante du cache
 * des mesures. C'est de la donnee utilisateur originale : on ne veut pas la
 * perdre.
 *
 * Compile via l'option CMake MA_BUILD_TESTS. Retourne 0 si tout passe.
 */

#include <cstdio>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

#include "morfanalytics/data/AnnotationStore.h"

using namespace morfanalytics;

static int failures = 0;
static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

// Fabrique un objet d'entree valide pour un episode donne.
static QJsonObject makeIn(qint64 start, qint64 end, const QStringList& types,
                          const QString& desc) {
    QJsonArray t;
    for (const QString& s : types) t.append(s);
    return QJsonObject{
        {"start", static_cast<double>(start)},
        {"end", static_cast<double>(end)},
        {"types", t},
        {"description", desc},
    };
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString path = QDir(QDir::tempPath()).filePath("morfanalytics_annotations_test.json");
    QFile::remove(path);

    // Bornes de reference inspirees de l'episode du 26/08 (valeurs libres ici).
    const qint64 start1 = 1756241000;   // premier passage
    const qint64 end1   = 1756245000;
    const qint64 start2 = 1756230000;   // episode plus ancien (start plus petit)
    const qint64 end2   = 1756232000;

    // --- Creation + validation ----------------------------------------------
    {
        AnnotationStore store(path);
        check(store.load(), "load : fichier absent = demarrage vide (pas une erreur)");
        check(store.count() == 0, "store neuf : aucune observation");

        int code = 0; QString err;

        // Debut manquant -> 400.
        QJsonObject bad = makeIn(start1, end1, {"orage"}, "x");
        bad.remove("start");
        store.upsert(bad, &code, &err);
        check(code == 400, "validation : debut manquant rejete (400)");

        // Fin avant debut -> 400.
        store.upsert(makeIn(end1, start1, {"orage"}, "x"), &code, &err);
        check(code == 400, "validation : fin avant debut rejetee (400)");

        // Aucun type -> 400.
        store.upsert(makeIn(start1, end1, {}, "x"), &code, &err);
        check(code == 400, "validation : aucun type rejete (400)");

        // Cas nominal.
        const QJsonObject saved = store.upsert(
            makeIn(start1, end1, {"orage", "pluie", "vent_fort", "grele"},
                   "Premiere degradation puis seconde cellule."), &code, &err);
        check(code == 200, "creation valide acceptee (200)");
        check(!saved.value("id").toString().isEmpty(), "creation : id genere");
        check(!saved.value("created_at").toString().isEmpty(), "creation : created_at pose");
        check(saved.value("types").toArray().size() == 4, "creation : 4 types conserves");
        check(store.count() == 1, "creation : une observation en memoire");
    }

    // --- Persistance : rouverture depuis un store NEUF ----------------------
    QString keptId;
    QString firstCreatedAt;
    {
        AnnotationStore store(path);
        check(store.load(), "reload : fichier present relu sans erreur");
        const QJsonArray all = store.all();
        check(all.size() == 1, "persistance : observation retrouvee apres redemarrage");
        keptId = all.at(0).toObject().value("id").toString();
        firstCreatedAt = all.at(0).toObject().value("created_at").toString();
        check(!keptId.isEmpty(), "persistance : id conserve");
    }

    // --- Mise a jour : created_at preserve, updated_at rafraichi ------------
    {
        AnnotationStore store(path);
        store.load();
        int code = 0; QString err;

        // Id inconnu -> 404 (pas de creation deguisee).
        QJsonObject ghost = makeIn(start1, end1, {"orage"}, "x");
        ghost.insert("id", "id-qui-nexiste-pas");
        store.upsert(ghost, &code, &err);
        check(code == 404, "maj : id inconnu rejete (404)");

        // Mise a jour reelle.
        QJsonObject upd = makeIn(start1, end1, {"orage"}, "Recit corrige.");
        upd.insert("id", keptId);
        const QJsonObject after = store.upsert(upd, &code, &err);
        check(code == 200, "maj : id connu accepte (200)");
        check(after.value("created_at").toString() == firstCreatedAt,
              "maj : created_at d'origine preserve");
        check(after.value("description").toString() == "Recit corrige.",
              "maj : description mise a jour");
        check(store.count() == 1, "maj : toujours une seule observation (pas de doublon)");
    }

    // --- Ordre de restitution : plus recent (start) en tete -----------------
    {
        AnnotationStore store(path);
        store.load();
        int code = 0; QString err;
        store.upsert(makeIn(start2, end2, {"pluie"}, "Episode plus ancien."), &code, &err);
        check(code == 200, "ajout d'un second episode (200)");
        const QJsonArray all = store.all();
        check(all.size() == 2, "deux observations enregistrees");
        check(all.at(0).toObject().value("start").toDouble() == static_cast<double>(start1),
              "ordre : le start le plus recent est en tete");
    }

    // --- Suppression --------------------------------------------------------
    {
        AnnotationStore store(path);
        store.load();
        int code = 0; QString err;
        store.removeById("inconnu", &code, &err);
        check(code == 404, "suppression : id inconnu rejete (404)");

        const QJsonObject r = store.removeById(keptId, &code, &err);
        check(code == 200 && r.value("ok").toBool(), "suppression : id connu supprime (200)");
        check(store.count() == 1, "suppression : une observation restante");
    }

    // --- La suppression est persistee --------------------------------------
    {
        AnnotationStore store(path);
        store.load();
        check(store.count() == 1, "persistance : suppression conservee apres redemarrage");
    }

    QFile::remove(path);

    std::printf(failures == 0 ? "\nTOUS LES TESTS PASSENT\n" : "\n%d ECHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
