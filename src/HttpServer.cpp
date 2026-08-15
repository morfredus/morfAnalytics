/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/HttpServer.h"
#include "morfanalytics/ModuleRegistry.h"
#include "morfanalytics/AnalyticsModule.h"
#include "morfanalytics/Version.h"
#include "morfanalytics/SelfDescription.h"
#include "morfanalytics/pages/PortalPage.h"
#include "morfanalytics/pages/MeteoHubPage.h"
#include "morfanalytics/pages/SiteWatchPage.h"
#include "morfanalytics/pages/PhotoPage.h"
#include "morfanalytics/pages/MonitorPage.h"
#include "morfanalytics/PhotoAnalyticsModule.h"
#include "morfanalytics/MonitorModule.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QSettings>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QLocale>
#include <QPair>
#include <QVector>
#include <QStringList>
#include <QDate>

#include <algorithm>
#include <cmath>
#include <utility>

namespace morfanalytics {

namespace {
// Les rapports SiteWatch peuvent contenir des classements et des séries
// journalières. Une limite d'un mégaoctet reste protectrice sur le réseau local
// tout en acceptant les rapports produits par les versions antérieures.
constexpr int kMaxRequestBytes = 1024 * 1024;

QByteArray toJson(const QJsonObject& o) {
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

int contentLength(const QByteArray& headerBlock) {
    for (const QByteArray& line : headerBlock.split('\n')) {
        const QByteArray l = line.trimmed();
        if (l.toLower().startsWith("content-length:"))
            return l.mid(l.indexOf(':') + 1).trimmed().toInt();
    }
    return 0;
}
} // namespace

HttpServer::HttpServer(ServiceConfig config, ModuleRegistry* registry, QObject* parent)
    : QObject(parent),
      m_config(std::move(config)),
      m_registry(registry),
      m_server(new QTcpServer(this)) {
    openSiteWatchStore();
    loadSiteWatchReports();
    connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

HttpServer::~HttpServer() {
    closeSiteWatchStore();
}

bool HttpServer::openSiteWatchStore() {
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        m_siteWatchStoreError = QStringLiteral("pilote QSQLITE indisponible");
        return false;
    }

    const QString dbPath = QDir(m_config.siteWatchCacheDir).filePath(QStringLiteral("sitewatch-history.sqlite"));
    if (!QDir().mkpath(QFileInfo(dbPath).absolutePath())) {
        m_siteWatchStoreError = QStringLiteral("impossible de créer %1").arg(m_config.siteWatchCacheDir);
        return false;
    }

    m_siteWatchConnectionName = QStringLiteral("morfanalytics-sitewatch-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_siteWatchDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_siteWatchConnectionName);
    m_siteWatchDb.setDatabaseName(dbPath);
    if (!m_siteWatchDb.open()) {
        m_siteWatchStoreError = m_siteWatchDb.lastError().text();
        return false;
    }

    QSqlQuery q(m_siteWatchDb);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS sitewatch_report ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                               "site_id TEXT NOT NULL, site_label TEXT, received_at INTEGER NOT NULL,"
                               "payload BLOB NOT NULL)")) ||
        !q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sitewatch_report_site_time "
                               "ON sitewatch_report(site_id, received_at DESC)"))) {
        m_siteWatchStoreError = q.lastError().text();
        closeSiteWatchStore();
        return false;
    }
    return true;
}

void HttpServer::closeSiteWatchStore() {
    if (m_siteWatchDb.isOpen()) m_siteWatchDb.close();
    m_siteWatchDb = QSqlDatabase();
    if (!m_siteWatchConnectionName.isEmpty() && QSqlDatabase::contains(m_siteWatchConnectionName))
        QSqlDatabase::removeDatabase(m_siteWatchConnectionName);
    m_siteWatchConnectionName.clear();
}

bool HttpServer::saveSiteWatchReport(const QJsonObject& report) {
    if (!m_siteWatchDb.isOpen()) return false;
    QSqlQuery q(m_siteWatchDb);
    q.prepare(QStringLiteral("INSERT INTO sitewatch_report(site_id, site_label, received_at, payload) "
                             "VALUES(?, ?, ?, ?)"));
    q.addBindValue(report.value("site_id").toString());
    q.addBindValue(report.value("site_label").toString());
    q.addBindValue(static_cast<qint64>(report.value("received_at").toDouble()));
    q.addBindValue(QJsonDocument(report).toJson(QJsonDocument::Compact));
    if (q.exec()) return true;
    m_siteWatchStoreError = q.lastError().text();
    return false;
}

QJsonArray HttpServer::siteWatchReports() const {
    QJsonArray reports;
    if (m_siteWatchDb.isOpen()) {
        // La page Web lit la base, pas un instantané mémoire : un redémarrage du
        // service ou une longue période d'analyse ne peut donc pas masquer les
        // synthèses déjà historisées.
        QSqlQuery q(m_siteWatchDb);
        if (q.exec(QStringLiteral("SELECT payload FROM sitewatch_report WHERE id IN "
                                  "(SELECT MAX(id) FROM sitewatch_report GROUP BY site_id) "
                                  "ORDER BY received_at DESC"))) {
            while (q.next()) {
                const QJsonObject report = QJsonDocument::fromJson(q.value(0).toByteArray()).object();
                if (!report.isEmpty()) reports.append(report);
            }
            return reports;
        }
    }
    // Compatibilité seulement si une ancienne synthèse QSettings est en cours
    // de migration ou si SQLite n'est pas disponible au démarrage.
    for (const QJsonObject& report : m_siteWatchReports) reports.append(report);
    return reports;
}

QJsonArray HttpServer::siteWatchHistory(const QString& siteId, int limit) const {
    QJsonArray reports;
    if (!m_siteWatchDb.isOpen() || siteId.isEmpty()) return reports;
    QSqlQuery q(m_siteWatchDb);
    q.prepare(QStringLiteral("SELECT payload FROM sitewatch_report WHERE site_id = ? "
                             "ORDER BY received_at DESC LIMIT ?"));
    q.addBindValue(siteId);
    q.addBindValue(qBound(1, limit, 365));
    if (!q.exec()) return reports;
    while (q.next()) {
        const QJsonObject report = QJsonDocument::fromJson(q.value(0).toByteArray()).object();
        if (!report.isEmpty()) reports.append(report);
    }
    return reports;
}

void HttpServer::loadSiteWatchReports() {
    if (m_siteWatchDb.isOpen()) {
        QSqlQuery q(m_siteWatchDb);
        if (q.exec(QStringLiteral("SELECT payload FROM sitewatch_report WHERE id IN "
                                  "(SELECT MAX(id) FROM sitewatch_report GROUP BY site_id)"))) {
            while (q.next()) {
                const QJsonObject report = QJsonDocument::fromJson(q.value(0).toByteArray()).object();
                const QString siteId = report.value("site_id").toString();
                if (!siteId.isEmpty()) m_siteWatchReports.insert(siteId, report);
            }
        }
    }

    // Migration transparente de l'ancien état QSettings, puis abandon de ce
    // stockage limité : SQLite garde chaque analyse et non seulement la dernière.
    const QJsonArray legacy = QJsonDocument::fromJson(
        QSettings("morfSystem", "morfAnalytics").value("sitewatch/reports").toByteArray()).array();
    if (m_siteWatchReports.isEmpty() && !legacy.isEmpty()) {
        for (const QJsonValue& value : legacy) {
            const QJsonObject report = value.toObject();
            const QString siteId = report.value("site_id").toString();
            if (siteId.isEmpty()) continue;
            m_siteWatchReports.insert(siteId, report);
            saveSiteWatchReport(report);
        }
    }
}

bool HttpServer::start() {
    if (m_config.httpPort == 0)
        return false;
    m_uptime.start();
    QHostAddress addr(m_config.bindAddress);
    if (addr.isNull())
        addr = QHostAddress(QHostAddress::AnyIPv4);
    return m_server->listen(addr, m_config.httpPort);
}

void HttpServer::stop()            { m_server->close(); }
bool HttpServer::isListening() const { return m_server->isListening(); }
quint16 HttpServer::port() const   { return m_server->isListening() ? m_server->serverPort() : 0; }

void HttpServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { onSocketReadyRead(sock); });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void HttpServer::onSocketReadyRead(QTcpSocket* sock) {
    QByteArray buf = sock->property("buf").toByteArray();
    buf += sock->readAll();

    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buf.size() > kMaxRequestBytes) { sock->abort(); return; }
        sock->setProperty("buf", buf);
        return;
    }

    const QByteArray headerBlock = buf.left(headerEnd);
    const int needed = contentLength(headerBlock);
    const int bodyStart = headerEnd + 4;
    if (needed < 0 || needed > kMaxRequestBytes) { sock->abort(); return; }
    if (buf.size() - bodyStart < needed) {
        if (buf.size() > kMaxRequestBytes) { sock->abort(); return; }
        sock->setProperty("buf", buf);
        return;
    }

    const int lineEnd = buf.indexOf("\r\n");
    const QList<QByteArray> parts = buf.left(lineEnd).split(' ');
    const QByteArray method = parts.value(0);
    const QByteArray path   = parts.value(1);
    const QByteArray body   = buf.mid(bodyStart, needed);

    sock->setProperty("buf", QByteArray());
    handleRequest(sock, method, path, body);
}

void HttpServer::handleRequest(QTcpSocket* sock, const QByteArray& method,
                               const QByteArray& rawPath, const QByteArray& body) {
    const QByteArray path = rawPath.left(rawPath.indexOf('?') < 0 ? rawPath.size()
                                                                  : rawPath.indexOf('?'));
    int        code   = 200;
    QByteArray reason = "OK";
    QByteArray out;

    // ---- Analyse a la demande (POST), servie par AnalyticsModule -------------
    if (path == "/sitewatch/ingest") {
        if (method != "POST") { code = 405; reason = "Method Not Allowed"; out = "{\"error\":\"use POST /sitewatch/ingest\"}"; }
        else out = handleSiteWatchPost(body, code, reason);
    } else if (path == "/analyze") {
        if (method != "POST") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use POST /analyze\"}";
        } else {
            out = handleAnalyzePost(body, code, reason);
        }
    }
    // ---- Nettoyage du cache local (POST) ---------------------------------
    // N'agit QUE sur la copie de travail : les mesures d'origine, sur
    // l'appareil, ne sont jamais touchees (le collecteur n'emet que des GET).
    else if (path == "/data/cleanup") {
        if (method != "POST") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use POST /data/cleanup\"}";
        } else {
            out = handleCleanupPost(body, code, reason);
        }
    }
    // ---- Ingestion d'activite (POST) : compilations, indexations... ------
    // Signalee par le composant qui CONNAIT l'activite (morfDeploy pour un build),
    // jamais devinee d'un pic CPU. Historisee dans le domaine Monitor.
    else if (path == "/api/monitor/activity") {
        if (method != "POST") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use POST /api/monitor/activity\"}";
        } else {
            auto* module = m_registry
                ? qobject_cast<MonitorModule*>(m_registry->firstOfType(QStringLiteral("monitor")))
                : nullptr;
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (!module) {
                code = 503; reason = "Service Unavailable";
                out = "{\"error\":\"aucun module 'monitor' configure\"}";
            } else if (!doc.isObject()) {
                code = 400; reason = "Bad Request";
                out = "{\"error\":\"corps JSON attendu\"}";
            } else {
                const qint64 id = module->ingestActivity(doc.object());
                if (id < 0) {
                    code = 400; reason = "Bad Request";
                    out = "{\"accepted\":false,\"error\":\"champ 'type' manquant ou stockage indisponible\"}";
                } else {
                    out = toJson(QJsonObject{{"accepted", true}, {"id", static_cast<double>(id)}});
                }
            }
        }
    }
    // ---- Routes GET ------------------------------------------------------
    else if (method != "GET") {
        code = 405; reason = "Method Not Allowed";
        out = "{\"error\":\"method not allowed\"}";
    } else if (path == "/" || path == "/index.html") {
        reply(sock, 200, "OK", pages::PortalPage::render(siteWatchReports()), "text/html; charset=utf-8");
        return;
    } else if (path == "/meteohub") {
        // Page d'accueil : c'est la cible du lien "Analyse avancee" affiche par
        // MeteoHub quand il detecte ce service sur le reseau. Elle doit donc
        // repondre quelque chose d'utile des la premiere version, avant meme
        // que les analyses existent.
        reply(sock, 200, "OK", pages::MeteoHubPage::render(landingPage()), "text/html; charset=utf-8");
        return;
    } else if (path == "/sitewatch") {
        const QJsonArray reports = siteWatchReports();
        reply(sock, 200, "OK", pages::SiteWatchPage::render(siteWatchPage(), reports), "text/html; charset=utf-8");
        return;
    } else if (path == "/photo") {
        // Specialisation Photo : lit l'instantane du module (agregats de morfPhoto
        // deja interpretes), sans interroger morfPhoto a chaque requete.
        auto* module = m_registry
            ? qobject_cast<PhotoAnalyticsModule*>(m_registry->firstOfType(QStringLiteral("photo")))
            : nullptr;
        const QJsonObject snap = module ? module->snapshot() : QJsonObject{{"reachable", false},
            {"last_error", QStringLiteral("aucun module 'photo' configure")}};
        reply(sock, 200, "OK", pages::PhotoPage::render(snap), "text/html; charset=utf-8");
        return;
    } else if (path == "/photo/data") {
        // Donnees brutes de la page Photo (instantane du module = agregats + dataset
        // compact rapatries de morfPhoto). La page /photo les recupere en JS et fait
        // toute l'agregation/le filtrage cote navigateur. Separer donnees et rendu
        // evite d'inliner un gros dataset dans le HTML et permet de recharger seul.
        auto* module = m_registry
            ? qobject_cast<PhotoAnalyticsModule*>(m_registry->firstOfType(QStringLiteral("photo")))
            : nullptr;
        // Handoff PhotoHub : ?source=<baseUrl morfPhoto> => analyser CETTE photothèque,
        // rapatriée à la demande, plutôt que la source périodique configurée.
        QString source;
        const int qm = rawPath.indexOf('?');
        if (qm >= 0) {
            const QUrlQuery q(QString::fromUtf8(rawPath.mid(qm + 1)));
            source = q.queryItemValue(QStringLiteral("source"), QUrl::FullyDecoded);
        }
        if (module && !source.isEmpty())
            out = toJson(module->fetchNow(source));
        else
            out = toJson(module ? module->snapshot()
                                : QJsonObject{{"reachable", false},
                                              {"last_error", QStringLiteral("aucun module 'photo' configure")}});
    } else if (path == "/monitor") {
        // Domaine Monitor : historique des machines. Page autonome qui récupère
        // ses données via /monitor/data.
        reply(sock, 200, "OK", pages::MonitorPage::render(), "text/html; charset=utf-8");
        return;
    } else if (path == "/monitor/data") {
        // Données de la page Monitor : machines connues + vue d'ensemble + séries
        // sous-échantillonnées pour la machine et la période demandées.
        auto* module = m_registry
            ? qobject_cast<MonitorModule*>(m_registry->firstOfType(QStringLiteral("monitor")))
            : nullptr;
        QString machine;
        int periodS = 86400;                 // 24 h par défaut
        const int qm = rawPath.indexOf('?');
        if (qm >= 0) {
            const QUrlQuery q(QString::fromUtf8(rawPath.mid(qm + 1)));
            machine = q.queryItemValue(QStringLiteral("machine"), QUrl::FullyDecoded);
            const int p = q.queryItemValue(QStringLiteral("period")).toInt();
            if (p > 0)
                periodS = p;
        }
        if (!module) {
            out = toJson(QJsonObject{{"machines", QJsonArray{}},
                                     {"error", QStringLiteral("aucun module 'monitor' configure")}});
        } else {
            const qint64 to = QDateTime::currentSecsSinceEpoch();
            const qint64 from = to - periodS;
            // ~300 points : assez pour un tracé net, sans jamais déverser le brut.
            out = toJson(module->data(machine, from, to, 300));
        }
    } else if (path == "/sitewatch/reports") {
        out = toJson(QJsonObject{{"reports", siteWatchReports()}, {"storage", "sqlite"}});
    } else if (path == "/analyses") {
        // Catalogue des analyses : l'interface se construit a partir de cette
        // liste, sans qu'aucune analyse ne soit codee en dur cote page.
        QJsonObject o;
        auto* module = m_registry
            ? qobject_cast<AnalyticsModule*>(m_registry->firstOfType(QStringLiteral("analytics")))
            : nullptr;
        o["analyses"] = module ? module->analysisCatalog() : QJsonArray{};
        out = toJson(o);
    } else if (path == "/healthz") {
        out = "{\"status\":\"ok\"}";
    } else if (path == "/status") {
        out = buildStatusJson();
    } else if (path == "/modules") {
        QJsonObject o;
        o["modules"] = m_registry ? m_registry->modulesJson() : QJsonArray{};
        o["count"]   = m_registry ? m_registry->count() : 0;
        o["ts"]      = static_cast<double>(QDateTime::currentSecsSinceEpoch());
        out = toJson(o);
    } else if (path.startsWith("/modules/")) {
        const QString id = QUrl::fromPercentEncoding(path.mid(9));
        bool found = false;
        const QJsonObject o = m_registry ? m_registry->moduleJson(id, &found) : QJsonObject{};
        if (found) { out = toJson(o); }
        else { code = 404; reason = "Not Found"; out = "{\"error\":\"module not found\"}"; }
    } else {
        code = 404; reason = "Not Found";
        out = "{\"error\":\"not found\"}";
    }

    reply(sock, code, reason, out);
}

QByteArray HttpServer::handleAnalyzePost(const QByteArray& body, int& code, QByteArray& reason) const {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        code = 400; reason = "Bad Request";
        return "{\"error\":\"corps JSON invalide\"}";
    }

    auto* module = m_registry
        ? qobject_cast<AnalyticsModule*>(m_registry->firstOfType(QStringLiteral("analytics")))
        : nullptr;
    if (!module) {
        code = 503; reason = "Service Unavailable";
        return "{\"error\":\"aucun module d'analyse actif\"}";
    }

    // Une analyse qui echoue faute de donnees n'est pas une erreur HTTP : le
    // service a bien repondu, et le corps explique pourquoi le resultat manque.
    // Reserver les codes d'erreur aux vrais problemes de requete garde les
    // diagnostics lisibles.
    return toJson(module->analyze(doc.object()));
}

QByteArray HttpServer::handleCleanupPost(const QByteArray& body, int& code, QByteArray& reason) const {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        code = 400; reason = "Bad Request";
        return "{\"error\":\"corps JSON invalide\"}";
    }

    auto* module = m_registry
        ? qobject_cast<AnalyticsModule*>(m_registry->firstOfType(QStringLiteral("analytics")))
        : nullptr;
    if (!module) {
        code = 503; reason = "Service Unavailable";
        return "{\"error\":\"aucun module d'analyse actif\"}";
    }
    return toJson(module->cleanupData(doc.object()));
}

QByteArray HttpServer::handleSiteWatchPost(const QByteArray& body, int& code, QByteArray& reason) {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        code = 400; reason = "Bad Request"; return "{\"error\":\"corps JSON invalide\"}";
    }
    QJsonObject report = doc.object();
    const QString siteId = report.value("site_id").toString();
    if (siteId.isEmpty()) { code = 400; reason = "Bad Request"; return "{\"error\":\"site_id manquant\"}"; }
    report["received_at"] = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    if (!saveSiteWatchReport(report)) {
        code = 503; reason = "Service Unavailable";
        return toJson(QJsonObject{{"error", "stockage SiteWatch indisponible"},
                                  {"detail", m_siteWatchStoreError}});
    }
    m_siteWatchReports.insert(siteId, report);
    return toJson(QJsonObject{{"ok", true}, {"site_id", siteId}});
}

QByteArray HttpServer::siteWatchPage() const {
    const auto formatNumber = [](double value) {
        return QLocale(QLocale::French, QLocale::France).toString(static_cast<qlonglong>(value));
    };
    const auto topEntries = [&formatNumber](const QJsonObject& values) {
        QVector<QPair<QString, double>> ranked;
        ranked.reserve(values.size());
        for (auto it = values.begin(); it != values.end(); ++it)
            ranked.append({it.key(), it.value().toDouble()});
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        QStringList result;
        for (int i = 0; i < ranked.size() && i < 3; ++i)
            result << QStringLiteral("%1 (%2)").arg(ranked[i].first.toHtmlEscaped(), formatNumber(ranked[i].second));
        return result.isEmpty() ? QStringLiteral("aucune") : result.join(QStringLiteral(" · "));
    };
    const auto sumDaily = [](const QJsonObject& values) {
        double sum = 0;
        for (auto it = values.begin(); it != values.end(); ++it) sum += it.value().toDouble();
        return sum;
    };
    const auto peakDay = [&formatNumber](const QJsonObject& values) {
        QString day;
        double peak = 0;
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (it.value().toDouble() > peak) { day = it.key(); peak = it.value().toDouble(); }
        }
        return day.isEmpty() ? QStringLiteral("aucun")
                             : QStringLiteral("%1 (%2)").arg(day.toHtmlEscaped(), formatNumber(peak));
    };
    const auto unusualDays = [&formatNumber](const QJsonObject& values) {
        QVector<double> samples;
        for (auto it = values.begin(); it != values.end(); ++it) samples.append(it.value().toDouble());
        if (samples.size() < 7) return QStringLiteral("pas assez de jours observés");
        double mean = 0; for (double value : samples) mean += value; mean /= samples.size();
        double variance = 0; for (double value : samples) variance += (value - mean) * (value - mean);
        const double threshold = mean + 2.0 * std::sqrt(variance / samples.size());
        QStringList result;
        for (auto it = values.begin(); it != values.end(); ++it)
            if (it.value().toDouble() > threshold)
                result << QStringLiteral("%1 (%2)").arg(it.key().toHtmlEscaped(), formatNumber(it.value().toDouble()));
        return result.isEmpty() ? QStringLiteral("aucune journée anormale") : result.join(QStringLiteral(" · "));
    };

    const QJsonArray reports = siteWatchReports();
    QString page = QStringLiteral(R"HTML(<!doctype html><html lang="fr"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="refresh" content="30"><title>morfAnalytics - SiteWatch</title><style>body{margin:0;background:#15171b;color:#e7e9ec;font:16px system-ui;padding:2rem}.wrap{max-width:70rem;margin:auto}.card{background:#1e2126;border:1px solid #2c3037;border-radius:12px;padding:1.25rem;margin:1rem 0}h1{margin:0}h2{font-size:1.05rem}.muted{color:#99a1ad}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(13rem,1fr));gap:1rem}.number{font-size:2rem;font-weight:700}.vb{font-size:.8rem;font-weight:600;vertical-align:middle;color:#6f9bff;background:rgba(111,155,255,.12);border:1px solid rgba(111,155,255,.3);border-radius:999px;padding:.1rem .5rem;margin-left:.4rem}</style><body><div class="wrap"><p><a href="/" style="color:#6f9bff">← morfAnalytics</a></p><h1>Analyse des sites <span class="vb">v%1</span></h1><p class="muted">Synthèses reçues de SiteWatch · actualisation automatique toutes les 30 secondes.</p>)HTML").arg(morfanalytics::version());
    if (reports.isEmpty()) {
        page += QStringLiteral("<section class=\"card\">Aucune synthèse SiteWatch n'est encore enregistrée.</section>");
    }
    for (const QJsonValue& value : reports) {
        const QJsonObject report = value.toObject();
        const QJsonObject stats = report.value(QStringLiteral("stats")).toObject();
        const double errors = stats.value(QStringLiteral("errors_404")).toDouble()
                            + stats.value(QStringLiteral("errors_403")).toDouble()
                            + stats.value(QStringLiteral("errors_500")).toDouble();
        const double requests = stats.value(QStringLiteral("requests")).toDouble();
        const QString verdict = stats.value(QStringLiteral("errors_500")).toDouble() > 0
            ? QStringLiteral("À surveiller : erreurs serveur détectées.")
            : stats.value(QStringLiteral("attacks")).toDouble() > 0
                ? QStringLiteral("À surveiller : tentatives sensibles détectées.")
                : QStringLiteral("Activité globalement normale.");
        const QString site = report.value(QStringLiteral("site_label")).toString(
            report.value(QStringLiteral("site_id")).toString()).toHtmlEscaped();
        const QString rate = requests > 0 ? QLocale(QLocale::French, QLocale::France).toString(errors * 100.0 / requests, 'f', 2) : QStringLiteral("0,00");
        const QString siteId = report.value(QStringLiteral("site_id")).toString();
        const QJsonArray history = siteWatchHistory(siteId);
        QJsonObject dailyTraffic = stats.value(QStringLiteral("daily_humans")).toObject();
        const QJsonObject dailyBots = stats.value(QStringLiteral("daily_bots")).toObject();
        const QJsonObject dailyAttacks = stats.value(QStringLiteral("daily_attacks")).toObject();
        for (auto it = dailyBots.begin(); it != dailyBots.end(); ++it)
            dailyTraffic[it.key()] = dailyTraffic.value(it.key()).toDouble() + it.value().toDouble();
        int attackDays = 0;
        for (auto it = dailyAttacks.begin(); it != dailyAttacks.end(); ++it)
            if (it.value().toDouble() > 0) ++attackDays;
        page += QStringLiteral("<section class=\"card\"><h2>%1</h2><p>%2</p><div class=\"grid\"><div><span class=\"number\">%3</span><br><span class=\"muted\">requêtes analysées</span></div><div><span class=\"number\">%4</span><br><span class=\"muted\">erreurs HTTP (%5 %)</span></div><div><span class=\"number\">%6</span><br><span class=\"muted\">requêtes de robots</span></div><div><span class=\"number\">%7</span><br><span class=\"muted\">tentatives sensibles</span></div></div><h2>Points à examiner</h2><p>Pages les plus touchées : %8</p><p>Robots les plus actifs : %9</p><p>Pages les plus visitées : %10</p><p class=\"muted\">Période : %11 → %12</p></section>")
            .arg(site).arg(verdict).arg(formatNumber(requests)).arg(formatNumber(errors)).arg(rate)
            .arg(formatNumber(stats.value(QStringLiteral("bots")).toDouble()))
            .arg(formatNumber(stats.value(QStringLiteral("attacks")).toDouble()))
            .arg(topEntries(stats.value(QStringLiteral("top_attacked")).toObject()))
            .arg(topEntries(stats.value(QStringLiteral("bot_counts")).toObject()))
            .arg(topEntries(stats.value(QStringLiteral("top_pages")).toObject()))
            .arg(report.value(QStringLiteral("from")).toString().toHtmlEscaped())
            .arg(report.value(QStringLiteral("to")).toString().toHtmlEscaped());
        if (history.size() < 2) {
            const int progress = std::min(100, static_cast<int>(history.size()) * 50);
            page += QStringLiteral("<section class=\"card\"><h2>Analyses approfondies · en apprentissage</h2><p>Une synthèse supplémentaire permettra de comparer l'activité dans le temps.</p><div style=\"background:#2c3037;border-radius:5px;height:8px\"><div style=\"background:#6f9bff;border-radius:5px;height:8px;width:%1%\"></div></div><p class=\"muted\">%2 synthèse sur 2 nécessaire pour les comparaisons.</p></section>")
                .arg(progress).arg(history.size());
        } else {
            const QJsonObject previousStats = history.at(1).toObject().value(QStringLiteral("stats")).toObject();
            const double previousRequests = previousStats.value(QStringLiteral("requests")).toDouble();
            const double requestChange = previousRequests > 0 ? (requests - previousRequests) * 100.0 / previousRequests : 0;
            const double previousErrors = previousStats.value(QStringLiteral("errors_404")).toDouble()
                + previousStats.value(QStringLiteral("errors_403")).toDouble() + previousStats.value(QStringLiteral("errors_500")).toDouble();
            const double previousRate = previousRequests > 0 ? previousErrors * 100.0 / previousRequests : 0;
            QStringList newBots;
            const QJsonObject previousBots = previousStats.value(QStringLiteral("bot_counts")).toObject();
            const QJsonObject currentBots = stats.value(QStringLiteral("bot_counts")).toObject();
            for (auto it = currentBots.begin(); it != currentBots.end(); ++it)
                if (!previousBots.contains(it.key())) newBots << it.key().toHtmlEscaped();
            const QString botNovelty = newBots.isEmpty() ? QStringLiteral("aucun nouveau robot détecté")
                : newBots.mid(0, 3).join(QStringLiteral(" · "));
            page += QStringLiteral("<section class=\"card\"><h2>Analyses approfondies</h2><div class=\"grid\"><div><strong>%1 %</strong><br><span class=\"muted\">évolution des requêtes vs analyse précédente</span></div><div><strong>%2 % → %3 %</strong><br><span class=\"muted\">taux d'erreurs HTTP</span></div><div><strong>%4</strong><br><span class=\"muted\">jours avec tentatives sensibles</span></div></div><p>Variation inhabituelle du trafic : %5.</p><p>Pic de trafic : %6. Pic de robots : %7. Pic d'erreurs 404 : %8.</p><p>Nouveaux robots : %9.</p><p>Pages ciblées de façon récurrente : %10.</p></section>")
                .arg(QLocale(QLocale::French, QLocale::France).toString(requestChange, 'f', 1))
                .arg(QLocale(QLocale::French, QLocale::France).toString(previousRate, 'f', 2)).arg(rate)
                .arg(attackDays).arg(unusualDays(dailyTraffic)).arg(peakDay(dailyTraffic))
                .arg(peakDay(stats.value(QStringLiteral("daily_bots")).toObject()))
                .arg(peakDay(stats.value(QStringLiteral("daily_404")).toObject())).arg(botNovelty)
                .arg(topEntries(stats.value(QStringLiteral("top_attacked")).toObject()));
        }
    }
    page += QStringLiteral("</div></body></html>");
    return page.toUtf8();
}

QByteArray HttpServer::landingPage() {
    // Page autonome : ni CDN, ni fichier externe. Le service doit rester
    // consultable sur un reseau local sans acces Internet, et s'installer par
    // simple copie du binaire (aucun dossier de ressources a deployer).
    //
    // Le catalogue d'analyses est lu depuis /analyses : la page ne code en dur
    // aucune analyse, elle sait seulement les METTRE EN FORME. Ajouter une
    // analyse cote serveur la fait apparaitre ici sans toucher a cette page
    // (elle s'affichera avec le rendu generique tant qu'aucun rendu dedie
    // n'est ecrit pour elle).
    return R"HTML(<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>morfAnalytics - Météo</title>
<style>
  :root {
    color-scheme: light dark;
    --bg: #f4f5f7; --fg: #1b1d21; --muted: #6b7280;
    --card: #ffffff; --line: #e3e5e9;
    --accent: #2f6fed; --warm: #e0662b; --cold: #2f7fd6;
    --ok: #2e8b57; --warn: #d98324; --bad: #c8483a;
  }
  @media (prefers-color-scheme: dark) {
    :root { --bg: #15171b; --fg: #e7e9ec; --muted: #99a1ad;
            --card: #1e2126; --line: #2c3037; --accent: #6f9bff; }
  }
  * { box-sizing: border-box; }
  body { margin: 0; padding: 2.5rem 1.25rem 5rem; background: var(--bg); color: var(--fg);
         font-family: system-ui, -apple-system, "Segoe UI", sans-serif; line-height: 1.5; }
  .wrap { max-width: 72rem; margin: 0 auto; }
  header.top { margin-bottom: 2rem; }
  a.back { display: inline-block; margin-bottom: .6rem; color: var(--accent);
           text-decoration: none; font-size: .9rem; }
  a.back:hover { text-decoration: underline; }
  h1 { margin: 0 0 .2rem; font-size: 2rem; letter-spacing: -.025em; }
  .version-badge { font-size: .8rem; font-weight: 600; vertical-align: middle;
                   color: var(--accent);
                   background: color-mix(in srgb, var(--accent) 12%, transparent);
                   border: 1px solid color-mix(in srgb, var(--accent) 30%, transparent);
                   border-radius: 999px; padding: .1rem .5rem; margin-left: .4rem; }
  .sub { margin: 0; color: var(--muted); }

  h2.section { font-size: .8rem; text-transform: uppercase; letter-spacing: .08em;
               color: var(--muted); margin: 2.5rem 0 .9rem; font-weight: 600; }

  .grid { display: grid; gap: 1.25rem;
          grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .analysis-row + .analysis-row { margin-top: 1.25rem; }
  .analysis-row .card { height: 100%; }
  .analysis-row.solo { grid-template-columns: minmax(0, 1fr); }
  .card { background: var(--card); border: 1px solid var(--line); border-radius: .75rem;
          padding: 1.35rem 1.5rem; }
  .card h3 { margin: 0 0 1rem; font-size: 1.05rem; font-weight: 650; }
  .card h4 { margin: 1.4rem 0 .65rem; font-size: .95rem; }
  .card h4:first-child { margin-top: 0; }
  .card.wide { grid-column: 1 / -1; }
  .summary { margin: 0 0 2rem; }
  .summary h2 { margin: 0 0 .65rem; font-size: .8rem; text-transform: uppercase;
                letter-spacing: .08em; color: var(--muted); font-weight: 600; }
  .summary .summary-main { font-size: 1.2rem; font-weight: 600; margin: 0 0 .45rem; }
  .summary .summary-lines { margin: 0; color: var(--muted); }
  .summary .summary-lines span + span::before { content: " · "; }

  dl { display: grid; grid-template-columns: auto 1fr; gap: .35rem .9rem; margin: 0; }
  dt { color: var(--muted); white-space: nowrap; }
  dd { min-width: 0; margin: 0; text-align: right; font-variant-numeric: tabular-nums;
       white-space: nowrap; }
  dd.wide-value { grid-column: 1 / -1; white-space: normal; }

  .hero { font-size: 2.6rem; font-weight: 600; letter-spacing: -.03em; line-height: 1.1; }
  .hero .unit { font-size: 1.2rem; color: var(--muted); font-weight: 400; }
  .hero-sub { color: var(--muted); margin-top: .15rem; }
  .quote { font-size: 1.15rem; font-weight: 500; }

  .chips { display: flex; flex-wrap: wrap; gap: .4rem; margin-top: .9rem; }
  .chip { background: color-mix(in srgb, var(--accent) 10%, transparent);
          border: 1px solid color-mix(in srgb, var(--accent) 25%, transparent);
          color: var(--fg); border-radius: 999px; padding: .15rem .65rem; font-size: .85rem;
          font-variant-numeric: tabular-nums; }
  .chip b { font-weight: 600; }

  .badge { display: inline-block; border-radius: 999px; padding: .1rem .6rem;
           font-size: .85rem; font-weight: 600; }
  .badge.ok   { background: color-mix(in srgb, var(--ok) 18%, transparent);   color: var(--ok); }
  .badge.warn { background: color-mix(in srgb, var(--warn) 20%, transparent); color: var(--warn); }
  .badge.bad  { background: color-mix(in srgb, var(--bad) 18%, transparent);  color: var(--bad); }

  .note { font-size: .9rem; color: var(--muted); margin: .9rem 0 0; }
  .warning { font-size: .82rem; margin: .8rem 0 0; padding: .5rem .7rem; border-radius: .4rem;
             background: color-mix(in srgb, var(--warn) 12%, transparent);
             border-left: 3px solid var(--warn); }
  .unavailable { color: var(--muted); font-size: .9rem; }
  .learning { color: var(--muted); font-size: .9rem; }
  .learning p { margin: .55rem 0 0; }
  .progress { height: .45rem; margin-top: .5rem; border-radius: 999px;
              overflow: hidden; background: color-mix(in srgb, var(--muted) 18%, transparent); }
  .progress span { display: block; height: 100%; border-radius: inherit; background: var(--accent); }
  details.maintenance { margin-top: 2.5rem; }
  details.maintenance > summary { cursor: pointer; color: var(--muted); font-size: .8rem;
                                  text-transform: uppercase; letter-spacing: .08em; font-weight: 600; }
  details.maintenance[open] > summary { margin-bottom: .9rem; }

  table { width: 100%; border-collapse: collapse; font-size: .92rem; }
  th, td { text-align: right; padding: .55rem .4rem; border-bottom: 1px solid var(--line); }
  th:first-child, td:first-child { text-align: left; }
  th { color: var(--muted); font-weight: 500; font-size: .8rem; }
  tbody tr:last-child td { border-bottom: none; }
  .scroll { overflow-x: auto; }
  details.analysis-detail { margin-top: 1rem; }
  details.analysis-detail > summary { cursor: pointer; color: var(--accent); font-size: .9rem; }
  details.analysis-detail[open] > summary { margin-bottom: .8rem; }
  details.advanced-section { margin-top: 2.75rem; }
  details.advanced-section > summary { cursor: pointer; color: var(--muted); font-size: .8rem;
                                       text-transform: uppercase; letter-spacing: .08em; font-weight: 650; }
  details.advanced-section[open] > summary { margin-bottom: .9rem; }
  details.service-details { margin-top: 2.75rem; }
  details.service-details > summary { cursor: pointer; color: var(--muted); font-size: .8rem;
                                      text-transform: uppercase; letter-spacing: .08em; font-weight: 650; }
  details.service-details[open] > summary { margin-bottom: .9rem; }

  .bars { display: grid; gap: .3rem; margin-top: .3rem; }
  .bar-row { display: grid; grid-template-columns: 4.5rem 1fr auto; gap: .5rem;
             align-items: center; font-size: .85rem; }
  .bar { height: .55rem; border-radius: 999px; background: var(--accent); min-width: 2px; }
  .bar-track { background: color-mix(in srgb, var(--muted) 15%, transparent);
               border-radius: 999px; }

  button { font: inherit; font-size: .88rem; border-radius: .45rem; cursor: pointer;
           padding: .4rem .85rem; border: 1px solid var(--line);
           background: var(--card); color: var(--fg); }
  button:hover { border-color: var(--accent); }
  button:disabled { opacity: .5; cursor: default; }
  button.danger { color: var(--bad);
                  border-color: color-mix(in srgb, var(--bad) 40%, transparent); }
  button.danger:hover { background: color-mix(in srgb, var(--bad) 10%, transparent); }
  .actions { display: flex; flex-wrap: wrap; gap: .5rem; align-items: center;
             margin-top: .6rem; }
  .field { display: flex; flex-direction: column; gap: .2rem; font-size: .82rem;
           color: var(--muted); }
  input[type="datetime-local"], select {
    font: inherit; font-size: .88rem; color: var(--fg); background: var(--bg);
    border: 1px solid var(--line); border-radius: .45rem; padding: .35rem .5rem; }
  .cleanup-result { font-size: .88rem; margin-top: .6rem; }
  .cleanup-result.ok { color: var(--ok); }
  .cleanup-result.err { color: var(--bad); }
  .refresh-line { display: flex; align-items: center; gap: .6rem; margin-top: .5rem;
                  font-size: .82rem; color: var(--muted); }

  .anomaly { font-size: 2rem; font-weight: 600; letter-spacing: -.02em; }
  .anomaly.warm { color: var(--warm); }
  .anomaly.cold { color: var(--cold); }

  svg.spark { width: 100%; height: 7rem; display: block; margin-top: .3rem; }
  .spark-grid { stroke: var(--line); stroke-width: .6; }
  .spark-label { fill: var(--muted); font-size: 3px; font-family: inherit; }
  .spark-point { fill: var(--accent); stroke: var(--card); stroke-width: .8; }
  code { background: color-mix(in srgb, var(--muted) 15%, transparent);
         padding: .1rem .35rem; border-radius: .25rem; font-size: .85em; }
  footer { margin-top: 3rem; font-size: .82rem; color: var(--muted); }
  @media (max-width: 42rem) {
    body { padding: 1.5rem 1rem 3rem; }
    .card { padding: 1.15rem; }
    h1 { font-size: 1.7rem; }
    .grid { grid-template-columns: minmax(0, 1fr); }
  }
</style>
</head>
<body>
<div class="wrap">
  <header class="top">
    <a id="backlink" class="back" href="#" hidden>&larr; Retour à MeteoHub</a>
    <h1>Analyse de la météo <span id="version-badge" class="version-badge"></span></h1>
    <p class="sub">Analyses avancées - <span id="hostline">…</span></p>
    <div class="refresh-line">
      <span>Analyses actualisées : <span id="refreshed">…</span></span>
      <button id="refresh-btn" type="button">Actualiser</button>
    </div>
  </header>

  <section id="summary" class="card summary" hidden aria-live="polite"></section>

  <div id="groups"></div>

  <details class="service-details">
    <summary>Service et collecte</summary>
    <div class="grid">
      <div class="card">
        <h3>Service</h3>
        <dl>
          <dt>État</dt><dd id="state">…</dd>
          <dt>Version</dt><dd id="version">…</dd>
          <dt>Machine</dt><dd id="host">…</dd>
        </dl>
      </div>
      <div class="card">
        <h3>Collecte</h3>
        <dl>
          <dt>Source</dt><dd id="source">…</dd>
          <dt>Mesures en cache</dt><dd id="points">…</dd>
          <dt>Période couverte</dt><dd id="period">…</dd>
          <dt>Dernière collecte</dt><dd id="lastsync">…</dd>
        </dl>
        <div id="collecte-warn"></div>
      </div>
    </div>
  </details>

  <details class="maintenance">
    <summary>Maintenance avancée</summary>
    <div class="grid">
    <div class="card">
      <h3>Mesures aberrantes</h3>
      <p class="note" style="margin-top:0">Relevés portant une pression physiquement
        impossible (capteur en panne : 0&nbsp;hPa, 0&nbsp;°C…). La ligne entière est
        neutralisée, le relevé n'étant pas une mesure.</p>
      <div class="actions">
        <button id="scan-faults" type="button">Rechercher</button>
        <button id="fix-faults" type="button" hidden>Neutraliser</button>
        <span id="faults-count" class="unavailable"></span>
      </div>
      <div id="faults-result" class="cleanup-result"></div>
    </div>
    <div class="card">
      <h3>Neutraliser une plage</h3>
      <p class="note" style="margin-top:0">Marque comme manquantes les valeurs d'une
        période (capteur resté en défaut, valeurs douteuses…). Les lignes restent en
        place : rien n'est re-téléchargé depuis l'appareil.</p>
      <div class="actions">
        <label class="field">Du <input type="datetime-local" id="inv-from"></label>
        <label class="field">Au <input type="datetime-local" id="inv-to"></label>
        <label class="field">Canal
          <select id="inv-channel">
            <option value="">tous</option>
            <option value="temp">température</option>
            <option value="hum">humidité</option>
            <option value="pres">pression</option>
          </select>
        </label>
      </div>
      <div class="actions">
        <button id="inv-preview" type="button">Compter</button>
        <button id="inv-apply" type="button" class="danger" hidden>Neutraliser</button>
      </div>
      <div id="inv-result" class="cleanup-result"></div>
    </div>
    <div class="card">
      <h3>Purge totale</h3>
      <p class="note" style="margin-top:0">Vide entièrement le cache local. Il se
        reconstruit depuis l'appareil au prochain cycle de collecte - les mesures
        d'origine, sur l'appareil, ne sont jamais touchées. Les neutralisations
        manuelles sont alors perdues (les pannes capteur, elles, restent filtrées
        à l'import).</p>
      <div class="actions">
        <button id="purge-all" type="button" class="danger">Vider le cache</button>
      </div>
      <div id="purge-result" class="cleanup-result"></div>
    </div>
    </div>
  </details>

  <footer>
    État détaillé au format JSON : <code>/status</code>, <code>/modules</code>,
    <code>/analyses</code>, <code>/healthz</code>.
    Les mesures d'origine restent sur MeteoHub, seule source de vérité ;
    ce service travaille sur une copie en lecture seule.
  </footer>
</div>

<script>
const GROUP_LABELS = {
  nowcast: 'Conditions et prévision locale',
  climat: 'Climatologie',
  qualite: 'Qualité des données',
  avancé: 'Analyses approfondies'
};

// Les noms de champs de l'API restent stables pour les integrations, mais ils
// ne doivent jamais devenir des intitulés bruts dans l'interface.
const FIELD_LABELS = {
  days: 'Période analysée', window_days: 'Période analysée',
  heat_threshold: 'Seuil de chaleur', cold_threshold: 'Seuil de froid',
  min_days: 'Durée minimale', threshold: 'Sensibilité de détection',
  max_lag_hours: 'Décalage maximal recherché', channel: 'Mesure analysée',
  total_anomalies: 'Valeurs inhabituelles détectées', residual_std: 'Variations non expliquées'
};

const humanLabel = (key) => FIELD_LABELS[key] || key
  .replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
const channelLabel = (key) => ({ temp: 'Température', hum: 'Humidité', pres: 'Pression' }[key] || key);
const pairLabel = (pair) => String(pair).split('~').map(channelLabel).join(' et ');

const esc = (s) => String(s).replace(/[&<>"]/g, (c) =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

const fmtDate = (ts) => (!ts || ts <= 0)
  ? 'jamais' : new Intl.DateTimeFormat('fr-FR', {
    dateStyle: 'medium', timeStyle: 'short'
  }).format(new Date(ts * 1000));
const fmtDay = (iso) => {
  const d = new Date(iso);
  return isNaN(d) ? iso : d.toLocaleDateString('fr-FR');
};
const num = (v, unit) => (v === undefined || v === null)
  ? '-' : `${v}${unit ? ' ' + unit : ''}`;

const riskBadge = (level) => {
  const cls = ['élevé', 'très élevé', 'sec', 'très sec'].includes(level)
    ? 'bad' : (level === 'modéré' ? 'warn' : 'ok');
  return `<span class="badge ${cls}">${esc(level)}</span>`;
};

function notes(r) {
  let html = '';
  if (r.warning) html += `<p class="warning">${esc(r.warning)}</p>`;
  if (r.storm_note) html += `<p class="warning">${esc(r.storm_note)}</p>`;
  if (r.note) html += `<p class="note">${esc(r.note)}</p>`;
  return html;
}

function dl(rows) {
  const body = rows.filter((r) => r[1] !== undefined && r[1] !== null)
    .map(([k, v]) => `<dt>${esc(k)}</dt><dd>${v}</dd>`).join('');
  return `<dl>${body}</dl>`;
}

// Une valeur reste a droite de son libelle tant que les deux tiennent sur la
// meme ligne. Sinon, elle prend la ligne suivante et toute la largeur : aucun
// mot n'est coupe, et le libelle ne peut jamais etre recouvert.
function fitDefinitionLists() {
  document.querySelectorAll('dl dd').forEach((value) => {
    value.classList.remove('wide-value');
    if (value.scrollWidth > value.clientWidth + 1)
      value.classList.add('wide-value');
  });
}

function renderSummary(results) {
  const summary = document.getElementById('summary');
  const current = results.current;
  if (!current || current.ok === false || current.temperature === undefined) {
    summary.hidden = true;
    return;
  }

  const temp = current.temperature;
  let headline = temp >= 30 ? 'Conditions très chaudes.'
    : temp >= 25 ? 'Conditions chaudes.'
    : temp <= 0 ? 'Conditions hivernales.' : 'Conditions locales stables.';
  const lines = [`Température mesurée : ${num(temp, '°C')}`];

  const trend = results.temp_trend;
  if (trend && trend.ok !== false && trend.tendency)
    lines.push(`Température : ${esc(trend.tendency)}`);
  const pressure = results.pressure_trend;
  if (pressure && pressure.ok !== false && pressure.tendency)
    lines.push(`Pression : ${esc(pressure.tendency)}`);

  const alerts = [];
  [results.heat_risk, results.dry_air, results.fog_risk, results.frost_risk]
    .filter((r) => r && r.ok !== false && ['modéré', 'élevé', 'très élevé', 'sec', 'très sec'].includes(r.risk))
    .forEach((r) => alerts.push(r.title || 'condition à surveiller'));
  if (pressure && pressure.storm_warning) alerts.push('chute de pression rapide');
  lines.push(alerts.length ? `À surveiller : ${esc(alerts.join(', '))}`
                           : 'Aucune alerte locale détectée');

  const forecast = results.zambretti;
  if (forecast && forecast.ok !== false && forecast.forecast)
    lines.push(esc(forecast.forecast));

  summary.innerHTML = `<h2>En un coup d’œil</h2><p class="summary-main">${headline}</p>` +
    `<p class="summary-lines">${lines.map((line) => `<span>${line}</span>`).join('')}</p>`;
  summary.hidden = false;
}

// Courbe simple, tracee a la main : evite d'embarquer une bibliotheque de
// graphiques pour une seule serie de 24 valeurs. Les bornes et les heures
// donnent une echelle explicite : une courbe seule ne permet pas de juger une
// valeur ni l'amplitude reelle du cycle.
function sparkline(values) {
  const pts = values.map((v, i) => [i, v]).filter(([, v]) => v !== null);
  if (pts.length < 2) return '';
  const ys = pts.map(([, v]) => v);
  const lo = Math.min(...ys), hi = Math.max(...ys);
  const span = (hi - lo) || 1;
  const W = 100, H = 36, left = 16, right = 3, top = 4, bottom = 7;
  const yFor = (v) => H - bottom - ((v - lo) / span) * (H - top - bottom);
  const coords = pts.map(([i, v]) => {
    const x = left + (i / 23) * (W - left - right);
    const y = yFor(v);
    return `${x.toFixed(2)},${y.toFixed(2)}`;
  }).join(' ');
  const peak = pts.reduce((best, point) => point[1] > best[1] ? point : best);
  const low = pts.reduce((best, point) => point[1] < best[1] ? point : best);
  const point = ([i, v]) => `${(left + (i / 23) * (W - left - right)).toFixed(2)},${yFor(v).toFixed(2)}`;
  return `<svg class="spark" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none"
      role="img" aria-label="Cycle journalier moyen, de ${lo.toFixed(1)} à ${hi.toFixed(1)} degrés Celsius">
    <line class="spark-grid" x1="${left}" y1="${top}" x2="${W - right}" y2="${top}"/>
    <line class="spark-grid" x1="${left}" y1="${H - bottom}" x2="${W - right}" y2="${H - bottom}"/>
    <text class="spark-label" x="0" y="${top + 2.5}">${hi.toFixed(1)} °C</text>
    <text class="spark-label" x="0" y="${H - bottom}">${lo.toFixed(1)} °C</text>
    <polyline points="${coords}" fill="none" stroke="var(--accent)"
              stroke-width="1" stroke-linejoin="round" stroke-linecap="round"/>
    <circle class="spark-point" cx="${point(peak).split(',')[0]}" cy="${point(peak).split(',')[1]}" r="1.2"/>
    <circle class="spark-point" cx="${point(low).split(',')[0]}" cy="${point(low).split(',')[1]}" r="1.2"/>
    <text class="spark-label" x="${left}" y="${H - 1}">0 h</text>
    <text class="spark-label" x="${left + (12 / 23) * (W - left - right)}" y="${H - 1}" text-anchor="middle">12 h</text>
    <text class="spark-label" x="${W - right}" y="${H - 1}" text-anchor="end">23 h</text>
  </svg>`;
}

const fmtTime = (ts) => (!ts || ts <= 0) ? ''
  : new Date(ts * 1000).toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' });
const signed = (v, unit) => (v === undefined || v === null)
  ? null : `${v > 0 ? '+' : ''}${num(v, unit)}`;

const RENDERERS = {
  current: (r) => {
    let html = `<div class="hero">${num(r.temperature)}<span class="unit"> °C</span></div>`;
    const heroSub = [];
    if (r.humidex !== undefined && r.humidex > r.temperature + 0.4)
      heroSub.push(`ressenti ${num(r.humidex, '°C')}`);
    if (r.ts) heroSub.push(`mesuré à ${fmtTime(r.ts)}`);
    if (heroSub.length)
      html += `<div class="hero-sub">${heroSub.join(' - ')}</div>`;
    const chips = [];
    if (r.humidity !== undefined) chips.push(['Humidité', num(r.humidity, '%')]);
    if (r.dew_point !== undefined) chips.push(['Point de rosée', num(r.dew_point, '°C')]);
    if (r.dew_point_spread !== undefined) chips.push(['Écart rosée', num(r.dew_point_spread, '°C')]);
    if (r.absolute_humidity !== undefined) chips.push(['Humidité absolue', num(r.absolute_humidity, 'g/m³')]);
    if (r.pressure_sea_level !== undefined) chips.push(['Pression (mer)', num(r.pressure_sea_level, 'hPa')]);
    html += `<div class="chips">${chips.map(([k, v]) =>
      `<span class="chip">${esc(k)} <b>${v}</b></span>`).join('')}</div>`;
    return html + notes(r);
  },

  heat_risk: (r) => `<div>${riskBadge(r.risk || '-')}</div>` + dl([
    ['Température', num(r.temperature, '°C')],
    ['Humidité', num(r.humidity, '%')],
    ['Humidex', num(r.humidex, '°C')]
  ]) + notes(r),

  dry_air: (r) => `<div>${riskBadge(r.risk || '-')}</div>` + dl([
    ['Température', num(r.temperature, '°C')],
    ['Humidité', num(r.humidity, '%')],
    ['Déficit de vapeur', num(r.vpd_kpa, 'kPa')]
  ]) + notes(r),

  zambretti: (r) => `<div class="quote">${esc(r.forecast || '-')}</div>` +
    `<div class="chips">
       <span class="chip">Pression <b>${num(r.pressure_sea_level, 'hPa')}</b></span>
       <span class="chip">3 h <b>${r.delta_3h > 0 ? '+' : ''}${num(r.delta_3h, 'hPa')}</b></span>
       <span class="chip">${esc(r.tendency || '')}</span>
     </div>` + notes(r),

  pressure_trend: (r) => dl([
    ['Pression (niveau mer)', num(r.pressure_sea_level, 'hPa')],
    ['Variation 1 h', signed(r.delta_1h, 'hPa')],
    ['Variation 3 h', signed(r.delta_3h, 'hPa')],
    ['Tendance', esc(r.tendency || '')],
    ['Alerte orage', r.storm_warning === undefined ? null :
      (r.storm_warning ? '<span class="badge bad">oui</span>' : '<span class="badge ok">non</span>')]
  ]) + notes(r),

  temp_trend: (r) => dl([
    ['Température', num(r.temperature, '°C')],
    ['Variation 1 h', signed(r.delta_1h, '°C')],
    ['Variation 3 h', signed(r.delta_3h, '°C')],
    ['Écart avec hier même heure', signed(r.delta_24h, '°C')],
    ['Tendance', esc(r.tendency || '')]
  ]) + notes(r),

  fog_risk: (r) => `<div>${riskBadge(r.risk || '-')}</div>` + dl([
    ['Écart au point de rosée', num(r.dew_point_spread, '°C')],
    ['Évolution sur 2 h', r.spread_trend_2h === undefined ? null :
      `${r.spread_trend_2h > 0 ? '+' : ''}${num(r.spread_trend_2h, '°C')}`]
  ]) + notes(r),

  frost_risk: (r) => `<div>${riskBadge(r.risk || '-')}</div>` + dl([
    ['Température actuelle', num(r.temperature, '°C')],
    ['Refroidissement', num(r.cooling_per_hour, '°C/h')],
    ['Minimum projeté', r.projected_min === undefined ? null : num(r.projected_min, '°C')],
    ['Heures avant l\'aube', r.hours_to_dawn === undefined || r.hours_to_dawn <= 0
      ? null : num(r.hours_to_dawn, 'h')],
    ['Point de rosée', r.dew_point === undefined ? null : num(r.dew_point, '°C')]
  ]) + notes(r),

  normals: (r) => {
    let html = '';
    if (r.anomaly !== undefined) {
      const cls = r.anomaly >= 0 ? 'warm' : 'cold';
      html += `<div class="anomaly ${cls}">${r.anomaly > 0 ? '+' : ''}${r.anomaly} °C</div>
               <div class="hero-sub">par rapport à la normale du jour</div>`;
    }
    html += dl([
      ["Normale du jour", num(r.normal_temp, '°C')],
      ["Aujourd'hui", r.today_temp === undefined ? null : num(r.today_temp, '°C')],
      ['Plage observée', `${num(r.normal_min)} - ${num(r.normal_max, '°C')}`],
      ['Journées prises en compte', r.sample_days],
      ['Années couvertes', r.years]
    ]);
    return html + notes(r);
  },

  degree_days: (r) => {
    let html = dl([
      ['Degrés-jours de chauffage', `<b>${num(r.heating_degree_days)}</b>`],
      ['Degrés-jours de climatisation', `<b>${num(r.cooling_degree_days)}</b>`],
      ['Bases', `${num(r.heating_base, '°C')} / ${num(r.cooling_base, '°C')}`],
      ['Journées comptées', r.days_counted]
    ]);
    const months = r.heating_by_month || {};
    const keys = Object.keys(months).sort();
    if (keys.length) {
      const max = Math.max(...keys.map((k) => months[k])) || 1;
      html += `<div class="bars">` + keys.map((k) => {
        const pct = Math.max(1, (months[k] / max) * 100);
        return `<div class="bar-row"><span>${esc(k)}</span>
          <span class="bar-track"><span class="bar" style="width:${pct.toFixed(1)}%"></span></span>
          <span>${months[k]}</span></div>`;
      }).join('') + `</div>`;
    }
    return html + notes(r);
  },

  diurnal_amplitude: (r) => dl([
    ['Amplitude moyenne', `<b>${num(r.mean_amplitude, '°C')}</b>`],
    ['Plus forte amplitude', `${num(r.max_amplitude, '°C')} <span class="unavailable">(${fmtDay(r.max_date)})</span>`],
    ['Plus faible amplitude', `${num(r.min_amplitude, '°C')} <span class="unavailable">(${fmtDay(r.min_date)})</span>`],
    ['Journées retenues', r.days_counted]
  ]) + notes(r),

  records: (r) => {
    const rows = [
      ['Température', r.temperature, '°C'],
      ['Humidité', r.humidity, '%'],
      ['Pression', r.pressure, 'hPa']
    ].filter(([, v]) => v);
    if (!rows.length) return '<p class="unavailable">Aucun record disponible.</p>';
    const period = (r.from_ts && r.to_ts)
      ? `<p class="note">Sur l'ensemble de l'historique : du ${fmtDate(r.from_ts)}
         au ${fmtDate(r.to_ts)}.</p>` : '';
    return `<div class="scroll"><table>
      <thead><tr><th>Grandeur</th><th>Minimum</th><th>Maximum</th></tr></thead>
      <tbody>` + rows.map(([label, v, unit]) =>
        `<tr><td>${esc(label)}</td>
         <td>${num(v.min, unit)}<br><span class="unavailable">${fmtDate(v.min_ts)}</span></td>
         <td>${num(v.max, unit)}<br><span class="unavailable">${fmtDate(v.max_ts)}</span></td></tr>`
      ).join('') + `</tbody></table></div>` + period + notes(r);
  },

  streaks: (r) => {
    const rows = [
      ['Jours de gel', r.frost_days],
      ['Journées chaudes (> 25 °C)', r.hot_days],
      ['Fortes chaleurs (> 30 °C)', r.very_hot_days],
      ['Nuits tropicales (> 20 °C)', r.tropical_nights]
    ].filter(([, v]) => v);
    return `<div class="scroll"><table>
      <thead><tr><th></th><th>Jours</th><th>Série la plus longue</th></tr></thead>
      <tbody>` + rows.map(([label, v]) =>
        `<tr><td>${esc(label)}</td><td>${v.days}</td><td>${v.longest_streak}</td></tr>`
      ).join('') + `</tbody></table></div>` +
      `<p class="note">${esc(r.thresholds || '')} Sur ${r.days_counted} journées.</p>`;
  },

  daily_cycle: (r) => {
    const h = (n) => `${String(n).padStart(2, '0')} h`;
    return sparkline(r.hourly_mean || []) + dl([
      ['Heure la plus chaude', `${h(r.warmest_hour)} - ${num(r.warmest_temp, '°C')}`],
      ['Heure la plus fraîche', `${h(r.coldest_hour)} - ${num(r.coldest_temp, '°C')}`],
      ['Amplitude moyenne', num(r.amplitude, '°C')],
      ['Fenêtre', `${r.days_counted} jours`]
    ]) + notes(r);
  },

  data_quality: (r) => {
    let html = dl([
      ['Journées observées', r.days_seen],
      ['Journées complètes', `<span class="badge ok">${r.complete_days}</span>`],
      ['Journées partielles', r.partial_days
        ? `<span class="badge warn">${r.partial_days}</span>` : '0']
    ]);
    const gaps = r.incomplete || [];
    if (gaps.length) {
      html += `<div class="scroll"><table>
        <thead><tr><th>Date</th><th>Mesures</th><th>Complétude</th></tr></thead><tbody>` +
        gaps.map((g) => `<tr><td>${fmtDay(g.date)}</td><td>${g.measures}</td>
          <td>${g.completeness} %</td></tr>`).join('') +
        `</tbody></table></div>`;
    }
    return html + notes(r);
  },

  anomalies: (r) => {
    const channels = r.channels || {};
    const sections = Object.entries(channels).map(([key, values]) => {
      const listed = values.anomalies || [];
      const rows = listed.length ? `<details class="analysis-detail"><summary>Voir quelques relevés concernés</summary><div class="scroll"><table>
        <thead><tr><th>Jour</th><th>Valeur</th><th>Constat</th></tr></thead>
        <tbody>${listed.slice(0, 5).map((a) => `<tr><td>${fmtDay(new Date(a.ts * 1000).toISOString())}</td>
          <td>${num(a.value, key === 'temp' ? '°C' : key === 'hum' ? '%' : 'hPa')}</td>
          <td>${a.direction === 'haut' ? 'plus élevée' : 'plus basse'} que d'habitude</td></tr>`).join('')}</tbody>
        </table></div></details>` : '<p class="note">Aucune valeur inhabituellement éloignée des mesures habituelles.</p>';
      return `<h4>${esc(channelLabel(key))}</h4>` + dl([
        ['Mesures étudiées', values.count], ['Valeurs inhabituelles', values.anomalies_count]
      ]) + rows + (values.note ? `<p class="note">${esc(values.note)}</p>` : '');
    }).join('');
    return `<p class="note">Sur les ${r.window_days} derniers jours, cette analyse repère les relevés vraiment éloignés des valeurs habituelles. Les détails sont volontairement regroupés pour garder une vue utile.</p>` + sections;
  },

  correlations: (r) => {
    const rows = (r.pairs || []).map((p) => {
      const delay = p.best_lag_hours;
      const timing = delay > 0 ? `la seconde mesure suit environ ${delay} h après`
        : delay < 0 ? `la première mesure suit environ ${Math.abs(delay)} h après`
        : 'les deux mesures évoluent au même moment';
      return `<tr><td>${esc(pairLabel(p.pair))}</td><td>${p.best_r}</td><td>${timing}</td></tr>`;
    }).join('');
    return `<p class="note">Comparaison des ${r.window_days} derniers jours, avec une recherche de décalage jusqu'à ${r.max_lag_hours} h. Une corrélation décrit un lien observé, pas une cause.</p>` +
      `<div class="scroll"><table><thead><tr><th>Mesures comparées</th><th>Lien observé</th><th>Décalage le plus net</th></tr></thead><tbody>${rows}</tbody></table></div>`;
  },

  episodes: (r) => {
    const episodes = r.episodes || [];
    const rows = episodes.length ? `<div class="scroll"><table>
      <thead><tr><th>Épisode</th><th>Du</th><th>Au</th><th>Durée</th><th>Température la plus marquée</th></tr></thead>
      <tbody>${episodes.map((e) => `<tr><td>${e.type === 'canicule' ? 'Période de forte chaleur' : 'Période de froid'}</td>
        <td>${fmtDay(e.start)}</td><td>${fmtDay(e.end)}</td><td>${e.days} jours</td><td>${num(e.extreme, '°C')}</td></tr>`).join('')}</tbody>
      </table></div>` : '<p class="note">Aucun épisode répondant à ces critères sur la période analysée.</p>';
    return `<p class="note">Recherche sur les ${r.window_days} derniers jours.</p>` + rows +
      `<details class="analysis-detail"><summary>Critères d'identification</summary>` + dl([
        ['Chaleur marquée', `${num(r.heat_threshold, '°C')} au maximum de la journée`],
        ['Froid marqué', `${num(r.cold_threshold, '°C')} au minimum de la journée`],
        ['Durée retenue', `${r.min_days} jours consécutifs`]
      ]) + `<p class="note">Une période est retenue lorsque le seuil est atteint chaque jour pendant la durée indiquée.</p></details>`;
  },

  decomposition: (r) => {
    const unit = r.channel === 'temp' ? '°C' : r.channel === 'hum' ? '%' : 'hPa';
    const trend = r.trend || {}, seasonal = r.seasonal || {}, strength = r.strength || {};
    return dl([
      ['Mesure analysée', channelLabel(r.channel)], ['Période analysée', `${r.window_days} jours`],
      ['Évolution de fond', trend.change === undefined ? null : num(trend.change, unit)],
      ['Variation habituelle au cours d’une journée', num(seasonal.amplitude, unit)]
    ]) + `<p class="note">Cette lecture sépare l'évolution progressive de la mesure de son rythme habituel sur 24 h.</p>` +
      `<details class="analysis-detail"><summary>Indicateurs complémentaires</summary>` + dl([
        ['Évolution moyenne par jour', trend.slope_per_day === undefined ? null : num(trend.slope_per_day, `${unit}/jour`)],
        ['Part expliquée par la tendance', strength.trend === undefined ? null : `${Math.round(strength.trend * 100)} %`],
        ['Part expliquée par le cycle quotidien', strength.seasonal === undefined ? null : `${Math.round(strength.seasonal * 100)} %`]
      ]) + `</details>`;
  }
};

// Rendu de secours : une analyse ajoutee cote serveur s'affiche lisiblement
// meme si aucun rendu dedie n'a encore ete ecrit pour elle.
function renderGeneric(r) {
  const skip = new Set(['ok', 'id', 'title', 'group', 'ts', 'note', 'warning']);
  const rows = Object.entries(r).filter(([k, v]) =>
    !skip.has(k) && (typeof v === 'number' || typeof v === 'string' || typeof v === 'boolean'));
  return dl(rows.map(([k, v]) => [humanLabel(k), esc(v)])) + notes(r);
}

function renderCard(meta, result) {
  let body;
  if (!result || result.ok === false) {
    const reason = (result && result.reason) || 'indisponible';
    if (result && result.required_span_s) {
      const need = Math.max(1, Math.ceil(result.required_span_s / 86400));
      const have = Math.min(need, Math.max(0, Math.floor((result.span_s || 0) / 86400)));
      const remaining = Math.max(0, need - have);
      const percent = Math.round((have / need) * 100);
      body = `<div class="learning"><span class="badge warn">En apprentissage</span>` +
        `<p>${have} / ${need} jours de mesures collectés</p>` +
        `<div class="progress" aria-label="Progression : ${percent} %"><span style="width:${percent}%"></span></div>` +
        `<p>Encore ${remaining} jour${remaining > 1 ? 's' : ''} avant les premières statistiques.</p></div>`;
    } else {
      body = `<p class="unavailable">${esc(reason)}</p>`;
    }
  } else {
    const renderer = RENDERERS[meta.id] || renderGeneric;
    try { body = renderer(result); }
    catch (e) { body = renderGeneric(result); }
  }
  return `<div class="card">
      <h3>${esc(meta.title)}</h3>${body}</div>`;
}

function renderRow(ids, catalogById, resultsById) {
  const cards = ids.map((id) => {
    const meta = catalogById[id];
    return meta ? renderCard(meta, resultsById[id]) : '';
  }).filter(Boolean);
  if (!cards.length) return '';
  return `<div class="grid analysis-row${cards.length === 1 ? ' solo' : ''}">${cards.join('')}</div>`;
}

function renderSection(title, rows, catalogById, resultsById) {
  const body = rows.map((ids) => renderRow(ids, catalogById, resultsById)).join('');
  return body ? `${title ? `<h2 class="section">${esc(title)}</h2>` : ''}${body}` : '';
}

async function loadAnalyses() {
  const container = document.getElementById('groups');
  let catalog;
  try {
    catalog = (await fetch('/analyses').then((r) => r.json())).analyses || [];
  } catch (e) {
    container.innerHTML = '<p class="unavailable">Catalogue d\'analyses injoignable.</p>';
    return;
  }
  if (!catalog.length) {
    container.innerHTML = '<p class="unavailable">Aucune analyse enregistrée.</p>';
    return;
  }

  // Les analyses sont demandees en parallele : chacune est independante et
  // travaille sur le meme cache en lecture seule.
  const results = await Promise.all(catalog.map((meta) =>
    fetch('/analyze', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ type: meta.id })
    }).then((r) => r.json()).catch(() => null)));

  const byId = {};
  const catalogById = {};
  catalog.forEach((meta, i) => {
    byId[meta.id] = results[i];
    catalogById[meta.id] = meta;
  });

  // L'affichage suit une lecture naturelle : ce qui se passe maintenant,
  // ce qui s'est passe, puis les outils de diagnostic. Des rangees explicites
  // evitent qu'une carte haute laisse des vides au milieu de la page.
  const layout = [
    ['Situation actuelle', [
      ['current']
    ]],
    ['Conditions locales', [
      ['zambretti', 'temp_trend'], ['pressure_trend', 'heat_risk'],
      ['dry_air', 'fog_risk'], ['frost_risk']
    ]],
    ['Historique et repères', [
      ['normals', 'daily_cycle'], ['streaks', 'diurnal_amplitude'],
      ['degree_days'], ['records'], ['data_quality']
    ]]
  ];
  const displayed = new Set(layout.flatMap(([, rows]) => rows.flat()));
  const main = layout.map(([title, rows]) =>
    renderSection(title, rows, catalogById, byId)).join('');
  const advancedIds = ['episodes', 'anomalies', 'correlations', 'decomposition'];
  advancedIds.forEach((id) => displayed.add(id));
  const advanced = renderSection('', [
    ['episodes', 'anomalies'], ['correlations', 'decomposition']
  ], catalogById, byId);
  const remaining = catalog.filter((meta) => !displayed.has(meta.id));
  const extra = remaining.length ? renderSection('Autres analyses',
    remaining.map((meta) => [meta.id]), catalogById, byId) : '';
  container.innerHTML = main + (advanced
    ? `<details class="advanced-section"><summary>Analyses approfondies et diagnostic</summary>${advanced}</details>`
    : '') + extra;
  renderSummary(byId);
  requestAnimationFrame(fitDefinitionLists);
  document.getElementById('refreshed').textContent =
    new Date().toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' });
}

async function loadStatus() {
  try {
    const [status, modules] = await Promise.all([
      fetch('/status').then((r) => r.json()),
      fetch('/modules').then((r) => r.json())
    ]);
    document.getElementById('state').textContent = status.state || '?';
    document.getElementById('version').textContent = status.version || '?';
    document.getElementById('version-badge').textContent = status.version ? 'v' + status.version : '';
    document.getElementById('host').textContent = status.host || '?';
    document.getElementById('hostline').textContent = status.host || '';

    const mods = modules.modules || [];
    const col = mods.map((m) => m.status && m.status.collector).find((c) => c);
    const warn = document.getElementById('collecte-warn');

    // Lien de retour vers la station. MeteoHub ouvre ce service dans le MEME
    // onglet pour eviter d'en accumuler : sans ce lien, l'utilisateur n'aurait
    // que le bouton « precedent » du navigateur pour revenir. L'adresse est
    // celle de la source collectee - la seule que ce service connaisse.
    const back = document.getElementById('backlink');
    if (back && col && col.source) {
      back.href = col.source;
      back.hidden = false;
    }
    if (col) {
      document.getElementById('source').textContent = col.source || '?';
      document.getElementById('points').textContent =
        (col.cached_points || 0).toLocaleString('fr-FR');
      document.getElementById('period').textContent = col.first_ts
        ? `${fmtDate(col.first_ts)} → ${fmtDate(col.last_ts)}` : 'aucune donnée';
      document.getElementById('lastsync').textContent = fmtDate(col.last_sync_ts);
      warn.innerHTML = (col.ok === false)
        ? `<p class="warning">Dernière collecte en échec : ${esc(col.error || 'raison inconnue')}</p>`
        : '';
    } else {
      document.getElementById('source').textContent = 'aucun collecteur configuré';
      ['points', 'period', 'lastsync'].forEach((id) =>
        document.getElementById(id).textContent = '-');
      warn.innerHTML = `<p class="warning">Aucune source configurée : renseigner
        <code>source_url</code> dans le module <code>analytics</code> de la
        configuration, sans quoi aucune mesure n'est collectée et les analyses
        restent vides.</p>`;
    }
  } catch (e) {
    document.getElementById('state').textContent = 'service injoignable';
  }
}

// --- Gestion du cache -------------------------------------------------------
// Ces actions ne touchent QUE la copie locale : les mesures d'origine restent
// sur l'appareil, seule source de vérité. La purge exige une double
// confirmation ; les neutralisations passent d'abord par un comptage.
async function cleanup(payload) {
  const resp = await fetch('/data/cleanup', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  return resp.json();
}

function showResult(id, r, okText) {
  const el = document.getElementById(id);
  el.className = 'cleanup-result ' + (r && r.ok ? 'ok' : 'err');
  el.textContent = (r && r.ok) ? okText : `Échec : ${(r && r.error) || 'service injoignable'}`;
}

document.getElementById('scan-faults').addEventListener('click', async () => {
  const r = await cleanup({ action: 'scan_faults' }).catch(() => null);
  const fix = document.getElementById('fix-faults');
  const count = document.getElementById('faults-count');
  if (!r || !r.ok) { showResult('faults-result', r, ''); return; }
  document.getElementById('faults-result').textContent = '';
  if (r.affected > 0) {
    count.textContent = `${r.affected.toLocaleString('fr-FR')} relevé(s) aberrant(s)`;
    fix.hidden = false;
  } else {
    count.textContent = 'aucun relevé aberrant';
    fix.hidden = true;
  }
});

document.getElementById('fix-faults').addEventListener('click', async () => {
  const r = await cleanup({ action: 'invalidate_faults' }).catch(() => null);
  showResult('faults-result', r,
    r && r.ok ? `${r.affected.toLocaleString('fr-FR')} relevé(s) neutralisé(s).` : '');
  if (r && r.ok) {
    document.getElementById('fix-faults').hidden = true;
    document.getElementById('faults-count').textContent = '';
    loadAnalyses();
  }
});

function invalidatePayload(dryRun) {
  const from = document.getElementById('inv-from').value;
  const to   = document.getElementById('inv-to').value;
  if (!from || !to) return null;
  const payload = {
    action: 'invalidate_range',
    from_ts: Math.floor(new Date(from).getTime() / 1000),
    to_ts:   Math.floor(new Date(to).getTime() / 1000),
    dry_run: dryRun
  };
  const channel = document.getElementById('inv-channel').value;
  if (channel) payload.channels = [channel];
  return payload;
}

document.getElementById('inv-preview').addEventListener('click', async () => {
  const payload = invalidatePayload(true);
  const el = document.getElementById('inv-result');
  if (!payload) {
    el.className = 'cleanup-result err';
    el.textContent = 'Renseigner les deux dates.';
    return;
  }
  const r = await cleanup(payload).catch(() => null);
  if (!r || !r.ok) { showResult('inv-result', r, ''); return; }
  el.className = 'cleanup-result';
  el.textContent = `${r.affected.toLocaleString('fr-FR')} mesure(s) concernée(s).`;
  document.getElementById('inv-apply').hidden = (r.affected === 0);
});

document.getElementById('inv-apply').addEventListener('click', async () => {
  const payload = invalidatePayload(false);
  if (!payload) return;
  if (!confirm('Neutraliser ces mesures dans le cache ? Les valeurs resteront '
    + 'intactes sur l\'appareil.')) return;
  const r = await cleanup(payload).catch(() => null);
  showResult('inv-result', r,
    r && r.ok ? `${r.affected.toLocaleString('fr-FR')} mesure(s) neutralisée(s).` : '');
  if (r && r.ok) {
    document.getElementById('inv-apply').hidden = true;
    loadAnalyses();
  }
});

document.getElementById('purge-all').addEventListener('click', async () => {
  if (!confirm('Vider entièrement le cache local ? Il sera reconstruit depuis '
    + 'l\'appareil au prochain cycle de collecte.')) return;
  if (!confirm('Confirmer la purge totale du cache ?')) return;
  const r = await cleanup({ action: 'purge_all' }).catch(() => null);
  showResult('purge-result', r,
    r && r.ok ? 'Cache vidé. Reconstruction au prochain cycle de collecte.' : '');
  if (r && r.ok) { loadStatus(); loadAnalyses(); }
});

document.getElementById('refresh-btn').addEventListener('click', () => {
  loadStatus();
  loadAnalyses();
});

let fitTimer;
window.addEventListener('resize', () => {
  clearTimeout(fitTimer);
  fitTimer = setTimeout(fitDefinitionLists, 100);
});

loadStatus();
loadAnalyses();
setInterval(loadStatus, 15000);
setInterval(loadAnalyses, 120000);
</script>
</body>
</html>)HTML";
}

QByteArray HttpServer::buildStatusJson() const {
    QJsonObject o;
    o["app"]      = m_config.appName;
    o["host"]     = QHostInfo::localHostName();
    o["version"]  = morfanalytics::version();
    o["proto"]    = QString::fromLatin1(morfanalytics::kProtocol);
    o["state"]    = m_registry ? m_registry->state() : QStringLiteral("ok");
    o["uptime_s"] = static_cast<double>(m_uptime.isValid() ? m_uptime.elapsed() / 1000 : 0);
    o["ts"]       = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    o["metrics"]  = m_registry ? m_registry->metrics() : QJsonObject{};

    // Detail annonce (interface web + API). morfAnalytics sert son PROPRE
    // /status plutot que le StatusServer de morfBeacon ; il appelle donc le
    // MEME point unique (fillAnnouncedDetail + describeService) pour que son
    // /status et son heartbeat ne puissent pas diverger.
    morfbeacon::PresenceConfig self;
    fillAnnouncedDetail(self);
    const QJsonObject detail = morfbeacon::describeService(self, port());
    for (auto it = detail.constBegin(); it != detail.constEnd(); ++it)
        o[it.key()] = it.value();

    return toJson(o);
}

void HttpServer::reply(QTcpSocket* sock, int code, const QByteArray& reason, const QByteArray& body,
                       const QByteArray& contentType) {
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    resp += "Content-Type: " + contentType + "\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Access-Control-Allow-Origin: *\r\n";
    resp += "Connection: close\r\n\r\n";
    resp += body;
    sock->write(resp);
    // Vider le tampon d'écriture AVANT de fermer : sur une grande réponse (la page
    // Photo dépasse 20 Ko), le corps déborde du tampon socket et `disconnectFromHost`
    // seul en tronquait la fin. On draine jusqu'à ce qu'il ne reste rien à écrire,
    // avec un délai de garde pour ne jamais bloquer indéfiniment.
    while (sock->bytesToWrite() > 0)
        if (!sock->waitForBytesWritten(2000))
            break;
    sock->disconnectFromHost();
}

} // namespace morfanalytics
