/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/pages/MeteoGraphsPage.h"

#include <QByteArray>

namespace morfanalytics::pages {

// -----------------------------------------------------------------------------
// Page /meteohub/graphs : onglet Graphiques.
//
// Là où les analyses répondent « qu'est-ce que ça veut dire ? », le graphique
// répond « montre-moi ce qui s'est réellement passé ». L'œil voit d'un coup le
// décalage, l'amortissement, la montée, la stabilité.
//
// Choix de tracé :
//   - Les mesures arrivent ~toutes les 5 min (jusqu'à 10 au pire) : ces écarts ne
//     sont PAS des trous, on relie les points. Le trait n'est coupé que sur un vrai
//     silence (écart bien plus grand que la cadence).
//   - IN et OUT : deux COULEURS distinctes (bleu / orange), jamais des pointillés.
//   - Grandeur « Toutes » : un graphe par grandeur (les échelles °C / % / hPa n'ont
//     rien à voir), chacun avec IN et OUT dans les deux mêmes couleurs. C'est
//     l'astuce qui garde 6 courbes lisibles sans les empiler sur un axe commun.
//
// Page autonome (SVG dessiné côté navigateur, sans CDN), données via /meteohub/series.
// -----------------------------------------------------------------------------
QByteArray MeteoGraphsPage::render() {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>morfAnalytics - Graphiques météo</title><style>
:root{--bg:#15171b;--card:#1e2126;--line:#2c3037;--ink:#e7e9ec;--muted:#99a1ad;--soft:#c7cdd6;
--accent:#6f9bff;--ok:#2e8b57;--warn:#e6a54e;--bad:#c8483a;--track:#242830}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:1.5rem}
.wrap{max-width:72rem;margin:auto}h1{margin:.2rem 0}
.muted{color:var(--muted)}a{color:var(--accent)}
.vb{font-size:.8rem;font-weight:600;vertical-align:middle;color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,transparent);border:1px solid color-mix(in srgb,var(--accent) 30%,transparent);border-radius:999px;padding:.1rem .5rem;margin-left:.4rem}
.tabs{margin:.3rem 0 1rem}.tabs .tab{display:inline-block;padding:.25rem .7rem;border:1px solid var(--line);border-radius:999px;margin-right:.4rem;font-size:.9rem;color:var(--soft);text-decoration:none}
.tabs .tab.on{background:#2a3350;border-color:var(--accent);color:#fff}
.controls{display:flex;flex-wrap:wrap;gap:.8rem 1.2rem;align-items:center;margin:1rem 0}
label{font-size:.9rem;color:var(--soft)}
select{background:#242830;border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.3rem .5rem;font-size:.9rem}
.periods{display:flex;gap:.3rem;flex-wrap:wrap}
.pbtn{background:#242830;border:1px solid var(--line);color:var(--soft);border-radius:8px;padding:.3rem .7rem;cursor:pointer;font-size:.85rem}
.pbtn:hover{border-color:var(--accent);color:var(--ink)}.pbtn.on{background:#2a3350;border-color:var(--accent);color:#fff}
.chart{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:.9rem 1.1rem;margin:1rem 0}
.chart h3{margin:0 0 .1rem;font-size:1.05rem}
.chart svg{display:block;width:100%;height:auto;margin-top:.5rem}
.chart .ax{fill:var(--muted);font-size:11px}.chart .grid-l{stroke:var(--line);stroke-width:1}
.legend{display:flex;gap:1.1rem;flex-wrap:wrap;margin-top:.5rem;font-size:.85rem;color:var(--soft)}
.legend .k{display:inline-flex;align-items:center;gap:.4rem}
.legend .sw{width:1.4rem;height:0;border-top:3px solid;display:inline-block}
.note{color:var(--muted);font-size:.82rem;margin-top:.4rem}
</style></head><body><div class="wrap">
<p><a href="/">&larr; morfAnalytics</a></p>
<h1>Météo <span id="vb" class="vb"></span></h1>
<div class="tabs"><a class="tab" href="/meteohub">Analyses</a><span class="tab on">Graphiques</span></div>
<p class="muted">Montre ce que font réellement les données dans le temps. Les analyses, elles, disent ce que ça signifie.</p>

<div class="controls">
  <label>Grandeur&nbsp;<select id="metric"></select></label>
  <label>Source&nbsp;<select id="source"></select></label>
  <div class="periods" id="periods"></div>
</div>

<div id="charts"><p class="muted">Chargement&hellip;</p></div>
</div>
<script>
"use strict";
const LS="morfanalytics.graphs.";
// [clé, libellé, unité, décimales]
const METRICS=[["temp","Température","°C",1],["hum","Humidité","%",0],["pres","Pression","hPa",1]];
const METRIC_OPTS=METRICS.concat([["all","Toutes","",0]]);
const SOURCES=[["out","Extérieur"],["in","Intérieur"],["both","Intérieur + Extérieur"]];
const PERIODS=[["6 h",6],["12 h",12],["24 h",24],["3 j",72],["7 j",168],["30 j",720]];
// IN et OUT : deux couleurs bien distinctes (jamais de pointillés). Convention
// unique et constante sur tous les graphes : OUT bleu, IN orange.
const COL={out:"#6f9bff",in:"#e6a54e"};

let S={
  metric:localStorage.getItem(LS+"metric")||"temp",
  source:localStorage.getItem(LS+"source")||"out",
  hours:+(localStorage.getItem(LS+"hours")||24)
};

const $=s=>document.querySelector(s);
function metricDef(k){return METRICS.find(m=>m[0]===k)||METRICS[0];}
function srcLabel(k){return (SOURCES.find(s=>s[0]===k)||["","",""])[1];}
function fmtClock(ts){const d=new Date(ts*1000);const sameDay=(new Date()*1-d)<86400000;
  return d.toLocaleString("fr-FR",sameDay?{hour:"2-digit",minute:"2-digit"}:{day:"2-digit",month:"2-digit",hour:"2-digit",minute:"2-digit"});}

// Graphe multi-séries (SVG), auto-échelle sur l'ensemble. Chaque série =
// {ts,vals,color,label,bucket}. Les points sont RELIÉS ; le trait n'est coupé
// qu'entre deux points dont l'écart temporel dépasse ~2,5 tranches (un vrai
// silence du capteur, pas la simple cadence de 5 min).
function multiChart(series, opt){
  opt=opt||{};
  const W=760,H=230,pL=46,pR=12,pT=12,pB=24;
  let all=[],t0=Infinity,t1=-Infinity;
  series.forEach(s=>{for(let i=0;i<s.vals.length;i++){const v=s.vals[i];
    if(v!==null&&v!==undefined){all.push(v);const t=s.ts[i];if(t<t0)t0=t;if(t>t1)t1=t;}}});
  if(!all.length) return '<div class="muted">Pas encore de mesure sur cette période.</div>';
  if(!(t1>t0))t1=t0+1;
  let ymin=Math.min.apply(null,all), ymax=Math.max.apply(null,all);
  let pad=(ymax-ymin)*0.08; if(pad<0.5)pad=0.5; ymin-=pad; ymax+=pad;
  const X=t=>pL+(W-pL-pR)*((t-t0)/Math.max(1,(t1-t0)));
  const Y=v=>pT+(H-pT-pB)*(1-(v-ymin)/Math.max(1e-9,ymax-ymin));
  let grid="",labels="";
  [0,.5,1].forEach(f=>{const y=pT+(H-pT-pB)*(1-f);const val=(ymin+(ymax-ymin)*f);
    grid+='<line class="grid-l" x1="'+pL+'" y1="'+y.toFixed(1)+'" x2="'+(W-pR)+'" y2="'+y.toFixed(1)+'"/>';
    labels+='<text class="ax" x="'+(pL-6)+'" y="'+(y+3).toFixed(1)+'" text-anchor="end">'+val.toFixed(opt.dec||0)+(opt.unit||"")+'</text>';});
  const xt0='<text class="ax" x="'+pL+'" y="'+(H-6)+'">'+fmtClock(t0)+'</text>';
  const xt1='<text class="ax" x="'+(W-pR)+'" y="'+(H-6)+'" text-anchor="end">'+fmtClock(t1)+'</text>';
  let paths="";
  series.forEach(s=>{
    const gapMax=(s.bucket&&s.bucket>0?s.bucket:600)*2.5; // seuil de coupure du trait
    let d="",prevT=null;
    for(let i=0;i<s.ts.length;i++){const v=s.vals[i];if(v===null||v===undefined)continue;
      const t=s.ts[i];
      const move=(prevT===null)||((t-prevT)>gapMax); // nouveau tracé après un vrai trou
      d+=(move?"M":"L")+X(t).toFixed(1)+" "+Y(v).toFixed(1)+" ";prevT=t;}
    paths+='<path d="'+d+'" fill="none" stroke="'+s.color+'" stroke-width="2"/>';
    let last=null;for(let i=s.ts.length-1;i>=0;i--){if(s.vals[i]!==null&&s.vals[i]!==undefined){last=[s.ts[i],s.vals[i]];break;}}
    if(last)paths+='<circle cx="'+X(last[0]).toFixed(1)+'" cy="'+Y(last[1]).toFixed(1)+'" r="3" fill="'+s.color+'"/>';});
  return '<svg viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none" role="img">'+grid+labels+paths+xt0+xt1+'</svg>';
}

function fetchSeries(ctx, metric){
  return fetch("/meteohub/series?ctx="+ctx+"&metric="+metric+"&hours="+S.hours)
    .then(r=>r.json()).catch(()=>({ts:[],v:[],bucket_s:0}));
}

// Construit une carte-graphe pour UNE grandeur, selon la source choisie.
function chartCard(metricKey, packs){
  const md=metricDef(metricKey);
  const opt={unit:md[2],dec:md[3]};
  let series=[];
  if(S.source==="both"){
    series=[
      {ts:packs.out.ts||[],vals:packs.out.v||[],color:COL.out,label:"Extérieur",bucket:packs.out.bucket_s||0},
      {ts:packs.in.ts||[],vals:packs.in.v||[],color:COL.in,label:"Intérieur",bucket:packs.in.bucket_s||0}
    ];
  } else {
    const p=packs[S.source];
    series=[{ts:p.ts||[],vals:p.v||[],color:COL[S.source],label:srcLabel(S.source),bucket:p.bucket_s||0}];
  }
  const legend=series.map(s=>'<span class="k"><span class="sw" style="border-color:'+s.color+'"></span>'+s.label+'</span>').join("");
  const title=md[1]+(S.metric==="all"?"":" ("+srcLabel(S.source)+")");
  return '<div class="chart"><h3>'+title+'</h3>'+multiChart(series,opt)+'<div class="legend">'+legend+'</div></div>';
}

function draw(){
  const charts=$("#charts");
  charts.innerHTML='<p class="muted">Chargement&hellip;</p>';
  const metrics = S.metric==="all" ? METRICS.map(m=>m[0]) : [S.metric];
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];

  // Toutes les combinaisons (grandeur × source) en parallèle : lecture SQLite
  // locale, pas de souci de concurrence ici (contrairement à l'ESP32).
  const jobs=[];
  metrics.forEach(m=>ctxs.forEach(c=>jobs.push(fetchSeries(c,m).then(r=>({m,c,r})))));
  Promise.all(jobs).then(list=>{
    const byMetric={};
    list.forEach(x=>{(byMetric[x.m]=byMetric[x.m]||{})[x.c]=x.r;});
    let html="";
    metrics.forEach(m=>{
      const packs=byMetric[m]||{};
      // Garantir les clés attendues même si une source manque.
      ["out","in"].forEach(c=>{if(!packs[c])packs[c]={ts:[],v:[],bucket_s:0};});
      html+=chartCard(m,packs);
    });
    // Note contextuelle sous le dernier graphe.
    html+='<p class="note">'+(S.source==="both"
      ? "IN et OUT en deux couleurs. L'écart, le décalage et l'amortissement se lisent d'un coup d'œil ; pour les chiffres, voir « Comportement thermique » et « Modèle d'inertie » dans les <a href=\"/meteohub\">analyses</a>."
      : "Points reliés tant que l'écart reste proche de la cadence ; le trait n'est coupé que sur un vrai silence du capteur.")+'</p>';
    charts.innerHTML=html;
  });
}

// Sélecteurs
$("#metric").innerHTML=METRIC_OPTS.map(m=>'<option value="'+m[0]+'"'+(m[0]===S.metric?" selected":"")+'>'+m[1]+'</option>').join("");
$("#source").innerHTML=SOURCES.map(s=>'<option value="'+s[0]+'"'+(s[0]===S.source?" selected":"")+'>'+s[1]+'</option>').join("");
$("#periods").innerHTML=PERIODS.map(p=>'<button class="pbtn'+(p[1]===S.hours?" on":"")+'" data-h="'+p[1]+'">'+p[0]+'</button>').join("");

$("#metric").addEventListener("change",e=>{S.metric=e.target.value;localStorage.setItem(LS+"metric",S.metric);draw();});
$("#source").addEventListener("change",e=>{S.source=e.target.value;localStorage.setItem(LS+"source",S.source);draw();});
$("#periods").addEventListener("click",e=>{const b=e.target.closest(".pbtn");if(!b)return;
  S.hours=+b.dataset.h;localStorage.setItem(LS+"hours",S.hours);
  document.querySelectorAll(".pbtn").forEach(x=>x.classList.toggle("on",+x.dataset.h===S.hours));draw();});

fetch("/status").then(r=>r.json()).then(s=>{const b=$("#vb");if(b)b.textContent=s.version?"v"+s.version:"";}).catch(()=>{});

draw();
setInterval(draw, 60000); // rafraîchissement doux (mesures ~toutes les 5 min)
</script>
</body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
