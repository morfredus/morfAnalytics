/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/MonitorModule.h"
#include "morfanalytics/data/MonitorStore.h"

#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QUrl>
#include <QHostAddress>
#include <QVariant>

#include <cmath>
#include <utility>

namespace morfanalytics {

namespace {
// Un nombre JSON, ou NaN si la clé manque : NaN => NULL en base, l'absence de
// mesure ne devient jamais un 0.
double num(const QJsonValue& v) { return v.isDouble() ? v.toDouble() : qQNaN(); }

// Epoch (secondes). Python envoie `int(time.time())` : entier JSON.
// Sur certaines versions de Qt, `isDouble()` est alors faux (type Integer),
// et les horodatages tombaient sur « maintenant » : durée 0, pas de CPU/temp.
qint64 jsonEpochS(const QJsonValue& v, bool* ok) {
    if (ok)
        *ok = false;
    if (v.isUndefined() || v.isNull() || v.isBool() || v.isArray() || v.isObject())
        return 0;
    qint64 n = 0;
    bool parsed = false;
    if (v.isString()) {
        n = v.toString().trimmed().toLongLong(&parsed);
    } else {
        const QVariant var = v.toVariant();
        n = var.toLongLong(&parsed);
        if (!parsed && v.isDouble()) {
            n = static_cast<qint64>(v.toDouble());
            parsed = true;
        }
    }
    if (!parsed)
        return 0;
    // Millisecondes (13 chiffres) parfois envoyées par erreur : on ramène en secondes.
    if (n > 100000000000LL)
        n /= 1000;
    if (ok)
        *ok = true;
    return n;
}

qint64 jsonEpochField(const QJsonObject& a, const char* k1, const char* k2, bool* ok) {
    bool parsed = false;
    qint64 n = jsonEpochS(a.value(QLatin1String(k1)), &parsed);
    if (!parsed)
        n = jsonEpochS(a.value(QLatin1String(k2)), &parsed);
    if (ok)
        *ok = parsed;
    return n;
}
} // namespace

MonitorModule::MonitorModule(const QString& id, QStringList sources, int intervalMs,
                             QString dbPath, int retentionDays, quint16 discoveryUdpPort,
                             bool discoveryEnabled, QObject* parent)
    : IModule(id, QStringLiteral("monitor"), parent),
      m_sources(std::move(sources)),
      m_intervalMs(intervalMs > 0 ? intervalMs : 15000),
      m_dbPath(std::move(dbPath)),
      m_retentionDays(retentionDays),
      m_discoveryPort(discoveryUdpPort),
      m_discoveryEnabled(discoveryEnabled) {}

MonitorModule::~MonitorModule() { stop(); }

bool MonitorModule::start() {
    m_store = std::make_unique<MonitorStore>(m_dbPath);
    if (!m_store->open()) {
        // Sans stockage, la collecte n'aurait rien à écrire : on démarre en état
        // dégradé (visible dans /modules) plutôt que de disparaître silencieusement.
        m_lastError = QStringLiteral("stockage indisponible : ") + m_store->lastError();
        m_store.reset();
        return true;
    }

    m_net = new QNetworkAccessManager(this);

    // Écoute du beacon : découverte automatique des morfMonitor du parc. On
    // ÉCOUTE, on ne sonde pas — les morfMonitor annoncent leur présence en
    // broadcast. ShareAddress car morfAnalytics ÉMET déjà son propre heartbeat
    // sur ce port (et le Dashboard peut aussi écouter) : plusieurs programmes de
    // la machine se partagent le port du parc. Un échec de bind (port pris sans
    // partage) ne fait pas tomber la collecte : les sources déclarées suffisent.
    if (m_discoveryEnabled) {
        m_beacon = new QUdpSocket(this);
        if (m_beacon->bind(QHostAddress::AnyIPv4, m_discoveryPort,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            connect(m_beacon, &QUdpSocket::readyRead, this, &MonitorModule::onBeaconDatagram);
        } else {
            m_lastError = QStringLiteral("écoute beacon impossible (port %1) : %2")
                              .arg(m_discoveryPort).arg(m_beacon->errorString());
            m_beacon->deleteLater();
            m_beacon = nullptr;
        }
    }

    m_timer = new QTimer(this);
    m_timer->setInterval(m_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &MonitorModule::poll);
    m_timer->start();
    poll();   // premier relevé immédiat
    return true;
}

void MonitorModule::stop() {
    if (m_timer)  { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
    if (m_beacon) { m_beacon->close(); m_beacon->deleteLater(); m_beacon = nullptr; }
    if (m_net)    { m_net->deleteLater(); m_net = nullptr; }
    m_store.reset();
}

void MonitorModule::poll() {
    if (!m_net)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    pruneDiscovered(now);
    const QStringList sources = activeSources();
    for (const QString& s : sources)
        fetch(s);

    // Rétention : au plus une fois par jour, supprimer les relevés bruts au-delà de
    // l'horizon configuré. Borne la base sur une machine modeste. 0 => illimité.
    if (m_store && m_retentionDays > 0) {
        if (now - m_lastPurgeAt > 86400) {
            m_lastPurgeAt = now;
            m_store->purgeSamplesBefore(now - static_cast<qint64>(m_retentionDays) * 86400);
        }
    }
}

QStringList MonitorModule::activeSources() const {
    // Union des sources déclarées (filet stable) et des morfMonitor découverts.
    QStringList all = m_sources;
    for (auto it = m_discovered.constBegin(); it != m_discovered.constEnd(); ++it)
        if (!all.contains(it.key()))
            all << it.key();
    return all;
}

void MonitorModule::pruneDiscovered(qint64 nowSec) {
    // Une source découverte muette depuis longtemps cesse d'être interrogée (elle
    // sera réintégrée d'elle-même si elle se réannonce). Sa donnée historique
    // reste en base : seule la SOURCE est oubliée, jamais la machine.
    for (auto it = m_discovered.begin(); it != m_discovered.end(); ) {
        if (nowSec - it.value() > kDiscoveryForgetAfterS) {
            m_sourceState.remove(it.key());
            it = m_discovered.erase(it);
        } else {
            ++it;
        }
    }
}

void MonitorModule::onBeaconDatagram() {
    if (!m_beacon)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    while (m_beacon->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_beacon->receiveDatagram();
        const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        if (o.isEmpty())
            continue;

        // Découverte par CAPACITÉ, jamais par nom : on ne retient un émetteur que
        // s'il annonce « system_monitor ». Renommer le service n'y change rien.
        bool isMonitor = false;
        for (const QJsonValue& c : o.value(QStringLiteral("capabilities")).toArray())
            if (c.toString() == QLatin1String("system_monitor")) { isMonitor = true; break; }
        if (!isMonitor)
            continue;

        // Le port du serveur /status EST celui qui sert /api/all (même serveur HTTP
        // dans morfMonitor). Sans lui, on sait qu'un morfMonitor vit mais pas où le
        // joindre : on ignore l'annonce plutôt que de deviner un port.
        const int statusPort = o.value(QStringLiteral("status_port")).toInt();
        if (statusPort <= 0)
            continue;

        // Adresse RÉELLE de l'émetteur (couche réseau), la seule dont on soit sûr
        // qu'elle permette de le joindre. Qt préfixe l'IPv4 mappée en IPv6
        // (« ::ffff:192.168.1.55 ») : un lien construit tel quel serait inutilisable.
        QString ip = dg.senderAddress().toString();
        if (ip.startsWith(QLatin1String("::ffff:")))
            ip = ip.mid(ip.lastIndexOf(QLatin1Char(':')) + 1);
        if (ip.isEmpty())
            continue;

        const QString url = QStringLiteral("http://%1:%2").arg(ip).arg(statusPort);
        const bool isNew = !m_discovered.contains(url) && !m_sources.contains(url);
        m_discovered.insert(url, now);
        if (isNew) {
            // Intégrer tout de suite plutôt que d'attendre le prochain cycle : une
            // machine qui s'annonce apparaît sans délai perceptible.
            fetch(url);
        }
    }
}

bool MonitorModule::forgetMachine(const QString& key) {
    if (!m_store)
        return false;
    const qint64 removed = m_store->forgetMachine(key);
    if (removed < 0)
        return false;   // machine inconnue

    // Retirer aussi la ou les sources découvertes qui pointaient vers cette machine,
    // pour ne pas la réintégrer au prochain cycle. On les reconnaît via l'état de
    // source (baseUrl -> machine). Une machine réellement partie ne se réannonce
    // plus : elle ne reviendra donc pas. Si elle se rallume et réannonce, c'est un
    // choix : elle sera redécouverte (l'oubli visait une machine déconnectée).
    const QStringList urls = m_sourceState.keys();
    for (const QString& url : urls) {
        if (m_sourceState.value(url).value(QStringLiteral("machine")).toString() == key) {
            m_discovered.remove(url);
            m_sourceState.remove(url);
        }
    }
    return true;
}

void MonitorModule::fetch(const QString& baseUrl) {
    // Tolère un slash de fin dans la source (http://pi4fred:8790/) : sans quoi on
    // construirait « …8790//api/all ». On retire les slashes terminaux.
    QString base = baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QNetworkRequest req{QUrl(base + QStringLiteral("/api/all"))};
    // Un morfMonitor lent ne doit pas retenir la collecte : borne courte.
    req.setTransferTimeout(4000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, baseUrl, reply]() { onReply(baseUrl, reply); });
}

void MonitorModule::onReply(const QString& baseUrl, QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_sourceState[baseUrl] = QJsonObject{
            {"reachable", false}, {"last_error", reply->errorString()}};
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        m_sourceState[baseUrl] = QJsonObject{
            {"reachable", false}, {"last_error", QStringLiteral("réponse non-JSON")}};
        return;
    }
    ingest(baseUrl, doc.object());
}

void MonitorModule::ingest(const QString& baseUrl, const QJsonObject& all) {
    if (!m_store)
        return;

    const QJsonObject sys = all.value(QStringLiteral("system")).toObject();
    const QJsonObject res = all.value(QStringLiteral("resources")).toObject();

    // Clé machine STABLE : le nom d'hôte (indépendant de l'IP DHCP). L'instance
    // morfBeacon serait idéale, mais /api/all ne la porte pas ; le hostname suffit.
    const QString host = sys.value(QStringLiteral("hostname")).toString();
    if (host.isEmpty()) {
        m_sourceState[baseUrl] = QJsonObject{
            {"reachable", false}, {"last_error", QStringLiteral("hostname absent de /api/system")}};
        return;
    }

    const qint64 ts = QDateTime::currentSecsSinceEpoch();
    const int mid = m_store->upsertMachine(host, host, sys.value(QStringLiteral("model")).toString(), ts);
    if (mid < 0) {
        m_lastError = m_store->lastError();
        return;
    }

    MachineSample s;
    s.cpuPercent = num(res.value(QStringLiteral("cpu_percent")));
    const QJsonArray load = res.value(QStringLiteral("load")).toArray();
    s.load1 = load.isEmpty() ? qQNaN() : load.at(0).toDouble();
    const QJsonObject mem = res.value(QStringLiteral("memory")).toObject();
    s.memPercent = num(mem.value(QStringLiteral("percent")));
    s.memUsed    = num(mem.value(QStringLiteral("used_b")));
    s.memTotal   = num(mem.value(QStringLiteral("total_b")));
    s.swapPercent = num(res.value(QStringLiteral("swap")).toObject().value(QStringLiteral("percent")));
    s.tempCpu     = num(res.value(QStringLiteral("temperature")).toObject().value(QStringLiteral("cpu_c")));
    s.diskPercent = num(res.value(QStringLiteral("disk")).toObject().value(QStringLiteral("percent")));
    s.uptimeS     = num(sys.value(QStringLiteral("uptime_s")));

    // Services : bloc systemd de /api/services (inclus dans /api/all). On stocke la
    // consommation de chacun (matière de la future vue « qui consomme quoi »), et on
    // compte les actifs pour la vue d'ensemble.
    const QJsonArray systemd = all.value(QStringLiteral("services")).toObject()
                                  .value(QStringLiteral("systemd")).toArray();
    QVector<ServiceSample> svc;
    int activeCount = 0;
    for (const QJsonValue& v : systemd) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("active")).toBool())
            ++activeCount;
        const QJsonObject r = o.value(QStringLiteral("resources")).toObject();
        if (r.isEmpty())
            continue;
        ServiceSample ss;
        ss.service    = o.value(QStringLiteral("unit")).toString();
        ss.cpuPercent = num(r.value(QStringLiteral("cpu_percent")));
        ss.memBytes   = num(r.value(QStringLiteral("memory_bytes")));
        ss.tasks      = num(r.value(QStringLiteral("tasks")));
        if (!ss.service.isEmpty())
            svc.append(ss);
    }
    s.servicesActive = activeCount;

    m_store->insertMachineSample(mid, ts, s);
    m_store->insertServiceSamples(mid, ts, svc);

    m_lastPollAt = ts;
    m_sourceState[baseUrl] = QJsonObject{
        {"reachable", true}, {"machine", host},
        {"last_error", QJsonValue(QJsonValue::Null)}};
    emit updated(id());
}

QJsonArray MonitorModule::machines() const {
    return m_store ? m_store->machines() : QJsonArray{};
}

QJsonObject MonitorModule::data(const QString& machineKey, qint64 fromTs, qint64 toTs,
                                int maxPoints) const {
    QJsonObject o;
    const QJsonArray machs = m_store ? m_store->machines() : QJsonArray{};
    o["machines"] = machs;

    // Machine par défaut : la première connue, pour que la page affiche quelque
    // chose dès l'ouverture sans choix explicite. On y retombe AUSSI quand la clé
    // demandée est inconnue (navigateur qui a mémorisé une machine d'avant une
    // réinstallation, ou machine oubliée) : sans ce repli, la page resterait vide
    // et figée « hors ligne » sur une machine absente, alors que d'autres sont là.
    QString key = machineKey;
    if (!machs.isEmpty()) {
        bool known = false;
        for (const QJsonValue& m : machs)
            if (m.toObject().value(QStringLiteral("key")).toString() == key) { known = true; break; }
        if (key.isEmpty() || !known)
            key = machs.first().toObject().value(QStringLiteral("key")).toString();
    }
    o["machine"] = key;
    o["from"] = static_cast<double>(fromTs);
    o["to"]   = static_cast<double>(toTs);

    if (m_store) {
        if (!key.isEmpty()) {
            const int mid = m_store->machineIdForKey(key);
            if (mid >= 0) {
                o["overview"] = m_store->latestMachine(mid);
                o["series"]   = m_store->machineSeries(mid, fromTs, toTs, maxPoints);
                o["services"] = m_store->serviceStats(mid, fromTs, toTs);
            }
        }
        // Activités et builds (indépendants des samples) : filtrés sur la machine
        // sélectionnée, ou toutes machines si aucune n'est encore connue.
        o["builds"]     = m_store->buildStats(key, fromTs, toTs);
        o["activities"] = m_store->recentActivities(key, fromTs, toTs, 20);
    }
    if (!m_store)
        o["error"] = m_lastError.isEmpty() ? QStringLiteral("stockage indisponible") : m_lastError;
    return o;
}

qint64 MonitorModule::ingestActivity(const QJsonObject& a) {
    if (!m_store)
        return -1;
    const QString type = a.value(QStringLiteral("type")).toString();
    if (type.isEmpty())
        return -1;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    bool hasStart = false, hasEnd = false, hasDur = false;
    qint64 start = jsonEpochField(a, "start", "start_ts", &hasStart);
    qint64 end   = jsonEpochField(a, "end", "end_ts", &hasEnd);
    qint64 dur   = jsonEpochField(a, "duration_s", "duration", &hasDur);
    if (hasDur && dur < 0)
        hasDur = false;
    if (!hasStart && !hasEnd) {
        start = end = now;
    } else if (!hasStart) {
        start = hasDur ? end - dur : end;
    } else if (!hasEnd) {
        end = hasDur ? start + dur : now;
    }
    if (end < start)
        std::swap(start, end);
    if (hasDur && (end - start) < dur)
        end = start + dur;
    return m_store->insertActivity(
        type,
        a.value(QStringLiteral("project")).toString(),
        a.value(QStringLiteral("machine")).toString(),
        start, end,
        a.value(QStringLiteral("status")).toString(),
        a.value(QStringLiteral("metadata")).toObject());
}

QJsonObject MonitorModule::statusJson() const {
    QJsonObject o;
    o["sources"]        = QJsonArray::fromStringList(m_sources);
    o["interval_ms"]    = m_intervalMs;
    o["retention_days"] = m_retentionDays;

    // Découverte beacon : de quoi voir d'un coup d'œil si l'écoute est active et
    // combien de morfMonitor ont été appris seuls (hors sources déclarées).
    QJsonObject disc;
    disc["enabled"]  = m_discoveryEnabled;
    disc["listening"] = (m_beacon != nullptr);
    disc["udp_port"] = m_discoveryPort;
    QJsonArray found;
    for (auto it = m_discovered.constBegin(); it != m_discovered.constEnd(); ++it)
        found.append(QJsonObject{{"source", it.key()},
                                 {"last_seen", static_cast<double>(it.value())}});
    disc["found"] = found;
    o["discovery"] = disc;

    o["machines_known"] = m_store ? m_store->machines().size() : 0;
    o["last_poll_at"]   = m_lastPollAt ? QJsonValue(static_cast<double>(m_lastPollAt))
                                       : QJsonValue(QJsonValue::Null);
    o["last_error"]     = m_lastError.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                : QJsonValue(m_lastError);
    QJsonObject srcs;
    for (auto it = m_sourceState.constBegin(); it != m_sourceState.constEnd(); ++it)
        srcs[it.key()] = it.value();
    o["source_state"] = srcs;
    return o;
}

} // namespace morfanalytics
