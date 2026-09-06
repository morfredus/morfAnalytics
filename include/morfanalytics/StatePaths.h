/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>

namespace morfanalytics {

// Racine d'ETAT du service (doctrine morfSystem, docs/FILESYSTEM.md) : les caches
// SQLite sont de l'etat GENERE par le service, pas du programme (/opt) ni de la
// config (/etc). Sous systemd, l'unite declare StateDirectory=morfsystem/morfanalytics
// et la racine arrive via $STATE_DIRECTORY -- que systemd cree ET possede au nom du
// user du service. C'est precisement ce qui evite le piege « base en lecture seule »
// apres un changement d'utilisateur : sous /opt, les fichiers gardaient un ancien
// proprietaire ; sous /var/lib via StateDirectory, systemd garantit le bon.
// Repli conforme a l'OS hors systemd. Le dossier est cree ; il doit etre inscriptible.
QString stateDir();

} // namespace morfanalytics
