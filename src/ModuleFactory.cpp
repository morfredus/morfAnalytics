/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/ModuleFactory.h"
#include "morfanalytics/IModule.h"
#include "morfanalytics/AnalyticsModule.h"
#include "morfanalytics/PhotoAnalyticsModule.h"
#include "morfanalytics/MonitorModule.h"
#include "morfanalytics/GitHubAnalyticsModule.h"
#include "morfanalytics/StatePaths.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QStringList>

namespace morfanalytics {
namespace ModuleFactory {

// -----------------------------------------------------------------------------
// POUR AJOUTER UN MODULE METIER :
//   1. ecrire la classe (heriter d'IModule) ;
//   2. ajouter une branche dans create() qui lit ses parametres (def.params) ;
//   3. ajouter son nom dans knownTypes().
// Aucune autre partie du code (registre, serveur HTTP, service) ne change.
// -----------------------------------------------------------------------------

IModule* create(const ModuleDef& def, QString* error, QObject* parent) {
    const QString type = def.type.toLower();

    if (type == QLatin1String("analytics")) {
        const int maintenanceMs = def.params.value("maintenance_ms").toInt(60000);
        const QString cacheDir  = def.params.value("cache_dir").toString();
        const QString sourceUrl = def.params.value("source_url").toString();
        // `altitude_m` n'existe plus : MeteoHub publie une pression deja ramenee
        // au niveau de la mer. Une ancienne config qui le porte encore est
        // SIGNALEE (jamais appliquee) : l'altitude se regle dans MeteoHub.
        if (def.params.contains("altitude_m"))
            qWarning().noquote()
                << QStringLiteral("module %1 : parametre 'altitude_m' ignore - l'altitude "
                                  "des capteurs se declare dans MeteoHub (page Systeme), "
                                  "a retirer de la configuration").arg(def.id);
        // Publication (facultative) des synthèses journalières vers morfSync.
        const QString morfsyncUrl   = def.params.value("morfsync_url").toString();
        const QString morfsyncToken = def.params.value("morfsync_token").toString();
        return new AnalyticsModule(def.id, maintenanceMs, cacheDir, sourceUrl,
                                   morfsyncUrl, morfsyncToken, parent);
    }

    // Spécialisation Photo : lit les agrégats de morfPhoto et les interprète.
    if (type == QLatin1String("photo")) {
        const QString sourceUrl = def.params.value("source_url").toString();
        const int refreshMs     = def.params.value("refresh_ms").toInt(60000);
        // Règles de regroupement facultatives : tableau de [min, max, "libellé"].
        QVector<PhotoAnalyticsModule::FocalBucket> buckets;
        for (const QJsonValue& v : def.params.value("focal_buckets").toArray()) {
            const QJsonArray b = v.toArray();
            if (b.size() == 3)
                buckets.append({b[0].toDouble(), b[1].toDouble(), b[2].toString()});
        }
        // Périmètre de pratique (corpus ≠ pratique) : boîtiers exclus par politique.
        // La donnée reste souveraine dans morfPhoto ; exclure est une interprétation.
        QStringList excludeCameras;
        for (const QJsonValue& v : def.params.value("exclude_cameras").toArray())
            if (v.isString())
                excludeCameras << v.toString();
        // Liste blanche de pratique : filet /etc, souvent vide ; la page Configuration
        // l'enregistre ensuite dans l'état du service (modifiable sans toucher /etc).
        QStringList ownedCameras;
        for (const QJsonValue& v : def.params.value("owned_cameras").toArray())
            if (v.isString())
                ownedCameras << v.toString();
        const QJsonObject discovery = def.params.value("discovery").toObject();
        const bool discoveryEnabled = discovery.value("enabled").toBool(true);
        const quint16 discoveryPort =
            static_cast<quint16>(discovery.value("udp_port").toInt(45454));
        return new PhotoAnalyticsModule(def.id, sourceUrl, refreshMs, buckets, excludeCameras,
                                        ownedCameras, discoveryPort, discoveryEnabled, parent);
    }

    // Domaine Monitor : historise les métriques d'un ou plusieurs morfMonitor.
    if (type == QLatin1String("monitor")) {
        const int intervalMs = def.params.value("interval_ms").toInt(15000);
        QStringList sources;
        for (const QJsonValue& v : def.params.value("sources").toArray())
            if (v.isString())
                sources << v.toString();
        // Tolère aussi une source unique (source_url), comme les autres modules.
        const QString single = def.params.value("source_url").toString();
        if (!single.isEmpty() && !sources.contains(single))
            sources << single;
        // Emplacement du cache historique : db_path explicite, sinon dérivé de
        // cache_dir, sinon l'emplacement standard du service.
        QString dbPath = def.params.value("db_path").toString();
        if (dbPath.isEmpty()) {
            // Defaut = etat sous /var/lib (StateDirectory), pas /opt : voir StatePaths.h.
            // Un cache_dir explicite en config reste honore (surcharge volontaire).
            QString cacheDir = def.params.value("cache_dir").toString();
            if (cacheDir.isEmpty())
                cacheDir = stateDir();
            dbPath = QDir(cacheDir).filePath(QStringLiteral("monitor.sqlite"));
        }
        // Rétention des relevés bruts, en jours (0 => illimité). Étape simple avant
        // la compaction par paliers à venir.
        const int retentionDays = def.params.value("retention_days").toInt(90);
        // Découverte beacon des morfMonitor du parc. Le port par défaut est celui du
        // parc (45454) ; il n'est pas dans les params du module (c'est un réglage
        // global), on le laisse donc surchargeable ici pour les cas particuliers.
        const QJsonObject discovery = def.params.value("discovery").toObject();
        const bool discoveryEnabled = discovery.value("enabled").toBool(true);
        const quint16 discoveryPort =
            static_cast<quint16>(discovery.value("udp_port").toInt(45454));
        return new MonitorModule(def.id, sources, intervalMs, dbPath, retentionDays,
                                 discoveryPort, discoveryEnabled, parent);
    }

    if (type == QLatin1String("github")) {
        QStringList collectors;
        for (const QJsonValue& v : def.params.value("collectors").toArray())
            if (v.isString())
                collectors << v.toString();
        const QString single = def.params.value("source_url").toString();
        if (!single.isEmpty() && !collectors.contains(single))
            collectors << single;
        QString dbPath = def.params.value("db_path").toString();
        if (dbPath.isEmpty()) {
            // Defaut = etat sous /var/lib (voir StatePaths.h) ; cache_dir explicite honore.
            QString cacheDir = def.params.value("cache_dir").toString();
            if (cacheDir.isEmpty())
                cacheDir = stateDir();
            dbPath = QDir(cacheDir).filePath(QStringLiteral("github.sqlite"));
        }
        const int intervalMs = def.params.value("interval_ms").toInt(300000);
        const QJsonObject discovery = def.params.value("discovery").toObject();
        const bool discoveryEnabled = discovery.value("enabled").toBool(true);
        const quint16 discoveryPort =
            static_cast<quint16>(discovery.value("udp_port").toInt(45454));
        return new GitHubAnalyticsModule(def.id, collectors, dbPath, intervalMs,
                                         discoveryPort, discoveryEnabled, parent);
    }

    if (error)
        *error = QStringLiteral("type de module inconnu : '%1'").arg(def.type);
    return nullptr;
}

QStringList knownTypes() {
    return { QStringLiteral("analytics"), QStringLiteral("photo"),
             QStringLiteral("monitor"), QStringLiteral("github") };
}

} // namespace ModuleFactory
} // namespace morfanalytics
