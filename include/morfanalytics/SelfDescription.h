/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfbeacon/PresenceConfig.h"

namespace morfanalytics {

// -----------------------------------------------------------------------------
// fillAnnouncedDetail : renseigne le DETAIL annonce du service -- interface web
// et liste d'API -- dans un PresenceConfig.
//
// Point UNIQUE, appele des deux cotes :
//   - Service.cpp, en construisant la config du heartbeat morfBeacon ;
//   - HttpServer::buildStatusJson, en servant /status (via describeService).
//
// Avant ce point unique, le bloc web_ui etait ecrit A LA MAIN aux deux endroits,
// avec les memes valeurs recopiees : la moindre modification d'un cote pouvait
// diverger de l'autre. Ici, l'interface et l'API qu'un observateur decouvre sont
// definies une seule fois.
//
// En-tete (inline) : aucun fichier source ni entree CMake supplementaires.
inline void fillAnnouncedDetail(morfbeacon::PresenceConfig& pc) {
    pc.webUiPath        = QStringLiteral("/");
    pc.webUiLabel       = QStringLiteral("Analyses");
    pc.webUiDescription = QStringLiteral(
        "Statistiques longue periode et correlations sur l'historique des equipements.");

    // API metier (les routes de cadre -- /status, /healthz, /modules -- ne sont
    // pas listees : un observateur les connait deja par le protocole). Chemins
    // a la racine, donc pas de prefixe commun.
    pc.api = {
        {QStringLiteral("GET"),  QStringLiteral("/analyses"),
         QStringLiteral("catalogue des analyses disponibles")},
        {QStringLiteral("POST"), QStringLiteral("/analyze"),
         QStringLiteral("executer une analyse sur l'historique")},
        {QStringLiteral("POST"), QStringLiteral("/data/cleanup"),
         QStringLiteral("purger la copie de travail locale")},
    };
}

} // namespace morfanalytics
