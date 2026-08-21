/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfanalytics/IModule.h"

#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <memory>

class QTimer;
class QNetworkAccessManager;
class QUdpSocket;

namespace morfanalytics {

class GitHubStore;

// Domaine GitHub : recoit la verite consolidee de SiteWatch (POST /github/ingest).
// Ne lit ni GitHub ni morfCollector.
class GitHubAnalyticsModule : public IModule {
    Q_OBJECT
public:
    GitHubAnalyticsModule(const QString& id, QStringList collectors, QString dbPath,
                          int intervalMs, quint16 discoveryUdpPort, bool discoveryEnabled,
                          QObject* parent = nullptr);
    ~GitHubAnalyticsModule() override;

    bool start() override;
    void stop() override;
    QJsonObject statusJson() const override;

    QJsonObject overview(const QString& repo = {}, const QString& fromDay = {},
                         const QString& toDay = {}) const;
    QJsonObject repository(const QString& fullName) const;
    bool ingestAuthority(const QJsonObject& payload);

private slots:
    void poll();
    void onBeaconDatagram();

private:
    QStringList activeCollectors() const;
    void importFrom(const QString& baseUrl);
    static QByteArray httpGet(const QString& url, int timeoutMs, int& status);

    QStringList m_collectors;
    QString     m_dbPath;
    int         m_intervalMs = 300000;
    quint16     m_discoveryPort = 45454;
    bool        m_discoveryEnabled = true;
    QString     m_lastError;
    int         m_imported = 0;
    QHash<QString, qint64> m_discovered;
    std::unique_ptr<GitHubStore> m_store;
    QTimer* m_timer = nullptr;
    QUdpSocket* m_beacon = nullptr;
};

} // namespace morfanalytics
