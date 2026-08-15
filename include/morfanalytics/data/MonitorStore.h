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

    // --- Activités (compilations, indexations…) -----------------------------
    // Une activité est signalée par le composant qui la connaît (§34) : on ne la
    // devine pas d'un pic CPU. `metadata` est un objet libre (preset, commit…).
    // Renvoie l'id, ou -1.
    qint64 insertActivity(const QString& type, const QString& project,
                          const QString& machine, qint64 startTs, qint64 endTs,
                          const QString& status, const QJsonObject& metadata);

    // Statistiques système (CPU, température, mémoire, charge : moyenne et max)
    // sur la fenêtre [from, to] d'une machine. Sert à mesurer le COÛT réel d'une
    // activité (ce que faisait la machine pendant qu'elle compilait).
    QJsonObject windowStats(int machineId, qint64 from, qint64 to) const;

    // Statistiques de compilation par projet sur [from, to] : nombre, réussites,
    // échecs, temps total et durée moyenne/min/max (sur les builds RÉUSSIS, pour
    // qu'un échec de 3 s ne fausse pas la durée normale, §43). Plus des totaux.
    QJsonObject buildStats(const QString& machine, qint64 from, qint64 to) const;

    // Dernières activités sur [from, to], chacune enrichie de ses stats système
    // (fenêtre exacte de l'activité). `machine` vide => toutes machines.
    QJsonArray recentActivities(const QString& machine, qint64 from, qint64 to,
                                int limit) const;

    int machineIdForKey(const QString& key) const;

private:
    QString      m_dbPath;
    QString      m_connectionName;
    QString      m_lastError;
    QSqlDatabase m_db;
};

} // namespace morfanalytics
