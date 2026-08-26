/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/data/AnnotationStore.h"

#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonValue>
#include <algorithm>

namespace morfanalytics {

namespace {

// Horodatage de creation/modification. UTC + ISO-8601 : lisible, trie comme du
// texte, sans piege de fuseau. Les BORNES d'une observation, elles, sont en
// secondes Unix (voir en-tete) pour s'aligner sur l'axe de temps des mesures.
QString nowIso() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

// Lit une borne temporelle en secondes Unix. Accepte un nombre JSON (cas normal
// de l'interface) comme une chaine de chiffres (robustesse aux clients tiers).
// Renvoie false si la valeur est absente ou non entiere positive.
bool parseEpoch(const QJsonValue& v, qint64* out) {
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d <= 0.0) return false;
        *out = static_cast<qint64>(d);
        return true;
    }
    if (v.isString()) {
        bool ok = false;
        const qint64 n = v.toString().trimmed().toLongLong(&ok);
        if (!ok || n <= 0) return false;
        *out = n;
        return true;
    }
    return false;
}

} // namespace

AnnotationStore::AnnotationStore(QString filePath) : m_path(std::move(filePath)) {}

QStringList AnnotationStore::knownTypes() {
    // Vocabulaire propose, volontairement descriptif et non scientifique. « autre »
    // ferme la liste sans la contraindre ; le stockage accepte de toute facon
    // n'importe quel type non vide, ce qui garde le vocabulaire extensible.
    return {
        QStringLiteral("orage"),
        QStringLiteral("pluie"),
        QStringLiteral("vent_fort"),
        QStringLiteral("grele"),
        QStringLiteral("neige"),
        QStringLiteral("brouillard"),
        QStringLiteral("gel"),
        QStringLiteral("autre"),
    };
}

bool AnnotationStore::load() {
    m_items.clear();
    QFile f(m_path);
    if (!f.exists())
        return true;                 // aucune observation encore : demarrage vide, pas une erreur
    if (!f.open(QIODevice::ReadOnly))
        return false;                // fichier present mais illisible : le signaler, ne pas l'ignorer

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    for (const QJsonValue& v : doc.object().value(QStringLiteral("annotations")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString().isEmpty())
            continue;                // ligne sans id : ignoree plutot que de la propager
        m_items.push_back(o);
    }
    return true;
}

QJsonArray AnnotationStore::all() const {
    QVector<QJsonObject> sorted = m_items;
    // Plus recentes d'abord : l'interface liste les derniers episodes en tete.
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a.value(QStringLiteral("start")).toDouble() >
               b.value(QStringLiteral("start")).toDouble();
    });
    QJsonArray arr;
    for (const QJsonObject& o : sorted)
        arr.append(o);
    return arr;
}

bool AnnotationStore::normalize(const QJsonObject& in, QJsonObject* out, QString* error) {
    const auto fail = [&](const QString& msg) { if (error) *error = msg; return false; };

    qint64 start = 0, end = 0;
    if (!parseEpoch(in.value(QStringLiteral("start")), &start))
        return fail(QStringLiteral("début manquant ou invalide"));
    if (!parseEpoch(in.value(QStringLiteral("end")), &end))
        return fail(QStringLiteral("fin manquante ou invalide"));
    if (end < start)
        return fail(QStringLiteral("la fin précède le début"));

    // Types : au moins un, non vides. Normalises (minuscule, sans espaces), sans
    // doublon, dans l'ordre de saisie. Le vocabulaire reste libre : on ne rejette
    // pas un type hors de knownTypes(), c'est l'utilisateur qui decrit ce qu'il a vu.
    if (!in.value(QStringLiteral("types")).isArray())
        return fail(QStringLiteral("liste de types manquante"));
    QJsonArray types;
    QStringList seen;
    for (const QJsonValue& v : in.value(QStringLiteral("types")).toArray()) {
        const QString t = v.toString().trimmed().toLower();
        if (t.isEmpty() || seen.contains(t))
            continue;
        seen << t;
        types.append(t);
    }
    if (types.isEmpty())
        return fail(QStringLiteral("au moins un type d'événement est requis"));

    // Description libre, facultative. C'est ici que vit le recit que les types
    // seuls ne capturent pas (enchainement des phases, accalmies, direction).
    const QString description = in.value(QStringLiteral("description")).toString().trimmed();

    out->insert(QStringLiteral("start"), static_cast<double>(start));
    out->insert(QStringLiteral("end"), static_cast<double>(end));
    out->insert(QStringLiteral("types"), types);
    out->insert(QStringLiteral("description"), description);
    return true;
}

QJsonObject AnnotationStore::upsert(const QJsonObject& in, int* code, QString* error) {
    const auto reject = [&](int c, const QString& msg) {
        if (code) *code = c;
        if (error) *error = msg;
        return QJsonObject{};
    };

    QJsonObject fields;
    QString normError;
    if (!normalize(in, &fields, &normError))
        return reject(400, normError);

    const QString id = in.value(QStringLiteral("id")).toString().trimmed();
    if (!id.isEmpty()) {
        // Mise a jour : l'id doit exister. Un id inconnu est un echec 404, pas une
        // creation deguisee : on ne recree pas silencieusement une annotation que
        // le client croyait deja presente (elle a pu etre supprimee entre-temps).
        int idx = -1;
        for (int i = 0; i < m_items.size(); ++i)
            if (m_items[i].value(QStringLiteral("id")).toString() == id) { idx = i; break; }
        if (idx < 0)
            return reject(404, QStringLiteral("annotation introuvable"));

        fields.insert(QStringLiteral("id"), id);
        fields.insert(QStringLiteral("created_at"),
                      m_items[idx].value(QStringLiteral("created_at")));   // date d'origine preservee
        fields.insert(QStringLiteral("updated_at"), nowIso());
        m_items[idx] = fields;
    } else {
        // Creation : id genere (uuid sans accolades), horodatages poses.
        const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString ts = nowIso();
        fields.insert(QStringLiteral("id"), newId);
        fields.insert(QStringLiteral("created_at"), ts);
        fields.insert(QStringLiteral("updated_at"), ts);
        m_items.push_back(fields);
    }

    QString saveError;
    if (!persist(&saveError))
        return reject(500, QStringLiteral("écriture impossible : %1").arg(saveError));

    if (code) *code = 200;
    if (error) error->clear();
    return fields;
}

QJsonObject AnnotationStore::removeById(const QString& id, int* code, QString* error) {
    const auto reject = [&](int c, const QString& msg) {
        if (code) *code = c;
        if (error) *error = msg;
        return QJsonObject{};
    };

    const QString wanted = id.trimmed();
    if (wanted.isEmpty())
        return reject(400, QStringLiteral("identifiant manquant"));

    int idx = -1;
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].value(QStringLiteral("id")).toString() == wanted) { idx = i; break; }
    if (idx < 0)
        return reject(404, QStringLiteral("annotation introuvable"));

    m_items.remove(idx);
    QString saveError;
    if (!persist(&saveError))
        return reject(500, QStringLiteral("écriture impossible : %1").arg(saveError));

    if (code) *code = 200;
    if (error) error->clear();
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("id"), wanted}};
}

bool AnnotationStore::persist(QString* error) const {
    // Le dossier parent peut ne pas exister au tout premier enregistrement.
    QDir().mkpath(QFileInfo(m_path).absolutePath());

    QJsonObject root{
        {QStringLiteral("annotations"), all()},
        {QStringLiteral("updated_at"), nowIso()},
    };

    // Ecriture atomique : temporaire puis renommage (QSaveFile::commit). La donnee
    // utilisateur n'est jamais laissee a moitie ecrite si le service s'arrete au
    // mauvais moment ; l'ancien fichier reste intact jusqu'au succes complet.
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace morfanalytics
