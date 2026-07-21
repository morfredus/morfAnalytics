/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/HttpServer.h"
#include "morfanalytics/ModuleRegistry.h"
#include "morfanalytics/AnalyticsModule.h"
#include "morfanalytics/Version.h"

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

#include <utility>

namespace morfanalytics {

namespace {
constexpr int kMaxRequestBytes = 65536;

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
    connect(m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

HttpServer::~HttpServer() = default;

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
    if (path == "/analyze") {
        if (method != "POST") {
            code = 405; reason = "Method Not Allowed";
            out = "{\"error\":\"use POST /analyze\"}";
        } else {
            out = handleAnalyzePost(body, code, reason);
        }
    }
    // ---- Routes GET ------------------------------------------------------
    else if (method != "GET") {
        code = 405; reason = "Method Not Allowed";
        out = "{\"error\":\"method not allowed\"}";
    } else if (path == "/" || path == "/index.html") {
        // Page d'accueil : c'est la cible du lien "Analyse avancee" affiche par
        // MeteoHub quand il detecte ce service sur le reseau. Elle doit donc
        // repondre quelque chose d'utile des la premiere version, avant meme
        // que les analyses existent.
        reply(sock, 200, "OK", landingPage(), "text/html; charset=utf-8");
        return;
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
<title>morfAnalytics</title>
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
  body { margin: 0; padding: 2rem 1rem 4rem; background: var(--bg); color: var(--fg);
         font-family: system-ui, -apple-system, "Segoe UI", sans-serif; line-height: 1.5; }
  .wrap { max-width: 60rem; margin: 0 auto; }
  header.top { margin-bottom: 2rem; }
  a.back { display: inline-block; margin-bottom: .6rem; color: var(--accent);
           text-decoration: none; font-size: .9rem; }
  a.back:hover { text-decoration: underline; }
  h1 { margin: 0 0 .2rem; font-size: 1.75rem; letter-spacing: -.02em; }
  .sub { margin: 0; color: var(--muted); }

  h2.section { font-size: .8rem; text-transform: uppercase; letter-spacing: .08em;
               color: var(--muted); margin: 2.5rem 0 .9rem; font-weight: 600; }

  .grid { display: grid; gap: 1rem;
          grid-template-columns: repeat(auto-fit, minmax(17rem, 1fr)); }
  .card { background: var(--card); border: 1px solid var(--line); border-radius: .75rem;
          padding: 1.1rem 1.25rem; }
  .card h3 { margin: 0 0 .8rem; font-size: .95rem; font-weight: 600; }
  .card.wide { grid-column: 1 / -1; }

  dl { display: grid; grid-template-columns: auto 1fr; gap: .35rem .9rem; margin: 0; }
  dt { color: var(--muted); white-space: nowrap; }
  dd { margin: 0; text-align: right; font-variant-numeric: tabular-nums; }

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

  .note { font-size: .82rem; color: var(--muted); margin: .8rem 0 0; }
  .warning { font-size: .82rem; margin: .8rem 0 0; padding: .5rem .7rem; border-radius: .4rem;
             background: color-mix(in srgb, var(--warn) 12%, transparent);
             border-left: 3px solid var(--warn); }
  .unavailable { color: var(--muted); font-size: .9rem; }

  table { width: 100%; border-collapse: collapse; font-size: .9rem; }
  th, td { text-align: right; padding: .3rem .4rem; border-bottom: 1px solid var(--line); }
  th:first-child, td:first-child { text-align: left; }
  th { color: var(--muted); font-weight: 500; font-size: .8rem; }
  tbody tr:last-child td { border-bottom: none; }
  .scroll { overflow-x: auto; }

  .bars { display: grid; gap: .3rem; margin-top: .3rem; }
  .bar-row { display: grid; grid-template-columns: 4.5rem 1fr auto; gap: .5rem;
             align-items: center; font-size: .85rem; }
  .bar { height: .55rem; border-radius: 999px; background: var(--accent); min-width: 2px; }
  .bar-track { background: color-mix(in srgb, var(--muted) 15%, transparent);
               border-radius: 999px; }

  .anomaly { font-size: 2rem; font-weight: 600; letter-spacing: -.02em; }
  .anomaly.warm { color: var(--warm); }
  .anomaly.cold { color: var(--cold); }

  svg.spark { width: 100%; height: 5.5rem; display: block; margin-top: .3rem; }
  code { background: color-mix(in srgb, var(--muted) 15%, transparent);
         padding: .1rem .35rem; border-radius: .25rem; font-size: .85em; }
  footer { margin-top: 3rem; font-size: .82rem; color: var(--muted); }
</style>
</head>
<body>
<div class="wrap">
  <header class="top">
    <a id="backlink" class="back" href="#" hidden>&larr; Retour à MeteoHub</a>
    <h1>morfAnalytics</h1>
    <p class="sub">Analyses avancées &mdash; <span id="hostline">…</span></p>
  </header>

  <h2 class="section">Service et collecte</h2>
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

  <div id="groups"></div>

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
  qualite: 'Qualité des données'
};

const esc = (s) => String(s).replace(/[&<>"]/g, (c) =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

const fmtDate = (ts) => (!ts || ts <= 0)
  ? 'jamais' : new Date(ts * 1000).toLocaleString('fr-FR');
const fmtDay = (iso) => {
  const d = new Date(iso);
  return isNaN(d) ? iso : d.toLocaleDateString('fr-FR');
};
const num = (v, unit) => (v === undefined || v === null)
  ? '—' : `${v}${unit ? ' ' + unit : ''}`;

const riskBadge = (level) => {
  const cls = level === 'élevé' ? 'bad' : (level === 'modéré' ? 'warn' : 'ok');
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

// Courbe simple, tracee a la main : evite d'embarquer une bibliotheque de
// graphiques pour une seule serie de 24 valeurs.
function sparkline(values) {
  const pts = values.map((v, i) => [i, v]).filter(([, v]) => v !== null);
  if (pts.length < 2) return '';
  const ys = pts.map(([, v]) => v);
  const lo = Math.min(...ys), hi = Math.max(...ys);
  const span = (hi - lo) || 1;
  const W = 100, H = 30, pad = 3;
  const coords = pts.map(([i, v]) => {
    const x = pad + (i / 23) * (W - 2 * pad);
    const y = H - pad - ((v - lo) / span) * (H - 2 * pad);
    return `${x.toFixed(2)},${y.toFixed(2)}`;
  }).join(' ');
  return `<svg class="spark" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">
    <polyline points="${coords}" fill="none" stroke="var(--accent)"
              stroke-width="1" stroke-linejoin="round" stroke-linecap="round"/>
  </svg>`;
}

const RENDERERS = {
  current: (r) => {
    let html = `<div class="hero">${num(r.temperature)}<span class="unit"> °C</span></div>`;
    if (r.humidex !== undefined && r.humidex > r.temperature + 0.4)
      html += `<div class="hero-sub">ressenti ${num(r.humidex, '°C')}</div>`;
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

  zambretti: (r) => `<div class="quote">${esc(r.forecast || '—')}</div>` +
    `<div class="chips">
       <span class="chip">Pression <b>${num(r.pressure_sea_level, 'hPa')}</b></span>
       <span class="chip">3 h <b>${r.delta_3h > 0 ? '+' : ''}${num(r.delta_3h, 'hPa')}</b></span>
       <span class="chip">${esc(r.tendency || '')}</span>
     </div>` + notes(r),

  pressure_trend: (r) => dl([
    ['Pression (niveau mer)', num(r.pressure_sea_level, 'hPa')],
    ['Variation 1 h', r.delta_1h === undefined ? null : `${r.delta_1h > 0 ? '+' : ''}${num(r.delta_1h, 'hPa')}`],
    ['Variation 3 h', `${r.delta_3h > 0 ? '+' : ''}${num(r.delta_3h, 'hPa')}`],
    ['Tendance', esc(r.tendency || '')],
    ['Alerte orage', r.storm_warning === undefined ? null :
      (r.storm_warning ? '<span class="badge bad">oui</span>' : '<span class="badge ok">non</span>')]
  ]) + notes(r),

  fog_risk: (r) => `<div>${riskBadge(r.risk || '—')}</div>` + dl([
    ['Écart au point de rosée', num(r.dew_point_spread, '°C')],
    ['Évolution sur 2 h', r.spread_trend_2h === undefined ? null :
      `${r.spread_trend_2h > 0 ? '+' : ''}${num(r.spread_trend_2h, '°C')}`]
  ]) + notes(r),

  frost_risk: (r) => `<div>${riskBadge(r.risk || '—')}</div>` + dl([
    ['Température actuelle', num(r.temperature, '°C')],
    ['Refroidissement', num(r.cooling_per_hour, '°C/h')],
    ['Minimum projeté', r.projected_min === undefined ? null : num(r.projected_min, '°C')],
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
      ['Plage observée', `${num(r.normal_min)} – ${num(r.normal_max, '°C')}`],
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
    return `<div class="scroll"><table>
      <thead><tr><th>Grandeur</th><th>Minimum</th><th>Maximum</th></tr></thead>
      <tbody>` + rows.map(([label, v, unit]) =>
        `<tr><td>${esc(label)}</td>
         <td>${num(v.min, unit)}<br><span class="unavailable">${fmtDate(v.min_ts)}</span></td>
         <td>${num(v.max, unit)}<br><span class="unavailable">${fmtDate(v.max_ts)}</span></td></tr>`
      ).join('') + `</tbody></table></div>` + notes(r);
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
      ['Heure la plus chaude', `${h(r.warmest_hour)} — ${num(r.warmest_temp, '°C')}`],
      ['Heure la plus fraîche', `${h(r.coldest_hour)} — ${num(r.coldest_temp, '°C')}`],
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
  }
};

// Rendu de secours : une analyse ajoutee cote serveur s'affiche lisiblement
// meme si aucun rendu dedie n'a encore ete ecrit pour elle.
function renderGeneric(r) {
  const skip = new Set(['ok', 'id', 'title', 'group', 'ts', 'note', 'warning']);
  const rows = Object.entries(r).filter(([k, v]) =>
    !skip.has(k) && (typeof v === 'number' || typeof v === 'string' || typeof v === 'boolean'));
  return dl(rows.map(([k, v]) => [k, esc(v)])) + notes(r);
}

function renderCard(meta, result) {
  let body;
  if (!result || result.ok === false) {
    const reason = (result && result.reason) || 'indisponible';
    let extra = '';
    if (result && result.required_span_s) {
      const need = Math.round(result.required_span_s / 86400);
      const have = Math.round((result.span_s || 0) / 86400);
      extra = ` <span class="unavailable">(${have} j d'historique, ${need} j requis)</span>`;
    }
    body = `<p class="unavailable">${esc(reason)}${extra}</p>`;
  } else {
    const renderer = RENDERERS[meta.id] || renderGeneric;
    try { body = renderer(result); }
    catch (e) { body = renderGeneric(result); }
  }
  return `<div class="card${meta.id === 'records' || meta.id === 'streaks'
    || meta.id === 'degree_days' || meta.id === 'data_quality' ? ' wide' : ''}">
      <h3>${esc(meta.title)}</h3>${body}</div>`;
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

  const byGroup = {};
  catalog.forEach((meta, i) => {
    (byGroup[meta.group] = byGroup[meta.group] || []).push([meta, results[i]]);
  });

  container.innerHTML = Object.entries(byGroup).map(([group, items]) =>
    `<h2 class="section">${esc(GROUP_LABELS[group] || group)}</h2>
     <div class="grid">${items.map(([m, r]) => renderCard(m, r)).join('')}</div>`
  ).join('');
}

async function loadStatus() {
  try {
    const [status, modules] = await Promise.all([
      fetch('/status').then((r) => r.json()),
      fetch('/modules').then((r) => r.json())
    ]);
    document.getElementById('state').textContent = status.state || '?';
    document.getElementById('version').textContent = status.version || '?';
    document.getElementById('host').textContent = status.host || '?';
    document.getElementById('hostline').textContent = status.host || '';

    const mods = modules.modules || [];
    const col = mods.map((m) => m.status && m.status.collector).find((c) => c);
    const warn = document.getElementById('collecte-warn');

    // Lien de retour vers la station. MeteoHub ouvre ce service dans le MEME
    // onglet pour eviter d'en accumuler : sans ce lien, l'utilisateur n'aurait
    // que le bouton « precedent » du navigateur pour revenir. L'adresse est
    // celle de la source collectee — la seule que ce service connaisse.
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
        document.getElementById(id).textContent = '—');
      warn.innerHTML = `<p class="warning">Aucune source configurée : renseigner
        <code>source_url</code> dans le module <code>analytics</code> de la
        configuration, sans quoi aucune mesure n'est collectée et les analyses
        restent vides.</p>`;
    }
  } catch (e) {
    document.getElementById('state').textContent = 'service injoignable';
  }
}

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

    // Detail de l'interface Web, attendu par tout consommateur ayant vu passer
    // la capacite « web_ui » dans le heartbeat. morfAnalytics sert son PROPRE
    // /status au lieu d'utiliser le StatusServer de morfBeacon : il doit donc
    // publier ce bloc lui-meme, sans quoi il annoncerait une capacite dont le
    // detail resterait introuvable.
    QJsonObject ui;
    ui["path"]        = QStringLiteral("/");
    ui["label"]       = QStringLiteral("Analyses");
    ui["port"]        = static_cast<int>(port());
    ui["description"] = QStringLiteral(
        "Statistiques longue periode et correlations sur l'historique des equipements.");
    o["web_ui"] = ui;

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
    sock->flush();
    sock->disconnectFromHost();
}

} // namespace morfanalytics
