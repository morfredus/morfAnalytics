/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>

class QNetworkAccessManager;
class QNetworkReply;

namespace morfanalytics {

class ForecastStore;

// -----------------------------------------------------------------------------
// ForecastCollector : recopie les prévisions « day-ahead » archivées par MeteoHub
// (route /api/forecast/history) vers le cache local, pour l'analyse « prévu vs
// observé » (étape 9).
//
// Bien plus simple que MeteoHubCollector : le volume est faible (~1 par jour), une
// seule requête GET suffit, et l'écriture est un UPSERT par jour cible (la
// prévision d'un jour est réécrite au fil de la veille côté MeteoHub). Sens
// unique comme toujours : l'appareil écrit, morfAnalytics lit.
// -----------------------------------------------------------------------------
class ForecastCollector : public QObject {
    Q_OBJECT
public:
    ForecastCollector(QString baseUrl, ForecastStore* store, QObject* parent = nullptr);

    void sync();
    bool isRunning() const { return m_running; }
    QJsonObject statusJson() const;

signals:
    void finished(int imported);

private:
    void onReply(QNetworkReply* reply);
    void finish(const QString& error);

    QString                m_baseUrl;
    ForecastStore*         m_store;
    QNetworkAccessManager* m_net;

    bool    m_running = false;
    int     m_imported = 0;
    QString m_lastError;
    qint64  m_lastSyncTs = 0;
    int     m_lastImported = 0;
};

} // namespace morfanalytics
