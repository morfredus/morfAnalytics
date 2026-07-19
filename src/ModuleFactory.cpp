/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/ModuleFactory.h"
#include "morfanalytics/IModule.h"
#include "morfanalytics/AnalyticsModule.h"

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
        return new AnalyticsModule(def.id, maintenanceMs, cacheDir, parent);
    }

    // >>> AJOUTER D'AUTRES TYPES ICI (au fur et à mesure des analyses) <<<

    if (error)
        *error = QStringLiteral("type de module inconnu : '%1'").arg(def.type);
    return nullptr;
}

QStringList knownTypes() {
    return { QStringLiteral("analytics") };
}

} // namespace ModuleFactory
} // namespace morfanalytics
