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
#include <QtGlobal>

namespace morfanalytics {

struct GithubAssetClass {
    QString platform;      // windows | linux | firmware | other
    QString architecture;  // x86_64 | amd64 | arm64 | unknown | none
    QString canonical;
};

// Classe un nom d'asset de release (win64, linux-amd64, firmware, etc.).
GithubAssetClass classifyGithubAsset(const QString& filename);

class GitHubStore {
public:
    explicit GitHubStore(QString dbPath);
    ~GitHubStore();

    bool open();
    void close();
    bool isOpen() const;
    QString lastError() const { return m_lastError; }

    bool hasObject(const QString& objectId) const;
    // Import idempotent : un object_id deja vu ne produit aucun doublon.
    bool importSnapshot(const QString& objectId, const QJsonObject& snap);
    // Verite SiteWatch : ecrase la copie d'analyse (jamais GitHub ni le collecteur).
    bool ingestAuthority(const QJsonObject& payload);

    QJsonObject overview(const QString& repo = {}, const QString& fromDay = {},
                         const QString& toDay = {}) const;
    QJsonObject repository(const QString& fullName) const;

private:
    bool exec(const QString& sql);
    qint64 previousDownloadCount(const QString& fullName, qint64 assetId) const;

    QString      m_dbPath;
    QString      m_connectionName;
    QString      m_lastError;
    QSqlDatabase m_db;
};

} // namespace morfanalytics
