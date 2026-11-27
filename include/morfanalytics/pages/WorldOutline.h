/* Contours des pays (Natural Earth 1:110m, domaine public) embarques dans le binaire. */
#pragma once

#include <QByteArray>

namespace morfanalytics::pages {
// JSON : tableau de contours, chacun une liste plate [lon0, lat0, lon1, lat1, ...] en degres.
// Genere par scripts/gen-world-outline.py. Sert le fond de la carte de la page /photo, sans aucun appel
// exterieur.
QByteArray worldOutlineJson();
} // namespace morfanalytics::pages
