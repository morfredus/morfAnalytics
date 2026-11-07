/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Lecture d'une ligne /api/history/raw : null (hub >= 1.62.0) et 0.0 (anciens hubs)
 * doivent devenir manquants, jamais une mesure a 0. Retourne 0 si tout passe.
 */

#include <cmath>
#include <cstdio>

#include <QJsonArray>
#include <QJsonDocument>

#include "morfanalytics/collect/MeteoHubCollector.h"

using namespace morfanalytics;

static int failures = 0;
static void check(bool cond, const char* msg) {
    std::printf(cond ? "ok  : %s\n" : "FAIL: %s\n", msg);
    if (!cond) ++failures;
}

static QHash<QString, double> parse(const char* json) {
    return MeteoHubCollector::channelsFromRow(QJsonDocument::fromJson(json).array());
}

int main() {
    auto n = parse("[1700000000, 12.5, 55.0, 1013.2]");
    check(n["temp"] == 12.5 && n["hum"] == 55.0 && n["pres"] == 1013.2, "ligne normale");

    auto nh = parse("[1700000000, 12.5, null, 1013.2]");
    check(std::isnan(nh["hum"]) && nh["temp"] == 12.5 && nh["pres"] == 1013.2,
          "humidite null : manquante, reste de la ligne conservee");

    auto np = parse("[1700000000, 12.5, 55.0, null]");
    check(std::isnan(np["pres"]) && std::isnan(np["hum"]) && std::isnan(np["temp"]),
          "pression null : ligne entiere manquante");

    auto old = parse("[1700000000, 0.0, 0.0, 0.0]");
    check(std::isnan(old["pres"]) && std::isnan(old["hum"]) && std::isnan(old["temp"]),
          "ancien hub a 0.0 : ligne entiere manquante");

    auto oh = parse("[1700000000, 0.0, 0.0, 1005.0]");
    check(std::isnan(oh["hum"]) && oh["temp"] == 0.0, "humidite 0 % ecartee, 0 degre conserve");
    return failures;
}
