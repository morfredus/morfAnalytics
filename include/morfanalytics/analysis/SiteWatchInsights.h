/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace morfanalytics {
namespace sitewatch {

// -----------------------------------------------------------------------------
// SiteWatchInsights : l'analyse approfondie d'un site, calculee a partir du rapport
// que SiteWatch publie (series quotidiennes consolidees + classements).
//
// Fonctions PURES, comme MeteoEvents : aucune E/S, aucun etat, la date du jour et la
// configuration sont des PARAMETRES (testable). Le JSON de l'API, la page et les alertes
// consomment ces memes calculs : une anomalie ou une regle d'alerte est ecrite UNE fois.
//
// Series lues dans report.stats : daily_humans, daily_bots, daily_ai, daily_seo,
// daily_404, daily_403, daily_500, daily_attacks, daily_normal ("AAAA-MM-JJ" -> n), et
// pour les analyses recentes : top_404, hourly, pages_recent/prior, bots_recent/prior.
// -----------------------------------------------------------------------------

// Reglage d'une regle d'alerte.
struct RuleSetting {
    bool enabled = true;     // regle evaluee ou non
    QString level;           // "" = niveau par defaut de la regle ; sinon info|warning|error
    bool notify = true;      // envoyer a morfNotify (une alerte non envoyee reste visible)
    QStringList targets;     // destinations morfNotify (telegram, mail...) ; vide = destinations globales
};

// Configuration des alertes et de l'analyse. Valeurs par defaut = comportement d'origine.
struct AlertConfig {
    double sensitivity = 3.5;      // ecart robuste (en « sigmas ») au-dela duquel un jour est anormal
    int recentDays = 3;            // nombre de jours complets juges
    int staleWarnDays = 2;         // retard de SiteWatch : avertissement
    int staleErrorDays = 3;        // retard de SiteWatch : erreur
    double botSharePoints = 15;    // hausse de la part des robots (points) sur 7 jours
    double aiGrowthFactor = 2;     // multiplication des robots IA sur 7 jours
    int e500ErrorAt = 10;          // erreurs 500 dans la journee : niveau erreur a partir de la
    QMap<QString, RuleSetting> rules;   // par identifiant de regle ; absent = reglage par defaut
    QMap<QString, QString> muted;       // regle -> date ISO « jusqu'au » incluse ; « * » = toutes

    RuleSetting rule(const QString& id) const { return rules.value(id); }
    bool isMuted(const QString& id, const QDate& today) const;
};

// Description d'une regle, pour l'interface de configuration.
struct RuleInfo { QString id; QString label; QString description; QString defaultLevel; };
QVector<RuleInfo> ruleCatalog();

// Une alerte candidate. `key` est stable : la meme situation donne la meme cle, ce
// qui permet de ne la notifier qu'une fois (voir HttpServer).
struct Alert {
    QString key;       // ex. "e500|<site>|2026-10-09"
    QString rule;      // identifiant de la regle (e500, attack_spike, stale_data...)
    QString level;     // "info" | "warning" | "error"
    QString day;       // jour concerne (AAAA-MM-JJ)
    QString title;
    QString message;
    bool notify = true;   // la regle autorise l'envoi
    QStringList targets;  // destinations propres a la regle (vide = globales)
    bool muted = false;   // regle en sourdine : enregistree, jamais envoyee
};

// Lecture et ecriture de la configuration en JSON, avec bornes de securite (une valeur
// aberrante saisie dans l'interface ne doit jamais desactiver la detection par accident).
// Nettoie une liste de noms de destinations : sans doublon, 64 caracteres et 20 noms au plus.
QStringList sanitizeTargets(const QJsonArray& a);
AlertConfig alertConfigFromJson(const QJsonObject& o);
QJsonObject alertConfigToJson(const AlertConfig& c);
// Applique les ecarts propres a un site par-dessus la configuration globale.
AlertConfig mergeSiteConfig(const AlertConfig& global, const QJsonObject& siteOverride);

// Famille d'un robot : "ia" | "seo" | "moteur" | "social" | "autre".
QString botCategory(const QString& engine);

// Analyse du site sur la fenetre [from, to]. Fenetre invalide : les 30 derniers
// jours de donnees. La periode PRECEDENTE (meme longueur, juste avant) sert aux
// comparaisons.
QJsonObject insights(const QJsonObject& report, const QDate& from, const QDate& to,
                     const QDate& today, const AlertConfig& cfg = AlertConfig());

// Regles d'alerte evaluees sur les JOURS RECENTS (les derniers jours complets) : un
// historique ancien ne declenche jamais rien.
QVector<Alert> evaluateAlerts(const QJsonObject& report, const QDate& today,
                              const AlertConfig& cfg = AlertConfig());

} // namespace sitewatch
} // namespace morfanalytics
