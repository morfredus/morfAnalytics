/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/PhotoAnalyticsModule.h"

#include <QTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QDateTime>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace morfanalytics {

namespace {
// Endpoints d'agrégats de morfPhoto interrogés à chaque cycle. On ne rapatrie que
// des résumés (jamais la liste complète des fichiers) : morfPhoto reste la source,
// morfAnalytics n'en tire qu'une lecture synthétique.
struct Endpoint { const char* path; const char* key; };
const Endpoint kEndpoints[] = {
    {"/api/v1/photos/summary", "summary"},
    {"/api/v1/photos/years",   "years"},
    {"/api/v1/photos/cameras", "cameras"},
    {"/api/v1/photos/lenses",  "lenses"},
    {"/api/v1/photos/focals",  "focals"},
    // Export compact (colonnaire + dictionnaires) de toutes les photos presentes :
    // c'est LUI qui alimente l'interface d'exploration cote page (agregation en JS).
    {"/api/v1/photos/dataset", "dataset"},
};

QString nowIso() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }
} // namespace

PhotoAnalyticsModule::PhotoAnalyticsModule(const QString& id, QString sourceUrl,
                                           int refreshMs, QVector<FocalBucket> buckets,
                                           QStringList excludeCameras, QObject* parent)
    : IModule(id, QStringLiteral("photo"), parent),
      m_sourceUrl(std::move(sourceUrl)),
      m_refreshMs(refreshMs > 0 ? refreshMs : 60000),
      m_buckets(std::move(buckets)),
      m_excludeCameras(std::move(excludeCameras)) {
    if (m_buckets.isEmpty())
        m_buckets = defaultBuckets();
    // Instantané initial sûr : la page fonctionne avant tout pull.
    m_snapshot = QJsonObject{
        {"source_url", m_sourceUrl},
        {"reachable", false},
        {"fetched_at", QJsonValue(QJsonValue::Null)},
        {"last_error", QJsonValue(QJsonValue::Null)},
    };
}

PhotoAnalyticsModule::~PhotoAnalyticsModule() { stop(); }

bool PhotoAnalyticsModule::start() {
    m_net = new QNetworkAccessManager(this);
    m_timer = new QTimer(this);
    m_timer->setInterval(m_refreshMs);
    connect(m_timer, &QTimer::timeout, this, &PhotoAnalyticsModule::refresh);
    m_timer->start();
    refresh();   // premier pull immédiat
    return true;
}

void PhotoAnalyticsModule::stop() {
    if (m_timer) { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
    if (m_net)   { m_net->deleteLater(); m_net = nullptr; }
    m_pending = 0;
}

void PhotoAnalyticsModule::refresh() {
    // Pas de source configurée : publier un instantané explicite, ne rien tenter.
    if (m_sourceUrl.isEmpty()) {
        m_snapshot = QJsonObject{
            {"source_url", QString()},
            {"reachable", false},
            {"fetched_at", QJsonValue(QJsonValue::Null)},
            {"last_error", QStringLiteral("aucune source morfPhoto configuree (source_url vide)")},
        };
        return;
    }
    if (m_pending > 0 || !m_net)   // un cycle est déjà en vol : ne pas empiler
        return;

    m_partial = QJsonObject{};
    m_cycleError.clear();
    m_pending = static_cast<int>(std::size(kEndpoints));
    for (const Endpoint& e : kEndpoints)
        fetch(QString::fromLatin1(e.path), QString::fromLatin1(e.key));
}

void PhotoAnalyticsModule::fetch(const QString& path, const QString& key) {
    QNetworkRequest req{QUrl(m_sourceUrl + path)};
    req.setTransferTimeout(8000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, key, reply]() {
        onReply(key, reply);
    });
}

void PhotoAnalyticsModule::onReply(const QString& key, QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        if (m_cycleError.isEmpty())
            m_cycleError = reply->errorString();
    } else {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject())
            m_partial.insert(key, doc.object());
    }
    if (--m_pending <= 0)
        finalize();
}

void PhotoAnalyticsModule::finalize() {
    m_pending = 0;
    // Un cycle échoue si une seule réponse a manqué : on garde le dernier
    // instantané valide et on signale l'erreur, plutôt que de publier un état partiel.
    if (!m_cycleError.isEmpty()) {
        m_snapshot["reachable"] = false;
        m_snapshot["last_error"] = m_cycleError;
        m_snapshot["source_url"] = m_sourceUrl;
        return;
    }

    const auto items = [this](const char* key) {
        return m_partial.value(QLatin1String(key)).toObject()
                        .value(QStringLiteral("items")).toArray();
    };

    QJsonObject snap;
    snap["source_url"]     = m_sourceUrl;
    snap["reachable"]      = true;
    snap["fetched_at"]     = nowIso();
    snap["last_error"]     = QJsonValue(QJsonValue::Null);
    snap["summary"]        = m_partial.value(QStringLiteral("summary")).toObject();
    snap["years"]          = items("years");
    snap["cameras"]        = items("cameras");
    snap["lenses"]         = items("lenses");
    const QJsonArray rawFocals = items("focals");
    snap["focals_raw_count"] = rawFocals.size();
    // INTERPRÉTATION : regroupement des focales brutes en focales usuelles.
    snap["focals_grouped"] = groupFocals(rawFocals, m_buckets);

    // Jeu de données complet (colonnaire) : la matière première de l'exploration.
    // La page l'agrège et le filtre en JS ; morfAnalytics ne le stocke pas, il n'en
    // est qu'un relais depuis morfPhoto (source de vérité).
    snap["dataset"] = m_partial.value(QStringLiteral("dataset")).toObject();

    // Règles d'interprétation exposées à la page : elle réutilise le même
    // regroupement de focales configuré (jamais recodé dans le navigateur).
    QJsonArray buckets;
    for (const FocalBucket& b : m_buckets)
        buckets.append(QJsonObject{{"min", b.min}, {"max", b.max}, {"label", b.label}});
    snap["focal_buckets"] = buckets;

    // Périmètre de pratique côté service (corpus ≠ pratique) : boîtiers exclus par
    // politique. La page les fusionne avec les exclusions locales du navigateur.
    QJsonArray excl;
    for (const QString& c : m_excludeCameras)
        excl.append(c);
    snap["exclude_cameras"] = excl;

    m_snapshot = snap;
    emit updated(id());
}

QJsonArray PhotoAnalyticsModule::groupFocals(const QJsonArray& rawFocals,
                                             const QVector<FocalBucket>& buckets) {
    // Trier les PLAGES par focale representative (min) : la sortie sort alors
    // naturellement ordonnee, sans trier le QJsonArray (QJsonValueRef n'est pas
    // swappable, std::sort ne s'y applique pas).
    QVector<FocalBucket> ordered = buckets;
    std::sort(ordered.begin(), ordered.end(),
              [](const FocalBucket& a, const FocalBucket& b) { return a.min < b.min; });

    // Accumuler les comptes par plage ; « autres » hors de toute plage.
    QVector<int> counts(ordered.size(), 0);
    int other = 0;
    for (const QJsonValue& v : rawFocals) {
        const QJsonObject o = v.toObject();
        const double f = o.value(QStringLiteral("focal_length")).toDouble();
        const int c = o.value(QStringLiteral("count")).toInt();
        int idx = -1;
        for (int i = 0; i < ordered.size(); ++i)
            if (f >= ordered[i].min && f <= ordered[i].max) { idx = i; break; }
        if (idx >= 0) counts[idx] += c; else other += c;
    }
    QJsonArray out;
    for (int i = 0; i < ordered.size(); ++i)
        if (counts[i] > 0)
            out.append(QJsonObject{{"label", ordered[i].label}, {"count", counts[i]},
                                   {"min", ordered[i].min}, {"max", ordered[i].max}});
    if (other > 0)
        out.append(QJsonObject{{"label", QStringLiteral("autres")}, {"count", other}});
    return out;
}

QVector<PhotoAnalyticsModule::FocalBucket> PhotoAnalyticsModule::defaultBuckets() {
    // Focales usuelles du 24x36. Configurable via "focal_buckets" ; ces valeurs
    // ne sont qu'un point de depart raisonnable, jamais imposees a la donnee brute.
    return {
        {14, 15, QStringLiteral("14 mm")},   {15.5, 17, QStringLiteral("16 mm")},
        {17.5, 19, QStringLiteral("18 mm")}, {19, 22, QStringLiteral("20 mm")},
        {23, 25, QStringLiteral("24 mm")},   {26, 30, QStringLiteral("28 mm")},
        {33, 37, QStringLiteral("35 mm")},   {38, 42, QStringLiteral("40 mm")},
        {47, 53, QStringLiteral("50 mm")},   {58, 62, QStringLiteral("60 mm")},
        {66, 72, QStringLiteral("70 mm")},   {83, 90, QStringLiteral("85 mm")},
        {98, 105, QStringLiteral("100 mm")}, {130, 140, QStringLiteral("135 mm")},
        {195, 210, QStringLiteral("200 mm")},{290, 310, QStringLiteral("300 mm")},
    };
}

QJsonObject PhotoAnalyticsModule::fetchNow(const QString& sourceUrl) const {
    // Interprétation identique quelle que soit la source (regroupement de focales,
    // périmètre de pratique) : la page les réutilise telles quelles.
    QJsonArray buckets;
    for (const FocalBucket& b : m_buckets)
        buckets.append(QJsonObject{{"min", b.min}, {"max", b.max}, {"label", b.label}});
    QJsonArray excl;
    for (const QString& c : m_excludeCameras)
        excl.append(c);

    QJsonObject snap{
        {"source_url", sourceUrl}, {"reachable", false},
        {"fetched_at", QJsonValue(QJsonValue::Null)}, {"last_error", QJsonValue(QJsonValue::Null)},
        {"focal_buckets", buckets}, {"exclude_cameras", excl},
    };
    if (sourceUrl.isEmpty()) {
        snap["last_error"] = QStringLiteral("source vide");
        return snap;
    }

    // Fetch synchrone borné : boucle d'événements imbriquée quittée par la fin de la
    // requête OU un délai de garde. Acceptable pour une action ponctuelle (le handoff
    // n'est pas la voie chaude périodique) ; la source périodique du module n'est pas
    // touchée. On ne bloque jamais indéfiniment sur une source muette.
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(sourceUrl + QStringLiteral("/api/v1/photos/dataset"))};
    req.setTransferTimeout(8000);
    QEventLoop loop;
    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(10000);
    loop.exec();

    if (reply->isRunning())
        reply->abort();
    if (reply->error() != QNetworkReply::NoError) {
        snap["last_error"] = reply->errorString();
        reply->deleteLater();
        return snap;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    snap["dataset"]    = doc.object();
    snap["reachable"]  = true;
    snap["fetched_at"] = nowIso();
    return snap;
}

QJsonObject PhotoAnalyticsModule::statusJson() const {
    const QJsonObject summary = m_snapshot.value(QStringLiteral("summary")).toObject();
    return QJsonObject{
        {"source_url", m_sourceUrl},
        {"reachable", m_snapshot.value(QStringLiteral("reachable"))},
        {"fetched_at", m_snapshot.value(QStringLiteral("fetched_at"))},
        {"last_error", m_snapshot.value(QStringLiteral("last_error"))},
        {"files_present", summary.value(QStringLiteral("files_present"))},
    };
}

} // namespace morfanalytics
