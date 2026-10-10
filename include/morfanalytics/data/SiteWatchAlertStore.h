/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

#include "morfanalytics/analysis/SiteWatchInsights.h"

namespace morfanalytics {

// -----------------------------------------------------------------------------
// SiteWatchAlertStore : persistance des alertes SiteWatch, de leur envoi et des reglages.
//
// Sur la base SQLite deja ouverte par le serveur (sitewatch-history.sqlite). Separe de
// HttpServer pour etre testable seul (base en memoire) : la logique « une alerte n'est
// envoyee qu'une fois, sauf echec d'envoi » est le coeur de la fiabilite des alertes.
//
// Cycle d'une alerte (colonne `delivery`) :
//   pending  -> a envoyer           sent    -> morfNotify a accepte (2xx)
//   failed   -> envoi echoue, sera retente (attempts < max)
//   skipped  -> jamais envoyee : etat des lieux initial, niveau sous le seuil, envoi coupe
//   muted    -> regle en sourdine au moment de la detection
// -----------------------------------------------------------------------------
class SiteWatchAlertStore {
public:
    struct Pending { QString key, title, message, level; QStringList targets; };

    explicit SiteWatchAlertStore(QSqlDatabase db) : m_db(std::move(db)) {}

    // Cree les tables et migre les anciennes (colonnes de suivi d'envoi).
    bool ensureSchema();
    QString lastError() const { return m_error; }

    // ---- Reglages (JSON) : portee « global » ou « site:<id> » ----
    QJsonObject settings(const QString& scope) const;
    bool saveSettings(const QString& scope, const QJsonObject& obj);

    // ---- Alertes ----
    // Enregistre les alertes d'un site (une seule fois par cle) et renvoie celles a envoyer
    // maintenant. Premiere evaluation d'un site : etat des lieux silencieux. `minLevel` :
    // niveau minimal envoye (info < warning < error) ; `notifyEnabled` coupe tout envoi.
    QVector<Pending> record(const QString& siteId, const QVector<sitewatch::Alert>& alerts,
                            const QString& minLevel, bool notifyEnabled);
    // Alertes a (re)envoyer : en attente ou echouees, recentes, sous le plafond d'essais.
    QVector<Pending> due(int maxAttempts = 5, int maxAgeDays = 3) const;
    void markDelivery(const QString& key, bool ok, const QString& error);
    QJsonArray history(const QString& siteId, int limit) const;

    // ---- Indicateurs pour /status ----
    int createdSince(qint64 ts) const;
    int failedCount() const;

    // ---- Retention des rapports ----
    void pruneReports(const QString& siteId, int keep);

private:
    QSqlDatabase m_db;
    QString m_error;
};

} // namespace morfanalytics
