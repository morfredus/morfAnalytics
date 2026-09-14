/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/data/ForecastStore.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QDir>
#include <QUuid>

namespace morfanalytics {

ForecastStore::ForecastStore(QString dbPath)
    : m_dbPath(std::move(dbPath)) {
    // Connexion nommee propre a l'instance (Qt interdit de partager une
    // QSqlDatabase entre threads), comme SampleStore.
    m_connectionName = QStringLiteral("morfanalytics-fc-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

ForecastStore::~ForecastStore() {
    close();
}

bool ForecastStore::open() {
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        m_lastError = QStringLiteral("pilote QSQLITE indisponible");
        return false;
    }

    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    // Cle primaire = jour cible : l'UPSERT (INSERT OR REPLACE) garde toujours la
    // DERNIERE prevision archivee pour ce jour (la prevision « day-ahead » converge
    // au fil de la veille cote MeteoHub).
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS forecast ("
                               "target_day INTEGER PRIMARY KEY,"
                               "issued_ts  INTEGER NOT NULL,"
                               "temp_min   REAL,"
                               "temp_max   REAL,"
                               "description TEXT)"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

void ForecastStore::close() {
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

bool ForecastStore::isOpen() const {
    return m_db.isOpen();
}

bool ForecastStore::upsert(const ForecastEntry& e) {
    if (!m_db.isOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO forecast "
        "(target_day, issued_ts, temp_min, temp_max, description) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(static_cast<qlonglong>(e.targetDay));
    q.addBindValue(static_cast<qlonglong>(e.issuedTs));
    q.addBindValue(e.tempMin);
    q.addBindValue(e.tempMax);
    q.addBindValue(e.description);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QVector<ForecastEntry> ForecastStore::range(quint32 dayFrom, quint32 dayTo) const {
    QVector<ForecastEntry> out;
    if (!m_db.isOpen()) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT target_day, issued_ts, temp_min, temp_max, description "
        "FROM forecast WHERE target_day >= ? AND target_day <= ? "
        "ORDER BY target_day ASC"));
    q.addBindValue(static_cast<qlonglong>(dayFrom));
    q.addBindValue(static_cast<qlonglong>(dayTo));
    if (!q.exec()) return out;
    while (q.next()) {
        ForecastEntry e;
        e.targetDay   = static_cast<quint32>(q.value(0).toLongLong());
        e.issuedTs    = q.value(1).toLongLong();
        e.tempMin     = q.value(2).toDouble();
        e.tempMax     = q.value(3).toDouble();
        e.description = q.value(4).toString();
        out.push_back(e);
    }
    return out;
}

qint64 ForecastStore::count() const {
    if (!m_db.isOpen()) return 0;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM forecast")) && q.next())
        return q.value(0).toLongLong();
    return 0;
}

bool ForecastStore::purgeAll() {
    if (!m_db.isOpen()) return false;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM forecast"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

} // namespace morfanalytics
