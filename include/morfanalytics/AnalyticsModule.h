/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfanalytics/IModule.h"

class QTimer;

namespace morfanalytics {

// -----------------------------------------------------------------------------
// AnalyticsModule : moteur d'analyse (STUB d'amorçage).
//
// Conformément à la vision d'architecture de morfSystem :
//   - morfAnalytics ne possède JAMAIS la vérité des données : il travaille sur une
//     COPIE synchronisée (cache de travail), alimentée via morfSync. La source de
//     vérité reste MeteoHub (et les autres équipements). MeteoHub écrit, morfAnalytics
//     lit — jamais l'inverse.
//   - le cache est maintenu à jour en tâche de fond (ne récupérer que les nouvelles
//     mesures non encore transférées), mais les CALCULS LOURDS ne tournent pas en
//     permanence : ils sont exécutés uniquement à la demande (voir analyze()).
//   - une analyse ne renvoie qu'un RÉSULTAT synthétique (tendance, score, anomalie,
//     rapport…), jamais des milliers de points.
//
// Ce module est un point de départ : la logique réelle (synchronisation morfSync,
// algorithmes d'analyse) est marquée par des TODO.
//
// Paramètres (ModuleDef::params) :
//   "maintenance_ms" : période de maintenance du cache (défaut 60000).
//   "cache_dir"      : dossier du cache de travail (défaut : dossier courant).
// -----------------------------------------------------------------------------
class AnalyticsModule : public IModule {
    Q_OBJECT
public:
    AnalyticsModule(const QString& id, int maintenanceMs = 60000,
                    QString cacheDir = QString(), QObject* parent = nullptr);

    bool start() override;
    void stop() override;
    QJsonObject statusJson() const override;

    // Analyse À LA DEMANDE. Travaille uniquement sur le cache local (lecture seule)
    // et renvoie un résultat synthétique. `request` décrit l'analyse demandée
    // (p. ex. {"type":"trend","metric":"temp","window_days":30}).
    QJsonObject analyze(const QJsonObject& request) const;

private:
    void maintainCache(); // rafraîchit le cache via morfSync (TODO)

    int      m_maintenanceMs;
    QString  m_cacheDir;
    QTimer*  m_timer;
    bool     m_running = false;
    qint64   m_cachedPoints = 0; // nombre de mesures dans le cache de travail
    qint64   m_lastSyncTs = 0;   // horodatage de la dernière mesure synchronisée
};

} // namespace morfanalytics
