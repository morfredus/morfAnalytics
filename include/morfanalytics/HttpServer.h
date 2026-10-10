/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <memory>
#include "morfanalytics/analysis/SiteWatchInsights.h"
#include "morfanalytics/data/SiteWatchAlertStore.h"
#include "morfanalytics/ServiceConfig.h"

class QTcpServer;
class QTcpSocket;
class QTimer;
class QNetworkAccessManager;

namespace morfanalytics {

class ModuleRegistry;

// -----------------------------------------------------------------------------
// HttpServer : serveur HTTP/1.1 minimal, gerant GET *et* POST (avec corps).
//
// Routes fournies (a ADAPTER selon votre metier) :
//   GET  /             -> page d'accueil HTML (etat du service et de la collecte)
//   GET  /status        -> compatible morfBeacon (app, version, uptime, metrics)
//   GET  /healthz       -> { "status": "ok" }
//   GET  /modules       -> etat de tous les modules
//   GET  /modules/{id}  -> etat d'un module
//   POST /analyze       -> analyse a la demande (AnalyticsModule)
//   POST /data/cleanup  -> nettoyage du cache local (jamais de la source)
// -----------------------------------------------------------------------------
class HttpServer : public QObject {
    Q_OBJECT
public:
    HttpServer(ServiceConfig config, ModuleRegistry* registry, QObject* parent = nullptr);
    ~HttpServer() override;

    bool start();
    void stop();
    bool isListening() const;
    quint16 port() const;

private:
    void onNewConnection();
    void onSocketReadyRead(QTcpSocket* sock);
    void handleRequest(QTcpSocket* sock, const QByteArray& method,
                       const QByteArray& path, const QByteArray& body);
    QByteArray handleAnalyzePost(const QByteArray& body, int& code, QByteArray& reason) const;
    QByteArray handleCleanupPost(const QByteArray& body, int& code, QByteArray& reason) const;
    QByteArray handleSiteWatchPost(const QByteArray& body, int& code, QByteArray& reason);
    QByteArray handleGitHubIngest(const QByteArray& body, int& code, QByteArray& reason);
    bool openSiteWatchStore();
    void loadSiteWatchReports();
    bool saveSiteWatchReport(const QJsonObject& report);
    QJsonArray siteWatchReports() const;
    QJsonArray siteWatchHistory(const QString& siteId, int limit = 90) const;
    void closeSiteWatchStore();
    QByteArray buildStatusJson() const;
    void reply(QTcpSocket* sock, int code, const QByteArray& reason, const QByteArray& body,
               const QByteArray& contentType = "application/json; charset=utf-8");
    static QByteArray landingPage();
    // Analyse approfondie SiteWatch : lecture, configuration, alertes (envoi suivi vers morfNotify).
    QByteArray siteWatchInsightsJson(const QByteArray& rawPath) const;
    QByteArray siteWatchOverviewJson(const QByteArray& rawPath) const;
    QByteArray siteWatchExport(const QByteArray& rawPath, QByteArray* contentType) const;
    QByteArray siteWatchAlertsJson(const QByteArray& rawPath) const;
    QByteArray siteWatchConfigJson() const;
    QByteArray handleSiteWatchConfigPost(const QByteArray& body, int& code, QByteArray& reason);
    QByteArray handleSiteWatchMutePost(const QByteArray& body, int& code, QByteArray& reason);
    QByteArray handleSiteWatchTestPost(const QByteArray& body);
    QByteArray siteWatchTargetsJson();
    sitewatch::AlertConfig alertConfigFor(const QString& siteId) const;
    QJsonObject notifySettings() const;
    QString notifyUrl() const;
    QString notifyUrlDisplay(bool* local = nullptr) const;   // adresse lisible (nom de la machine au lieu de 127.0.0.1)
    QJsonObject notifyMetrics();                              // compteurs de morfNotify (GET /status), {} si injoignable
    QStringList notifyTargets() const;
    int reportsPerSite() const;
    void evaluateSiteWatchAlerts(const QJsonObject& report);
    void evaluateAllSiteWatchAlerts();
    void sendNotification(const SiteWatchAlertStore::Pending& p);

    ServiceConfig   m_config;
    ModuleRegistry* m_registry;
    QTcpServer*     m_server;
    QElapsedTimer   m_uptime;
    QHash<QString, QJsonObject> m_siteWatchReports;
    QSqlDatabase    m_siteWatchDb;
    QString         m_siteWatchConnectionName;
    QString         m_siteWatchStoreError;
    QTimer*         m_alertTimer = nullptr;
    QNetworkAccessManager* m_notifyNet = nullptr;
    std::unique_ptr<SiteWatchAlertStore> m_alerts;
    QSet<QString> m_notifyInflight;   // alertes dont l'envoi est en cours (pas de doublon)
};

} // namespace morfanalytics
