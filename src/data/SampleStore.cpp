/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/data/SampleStore.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QDir>
#include <QUuid>

namespace morfanalytics {

namespace {
// Les noms de canaux viennent de la configuration : on ne les interpole jamais
// tels quels dans du SQL. Seuls [a-z0-9_] sont conserves, ce qui donne un nom de
// colonne sur et stable.
QString sanitize(const QString& name) {
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            out.append(c.toLower());
    }
    return out.isEmpty() ? QStringLiteral("c") : out;
}
} // namespace

SampleStore::SampleStore(QString dbPath, QStringList channels)
    : m_dbPath(std::move(dbPath)), m_channels(std::move(channels)) {
    // Chaque instance a sa propre connexion nommee : Qt interdit de partager une
    // QSqlDatabase entre threads, et le collecteur peut vivre ailleurs que les analyses.
    m_connectionName = QStringLiteral("morfanalytics-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

SampleStore::~SampleStore() {
    close();
}

QString SampleStore::column(const QString& channel) const {
    return QStringLiteral("ch_") + sanitize(channel);
}

bool SampleStore::open() {
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
    // WAL : les analyses lisent pendant que le collecteur ecrit, sans se bloquer.
    // NORMAL : on accepte de perdre les toutes dernieres insertions en cas de
    // coupure brutale — le cache est reconstructible depuis la source, la
    // durabilite stricte ne vaut pas le cout en ecritures sur carte SD.
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    QStringList columns;
    for (const QString& ch : m_channels)
        columns << (column(ch) + QStringLiteral(" REAL"));

    const QString createSample =
        QStringLiteral("CREATE TABLE IF NOT EXISTS sample ("
                       "day_key INTEGER NOT NULL,"
                       "idx     INTEGER NOT NULL,"
                       "ts      INTEGER NOT NULL,"
                       "%1,"
                       "PRIMARY KEY (day_key, idx))")
            .arg(columns.join(QStringLiteral(",")));

    if (!q.exec(createSample)) {
        m_lastError = q.lastError().text();
        return false;
    }
    // Les analyses interrogent presque toujours par plage temporelle.
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sample_ts ON sample(ts)"));

    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS sync_cursor ("
                               "source TEXT PRIMARY KEY,"
                               "day_key INTEGER NOT NULL,"
                               "idx INTEGER NOT NULL,"
                               "updated_at INTEGER NOT NULL)"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

void SampleStore::close() {
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

bool SampleStore::isOpen() const {
    return m_db.isOpen();
}

bool SampleStore::insertBatch(quint32 dayKey, quint32 firstIndex,
                              const QVector<qint64>& timestamps,
                              const QVector<QHash<QString, double>>& values) {
    if (timestamps.isEmpty())
        return true;
    if (timestamps.size() != values.size()) {
        m_lastError = QStringLiteral("insertBatch : tailles incoherentes");
        return false;
    }

    QStringList cols{QStringLiteral("day_key"), QStringLiteral("idx"), QStringLiteral("ts")};
    QStringList placeholders{QStringLiteral("?"), QStringLiteral("?"), QStringLiteral("?")};
    for (const QString& ch : m_channels) {
        cols << column(ch);
        placeholders << QStringLiteral("?");
    }

    // OR IGNORE : reimporter une plage deja connue ne cree pas de doublon et
    // n'est pas une erreur (cf. idempotence documentee dans l'en-tete).
    const QString sql = QStringLiteral("INSERT OR IGNORE INTO sample (%1) VALUES (%2)")
                            .arg(cols.join(QStringLiteral(",")),
                                 placeholders.join(QStringLiteral(",")));

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    QSqlQuery q(m_db);
    if (!q.prepare(sql)) {
        m_lastError = q.lastError().text();
        m_db.rollback();
        return false;
    }

    for (int i = 0; i < timestamps.size(); ++i) {
        q.addBindValue(dayKey);
        q.addBindValue(firstIndex + static_cast<quint32>(i));
        q.addBindValue(static_cast<qlonglong>(timestamps[i]));
        for (const QString& ch : m_channels) {
            auto it = values[i].constFind(ch);
            // Un canal absent ou NaN devient NULL en base : les analyses le
            // reliront comme "manquant" plutot que comme une valeur nulle.
            if (it == values[i].constEnd() || std::isnan(it.value()))
                q.addBindValue(QVariant(QMetaType(QMetaType::Double)));
            else
                q.addBindValue(it.value());
        }
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

QHash<quint32, quint32> SampleStore::importedPerDay() const {
    QHash<quint32, quint32> out;
    QSqlQuery q(m_db);
    // MAX(idx)+1 et non COUNT(*) : si un lot a ete interrompu et qu'un trou
    // subsiste au milieu d'une journee, reprendre a COUNT(*) sauterait
    // definitivement les enregistrements manquants. MAX(idx)+1 garantit qu'on
    // redemande tout ce qui suit ; les doublons sont ignores a l'insertion.
    if (q.exec(QStringLiteral("SELECT day_key, MAX(idx) + 1 FROM sample GROUP BY day_key"))) {
        while (q.next())
            out.insert(q.value(0).toUInt(), q.value(1).toUInt());
    }
    return out;
}

Cursor SampleStore::cursor(const QString& source) const {
    Cursor c;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT day_key, idx FROM sync_cursor WHERE source = ?"));
    q.addBindValue(source);
    if (q.exec() && q.next()) {
        c.dayKey = q.value(0).toUInt();
        c.index  = q.value(1).toUInt();
    }
    return c;
}

bool SampleStore::setCursor(const QString& source, const Cursor& c) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO sync_cursor (source, day_key, idx, updated_at) "
                             "VALUES (?, ?, ?, strftime('%s','now')) "
                             "ON CONFLICT(source) DO UPDATE SET "
                             "day_key = excluded.day_key, idx = excluded.idx, "
                             "updated_at = excluded.updated_at"));
    q.addBindValue(source);
    q.addBindValue(c.dayKey);
    q.addBindValue(c.index);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

qint64 SampleStore::invalidateChannels(qint64 fromTs, qint64 toTs,
                                       const QStringList& channels, bool dryRun) {
    QStringList cols;
    for (const QString& ch : channels) {
        // Seuls les canaux connus du store sont acceptes : un nom inconnu vient
        // d'une requete mal formee, pas d'une intention de nettoyage.
        if (!m_channels.contains(ch)) {
            m_lastError = QStringLiteral("canal inconnu : %1").arg(ch);
            return -1;
        }
        cols << column(ch);
    }
    if (cols.isEmpty()) {
        m_lastError = QStringLiteral("aucun canal demandé");
        return -1;
    }

    // La clause NOT NULL rend le decompte significatif : on ne compte que les
    // lignes ou quelque chose a reellement ete neutralise.
    QStringList notNull;
    for (const QString& c : cols)
        notNull << (c + QStringLiteral(" IS NOT NULL"));
    const QString where = QStringLiteral("ts BETWEEN ? AND ? AND (%1)")
                              .arg(notNull.join(QStringLiteral(" OR ")));

    QSqlQuery q(m_db);
    if (dryRun) {
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM sample WHERE %1").arg(where));
    } else {
        QStringList sets;
        for (const QString& c : cols)
            sets << (c + QStringLiteral(" = NULL"));
        q.prepare(QStringLiteral("UPDATE sample SET %1 WHERE %2")
                      .arg(sets.join(QStringLiteral(",")), where));
    }
    q.addBindValue(static_cast<qlonglong>(fromTs));
    q.addBindValue(static_cast<qlonglong>(toTs));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    if (dryRun)
        return q.next() ? q.value(0).toLongLong() : 0;
    return q.numRowsAffected();
}

qint64 SampleStore::invalidateOutliers(const QString& channel, double lo, double hi,
                                       bool dryRun) {
    if (!m_channels.contains(channel)) {
        m_lastError = QStringLiteral("canal inconnu : %1").arg(channel);
        return -1;
    }
    const QString ref = column(channel);
    const QString where = QStringLiteral("%1 IS NOT NULL AND (%1 < ? OR %1 > ?)").arg(ref);

    QSqlQuery q(m_db);
    if (dryRun) {
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM sample WHERE %1").arg(where));
    } else {
        QStringList sets;
        for (const QString& ch : m_channels)
            sets << (column(ch) + QStringLiteral(" = NULL"));
        q.prepare(QStringLiteral("UPDATE sample SET %1 WHERE %2")
                      .arg(sets.join(QStringLiteral(",")), where));
    }
    q.addBindValue(lo);
    q.addBindValue(hi);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    if (dryRun)
        return q.next() ? q.value(0).toLongLong() : 0;
    return q.numRowsAffected();
}

bool SampleStore::purgeAll() {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM sample"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral("DELETE FROM sync_cursor"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    // VACUUM rend l'espace au systeme de fichiers : une purge sert souvent a
    // repartir d'un cache leger, autant que le fichier suive.
    q.exec(QStringLiteral("VACUUM"));
    return true;
}

Series SampleStore::range(qint64 fromTs, qint64 toTs) const {
    Series series(m_channels);

    QStringList cols;
    for (const QString& ch : m_channels)
        cols << column(ch);

    QSqlQuery q(m_db);
    // Tri par ts : l'ordre (day_key, idx) suit l'ordre d'ecriture, mais un
    // recalage d'horloge sur la source peut le desynchroniser de l'ordre
    // chronologique. Les analyses, elles, exigent un axe de temps croissant.
    q.prepare(QStringLiteral("SELECT ts, %1 FROM sample "
                             "WHERE ts BETWEEN ? AND ? ORDER BY ts ASC")
                  .arg(cols.join(QStringLiteral(","))));
    q.addBindValue(static_cast<qlonglong>(fromTs));
    q.addBindValue(static_cast<qlonglong>(toTs));
    if (!q.exec())
        return series;

    while (q.next()) {
        QHash<QString, double> values;
        for (int i = 0; i < m_channels.size(); ++i) {
            const QVariant v = q.value(i + 1);
            values.insert(m_channels[i], v.isNull() ? Series::missing() : v.toDouble());
        }
        series.append(q.value(0).toLongLong(), values);
    }
    return series;
}

qint64 SampleStore::count() const {
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM sample")) && q.next())
        return q.value(0).toLongLong();
    return 0;
}

bool SampleStore::bounds(qint64& firstTs, qint64& lastTs) const {
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT MIN(ts), MAX(ts) FROM sample")) && q.next()
        && !q.value(0).isNull()) {
        firstTs = q.value(0).toLongLong();
        lastTs  = q.value(1).toLongLong();
        return true;
    }
    return false;
}

} // namespace morfanalytics
