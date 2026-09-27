/* Thème commun des pages Web de morfAnalytics (sombre par défaut, clair au choix). */
#pragma once

#include <QByteArray>

namespace morfanalytics::pages {

// -----------------------------------------------------------------------------
// Une seule palette pour toutes les pages. Avant, chaque page recopiait la sienne
// (cinq copies qui divergeaient, et une page qui suivait le thème du système
// quand les autres restaient sombres).
//
// Chaque page porte trois marqueurs, remplacés par apply() :
//   <!--theme-head-->   dans <head> : palette (variables CSS des deux thèmes) et
//                       script qui pose le thème mémorisé AVANT l'affichage
//                       (pas de flash blanc au chargement) ;
//   <!--theme-toggle--> juste après la version : bouton soleil / lune ;
//   <!--nav-back-->     en tête de page : retour vers morfAnalytics et, quand
//                       elle est connue, vers l'application d'origine.
//
// Le choix est mémorisé dans le navigateur (localStorage), commun à toutes les
// pages. Basculer recharge la page : les graphiques SVG sont dessinés en
// JavaScript avec les couleurs lues dans la palette (mfaColor), un nouveau rendu
// est le moyen le plus sûr qu'aucun tracé ne garde les couleurs de l'autre thème.
// -----------------------------------------------------------------------------
class Theme {
public:
    static QByteArray headBlock();
    static QByteArray toggleButton();
    static QByteArray navBlock();

    // Remplace les marqueurs dans une page complète.
    static QByteArray apply(QByteArray page);
};

}  // namespace morfanalytics::pages
