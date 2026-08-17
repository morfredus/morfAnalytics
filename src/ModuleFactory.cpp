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
        // Une altitude nulle etant legitime, c'est la PRESENCE de la cle qui
        // distingue "station au bord de mer" de "parametre oublie".
        const bool altitudeKnown = def.params.contains("altitude_m");
        const double altitudeM   = def.params.value("altitude_m").toDouble(0.0);
        // Publication (facultative) des synthèses journalières vers morfSync.
        const QString morfsyncUrl   = def.params.value("morfsync_url").toString();
        const QString morfsyncToken = def.params.value("morfsync_token").toString();
        return new AnalyticsModule(def.id, maintenanceMs, cacheDir, sourceUrl,
                                   altitudeM, altitudeKnown,
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
        // Découverte beacon des morfPhoto du parc (capacité photo_index) : la page
        // /photo laisse choisir plusieurs postes à analyser. Port du parc par défaut.
        const QJsonObject discovery = def.params.value("discovery").toObject();
        const bool discoveryEnabled = discovery.value("enabled").toBool(true);
        const quint16 discoveryPort =
            static_cast<quint16>(discovery.value("udp_port").toInt(45454));
        return new PhotoAnalyticsModule(def.id, sourceUrl, refreshMs, buckets, excludeCameras,
                                        discoveryPort, discoveryEnabled, parent);
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
            const QString cacheDir = def.params.value("cache_dir")
                .toString(QStringLiteral("/opt/morfanalytics/cache"));
            dbPath = cacheDir + QStringLiteral("/monitor.sqlite");
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

    if (error)
        *error = QStringLiteral("type de module inconnu : '%1'").arg(def.type);
    return nullptr;
}

QStringList knownTypes() {
    return { QStringLiteral("analytics"), QStringLiteral("photo"), QStringLiteral("monitor") };
}

} // namespace ModuleFactory
} // namespace morfanalytics
