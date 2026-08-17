/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/publish/MeteoSyncPublisher.h"
#include "morfanalytics/data/SampleStore.h"
#include "morfanalytics/data/Series.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>

namespace morfanalytics {
namespace {

QString nowIso8601Utc() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}
QString tsToIso(qint64 ts) {
    // UTC sans avertissement ET sans casser les Qt plus anciens : la constante
    // QTimeZone::UTC (et la surcharge fromSecsSinceEpoch prenant un QTimeZone)
    // n'existe qu'a partir de Qt 6.5. Sous Qt 6.4 (Linux Mint), il faut la
    // surcharge historique Qt::UTC, qui n'y est pas encore depreciee. Un garde de
    // version choisit la bonne selon le Qt qui compile -- aucun avertissement
    // d'un cote, aucune erreur de l'autre.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QDateTime::fromSecsSinceEpoch(ts, QTimeZone::UTC).toString(Qt::ISODate);
#else
    return QDateTime::fromSecsSinceEpoch(ts, Qt::UTC).toString(Qt::ISODate);
#endif
}

// Synthese d'un canal sur une journee : min/max/moyenne + premiere/derniere
// valeur (pour la variation), en ignorant les trous (NaN). Renvoie un objet vide
// (n=0) si le canal n'a aucune valeur valide ce jour-la.
QJsonObject synthChannel(const QVector<double>* col) {
    QJsonObject o;
    if (!col) { o["n"] = 0; return o; }
    double mn = 0, mx = 0, sum = 0, first = 0, last = 0;
    int n = 0;
    for (double v : *col) {
        if (std::isnan(v)) continue;
        if (n == 0) { mn = mx = first = v; }
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
        last = v;
        ++n;
    }
    o["n"] = n;
    if (n > 0) {
        o["min"]   = mn;
        o["max"]   = mx;
        o["avg"]   = sum / n;
        o["first"] = first;
        o["last"]  = last;
    }
    return o;
}

// Base d'URL sans slash final.
QString trimSlash(QString u) {
    while (u.endsWith('/')) u.chop(1);
    return u;
}

} // namespace

MeteoSyncPublisher::MeteoSyncPublisher(SampleStore* store, Config cfg, QObject* parent)
    : QObject(parent), m_store(store), m_cfg(std::move(cfg)) {}

int MeteoSyncPublisher::publish() {
    if (!enabled() || !m_store)
        return 0;

    // Nombre d'echantillons par jour = revision de la synthese du jour. Un jour
    // dont le compte n'a pas bouge depuis le dernier envoi n'est pas republie.
    const QHash<quint32, quint32> perDay = m_store->importedPerDay();

    QJsonArray changes;
    QVector<QPair<quint32, quint32>> pushed; // (day_key, rev) a valider si l'envoi reussit
    const QString now = nowIso8601Utc();

    for (auto it = perDay.constBegin(); it != perDay.constEnd(); ++it) {
        const quint32 dayKey = it.key();
        const quint32 rev    = it.value();
        if (dayKey == 0 || rev == 0)
            continue;
        if (rev <= m_publishedRev.value(dayKey, 0))
            continue; // inchange depuis le dernier envoi reussi

        const Series s = m_store->rangeForDay(dayKey);
        const QVector<qint64>& ts = s.timestamps();

        QJsonObject data;
        data["source"]   = m_cfg.domain;
        data["day"]      = static_cast<double>(dayKey);
        data["count"]    = s.size();
        if (!ts.isEmpty()) {
            data["first_ts"] = static_cast<double>(ts.first());
            data["last_ts"]  = static_cast<double>(ts.last());
        }
        QJsonObject channels;
        for (const QString& ch : m_cfg.channels)
            channels[ch] = synthChannel(s.channel(ch));
        data["channels"] = channels;

        QJsonObject env;
        env["id"]        = QStringLiteral("meteohub-%1").arg(dayKey);
        env["type"]      = QStringLiteral("daily_synthesis");
        env["deleted"]   = false;
        env["rev"]       = static_cast<double>(rev);
        env["origin"]    = m_cfg.deviceId;
        env["createdAt"] = ts.isEmpty() ? now : tsToIso(ts.first());
        env["updatedAt"] = now;
        env["data"]      = data;
        changes.append(env);
        pushed.append(qMakePair(dayKey, rev));
    }

    if (changes.isEmpty())
        return 0;

    // POST /api/{domain}/changes -- envoi bloquant borne par un delai. Le service
    // a une boucle d'evenements ; une boucle imbriquee courte est acceptable pour
    // une publication periodique, et le timeout garantit qu'elle rend la main.
    QJsonObject root;
    root["deviceId"] = m_cfg.deviceId;
    root["changes"]  = changes;
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QNetworkAccessManager mgr;
    QNetworkRequest req(QUrl(trimSlash(m_cfg.baseUrl) + "/api/" + m_cfg.domain + "/changes"));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json; charset=utf-8"));
    if (!m_cfg.token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_cfg.token.toUtf8());

    QNetworkReply* reply = mgr.post(req, payload);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(m_cfg.timeoutMs > 0 ? m_cfg.timeoutMs : 4000);
    loop.exec();

    int published = 0;
    if (!reply->isFinished()) {
        reply->abort();
        m_lastError = QStringLiteral("delai depasse en joignant morfSync");
    } else if (reply->error() != QNetworkReply::NoError) {
        m_lastError = reply->errorString();
    } else {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            // Envoi accepte : on avance les reperes en memoire pour ne pas
            // republier ces jours inchanges au prochain cycle.
            for (const auto& p : pushed)
                m_publishedRev[p.first] = p.second;
            published        = pushed.size();
            m_lastPublished  = published;
            m_lastPublishTs  = QDateTime::currentSecsSinceEpoch();
            m_lastError.clear();
        } else {
            m_lastError = QStringLiteral("morfSync a repondu HTTP %1").arg(status);
        }
    }
    reply->deleteLater();
    return published;
}

QJsonObject MeteoSyncPublisher::statusJson() const {
    QJsonObject o;
    o["enabled"] = enabled();
    if (enabled()) {
        o["domain"]           = m_cfg.domain;
        o["days_tracked"]     = m_publishedRev.size();
        o["last_published"]   = m_lastPublished;
        o["last_publish_ts"]  = static_cast<double>(m_lastPublishTs);
        if (!m_lastError.isEmpty())
            o["last_error"] = m_lastError;
    }
    return o;
}

} // namespace morfanalytics
