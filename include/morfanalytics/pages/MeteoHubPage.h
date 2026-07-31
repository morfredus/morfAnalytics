/* Contrat de la page d'analyse MeteoHub. */
#pragma once

#include <QByteArray>

namespace morfanalytics::pages {
class MeteoHubPage { public: static QByteArray render(const QByteArray& content); };
} // namespace morfanalytics::pages
