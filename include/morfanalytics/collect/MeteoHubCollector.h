/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QQueue>
#include <QPair>

class QNetworkAccessManager;
class QNetworkReply;

namespace morfanalytics {

class SampleStore;

// -----------------------------------------------------------------------------
// MeteoHubCollector : recopie INCREMENTALE de l'historique de MeteoHub vers le
// cache local du Raspberry Pi.
//
// Sens unique, toujours : MeteoHub ecrit, morfAnalytics lit. Le collecteur
// n'emet que des GET ; il ne peut par construction rien modifier sur l'ESP32,
// qui reste la source de verite.
//
// Deroulement d'un cycle :
//   1. GET /api/history/days      -> pour chaque jour, combien de mesures existent
//   2. comparaison avec le cache  -> ce qui manque, jour par jour
//   3. GET /api/history/raw?day=&index=  -> uniquement les mesures manquantes
//
// Rien n'est jamais retelecharge : sur un cycle ou l'ESP32 n'a rien de neuf, le
// collecteur ne fait qu'une seule requete (l'etape 1) et s'arrete.
//
// Les requetes sont SEQUENTIELLES, une seule en vol a la fois : l'ESP32 sert son
// interface web en parallele, le saturer de requetes concurrentes degraderait
// l'appareil qu'on cherche justement a soulager.
// -----------------------------------------------------------------------------
class MeteoHubCollector : public QObject {
    Q_OBJECT
public:
    MeteoHubCollector(QString baseUrl, SampleStore* store, QObject* parent = nullptr);

    // Lance un cycle de collecte. Sans effet si un cycle est deja en cours :
    // un cycle long ne doit pas etre relance par le timer de maintenance.
    void sync();

    bool isRunning() const { return m_running; }
    QJsonObject statusJson() const;

signals:
    // Emis en fin de cycle. `imported` = nombre de mesures reellement ajoutees.
    void finished(int imported);

private:
    void requestDays();
    void onDaysReply(QNetworkReply* reply);
    void requestNextChunk();
    void onChunkReply(QNetworkReply* reply);
    void finish(const QString& error);

    QString                m_baseUrl;
    SampleStore*           m_store;
    QNetworkAccessManager* m_net;

    bool    m_running = false;
    int     m_imported = 0;
    QString m_lastError;
    qint64  m_lastSyncTs = 0;
    int     m_lastImported = 0;

    // File des morceaux restant a telecharger : (jour, index de depart).
    QQueue<QPair<quint32, quint32>> m_pending;
    quint32 m_currentDay = 0;
    quint32 m_currentIndex = 0;
};

} // namespace morfanalytics
