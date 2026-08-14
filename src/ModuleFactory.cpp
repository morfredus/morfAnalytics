/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/ModuleFactory.h"
#include "morfanalytics/IModule.h"
#include "morfanalytics/AnalyticsModule.h"
#include "morfanalytics/PhotoAnalyticsModule.h"

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
        return new PhotoAnalyticsModule(def.id, sourceUrl, refreshMs, buckets, excludeCameras, parent);
    }

    if (error)
        *error = QStringLiteral("type de module inconnu : '%1'").arg(def.type);
    return nullptr;
}

QStringList knownTypes() {
    return { QStringLiteral("analytics"), QStringLiteral("photo") };
}

} // namespace ModuleFactory
} // namespace morfanalytics
