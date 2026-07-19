/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <cmath>

namespace morfanalytics {

// -----------------------------------------------------------------------------
// Series : serie temporelle GENERIQUE, sans aucune notion de meteo.
//
// C'est le seul type de donnees que voient les analyses. Il ne connait ni
// temperature, ni pression : uniquement des CANAUX nommes, alignes sur un axe
// de temps commun. Un autre projet (consommation electrique, qualite d'air,
// supervision de site...) reutilise le moteur en fournissant ses propres noms
// de canaux — c'est ce qui rend morfAnalytics transposable.
//
// Stockage EN COLONNES (un QVector par canal) et non en lignes : sur plusieurs
// centaines de milliers de points, un tableau de structures avec dictionnaire
// par echantillon couterait bien plus cher en memoire et en parcours. Ici, une
// analyse qui ne lit que la pression ne touche que la colonne pression.
//
// Valeur manquante = NaN. Toutes les analyses doivent utiliser isValid() plutot
// que de supposer les colonnes pleines : les trous d'acquisition sont normaux
// (coupure de courant, carte SD absente, capteur en defaut).
// -----------------------------------------------------------------------------
class Series {
public:
    Series() = default;
    explicit Series(QStringList channelNames) {
        for (const QString& name : channelNames)
            m_channels.insert(name, QVector<double>());
        m_names = std::move(channelNames);
    }

    static bool isValid(double v) { return !std::isnan(v); }
    static double missing() { return std::numeric_limits<double>::quiet_NaN(); }

    int size() const { return m_ts.size(); }
    bool isEmpty() const { return m_ts.isEmpty(); }

    const QVector<qint64>& timestamps() const { return m_ts; }
    const QStringList& channelNames() const { return m_names; }

    bool hasChannel(const QString& name) const { return m_channels.contains(name); }

    // Renvoie nullptr si le canal n'existe pas : une analyse peut ainsi se
    // declarer inapplicable au lieu de planter sur un jeu de donnees partiel.
    const QVector<double>* channel(const QString& name) const {
        auto it = m_channels.constFind(name);
        return it == m_channels.constEnd() ? nullptr : &it.value();
    }

    // Ajoute un echantillon. Les canaux absents de `values` sont marques manquants,
    // ce qui garde toutes les colonnes exactement de la meme longueur que l'axe
    // de temps — invariant sur lequel s'appuient toutes les analyses.
    void append(qint64 ts, const QHash<QString, double>& values) {
        m_ts.push_back(ts);
        for (const QString& name : m_names) {
            auto it = values.constFind(name);
            m_channels[name].push_back(it == values.constEnd() ? missing() : it.value());
        }
    }

    void reserve(int n) {
        m_ts.reserve(n);
        for (const QString& name : m_names)
            m_channels[name].reserve(n);
    }

private:
    QVector<qint64> m_ts;
    QStringList m_names;
    QHash<QString, QVector<double>> m_channels;
};

} // namespace morfanalytics
