/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QByteArray>

namespace morfanalytics::pages {

// Page /monitor : historique des machines du parc (vue d'ensemble + séries
// temporelles CPU / RAM / température). Page autonome (ni CDN ni fichier externe)
// qui récupère ses données via /monitor/data et dessine tout côté navigateur.
class MonitorPage {
public:
    static QByteArray render();
};

} // namespace morfanalytics::pages
