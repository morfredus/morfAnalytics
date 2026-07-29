/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QJsonObject>
#include <QtGlobal>

namespace morfanalytics {

class SampleStore;

// -----------------------------------------------------------------------------
// MeteoSyncPublisher : publie une SYNTHESE PAR JOUR dans le journal morfSync
// (domaine "meteohub"), pour rendre les resultats consultables par le reste du
// parc (morfDashboard, SiteWatch...).
//
// Sens unique, doctrine respectee : morfAnalytics lit son cache local et ECRIT
// vers morfSync uniquement. Il n'ecrit jamais vers l'appareil (source de verite),
// et morfSync n'est qu'un aval. Si le hub est absent, la publication est sautee
// sans consequence : le cache et les analyses continuent.
//
// --- Identite et revision ----------------------------------------------------
// Un jour = un enregistrement d'identite STABLE : id = "meteohub-<AAAAMMJJ>".
// La revision est le NOMBRE D'ECHANTILLONS du jour (importedPerDay) : elle croit
// quand la journee se remplit et ne change pas sinon. Republier un jour inchange
// porte donc le meme (id, rev) -- le hub le reconnait et n'ecrit rien (idempotent,
// docs/sync-contract.md 4.3). La consequence pratique : le suivi du dernier rev
// publie n'a pas besoin d'etre persiste. Il vit en memoire ; apres un redemarrage,
// le premier cycle republie tout, et le hub no-ope ce qui n'a pas bouge.
//
// --- Pourquoi une synthese et non les mesures --------------------------------
// La serie temporelle brute reste dans le cache SQLite (des centaines de milliers
// de points). Le journal morfSync ne porte que le RESULTAT : min/max/moyenne +
// premiere/derniere valeur par canal, pour un jour. Quelques centaines d'octets,
// ~365 par an. Le hub JSON ne devient jamais un entrepot de mesures.
// -----------------------------------------------------------------------------
class MeteoSyncPublisher : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString     baseUrl;                          // ex. http://127.0.0.1:8080 (morfSync)
        QString     token;                            // Bearer optionnel
        QString     domain   = QStringLiteral("meteohub"); // journal cote hub
        QString     deviceId;                         // origine stable de cet emetteur
        QStringList channels;                         // canaux a synthetiser
        int         timeoutMs = 4000;
    };

    MeteoSyncPublisher(SampleStore* store, Config cfg, QObject* parent = nullptr);

    // Publie les jours dont la synthese a change depuis le dernier envoi reussi.
    // Best-effort : renvoie le nombre de jours publies (0 si rien a faire, hub
    // injoignable, ou erreur -- l'erreur est lisible via statusJson()).
    int publish();

    bool enabled() const { return !m_cfg.baseUrl.isEmpty(); }
    QJsonObject statusJson() const;

private:
    SampleStore* m_store;
    Config       m_cfg;
    QHash<quint32, quint32> m_publishedRev; // day_key -> dernier rev publie (en memoire)
    qint64  m_lastPublishTs = 0;
    int     m_lastPublished = 0;
    QString m_lastError;
};

} // namespace morfanalytics
