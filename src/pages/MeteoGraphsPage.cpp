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
// décalage, l'amortissement, la montée, la stabilité - ce qu'aucun indicateur
// numérique ne remplace tout à fait.
//
// Volontairement simple : une grandeur, une période, une ou deux courbes (IN+OUT
// sur le même axe temporel). Page autonome (SVG dessiné côté navigateur, sans
// CDN), données via /meteohub/series.
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
.legend .sw{width:1.4rem;height:0;border-top-width:3px;border-top-style:solid;display:inline-block}
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

<div class="chart">
  <h3 id="chartTitle">Température</h3>
  <div id="plot"><p class="muted">Chargement&hellip;</p></div>
  <div class="legend" id="legend"></div>
  <p class="note" id="note"></p>
</div>
</div>
<script>
"use strict";
const LS="morfanalytics.graphs.";
const METRICS=[["temp","Température","°C",1],["hum","Humidité","%",0],["pres","Pression","hPa",1]];
const SOURCES=[["out","Extérieur"],["in","Intérieur"],["both","Intérieur + Extérieur"]];
const PERIODS=[["6 h",6],["12 h",12],["24 h",24],["3 j",72],["7 j",168],["30 j",720]];
// Couleurs : OUT bleu, IN vert (cohérent avec MeteoHub). En source unique, on
// garde une couleur par grandeur pour rester lisible.
const COL={out:"#6f9bff",in:"#7ee0b8"};
const METRIC_COL={temp:"#e6a54e",hum:"#7ee0b8",pres:"#c58bf2"};

let S={
  metric:localStorage.getItem(LS+"metric")||"temp",
  source:localStorage.getItem(LS+"source")||"out",
  hours:+(localStorage.getItem(LS+"hours")||24)
};

const $=s=>document.querySelector(s);
function metricDef(k){return METRICS.find(m=>m[0]===k)||METRICS[0];}
function fmtClock(ts){const d=new Date(ts*1000);const sameDay=(new Date()*1-d)<86400000;
  return d.toLocaleString("fr-FR",sameDay?{hour:"2-digit",minute:"2-digit"}:{day:"2-digit",month:"2-digit",hour:"2-digit",minute:"2-digit"});}

// Graphe multi-séries (SVG), auto-échelle sur l'ensemble, trous préservés (les
// null coupent le trait). `series` = [{ts,vals,color,label,dash}].
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
  series.forEach(s=>{let d="",started=false;
    for(let i=0;i<s.ts.length;i++){const v=s.vals[i];if(v===null||v===undefined){started=false;continue;}
      d+=(started?"L":"M")+X(s.ts[i]).toFixed(1)+" "+Y(v).toFixed(1)+" ";started=true;}
    const dash=s.dash?' stroke-dasharray="6 4"':"";
    paths+='<path d="'+d+'" fill="none" stroke="'+s.color+'" stroke-width="1.9"'+dash+'/>';
    // dernier point connu
    let last=null;for(let i=s.ts.length-1;i>=0;i--){if(s.vals[i]!==null&&s.vals[i]!==undefined){last=[s.ts[i],s.vals[i]];break;}}
    if(last)paths+='<circle cx="'+X(last[0]).toFixed(1)+'" cy="'+Y(last[1]).toFixed(1)+'" r="3" fill="'+s.color+'"/>';});
  return '<svg viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none" role="img">'+grid+labels+paths+xt0+xt1+'</svg>';
}

function fetchSeries(ctx){
  return fetch("/meteohub/series?ctx="+ctx+"&metric="+S.metric+"&hours="+S.hours)
    .then(r=>r.json()).catch(()=>({ts:[],v:[]}));
}

function draw(){
  const md=metricDef(S.metric);
  $("#chartTitle").textContent=md[1]+" ("+ (SOURCES.find(s=>s[0]===S.source)||["","",""])[1] +")";
  const opt={unit:md[2],dec:md[3]};
  const plot=$("#plot"), legend=$("#legend"), note=$("#note");
  plot.innerHTML='<p class="muted">Chargement&hellip;</p>';

  const jobs = S.source==="both" ? [fetchSeries("out"),fetchSeries("in")] : [fetchSeries(S.source)];
  Promise.all(jobs).then(res=>{
    let series=[];
    if(S.source==="both"){
      series=[
        {ts:res[0].ts||[],vals:res[0].v||[],color:COL.out,label:"Extérieur",dash:false},
        {ts:res[1].ts||[],vals:res[1].v||[],color:COL.in,label:"Intérieur",dash:true}
      ];
    } else {
      const col = S.source==="out"?COL.out : S.source==="in"?COL.in : METRIC_COL[S.metric];
      series=[{ts:res[0].ts||[],vals:res[0].v||[],color:col,label:(SOURCES.find(s=>s[0]===S.source)||["","",""])[1],dash:false}];
    }
    plot.innerHTML=multiChart(series,opt);
    legend.innerHTML=series.map(s=>'<span class="k"><span class="sw" style="border-top-color:'+s.color+(s.dash?';border-top-style:dashed':'')+'"></span>'+s.label+'</span>').join("");
    // Note contextuelle : en IN+OUT, on invite à lire l'inertie dans l'analyse dédiée.
    note.innerHTML = S.source==="both"
      ? "L'écart, le décalage et l'amortissement se lisent d'un coup d'œil. Pour les chiffres, voir « Comportement thermique » et « Modèle d'inertie » dans les <a href=\"/meteohub\">analyses</a>."
      : "Trous préservés : une coupure de trait = pas de mesure sur la tranche (jamais comblée par un zéro).";
  });
}

// Sélecteurs
$("#metric").innerHTML=METRICS.map(m=>'<option value="'+m[0]+'"'+(m[0]===S.metric?" selected":"")+'>'+m[1]+'</option>').join("");
$("#source").innerHTML=SOURCES.map(s=>'<option value="'+s[0]+'"'+(s[0]===S.source?" selected":"")+'>'+s[1]+'</option>').join("");
$("#periods").innerHTML=PERIODS.map(p=>'<button class="pbtn'+(p[1]===S.hours?" on":"")+'" data-h="'+p[1]+'">'+p[0]+'</button>').join("");

$("#metric").addEventListener("change",e=>{S.metric=e.target.value;localStorage.setItem(LS+"metric",S.metric);draw();});
$("#source").addEventListener("change",e=>{S.source=e.target.value;localStorage.setItem(LS+"source",S.source);draw();});
$("#periods").addEventListener("click",e=>{const b=e.target.closest(".pbtn");if(!b)return;
  S.hours=+b.dataset.h;localStorage.setItem(LS+"hours",S.hours);
  document.querySelectorAll(".pbtn").forEach(x=>x.classList.toggle("on",+x.dataset.h===S.hours));draw();});

fetch("/status").then(r=>r.json()).then(s=>{const b=$("#vb");if(b)b.textContent=s.version?"v"+s.version:"";}).catch(()=>{});

draw();
setInterval(draw, 60000); // rafraîchissement doux (les mesures arrivent ~toutes les 5 min)
</script>
</body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
