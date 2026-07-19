/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QByteArray>
#include "morfanalytics/ServiceConfig.h"

class QTcpServer;
class QTcpSocket;

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
//   POST /analyze      -> analyse a la demande (stub AnalyticsModule)
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
    QByteArray buildStatusJson() const;
    void reply(QTcpSocket* sock, int code, const QByteArray& reason, const QByteArray& body,
               const QByteArray& contentType = "application/json; charset=utf-8");
    static QByteArray landingPage();

    ServiceConfig   m_config;
    ModuleRegistry* m_registry;
    QTcpServer*     m_server;
    QElapsedTimer   m_uptime;
};

} // namespace morfanalytics
