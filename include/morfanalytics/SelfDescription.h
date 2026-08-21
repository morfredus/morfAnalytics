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
    // Annoncer le PORTAIL (racine), pas une specialisation : morfAnalytics est un
    // service multi-domaines (Meteo, SiteWatch, Photo...). Un consommateur (ex.
    // morfMonitor) doit ouvrir la racine qui liste les specialisations, et non une
    // page precise. Chaque specialisation reste accessible sous son propre chemin.
    pc.webUiPath        = QStringLiteral("/");
    pc.webUiLabel       = QStringLiteral("Analyses");
    pc.webUiDescription = QStringLiteral(
        "Portail d'analyses avancees : statistiques longue periode et correlations "
        "par domaine (meteo, journaux Web, GitHub, photo...).");

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
        {QStringLiteral("GET"),  QStringLiteral("/github"),
         QStringLiteral("analyses GitHub (vues, clones, telechargements)")},
        {QStringLiteral("GET"),  QStringLiteral("/github/data"),
         QStringLiteral("JSON des analyses GitHub (filtres repo, from, to)")},
        {QStringLiteral("POST"), QStringLiteral("/github/ingest"),
         QStringLiteral("recevoir la verite GitHub consolidee par SiteWatch")},
        {QStringLiteral("GET"),  QStringLiteral("/photo"),
         QStringLiteral("analyses de la phototheque (morfPhoto)")},
        {QStringLiteral("GET"),  QStringLiteral("/photo/data"),
         QStringLiteral("JSON phototheque fusionnee (sources)")},
        {QStringLiteral("GET"),  QStringLiteral("/photo/practice"),
         QStringLiteral("boitiers possedes (perimetre de pratique)")},
        {QStringLiteral("POST"), QStringLiteral("/photo/practice"),
         QStringLiteral("enregistrer les boitiers possedes")},
    };
}

} // namespace morfanalytics
