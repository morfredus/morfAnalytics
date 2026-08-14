#include "morfanalytics/pages/PortalPage.h"

#include <QDateTime>
#include <QJsonObject>
#include <algorithm>

namespace morfanalytics::pages {
QByteArray PortalPage::render(const QJsonArray& siteWatchReports) {
    QString status = QStringLiteral("aucune donnée SiteWatch reçue"); qint64 newest = 0;
    for (const QJsonValue& value : siteWatchReports) newest = std::max(newest, static_cast<qint64>(value.toObject().value("received_at").toDouble()));
    if (newest > 0) status = QStringLiteral("dernière synthèse SiteWatch : %1").arg(QDateTime::fromSecsSinceEpoch(newest).toString("dd/MM/yyyy HH:mm"));
    return QStringLiteral("<!doctype html><meta charset=utf-8><title>morfAnalytics</title><style>body{font:16px system-ui;margin:3rem;max-width:60rem}li{margin:.8rem 0}.muted{color:#667085}</style><h1>morfAnalytics</h1><p>Analyses avancées disponibles.</p><ul><li><a href='/meteohub'>Analyses MeteoHub</a> — données météo</li><li><a href='/sitewatch'>Analyses SiteWatch</a> — données de journaux Web</li><li><a href='/photo'>Analyses Photo</a> — photothèque (morfPhoto)</li></ul><p class='muted'>%1</p>").arg(status.toHtmlEscaped()).toUtf8();
}
} // namespace morfanalytics::pages
