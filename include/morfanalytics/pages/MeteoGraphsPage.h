/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QByteArray>

namespace morfanalytics::pages {

// Page /meteohub/graphs : onglet « Graphiques » du domaine météo. Complément
// visuel des analyses (« montrer ce que font les données ») : évolution d'une
// grandeur dans le temps, source Intérieur / Extérieur / les deux. Page autonome
// (ni CDN ni fichier externe, SVG dessiné côté navigateur), données via
// /meteohub/series.
class MeteoGraphsPage {
public:
    static QByteArray render();
};

} // namespace morfanalytics::pages
