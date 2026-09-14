/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/AnalyticsModule.h"
#include "morfanalytics/StatePaths.h"
#include "morfanalytics/data/SampleStore.h"
#include "morfanalytics/data/ForecastStore.h"
#include "morfanalytics/data/AnnotationStore.h"
#include "morfanalytics/collect/MeteoHubCollector.h"
#include "morfanalytics/collect/ForecastCollector.h"
#include "morfanalytics/publish/MeteoSyncPublisher.h"

#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QHostInfo>

namespace morfanalytics {

namespace {
// Canaux collectés depuis MeteoHub. Le cache et les analyses sont génériques :
// changer cette liste suffit à suivre un appareil exposant d'autres grandeurs.
const QStringList kChannels{QStringLiteral("temp"), QStringLiteral("hum"),
                            QStringLiteral("pres")};

// Dossier d'ETAT PERSISTANT par defaut (cache SQLite des echantillons), quand la
// config ne fixe pas 'cache_dir'. Le cache est de l'etat genere par le service,
// pas de la config ni du programme : il vit sous /var/lib (doctrine morfSystem,
// docs/FILESYSTEM.md), jamais dans le dossier courant (/opt).
//
// Sous systemd, l'unite declare StateDirectory=morfsystem/morfanalytics : la
// racine arrive via $STATE_DIRECTORY (generique, sans nom en dur). Repli conforme
// a l'OS hors systemd. Le dossier est cree et doit etre accessible en ecriture.
// Racine d'etat : desormais mutualisee (voir morfanalytics::stateDir). Ce wrapper
// garde les appels locaux lisibles ; tous les modules pointent la meme /var/lib.
QString defaultStateDir() {
    return morfanalytics::stateDir();
}
} // namespace

AnalyticsModule::AnalyticsModule(const QString& id, int maintenanceMs,
                                 QString cacheDir, QString sourceUrl,
                                 double altitudeM, bool altitudeKnown,
                                 QString morfsyncUrl, QString morfsyncToken,
                                 QObject* parent)
    : IModule(id, QStringLiteral("analytics"), parent),
      m_maintenanceMs(maintenanceMs > 0 ? maintenanceMs : 60000),
      m_cacheDir(std::move(cacheDir)),
      m_sourceUrl(std::move(sourceUrl)),
      m_altitudeM(altitudeM),
      m_altitudeKnown(altitudeKnown),
      m_morfsyncUrl(std::move(morfsyncUrl)),
      m_morfsyncToken(std::move(morfsyncToken)),
      m_timer(new QTimer(this)) {
    m_timer->setInterval(m_maintenanceMs);
    connect(m_timer, &QTimer::timeout, this, &AnalyticsModule::maintainCache);
    // Le moteur est générique ; c'est cet appel, et lui seul, qui le spécialise
    // en moteur météo. Un autre projet enregistre ici son propre jeu d'analyses.
    registerMeteoAnalyses(m_analyses);

    // Observations humaines : fichier d'ETAT (jamais le cache reconstructible),
    // toujours dans defaultStateDir() meme si 'cache_dir' pointe ailleurs, pour
    // qu'une purge ou un deplacement du cache ne les emporte pas. Cree et charge
    // des maintenant : les routes HTTP peuvent survenir avant tout cycle de
    // collecte, l'observation ne depend pas des mesures.
    const QString annPath = QDir(defaultStateDir())
                                .filePath(QStringLiteral("meteo-annotations.json"));
    m_annotations = std::make_unique<AnnotationStore>(annPath);
    if (!m_annotations->load()) {
        // Un fichier present mais illisible ne doit pas passer inapercu : c'est de
        // la donnee utilisateur potentiellement perdue. On le DIT, sans empecher
        // le service de tourner (les mesures, elles, restent exploitables).
        qWarning().noquote()
            << QStringLiteral("module analytics : annotations illisibles dans %1 — "
                              "observations non chargees").arg(annPath);
    }
}

AnalyticsModule::~AnalyticsModule() = default;

bool AnalyticsModule::start() {
    const QString dir = m_cacheDir.isEmpty() ? defaultStateDir() : m_cacheDir;
    const QString dbPath = QDir(dir).filePath(QStringLiteral("meteohub-cache.sqlite"));

    m_store = std::make_unique<SampleStore>(dbPath, kChannels);
    if (!m_store->open()) {
        // Sans cache, le module ne peut rien faire d'utile : on échoue franchement
        // plutôt que de tourner en apparence tout en n'accumulant rien. Et on le
        // DIT : cet échec est resté muet une fois — dossier /opt possédé par
        // root, cache incréable — et l'interface renvoyait vers source_url
        // pendant que la vraie cause, une permission, ne figurait nulle part.
        // Un diagnostic complet a coûté une enquête là où une ligne de journal
        // aurait suffi.
        qCritical().noquote()
            << QStringLiteral("module analytics : impossible d'ouvrir le cache %1 : %2")
                   .arg(dbPath, m_store->lastError())
            << QStringLiteral("— verifier les droits du dossier (le service tourne en User=, "
                              "le dossier doit lui appartenir) ; aucune mesure ne sera collectee.");
        m_store.reset();
        return false;
    }

    // Sans source configurée, le module reste valide mais inerte : il expose le
    // cache déjà constitué sans jamais le rafraîchir. Cela permet d'analyser un
    // historique déjà recopié même si l'appareil est hors service.
    // Cache OUT (météo extérieure), séparé du cache IN. Additif : s'il ne peut
    // s'ouvrir, l'IN continue de fonctionner (on le signale sans échouer).
    const QString dbPathOut = QDir(dir).filePath(QStringLiteral("meteohub-out-cache.sqlite"));
    m_storeOut = std::make_unique<SampleStore>(dbPathOut, kChannels);
    if (!m_storeOut->open()) {
        qWarning().noquote()
            << QStringLiteral("module analytics : cache OUT indisponible (%1) : %2 — "
                              "la météo extérieure ne sera pas historisée.")
                   .arg(dbPathOut, m_storeOut->lastError());
        m_storeOut.reset();
    }

    // Cache des prévisions « day-ahead » archivées par l'appareil (étape 9).
    // Additif comme le cache OUT : un échec n'empêche pas le reste de tourner.
    const QString dbPathFc = QDir(dir).filePath(QStringLiteral("meteohub-forecast-cache.sqlite"));
    m_forecastStore = std::make_unique<ForecastStore>(dbPathFc);
    if (!m_forecastStore->open()) {
        qWarning().noquote()
            << QStringLiteral("module analytics : cache prévisions indisponible (%1) : %2 — "
                              "l'analyse prévu vs observé sera inactive.")
                   .arg(dbPathFc, m_forecastStore->lastError());
        m_forecastStore.reset();
    }

    if (!m_sourceUrl.isEmpty()) {
        m_collector = new MeteoHubCollector(m_sourceUrl, m_store.get(),
                                            QStringLiteral("in"), this);
        // Collecteur OUT : même appareil, flux extérieur (ctx=out).
        if (m_storeOut)
            m_collectorOut = new MeteoHubCollector(m_sourceUrl, m_storeOut.get(),
                                                   QStringLiteral("out"), this);
        // Collecteur de prévisions : même appareil, route /api/forecast/history.
        if (m_forecastStore)
            m_forecastCollector = new ForecastCollector(m_sourceUrl, m_forecastStore.get(), this);
        // Première collecte immédiate : au démarrage du service, on ne fait pas
        // attendre une période de maintenance complète avant le premier import.
        QTimer::singleShot(0, this, &AnalyticsModule::maintainCache);
    }

    // Publication (facultative) des synthèses journalières vers morfSync. Écriture
    // seule, à sens unique : elle rend les résultats consultables par le reste du
    // parc sans jamais toucher à la source. Absente si aucun hub n'est configuré.
    if (!m_morfsyncUrl.isEmpty()) {
        MeteoSyncPublisher::Config cfg;
        cfg.baseUrl  = m_morfsyncUrl;
        cfg.token    = m_morfsyncToken;
        cfg.channels = kChannels;
        // Origine stable de cet émetteur dans le journal morfSync (deviceId).
        cfg.deviceId = QStringLiteral("morfanalytics@") + QHostInfo::localHostName();
        m_publisher  = new MeteoSyncPublisher(m_store.get(), cfg, this);
    }

    m_running = true;
    m_timer->start();
    return true;
}

void AnalyticsModule::stop() {
    m_running = false;
    m_timer->stop();
    if (m_store)
        m_store->close();
    if (m_storeOut)
        m_storeOut->close();
    if (m_forecastStore)
        m_forecastStore->close();
}

QJsonObject AnalyticsModule::statusJson() const {
    QJsonObject o;
    o["running"]        = m_running;
    o["altitude_m"]     = m_altitudeM;
    o["altitude_known"] = m_altitudeKnown;
    o["ts"]         = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    if (m_collector)
        o["collector"] = m_collector->statusJson();
    if (m_collectorOut)
        o["collector_out"] = m_collectorOut->statusJson();
    if (m_forecastCollector)
        o["collector_forecast"] = m_forecastCollector->statusJson();
    if (m_publisher)
        o["publisher"] = m_publisher->statusJson();
    return o;
}

QJsonObject AnalyticsModule::analyze(const QJsonObject& request) const {
    AnalysisContext ctx;
    // Les deux caches sont fournis ; le registre choisit la source primaire selon
    // le contexte de l'analyse (IN confort / OUT météo / Both relation). `store`
    // reste renseigné (IN) comme repli si aucun contexte n'est résolu.
    ctx.store     = m_store.get();
    ctx.storeIn   = m_store.get();
    ctx.storeOut  = m_storeOut ? m_storeOut.get() : nullptr;
    ctx.forecastStore = m_forecastStore ? m_forecastStore.get() : nullptr;
    ctx.altitudeM     = m_altitudeM;
    ctx.altitudeKnown = m_altitudeKnown;
    ctx.now       = QDateTime::currentSecsSinceEpoch();

    const QString type = request.value(QStringLiteral("type")).toString();
    QJsonObject result = m_analyses.run(type, ctx, request);
    result["ts"] = static_cast<double>(ctx.now);
    return result;
}

QJsonArray AnalyticsModule::analysisCatalog() const {
    return m_analyses.catalogJson();
}

QJsonObject AnalyticsModule::seriesJson(const QString& ctx, const QString& metric,
                                        qint64 from, qint64 to, int maxPoints) const {
    QJsonObject o;
    o["ctx"] = ctx;
    o["metric"] = metric;
    QJsonArray tsArr, vArr;

    const SampleStore* store =
        (ctx == QLatin1String("in"))  ? m_store.get() :
        (ctx == QLatin1String("out")) ? (m_storeOut ? m_storeOut.get() : nullptr) : nullptr;

    if (store && store->isOpen() && to > from) {
        const Series s = store->range(from, to);
        const QVector<double>* ch = s.channel(metric);
        const QVector<qint64>& ts = s.timestamps();
        if (ch && !ts.isEmpty()) {
            if (maxPoints < 1) maxPoints = 1;
            const qint64 span = to - from;
            qint64 bucket = span / maxPoints;
            if (bucket < 1) bucket = 1;
            int nb = static_cast<int>((span + bucket - 1) / bucket);
            if (nb < 1) nb = 1;

            QVector<double> sum(nb, 0.0);
            QVector<int>    cnt(nb, 0);
            for (int i = 0; i < ch->size() && i < ts.size(); ++i) {
                const double val = (*ch)[i];
                if (!Series::isValid(val)) continue;
                const int b = static_cast<int>((ts[i] - from) / bucket);
                if (b < 0 || b >= nb) continue;
                sum[b] += val; cnt[b]++;
            }
            // On n'émet QUE les tranches qui contiennent une mesure : pas de trou
            // artificiel entre deux points simplement espacés de la cadence (5 min).
            // Un VRAI trou (capteur muet longtemps) se lit alors à l'écart temporel
            // entre deux points consécutifs, que le client coupe au-delà d'un seuil.
            o["bucket_s"] = static_cast<double>(bucket);
            for (int b = 0; b < nb; ++b) {
                if (cnt[b] <= 0) continue;
                tsArr.append(static_cast<double>(from + static_cast<qint64>(b) * bucket + bucket / 2));
                vArr.append(qRound(sum[b] / cnt[b] * 10.0) / 10.0); // 1 décimale
            }
        }
    }
    o["ts"] = tsArr;
    o["v"]  = vArr;
    return o;
}

QJsonObject AnalyticsModule::cleanupData(const QJsonObject& request) {
    QJsonObject o;

    // Collecte à la demande : déclenche un vrai cycle de collecte (pull depuis
    // l'appareil) au lieu d'attendre le timer de maintenance. La collecte est
    // ASYNCHRONE (les nouvelles mesures arrivent dans les instants qui suivent) :
    // on la lance et on répond immédiatement. Distinct du simple rafraîchissement
    // d'affichage, qui ne fait que recalculer les analyses sur le cache existant.
    if (request.value(QStringLiteral("action")).toString() == QLatin1String("collect_now")) {
        if (m_collector) m_collector->sync();
        if (m_collectorOut) m_collectorOut->sync();
        if (m_forecastCollector) m_forecastCollector->sync();
        o["ok"] = true;
        o["note"] = QStringLiteral(
            "Cycle de collecte lancé : récupération des nouvelles mesures depuis "
            "l'appareil. Les données apparaissent dans les instants qui suivent.");
        return o;
    }

    if (!m_store || !m_store->isOpen()) {
        o["ok"] = false;
        o["error"] = QStringLiteral("cache indisponible");
        return o;
    }

    // Bornes de panne capteur : une pression hors de [300, 1200] hPa est
    // physiquement impossible — c'est la signature du BME280 en défaut (zéros),
    // et elle disqualifie tout le relevé (le 0 °C associé n'est pas une mesure).
    // Mêmes bornes que le filtre d'import du collecteur : ce nettoyage rattrape
    // l'historique entré AVANT que le filtre n'existe.
    constexpr double kPresMin = 300.0, kPresMax = 1200.0;
    const QString kPres = QStringLiteral("pres");

    const QString action = request.value(QStringLiteral("action")).toString();
    qint64 n = -1;

    if (action == QLatin1String("scan_faults") || action == QLatin1String("invalidate_faults")) {
        const bool dryRun = (action == QLatin1String("scan_faults"));
        n = m_store->invalidateOutliers(kPres, kPresMin, kPresMax, dryRun);
    } else if (action == QLatin1String("invalidate_range")) {
        const auto fromTs = static_cast<qint64>(request.value(QStringLiteral("from_ts")).toDouble());
        const auto toTs   = static_cast<qint64>(request.value(QStringLiteral("to_ts")).toDouble());
        if (fromTs <= 0 || toTs <= 0 || toTs < fromTs) {
            o["ok"] = false;
            o["error"] = QStringLiteral("plage from_ts / to_ts invalide");
            return o;
        }
        QStringList channels;
        for (const QJsonValue& v : request.value(QStringLiteral("channels")).toArray())
            channels << v.toString();
        if (channels.isEmpty())
            channels = kChannels; // sans précision, toute la ligne est neutralisée
        n = m_store->invalidateChannels(fromTs, toTs, channels,
                                        request.value(QStringLiteral("dry_run")).toBool());
    } else if (action == QLatin1String("purge_all")) {
        // Purge SYMÉTRIQUE : les deux caches météo, IN (m_store) ET OUT
        // (m_storeOut). Sans cela, vider « le cache » laissait tout l'historique
        // extérieur en place, et le repli d'analyse continuait sur d'anciennes
        // mesures. Chaque curseur de collecte est réinitialisé par purgeAll(),
        // donc les deux flux se reconstruisent depuis l'appareil au cycle suivant.
        if (!m_store->purgeAll()) {
            o["ok"] = false;
            o["error"] = m_store->lastError();
            return o;
        }
        if (m_storeOut && m_storeOut->isOpen() && !m_storeOut->purgeAll()) {
            o["ok"] = false;
            o["error"] = m_storeOut->lastError();
            return o;
        }
        // Le cache des prévisions se purge aussi (reconstruit depuis l'appareil).
        if (m_forecastStore && m_forecastStore->isOpen() && !m_forecastStore->purgeAll()) {
            o["ok"] = false;
            o["error"] = m_forecastStore->lastError();
            return o;
        }
        o["ok"] = true;
        o["note"] = QStringLiteral(
            "Caches intérieur et extérieur vidés. Ils seront reconstruits "
            "intégralement depuis l'appareil au prochain cycle de collecte ; les "
            "mesures d'origine n'ont pas été touchées.");
        o["cached_points"] = static_cast<double>(
            m_store->count() + (m_storeOut ? m_storeOut->count() : 0));
        return o;
    } else {
        o["ok"] = false;
        o["error"] = QStringLiteral("action inconnue : %1").arg(action);
        return o;
    }

    if (n < 0) {
        o["ok"] = false;
        o["error"] = m_store->lastError();
        return o;
    }
    o["ok"] = true;
    o["affected"] = static_cast<double>(n);
    o["cached_points"] = static_cast<double>(m_store->count());
    return o;
}

QJsonObject AnalyticsModule::annotationsJson() const {
    QJsonArray known;
    for (const QString& t : AnnotationStore::knownTypes())
        known.append(t);
    return QJsonObject{
        {QStringLiteral("annotations"), m_annotations ? m_annotations->all() : QJsonArray{}},
        {QStringLiteral("known_types"), known},
    };
}

QJsonObject AnalyticsModule::saveAnnotation(const QJsonObject& in, int* code, QString* error) {
    if (!m_annotations) {
        if (code) *code = 503;
        if (error) *error = QStringLiteral("stockage des observations indisponible");
        return QJsonObject{};
    }
    return m_annotations->upsert(in, code, error);
}

QJsonObject AnalyticsModule::deleteAnnotation(const QString& id, int* code, QString* error) {
    if (!m_annotations) {
        if (code) *code = 503;
        if (error) *error = QStringLiteral("stockage des observations indisponible");
        return QJsonObject{};
    }
    return m_annotations->removeById(id, code, error);
}

void AnalyticsModule::maintainCache() {
    // Le collecteur ignore l'appel si un cycle est déjà en cours : une période de
    // maintenance plus courte qu'un rattrapage complet n'empile donc rien.
    if (m_collector)
        m_collector->sync();
    if (m_collectorOut)
        m_collectorOut->sync();
    if (m_forecastCollector)
        m_forecastCollector->sync();
    // Publication des synthèses journalières. La collecte ci-dessus est ASYNCHRONE
    // (les mesures arrivent après cet appel) : on publie donc l'état STABILISÉ,
    // celui de la collecte du cycle précédent. Un jour qui vient de gagner des
    // mesures sera publié au cycle suivant — les synthèses journalières ne sont pas
    // à la seconde près, et la publication reste idempotente.
    if (m_publisher)
        m_publisher->publish();
    emit updated(id());
}

} // namespace morfanalytics
