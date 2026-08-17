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
class QUdpSocket;

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
//
// Découverte automatique (comme morfMonitor apprend les machines)
// ---------------------------------------------------------------
// En plus des sources DÉCLARÉES en configuration, le module ÉCOUTE le beacon du
// parc et découvre seul les morfMonitor (capacité « system_monitor »). Une
// machine qui s'annonce est intégrée sans aucune déclaration manuelle : son
// /api/all est ajouté aux sources interrogées. Ses données restent conservées
// tant que l'utilisateur ne l'oublie pas explicitement (forgetMachine), même si
// elle se déconnecte. La découverte se fait par CAPACITÉ, jamais par nom.
// -----------------------------------------------------------------------------
class MonitorModule : public IModule {
    Q_OBJECT
public:
    MonitorModule(const QString& id, QStringList sources, int intervalMs,
                  QString dbPath, int retentionDays, quint16 discoveryUdpPort = 45454,
                  bool discoveryEnabled = true, QObject* parent = nullptr);
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

    // Oubli DÉFINITIF d'une machine (« Oublier cette machine ») : efface la machine
    // et tout son historique. Geste explicite, réservé à une machine réellement
    // partie. Retire aussi sa source découverte pour ne pas la réintégrer aussitôt.
    // Renvoie false si la machine était inconnue.
    bool forgetMachine(const QString& key);

    // Ingestion d'une activité signalée par un composant métier (morfDeploy pour
    // les compilations, morfPhoto pour les indexations…). Renvoie l'id, ou -1.
    qint64 ingestActivity(const QJsonObject& a);

private slots:
    void poll();
    void onBeaconDatagram();

private:
    void fetch(const QString& baseUrl);
    void onReply(const QString& baseUrl, QNetworkReply* reply);
    void ingest(const QString& baseUrl, const QJsonObject& all);

    // Sources effectivement interrogées : union des sources déclarées et des
    // morfMonitor découverts par beacon (entendus récemment).
    QStringList activeSources() const;
    // Oublie les sources découvertes muettes depuis longtemps : polling propre,
    // sans s'acharner sur une URL morte. La donnée historique, elle, reste en base.
    void pruneDiscovered(qint64 nowSec);

    QStringList m_sources;         // sources déclarées en configuration (filet stable)
    int         m_intervalMs;
    QString     m_dbPath;
    int         m_retentionDays;   // 0 => conserver indéfiniment
    qint64      m_lastPurgeAt = 0; // throttle de la purge de rétention

    std::unique_ptr<MonitorStore> m_store;
    QTimer*                m_timer = nullptr;
    QNetworkAccessManager* m_net   = nullptr;

    // Découverte beacon.
    quint16     m_discoveryPort;
    bool        m_discoveryEnabled;
    QUdpSocket* m_beacon = nullptr;
    // URL de base d'un morfMonitor découvert -> dernier heartbeat entendu (s Unix).
    QHash<QString, qint64> m_discovered;
    // Au-delà, une source découverte muette est retirée du polling (sa donnée reste).
    static constexpr qint64 kDiscoveryForgetAfterS = 86400;   // 24 h

    QString m_lastError;
    qint64  m_lastPollAt = 0;
    QHash<QString, QJsonObject> m_sourceState;   // baseUrl -> { reachable, last_error, machine }
};

} // namespace morfanalytics
