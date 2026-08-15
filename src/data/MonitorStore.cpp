/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/data/MonitorStore.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QDateTime>
#include <QJsonDocument>

#include <cmath>

namespace morfanalytics {

namespace {
// NaN => NULL en base : l'invariant « absence != zéro » passe par le type NULL
// de SQLite, jamais par une valeur sentinelle.
QVariant orNull(double v) {
    return std::isnan(v) ? QVariant() : QVariant(v);
}
// Lecture inverse : une colonne NULL redevient un JSON null (jamais 0).
QJsonValue numOrNull(const QVariant& v) {
    return v.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(v.toDouble());
}
} // namespace

MonitorStore::MonitorStore(QString dbPath) : m_dbPath(std::move(dbPath)) {
    m_connectionName = QStringLiteral("morfanalytics-monitor-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

MonitorStore::~MonitorStore() { close(); }

bool MonitorStore::isOpen() const { return m_db.isValid() && m_db.isOpen(); }

bool MonitorStore::open() {
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

    // Registre des machines : la clé est un identifiant stable (hostname pour
    // l'instant, idéalement l'instance morfBeacon plus tard) pour que l'historique
    // reste associé à la bonne machine même après un redémarrage ou un changement d'IP.
    const bool okMachine = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS machine ("
        " id INTEGER PRIMARY KEY,"
        " key TEXT UNIQUE NOT NULL,"
        " hostname TEXT,"
        " model TEXT,"
        " first_seen INTEGER,"
        " last_seen INTEGER)"));

    // Table LARGE machine : une ligne par (machine, ts). Colonnes NULL-ables.
    const bool okSM = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sample_machine ("
        " machine_id INTEGER NOT NULL,"
        " ts INTEGER NOT NULL,"
        " cpu_percent REAL, load1 REAL, mem_percent REAL, mem_used REAL,"
        " mem_total REAL, swap_percent REAL, temp_cpu REAL, disk_percent REAL,"
        " uptime_s REAL, services_active REAL,"
        " PRIMARY KEY (machine_id, ts))"));

    // Table par service : une ligne par (machine, ts, service).
    const bool okSS = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sample_service ("
        " machine_id INTEGER NOT NULL,"
        " ts INTEGER NOT NULL,"
        " service TEXT NOT NULL,"
        " cpu_percent REAL, mem_bytes REAL, tasks REAL,"
        " PRIMARY KEY (machine_id, ts, service))"));

    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_ss_svc "
                          "ON sample_service (machine_id, service, ts)"));

    // Table des activités (compilations, indexations…), INDÉPENDANTE des samples :
    // la mémoire remarquable survit à la purge du brut. Signalées par le composant
    // qui les connaît (jamais devinées).
    const bool okAct = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS activity ("
        " id INTEGER PRIMARY KEY,"
        " type TEXT NOT NULL,"
        " project TEXT,"
        " machine TEXT,"
        " start_ts INTEGER,"
        " end_ts INTEGER,"
        " duration_s INTEGER,"
        " status TEXT,"
        " metadata TEXT)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_activity_time "
                          "ON activity (machine, start_ts)"));

    if (!okMachine || !okSM || !okSS || !okAct) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

void MonitorStore::close() {
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

int MonitorStore::machineIdForKey(const QString& key) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM machine WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}

int MonitorStore::upsertMachine(const QString& key, const QString& hostname,
                                const QString& model, qint64 ts) {
    QSqlQuery q(m_db);
    // INSERT OR IGNORE puis UPDATE : idempotent, et rafraîchit hostname/model/last_seen.
    q.prepare(QStringLiteral(
        "INSERT INTO machine (key, hostname, model, first_seen, last_seen) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(key) DO UPDATE SET hostname=excluded.hostname, "
        "model=excluded.model, last_seen=excluded.last_seen"));
    q.addBindValue(key);
    q.addBindValue(hostname);
    q.addBindValue(model);
    q.addBindValue(static_cast<qlonglong>(ts));
    q.addBindValue(static_cast<qlonglong>(ts));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return machineIdForKey(key);
}

bool MonitorStore::insertMachineSample(int machineId, qint64 ts, const MachineSample& s) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO sample_machine "
        "(machine_id, ts, cpu_percent, load1, mem_percent, mem_used, mem_total, "
        " swap_percent, temp_cpu, disk_percent, uptime_s, services_active) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(machineId);
    q.addBindValue(static_cast<qlonglong>(ts));
    q.addBindValue(orNull(s.cpuPercent));
    q.addBindValue(orNull(s.load1));
    q.addBindValue(orNull(s.memPercent));
    q.addBindValue(orNull(s.memUsed));
    q.addBindValue(orNull(s.memTotal));
    q.addBindValue(orNull(s.swapPercent));
    q.addBindValue(orNull(s.tempCpu));
    q.addBindValue(orNull(s.diskPercent));
    q.addBindValue(orNull(s.uptimeS));
    q.addBindValue(orNull(s.servicesActive));
    if (!q.exec()) { m_lastError = q.lastError().text(); return false; }
    return true;
}

bool MonitorStore::insertServiceSamples(int machineId, qint64 ts,
                                        const QVector<ServiceSample>& list) {
    if (list.isEmpty())
        return true;
    // Un lot dans UNE transaction : bien plus rapide que N commits séparés.
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO sample_service "
        "(machine_id, ts, service, cpu_percent, mem_bytes, tasks) VALUES (?,?,?,?,?,?)"));
    for (const ServiceSample& s : list) {
        q.addBindValue(machineId);
        q.addBindValue(static_cast<qlonglong>(ts));
        q.addBindValue(s.service);
        q.addBindValue(orNull(s.cpuPercent));
        q.addBindValue(orNull(s.memBytes));
        q.addBindValue(orNull(s.tasks));
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            m_db.rollback();
            return false;
        }
    }
    m_db.commit();
    return true;
}

QJsonArray MonitorStore::machines() const {
    QJsonArray arr;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT key, hostname, model, first_seen, last_seen FROM machine "
            "ORDER BY hostname, key")))
        return arr;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    while (q.next()) {
        const qint64 lastSeen = q.value(4).toLongLong();
        arr.append(QJsonObject{
            {"key",       q.value(0).toString()},
            {"hostname",  q.value(1).toString()},
            {"model",     q.value(2).toString()},
            {"first_seen", static_cast<double>(q.value(3).toLongLong())},
            {"last_seen",  static_cast<double>(lastSeen)},
            // « en ligne » = vu il y a moins de 2 minutes (au moins un relevé récent).
            {"online",    (now - lastSeen) < 120},
        });
    }
    return arr;
}

QJsonObject MonitorStore::latestMachine(int machineId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT ts, cpu_percent, load1, mem_percent, mem_used, mem_total, "
        "swap_percent, temp_cpu, disk_percent, uptime_s, services_active "
        "FROM sample_machine WHERE machine_id = ? ORDER BY ts DESC LIMIT 1"));
    q.addBindValue(machineId);
    if (!q.exec() || !q.next())
        return {};
    return QJsonObject{
        {"ts",              static_cast<double>(q.value(0).toLongLong())},
        {"cpu_percent",     numOrNull(q.value(1))},
        {"load1",           numOrNull(q.value(2))},
        {"mem_percent",     numOrNull(q.value(3))},
        {"mem_used",        numOrNull(q.value(4))},
        {"mem_total",       numOrNull(q.value(5))},
        {"swap_percent",    numOrNull(q.value(6))},
        {"temp_cpu",        numOrNull(q.value(7))},
        {"disk_percent",    numOrNull(q.value(8))},
        {"uptime_s",        numOrNull(q.value(9))},
        {"services_active", numOrNull(q.value(10))},
    };
}

QJsonObject MonitorStore::machineSeries(int machineId, qint64 from, qint64 to,
                                        int maxPoints) const {
    if (maxPoints < 1) maxPoints = 1;
    // Bucket = période / points visés, borné à 1 s minimum. Choisi côté serveur :
    // la granularité suit la fenêtre demandée, le navigateur ne reçoit que des buckets.
    qint64 span = to - from;
    if (span < 1) span = 1;
    qint64 bucket = span / maxPoints;
    if (bucket < 1) bucket = 1;

    QJsonArray ts, cpu, cpuMax, mem, temp, tempMax, load;
    QSqlQuery q(m_db);
    // AVG/MIN/MAX de SQLite ignorent les NULL : un bucket sans mesure d'une métrique
    // ressort NULL et devient un trou (null) dans la série, pas un zéro.
    q.prepare(QStringLiteral(
        "SELECT (ts/?)*? AS b, "
        " AVG(cpu_percent), MAX(cpu_percent), "
        " AVG(mem_percent), AVG(temp_cpu), MAX(temp_cpu), AVG(load1) "
        "FROM sample_machine WHERE machine_id=? AND ts>=? AND ts<=? "
        "GROUP BY b ORDER BY b"));
    q.addBindValue(static_cast<qlonglong>(bucket));
    q.addBindValue(static_cast<qlonglong>(bucket));
    q.addBindValue(machineId);
    q.addBindValue(static_cast<qlonglong>(from));
    q.addBindValue(static_cast<qlonglong>(to));
    if (q.exec()) {
        while (q.next()) {
            ts.append(static_cast<double>(q.value(0).toLongLong()));
            cpu.append(numOrNull(q.value(1)));
            cpuMax.append(numOrNull(q.value(2)));
            mem.append(numOrNull(q.value(3)));
            temp.append(numOrNull(q.value(4)));
            tempMax.append(numOrNull(q.value(5)));
            load.append(numOrNull(q.value(6)));
        }
    }
    return QJsonObject{
        {"bucket_s", static_cast<double>(bucket)},
        {"ts", ts},
        {"cpu", cpu}, {"cpu_max", cpuMax},
        {"mem", mem},
        {"temp", temp}, {"temp_max", tempMax},
        {"load", load},
    };
}

QJsonArray MonitorStore::serviceStats(int machineId, qint64 from, qint64 to) const {
    QJsonArray arr;
    QSqlQuery q(m_db);
    // AVG/MAX ignorent les NULL : un service dont le CPU n'a jamais été mesuré sur
    // la fenêtre ressort avec cpu_avg null, pas 0. Tri par CPU moyen décroissant :
    // « qui a le plus consommé sur la période » en tête.
    q.prepare(QStringLiteral(
        "SELECT service, AVG(cpu_percent), MAX(cpu_percent), "
        " AVG(mem_bytes), MAX(mem_bytes), COUNT(*) "
        "FROM sample_service WHERE machine_id=? AND ts>=? AND ts<=? "
        "GROUP BY service ORDER BY AVG(cpu_percent) DESC, service"));
    q.addBindValue(machineId);
    q.addBindValue(static_cast<qlonglong>(from));
    q.addBindValue(static_cast<qlonglong>(to));
    if (q.exec()) {
        while (q.next()) {
            arr.append(QJsonObject{
                {"service", q.value(0).toString()},
                {"cpu_avg", numOrNull(q.value(1))},
                {"cpu_max", numOrNull(q.value(2))},
                {"mem_avg", numOrNull(q.value(3))},
                {"mem_max", numOrNull(q.value(4))},
                {"samples", static_cast<double>(q.value(5).toLongLong())},
            });
        }
    }
    return arr;
}

qint64 MonitorStore::purgeSamplesBefore(qint64 cutoffTs) {
    QSqlQuery q(m_db);
    qint64 total = 0;
    for (const char* table : {"sample_machine", "sample_service"}) {
        q.prepare(QStringLiteral("DELETE FROM %1 WHERE ts < ?").arg(QLatin1String(table)));
        q.addBindValue(static_cast<qlonglong>(cutoffTs));
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            return -1;
        }
        total += q.numRowsAffected();
    }
    return total;
}

qint64 MonitorStore::insertActivity(const QString& type, const QString& project,
                                    const QString& machine, qint64 startTs, qint64 endTs,
                                    const QString& status, const QJsonObject& metadata) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO activity (type, project, machine, start_ts, end_ts, duration_s, status, metadata) "
        "VALUES (?,?,?,?,?,?,?,?)"));
    q.addBindValue(type);
    q.addBindValue(project.isEmpty() ? QVariant() : QVariant(project));
    q.addBindValue(machine.isEmpty() ? QVariant() : QVariant(machine));
    q.addBindValue(static_cast<qlonglong>(startTs));
    q.addBindValue(static_cast<qlonglong>(endTs));
    q.addBindValue(static_cast<qlonglong>(endTs >= startTs ? endTs - startTs : 0));
    q.addBindValue(status.isEmpty() ? QVariant() : QVariant(status));
    q.addBindValue(metadata.isEmpty() ? QVariant()
                                      : QVariant(QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact))));
    if (!q.exec()) { m_lastError = q.lastError().text(); return -1; }
    return q.lastInsertId().toLongLong();
}

QJsonObject MonitorStore::windowStats(int machineId, qint64 from, qint64 to) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT AVG(cpu_percent), MAX(cpu_percent), AVG(temp_cpu), MAX(temp_cpu), "
        " AVG(mem_percent), MAX(mem_percent), MAX(load1), COUNT(*) "
        "FROM sample_machine WHERE machine_id=? AND ts>=? AND ts<=?"));
    q.addBindValue(machineId);
    q.addBindValue(static_cast<qlonglong>(from));
    q.addBindValue(static_cast<qlonglong>(to));
    if (!q.exec() || !q.next())
        return {};
    return QJsonObject{
        {"cpu_avg",  numOrNull(q.value(0))}, {"cpu_max",  numOrNull(q.value(1))},
        {"temp_avg", numOrNull(q.value(2))}, {"temp_max", numOrNull(q.value(3))},
        {"mem_avg",  numOrNull(q.value(4))}, {"mem_max",  numOrNull(q.value(5))},
        {"load_max", numOrNull(q.value(6))},
        {"samples",  static_cast<double>(q.value(7).toLongLong())},
    };
}

QJsonObject MonitorStore::buildStats(const QString& machine, qint64 from, qint64 to) const {
    QJsonArray projects;
    QSqlQuery q(m_db);
    // Filtre machine optionnel : chaîne vide => toutes machines.
    const QString machineClause = machine.isEmpty() ? QString()
                                                    : QStringLiteral(" AND machine=?");
    // Durées sur les builds RÉUSSIS uniquement (CASE => NULL sinon, ignoré par AVG/MIN/MAX).
    q.prepare(QStringLiteral(
        "SELECT project, COUNT(*), "
        " SUM(CASE WHEN status='success' THEN 1 ELSE 0 END), "
        " SUM(CASE WHEN status='failed'  THEN 1 ELSE 0 END), "
        " SUM(CASE WHEN status='success' THEN duration_s END), "
        " AVG(CASE WHEN status='success' THEN duration_s END), "
        " MIN(CASE WHEN status='success' THEN duration_s END), "
        " MAX(CASE WHEN status='success' THEN duration_s END) "
        "FROM activity WHERE type='compile' AND start_ts>=? AND start_ts<=?") + machineClause +
        QStringLiteral(" GROUP BY project ORDER BY SUM(CASE WHEN status='success' THEN duration_s END) DESC"));
    q.addBindValue(static_cast<qlonglong>(from));
    q.addBindValue(static_cast<qlonglong>(to));
    if (!machine.isEmpty()) q.addBindValue(machine);

    qint64 totalCount = 0, totalSucc = 0, totalFail = 0, totalDur = 0;
    if (q.exec()) {
        while (q.next()) {
            const qint64 c = q.value(1).toLongLong();
            const qint64 s = q.value(2).toLongLong();
            const qint64 f = q.value(3).toLongLong();
            totalCount += c; totalSucc += s; totalFail += f;
            totalDur += q.value(4).toLongLong();
            projects.append(QJsonObject{
                {"project", q.value(0).toString().isEmpty() ? QStringLiteral("(inconnu)") : q.value(0).toString()},
                {"count", static_cast<double>(c)},
                {"success", static_cast<double>(s)},
                {"failed", static_cast<double>(f)},
                {"total_duration_s", numOrNull(q.value(4))},
                {"avg_duration_s", numOrNull(q.value(5))},
                {"min_duration_s", numOrNull(q.value(6))},
                {"max_duration_s", numOrNull(q.value(7))},
            });
        }
    }
    return QJsonObject{
        {"projects", projects},
        {"total", QJsonObject{
            {"count", static_cast<double>(totalCount)},
            {"success", static_cast<double>(totalSucc)},
            {"failed", static_cast<double>(totalFail)},
            {"total_duration_s", static_cast<double>(totalDur)}}},
    };
}

QJsonArray MonitorStore::recentActivities(const QString& machine, qint64 from, qint64 to,
                                          int limit) const {
    QJsonArray arr;
    if (limit < 1) limit = 20;
    QSqlQuery q(m_db);
    const QString machineClause = machine.isEmpty() ? QString()
                                                    : QStringLiteral(" AND machine=?");
    q.prepare(QStringLiteral(
        "SELECT id, type, project, machine, start_ts, end_ts, duration_s, status, metadata "
        "FROM activity WHERE end_ts>=? AND start_ts<=?") + machineClause +
        QStringLiteral(" ORDER BY start_ts DESC LIMIT ?"));
    q.addBindValue(static_cast<qlonglong>(from));
    q.addBindValue(static_cast<qlonglong>(to));
    if (!machine.isEmpty()) q.addBindValue(machine);
    q.addBindValue(limit);
    if (!q.exec())
        return arr;
    while (q.next()) {
        const QString mkey = q.value(3).toString();
        const qint64 st = q.value(4).toLongLong();
        const qint64 en = q.value(5).toLongLong();
        QJsonObject a{
            {"id", static_cast<double>(q.value(0).toLongLong())},
            {"type", q.value(1).toString()},
            {"project", q.value(2).toString()},
            {"machine", mkey},
            {"start_ts", static_cast<double>(st)},
            {"end_ts", static_cast<double>(en)},
            {"duration_s", static_cast<double>(q.value(6).toLongLong())},
            {"status", q.value(7).toString()},
        };
        const QString meta = q.value(8).toString();
        if (!meta.isEmpty())
            a["metadata"] = QJsonDocument::fromJson(meta.toUtf8()).object();
        // Coût système réel de l'activité : stats sur sa fenêtre exacte.
        const int mid = machineIdForKey(mkey);
        if (mid >= 0)
            a["window"] = windowStats(mid, st, en);
        arr.append(a);
    }
    return arr;
}

} // namespace morfanalytics
