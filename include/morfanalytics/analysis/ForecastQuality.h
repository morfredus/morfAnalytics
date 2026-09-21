/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <vector>

namespace morfanalytics {
namespace forecastq {

// -----------------------------------------------------------------------------
// ForecastQuality : les indicateurs de QUALITE d'une prevision, et rien d'autre.
//
// Fonctions PURES : pas d'etat, pas d'acces au cache, pas de JSON. On les separe
// de MeteoMath (qui ne porte QUE des formules meteorologiques) parce que ce ne
// sont pas de la physique : ce sont des mesures d'ecart entre une prevision
// archivee et une observation. Mais elles partagent l'exigence de MeteoMath :
// une erreur y resterait plausible (un chiffre faux ne « saute pas aux yeux »),
// donc elles doivent pouvoir se verifier isolement contre des cas connus.
//
// Convention de signe, systematique : ecart = observe - prevu. Un biais POSITIF
// signifie donc que les observations ont ete en moyenne plus CHAUDES que ce qui
// avait ete prevu.
//
// Unite : tous les ecarts sont en degres Celsius.
// -----------------------------------------------------------------------------

// Resume statistique d'une serie d'ecarts signes (observe - prevu).
struct ErrorStats {
    int    count = 0;    // nombre de couples (prevu, observe) reellement compares
    double bias  = 0.0;  // ecart moyen SIGNE (+ = observe plus chaud que prevu)
    double mae   = 0.0;  // erreur absolue moyenne : l'indicateur de REFERENCE
    double rmse  = 0.0;  // racine de l'erreur quadratique moyenne (pese les gros ecarts)
    bool valid() const { return count > 0; }
};

// Agrege une liste d'ecarts signes (observe - prevu) en biais / MAE / RMSE.
// Ignore SILENCIEUSEMENT les valeurs non finies (NaN/inf) : une journee sans
// couple exploitable ne doit jamais contaminer les moyennes. Une liste vide (ou
// entierement non finie) renvoie un ErrorStats a zero, dont valid() est faux.
ErrorStats accumulate(const std::vector<double>& signedErrors);

// --- Seuils et parametres, CENTRALISES et documentes -------------------------
// Tout ce qui pourrait etre « ajuste un jour » vit ici, en un seul endroit, pour
// que la lecture de l'interface reste coherente avec le calcul du serveur.

// MAE (en degres Celsius) a laquelle l'indice de fiabilite atteint 0. En deca,
// l'indice decroit lineairement depuis 100 (MAE nulle). 6 degres : une erreur
// day-ahead de cette ampleur sur une temperature min/max n'a plus de valeur
// indicative, autant considerer la prevision comme sans qualite mesurable.
constexpr double kDefaultMaxMae = 6.0;

// Nombre minimal de journees comparees en dessous duquel on REFUSE d'afficher un
// indice : trois points ne prouvent rien, mais autorisent une premiere lecture
// prudente. En dessous, l'etat affiche est « Donnees insuffisantes », jamais une
// valeur par defaut inventee.
constexpr int kMinDaysForIndex = 3;

// Seuils de MAE (degres Celsius) delimitant les paliers qualitatifs. Fondes sur
// l'ERREUR REELLE (honnete et reliable au vecu), pas sur l'indice sur 100 :
//   MAE <= kMaeHigh      -> « Fiabilite elevee »
//   MAE <= kMaeCorrect   -> « Fiabilite correcte »
//   MAE <= kMaeVariable  -> « Fiabilite variable »
//   au-dela              -> « Fiabilite faible »
constexpr double kMaeHigh     = 1.0;
constexpr double kMaeCorrect  = 2.0;
constexpr double kMaeVariable = 3.5;

// Indice de fiabilite 0..100 derive de la MAE. Decroissance LINEAIRE et bornee :
//
//   index = 100 * max(0, 1 - mae / maxMae)
//
// Choisi pour etre trivial a expliquer -- chaque degre d'erreur coute un nombre
// fixe de points -- plutot que mathematiquement seduisant. Ce n'est PAS une
// probabilite de prevision correcte : c'est un score interne de qualite PASSEE,
// une autre facon de lire la MAE. La MAE en degres reste la reference.
// Renvoie une valeur arrondie a l'entier. Une MAE non finie ou negative, ou un
// maxMae non positif, renvoient 0.
double reliabilityIndex(double mae, double maxMae = kDefaultMaxMae);

// Libelle qualitatif PRUDENT a partir de la MAE et du nombre de jours compares.
// Renvoie « Donnees insuffisantes » tant que `evaluated` < kMinDaysForIndex (ou
// si la MAE n'est pas finie), quelle que soit la valeur de la MAE : on ne
// qualifie jamais une fiabilite sur trop peu de points.
QString reliabilityLabel(double mae, int evaluated);

} // namespace forecastq
} // namespace morfanalytics
