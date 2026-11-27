#!/usr/bin/env python3
"""Genere src/pages/WorldOutline.cpp : les contours des pays, embarques dans la page Photo.

Source : Natural Earth, jeu « admin 0 - countries » a l'echelle 1:110 millions, domaine public
(https://www.naturalearthdata.com/about/terms-of-use/). Aucune attribution n'est exigee ; elle est
donnee dans le README par courtoisie.

Pourquoi embarquer : la page /photo dessine la carte sans AUCUN appel exterieur (les positions de la
photetheque ne quittent pas le parc). Un fond de contours suffit a situer les amas de photos ; il est
volontairement grossier (110 m : erreur de l'ordre de dizaines de km a l'echelle d'une ville).

Usage : python3 scripts/gen-world-outline.py <ne_110m_admin_0_countries.geojson>
"""
import json
import math
import sys

MIN_STEP = 0.12   # degres : on saute les points plus proches que ca du precedent (allege sans trahir la forme)


def simplify(ring):
    out = [ring[0]]
    for lon, lat in ring[1:-1]:
        if math.hypot(lon - out[-1][0], lat - out[-1][1]) >= MIN_STEP:
            out.append((lon, lat))
    out.append(ring[-1])
    return out


def main(path):
    data = json.load(open(path, encoding="utf-8"))
    rings = []
    for feature in data["features"]:
        geom = feature["geometry"]
        polygons = geom["coordinates"] if geom["type"] == "MultiPolygon" else [geom["coordinates"]]
        for polygon in polygons:
            for ring in polygon:      # contour exterieur et trous (lacs) : tous des lignes
                pts = simplify([(p[0], p[1]) for p in ring])
                if len(pts) >= 4:
                    flat = []
                    for lon, lat in pts:
                        flat += [round(lon, 2), round(lat, 2)]
                    rings.append(flat)
    blob = json.dumps(rings, separators=(",", ":"))
    # Un litteral C++ brut par tranche de 4000 caracteres : certains compilateurs bornent la taille d'un litteral.
    chunks = [blob[i:i + 4000] for i in range(0, len(blob), 4000)]
    lines = [
        "// FICHIER GENERE par scripts/gen-world-outline.py : ne pas editer a la main.",
        "// Contours des pays : Natural Earth 1:110m, domaine public.",
        '#include "morfanalytics/pages/WorldOutline.h"',
        "",
        "namespace morfanalytics::pages {",
        "",
        "QByteArray worldOutlineJson() {",
        "    QByteArray out;",
        "    out.reserve(%d);" % len(blob),
    ]
    for c in chunks:
        lines.append('    out += QByteArrayLiteral(R"OUTLINE(%s)OUTLINE");' % c)
    lines += ["    return out;", "}", "", "} // namespace morfanalytics::pages", ""]
    with open("src/pages/WorldOutline.cpp", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("%d contours, %d octets de JSON" % (len(rings), len(blob)))


if __name__ == "__main__":
    main(sys.argv[1])
