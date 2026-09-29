/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>

namespace morfanalytics {
namespace meteo {

// -----------------------------------------------------------------------------
// MeteoMath : les formules meteorologiques, et rien d'autre.
//
// Fonctions PURES : pas d'etat, pas d'acces au cache, pas de JSON. C'est
// volontaire - ce sont les seules parties du moteur ou une erreur est silencieuse
// (un resultat faux reste plausible), donc les seules qui doivent pouvoir se
// verifier isolement contre des valeurs de reference.
//
// Unites, systematiquement : temperature en degres Celsius, humidite relative en
// pourcent, pression en hectopascals.
// -----------------------------------------------------------------------------

// Point de rosee (formule de Magnus-Tetens). Temperature a laquelle l'air doit
// etre refroidi pour atteindre la saturation : c'est le seuil de condensation,
// donc l'indicateur du risque de buee et de moisissure.
double dewPoint(double tempC, double humidityPct);

// Humidite absolue, en grammes de vapeur d'eau par metre cube. Contrairement a
// l'humidite relative, elle ne depend pas de la temperature : c'est elle qui dit
// si l'air contient reellement plus d'eau qu'hier.
double absoluteHumidity(double tempC, double humidityPct);

// Humidex (Environnement Canada) : temperature ressentie par combinaison de la
// chaleur et de l'humidite. N'a de sens qu'au-dessus d'environ 20 degres ; en
// dessous, la fonction renvoie la temperature telle quelle.
double humidex(double tempC, double humidityPct);

// Deficit de pression de vapeur (kPa) : mesure de la capacite de l'air a
// assecher la vegetation. Zero signifie air sature ; la valeur augmente avec
// la chaleur et la secheresse de l'air.
double vaporPressureDeficit(double tempC, double humidityPct);

// Code de tendance barometrique de l'OMM, evalue sur 3 heures. Renvoie un
// libelle ("hausse rapide", "baisse lente", "stable"...).
QString pressureTendencyLabel(double deltaHpa3h);

// Previsions Zambretti : algorithme classique des stations personnelles. A
// partir de la pression reduite au niveau de la mer, de sa tendance sur 3 heures
// et de la saison, il produit l'une de 26 previsions textuelles.
//
// Limites assumees : concu pour les latitudes temperees de l'hemisphere nord, il
// ignore le vent (non mesure ici) et ne vaut que pour les 12 a 24 heures a venir.
// C'est une indication locale, pas un bulletin.
//
// `month` va de 1 a 12 et sert a la correction saisonniere.
QString zambrettiForecast(double seaLevelHpa, double delta3h, int month);

} // namespace meteo
} // namespace morfanalytics
