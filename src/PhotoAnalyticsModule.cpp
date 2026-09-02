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
#include <QHostInfo>
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QUrl>
#include <QSet>
#include <QFile>
#include <QDir>
#include <QJsonDocument>

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
                                           QStringList excludeCameras, QStringList ownedCameras,
                                           quint16 discoveryUdpPort,
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
    loadPractice(ownedCameras);
    m_snapshot = QJsonObject{
        {"source_url", m_sourceUrl},
        {"reachable", false},
        {"fetched_at", QJsonValue(QJsonValue::Null)},
        {"last_error", QJsonValue(QJsonValue::Null)},
    };
    attachPractice(m_snapshot);
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
        // Le heartbeat porte le hostname de la machine : on le garde pour un affichage
        // lisible (l'URL, elle, reste construite sur l'IP -- la seule joignable).
        const QString host = o.value(QStringLiteral("host")).toString();
        m_discovered.insert(QStringLiteral("http://%1:%2").arg(ip).arg(statusPort),
                            Discovered{now, host});
    }
    pruneDiscovered(now);
}

void PhotoAnalyticsModule::pruneDiscovered(qint64 nowSec) {
    for (auto it = m_discovered.begin(); it != m_discovered.end(); ) {
        if (nowSec - it.value().lastSeen > kDiscoveryForgetAfterS)
            it = m_discovered.erase(it);
        else
            ++it;
    }
}

QString PhotoAnalyticsModule::hostForUrl(const QString& url) const {
    const QString h = QUrl(url).host();
    auto stripLocal = [](QString n) {
        if (n.endsWith(QLatin1String(".local"), Qt::CaseInsensitive))
            n.chop(6);
        return n;
    };
    // Loopback = CETTE machine : donner son nom plutôt que « 127.0.0.1 ».
    if (h == QLatin1String("localhost") || h == QLatin1String("::1")
        || h.startsWith(QLatin1String("127."))) {
        const QString local = stripLocal(QHostInfo::localHostName());
        return local.isEmpty() ? h : local;
    }
    const auto it = m_discovered.constFind(url);
    if (it != m_discovered.constEnd() && !it.value().host.isEmpty())
        return stripLocal(it.value().host);         // nom annoncé par le beacon (lisible)
    return h;                           // repli : hôte de l'URL (souvent une IP)
}

QJsonArray PhotoAnalyticsModule::availableSources() const {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    auto stripLocal = [](QString n) {
        if (n.endsWith(QLatin1String(".local"), Qt::CaseInsensitive))
            n.chop(6);
        return n.toLower();
    };
    const QString localHost = stripLocal(QHostInfo::localHostName());
    auto isLoop = [](const QString& h) {
        return h == QLatin1String("localhost") || h == QLatin1String("::1")
            || h.startsWith(QLatin1String("127."));
    };
    auto isIpv4 = [](const QString& h) {
        QHostAddress addr(h);
        return addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback();
    };
    QSet<QString> localIps;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        for (const QNetworkAddressEntry& e : iface.addressEntries()) {
            const QHostAddress ip = e.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol)
                localIps.insert(ip.toString());
        }
    }

    struct Cand { QString url; QString host; bool configured; bool loopback; bool online; };
    QVector<Cand> cands;
    if (!m_sourceUrl.isEmpty()) {
        const QString h = QUrl(m_sourceUrl).host();
        cands.push_back({m_sourceUrl, hostForUrl(m_sourceUrl), true, isLoop(h), true});
    }
    for (auto it = m_discovered.constBegin(); it != m_discovered.constEnd(); ++it)
        if (now - it.value().lastSeen <= kDiscoveryForgetAfterS) {
            const QString rawHost = QUrl(it.key()).host();
            cands.push_back({it.key(), hostForUrl(it.key()), false, isLoop(rawHost),
                             (now - it.value().lastSeen) < 120});
        }

    // Une machine = un nom (mDNS / hostname), pas une URL. Loopback, IP LAN et
    // nom annoncé (pi4dev vs pi4dev.local) sont le même poste.
    auto machineId = [&](const Cand& c) -> QString {
        const QString urlHost = QUrl(c.url).host();
        if (c.loopback || localIps.contains(urlHost))
            return localHost.isEmpty() ? QStringLiteral("__local__") : localHost;
        const QString named = stripLocal(c.host);
        if (!named.isEmpty() && !isIpv4(named) && named != QLatin1String("localhost"))
            return named;
        return urlHost.toLower();
    };

    QHash<QString, int> pos;
    QJsonArray arr;
    for (const Cand& c : cands) {
        const QString mid = machineId(c);
        const bool local = c.loopback || localIps.contains(QUrl(c.url).host())
            || (!localHost.isEmpty() && mid == localHost);
        const QString named = stripLocal(c.host);
        const bool namedOk = !named.isEmpty() && !isIpv4(named)
            && named != QLatin1String("localhost");
        QString label = namedOk ? (local ? (c.host + QStringLiteral(" (local)")) : c.host)
                                : (local ? (localHost.isEmpty() ? QStringLiteral("base locale")
                                                                : localHost + QStringLiteral(" (local)"))
                                         : QUrl(c.url).host());
        const auto it = pos.constFind(mid);
        if (it == pos.constEnd()) {
            pos.insert(mid, arr.size());
            QJsonArray aliases;
            aliases.append(c.url);
            arr.append(QJsonObject{{"url", c.url}, {"host", label}, {"online", c.online},
                                   {"configured", c.configured}, {"local", local},
                                   {"aliases", aliases}});
        } else {
            QJsonObject e = arr.at(it.value()).toObject();
            QJsonArray aliases = e.value(QStringLiteral("aliases")).toArray();
            bool have = false;
            for (const QJsonValue& v : aliases)
                if (v.toString() == c.url) { have = true; break; }
            if (!have)
                aliases.append(c.url);
            e["aliases"] = aliases;
            if (local && c.loopback)
                e["url"] = c.url;
            if (c.online)
                e["online"] = true;
            if (c.configured)
                e["configured"] = true;
            if (namedOk)
                e["host"] = label;
            arr.replace(it.value(), e);
        }
    }
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
    req.setTransferTimeout(key == QLatin1String("dataset") ? 60000 : 8000);
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
    attachPractice(snap);

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

QJsonObject PhotoAnalyticsModule::fetchJsonSync(const QString& sourceUrl, const QString& path,
                                                int timeoutMs, bool* ok, QString* error) const {
    if (ok) *ok = false;
    if (sourceUrl.isEmpty()) {
        if (error) *error = QStringLiteral("source vide");
        return {};
    }
    QString base = sourceUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(base + path)};
    const int ms = timeoutMs > 0 ? timeoutMs : 8000;
    req.setTransferTimeout(ms);
    QEventLoop loop;
    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(ms + 2000);
    loop.exec();

    if (reply->isRunning())
        reply->abort();
    if (reply->error() != QNetworkReply::NoError) {
        if (error) *error = reply->errorString();
        reply->deleteLater();
        return {};
    }
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    if (ok) *ok = true;
    return obj;
}

QJsonObject PhotoAnalyticsModule::fetchDatasetSync(const QString& sourceUrl, bool* ok,
                                                   QString* error) const {
    // Le dataset d'une vraie photothèque dépasse largement 8 s : un timeout trop
    // court tronquait l'export et l'analyse comptait moins de photos que PhotoHub.
    return fetchJsonSync(sourceUrl, QStringLiteral("/api/v1/photos/dataset"), 60000, ok, error);
}

QString PhotoAnalyticsModule::stateDir() {
    const QByteArray env = qgetenv("STATE_DIRECTORY");
    if (!env.isEmpty()) {
        const QString first = QString::fromLocal8Bit(env).split(QLatin1Char(':')).first();
        if (!first.isEmpty()) { QDir().mkpath(first); return first; }
    }
#if defined(Q_OS_WIN)
    const QString base = qEnvironmentVariable("ProgramData", QStringLiteral("C:/ProgramData"));
    const QString dir  = QDir(base).filePath(QStringLiteral("morfsystem/morfanalytics/state"));
#else
    const QString dir  = QStringLiteral("/var/lib/morfsystem/morfanalytics");
#endif
    QDir().mkpath(dir);
    return dir;
}

QString PhotoAnalyticsModule::practicePath() const {
    return QDir(stateDir()).filePath(QStringLiteral("photo-practice.json"));
}

void PhotoAnalyticsModule::loadPractice(const QStringList& configOwned) {
    QFile f(practicePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        QStringList owned;
        for (const QJsonValue& v : o.value(QStringLiteral("owned_cameras")).toArray()) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty())
                owned << s;
        }
        if (!owned.isEmpty()) {
            owned.removeDuplicates();
            std::sort(owned.begin(), owned.end());
            m_ownedCameras = owned;
            return;
        }
    }
    m_ownedCameras = configOwned;
    m_ownedCameras.removeAll(QString());
    m_ownedCameras.removeDuplicates();
    std::sort(m_ownedCameras.begin(), m_ownedCameras.end());
}

bool PhotoAnalyticsModule::savePractice() const {
    QDir().mkpath(stateDir());
    QJsonArray arr;
    for (const QString& c : m_ownedCameras)
        arr.append(c);
    const QJsonObject o{
        {"owned_cameras", arr},
        {"updated_at", nowIso()},
    };
    QFile f(practicePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool PhotoAnalyticsModule::setOwnedCameras(QStringList names) {
    names.removeAll(QString());
    for (QString& s : names)
        s = s.trimmed();
    names.removeAll(QString());
    names.removeDuplicates();
    std::sort(names.begin(), names.end());
    m_ownedCameras = names;
    attachPractice(m_snapshot);
    return savePractice();
}

void PhotoAnalyticsModule::attachPractice(QJsonObject& snap) const {
    QJsonArray owned;
    for (const QString& c : m_ownedCameras)
        owned.append(c);
    snap[QStringLiteral("owned_cameras")] = owned;
    QJsonArray excl;
    for (const QString& c : m_excludeCameras)
        excl.append(c);
    snap[QStringLiteral("exclude_cameras")] = excl;
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
        attachPractice(snap);
        return snap;
    }
    snap["dataset"]    = ds;
    snap["reachable"]  = true;
    snap["fetched_at"] = nowIso();
    attachPractice(snap);
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
    QJsonArray gCam, gLens, gType, gCtx, gSubj;
    QHash<QString, int> ciCam, ciLens, ciType, ciCtx, ciSubj;
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
    QJsonArray cCtx, cSubj;   // contexte photographique (morfphoto-context/2)
    // Empreinte -> rang de la PREMIÈRE source où on l'a vue. Le dédoublonnage est
    // strictement INTER-POSTES : une même empreinte revue depuis un AUTRE poste est un
    // doublon (même fichier indexé sur deux machines) et on l'écarte ; revue dans le
    // MÊME poste, c'est un fichier distinct (morfPhoto garantit des chemins uniques par
    // poste ; deux fichiers peuvent partager nom+taille+date, surtout date EXIF absente)
    // et on le GARDE. Sans cette distinction, une source sans EXIF s'auto-amputait.
    QHash<QString, int> seenFp;
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
        const QString host = hostForUrl(src);   // nom lisible du poste (beacon), pas l'IP
        if (!ok) {
            sourceStates.append(QJsonObject{{"url", src}, {"host", host},
                {"reachable", false}, {"last_error", err}, {"count", 0}, {"kept", 0}});
            ++srcIdx;
            continue;
        }
        anyReachable = true;
        bool sumOk = false;
        const QJsonObject summary = fetchJsonSync(src, QStringLiteral("/api/v1/photos/summary"),
                                                  8000, &sumOk, nullptr);
        bool listOk = false;
        const QJsonObject listed = fetchJsonSync(src, QStringLiteral("/api/v1/photos?limit=1"),
                                                 8000, &listOk, nullptr);
        const int filesPresent = sumOk ? summary.value(QStringLiteral("files_present")).toInt(-1) : -1;
        const int filesTotal = sumOk ? summary.value(QStringLiteral("files_total")).toInt(-1) : -1;
        const int listTotal = listOk ? listed.value(QStringLiteral("total")).toInt(-1) : -1;
        const QJsonObject cols = ds.value(QStringLiteral("columns")).toObject();
        const QJsonObject dict = ds.value(QStringLiteral("dictionaries")).toObject();
        const QJsonObject folders = ds.value(QStringLiteral("folders")).toObject();
        const QJsonArray dCam = dict.value(QStringLiteral("camera")).toArray();
        const QJsonArray dLens = dict.value(QStringLiteral("lens")).toArray();
        const QJsonArray dType = dict.value(QStringLiteral("file_type")).toArray();
        const QJsonArray dCtx = dict.value(QStringLiteral("context")).toArray();
        const QJsonArray dSubj = dict.value(QStringLiteral("subject")).toArray();
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
        const QJsonArray ctxCol = cols.value(QStringLiteral("context")).toArray();
        const QJsonArray subjCol = cols.value(QStringLiteral("subject")).toArray();
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
            // Dédoublonnage INTER-POSTES uniquement (voir seenFp).
            const QString f = at(fp, i).toString();
            if (!f.isEmpty()) {
                const auto it = seenFp.constFind(f);
                if (it != seenFp.constEnd()) {
                    if (it.value() != srcIdx) { ++dupRemoved; continue; }  // même fichier, autre poste
                    // même poste : fichier distinct à empreinte identique -> gardé.
                } else {
                    seenFp.insert(f, srcIdx);
                }
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
            // Contexte : ré-interné dans les dictionnaires globaux ; null (non qualifié)
            // préservé, distinct d'INCONNU (valeur du dictionnaire).
            cCtx.append(remap(at(ctxCol, i), dCtx, gCtx, ciCtx));
            cSubj.append(remap(at(subjCol, i), dSubj, gSubj, ciSubj));
            ++kept;
            ++total;
        }
        sourceStates.append(QJsonObject{
            {"url", src}, {"host", host}, {"reachable", true},
            {"count", n}, {"kept", kept},
            {"files_present", filesPresent}, {"files_total", filesTotal},
            {"list_total", listTotal},
        });
        ++srcIdx;
    }

    QJsonObject columns{
        {"taken_at", cTaken}, {"camera", cCam}, {"lens", cLens}, {"file_type", cType},
        {"focal_length", cFocal}, {"focal_length_35mm", cFocal35}, {"aperture", cAper},
        {"iso", cIso}, {"shutter_speed_s", cShut}, {"folder_id", cFolder},
        {"context", cCtx}, {"subject", cSubj},
    };
    QJsonObject dictionaries{{"camera", gCam}, {"lens", gLens}, {"file_type", gType},
        {"context", gCtx}, {"subject", gSubj}};
    QJsonObject dataset{
        {"count", total}, {"dictionaries", dictionaries}, {"columns", columns},
        {"folders", gFolders},
    };

    QJsonObject out{
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
    attachPractice(out);
    return out;
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
