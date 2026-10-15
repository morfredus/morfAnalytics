/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include "morfanalytics/analysis/SiteWatchInsights.h"

#include <QDate>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

namespace morfanalytics {
namespace github {

// -----------------------------------------------------------------------------
// GitHubInsights : regles d'alerte sur le trafic d'un depot GitHub.
//
// Fonctions PURES, comme SiteWatchInsights : aucune E/S, la date du jour et la
// configuration sont des parametres. Elles lisent le JSON que renvoie
// GitHubStore::repository() (champs `full_name`, `last_release`, `daily` = liste de
// {day, metric, count}). Les types RuleSetting et Alert sont ceux de SiteWatch : les
// alertes GitHub partagent le meme stockage, le meme suivi d'envoi et la meme sourdine.
// -----------------------------------------------------------------------------

struct AlertConfig {
    double dropPct = 50;        // baisse des vues (7 j contre 7 j precedents) qui declenche l'alerte
    double spikeFactor = 3;     // jour > facteur x mediane des 14 jours precedents
    int minViews = 30;          // volume plancher : en dessous, une variation n'est que du bruit
    int staleWarnDays = 3;      // aucune donnee depuis N jours : avertissement
    int staleErrorDays = 7;     // ... erreur
    QMap<QString, sitewatch::RuleSetting> rules;
    QMap<QString, QString> muted;   // regle -> date ISO « jusqu'au » incluse ; « * » = toutes

    sitewatch::RuleSetting rule(const QString& id) const { return rules.value(id); }
    bool isMuted(const QString& id, const QDate& today) const;
};

QVector<sitewatch::RuleInfo> ruleCatalog();
AlertConfig alertConfigFromJson(const QJsonObject& o);
QJsonObject alertConfigToJson(const AlertConfig& c);

// Alertes d'un depot a la date `today`. Un historique ancien ne declenche rien : seuls les
// derniers jours complets (jusqu'a la veille) sont juges.
QVector<sitewatch::Alert> evaluateAlerts(const QJsonObject& repo, const QDate& today,
                                         const AlertConfig& cfg = AlertConfig());

} // namespace github
} // namespace morfanalytics
