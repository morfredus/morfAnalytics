/* Page d'analyse Photo : rendue cote serveur a partir de l'instantane du module. */
#pragma once

#include <QByteArray>
#include <QJsonObject>

namespace morfanalytics::pages {
class PhotoPage { public: static QByteArray render(const QJsonObject& snapshot); };
} // namespace morfanalytics::pages
