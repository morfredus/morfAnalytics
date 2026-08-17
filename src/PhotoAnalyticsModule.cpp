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
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QUrl>
#include <QSet>

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
                                           QStringList excludeCameras, quint16 discoveryUdpPort,
                                           bool discoveryEnabled, QObject* parent)
    : IModule(id, QStringLiteral("photo"), parent),
      m_sourceUrl(std::move(sourceUrl)),
      m_refreshMs(refreshMs > 0 ? refreshMs : 60000),
      m_buckets(std::move(buckets)),
      m_excludeCameras(std::move(excludeCameras)),
      m_discoveryPort(discoveryUdpPort),
      m_discoveryEnabled(discoveryEnabled) {
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

    // Écoute du beacon : découverte des morfPhoto du parc (capacité photo_index),
    // exactement comme le domaine Monitor découvre les morfMonitor. ShareAddress car
    // morfAnalytics émet déjà son propre heartbeat sur ce port. Un échec de bind ne
    // fait pas tomber le module : la source configurée reste utilisable.
    if (m_discoveryEnabled) {
        m_beacon = new QUdpSocket(this);
        if (m_beacon->bind(QHostAddress::AnyIPv4, m_discoveryPort,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            connect(m_beacon, &QUdpSocket::readyRead, this, &PhotoAnalyticsModule::onBeaconDatagram);
        } else {
            m_beacon->deleteLater();
            m_beacon = nullptr;
        }
    }

    m_timer = new QTimer(this);
    m_timer->setInterval(m_refreshMs);
    connect(m_timer, &QTimer::timeout, this, &PhotoAnalyticsModule::refresh);
    m_timer->start();
    refresh();   // premier pull immédiat
    return true;
}

void PhotoAnalyticsModule::stop() {
    if (m_timer)  { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
    if (m_beacon) { m_beacon->close(); m_beacon->deleteLater(); m_beacon = nullptr; }
    if (m_net)    { m_net->deleteLater(); m_net = nullptr; }
    m_pending = 0;
}

void PhotoAnalyticsModule::onBeaconDatagram() {
    if (!m_beacon)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    while (m_beacon->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_beacon->receiveDatagram();
        const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        if (o.isEmpty())
            continue;
        // Découverte par CAPACITÉ (photo_index), jamais par nom.
        bool isPhoto = false;
        for (const QJsonValue& c : o.value(QStringLiteral("capabilities")).toArray())
            if (c.toString() == QLatin1String("photo_index")) { isPhoto = true; break; }
        if (!isPhoto)
            continue;
        const int statusPort = o.value(QStringLiteral("status_port")).toInt();
        if (statusPort <= 0)
            continue;
        QString ip = dg.senderAddress().toString();
        if (ip.startsWith(QLatin1String("::ffff:")))
            ip = ip.mid(ip.lastIndexOf(QLatin1Char(':')) + 1);
        if (ip.isEmpty())
            continue;
        m_discovered.insert(QStringLiteral("http://%1:%2").arg(ip).arg(statusPort), now);
    }
    pruneDiscovered(now);
}

void PhotoAnalyticsModule::pruneDiscovered(qint64 nowSec) {
    for (auto it = m_discovered.begin(); it != m_discovered.end(); ) {
        if (nowSec - it.value() > kDiscoveryForgetAfterS)
            it = m_discovered.erase(it);
        else
            ++it;
    }
}

QJsonArray PhotoAnalyticsModule::availableSources() const {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QJsonArray arr;
    QSet<QString> seen;
    auto add = [&](const QString& url, bool configured, bool online) {
        if (url.isEmpty() || seen.contains(url))
            return;
        seen.insert(url);
        arr.append(QJsonObject{{"url", url}, {"host", QUrl(url).host()},
                               {"online", online}, {"configured", configured}});
    };
    // Source déclarée d'abord (toujours proposée, même hors ligne : c'est un choix).
    if (!m_sourceUrl.isEmpty())
        add(m_sourceUrl, true, true);
    // Puis les découvertes récentes (annoncées il y a moins du seuil d'oubli).
    for (auto it = m_discovered.constBegin(); it != m_discovered.constEnd(); ++it)
        if (now - it.value() <= kDiscoveryForgetAfterS)
            add(it.key(), false, (now - it.value()) < 120);
    return arr;
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

QJsonObject PhotoAnalyticsModule::fetchDatasetSync(const QString& sourceUrl, bool* ok,
                                                   QString* error) const {
    if (ok) *ok = false;
    if (sourceUrl.isEmpty()) {
        if (error) *error = QStringLiteral("source vide");
        return {};
    }
    QString base = sourceUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    // Fetch synchrone borné : boucle d'événements imbriquée quittée par la fin de la
    // requête OU un délai de garde. Acceptable pour une action ponctuelle (pas la voie
    // chaude périodique) ; on ne bloque jamais indéfiniment sur une source muette.
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(base + QStringLiteral("/api/v1/photos/dataset"))};
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
        if (error) *error = reply->errorString();
        reply->deleteLater();
        return {};
    }
    const QJsonObject ds = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    if (ok) *ok = true;
    return ds;
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
    bool ok = false;
    QString err;
    const QJsonObject ds = fetchDatasetSync(sourceUrl, &ok, &err);
    if (!ok) {
        snap["last_error"] = err;
        return snap;
    }
    snap["dataset"]    = ds;
    snap["reachable"]  = true;
    snap["fetched_at"] = nowIso();
    return snap;
}

QJsonObject PhotoAnalyticsModule::fetchMerged(const QStringList& sources) const {
    // Règles d'interprétation (identiques toutes sources) : réutilisées par la page.
    QJsonArray bucketsJson;
    for (const FocalBucket& b : m_buckets)
        bucketsJson.append(QJsonObject{{"min", b.min}, {"max", b.max}, {"label", b.label}});
    QJsonArray exclJson;
    for (const QString& c : m_excludeCameras)
        exclJson.append(c);

    // Dictionnaires GLOBAUX (chaînes uniques toutes sources confondues) : chaque source
    // a ses propres index, on ré-interne pour un espace d'index commun.
    QJsonArray gCam, gLens, gType;
    QHash<QString, int> ciCam, ciLens, ciType;
    auto intern = [](QJsonArray& dict, QHash<QString, int>& idx, const QString& s) -> int {
        auto it = idx.constFind(s);
        if (it != idx.constEnd())
            return it.value();
        const int n = dict.size();
        dict.append(s);
        idx.insert(s, n);
        return n;
    };
    auto remap = [&intern](const QJsonValue& v, const QJsonArray& srcDict,
                           QJsonArray& gDict, QHash<QString, int>& gIdx) -> QJsonValue {
        if (!v.isDouble())
            return QJsonValue::Null;
        const int si = v.toInt();
        if (si < 0 || si >= srcDict.size())
            return QJsonValue::Null;
        return intern(gDict, gIdx, srcDict.at(si).toString());
    };

    QJsonObject gFolders;
    QJsonArray cTaken, cCam, cLens, cType, cFocal, cFocal35, cAper, cIso, cShut, cFolder;
    QSet<QString> seenFp;
    int dupRemoved = 0, total = 0;
    QJsonArray sourceStates;
    bool anyReachable = false;
    // Espace d'identifiants de dossier PAR SOURCE : les folder_id d'un poste ne doivent
    // pas écraser ceux d'un autre. On préfixe par le rang de la source.
    constexpr qint64 kFolderNs = 1000000;

    int srcIdx = 0;
    for (const QString& src : sources) {
        bool ok = false;
        QString err;
        const QJsonObject ds = fetchDatasetSync(src, &ok, &err);
        const QString host = QUrl(src).host();
        if (!ok) {
            sourceStates.append(QJsonObject{{"url", src}, {"host", host},
                {"reachable", false}, {"last_error", err}, {"count", 0}, {"kept", 0}});
            ++srcIdx;
            continue;
        }
        anyReachable = true;
        const QJsonObject cols = ds.value(QStringLiteral("columns")).toObject();
        const QJsonObject dict = ds.value(QStringLiteral("dictionaries")).toObject();
        const QJsonObject folders = ds.value(QStringLiteral("folders")).toObject();
        const QJsonArray dCam = dict.value(QStringLiteral("camera")).toArray();
        const QJsonArray dLens = dict.value(QStringLiteral("lens")).toArray();
        const QJsonArray dType = dict.value(QStringLiteral("file_type")).toArray();
        const QJsonArray taken = cols.value(QStringLiteral("taken_at")).toArray();
        const QJsonArray camera = cols.value(QStringLiteral("camera")).toArray();
        const QJsonArray lens = cols.value(QStringLiteral("lens")).toArray();
        const QJsonArray ftype = cols.value(QStringLiteral("file_type")).toArray();
        const QJsonArray focal = cols.value(QStringLiteral("focal_length")).toArray();
        const QJsonArray focal35 = cols.value(QStringLiteral("focal_length_35mm")).toArray();
        const QJsonArray aper = cols.value(QStringLiteral("aperture")).toArray();
        const QJsonArray iso = cols.value(QStringLiteral("iso")).toArray();
        const QJsonArray shut = cols.value(QStringLiteral("shutter_speed_s")).toArray();
        const QJsonArray fol = cols.value(QStringLiteral("folder_id")).toArray();
        const QJsonArray fp = cols.value(QStringLiteral("fingerprint")).toArray();
        const int n = taken.size();

        // Libellés de dossiers préfixés du poste (« host · libellé »), id namespacés.
        const qint64 nsBase = static_cast<qint64>(srcIdx + 1) * kFolderNs;
        for (auto it = folders.constBegin(); it != folders.constEnd(); ++it)
            gFolders.insert(QString::number(nsBase + it.key().toLongLong()),
                            host + QStringLiteral(" · ") + it.value().toString());

        auto at = [](const QJsonArray& a, int i) -> QJsonValue {
            return i < a.size() ? a.at(i) : QJsonValue(QJsonValue::Null);
        };
        int kept = 0;
        for (int i = 0; i < n; ++i) {
            // Dédoublonnage : une photo déjà vue (même empreinte, donc même fichier sur
            // un autre poste) n'est comptée qu'une fois.
            const QString f = at(fp, i).toString();
            if (!f.isEmpty()) {
                if (seenFp.contains(f)) { ++dupRemoved; continue; }
                seenFp.insert(f);
            }
            cTaken.append(at(taken, i));
            cCam.append(remap(at(camera, i), dCam, gCam, ciCam));
            cLens.append(remap(at(lens, i), dLens, gLens, ciLens));
            cType.append(remap(at(ftype, i), dType, gType, ciType));
            cFocal.append(at(focal, i));
            cFocal35.append(at(focal35, i));
            cAper.append(at(aper, i));
            cIso.append(at(iso, i));
            cShut.append(at(shut, i));
            const QJsonValue fv = at(fol, i);
            cFolder.append(fv.isDouble()
                ? QJsonValue(static_cast<double>(nsBase + static_cast<qint64>(fv.toDouble())))
                : QJsonValue(QJsonValue::Null));
            ++kept;
            ++total;
        }
        sourceStates.append(QJsonObject{{"url", src}, {"host", host}, {"reachable", true},
                                        {"count", n}, {"kept", kept}});
        ++srcIdx;
    }

    QJsonObject columns{
        {"taken_at", cTaken}, {"camera", cCam}, {"lens", cLens}, {"file_type", cType},
        {"focal_length", cFocal}, {"focal_length_35mm", cFocal35}, {"aperture", cAper},
        {"iso", cIso}, {"shutter_speed_s", cShut}, {"folder_id", cFolder},
    };
    QJsonObject dictionaries{{"camera", gCam}, {"lens", gLens}, {"file_type", gType}};
    QJsonObject dataset{
        {"count", total}, {"dictionaries", dictionaries}, {"columns", columns},
        {"folders", gFolders},
    };

    return QJsonObject{
        {"source_url", sources.join(QStringLiteral(", "))},
        {"sources", sourceStates},
        {"reachable", anyReachable},
        {"fetched_at", anyReachable ? QJsonValue(nowIso()) : QJsonValue(QJsonValue::Null)},
        {"last_error", anyReachable ? QJsonValue(QJsonValue::Null)
                                    : QJsonValue(QStringLiteral("aucune source joignable"))},
        {"duplicates_removed", dupRemoved},
        {"dataset", dataset},
        {"focal_buckets", bucketsJson},
        {"exclude_cameras", exclJson},
    };
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
