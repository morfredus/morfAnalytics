/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfanalytics/IModule.h"
#include "morfanalytics/analysis/AnalysisRegistry.h"
#include "morfanalytics/data/AnnotationStore.h"
#include "morfanalytics/analysis/MeteoQuality.h"
#include <QString>
#include <QJsonArray>
#include <memory>

class QTimer;

namespace morfanalytics {

class SampleStore;
class MeteoHubCollector;
class MeteoSyncPublisher;
class ForecastStore;
class ForecastCollector;

// -----------------------------------------------------------------------------
// AnalyticsModule : moteur d'analyse.
//
// Conformément à la vision d'architecture de morfSystem :
//   - morfAnalytics ne possède JAMAIS la vérité des données : il travaille sur une
//     COPIE locale (cache de travail) recopiée depuis l'appareil. La source de
//     vérité reste MeteoHub. MeteoHub écrit, morfAnalytics lit - jamais l'inverse,
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
//   "cache_dir"      : dossier du cache de travail (défaut : état persistant,
//                      $STATE_DIRECTORY sinon /var/lib/morfsystem/morfanalytics ;
//                      voir docs/FILESYSTEM.md).
//   "source_url"     : URL de base de MeteoHub, p. ex. "http://192.168.1.42".
//                      Si absent, aucune collecte n'est lancée.
//
// Aucune altitude ici : la pression reçue de MeteoHub est DÉJÀ ramenée au niveau
// de la mer par le hub, avec l'altitude de chaque capteur. Règle du parc : le
// composant qui connaît la réalité physique de la mesure la normalise ; les
// analyses consomment la donnée normalisée sans reconstituer l'installation.
//   "morfsync_url"   : URL de base du hub morfSync, p. ex. "http://127.0.0.1:8080".
//                      Si absent, aucune publication n'est faite (le service reste
//                      un moteur d'analyse local). Écriture SEULE vers morfSync.
//   "morfsync_token" : Bearer optionnel du hub (LAN de confiance : souvent vide).
// -----------------------------------------------------------------------------
class AnalyticsModule : public IModule {
    Q_OBJECT
public:
    AnalyticsModule(const QString& id, int maintenanceMs = 60000,
                    QString cacheDir = QString(), QString sourceUrl = QString(),
                    QString morfsyncUrl = QString(), QString morfsyncToken = QString(),
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

    // Série temporelle sous-échantillonnée d'une grandeur, pour l'onglet
    // Graphiques (« montrer ce que font les données », complément visuel des
    // analyses). `ctx` = "in"|"out" (une seule source ; l'UI combine IN+OUT en
    // deux appels), `metric` = "temp"|"hum"|"pres". Renvoie
    // { ctx, metric, ts:[...], v:[... | null] } : moyennes par tranche, une tranche
    // vide donne null (trou préservé, jamais comblé). ~maxPoints points au plus.
    QJsonObject seriesJson(const QString& ctx, const QString& metric,
                           qint64 from, qint64 to, int maxPoints) const;

    // Événements temporels de la fenêtre [from, to] (secondes epoch : période
    // glissante ou période de consultation libre), calculés par la SOURCE
    // COMMUNE (MeteoEvents) et partagés par les Graphiques (marqueurs + encart) et
    // les analyses (enrichissement de leur texte) :
    //   - croisements IN/OUT par grandeur (temp/hum/pres) : instant où les deux
    //     valeurs deviennent égales (nécessite les deux caches) ;
    //   - changements de tendance de chaque grandeur EXTÉRIEURE (météo) ;
    //   - changements de régime : plusieurs changements de tendance rapprochés.
    // Renvoie { from, to, hours, crossings:[...], trend_changes:[...], regime_changes:[...] }.
    QJsonObject eventsJson(qint64 from, qint64 to) const;

    // Diagnostic météo EXTÉRIEUR à l'instant `to` (maintenant si <= 0) : situation,
    // niveaux précipitations/brouillard/gel, changement probable de l'air,
    // convergence des signaux et explication chiffrée. Calcul dans MeteoDiagnosis.
    QJsonObject diagnosisJson(qint64 to) const;

    // Nettoyage du CACHE - et de lui seul : la source de vérité (l'appareil)
    // n'est jamais touchée, le collecteur n'émettant que des GET.
    // `request` : {"action": "invalidate_range" (+ from_ts, to_ts, channels[],
    //              dry_run) | "purge_all" | "collect_now"
    //              | "keep_point" / "unkeep_point" (+ ctx, channel, ts)
    //              | "list_kept"}.
    // La purge totale se reconstruit depuis l'appareil au cycle suivant. Les
    // points reintegres, eux, sont de l'ETAT : ils survivent a la purge.
    QJsonObject cleanupData(const QJsonObject& request);

    // -------------------------------------------------------------------------
    // Observations METEO humaines (annotations). Distinctes des mesures : ce sont
    // des evenements observes par l'utilisateur, rattaches a une periode, stockes
    // dans l'etat du service et non dans le cache reconstructible (voir
    // AnnotationStore). Le module meteo en est le proprietaire naturel.
    // -------------------------------------------------------------------------

    // Liste des observations + vocabulaire propose, pour que l'interface se
    // construise sans coder les types en dur : { "annotations":[...],
    // "known_types":[...] }.
    QJsonObject annotationsJson() const;

    // Cree ou met a jour une observation. Delegue la validation au store ; *code
    // porte le statut HTTP a renvoyer (200/400/404/500), *error le message humain.
    QJsonObject saveAnnotation(const QJsonObject& in, int* code, QString* error);

    // Supprime une observation par id. Meme convention *code/*error.
    QJsonObject deleteAnnotation(const QString& id, int* code, QString* error);

    // Type d'annotation qui ecarte les mesures EXTERIEURES de sa periode des
    // analyses (sonde rentree a l'interieur, sur l'etabli, en test).
    static constexpr const char* kExcludeAnnotationType = "sonde_hors_conditions";

private:
    QVector<meteo::TimeRange> outExclusions() const;

    // Points reintegres par l'utilisateur (« ce pic etait reel ») : la
    // qualification ne les ecarte plus. Fichier d'etat meteo-kept-points.json,
    // a cote des annotations, jamais dans le cache.
    meteo::KeptPoints keptFor(const QString& ctx) const { return m_kept.value(ctx); }
    void loadKept();
    bool saveKept() const;
    QString m_keptPath;
    QHash<QString, meteo::KeptPoints> m_kept; // ctx ("in"/"out") -> canal -> ts

    void maintainCache();

    int     m_maintenanceMs;
    QString m_cacheDir;
    QString m_sourceUrl;
    QString m_morfsyncUrl;
    QString m_morfsyncToken;
    QTimer* m_timer;
    bool    m_running = false;

    // Deux caches distincts, un par contexte météo (doctrine du store générique :
    // un flux = un cache). IN = confort intérieur (flux legacy, historique) ;
    // OUT = météo extérieure (sonde ESP-NOW). Les analyses météo viseront l'OUT
    // (étape suivante) ; l'IN reste la référence du confort.
    std::unique_ptr<SampleStore> m_store;     // IN (intérieur)
    std::unique_ptr<SampleStore> m_storeOut;  // OUT (extérieur)
    MeteoHubCollector*           m_collector = nullptr;    // IN, possédé via l'arbre QObject
    MeteoHubCollector*           m_collectorOut = nullptr; // OUT, possédé via l'arbre QObject
    MeteoSyncPublisher*          m_publisher = nullptr; // possédé via l'arbre QObject

    // Prévisions « day-ahead » archivées par l'appareil (étape 9 : prévu vs
    // observé). Cache et collecteur dédiés, distincts des mesures : un flux d'un
    // jour par prévision, réécrit au fil de la veille (UPSERT).
    std::unique_ptr<ForecastStore> m_forecastStore;
    ForecastCollector*             m_forecastCollector = nullptr; // possédé via l'arbre QObject
    AnalysisRegistry             m_analyses;

    // Observations humaines. Etat persistant, distinct du cache : cree des la
    // construction pour etre disponible meme avant start() (routes HTTP).
    std::unique_ptr<AnnotationStore> m_annotations;
};

} // namespace morfanalytics
