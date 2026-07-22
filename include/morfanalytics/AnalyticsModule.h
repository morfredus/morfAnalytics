/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfanalytics/IModule.h"
#include "morfanalytics/analysis/AnalysisRegistry.h"
#include <QString>
#include <QJsonArray>
#include <memory>

class QTimer;

namespace morfanalytics {

class SampleStore;
class MeteoHubCollector;

// -----------------------------------------------------------------------------
// AnalyticsModule : moteur d'analyse.
//
// Conformément à la vision d'architecture de morfSystem :
//   - morfAnalytics ne possède JAMAIS la vérité des données : il travaille sur une
//     COPIE locale (cache de travail) recopiée depuis l'appareil. La source de
//     vérité reste MeteoHub. MeteoHub écrit, morfAnalytics lit — jamais l'inverse,
//     ce que garantit le collecteur, qui n'émet que des requêtes GET.
//   - le cache est maintenu à jour en tâche de fond, en ne récupérant que les
//     mesures non encore présentes sur le Raspberry Pi, mais les CALCULS LOURDS ne
//     tournent pas en permanence : ils sont exécutés à la demande (voir analyze()).
//   - une analyse ne renvoie qu'un RÉSULTAT synthétique (tendance, score, anomalie,
//     rapport…), jamais des milliers de points.
//
// État d'avancement : la collecte incrémentale est opérationnelle ; les algorithmes
// d'analyse (analyze()) restent à écrire.
//
// Paramètres (ModuleDef::params) :
//   "maintenance_ms" : période de rafraîchissement du cache (défaut 60000).
//   "cache_dir"      : dossier du cache de travail (défaut : dossier courant).
//   "source_url"     : URL de base de MeteoHub, p. ex. "http://192.168.1.42".
//                      Si absent, aucune collecte n'est lancée.
//   "altitude_m"     : altitude de la station, en mètres. Sert à ramener la
//                      pression au niveau de la mer. Une altitude nulle est une
//                      valeur valide ; c'est l'ABSENCE du paramètre qui est
//                      signalée dans les analyses de pression.
// -----------------------------------------------------------------------------
class AnalyticsModule : public IModule {
    Q_OBJECT
public:
    AnalyticsModule(const QString& id, int maintenanceMs = 60000,
                    QString cacheDir = QString(), QString sourceUrl = QString(),
                    double altitudeM = 0.0, bool altitudeKnown = false,
                    QObject* parent = nullptr);
    ~AnalyticsModule() override;

    bool start() override;
    void stop() override;
    QJsonObject statusJson() const override;

    // Analyse À LA DEMANDE. Travaille uniquement sur le cache local (lecture seule)
    // et renvoie un résultat synthétique. `request` décrit l'analyse demandée
    // (p. ex. {"type":"degree_days","days":365}).
    QJsonObject analyze(const QJsonObject& request) const;

    // Catalogue des analyses disponibles, pour que l'interface se construise
    // sans les connaître à l'avance.
    QJsonArray analysisCatalog() const;

    // Nettoyage du CACHE — et de lui seul : la source de vérité (l'appareil)
    // n'est jamais touchée, le collecteur n'émettant que des GET.
    // `request` : {"action": "scan_faults" | "invalidate_faults"
    //              | "invalidate_range" (+ from_ts, to_ts, channels[])
    //              | "purge_all"}.
    // Les scans comptent sans modifier ; la purge totale se reconstruit depuis
    // l'appareil au cycle de collecte suivant.
    QJsonObject cleanupData(const QJsonObject& request);

private:
    void maintainCache();

    int     m_maintenanceMs;
    QString m_cacheDir;
    QString m_sourceUrl;
    double  m_altitudeM;
    bool    m_altitudeKnown;
    QTimer* m_timer;
    bool    m_running = false;

    std::unique_ptr<SampleStore> m_store;
    MeteoHubCollector*           m_collector = nullptr; // possédé via l'arbre QObject
    AnalysisRegistry             m_analyses;
};

} // namespace morfanalytics
