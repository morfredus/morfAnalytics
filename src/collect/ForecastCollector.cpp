/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/collect/ForecastCollector.h"
#include "morfanalytics/data/ForecastStore.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUrl>

namespace morfanalytics {

namespace {
// L'appareil sert ce flux depuis la carte SD tout en tenant son interface web :
// on laisse large, comme le collecteur de mesures.
constexpr int kTimeoutMs = 45000;
} // namespace

ForecastCollector::ForecastCollector(QString baseUrl, ForecastStore* store, QObject* parent)
    : QObject(parent), m_baseUrl(std::move(baseUrl)), m_store(store),
      m_net(new QNetworkAccessManager(this)) {
    while (m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl.chop(1);
}

void ForecastCollector::sync() {
    if (m_running)
        return;
    if (!m_store || !m_store->isOpen()) {
        m_lastError = QStringLiteral("cache prévisions indisponible");
        return;
    }
    m_running  = true;
    m_imported = 0;
    m_lastError.clear();

    // Sans bornes : le volume est faible (une prévision par jour), on récupère
    // tout et l'UPSERT garde la dernière version de chaque jour.
    QUrl url(m_baseUrl + QStringLiteral("/api/forecast/history"));
    QNetworkRequest req{url};
    req.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onReply(reply); });
}

void ForecastCollector::onReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        // MeteoHub éteint / hors de portée : pas une anomalie, on reprendra.
        finish(reply->errorString());
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonArray data  = root.value(QStringLiteral("data")).toArray();

    for (const QJsonValue& v : data) {
        const QJsonObject o = v.toObject();
        ForecastEntry e;
        e.targetDay   = static_cast<quint32>(o.value(QStringLiteral("target_day")).toDouble());
        e.issuedTs    = static_cast<qint64>(o.value(QStringLiteral("issued_ts")).toDouble());
        e.tempMin     = o.value(QStringLiteral("temp_min")).toDouble();
        e.tempMax     = o.value(QStringLiteral("temp_max")).toDouble();
        e.description = o.value(QStringLiteral("description")).toString();
        if (e.targetDay == 0) continue; // entrée mal formée : on l'ignore
        if (m_store->upsert(e))
            m_imported++;
    }

    finish(QString());
}

void ForecastCollector::finish(const QString& error) {
    m_lastError    = error;
    m_running      = false;
    m_lastSyncTs   = QDateTime::currentSecsSinceEpoch();
    m_lastImported = m_imported;
    emit finished(m_imported);
}

QJsonObject ForecastCollector::statusJson() const {
    QJsonObject o;
    o["source"]        = m_baseUrl;
    o["running"]       = m_running;
    o["last_sync_ts"]  = static_cast<double>(m_lastSyncTs);
    o["last_imported"] = m_lastImported;
    o["ok"]            = m_lastError.isEmpty();
    if (!m_lastError.isEmpty())
        o["error"] = m_lastError;
    if (m_store && m_store->isOpen())
        o["cached_days"] = static_cast<double>(m_store->count());
    return o;
}

} // namespace morfanalytics
