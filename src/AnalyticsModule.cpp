/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/AnalyticsModule.h"

#include <QTimer>
#include <QDateTime>

namespace morfanalytics {

AnalyticsModule::AnalyticsModule(const QString& id, int maintenanceMs,
                                 QString cacheDir, QObject* parent)
    : IModule(id, QStringLiteral("analytics"), parent),
      m_maintenanceMs(maintenanceMs > 0 ? maintenanceMs : 60000),
      m_cacheDir(std::move(cacheDir)),
      m_timer(new QTimer(this)) {
    m_timer->setInterval(m_maintenanceMs);
    connect(m_timer, &QTimer::timeout, this, &AnalyticsModule::maintainCache);
}

bool AnalyticsModule::start() {
    m_running = true;
    m_timer->start();
    // TODO : ouvrir/charger le cache de travail (m_cacheDir), reprendre m_lastSyncTs.
    return true;
}

void AnalyticsModule::stop() {
    m_running = false;
    m_timer->stop();
    // TODO : flush du cache si nécessaire.
}

QJsonObject AnalyticsModule::statusJson() const {
    QJsonObject o;
    o["running"]       = m_running;
    o["cached_points"] = static_cast<double>(m_cachedPoints);
    o["last_sync_ts"]  = static_cast<double>(m_lastSyncTs);
    o["ts"]            = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return o;
}

QJsonObject AnalyticsModule::analyze(const QJsonObject& request) const {
    // >>> MÉTIER À IMPLÉMENTER (valeur ajoutée de morfAnalytics) <<<
    // Selon request["type"] : tendance, corrélation, détection d'anomalie,
    // tendance saisonnière, comparaison d'équipements, rapport de synthèse…
    // Contraintes :
    //   - travailler UNIQUEMENT sur le cache local (copie lecture seule) ;
    //   - ne jamais modifier les mesures d'origine (MeteoHub = source de vérité) ;
    //   - ne renvoyer qu'un résultat synthétique (pas de flot de points).
    QJsonObject r;
    r["ok"]        = false;
    r["status"]    = QStringLiteral("not_implemented");
    r["requested"] = request.value(QStringLiteral("type"));
    r["ts"]        = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    return r;
}

void AnalyticsModule::maintainCache() {
    // >>> À IMPLÉMENTER : rafraîchir le cache via morfSync en ne récupérant que les
    // NOUVELLES mesures (depuis m_lastSyncTs). Ici : simple pouls de présence. <<<
    emit updated(id());
}

} // namespace morfanalytics
