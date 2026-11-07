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
#include <algorithm>
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

QVector<quint32> MeteoSyncPublisher::selectBatch(const QHash<quint32, quint32>& perDay,
                                                 const QHash<quint32, quint32>& published,
                                                 int maxDays) {
    // Jours a publier = ceux dont la revision a avance, en ordre CHRONOLOGIQUE (le
    // QHash n'a pas d'ordre) : les plus anciens partent d'abord, un rattrapage
    // complet progresse donc de facon previsible lot apres lot.
    QVector<quint32> due;
    for (auto it = perDay.constBegin(); it != perDay.constEnd(); ++it) {
        if (it.key() == 0 || it.value() == 0)
            continue;
        if (it.value() <= published.value(it.key(), 0))
            continue; // inchange depuis le dernier envoi reussi
        due.append(it.key());
    }
    std::sort(due.begin(), due.end());
    if (maxDays > 0 && due.size() > maxDays)
        due.resize(maxDays);
    return due;
}

void MeteoSyncPublisher::publish() {
    if (!enabled() || !m_store || m_inflight || m_stopped)
        return;

    // Nombre d'echantillons par jour = revision de la synthese du jour. Un jour
    // dont le compte n'a pas bouge depuis le dernier envoi n'est pas republie.
    const QHash<quint32, quint32> perDay = m_store->samplesPerDay();

    // PAR LOTS : apres un redemarrage m_publishedRev est vide et tout l'historique
    // serait du. Un seul POST geant (plusieurs centaines de jours) depassait le delai
    // et echouait en boucle, sans jamais rien valider. Un lot court passe, est valide,
    // et le suivant part aussitot (voir onReply). Pas de persistance du repere : la
    // publication est idempotente (meme id + meme rev = aucune ecriture cote morfSync).
    const QVector<quint32> days = selectBatch(perDay, m_publishedRev, kBatchDays);
    if (days.isEmpty())
        return;

    QJsonArray changes;
    m_batch.clear();
    const QString now = nowIso8601Utc();

    for (quint32 dayKey : days) {
        const quint32 rev = perDay.value(dayKey);
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
        m_batch.append(qMakePair(dayKey, rev));
    }

    // POST /api/{domain}/changes, ASYNCHRONE : plus de boucle d'evenements imbriquee
    // (elle gelait le serveur HTTP et le heartbeat jusqu'a 4 s par cycle).
    QJsonObject root;
    root["deviceId"] = m_cfg.deviceId;
    root["changes"]  = changes;
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);

    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(trimSlash(m_cfg.baseUrl) + "/api/" + m_cfg.domain + "/changes"));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json; charset=utf-8"));
    req.setTransferTimeout(m_cfg.timeoutMs > 0 ? m_cfg.timeoutMs : 4000);
    if (!m_cfg.token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_cfg.token.toUtf8());

    m_inflight = true;
    QNetworkReply* reply = m_net->post(req, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onReply(reply); });
}

void MeteoSyncPublisher::onReply(QNetworkReply* reply) {
    reply->deleteLater();
    m_inflight = false;
    bool more = false;
    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = reply->errorString();
    } else {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            // Lot accepte : valider UNIQUEMENT ses jours. Un echec ulterieur ne
            // remet donc jamais en cause ce qui est deja parti.
            for (const auto& p : m_batch)
                m_publishedRev[p.first] = p.second;
            m_lastPublished = m_batch.size();
            m_lastPublishTs = QDateTime::currentSecsSinceEpoch();
            m_lastError.clear();
            more = m_batch.size() >= kBatchDays;
        } else {
            m_lastError = QStringLiteral("morfSync a repondu HTTP %1").arg(status);
        }
    }
    m_batch.clear();
    // Lot plein = il reste probablement des jours : enchainer, via la boucle
    // d'evenements (pas de recursion). Un echec s'arrete la, le prochain cycle reprend.
    if (more && !m_stopped)
        QTimer::singleShot(0, this, [this] { publish(); });
}

void MeteoSyncPublisher::abort() {
    // Arret du module : plus de requete en vol, plus d'enchainement.
    m_stopped = true;
    if (m_net)
        for (QNetworkReply* r : m_net->findChildren<QNetworkReply*>())
            r->abort();
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
