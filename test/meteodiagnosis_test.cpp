/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Test de MeteoDiagnosis sur des scenarios construits a la main : on verifie
 * surtout la distinction « vraie humidification » / « simple refroidissement ».
 */

#include <cstdio>
#include <cmath>
#include <QVector>

#include "morfanalytics/analysis/MeteoDiagnosis.h"

using namespace morfanalytics::meteo;

static int failures = 0;
static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

// 13 h de mesures toutes les 10 min, se terminant a `now`. `f(h)` donne (T, HR, P)
// pour h = heures ecoulees depuis le debut (0..13).
template <class F>
static void mk(F f, QVector<qint64>& ts, QVector<double>& t, QVector<double>& h,
               QVector<double>& p, qint64 now) {
    ts.clear(); t.clear(); h.clear(); p.clear();
    for (int i = 0; i <= 78; ++i) {
        const double hr = i / 6.0;
        double T, H, P;
        f(hr, T, H, P);
        ts.push_back(now - 13 * 3600 + i * 600);
        t.push_back(T); h.push_back(H); p.push_back(P);
    }
}

int main() {
    const qint64 now = 1000000;
    QVector<qint64> ts; QVector<double> t, h, p;

    // Humidification : T stable, HR 60 -> 90 et pression -4 hPa sur les 6 dernieres heures.
    mk([](double x, double& T, double& H, double& P) {
        const double k = x < 7 ? 0 : (x - 7) / 6.0;
        T = 18; H = 60 + 30 * k; P = 1015 - 4 * k;
    }, ts, t, h, p, now);
    Diagnosis a = diagnose(ts, t, h, p, now, 15);
    check(a.valid, "humidification : valide");
    check(a.primary == "humidification", "humidification : situation principale");
    check(a.total == 5 && a.agree == 5, "humidification : 5 signaux sur 5");
    check(a.precipitation == "moderate" || a.precipitation == "high", "humidification : precipitations favorables");
    check(!a.why.isEmpty(), "humidification : explication fournie");

    // Refroidissement nocturne : Td figee a 10 C, T 20 -> 14, donc HR monte seule.
    mk([](double x, double& T, double& H, double& P) {
        const double k = x < 7 ? 0 : (x - 7) / 6.0;
        T = 20 - 6 * k;
        const double Td = 10;
        H = 100 * std::exp(17.62 * Td / (243.12 + Td) - 17.62 * T / (243.12 + T));
        P = 1015;
    }, ts, t, h, p, now);
    Diagnosis b = diagnose(ts, t, h, p, now, 23);
    check(b.primary == "refroidissement", "refroidissement : pas une humidification");
    check(!b.situations.contains("humidification"), "refroidissement : humidification exclue");
    check(b.precipitation == "none" || b.precipitation == "low", "refroidissement : pas de precipitations");

    // Donnees manquantes : pas de crash, diagnostic invalide.
    Diagnosis c = diagnose({}, {}, {}, {}, now, 12);
    check(!c.valid, "vide : invalide");

    // Gel : T 4 -> 0.5, air quasi sature.
    mk([](double x, double& T, double& H, double& P) {
        const double k = x < 7 ? 0 : (x - 7) / 6.0;
        T = 4 - 3.5 * k; H = 92; P = 1020;
    }, ts, t, h, p, now);
    Diagnosis g = diagnose(ts, t, h, p, now, 5);
    check(g.frost == "high", "gel : niveau fort");

    // Habitude du lieu : 30 jours avec un cycle quotidien (HR monte chaque soir).
    // Le meme soir ne doit rien declencher ; une humidification en plus, si.
    {
        const double pi = 3.14159265358979;
        const qint64 now2 = 30 * 86400 + 19 * 3600;
        auto build = [&](double extraRamp, QVector<qint64>& ts2, QVector<double>& t2,
                         QVector<double>& h2, QVector<double>& p2) {
            ts2.clear(); t2.clear(); h2.clear(); p2.clear();
            for (qint64 x = 0; x <= now2; x += 600) {
                const double hr = double(x % 86400) / 3600.0;
                const double ph = 2 * pi * (hr - 9) / 24;
                const double wob = 0.3 * std::sin(double(x / 600) * 1.7);
                double T = 15 + 6 * std::sin(ph) + wob;
                double H = 70 - 20 * std::sin(ph) + wob;
                const double ago = double(now2 - x) / 3600.0; // heures avant « maintenant »
                if (ago < 6) H += extraRamp * (6 - ago) / 6.0;
                ts2.push_back(x); t2.push_back(T); h2.push_back(std::min(H, 99.0)); p2.push_back(1015);
            }
        };
        QVector<qint64> t3; QVector<double> a3, b3, c3;
        build(0, t3, a3, b3, c3);
        Baseline bl = buildBaseline(t3, a3, b3, c3, now2, 0);
        check(bl.ok, "habitude : baseline suffisante");
        Diagnosis n = diagnose(t3, a3, b3, c3, now2, 19, &bl);
        check(n.adaptive, "habitude : seuils adaptatifs");
        check(!n.situations.contains("humidification") && n.unusualCount == 0,
              "habitude : un soir ordinaire ne declenche rien");
        build(25, t3, a3, b3, c3);
        Diagnosis u = diagnose(t3, a3, b3, c3, now2, 19, &bl);
        check(u.signalsList.size() == 5 && u.signalsList[2].unusual,
              "habitude : une hausse d'HR hors norme est inhabituelle");
    }

    return failures ? 1 : 0;
}
