/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/GitHubAnalyticsModule.h"
#include "morfanalytics/data/GitHubStore.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUdpSocket>
#include <QHostAddress>
#include <QUrl>

namespace morfanalytics {

GitHubAnalyticsModule::GitHubAnalyticsModule(const QString& id, QStringList collectors,
                                             QString dbPath, int intervalMs,
                                             quint16 discoveryUdpPort, bool discoveryEnabled,
                                             QObject* parent)
    : IModule(id, QStringLiteral("github"), parent),
      m_collectors(std::move(collectors)),
      m_dbPath(std::move(dbPath)),
      m_intervalMs(intervalMs > 0 ? intervalMs : 300000),
      m_discoveryPort(discoveryUdpPort),
      m_discoveryEnabled(discoveryEnabled) {}

GitHubAnalyticsModule::~GitHubAnalyticsModule() { stop(); }

bool GitHubAnalyticsModule::start() {
    m_store = std::make_unique<GitHubStore>(m_dbPath);
    if (!m_store->open()) {
        m_lastError = QStringLiteral("stockage indisponible : ") + m_store->lastError();
        m_store.reset();
        return true;
    }
    if (m_discoveryEnabled) {
        m_beacon = new QUdpSocket(this);
        if (m_beacon->bind(QHostAddress::AnyIPv4, m_discoveryPort,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            connect(m_beacon, &QUdpSocket::readyRead, this, &GitHubAnalyticsModule::onBeaconDatagram);
        } else {
            m_lastError = QStringLiteral("écoute beacon impossible");
            m_beacon->deleteLater();
            m_beacon = nullptr;
        }
    }
    m_timer = new QTimer(this);
    m_timer->setInterval(m_intervalMs);
    connect(m_timer, &QTimer::timeout, this, &GitHubAnalyticsModule::poll);
    m_timer->start();
    return true;
}

void GitHubAnalyticsModule::stop() {
    if (m_timer) { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
    if (m_beacon) { m_beacon->close(); m_beacon->deleteLater(); m_beacon = nullptr; }
    m_store.reset();
}

QJsonObject GitHubAnalyticsModule::statusJson() const {
    return QJsonObject{
        {QStringLiteral("collectors"), m_collectors.size() + m_discovered.size()},
        {QStringLiteral("imported"), m_imported},
        {QStringLiteral("last_error"), m_lastError},
        {QStringLiteral("store"), m_store && m_store->isOpen()},
    };
}

QJsonObject GitHubAnalyticsModule::overview(const QString& repo, const QString& fromDay,
                                            const QString& toDay) const {
    if (!m_store)
        return QJsonObject{{QStringLiteral("error"), m_lastError.isEmpty()
            ? QStringLiteral("magasin GitHub indisponible") : m_lastError}};
    QJsonObject o = m_store->overview(repo, fromDay, toDay);
    if (!m_lastError.isEmpty())
        o[QStringLiteral("last_error")] = m_lastError;
    return o;
}

QJsonObject GitHubAnalyticsModule::repository(const QString& fullName) const {
    return m_store ? m_store->repository(fullName) : QJsonObject{};
}

QStringList GitHubAnalyticsModule::activeCollectors() const {
    QStringList all = m_collectors;
    for (auto it = m_discovered.constBegin(); it != m_discovered.constEnd(); ++it)
        if (!all.contains(it.key()))
            all << it.key();
    return all;
}

void GitHubAnalyticsModule::onBeaconDatagram() {
    if (!m_beacon) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    while (m_beacon->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_beacon->receiveDatagram();
        const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        bool collection = false;
        bool github = false;
        for (const QJsonValue& c : o.value(QStringLiteral("capabilities")).toArray()) {
            if (c.toString() == QLatin1String("collection")) collection = true;
            if (c.toString() == QLatin1String("github-traffic")) github = true;
        }
        // On exige la capacite de collecte ; github-traffic est un bonus (connecteur present).
        if (!collection)
            continue;
        const int port = o.value(QStringLiteral("status_port")).toInt();
        if (port <= 0) continue;
        QString ip = dg.senderAddress().toString();
        if (ip.startsWith(QLatin1String("::ffff:")))
            ip = ip.mid(ip.lastIndexOf(QLatin1Char(':')) + 1);
        Q_UNUSED(github);
        m_discovered.insert(QStringLiteral("http://%1:%2").arg(ip).arg(port), now);
    }
}

QByteArray GitHubAnalyticsModule::httpGet(const QString& url, int timeoutMs, int& status) {
    QNetworkAccessManager nam;
    QNetworkRequest req((QUrl(url)));
    QNetworkReply* reply = nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    if (!timer.isActive() && reply->isRunning())
        reply->abort();
    status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    return body;
}

void GitHubAnalyticsModule::poll() {
    // Plus de tirage depuis morfCollector : SiteWatch pousse la verite consolidee.
}

bool GitHubAnalyticsModule::ingestAuthority(const QJsonObject& payload) {
    if (!m_store)
        return false;
    if (!m_store->ingestAuthority(payload)) {
        m_lastError = m_store->lastError();
        return false;
    }
    ++m_imported;
    m_lastError.clear();
    return true;
}

void GitHubAnalyticsModule::importFrom(const QString& baseUrl) {
    if (!m_store)
        return;
    int status = 0;
    const QJsonObject sources = QJsonDocument::fromJson(
        httpGet(baseUrl + QStringLiteral("/sources"), 8000, status)).object();
    if (status != 200) {
        m_lastError = QStringLiteral("collecteur injoignable : %1").arg(baseUrl);
        return;
    }
    for (const QJsonValue& v : sources.value(QStringLiteral("sources")).toArray()) {
        const QJsonObject s = v.toObject();
        if (s.value(QStringLiteral("connector")).toString() != QLatin1String("github-traffic"))
            continue;
        const QString sid = s.value(QStringLiteral("source_id")).toString();
        const QString enc = QString::fromUtf8(QUrl::toPercentEncoding(sid));
        const QJsonObject objs = QJsonDocument::fromJson(
            httpGet(baseUrl + QStringLiteral("/sources/") + enc + QStringLiteral("/objects"),
                    8000, status)).object();
        for (const QJsonValue& ov : objs.value(QStringLiteral("objects")).toArray()) {
            const QString oid = ov.toObject().value(QStringLiteral("object_id")).toString();
            if (oid.isEmpty() || m_store->hasObject(oid))
                continue;
            const QByteArray raw = httpGet(
                baseUrl + QStringLiteral("/objects/") + QString::fromUtf8(QUrl::toPercentEncoding(oid)),
                20000, status);
            if (status != 200)
                continue;
            const QJsonObject snap = QJsonDocument::fromJson(raw).object();
            if (snap.value(QStringLiteral("contract")).toString() != QLatin1String("github-traffic/1"))
                continue;
            if (m_store->importSnapshot(oid, snap))
                ++m_imported;
            else
                m_lastError = m_store->lastError();
        }
    }
}

} // namespace morfanalytics
