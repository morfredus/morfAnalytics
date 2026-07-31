/* Contrat de la page d'analyse SiteWatch. */
#pragma once

#include <QByteArray>
#include <QJsonArray>

namespace morfanalytics::pages {
class SiteWatchPage { public: static QByteArray render(const QByteArray& content, const QJsonArray& reports); };
} // namespace morfanalytics::pages
