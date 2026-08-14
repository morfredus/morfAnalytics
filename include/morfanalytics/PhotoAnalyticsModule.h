/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfanalytics/IModule.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

namespace morfanalytics {

// -----------------------------------------------------------------------------
// PhotoAnalyticsModule : spécialisation Photo du service d'analyse.
//
// Conformément à la vision morfSystem, morfAnalytics ne possède JAMAIS la donnée :
// la source de vérité de la photothèque est morfPhoto. Ce module LIT ses agrégats
// via son API HTTP (/api/v1), n'émet que des GET, ne touche jamais au disque ni à
// la base de morfPhoto. L'INTERPRÉTATION vit ici (regroupement des focales brutes
// en focales usuelles), jamais dans morfPhoto qui reste souverain sur les valeurs.
//
// Modèle « pull » (comme la collecte Meteo) : rafraîchissement périodique en tâche
// de fond vers un instantané en mémoire ; la page /photo lit cet instantané, sans
// bloquer le serveur ni interroger morfPhoto à chaque requête.
//
// Paramètres (ModuleDef::params) :
//   "source_url"    : base de l'API morfPhoto, p. ex. "http://127.0.0.1:8793".
//                     Si absent, aucun pull ; la page indique la source manquante.
//   "refresh_ms"    : période de rafraîchissement (défaut 60000).
//   "focal_buckets" : règles de regroupement, tableau de [min, max, "libellé"].
//                     Configurable et documenté ; défaut fourni. Une focale hors
//                     de toute plage tombe dans « autres ». Les valeurs BRUTES ne
//                     sont jamais remplacées : le regroupement est une lecture.
// -----------------------------------------------------------------------------
class PhotoAnalyticsModule : public IModule {
    Q_OBJECT
public:
    // Une plage de regroupement de focales : [min, max] -> libellé.
    struct FocalBucket { double min; double max; QString label; };

    PhotoAnalyticsModule(const QString& id, QString sourceUrl, int refreshMs,
                         QVector<FocalBucket> buckets, QStringList excludeCameras = {},
                         QObject* parent = nullptr);
    ~PhotoAnalyticsModule() override;

    bool start() override;
    void stop() override;
    QJsonObject statusJson() const override;

    // Instantané interprété, lu par la page /photo. Toujours sûr, même avant le
    // premier pull (reachable=false).
    QJsonObject snapshot() const { return m_snapshot; }

    // Handoff (PhotoHub → /photo?source=) : rapatrie À LA DEMANDE le dataset d'une
    // AUTRE instance morfPhoto que celle configurée, et renvoie un instantané de la
    // même forme que snapshot(). Fetch synchrone borné (boucle d'événements imbriquée
    // avec délai maximal) : la page ne pilote alors pas la source périodique du module,
    // elle analyse la photothèque désignée. Vide/injoignable => reachable=false.
    QJsonObject fetchNow(const QString& sourceUrl) const;

    // Regroupe des focales brutes ({focal_length,count}) selon les règles. Pur et
    // statique : testable sans réseau (cœur de l'interprétation).
    static QJsonArray groupFocals(const QJsonArray& rawFocals,
                                  const QVector<FocalBucket>& buckets);

    // Jeu de règles par défaut si la config n'en fournit pas.
    static QVector<FocalBucket> defaultBuckets();

private:
    void refresh();
    void fetch(const QString& path, const QString& key);
    void onReply(const QString& key, QNetworkReply* reply);
    void finalize();

    QString              m_sourceUrl;
    int                  m_refreshMs;
    QVector<FocalBucket> m_buckets;
    QStringList          m_excludeCameras;   // boîtiers hors pratique (politique service)

    QTimer*                m_timer = nullptr;
    QNetworkAccessManager* m_net = nullptr;
    int                    m_pending = 0;      // requêtes en vol du cycle courant
    QJsonObject            m_partial;          // réponses accumulées du cycle
    QString                m_cycleError;
    QJsonObject            m_snapshot;         // dernier instantané publié
};

} // namespace morfanalytics
