#include "morfanalytics/pages/PortalPage.h"
#include "morfanalytics/Version.h"

#include <QDateTime>
#include <QJsonObject>
#include <algorithm>

namespace morfanalytics::pages {
QByteArray PortalPage::render(const QJsonArray& siteWatchReports) {
    QString status = QStringLiteral("aucune donnée SiteWatch reçue"); qint64 newest = 0;
    for (const QJsonValue& value : siteWatchReports) newest = std::max(newest, static_cast<qint64>(value.toObject().value("received_at").toDouble()));
    if (newest > 0) status = QStringLiteral("dernière synthèse SiteWatch : %1").arg(QDateTime::fromSecsSinceEpoch(newest).toString("dd/MM/yyyy HH:mm"));
    // Même habillage que les autres pages : palette commune (<!--theme-head-->),
    // version et bascule clair / sombre (<!--theme-toggle-->) près du titre.
    return QStringLiteral(
        "<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><!--theme-head-->"
        "<title>morfAnalytics</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:2.5rem 1.25rem}"
        ".wrap{max-width:60rem;margin:auto}a{color:var(--accent)}.muted{color:var(--muted)}li{margin:.8rem 0}"
        ".vb{font-size:.8rem;font-weight:600;vertical-align:middle;color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,transparent);"
        "border:1px solid color-mix(in srgb,var(--accent) 30%,transparent);border-radius:999px;padding:.1rem .5rem;margin-left:.4rem}"
        "</style></head><body><div class=\"wrap\">"
        "<h1>morfAnalytics <span class=\"vb\">v%1</span><!--theme-toggle--></h1><p>Analyses avancées disponibles.</p><ul>"
        "<li><a href='/meteohub'>Analyses MeteoHub</a> - données météo</li>"
        "<li><a href='/sitewatch'>Analyses SiteWatch</a> - journaux Web</li>"
        "<li><a href='/github'>Analyses GitHub</a> - vues, clones et téléchargements</li>"
        "<li><a href='/photo'>Analyses Photo</a> - photothèque (morfPhoto)</li>"
        "<li><a href='/monitor'>Analyse des machines</a> - historique du parc (morfMonitor)</li></ul>"
        "<p class='muted'>%2</p></div></body></html>")
        .arg(morfanalytics::version().toHtmlEscaped(), status.toHtmlEscaped()).toUtf8();
}
} // namespace morfanalytics::pages
