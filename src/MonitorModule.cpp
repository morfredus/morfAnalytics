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
#include <QJsonDocument>
#include <QDateTime>
#include <QUrl>

#include <cmath>
#include <utility>

namespace morfanalytics {

namespace {
// Un nombre JSON, ou NaN si la clé manque : NaN => NULL en base, l'absence de
// mesure ne devient jamais un 0.
double num(const QJsonValue& v) { return v.isDouble() ? v.toDouble() : qQNaN(); }
} // namespace

MonitorModule::MonitorModule(const QString& id, QStringList sources, int intervalMs,
                             QString dbPath, QObject* parent)
    : IModule(id, QStringLiteral("monitor"), parent),
      m_sources(std::move(sources)),
      m_intervalMs(intervalMs > 0 ? intervalMs : 15000),
      m_dbPath(std::move(dbPath)) {}

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
    m_timer = new QTimer(this);
    m_timer->setInterval(m_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &MonitorModule::poll);
    m_timer->start();
    poll();   // premier relevé immédiat
    return true;
}

void MonitorModule::stop() {
    if (m_timer) { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
    if (m_net)   { m_net->deleteLater(); m_net = nullptr; }
    m_store.reset();
}

void MonitorModule::poll() {
    if (!m_net || m_sources.isEmpty())
        return;
    for (const QString& s : m_sources)
        fetch(s);
}

void MonitorModule::fetch(const QString& baseUrl) {
    QNetworkRequest req{QUrl(baseUrl + QStringLiteral("/api/all"))};
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
    // chose dès l'ouverture sans choix explicite.
    QString key = machineKey;
    if (key.isEmpty() && !machs.isEmpty())
        key = machs.first().toObject().value(QStringLiteral("key")).toString();
    o["machine"] = key;
    o["from"] = static_cast<double>(fromTs);
    o["to"]   = static_cast<double>(toTs);

    if (m_store && !key.isEmpty()) {
        const int mid = m_store->machineIdForKey(key);
        if (mid >= 0) {
            o["overview"] = m_store->latestMachine(mid);
            o["series"]   = m_store->machineSeries(mid, fromTs, toTs, maxPoints);
        }
    }
    if (!m_store)
        o["error"] = m_lastError.isEmpty() ? QStringLiteral("stockage indisponible") : m_lastError;
    return o;
}

QJsonObject MonitorModule::statusJson() const {
    QJsonObject o;
    o["sources"]        = QJsonArray::fromStringList(m_sources);
    o["interval_ms"]    = m_intervalMs;
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
