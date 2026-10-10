/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/data/SiteWatchAlertStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace morfanalytics {

namespace {
int levelRank(const QString& level) {
    if (level == QLatin1String("error")) return 2;
    if (level == QLatin1String("warning")) return 1;
    return 0;
}
} // namespace

bool SiteWatchAlertStore::ensureSchema() {
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    const char* ddl[] = {
        "CREATE TABLE IF NOT EXISTS sitewatch_report ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "site_id TEXT NOT NULL, site_label TEXT, received_at INTEGER NOT NULL,"
        "payload BLOB NOT NULL)",
        "CREATE INDEX IF NOT EXISTS idx_sitewatch_report_site_time "
        "ON sitewatch_report(site_id, received_at DESC)",
        "CREATE TABLE IF NOT EXISTS sitewatch_alert ("
        "key TEXT PRIMARY KEY, site_id TEXT NOT NULL, rule TEXT, level TEXT,"
        "day TEXT, title TEXT, message TEXT, created_at INTEGER NOT NULL,"
        "notified INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS sitewatch_alert_baseline ("
        "site_id TEXT PRIMARY KEY, created_at INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS sitewatch_setting ("
        "scope TEXT PRIMARY KEY, json TEXT NOT NULL)",
    };
    for (const char* sql : ddl) {
        if (!q.exec(QLatin1String(sql))) { m_error = q.lastError().text(); return false; }
    }
    // Migration : suivi reel de l'envoi. Les erreurs « colonne deja presente » sont normales.
    q.exec(QStringLiteral("ALTER TABLE sitewatch_alert ADD COLUMN delivery TEXT NOT NULL DEFAULT ''"));
    q.exec(QStringLiteral("ALTER TABLE sitewatch_alert ADD COLUMN attempts INTEGER NOT NULL DEFAULT 0"));
    q.exec(QStringLiteral("ALTER TABLE sitewatch_alert ADD COLUMN last_error TEXT"));
    q.exec(QStringLiteral("ALTER TABLE sitewatch_alert ADD COLUMN last_attempt INTEGER"));
    q.exec(QStringLiteral("ALTER TABLE sitewatch_alert ADD COLUMN targets TEXT NOT NULL DEFAULT '[]'"));
    // Anciennes lignes (avant le suivi) : « notified » valait 1 pour une alerte envoyee.
    q.exec(QStringLiteral("UPDATE sitewatch_alert SET delivery = CASE WHEN notified=1 THEN 'sent' ELSE 'skipped' END "
                          "WHERE delivery = ''"));
    return true;
}

QJsonObject SiteWatchAlertStore::settings(const QString& scope) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT json FROM sitewatch_setting WHERE scope=?"));
    q.addBindValue(scope);
    if (q.exec() && q.next())
        return QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
    return {};
}

bool SiteWatchAlertStore::saveSettings(const QString& scope, const QJsonObject& obj) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO sitewatch_setting(scope, json) VALUES(?,?)"));
    q.addBindValue(scope);
    q.addBindValue(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    if (q.exec()) return true;
    m_error = q.lastError().text();
    return false;
}

QVector<SiteWatchAlertStore::Pending> SiteWatchAlertStore::record(
        const QString& siteId, const QVector<sitewatch::Alert>& alerts,
        const QString& minLevel, bool notifyEnabled) {
    QVector<Pending> toSend;
    if (siteId.isEmpty()) return toSend;

    QSqlQuery b(m_db);
    b.prepare(QStringLiteral("SELECT 1 FROM sitewatch_alert_baseline WHERE site_id=?"));
    b.addBindValue(siteId);
    b.exec();
    // Premiere evaluation du site : etat des lieux SILENCIEUX, pour ne pas inonder l'utilisateur
    // de l'historique recent au premier deploiement ; seules les situations nouvelles
    // sont ensuite envoyees.
    const bool baseline = !b.next();
    if (baseline) {
        QSqlQuery ib(m_db);
        ib.prepare(QStringLiteral("INSERT OR IGNORE INTO sitewatch_alert_baseline(site_id, created_at) VALUES(?,?)"));
        ib.addBindValue(siteId);
        ib.addBindValue(static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
        ib.exec();
    }
    const int minRank = levelRank(minLevel);
    for (const sitewatch::Alert& a : alerts) {
        QString delivery = QStringLiteral("pending");
        if (baseline || !notifyEnabled || !a.notify || levelRank(a.level) < minRank)
            delivery = QStringLiteral("skipped");
        if (a.muted) delivery = QStringLiteral("muted");
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO sitewatch_alert(key, site_id, rule, level, day, title, message, created_at, notified, delivery, targets) "
            "VALUES(?,?,?,?,?,?,?,?,0,?,?)"));
        ins.addBindValue(a.key); ins.addBindValue(siteId); ins.addBindValue(a.rule);
        ins.addBindValue(a.level); ins.addBindValue(a.day); ins.addBindValue(a.title);
        ins.addBindValue(a.message);
        ins.addBindValue(static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
        ins.addBindValue(delivery);
        ins.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(a.targets)).toJson(QJsonDocument::Compact)));
        if (ins.exec() && ins.numRowsAffected() > 0 && delivery == QLatin1String("pending"))
            toSend.append({a.key, a.title, a.message, a.level, a.targets});
    }
    return toSend;
}

QVector<SiteWatchAlertStore::Pending> SiteWatchAlertStore::due(int maxAttempts, int maxAgeDays) const {
    QVector<Pending> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT key, title, message, level, targets FROM sitewatch_alert "
        "WHERE delivery IN ('pending','failed') AND attempts < ? AND created_at >= ? ORDER BY created_at"));
    q.addBindValue(maxAttempts);
    q.addBindValue(static_cast<qint64>(QDateTime::currentSecsSinceEpoch()) - static_cast<qint64>(maxAgeDays) * 86400);
    if (q.exec())
        while (q.next())
            out.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                        sitewatch::sanitizeTargets(QJsonDocument::fromJson(q.value(4).toString().toUtf8()).array())});
    return out;
}

void SiteWatchAlertStore::markDelivery(const QString& key, bool ok, const QString& error) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE sitewatch_alert SET delivery=?, notified=?, attempts=attempts+1, last_error=?, last_attempt=? WHERE key=?"));
    q.addBindValue(ok ? QStringLiteral("sent") : QStringLiteral("failed"));
    q.addBindValue(ok ? 1 : 0);
    q.addBindValue(ok ? QString() : error);
    q.addBindValue(static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
    q.addBindValue(key);
    q.exec();
}

QJsonArray SiteWatchAlertStore::history(const QString& siteId, int limit) const {
    QJsonArray out;
    QSqlQuery q(m_db);
    // Pas de parametres numerotes : le pilote SQLite les melange avec les positionnels.
    q.prepare(QStringLiteral(
        "SELECT key, site_id, rule, level, day, title, message, created_at, delivery, attempts, last_error "
        "FROM sitewatch_alert ")
        + (siteId.isEmpty() ? QString() : QStringLiteral("WHERE site_id = ? "))
        + QStringLiteral("ORDER BY created_at DESC, day DESC LIMIT ?"));
    if (!siteId.isEmpty()) q.addBindValue(siteId);
    q.addBindValue(std::max(1, std::min(500, limit)));
    if (q.exec())
        while (q.next())
            out.append(QJsonObject{{"key", q.value(0).toString()}, {"site_id", q.value(1).toString()},
                {"rule", q.value(2).toString()}, {"level", q.value(3).toString()},
                {"day", q.value(4).toString()}, {"title", q.value(5).toString()},
                {"message", q.value(6).toString()},
                {"created_at", static_cast<double>(q.value(7).toLongLong())},
                {"delivery", q.value(8).toString()}, {"attempts", q.value(9).toInt()},
                {"last_error", q.value(10).toString()}, {"notified", q.value(8).toString() == QLatin1String("sent")}});
    return out;
}

int SiteWatchAlertStore::createdSince(qint64 ts) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM sitewatch_alert WHERE created_at >= ? AND delivery != 'skipped'"));
    q.addBindValue(ts);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

int SiteWatchAlertStore::failedCount() const {
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM sitewatch_alert WHERE delivery IN ('failed','pending')"));
    return q.next() ? q.value(0).toInt() : 0;
}

void SiteWatchAlertStore::pruneReports(const QString& siteId, int keep) {
    if (siteId.isEmpty() || keep < 1) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM sitewatch_report WHERE site_id=? AND id NOT IN "
        "(SELECT id FROM sitewatch_report WHERE site_id=? ORDER BY id DESC LIMIT ?)"));
    q.addBindValue(siteId);
    q.addBindValue(siteId);
    q.addBindValue(keep);
    q.exec();
}

} // namespace morfanalytics
