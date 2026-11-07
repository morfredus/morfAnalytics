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
#include <QJsonParseError>
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

// Une cellule JSON devient NaN (donc NULL en base) si elle est null, non numerique ou non
// finie. Sans cela, toDouble() transformerait le null de MeteoHub >= 1.62.0 en 0.0, que
// l'humidite accepterait comme une vraie mesure.
double cell(const QJsonValue& v) {
    const double d = v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
    return std::isfinite(d) ? d : std::numeric_limits<double>::quiet_NaN();
}
} // namespace

QHash<QString, double> MeteoHubCollector::channelsFromRow(const QJsonArray& row) {
    // Filtre de plausibilite AVANT insertion : une valeur physiquement
    // impossible devient "manquante" au lieu de polluer les analyses.
    double temp = plausible(cell(row.at(1)), kTempMin, kTempMax);
    double hum  = plausible(cell(row.at(2)), kHumMin,  kHumMax);
    // 0 % exact : sentinelle des anciens hubs (avant le null de 1.62.0), jamais une vraie mesure.
    if (hum == 0.0)
        hum = std::numeric_limits<double>::quiet_NaN();
    const double pres = plausible(cell(row.at(3)), kPresMin, kPresMax);
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
    return channels;
}

MeteoHubCollector::MeteoHubCollector(QString baseUrl, SampleStore* store,
                                     QString ctx, QObject* parent)
    : QObject(parent), m_baseUrl(std::move(baseUrl)), m_ctx(std::move(ctx)), m_store(store),
      m_net(new QNetworkAccessManager(this)) {
    while (m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl.chop(1);
    if (m_ctx.isEmpty())
        m_ctx = QStringLiteral("in");
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
    QUrl daysUrl(m_baseUrl + QStringLiteral("/api/history/days"));
    QUrlQuery daysQuery;
    daysQuery.addQueryItem(QStringLiteral("ctx"), m_ctx);
    daysUrl.setQuery(daysQuery);
    QNetworkRequest req{daysUrl};
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

    // Reponse non-JSON ou sans la cle attendue : ce n'est pas un jour vide, c'est une
    // reponse invalide (un 200 d'un proxy ou d'un firmware en panne). On s'arrete
    // sans rien avancer.
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
    if (!m_store->isOpen() || pe.error != QJsonParseError::NoError || !doc.isObject()
        || !doc.object().value(QStringLiteral("days")).isArray()) {
        finish(QStringLiteral("reponse invalide"));
        return;
    }
    const QJsonArray days = doc.object().value(QStringLiteral("days")).toArray();

    for (const QJsonValue& v : days) {
        const QJsonObject d = v.toObject();
        const auto dayKey  = static_cast<quint32>(d.value(QStringLiteral("day")).toDouble());
        const auto nrec    = static_cast<quint32>(d.value(QStringLiteral("nrec")).toDouble());
        const auto firstTs = static_cast<qint64>(d.value(QStringLiteral("first_ts")).toDouble());

        // Quel FICHIER la source sert-elle pour ce jour ? Son identifiant est
        // l'horodatage de son enregistrement 0 (first_ts). S'il differe de celui
        // du cache, la source est repartie d'un fichier neuf (carte SD ou hub
        // remplace en cours de journee) : ses positions ne prolongent PAS celles
        // du cache. Avant 0.59.0, la comparaison des seuls nombres de mesures
        // sautait alors le debut du nouveau fichier (constate le 2026-09-27 :
        // 02:35 -> 03:08 absents apres l'echange des hubs).
        DaySource src = m_store->daySource(dayKey);
        if (!src.known || src.firstTs == 0) {
            // Journee neuve, ou cache migre sans index 0 : on adopte l'identifiant.
            if (firstTs != 0)
                m_store->setDaySource(dayKey, src.gen, firstTs);
        } else if (firstTs != 0 && firstTs != src.firstTs) {
            const qint64 gen = m_store->startNewGeneration(dayKey, firstTs);
            if (gen < 0) {
                finish(m_store->lastError());
                return;
            }
            src.gen = static_cast<quint32>(gen);
            m_generationSwitches++;
        }
        const quint32 have = m_store->resumeIndex(dayKey, src.gen);

        // Le seul cas ou l'on telecharge quoi que ce soit : la source annonce
        // plus de mesures que le cache n'en detient pour ce fichier.
        if (nrec > have)
            m_pending.enqueue(Chunk{dayKey, src.gen, have});
    }

    requestNextChunk();
}

void MeteoHubCollector::requestNextChunk() {
    if (m_pending.isEmpty()) {
        finish(QString());
        return;
    }

    const Chunk next = m_pending.dequeue();
    m_currentDay   = next.day;
    m_currentGen   = next.gen;
    m_currentIndex = next.index;

    QUrl url(m_baseUrl + QStringLiteral("/api/history/raw"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("day"), QString::number(m_currentDay));
    query.addQueryItem(QStringLiteral("index"), QString::number(m_currentIndex));
    query.addQueryItem(QStringLiteral("limit"), QString::number(kChunkLimit));
    query.addQueryItem(QStringLiteral("ctx"), m_ctx);
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

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
    if (!m_store->isOpen() || pe.error != QJsonParseError::NoError || !doc.isObject()
        || !doc.object().value(QStringLiteral("data")).isArray()
        || !doc.object().value(QStringLiteral("total")).isDouble()) {
        finish(QStringLiteral("reponse invalide"));   // le curseur n'avance pas
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray data  = root.value(QStringLiteral("data")).toArray();
    const auto total = static_cast<quint32>(root.value(QStringLiteral("total")).toDouble());

    QVector<qint64> timestamps;
    QVector<QHash<QString, double>> values;
    QVector<quint32> flags;
    timestamps.reserve(data.size());
    values.reserve(data.size());

    for (const QJsonValue& v : data) {
        // Format compact emis par MeteoHub : [ts, temperature, humidite, pression],
        // plus un 5e element, les MARQUES, quand le hub en a pose (1.54.0 :
        // demarrage a froid non conforme). Le hub a juge la mesure ; on garde son
        // verdict pour que la qualification l'ecarte et permette de la reintegrer.
        const QJsonArray row = v.toArray();
        if (row.size() < 4)
            continue;
        timestamps.push_back(static_cast<qint64>(row.at(0).toDouble()));
        flags.push_back(row.size() >= 5 ? static_cast<quint32>(row.at(4).toDouble()) : 0u);

        // La ligne est stockee meme avec des NULL : la position de reprise reste
        // exacte, la ligne existe bien sur l'appareil.
        const QHash<QString, double> channels = channelsFromRow(row);
        values.push_back(channels);
    }

    if (!timestamps.isEmpty()) {
        if (!m_store->insertBatch(m_currentDay, m_currentGen, m_currentIndex, timestamps,
                                  values, flags)) {
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
            m_pending.enqueue(Chunk{m_currentDay, m_currentGen, nextIndex});
    }

    requestNextChunk();
}

void MeteoHubCollector::abort() {
    // Appele par stop() AVANT la fermeture des bases : une reponse tardive ne doit
    // jamais ecrire dans un store ferme. abort() termine la reponse en erreur.
    for (QNetworkReply* r : m_net->findChildren<QNetworkReply*>())
        r->abort();
}

void MeteoHubCollector::finish(const QString& error) {
    m_lastError    = error;
    m_running      = false;
    m_pending.clear();
    // Date de la derniere synchro REUSSIE (un echec ne doit pas faire croire a un cache a jour).
    if (error.isEmpty())
        m_lastSyncTs = QDateTime::currentSecsSinceEpoch();
    m_lastImported = m_imported;
    emit finished(m_imported);
}

QJsonObject MeteoHubCollector::statusJson() const {
    QJsonObject o;
    o["source"]        = m_baseUrl;
    o["ctx"]           = m_ctx;
    o["running"]       = m_running;
    o["last_sync_ts"]  = static_cast<double>(m_lastSyncTs);
    o["last_imported"] = m_lastImported;
    // Journees dont la source a change de fichier depuis le demarrage (voir
    // onDaysReply) : un compteur non nul trace un echange de carte ou de hub.
    o["generation_switches"] = m_generationSwitches;
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
