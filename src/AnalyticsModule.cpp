/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/AnalyticsModule.h"
#include "morfanalytics/data/SampleStore.h"
#include "morfanalytics/collect/MeteoHubCollector.h"

#include <QTimer>
#include <QDateTime>
#include <QDir>

namespace morfanalytics {

namespace {
// Canaux collectés depuis MeteoHub. Le cache et les analyses sont génériques :
// changer cette liste suffit à suivre un appareil exposant d'autres grandeurs.
const QStringList kChannels{QStringLiteral("temp"), QStringLiteral("hum"),
                            QStringLiteral("pres")};
} // namespace

AnalyticsModule::AnalyticsModule(const QString& id, int maintenanceMs,
                                 QString cacheDir, QString sourceUrl,
                                 double altitudeM, bool altitudeKnown,
                                 QObject* parent)
    : IModule(id, QStringLiteral("analytics"), parent),
      m_maintenanceMs(maintenanceMs > 0 ? maintenanceMs : 60000),
      m_cacheDir(std::move(cacheDir)),
      m_sourceUrl(std::move(sourceUrl)),
      m_altitudeM(altitudeM),
      m_altitudeKnown(altitudeKnown),
      m_timer(new QTimer(this)) {
    m_timer->setInterval(m_maintenanceMs);
    connect(m_timer, &QTimer::timeout, this, &AnalyticsModule::maintainCache);
    // Le moteur est générique ; c'est cet appel, et lui seul, qui le spécialise
    // en moteur météo. Un autre projet enregistre ici son propre jeu d'analyses.
    registerMeteoAnalyses(m_analyses);
}

AnalyticsModule::~AnalyticsModule() = default;

bool AnalyticsModule::start() {
    const QString dir = m_cacheDir.isEmpty() ? QDir::currentPath() : m_cacheDir;
    const QString dbPath = QDir(dir).filePath(QStringLiteral("meteohub-cache.sqlite"));

    m_store = std::make_unique<SampleStore>(dbPath, kChannels);
    if (!m_store->open()) {
        // Sans cache, le module ne peut rien faire d'utile : on échoue franchement
        // plutôt que de tourner en apparence tout en n'accumulant rien.
        m_store.reset();
        return false;
    }

    // Sans source configurée, le module reste valide mais inerte : il expose le
    // cache déjà constitué sans jamais le rafraîchir. Cela permet d'analyser un
    // historique déjà recopié même si l'appareil est hors service.
    if (!m_sourceUrl.isEmpty()) {
        m_collector = new MeteoHubCollector(m_sourceUrl, m_store.get(), this);
        // Première collecte immédiate : au démarrage du service, on ne fait pas
        // attendre une période de maintenance complète avant le premier import.
        QTimer::singleShot(0, this, &AnalyticsModule::maintainCache);
    }

    m_running = true;
    m_timer->start();
    return true;
}

void AnalyticsModule::stop() {
    m_running = false;
    m_timer->stop();
    if (m_store)
        m_store->close();
}

QJsonObject AnalyticsModule::statusJson() const {
    QJsonObject o;
    o["running"]        = m_running;
    o["altitude_m"]     = m_altitudeM;
    o["altitude_known"] = m_altitudeKnown;
    o["ts"]         = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    if (m_collector)
        o["collector"] = m_collector->statusJson();
    return o;
}

QJsonObject AnalyticsModule::analyze(const QJsonObject& request) const {
    AnalysisContext ctx;
    ctx.store     = m_store.get();
    ctx.altitudeM     = m_altitudeM;
    ctx.altitudeKnown = m_altitudeKnown;
    ctx.now       = QDateTime::currentSecsSinceEpoch();

    const QString type = request.value(QStringLiteral("type")).toString();
    QJsonObject result = m_analyses.run(type, ctx, request);
    result["ts"] = static_cast<double>(ctx.now);
    return result;
}

QJsonArray AnalyticsModule::analysisCatalog() const {
    return m_analyses.catalogJson();
}

void AnalyticsModule::maintainCache() {
    // Le collecteur ignore l'appel si un cycle est déjà en cours : une période de
    // maintenance plus courte qu'un rattrapage complet n'empile donc rien.
    if (m_collector)
        m_collector->sync();
    emit updated(id());
}

} // namespace morfanalytics
