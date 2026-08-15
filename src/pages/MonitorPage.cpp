/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/pages/MonitorPage.h"

#include <QByteArray>

namespace morfanalytics::pages {

// -----------------------------------------------------------------------------
// Page /monitor : historique des machines du parc.
//
// Rôle de morfAnalytics : agréger, historiser, représenter. morfMonitor reste la
// sonde (« maintenant ») ; ici on montre le TEMPS. Premier incrément : vue
// d'ensemble + séries CPU / RAM / température. Le détail par service, les
// activités, la baseline et les anomalies viendront dans les incréments suivants.
//
// Données via /monitor/data (JSON) ; tout le rendu est fait côté navigateur.
// -----------------------------------------------------------------------------
QByteArray MonitorPage::render() {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>morfAnalytics - Machines</title><style>
:root{--bg:#15171b;--card:#1e2126;--line:#2c3037;--ink:#e7e9ec;--muted:#99a1ad;--soft:#c7cdd6;
--accent:#6f9bff;--ok:#2e8b57;--warn:#e6a54e;--bad:#c8483a;--track:#242830}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:1.5rem}
.wrap{max-width:82rem;margin:auto}h1{margin:.2rem 0}h2{font-size:1.05rem;margin:1.4rem 0 .5rem}
.muted{color:var(--muted)}a{color:var(--accent)}
.vb{font-size:.8rem;font-weight:600;vertical-align:middle;color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,transparent);border:1px solid color-mix(in srgb,var(--accent) 30%,transparent);border-radius:999px;padding:.1rem .5rem;margin-left:.4rem}
.controls{display:flex;flex-wrap:wrap;gap:.8rem 1.2rem;align-items:center;margin:1rem 0}
select{background:#242830;border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.3rem .5rem;font-size:.9rem}
.periods{display:flex;gap:.3rem;flex-wrap:wrap}
.pbtn{background:#242830;border:1px solid var(--line);color:var(--soft);border-radius:8px;padding:.3rem .7rem;cursor:pointer;font-size:.85rem}
.pbtn:hover{border-color:var(--accent);color:var(--ink)}.pbtn.on{background:#2a3350;border-color:var(--accent);color:#fff}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(9rem,1fr));gap:.8rem;margin:1rem 0}
.tile{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:.9rem}
.tile .k{font-size:.72rem;letter-spacing:.05em;text-transform:uppercase;color:var(--muted)}
.tile .number{font-size:1.55rem;font-weight:700;font-variant-numeric:tabular-nums;margin-top:.15rem}
.tile .sub{font-size:.76rem;color:var(--muted);margin-top:.15rem}
.chart{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:.9rem 1.1rem;margin:1rem 0}
.chart h3{margin:0 0 .1rem;font-size:1rem}.chart .cur{float:right;font-variant-numeric:tabular-nums;color:var(--soft)}
.chart svg{display:block;width:100%;height:auto;margin-top:.4rem}
.chart .ax{fill:var(--muted);font-size:10px}.chart .grid-l{stroke:var(--line);stroke-width:1}
.dot{width:.55rem;height:.55rem;border-radius:50%;display:inline-block;margin-right:.35rem;vertical-align:middle}
.pill{font-size:.74rem;border:1px solid var(--line);border-radius:999px;padding:.1rem .5rem;color:var(--muted)}
</style></head><body><div class="wrap">
<p><a href="/">&larr; morfAnalytics</a></p>
<h1>Analyse des machines <span id="vb" class="vb"></span></h1>
<p class="muted">Historique du parc dans le temps, à partir des relevés de morfMonitor.
morfMonitor dit &laquo;&nbsp;maintenant&nbsp;&raquo; ; ici on regarde comment la machine se comporte dans la dur&eacute;e.</p>

<div class="controls">
  <label>Machine&nbsp;
    <select id="machine"></select>
  </label>
  <div class="periods" id="periods"></div>
  <span id="conn" class="pill"></span>
</div>

<div id="app"><p class="muted">Chargement&hellip;</p></div>
</div>
<script>
"use strict";
const LS_M="morfanalytics.monitor.machine", LS_P="morfanalytics.monitor.period";
const PERIODS=[["1 h",3600],["6 h",21600],["24 h",86400],["7 j",604800],["30 j",2592000]];
let S={machine:localStorage.getItem(LS_M)||"", period:+(localStorage.getItem(LS_P)||86400)};

const $=s=>document.querySelector(s);
function fmtNum(v,d){return (v===null||v===undefined)?"—":(+v).toFixed(d===undefined?0:d);}
function fmtBytes(b){if(b===null||b===undefined)return "—";const u=["o","Ko","Mo","Go","To"];let i=0;b=+b;while(b>=1024&&i<u.length-1){b/=1024;i++;}return b.toFixed(b<10&&i>0?1:0)+" "+u[i];}
function fmtDur(s){if(s===null||s===undefined)return "—";s=+s;const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);if(d>0)return d+" j "+h+" h";if(h>0)return h+" h "+m+" min";return m+" min";}
function fmtClock(ts){const d=new Date(ts*1000);return d.toLocaleString("fr-FR",{day:"2-digit",month:"2-digit",hour:"2-digit",minute:"2-digit"});}

// Petit graphe en ligne (SVG), auto-echelle, trous preserves (les null coupent
// le trait au lieu d'inventer une valeur). Grille et valeur courante discretes.
function lineChart(ts, vals, opt){
  opt=opt||{};
  const W=760,H=150,pL=38,pR=10,pT=10,pB=20;
  const known=vals.filter(v=>v!==null&&v!==undefined);
  if(!known.length) return '<div class="muted">pas de mesure sur la période</div>';
  const t0=ts[0], t1=ts[ts.length-1]||t0+1;
  let ymax = opt.max!==undefined?opt.max:Math.max.apply(null,known);
  if(opt.max===undefined) ymax = ymax<=0?1:ymax*1.15;
  const ymin=0;
  const X=t=>pL+(W-pL-pR)*((t-t0)/Math.max(1,(t1-t0)));
  const Y=v=>pT+(H-pT-pB)*(1-(v-ymin)/Math.max(1e-9,ymax-ymin));
  let d="",started=false;
  for(let i=0;i<ts.length;i++){const v=vals[i];if(v===null||v===undefined){started=false;continue;}
    d+=(started?"L":"M")+X(ts[i]).toFixed(1)+" "+Y(v).toFixed(1)+" ";started=true;}
  // grille horizontale : 0, milieu, max
  let grid="",labels="";
  [0,.5,1].forEach(f=>{const y=pT+(H-pT-pB)*(1-f);const val=(ymin+(ymax-ymin)*f);
    grid+='<line class="grid-l" x1="'+pL+'" y1="'+y.toFixed(1)+'" x2="'+(W-pR)+'" y2="'+y.toFixed(1)+'"/>';
    labels+='<text class="ax" x="'+(pL-5)+'" y="'+(y+3).toFixed(1)+'" text-anchor="end">'+val.toFixed(opt.dec||0)+(opt.unit||"")+'</text>';});
  const xt0='<text class="ax" x="'+pL+'" y="'+(H-6)+'">'+fmtClock(t0)+'</text>';
  const xt1='<text class="ax" x="'+(W-pR)+'" y="'+(H-6)+'" text-anchor="end">'+fmtClock(t1)+'</text>';
  const col=opt.color||"#6f9bff";
  // dernier point connu, mis en evidence
  let last=null;for(let i=ts.length-1;i>=0;i--){if(vals[i]!==null&&vals[i]!==undefined){last=[ts[i],vals[i]];break;}}
  const dot=last?'<circle cx="'+X(last[0]).toFixed(1)+'" cy="'+Y(last[1]).toFixed(1)+'" r="3" fill="'+col+'"/>':"";
  return '<svg viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none" role="img">'+
    grid+labels+'<path d="'+d+'" fill="none" stroke="'+col+'" stroke-width="1.8"/>'+dot+xt0+xt1+'</svg>';
}

function tile(k,val,sub){return '<div class="tile"><div class="k">'+k+'</div><div class="number">'+val+'</div><div class="sub">'+(sub||"")+'</div></div>';}

function render(data){
  // Selecteur de machines
  const sel=$("#machine");const machs=data.machines||[];
  sel.innerHTML=machs.map(m=>'<option value="'+m.key+'"'+(m.key===data.machine?" selected":"")+'>'+
      (m.hostname||m.key)+(m.online?"":" (hors ligne)")+'</option>').join("");
  const app=$("#app");
  if(!machs.length){app.innerHTML='<div class="chart"><p class="muted">Aucune machine encore historisée. '+
    'Vérifiez qu’un morfMonitor est configuré comme source du module <code>monitor</code>, et laissez passer un relevé.</p></div>';
    $("#conn").textContent="";return;}

  const ov=data.overview||{}, sr=data.series||{};
  const online = (machs.find(m=>m.key===data.machine)||{}).online;
  $("#conn").innerHTML='<span class="dot" style="background:'+(online?"var(--ok)":"var(--bad)")+'"></span>'+(online?"en ligne":"hors ligne");

  // Vue d'ensemble
  let html='<h2>Vue d’ensemble</h2><div class="grid">'+
    tile("CPU", fmtNum(ov.cpu_percent,1)+" %", "charge processeur")+
    tile("Mémoire", fmtNum(ov.mem_percent,1)+" %", fmtBytes(ov.mem_used)+" / "+fmtBytes(ov.mem_total))+
    tile("Température", fmtNum(ov.temp_cpu,1)+" °C", "CPU")+
    tile("Charge (1 min)", fmtNum(ov.load1,2), "load average")+
    tile("Stockage", fmtNum(ov.disk_percent,1)+" %", "occupation /")+
    tile("Services actifs", fmtNum(ov.services_active), "systemd")+
    tile("Uptime", fmtDur(ov.uptime_s), "depuis le démarrage")+
    tile("Dernier relevé", ov.ts?fmtClock(ov.ts):"—", "")+
    '</div>';

  // Graphiques temporels
  const ts=sr.ts||[];
  function card(title,vals,cur,opt){
    return '<div class="chart"><span class="cur">'+cur+'</span><h3>'+title+'</h3>'+lineChart(ts,vals,opt)+'</div>';}
  html+='<h2>Dans le temps</h2>';
  html+=card("CPU","cpu" in sr?sr.cpu:[], fmtNum(ov.cpu_percent,1)+" %", {max:100,unit:"%",color:"#6f9bff"});
  html+=card("Mémoire","mem" in sr?sr.mem:[], fmtNum(ov.mem_percent,1)+" %", {max:100,unit:"%",color:"#7ee0b8"});
  html+=card("Température CPU","temp" in sr?sr.temp:[], fmtNum(ov.temp_cpu,1)+" °C", {unit:"°",dec:0,color:"#e6a54e"});
  html+=card("Charge (load 1 min)","load" in sr?sr.load:[], fmtNum(ov.load1,2), {dec:1,color:"#a487f2"});
  if(sr.bucket_s)html+='<p class="muted" style="font-size:.8rem">Résolution&nbsp;: 1 point ≈ '+fmtDur(sr.bucket_s)+'. Les trous (source hors ligne) restent visibles, jamais comblés par des zéros.</p>';
  app.innerHTML=html;
}

function load(){
  fetch("/monitor/data?machine="+encodeURIComponent(S.machine)+"&period="+S.period)
    .then(r=>r.json()).then(d=>{ if(!S.machine && d.machine)S.machine=d.machine; render(d); })
    .catch(e=>{$("#app").innerHTML='<div class="chart"><p class="muted">Données indisponibles : '+e+'</p></div>';});
}

// Boutons de periode
$("#periods").innerHTML=PERIODS.map(p=>'<button class="pbtn'+(p[1]===S.period?" on":"")+'" data-s="'+p[1]+'">'+p[0]+'</button>').join("");
$("#periods").addEventListener("click",e=>{const b=e.target.closest(".pbtn");if(!b)return;
  S.period=+b.dataset.s;localStorage.setItem(LS_P,S.period);
  document.querySelectorAll(".pbtn").forEach(x=>x.classList.toggle("on",+x.dataset.s===S.period));load();});
$("#machine").addEventListener("change",e=>{S.machine=e.target.value;localStorage.setItem(LS_M,S.machine);load();});

// Badge de version (comme les autres pages)
fetch("/status").then(r=>r.json()).then(s=>{const b=$("#vb");if(b)b.textContent=s.version?"v"+s.version:"";}).catch(()=>{});

load();
setInterval(load, 15000);   // rafraichissement doux
</script>
</body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
