/* Page d'accueil morfAnalytics. */
#pragma once

#include <QByteArray>
#include <QJsonArray>

namespace morfanalytics::pages {
class PortalPage { public: static QByteArray render(const QJsonArray& siteWatchReports); };
} // namespace morfanalytics::pages
