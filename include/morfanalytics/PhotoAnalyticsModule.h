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
#include <QHash>

class QTimer;
class QNetworkAccessManager;
class QNetworkReply;
class QUdpSocket;

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
                         QStringList ownedCameras = {},
                         quint16 discoveryUdpPort = 45454, bool discoveryEnabled = true,
                         QObject* parent = nullptr);
    ~PhotoAnalyticsModule() override;

    bool start() override;
    void stop() override;
    QJsonObject statusJson() const override;

    QJsonObject snapshot() const { return m_snapshot; }

    // Périmètre « boîtiers possédés » (liste blanche). Vit dans l'état du service
    // (/var/lib), pas dans /etc : la page Configuration doit pouvoir le modifier.
    QStringList ownedCameras() const { return m_ownedCameras; }
    bool setOwnedCameras(QStringList names);

    // Handoff (PhotoHub → /photo?source=) : rapatrie À LA DEMANDE le dataset d'une
    // AUTRE instance morfPhoto que celle configurée, et renvoie un instantané de la
    // même forme que snapshot(). Fetch synchrone borné (boucle d'événements imbriquée
    // avec délai maximal) : la page ne pilote alors pas la source périodique du module,
    // elle analyse la photothèque désignée. Vide/injoignable => reachable=false.
    QJsonObject fetchNow(const QString& sourceUrl) const;

    // Postes morfPhoto CONNUS : sources déclarées (config) + découvertes par beacon
    // (capacité photo_index). [{ url, host, online, configured }]. La page /photo s'en
    // sert pour proposer les cases à cocher des postes à inclure dans l'analyse.
    QJsonArray availableSources() const;

    // Analyse MULTI-SOURCES : rapatrie le dataset de chaque source, FUSIONNE en un seul
    // (dictionnaires unifiés, dossiers préfixés par poste) et DÉDOUBLONNE par empreinte
    // (une photo indexée sur deux postes -- même CD -- n'est comptée qu'une fois).
    // Renvoie un instantané de la même forme que snapshot(), plus `sources` (état par
    // poste) et `duplicates_removed`. Fetch synchrone borné, source par source.
    QJsonObject fetchMerged(const QStringList& sources) const;

    // Regroupe des focales brutes ({focal_length,count}) selon les règles. Pur et
    // statique : testable sans réseau (cœur de l'interprétation).
    static QJsonArray groupFocals(const QJsonArray& rawFocals,
                                  const QVector<FocalBucket>& buckets);

    // Jeu de règles par défaut si la config n'en fournit pas.
    static QVector<FocalBucket> defaultBuckets();

private slots:
    void onBeaconDatagram();

private:
    void refresh();
    void fetch(const QString& path, const QString& key);
    void onReply(const QString& key, QNetworkReply* reply);
    void finalize();
    // Rapatrie le `dataset` d'UNE source (synchrone borné). *ok/false + *error sinon.
    QJsonObject fetchDatasetSync(const QString& sourceUrl, bool* ok, QString* error) const;
    QJsonObject fetchJsonSync(const QString& sourceUrl, const QString& path,
                              int timeoutMs, bool* ok, QString* error) const;
    void attachPractice(QJsonObject& snap) const;
    static QString stateDir();
    QString practicePath() const;
    void loadPractice(const QStringList& configOwned);
    bool savePractice() const;
    void pruneDiscovered(qint64 nowSec);
    // Nom LISIBLE d'un poste depuis son URL : le hostname annoncé par le beacon si
    // connu (pi4fred, macbooklinux…), sinon l'hôte de l'URL (souvent une IP, moins
    // parlante). Sert les étiquettes de la page et les préfixes de dossiers fusionnés.
    QString hostForUrl(const QString& url) const;

    QString              m_sourceUrl;
    int                  m_refreshMs;
    QVector<FocalBucket> m_buckets;
    QStringList          m_excludeCameras;   // boîtiers hors pratique (politique /etc)
    QStringList          m_ownedCameras;     // boîtiers possédés (état, modifiable)

    QTimer*                m_timer = nullptr;
    QNetworkAccessManager* m_net = nullptr;
    int                    m_pending = 0;      // requêtes en vol du cycle courant
    QJsonObject            m_partial;          // réponses accumulées du cycle
    QString                m_cycleError;
    QJsonObject            m_snapshot;         // dernier instantané publié

    // Découverte beacon des morfPhoto (capacité photo_index), comme le domaine Monitor
    // découvre les morfMonitor. Une base par machine ; la page choisit lesquelles inclure.
    quint16                m_discoveryPort;
    bool                   m_discoveryEnabled;
    QUdpSocket*            m_beacon = nullptr;
    // url morfPhoto -> { dernier heartbeat (s Unix), hostname annoncé }. Le hostname
    // rend l'affichage lisible (pi4fred plutôt que 192.168.1.x).
    struct Discovered { qint64 lastSeen = 0; QString host; };
    QHash<QString, Discovered> m_discovered;
    static constexpr qint64 kDiscoveryForgetAfterS = 86400;   // 24 h sans annonce -> oubli
};

} // namespace morfanalytics
