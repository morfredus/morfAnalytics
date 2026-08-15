/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QVector>
#include <QSqlDatabase>
#include <QJsonObject>
#include <QJsonArray>
#include <QtGlobal>

namespace morfanalytics {

// -----------------------------------------------------------------------------
// MonitorStore : historique local (SQLite) des métriques remontées par un ou
// plusieurs morfMonitor. morfMonitor ne connaît que « maintenant » ; c'est ICI
// que morfSystem acquiert une mémoire de son fonctionnement dans le temps.
//
// Schéma HYBRIDE (cf. analyse du domaine Monitor) : une table LARGE par machine
// (`sample_machine`) et une table par service (`sample_service`), plutôt qu'une
// table longue générique — bien moins de lignes et des agrégations directes. Le
// schéma est déjà taillé pour la suite (rollups, rétention, purge sélective) :
// tables séparées par granularité à venir, `machine` comme registre stable.
//
// INVARIANT du domaine : une valeur absente est un NULL, jamais un 0. Distinguer
// « mesuré à zéro » de « non mesuré / hors ligne » est essentiel à l'analyse.
// -----------------------------------------------------------------------------

// Un relevé machine. NaN => la mesure n'était pas disponible => NULL en base.
struct MachineSample {
    double cpuPercent  = qQNaN();
    double load1       = qQNaN();
    double memPercent  = qQNaN();
    double memUsed     = qQNaN();
    double memTotal    = qQNaN();
    double swapPercent = qQNaN();
    double tempCpu     = qQNaN();
    double diskPercent = qQNaN();
    double uptimeS     = qQNaN();
    double servicesActive = qQNaN();
};

// Un relevé pour un service supervisé (consommation instantanée + cumul CPU).
struct ServiceSample {
    QString service;
    double  cpuPercent = qQNaN();
    double  memBytes   = qQNaN();
    double  tasks      = qQNaN();
};

class MonitorStore {
public:
    explicit MonitorStore(QString dbPath);
    ~MonitorStore();

    bool open();
    void close();
    bool isOpen() const;
    QString lastError() const { return m_lastError; }

    // --- Écriture (collecteur) ----------------------------------------------
    // Crée ou rafraîchit la ligne d'une machine (clé stable) et renvoie son id
    // interne, ou -1 en cas d'erreur. `hostname`/`model` alimentent l'affichage.
    int upsertMachine(const QString& key, const QString& hostname,
                      const QString& model, qint64 ts);

    bool insertMachineSample(int machineId, qint64 ts, const MachineSample& s);
    bool insertServiceSamples(int machineId, qint64 ts, const QVector<ServiceSample>& list);

    // --- Lecture (page /monitor) --------------------------------------------
    // Machines connues : [{ key, hostname, model, last_seen, online }].
    QJsonArray machines() const;

    // Dernier relevé d'une machine (tuiles de la vue d'ensemble), ou objet vide.
    QJsonObject latestMachine(int machineId) const;

    // Séries temporelles sous-échantillonnées sur [from, to], ~maxPoints points.
    // Le bucket est choisi selon la période demandée : une page 30 jours ne
    // télécharge jamais des dizaines de milliers de points bruts. Chaque bucket
    // porte la moyenne (et min/max pour le CPU et la température, pour ne pas
    // masquer les pics). Les trous restent des null, jamais des zéros.
    QJsonObject machineSeries(int machineId, qint64 from, qint64 to, int maxPoints) const;

    // « Qui consomme quoi » : consommation par service agrégée sur [from, to]
    // (moyenne et maximum de CPU et de mémoire), triée par CPU moyen décroissant.
    // C'est la matière de la vue Services du domaine Monitor.
    QJsonArray serviceStats(int machineId, qint64 from, qint64 to) const;

    // Rétention : supprime les relevés BRUTS antérieurs à `cutoffTs`, pour borner
    // la base sur une machine modeste. Étape simple ; la compaction par paliers
    // (rollups + mémoire remarquable) viendra ensuite. Renvoie le nombre de lignes
    // supprimées (machine + service), ou -1 en cas d'erreur.
    qint64 purgeSamplesBefore(qint64 cutoffTs);

    int machineIdForKey(const QString& key) const;

private:
    QString      m_dbPath;
    QString      m_connectionName;
    QString      m_lastError;
    QSqlDatabase m_db;
};

} // namespace morfanalytics
