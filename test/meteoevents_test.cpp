/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Test de MeteoEvents : la source commune des evenements temporels (croisements
 * IN/OUT, changements de tendance, changements de regime). Comme MeteoMath, ce
 * sont des fonctions pures : on les verifie sur des scenarios construits a la
 * main dont on connait la reponse attendue.
 *
 * Compile via l'option CMake MA_BUILD_TESTS. Retourne 0 si tout passe.
 */

#include <cstdio>
#include <cmath>
#include <QVector>

#include "morfanalytics/analysis/MeteoEvents.h"

using namespace morfanalytics::meteo;

static int failures = 0;

static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

// Fabrique une serie a cadence reguliere (5 min) a partir de valeurs.
static void mk(const QVector<double>& vals, QVector<qint64>& ts, QVector<double>& v) {
    ts.clear(); v.clear();
    const qint64 t0 = 1000, step = 300;
    for (int i = 0; i < vals.size(); ++i) { ts.push_back(t0 + i * step); v.push_back(vals[i]); }
}

int main() {
    const double NaN = std::nan("");

    // --- Croisements ---------------------------------------------------------
    {
        // OUT monte de 19 a 23, IN constant 21 : un seul croisement a ~21.
        QVector<qint64> to, ti; QVector<double> vo, vi;
        mk({19, 19.5, 20, 20.5, 21, 21.5, 22, 22.5, 23}, to, vo);
        mk({21, 21, 21, 21, 21, 21, 21, 21, 21}, ti, vi);
        const auto cr = detectCrossings(QStringLiteral("temp"), to, vo, ti, vi);
        check(cr.size() == 1, "un croisement simple (montee OUT a travers IN)");
        if (cr.size() == 1) {
            check(std::fabs(cr[0].value - 21.0) < 0.1, "valeur du croisement ~21");
            check(cr[0].outRising, "sens : l'exterieur passe au-dessus de l'interieur");
        }
    }
    {
        // Bruit qui frole l'egalite et repart du meme cote : aucun croisement.
        QVector<qint64> to, ti; QVector<double> vo, vi;
        mk({22, 21.1, 20.95, 21.1, 22, 22.5, 23, 23, 23}, to, vo);
        mk({21, 21, 21, 21, 21, 21, 21, 21, 21}, ti, vi);
        const auto cr = detectCrossings(QStringLiteral("temp"), to, vo, ti, vi);
        check(cr.isEmpty(), "bande morte : une oscillation sous eps ne cree pas d'evenement");
    }
    {
        // Descend sous puis remonte au-dessus : deux croisements.
        QVector<qint64> to, ti; QVector<double> vo, vi;
        mk({23, 22, 21, 20, 19, 20, 21, 22, 23}, to, vo);
        mk({21, 21, 21, 21, 21, 21, 21, 21, 21}, ti, vi);
        const auto cr = detectCrossings(QStringLiteral("temp"), to, vo, ti, vi);
        check(cr.size() == 2, "deux croisements (descente puis remontee)");
        if (cr.size() == 2)
            check(!cr[0].outRising && cr[1].outRising, "sens opposes des deux croisements");
    }
    {
        // Trou de mesure OUT au milieu (saut > gapMax) : le croisement s'y produit
        // mais ne doit pas etre invente.
        QVector<qint64> to, ti; QVector<double> vo, vi;
        to = {1000, 1300, 4000, 4300}; vo = {22, 22.5, 20, 19.5};
        mk({21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21}, ti, vi);
        const auto cr = detectCrossings(QStringLiteral("temp"), to, vo, ti, vi);
        check(cr.isEmpty(), "aucun croisement invente au travers d'un vrai trou");
    }
    {
        // Une mesure manquante (NaN) ne casse pas la detection sur le reste.
        QVector<qint64> to, ti; QVector<double> vo, vi;
        mk({19, 19.5, NaN, 20.5, 21, 21.5, 22, 22.5, 23}, to, vo);
        mk({21, 21, 21, 21, 21, 21, 21, 21, 21}, ti, vi);
        const auto cr = detectCrossings(QStringLiteral("temp"), to, vo, ti, vi);
        check(cr.size() == 1, "un NaN isole n'empeche pas le croisement");
    }

    // --- Changements de tendance --------------------------------------------
    {
        // Baisse pendant ~5 h puis hausse pendant ~5 h : un changement baisse->hausse.
        QVector<qint64> ts; QVector<double> v;
        QVector<double> vals;
        for (int h = 0; h < 5; ++h) for (int k = 0; k < 12; ++k) vals.push_back(20.0 - h * 1.0);
        for (int h = 0; h < 5; ++h) for (int k = 0; k < 12; ++k) vals.push_back(15.0 + h * 1.0);
        mk(vals, ts, v);
        const auto tc = detectTrendChanges(QStringLiteral("temp"), ts, v);
        check(!tc.isEmpty(), "un changement de tendance detecte sur un V thermique");
        if (!tc.isEmpty())
            check(tc.last().from == Trend::Down && tc.last().to == Trend::Up,
                  "tendance baisse -> hausse");
    }
    {
        // Serie quasi plate bruitee : pas de changement de tendance.
        QVector<qint64> ts; QVector<double> v;
        QVector<double> vals;
        for (int i = 0; i < 120; ++i) vals.push_back(20.0 + ((i % 2) ? 0.05 : -0.05));
        mk(vals, ts, v);
        const auto tc = detectTrendChanges(QStringLiteral("temp"), ts, v);
        check(tc.isEmpty(), "une serie plate bruitee ne genere pas de changement");
    }

    // --- Changements de regime ----------------------------------------------
    {
        // Deux grandeurs basculent a ~10 min d'ecart -> un regime ; une 3e loin -> ignoree.
        QVector<TrendChange> ch;
        ch.push_back({QStringLiteral("temp"), 100000, Trend::Down, Trend::Up});
        ch.push_back({QStringLiteral("hum"),  100000 + 600, Trend::Up, Trend::Down});
        ch.push_back({QStringLiteral("pres"), 100000 + 10000, Trend::Flat, Trend::Up});
        const auto rc = detectRegimeChanges(ch);
        check(rc.size() == 1, "un seul changement de regime (2 grandeurs rapprochees)");
        if (rc.size() == 1)
            check(rc[0].parts.size() == 2, "le regime regroupe les deux grandeurs proches");
    }
    {
        // Un seul changement isole n'est pas un regime.
        QVector<TrendChange> ch;
        ch.push_back({QStringLiteral("temp"), 100000, Trend::Down, Trend::Up});
        const auto rc = detectRegimeChanges(ch);
        check(rc.isEmpty(), "un changement de tendance isole n'est pas un regime");
    }

    std::printf("\n%s (%d echec%s)\n",
                failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ECHOUENT",
                failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
