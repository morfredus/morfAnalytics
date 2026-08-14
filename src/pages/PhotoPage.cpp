/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/pages/PhotoPage.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QString>

#include <algorithm>

namespace morfanalytics::pages {

namespace {
QString fr(double v) { return QLocale(QLocale::French, QLocale::France).toString(static_cast<qlonglong>(v)); }
QString esc(const QString& s) { return s.toHtmlEscaped(); }

// Rend une liste {label,count} en barres proportionnelles.
QString bars(const QJsonArray& rows, const QString& labelKey, const QString& countKey) {
    double max = 1;
    for (const QJsonValue& v : rows)
        max = std::max(max, v.toObject().value(countKey).toDouble());
    QString html;
    for (const QJsonValue& v : rows) {
        const QJsonObject o = v.toObject();
        const QString label = o.value(labelKey).isDouble()
            ? QString::number(o.value(labelKey).toDouble())
            : o.value(labelKey).toString();
        const double c = o.value(countKey).toDouble();
        const int pct = static_cast<int>(c * 100.0 / max);
        html += QStringLiteral(
            "<div class=\"row\"><span class=\"lab\">%1</span>"
            "<span class=\"bar\"><i style=\"width:%2%\"></i></span>"
            "<span class=\"val\">%3</span></div>")
            .arg(esc(label)).arg(pct).arg(fr(c));
    }
    return html.isEmpty() ? QStringLiteral("<p class=\"muted\">aucune donnee</p>") : html;
}
} // namespace

QByteArray PhotoPage::render(const QJsonObject& snap) {
    const bool reachable = snap.value(QStringLiteral("reachable")).toBool();
    const QString source = snap.value(QStringLiteral("source_url")).toString();

    QString page = QStringLiteral(R"HTML(<!doctype html><html lang="fr"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="refresh" content="60">
<title>morfAnalytics - Photo</title><style>
body{margin:0;background:#15171b;color:#e7e9ec;font:16px system-ui;padding:2rem}
.wrap{max-width:70rem;margin:auto}h1{margin:.2rem 0}h2{font-size:1.05rem;margin:1.5rem 0 .5rem}
.muted{color:#99a1ad}.card{background:#1e2126;border:1px solid #2c3037;border-radius:12px;padding:1.25rem;margin:1rem 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(11rem,1fr));gap:1rem}
.tile{background:#1e2126;border:1px solid #2c3037;border-radius:12px;padding:1rem}
.number{font-size:2rem;font-weight:700}.row{display:flex;align-items:center;gap:.6rem;margin:.25rem 0}
.lab{width:7rem;text-align:right;color:#c7cdd6}.val{width:5rem;color:#99a1ad}
.bar{flex:1;background:#242830;border-radius:6px;overflow:hidden}.bar i{display:block;height:.9rem;background:#6f9bff}
a{color:#6f9bff}</style><body><div class="wrap">
<p><a href="/">&larr; morfAnalytics</a></p><h1>Analyse de la phototh&egrave;que</h1>
<p class="muted">Donn&eacute;es lues depuis morfPhoto (source de v&eacute;rit&eacute;) &middot; actualisation toutes les 60&nbsp;s.</p>
)HTML");

    if (!reachable) {
        const QString err = snap.value(QStringLiteral("last_error")).toString();
        page += QStringLiteral("<section class=\"card\"><strong>morfPhoto injoignable.</strong>"
                               "<p class=\"muted\">Source : %1<br>%2</p></section>")
                    .arg(source.isEmpty() ? QStringLiteral("(non configur&eacute;e)") : esc(source),
                         esc(err));
        page += QStringLiteral("</div></body></html>");
        return page.toUtf8();
    }

    const QJsonObject summary = snap.value(QStringLiteral("summary")).toObject();
    const auto tile = [](const QString& label, const QString& value) {
        return QStringLiteral("<div class=\"tile\"><div class=\"number\">%1</div>"
                              "<div class=\"muted\">%2</div></div>").arg(value, label);
    };
    page += QStringLiteral("<div class=\"grid\">");
    page += tile(QStringLiteral("Photos pr&eacute;sentes"), fr(summary.value(QStringLiteral("files_present")).toDouble()));
    page += tile(QStringLiteral("Bo&icirc;tiers"), fr(summary.value(QStringLiteral("cameras")).toDouble()));
    page += tile(QStringLiteral("Objectifs"), fr(summary.value(QStringLiteral("lenses")).toDouble()));
    page += tile(QStringLiteral("Dossiers actifs"), fr(summary.value(QStringLiteral("folders_active")).toDouble()));
    if (summary.value(QStringLiteral("files_missing")).toDouble() > 0)
        page += tile(QStringLiteral("Disparues"), fr(summary.value(QStringLiteral("files_missing")).toDouble()));
    page += QStringLiteral("</div>");

    page += QStringLiteral("<h2>Focales usuelles</h2><div class=\"card\">%1"
                           "<p class=\"muted\">Regroupement interpr&eacute;t&eacute; par morfAnalytics ; "
                           "les valeurs brutes restent souveraines dans morfPhoto.</p></div>")
                .arg(bars(snap.value(QStringLiteral("focals_grouped")).toArray(),
                          QStringLiteral("label"), QStringLiteral("count")));

    page += QStringLiteral("<h2>Par ann&eacute;e</h2><div class=\"card\">%1</div>")
                .arg(bars(snap.value(QStringLiteral("years")).toArray(),
                          QStringLiteral("year"), QStringLiteral("count")));

    page += QStringLiteral("<h2>Bo&icirc;tiers</h2><div class=\"card\">%1</div>")
                .arg(bars(snap.value(QStringLiteral("cameras")).toArray(),
                          QStringLiteral("camera"), QStringLiteral("count")));

    page += QStringLiteral("<h2>Objectifs</h2><div class=\"card\">%1</div>")
                .arg(bars(snap.value(QStringLiteral("lenses")).toArray(),
                          QStringLiteral("lens"), QStringLiteral("count")));

    page += QStringLiteral("</div></body></html>");
    return page.toUtf8();
}

} // namespace morfanalytics::pages
