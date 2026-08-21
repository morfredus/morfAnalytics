/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/data/GitHubStore.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace morfanalytics {

GithubAssetClass classifyGithubAsset(const QString& filename) {
    GithubAssetClass c;
    c.canonical = filename;
    const QString n = filename.toLower();
    if (n.contains(QLatin1String("checksum")) || n.endsWith(QLatin1String(".sha256"))
        || n == QLatin1String("manifest.json") || n.contains(QLatin1String("source"))) {
        c.platform = QStringLiteral("other");
        c.architecture = QStringLiteral("none");
        return c;
    }
    if (n.contains(QLatin1String("firmware")) || n.endsWith(QLatin1String(".bin"))
        || n.endsWith(QLatin1String(".uf2")) || n.contains(QLatin1String("esp32"))) {
        c.platform = QStringLiteral("firmware");
        c.architecture = QStringLiteral("unknown");
        return c;
    }
    if (n.contains(QLatin1String("win64")) || n.contains(QLatin1String("windows"))
        || n.endsWith(QLatin1String(".exe")) || n.endsWith(QLatin1String(".msi"))) {
        c.platform = QStringLiteral("windows");
        c.architecture = QStringLiteral("x86_64");
        return c;
    }
    if (n.contains(QLatin1String("linux-arm64")) || n.contains(QLatin1String("aarch64"))
        || n.contains(QLatin1String("arm64"))) {
        c.platform = QStringLiteral("linux");
        c.architecture = QStringLiteral("arm64");
        return c;
    }
    if (n.contains(QLatin1String("linux-amd64")) || n.contains(QLatin1String("x86_64"))
        || n.contains(QLatin1String("amd64"))) {
        c.platform = QStringLiteral("linux");
        c.architecture = QStringLiteral("amd64");
        return c;
    }
    c.platform = QStringLiteral("other");
    c.architecture = QStringLiteral("unknown");
    return c;
}

GitHubStore::GitHubStore(QString dbPath) : m_dbPath(std::move(dbPath)) {
    m_connectionName = QStringLiteral("morfanalytics-github-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

GitHubStore::~GitHubStore() { close(); }

bool GitHubStore::isOpen() const { return m_db.isValid() && m_db.isOpen(); }

bool GitHubStore::exec(const QString& sql) {
    QSqlQuery q(m_db);
    if (q.exec(sql))
        return true;
    m_lastError = q.lastError().text();
    return false;
}

bool GitHubStore::open() {
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
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    const char* tables[] = {
        "CREATE TABLE IF NOT EXISTS github_repositories ("
        " full_name TEXT PRIMARY KEY, owner TEXT, name TEXT, source_id TEXT,"
        " stars INTEGER, forks INTEGER, watchers INTEGER,"
        " archived INTEGER, private INTEGER, last_release_tag TEXT,"
        " last_imported_at INTEGER)",
        "CREATE TABLE IF NOT EXISTS github_collection_runs ("
        " object_id TEXT PRIMARY KEY, full_name TEXT NOT NULL,"
        " collected_at TEXT NOT NULL, period_from TEXT, period_to TEXT,"
        " partial INTEGER NOT NULL, gap_before_days INTEGER, payload_hash TEXT,"
        " diagnostics TEXT)",
        "CREATE TABLE IF NOT EXISTS github_traffic_daily ("
        " full_name TEXT NOT NULL, metric TEXT NOT NULL, day TEXT NOT NULL,"
        " count INTEGER NOT NULL, uniques INTEGER NOT NULL,"
        " PRIMARY KEY (full_name, metric, day))",
        "CREATE TABLE IF NOT EXISTS github_traffic_snapshots ("
        " object_id TEXT PRIMARY KEY, full_name TEXT NOT NULL, collected_at TEXT,"
        " views_count INTEGER, views_uniques INTEGER,"
        " clones_count INTEGER, clones_uniques INTEGER, window_days INTEGER)",
        "CREATE TABLE IF NOT EXISTS github_popular_paths ("
        " object_id TEXT NOT NULL, full_name TEXT, collected_at TEXT,"
        " rank INTEGER, path TEXT, count INTEGER, uniques INTEGER,"
        " PRIMARY KEY (object_id, rank))",
        "CREATE TABLE IF NOT EXISTS github_referrers ("
        " object_id TEXT NOT NULL, full_name TEXT, collected_at TEXT,"
        " rank INTEGER, referrer TEXT, count INTEGER, uniques INTEGER,"
        " PRIMARY KEY (object_id, rank))",
        "CREATE TABLE IF NOT EXISTS github_releases ("
        " full_name TEXT NOT NULL, release_id INTEGER NOT NULL, tag TEXT,"
        " name TEXT, published_at TEXT, draft INTEGER, prerelease INTEGER,"
        " PRIMARY KEY (full_name, release_id))",
        "CREATE TABLE IF NOT EXISTS github_release_assets ("
        " full_name TEXT NOT NULL, release_id INTEGER NOT NULL,"
        " asset_id INTEGER NOT NULL, name TEXT, canonical_name TEXT,"
        " platform TEXT, architecture TEXT,"
        " PRIMARY KEY (full_name, asset_id))",
        "CREATE TABLE IF NOT EXISTS github_asset_download_snapshots ("
        " object_id TEXT NOT NULL, full_name TEXT, asset_id INTEGER NOT NULL,"
        " collected_at TEXT, download_count INTEGER NOT NULL, delta INTEGER,"
        " PRIMARY KEY (object_id, asset_id))"
    };
    for (const char* sql : tables) {
        if (!exec(QString::fromUtf8(sql)))
            return false;
    }
    return true;
}

void GitHubStore::close() {
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

bool GitHubStore::hasObject(const QString& objectId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM github_collection_runs WHERE object_id = ?"));
    q.addBindValue(objectId);
    return q.exec() && q.next();
}

qint64 GitHubStore::previousDownloadCount(const QString& fullName, qint64 assetId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT download_count FROM github_asset_download_snapshots "
        "WHERE full_name = ? AND asset_id = ? ORDER BY collected_at DESC LIMIT 1"));
    q.addBindValue(fullName);
    q.addBindValue(assetId);
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return -1;
}

bool GitHubStore::importSnapshot(const QString& objectId, const QJsonObject& snap) {
    if (!isOpen() || objectId.isEmpty())
        return false;
    if (hasObject(objectId))
        return true;   // deja importe : zero doublon

    const QString full = snap.value(QStringLiteral("full_name")).toString();
    const QString collectedAt = snap.value(QStringLiteral("collected_at")).toString();
    const QJsonObject period = snap.value(QStringLiteral("period")).toObject();
    const QJsonObject data = snap.value(QStringLiteral("data")).toObject();
    const QJsonObject repo = data.value(QStringLiteral("repository")).toObject();
    const QByteArray hash = QCryptographicHash::hash(
        QJsonDocument(snap).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();

    int gapDays = 0;
    QSqlQuery prev(m_db);
    prev.prepare(QStringLiteral(
        "SELECT collected_at FROM github_collection_runs WHERE full_name = ? "
        "ORDER BY collected_at DESC LIMIT 1"));
    prev.addBindValue(full);
    if (prev.exec() && prev.next()) {
        const QDateTime last = QDateTime::fromString(prev.value(0).toString(), Qt::ISODate);
        const QDateTime now = QDateTime::fromString(collectedAt, Qt::ISODate);
        if (last.isValid() && now.isValid())
            gapDays = last.daysTo(now);
    }

    QJsonArray diagnostics = snap.value(QStringLiteral("diagnostics")).toArray();
    if (gapDays > 14)
        diagnostics.append(QStringLiteral(
            "GitHub ne conserve que 14 jours de trafic detaille ; "
            "les jours anterieurs a cette fenetre sont perdus."));

    if (!m_db.transaction())
        return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO github_collection_runs(object_id, full_name, collected_at, "
        "period_from, period_to, partial, gap_before_days, payload_hash, diagnostics) "
        "VALUES(?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(objectId);
    q.addBindValue(full);
    q.addBindValue(collectedAt);
    q.addBindValue(period.value(QStringLiteral("from")).toString());
    q.addBindValue(period.value(QStringLiteral("to")).toString());
    q.addBindValue(snap.value(QStringLiteral("partial")).toBool() ? 1 : 0);
    q.addBindValue(gapDays);
    q.addBindValue(QString::fromLatin1(hash));
    q.addBindValue(QString::fromUtf8(QJsonDocument(diagnostics).toJson(QJsonDocument::Compact)));
    if (!q.exec()) { m_lastError = q.lastError().text(); m_db.rollback(); return false; }

    auto upsertDaily = [&](const QString& metric, const QJsonObject& block) {
        for (const QJsonValue& v : block.value(metric).toArray()) {
            const QJsonObject d = v.toObject();
            QString day = d.value(QStringLiteral("timestamp")).toString().left(10);
            if (day.size() < 10) continue;
            QSqlQuery u(m_db);
            u.prepare(QStringLiteral(
                "INSERT INTO github_traffic_daily(full_name, metric, day, count, uniques) "
                "VALUES(?,?,?,?,?) "
                "ON CONFLICT(full_name, metric, day) DO UPDATE SET "
                "count=excluded.count, uniques=excluded.uniques"));
            u.addBindValue(full);
            u.addBindValue(metric);
            u.addBindValue(day);
            u.addBindValue(static_cast<qint64>(d.value(QStringLiteral("count")).toDouble()));
            u.addBindValue(static_cast<qint64>(d.value(QStringLiteral("uniques")).toDouble()));
            if (!u.exec()) { m_lastError = u.lastError().text(); return false; }
        }
        return true;
    };
    const QJsonObject views = data.value(QStringLiteral("views")).toObject();
    const QJsonObject clones = data.value(QStringLiteral("clones")).toObject();
    if (!upsertDaily(QStringLiteral("views"), views) || !upsertDaily(QStringLiteral("clones"), clones)) {
        m_db.rollback();
        return false;
    }

    q.prepare(QStringLiteral(
        "INSERT INTO github_traffic_snapshots(object_id, full_name, collected_at, "
        "views_count, views_uniques, clones_count, clones_uniques, window_days) "
        "VALUES(?,?,?,?,?,?,?,?)"));
    q.addBindValue(objectId);
    q.addBindValue(full);
    q.addBindValue(collectedAt);
    q.addBindValue(static_cast<qint64>(views.value(QStringLiteral("count")).toDouble()));
    q.addBindValue(static_cast<qint64>(views.value(QStringLiteral("uniques")).toDouble()));
    q.addBindValue(static_cast<qint64>(clones.value(QStringLiteral("count")).toDouble()));
    q.addBindValue(static_cast<qint64>(clones.value(QStringLiteral("uniques")).toDouble()));
    q.addBindValue(period.value(QStringLiteral("days")).toInt(14));
    if (!q.exec()) { m_lastError = q.lastError().text(); m_db.rollback(); return false; }

    int rank = 1;
    for (const QJsonValue& v : data.value(QStringLiteral("popular_paths")).toArray()) {
        const QJsonObject p = v.toObject();
        QSqlQuery u(m_db);
        u.prepare(QStringLiteral(
            "INSERT INTO github_popular_paths(object_id, full_name, collected_at, rank, path, count, uniques) "
            "VALUES(?,?,?,?,?,?,?)"));
        u.addBindValue(objectId);
        u.addBindValue(full);
        u.addBindValue(collectedAt);
        u.addBindValue(rank++);
        u.addBindValue(p.value(QStringLiteral("path")).toString());
        u.addBindValue(static_cast<qint64>(p.value(QStringLiteral("count")).toDouble()));
        u.addBindValue(static_cast<qint64>(p.value(QStringLiteral("uniques")).toDouble()));
        if (!u.exec()) { m_lastError = u.lastError().text(); m_db.rollback(); return false; }
    }
    rank = 1;
    for (const QJsonValue& v : data.value(QStringLiteral("referrers")).toArray()) {
        const QJsonObject p = v.toObject();
        QSqlQuery u(m_db);
        u.prepare(QStringLiteral(
            "INSERT INTO github_referrers(object_id, full_name, collected_at, rank, referrer, count, uniques) "
            "VALUES(?,?,?,?,?,?,?)"));
        u.addBindValue(objectId);
        u.addBindValue(full);
        u.addBindValue(collectedAt);
        u.addBindValue(rank++);
        u.addBindValue(p.value(QStringLiteral("referrer")).toString());
        u.addBindValue(static_cast<qint64>(p.value(QStringLiteral("count")).toDouble()));
        u.addBindValue(static_cast<qint64>(p.value(QStringLiteral("uniques")).toDouble()));
        if (!u.exec()) { m_lastError = u.lastError().text(); m_db.rollback(); return false; }
    }

    QString lastTag;
    for (const QJsonValue& v : data.value(QStringLiteral("releases")).toArray()) {
        const QJsonObject rel = v.toObject();
        const qint64 relId = static_cast<qint64>(rel.value(QStringLiteral("id")).toDouble());
        if (lastTag.isEmpty())
            lastTag = rel.value(QStringLiteral("tag_name")).toString();
        QSqlQuery u(m_db);
        u.prepare(QStringLiteral(
            "INSERT INTO github_releases(full_name, release_id, tag, name, published_at, draft, prerelease) "
            "VALUES(?,?,?,?,?,?,?) "
            "ON CONFLICT(full_name, release_id) DO UPDATE SET "
            "tag=excluded.tag, name=excluded.name, published_at=excluded.published_at"));
        u.addBindValue(full);
        u.addBindValue(relId);
        u.addBindValue(rel.value(QStringLiteral("tag_name")).toString());
        u.addBindValue(rel.value(QStringLiteral("name")).toString());
        u.addBindValue(rel.value(QStringLiteral("published_at")).toString());
        u.addBindValue(rel.value(QStringLiteral("draft")).toBool() ? 1 : 0);
        u.addBindValue(rel.value(QStringLiteral("prerelease")).toBool() ? 1 : 0);
        if (!u.exec()) { m_lastError = u.lastError().text(); m_db.rollback(); return false; }

        for (const QJsonValue& av : rel.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject a = av.toObject();
            const qint64 assetId = static_cast<qint64>(a.value(QStringLiteral("id")).toDouble());
            const QString aname = a.value(QStringLiteral("name")).toString();
            const GithubAssetClass cls = classifyGithubAsset(aname);
            const qint64 count = static_cast<qint64>(a.value(QStringLiteral("download_count")).toDouble());
            QSqlQuery ua(m_db);
            ua.prepare(QStringLiteral(
                "INSERT INTO github_release_assets(full_name, release_id, asset_id, name, "
                "canonical_name, platform, architecture) VALUES(?,?,?,?,?,?,?) "
                "ON CONFLICT(full_name, asset_id) DO UPDATE SET "
                "name=excluded.name, canonical_name=excluded.canonical_name, "
                "platform=excluded.platform, architecture=excluded.architecture"));
            ua.addBindValue(full);
            ua.addBindValue(relId);
            ua.addBindValue(assetId);
            ua.addBindValue(aname);
            ua.addBindValue(cls.canonical);
            ua.addBindValue(cls.platform);
            ua.addBindValue(cls.architecture);
            if (!ua.exec()) { m_lastError = ua.lastError().text(); m_db.rollback(); return false; }

            const qint64 prevCount = previousDownloadCount(full, assetId);
            QVariant delta;
            if (prevCount >= 0) {
                const qint64 d = count - prevCount;
                delta = d < 0 ? 0 : d;   // jamais un telechargement negatif
            }
            QSqlQuery us(m_db);
            us.prepare(QStringLiteral(
                "INSERT INTO github_asset_download_snapshots(object_id, full_name, asset_id, "
                "collected_at, download_count, delta) VALUES(?,?,?,?,?,?)"));
            us.addBindValue(objectId);
            us.addBindValue(full);
            us.addBindValue(assetId);
            us.addBindValue(collectedAt);
            us.addBindValue(count);
            us.addBindValue(delta);
            if (!us.exec()) { m_lastError = us.lastError().text(); m_db.rollback(); return false; }
        }
    }

    q.prepare(QStringLiteral(
        "INSERT INTO github_repositories(full_name, owner, name, source_id, stars, forks, "
        "watchers, archived, private, last_release_tag, last_imported_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(full_name) DO UPDATE SET stars=excluded.stars, forks=excluded.forks, "
        "watchers=excluded.watchers, archived=excluded.archived, private=excluded.private, "
        "last_release_tag=excluded.last_release_tag, last_imported_at=excluded.last_imported_at"));
    q.addBindValue(full);
    q.addBindValue(snap.value(QStringLiteral("owner")).toString());
    q.addBindValue(snap.value(QStringLiteral("repository")).toString());
    q.addBindValue(QStringLiteral("sitewatch:github:") + full);
    q.addBindValue(repo.value(QStringLiteral("stargazers_count")).toInt());
    q.addBindValue(repo.value(QStringLiteral("forks_count")).toInt());
    q.addBindValue(repo.value(QStringLiteral("watchers_count")).toInt(
        repo.value(QStringLiteral("subscribers_count")).toInt()));
    q.addBindValue(repo.value(QStringLiteral("archived")).toBool() ? 1 : 0);
    q.addBindValue(repo.value(QStringLiteral("private")).toBool() ? 1 : 0);
    q.addBindValue(lastTag);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec()) { m_lastError = q.lastError().text(); m_db.rollback(); return false; }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

QJsonObject GitHubStore::overview(const QString& repo, const QString& fromDay,
                                  const QString& toDay) const {
    QJsonObject o;
    auto trafficAnd = [&](bool includeRepo) {
        QString s;
        if (includeRepo && !repo.isEmpty())
            s += QStringLiteral(" AND full_name=?");
        if (!fromDay.isEmpty())
            s += QStringLiteral(" AND day>=?");
        if (!toDay.isEmpty())
            s += QStringLiteral(" AND day<=?");
        return s;
    };
    auto bindTraffic = [&](QSqlQuery& q, bool includeRepo) {
        if (includeRepo && !repo.isEmpty())
            q.addBindValue(repo);
        if (!fromDay.isEmpty())
            q.addBindValue(fromDay);
        if (!toDay.isEmpty())
            q.addBindValue(toDay);
    };

    QSqlQuery q(m_db);
    if (repo.isEmpty())
        q.exec(QStringLiteral("SELECT COUNT(*) FROM github_repositories"));
    else {
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM github_repositories WHERE full_name=?"));
        q.addBindValue(repo);
        q.exec();
    }
    o[QStringLiteral("repositories")] = q.next() ? q.value(0).toInt() : 0;

    q.prepare(QStringLiteral("SELECT SUM(count) FROM github_traffic_daily WHERE metric='views'")
              + trafficAnd(true));
    bindTraffic(q, true);
    q.exec();
    o[QStringLiteral("views_total")] = q.next() ? q.value(0).toLongLong() : 0;

    q.prepare(QStringLiteral("SELECT SUM(count) FROM github_traffic_daily WHERE metric='clones'")
              + trafficAnd(true));
    bindTraffic(q, true);
    q.exec();
    o[QStringLiteral("clones_total")] = q.next() ? q.value(0).toLongLong() : 0;

    q.exec(QStringLiteral("SELECT SUM(CASE WHEN delta IS NULL THEN 0 ELSE delta END) "
                          "FROM github_asset_download_snapshots"));
    o[QStringLiteral("downloads_delta")] = q.next() ? q.value(0).toLongLong() : 0;

    q.prepare(QStringLiteral(
        "SELECT COUNT(DISTINCT day) FROM github_traffic_daily WHERE 1=1") + trafficAnd(true));
    bindTraffic(q, true);
    q.exec();
    o[QStringLiteral("days_known")] = q.next() ? q.value(0).toInt() : 0;

    q.prepare(QStringLiteral(
        "SELECT day, SUM(count) AS v FROM github_traffic_daily WHERE metric='views'")
              + trafficAnd(true) + QStringLiteral(" GROUP BY day ORDER BY v DESC LIMIT 1"));
    bindTraffic(q, true);
    q.exec();
    if (q.next()) {
        o[QStringLiteral("busiest_day")] = q.value(0).toString();
        o[QStringLiteral("busiest_views")] = q.value(1).toLongLong();
    }

    const qint64 viewsTotal = static_cast<qint64>(
        o.value(QStringLiteral("views_total")).toDouble());
    const int daysKnown = o.value(QStringLiteral("days_known")).toInt();
    o[QStringLiteral("avg_views_per_day")] =
        daysKnown > 0 ? viewsTotal / daysKnown : 0;

    QJsonArray repos;
    QString repoSql = QStringLiteral(
        "SELECT r.full_name, r.stars, r.last_release_tag, "
        "(SELECT SUM(count) FROM github_traffic_daily d WHERE d.full_name=r.full_name "
        " AND d.metric='views'");
    repoSql += trafficAnd(false);
    repoSql += QStringLiteral(") AS views, "
        "(SELECT SUM(count) FROM github_traffic_daily d WHERE d.full_name=r.full_name "
        " AND d.metric='clones'");
    repoSql += trafficAnd(false);
    repoSql += QStringLiteral(") AS clones FROM github_repositories r");
    if (!repo.isEmpty())
        repoSql += QStringLiteral(" WHERE r.full_name=?");
    repoSql += QStringLiteral(" ORDER BY views DESC");
    q.prepare(repoSql);
    bindTraffic(q, false); // first subquery views
    bindTraffic(q, false); // clones subquery
    if (!repo.isEmpty())
        q.addBindValue(repo);
    q.exec();
    while (q.next()) {
        repos.append(QJsonObject{
            {QStringLiteral("full_name"), q.value(0).toString()},
            {QStringLiteral("stars"), q.value(1).toInt()},
            {QStringLiteral("last_release"), q.value(2).toString()},
            {QStringLiteral("views"), q.value(3).toLongLong()},
            {QStringLiteral("clones"), q.value(4).toLongLong()},
        });
    }
    o[QStringLiteral("repos")] = repos;

    QJsonArray daily;
    q.prepare(QStringLiteral(
        "SELECT day, SUM(CASE WHEN metric='views' THEN count ELSE 0 END), "
        "SUM(CASE WHEN metric='clones' THEN count ELSE 0 END) "
        "FROM github_traffic_daily WHERE 1=1") + trafficAnd(true)
              + QStringLiteral(" GROUP BY day ORDER BY day"));
    bindTraffic(q, true);
    q.exec();
    while (q.next()) {
        daily.append(QJsonObject{
            {QStringLiteral("day"), q.value(0).toString()},
            {QStringLiteral("views"), q.value(1).toLongLong()},
            {QStringLiteral("clones"), q.value(2).toLongLong()},
        });
    }
    o[QStringLiteral("daily")] = daily;

    QJsonArray platforms;
    q.exec(QStringLiteral(
        "SELECT a.platform, SUM(s.delta) FROM github_asset_download_snapshots s "
        "JOIN github_release_assets a ON a.full_name=s.full_name AND a.asset_id=s.asset_id "
        "WHERE s.delta IS NOT NULL GROUP BY a.platform"));
    while (q.next()) {
        platforms.append(QJsonObject{
            {QStringLiteral("platform"), q.value(0).toString()},
            {QStringLiteral("downloads"), q.value(1).toLongLong()},
        });
    }
    o[QStringLiteral("platforms")] = platforms;
    o[QStringLiteral("uniques_note")] = QStringLiteral(
        "Les visiteurs uniques quotidiens ne s'additionnent pas en un unique de periode.");
    o[QStringLiteral("filter_repo")] = repo;
    o[QStringLiteral("filter_from")] = fromDay;
    o[QStringLiteral("filter_to")] = toDay;
    return o;
}

QJsonObject GitHubStore::repository(const QString& fullName) const {
    QJsonObject o;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT owner, name, stars, forks, watchers, last_release_tag "
                             "FROM github_repositories WHERE full_name=?"));
    q.addBindValue(fullName);
    if (!q.exec() || !q.next())
        return o;
    o[QStringLiteral("full_name")] = fullName;
    o[QStringLiteral("owner")] = q.value(0).toString();
    o[QStringLiteral("name")] = q.value(1).toString();
    o[QStringLiteral("stars")] = q.value(2).toInt();
    o[QStringLiteral("forks")] = q.value(3).toInt();
    o[QStringLiteral("watchers")] = q.value(4).toInt();
    o[QStringLiteral("last_release")] = q.value(5).toString();

    QJsonArray daily;
    q.prepare(QStringLiteral(
        "SELECT day, metric, count, uniques FROM github_traffic_daily "
        "WHERE full_name=? ORDER BY day"));
    q.addBindValue(fullName);
    q.exec();
    while (q.next()) {
        daily.append(QJsonObject{
            {QStringLiteral("day"), q.value(0).toString()},
            {QStringLiteral("metric"), q.value(1).toString()},
            {QStringLiteral("count"), q.value(2).toLongLong()},
            {QStringLiteral("uniques"), q.value(3).toLongLong()},
        });
    }
    o[QStringLiteral("daily")] = daily;

    q.prepare(QStringLiteral(
        "SELECT views_count, views_uniques, clones_count, clones_uniques, window_days, collected_at "
        "FROM github_traffic_snapshots WHERE full_name=? ORDER BY collected_at DESC LIMIT 1"));
    q.addBindValue(fullName);
    if (q.exec() && q.next()) {
        o[QStringLiteral("window")] = QJsonObject{
            {QStringLiteral("views_count"), q.value(0).toLongLong()},
            {QStringLiteral("views_uniques"), q.value(1).toLongLong()},
            {QStringLiteral("clones_count"), q.value(2).toLongLong()},
            {QStringLiteral("clones_uniques"), q.value(3).toLongLong()},
            {QStringLiteral("window_days"), q.value(4).toInt()},
            {QStringLiteral("collected_at"), q.value(5).toString()},
        };
    }

    QJsonArray paths, refs, assets;
    q.prepare(QStringLiteral(
        "SELECT path, count, uniques FROM github_popular_paths WHERE object_id=("
        "SELECT object_id FROM github_collection_runs WHERE full_name=? "
        "ORDER BY collected_at DESC LIMIT 1) ORDER BY rank"));
    q.addBindValue(fullName);
    q.exec();
    while (q.next()) {
        paths.append(QJsonObject{
            {QStringLiteral("path"), q.value(0).toString()},
            {QStringLiteral("count"), q.value(1).toLongLong()},
            {QStringLiteral("uniques"), q.value(2).toLongLong()},
        });
    }
    o[QStringLiteral("popular_paths")] = paths;

    q.prepare(QStringLiteral(
        "SELECT referrer, count, uniques FROM github_referrers WHERE object_id=("
        "SELECT object_id FROM github_collection_runs WHERE full_name=? "
        "ORDER BY collected_at DESC LIMIT 1) ORDER BY rank"));
    q.addBindValue(fullName);
    q.exec();
    while (q.next()) {
        refs.append(QJsonObject{
            {QStringLiteral("referrer"), q.value(0).toString()},
            {QStringLiteral("count"), q.value(1).toLongLong()},
            {QStringLiteral("uniques"), q.value(2).toLongLong()},
        });
    }
    o[QStringLiteral("referrers")] = refs;

    q.prepare(QStringLiteral(
        "SELECT a.name, a.platform, a.architecture, "
        "(SELECT SUM(delta) FROM github_asset_download_snapshots s "
        " WHERE s.full_name=a.full_name AND s.asset_id=a.asset_id) AS dl "
        "FROM github_release_assets a WHERE a.full_name=?"));
    q.addBindValue(fullName);
    q.exec();
    while (q.next()) {
        assets.append(QJsonObject{
            {QStringLiteral("name"), q.value(0).toString()},
            {QStringLiteral("platform"), q.value(1).toString()},
            {QStringLiteral("architecture"), q.value(2).toString()},
            {QStringLiteral("downloads_delta"), q.value(3).toLongLong()},
        });
    }
    o[QStringLiteral("assets")] = assets;
    o[QStringLiteral("uniques_note")] = QStringLiteral(
        "Les uniques quotidiens ne representent pas des personnes distinctes sur toute la periode.");
    return o;
}

bool GitHubStore::ingestAuthority(const QJsonObject& payload) {
    if (!isOpen())
        return false;
    if (payload.value(QStringLiteral("contract")).toString() != QLatin1String("sitewatch-github/1")) {
        m_lastError = QStringLiteral("contrat sitewatch-github/1 attendu");
        return false;
    }
    const QString published = payload.value(QStringLiteral("published_at")).toString();
    if (!m_db.transaction())
        return false;
    for (const QJsonValue& v : payload.value(QStringLiteral("repositories")).toArray()) {
        const QJsonObject repo = v.toObject();
        const QString full = repo.value(QStringLiteral("full_name")).toString();
        if (full.isEmpty())
            continue;
        const int slash = full.indexOf(QLatin1Char('/'));
        const QString owner = slash > 0 ? full.left(slash) : full;
        const QString name = slash > 0 ? full.mid(slash + 1) : full;
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO github_repositories(full_name, owner, name, source_id, stars, forks, "
            "watchers, archived, private, last_release_tag, last_imported_at) "
            "VALUES(?,?,?,?,?,0,0,0,0,?,?) "
            "ON CONFLICT(full_name) DO UPDATE SET last_release_tag=excluded.last_release_tag, "
            "stars=excluded.stars, last_imported_at=excluded.last_imported_at"));
        q.addBindValue(full);
        q.addBindValue(owner);
        q.addBindValue(name);
        q.addBindValue(QStringLiteral("sitewatch:github:") + full);
        q.addBindValue(static_cast<qint64>(repo.value(QStringLiteral("stars")).toDouble()));
        q.addBindValue(repo.value(QStringLiteral("last_release")).toString());
        q.addBindValue(QDateTime::currentSecsSinceEpoch());
        if (!q.exec()) { m_lastError = q.lastError().text(); m_db.rollback(); return false; }

        for (const QJsonValue& dv : repo.value(QStringLiteral("daily")).toArray()) {
            const QJsonObject d = dv.toObject();
            QSqlQuery u(m_db);
            u.prepare(QStringLiteral(
                "INSERT INTO github_traffic_daily(full_name, metric, day, count, uniques) "
                "VALUES(?,?,?,?,?) "
                "ON CONFLICT(full_name, metric, day) DO UPDATE SET "
                "count=excluded.count, uniques=excluded.uniques"));
            u.addBindValue(full);
            u.addBindValue(d.value(QStringLiteral("metric")).toString());
            u.addBindValue(d.value(QStringLiteral("day")).toString());
            u.addBindValue(static_cast<qint64>(d.value(QStringLiteral("count")).toDouble()));
            u.addBindValue(static_cast<qint64>(d.value(QStringLiteral("uniques")).toDouble()));
            if (!u.exec()) { m_lastError = u.lastError().text(); m_db.rollback(); return false; }
        }

        const QString collectedAt = published.isEmpty()
            ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
            : published;
        const QString oid = QStringLiteral("sitewatch:") + full + QLatin1Char(':') + collectedAt;
        QSqlQuery run(m_db);
        run.prepare(QStringLiteral(
            "INSERT INTO github_collection_runs(object_id, full_name, collected_at, "
            "period_from, period_to, partial, gap_before_days, payload_hash, diagnostics) "
            "VALUES(?,?,?,?,?,0,0,'','') ON CONFLICT(object_id) DO NOTHING"));
        run.addBindValue(oid);
        run.addBindValue(full);
        run.addBindValue(collectedAt);
        run.addBindValue(QString());
        run.addBindValue(QString());
        if (!run.exec()) { m_lastError = run.lastError().text(); m_db.rollback(); return false; }

        QSqlQuery snap(m_db);
        snap.prepare(QStringLiteral(
            "INSERT INTO github_traffic_snapshots(object_id, full_name, collected_at, "
            "views_count, views_uniques, clones_count, clones_uniques, window_days) "
            "VALUES(?,?,?,?,?,?,?,14) ON CONFLICT(object_id) DO UPDATE SET "
            "views_count=excluded.views_count, views_uniques=excluded.views_uniques, "
            "clones_count=excluded.clones_count, clones_uniques=excluded.clones_uniques"));
        snap.addBindValue(oid);
        snap.addBindValue(full);
        snap.addBindValue(collectedAt);
        snap.addBindValue(static_cast<qint64>(repo.value(QStringLiteral("views_14")).toDouble()));
        snap.addBindValue(static_cast<qint64>(repo.value(QStringLiteral("uniques_14")).toDouble()));
        snap.addBindValue(static_cast<qint64>(repo.value(QStringLiteral("clones")).toDouble()));
        snap.addBindValue(0);
        if (!snap.exec()) { m_lastError = snap.lastError().text(); m_db.rollback(); return false; }

        int rank = 0;
        for (const QJsonValue& pv : repo.value(QStringLiteral("popular_paths")).toArray()) {
            const QJsonObject p = pv.toObject();
            QSqlQuery ins(m_db);
            ins.prepare(QStringLiteral(
                "INSERT INTO github_popular_paths(object_id, full_name, collected_at, rank, "
                "path, count, uniques) VALUES(?,?,?,?,?,?,?) "
                "ON CONFLICT(object_id, rank) DO UPDATE SET path=excluded.path, "
                "count=excluded.count, uniques=excluded.uniques"));
            ins.addBindValue(oid);
            ins.addBindValue(full);
            ins.addBindValue(collectedAt);
            ins.addBindValue(rank++);
            ins.addBindValue(p.value(QStringLiteral("path")).toString());
            ins.addBindValue(static_cast<qint64>(p.value(QStringLiteral("count")).toDouble()));
            ins.addBindValue(static_cast<qint64>(p.value(QStringLiteral("uniques")).toDouble()));
            if (!ins.exec()) { m_lastError = ins.lastError().text(); m_db.rollback(); return false; }
        }
        rank = 0;
        for (const QJsonValue& rv : repo.value(QStringLiteral("referrers")).toArray()) {
            const QJsonObject r = rv.toObject();
            QSqlQuery ins(m_db);
            ins.prepare(QStringLiteral(
                "INSERT INTO github_referrers(object_id, full_name, collected_at, rank, "
                "referrer, count, uniques) VALUES(?,?,?,?,?,?,?) "
                "ON CONFLICT(object_id, rank) DO UPDATE SET referrer=excluded.referrer, "
                "count=excluded.count, uniques=excluded.uniques"));
            ins.addBindValue(oid);
            ins.addBindValue(full);
            ins.addBindValue(collectedAt);
            ins.addBindValue(rank++);
            ins.addBindValue(r.value(QStringLiteral("referrer")).toString());
            ins.addBindValue(static_cast<qint64>(r.value(QStringLiteral("count")).toDouble()));
            ins.addBindValue(static_cast<qint64>(r.value(QStringLiteral("uniques")).toDouble()));
            if (!ins.exec()) { m_lastError = ins.lastError().text(); m_db.rollback(); return false; }
        }
        for (const QJsonValue& av : repo.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject a = av.toObject();
            const qint64 assetId = static_cast<qint64>(a.value(QStringLiteral("asset_id")).toDouble());
            if (assetId <= 0)
                continue;
            const QString aname = a.value(QStringLiteral("name")).toString();
            const GithubAssetClass cls = classifyGithubAsset(aname);
            const qint64 count = static_cast<qint64>(
                a.value(QStringLiteral("download_count")).toDouble());
            QSqlQuery ua(m_db);
            ua.prepare(QStringLiteral(
                "INSERT INTO github_release_assets(full_name, release_id, asset_id, name, "
                "canonical_name, platform, architecture) VALUES(?,?,?,?,?,?,?) "
                "ON CONFLICT(full_name, asset_id) DO UPDATE SET name=excluded.name, "
                "platform=excluded.platform, architecture=excluded.architecture"));
            ua.addBindValue(full);
            ua.addBindValue(0);
            ua.addBindValue(assetId);
            ua.addBindValue(aname);
            ua.addBindValue(cls.canonical);
            ua.addBindValue(cls.platform);
            ua.addBindValue(cls.architecture);
            if (!ua.exec()) { m_lastError = ua.lastError().text(); m_db.rollback(); return false; }

            const qint64 prevCount = previousDownloadCount(full, assetId);
            QVariant delta;
            if (prevCount >= 0) {
                const qint64 dlt = count - prevCount;
                delta = dlt < 0 ? 0 : dlt;
            }
            QSqlQuery us(m_db);
            us.prepare(QStringLiteral(
                "INSERT INTO github_asset_download_snapshots(object_id, full_name, asset_id, "
                "collected_at, download_count, delta) VALUES(?,?,?,?,?,?) "
                "ON CONFLICT(object_id, asset_id) DO UPDATE SET "
                "download_count=excluded.download_count, delta=excluded.delta"));
            us.addBindValue(oid);
            us.addBindValue(full);
            us.addBindValue(assetId);
            us.addBindValue(collectedAt);
            us.addBindValue(count);
            us.addBindValue(delta);
            if (!us.exec()) { m_lastError = us.lastError().text(); m_db.rollback(); return false; }
        }
    }
    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    return true;
}

} // namespace morfanalytics
