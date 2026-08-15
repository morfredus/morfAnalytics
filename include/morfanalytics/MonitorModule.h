/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfanalytics/IModule.h"

#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>

#include <memory>

class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

namespace morfanalytics {

class MonitorStore;

// -----------------------------------------------------------------------------
// MonitorModule : le domaine Monitor de morfAnalytics.
//
// Il interroge périodiquement un ou plusieurs morfMonitor (leur `/api/all`),
// extrait les métriques machine et par service, et les HISTORISE dans un
// MonitorStore local. morfMonitor dit « maintenant » ; ce module donne à
// morfSystem la mémoire de son fonctionnement dans le temps.
//
// Frontière stricte : morfMonitor reste la sonde brute, ce module ne fait
// qu'échantillonner, stocker et représenter — aucune sonde système ici.
// -----------------------------------------------------------------------------
class MonitorModule : public IModule {
    Q_OBJECT
public:
    MonitorModule(const QString& id, QStringList sources, int intervalMs,
                  QString dbPath, int retentionDays, QObject* parent = nullptr);
    ~MonitorModule() override;

    bool start() override;
    void stop() override;
    QJsonObject statusJson() const override;

    // --- Façade de lecture pour la page /monitor ----------------------------
    // Appelée depuis le serveur HTTP, sur le MÊME thread que la collecte (boucle
    // d'événements Qt mono-thread) : accès direct au store, sans verrou.
    QJsonArray  machines() const;
    QJsonObject data(const QString& machineKey, qint64 fromTs, qint64 toTs,
                     int maxPoints) const;

    // Ingestion d'une activité signalée par un composant métier (morfDeploy pour
    // les compilations, morfPhoto pour les indexations…). Renvoie l'id, ou -1.
    qint64 ingestActivity(const QJsonObject& a);

private slots:
    void poll();

private:
    void fetch(const QString& baseUrl);
    void onReply(const QString& baseUrl, QNetworkReply* reply);
    void ingest(const QString& baseUrl, const QJsonObject& all);

    QStringList m_sources;
    int         m_intervalMs;
    QString     m_dbPath;
    int         m_retentionDays;   // 0 => conserver indéfiniment
    qint64      m_lastPurgeAt = 0; // throttle de la purge de rétention

    std::unique_ptr<MonitorStore> m_store;
    QTimer*                m_timer = nullptr;
    QNetworkAccessManager* m_net   = nullptr;

    QString m_lastError;
    qint64  m_lastPollAt = 0;
    QHash<QString, QJsonObject> m_sourceState;   // baseUrl -> { reachable, last_error, machine }
};

} // namespace morfanalytics
