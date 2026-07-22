/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/collect/MeteoHubCollector.h"
#include "morfanalytics/data/SampleStore.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>
#include <limits>

namespace morfanalytics {

namespace {
// Taille d'un lot, alignee sur la borne acceptee par MeteoHub. Mesure sur
// l'appareil : au-dela d'environ 300 enregistrements, le flux de reponse
// asynchrone de l'ESP32 abandonne et la requete se termine sans rien renvoyer.
// Une journee complete (1440 mesures) demande donc six requetes ; c'est le prix
// a payer pour ne jamais dependre d'une taille de reponse qu'il ne tient pas.
constexpr int kChunkLimit = 250;

// Au-dela, on considere l'ESP32 injoignable. Genereux a dessein : il lit la
// carte SD (plusieurs secondes par requete) tout en servant son interface web.
constexpr int kTimeoutMs = 45000;

const char* kSource = "meteohub";

// Bornes de plausibilite physique. Un BME280 en defaut renvoie des zeros : un
// 0 hPa est impossible sur Terre, alors qu'un 0 °C est un vrai releve d'hiver.
// C'est donc la PRESSION qui sert de sentinelle de panne : hors bornes, tout le
// releve est suspect et la ligne entiere est marquee manquante. Les autres
// grandeurs ne sont filtrees qu'individuellement.
constexpr double kPresMin = 300.0,  kPresMax = 1200.0; // hPa (records terrestres inclus)
constexpr double kTempMin = -60.0,  kTempMax = 60.0;   // °C
constexpr double kHumMin  = 0.0,    kHumMax  = 100.0;  // %

double plausible(double v, double lo, double hi) {
    return (v < lo || v > hi) ? std::numeric_limits<double>::quiet_NaN() : v;
}
} // namespace

MeteoHubCollector::MeteoHubCollector(QString baseUrl, SampleStore* store, QObject* parent)
    : QObject(parent), m_baseUrl(std::move(baseUrl)), m_store(store),
      m_net(new QNetworkAccessManager(this)) {
    while (m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl.chop(1);
}

void MeteoHubCollector::sync() {
    if (m_running)
        return; // cycle deja en cours : le timer de maintenance ne doit rien empiler
    if (!m_store || !m_store->isOpen()) {
        m_lastError = QStringLiteral("cache indisponible");
        return;
    }
    m_running  = true;
    m_imported = 0;
    m_lastError.clear();
    m_pending.clear();
    requestDays();
}

void MeteoHubCollector::requestDays() {
    QNetworkRequest req{QUrl(m_baseUrl + QStringLiteral("/api/history/days"))};
    req.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onDaysReply(reply); });
}

void MeteoHubCollector::onDaysReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        // MeteoHub eteint ou hors de portee : ce n'est pas une anomalie. On
        // reprendra au prochain cycle, exactement la ou on s'est arrete.
        finish(reply->errorString());
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonArray days  = root.value(QStringLiteral("days")).toArray();

    const QHash<quint32, quint32> imported = m_store->importedPerDay();

    for (const QJsonValue& v : days) {
        const QJsonObject d = v.toObject();
        const auto dayKey = static_cast<quint32>(d.value(QStringLiteral("day")).toDouble());
        const auto nrec   = static_cast<quint32>(d.value(QStringLiteral("nrec")).toDouble());
        const quint32 have = imported.value(dayKey, 0);

        // Le seul cas ou l'on telecharge quoi que ce soit : la source annonce
        // plus de mesures que le cache n'en detient pour ce jour.
        if (nrec > have)
            m_pending.enqueue(qMakePair(dayKey, have));
    }

    requestNextChunk();
}

void MeteoHubCollector::requestNextChunk() {
    if (m_pending.isEmpty()) {
        finish(QString());
        return;
    }

    const QPair<quint32, quint32> next = m_pending.dequeue();
    m_currentDay   = next.first;
    m_currentIndex = next.second;

    QUrl url(m_baseUrl + QStringLiteral("/api/history/raw"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("day"), QString::number(m_currentDay));
    query.addQueryItem(QStringLiteral("index"), QString::number(m_currentIndex));
    query.addQueryItem(QStringLiteral("limit"), QString::number(kChunkLimit));
    url.setQuery(query);

    QNetworkRequest req{url};
    req.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onChunkReply(reply); });
}

void MeteoHubCollector::onChunkReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        finish(reply->errorString());
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonArray data  = root.value(QStringLiteral("data")).toArray();
    const auto total = static_cast<quint32>(root.value(QStringLiteral("total")).toDouble());

    QVector<qint64> timestamps;
    QVector<QHash<QString, double>> values;
    timestamps.reserve(data.size());
    values.reserve(data.size());

    for (const QJsonValue& v : data) {
        // Format compact emis par MeteoHub : [ts, temperature, humidite, pression].
        const QJsonArray row = v.toArray();
        if (row.size() < 4)
            continue;
        timestamps.push_back(static_cast<qint64>(row.at(0).toDouble()));

        // Filtre de plausibilite AVANT insertion : une valeur physiquement
        // impossible devient "manquante" au lieu de polluer les analyses. On
        // stocke tout de meme la ligne (avec des NULL) pour que la position de
        // reprise reste exacte — la ligne existe bien sur l'appareil.
        double temp = plausible(row.at(1).toDouble(), kTempMin, kTempMax);
        double hum  = plausible(row.at(2).toDouble(), kHumMin,  kHumMax);
        const double pres = plausible(row.at(3).toDouble(), kPresMin, kPresMax);
        if (std::isnan(pres)) {
            // Pression impossible = capteur hors service au moment du releve :
            // le 0 °C et le 0 % qui l'accompagnent ne sont pas des mesures,
            // toute la ligne est marquee manquante.
            temp = std::numeric_limits<double>::quiet_NaN();
            hum  = std::numeric_limits<double>::quiet_NaN();
        }

        QHash<QString, double> channels;
        channels.insert(QStringLiteral("temp"), temp);
        channels.insert(QStringLiteral("hum"),  hum);
        channels.insert(QStringLiteral("pres"), pres);
        values.push_back(channels);
    }

    if (!timestamps.isEmpty()) {
        if (!m_store->insertBatch(m_currentDay, m_currentIndex, timestamps, values)) {
            finish(m_store->lastError());
            return;
        }
        m_imported += timestamps.size();

        const quint32 nextIndex = m_currentIndex + static_cast<quint32>(timestamps.size());
        m_store->setCursor(QLatin1String(kSource), Cursor{m_currentDay, nextIndex});

        // La journee depasse la taille d'un lot : on remet la suite dans la file
        // plutot que de boucler ici, pour ne pas monopoliser l'ESP32 ni la boucle
        // d'evenements pendant un rattrapage de plusieurs mois.
        if (nextIndex < total)
            m_pending.enqueue(qMakePair(m_currentDay, nextIndex));
    }

    requestNextChunk();
}

void MeteoHubCollector::finish(const QString& error) {
    m_lastError    = error;
    m_running      = false;
    m_pending.clear();
    m_lastSyncTs   = QDateTime::currentSecsSinceEpoch();
    m_lastImported = m_imported;
    emit finished(m_imported);
}

QJsonObject MeteoHubCollector::statusJson() const {
    QJsonObject o;
    o["source"]        = m_baseUrl;
    o["running"]       = m_running;
    o["last_sync_ts"]  = static_cast<double>(m_lastSyncTs);
    o["last_imported"] = m_lastImported;
    o["ok"]            = m_lastError.isEmpty();
    if (!m_lastError.isEmpty())
        o["error"] = m_lastError;
    if (m_store && m_store->isOpen()) {
        o["cached_points"] = static_cast<double>(m_store->count());
        qint64 first = 0, last = 0;
        if (m_store->bounds(first, last)) {
            o["first_ts"] = static_cast<double>(first);
            o["last_ts"]  = static_cast<double>(last);
        }
    }
    return o;
}

} // namespace morfanalytics
