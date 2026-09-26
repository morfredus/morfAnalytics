/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QVector>
#include <QtGlobal>
#include "morfanalytics/data/Series.h"

namespace morfanalytics {
namespace meteo {

// -----------------------------------------------------------------------------
// MeteoQuality : QUALIFICATION des mesures, jamais destruction.
//
// Principe (Fred, 2026-09-26) : la sonde mesure ce qu'elle mesure ; la
// qualification se fait plus haut dans la chaine. Ici, on ne modifie pas le
// cache : on calcule, A LA LECTURE, quels points ne doivent pas entrer dans les
// analyses, et pourquoi. Les analyses et la detection d'evenements ne voient
// alors que les points valides ; les graphiques montrent les points ecartes,
// marques, avec leur motif. La donnee brute reste intacte et consultable.
//
// Trois motifs, cumulables (drapeaux) :
//   - BORNES : valeur physiquement impossible (capteur en defaut) ;
//   - PIC ISOLE : le point s'ecarte de ses DEUX voisins, dans le meme sens, au-
//     dela d'un seuil, alors que ces voisins s'accordent entre eux. C'est le
//     critere du retour en arriere : une vraie variation meteo persiste, un
//     artefact (capteur rechauffe au reveil, mesure isolee aberrante) revient.
//     Les voisins doivent etre proches dans le temps : un trou d'acquisition ne
//     fabrique pas de faux pic ;
//   - EXCLUSION MANUELLE : periode signalee par une annotation « sonde hors
//     conditions » (sonde rentree a l'interieur, sur l'etabli, en test). Aucune
//     regle automatique ne distingue sans risque une sonde a l'interieur d'une
//     vraie journee ou interieur et exterieur se ressemblent : c'est a l'humain
//     de le dire.
//
// Seuils volontairement PRUDENTS : mieux vaut laisser passer un petit artefact
// que d'ecarter une vraie variation rapide (orage, passage nuageux). Ils sont
// centralises ici pour etre ajustes apres quelques semaines d'observation.
// -----------------------------------------------------------------------------

enum QualityFlag : quint8 {
    QualityOk       = 0,
    QualityBounds   = 1,  // hors bornes physiques
    QualitySpike    = 2,  // pic isole (aller-retour)
    QualityExcluded = 4,  // periode exclue par annotation
};

struct QualityRules {
    double spikeTemp = 3.0;          // °C
    double spikeHum  = 12.0;         // %
    double spikePres = 1.5;          // hPa
    qint64 maxNeighbourGapS = 900;   // voisins a 15 min au plus (3 cadences)
};

// Periode [from, to] (secondes Unix, bornes incluses).
struct TimeRange {
    qint64 from = 0;
    qint64 to = 0;
};

// Un point ecarte, pour l'affichage et le bilan.
struct FlaggedPoint {
    qint64 ts = 0;
    QString channel;
    double value = 0.0;
    quint8 flags = QualityOk;
};

// Drapeaux d'un canal. `channel` = "temp" | "hum" | "pres" (un canal inconnu
// n'a ni bornes ni seuil : seule l'exclusion s'y applique). Un point manquant
// (NaN) reste QualityOk : il n'y a rien a qualifier.
QVector<quint8> qualifyChannel(const QString& channel, const QVector<qint64>& ts,
                               const QVector<double>& values,
                               const QVector<TimeRange>& exclusions,
                               const QualityRules& rules = QualityRules());

// Qualifie tous les canaux de `s` et remplace les points ecartes par NaN (les
// analyses les voient alors comme des trous). Renvoie les points ecartes, dans
// l'ordre des canaux puis du temps, avec leur valeur d'origine.
QVector<FlaggedPoint> applyQuality(Series& s, const QVector<TimeRange>& exclusions,
                                   const QualityRules& rules = QualityRules());

// Motif lisible (le plus grave si plusieurs) : "exclusion" | "bornes" | "pic".
const char* qualityReasonCode(quint8 flags);

} // namespace meteo
} // namespace morfanalytics
