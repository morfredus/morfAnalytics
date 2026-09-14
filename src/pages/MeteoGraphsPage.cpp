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
// répond « montre-moi ce qui s'est réellement passé ».
//
// Choix de tracé :
//   - Mesures ~toutes les 5 min (jusqu'à 10 au pire) : ces écarts ne sont pas des
//     trous. On relie les points ; le trait n'est coupé que sur un vrai silence
//     (écart bien plus grand que la cadence, seuil plancher ~20 min, quelle que
//     soit la finesse de sous-échantillonnage de la période).
//   - IN et OUT : deux COULEURS distinctes (bleu / orange) en vue mono-grandeur.
//   - « Toutes » : UN SEUL graphe, les 3 grandeurs superposées (6 courbes avec
//     IN+OUT). Comme les échelles °C / % / hPa n'ont rien à voir, chaque grandeur
//     est normalisée à sa propre échelle (on compare les FORMES et le TIMING, pas
//     les valeurs absolues) : couleur par grandeur, et l'intérieur en trait plus
//     fin et atténué pour distinguer IN de OUT sans pointillés. Les plages
//     réelles sont rappelées dans la légende.
//
// Page autonome (SVG côté navigateur, sans CDN), données via /meteohub/series.
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
  <label>Afficher&nbsp;<select id="metric"></select></label>
  <label>Source&nbsp;<select id="source"></select></label>
  <div class="periods" id="periods"></div>
</div>

<div id="charts"><p class="muted">Chargement&hellip;</p></div>
</div>
<script>
"use strict";
const LS="morfanalytics.graphs.";
// [clé, libellé, unité, décimales, couleur (mode "Toutes")]
const METRICS=[
  ["temp","Température","°C",1,"#e6a54e"],
  ["hum","Humidité","%",0,"#7ee0b8"],
  ["pres","Pression","hPa",1,"#c58bf2"]
];
const METRIC_OPTS=METRICS.map(m=>[m[0],m[1]]).concat([["all","Toutes"]]);
const SOURCES=[["out","Extérieur"],["in","Intérieur"],["both","Intérieur + Extérieur"]];
const PERIODS=[["6 h",6],["12 h",12],["24 h",24],["3 j",72],["7 j",168],["30 j",720]];
// Mono-grandeur : IN et OUT en deux couleurs bien distinctes (jamais de pointillés).
const SRC_COL={out:"#6f9bff",in:"#e6a54e"};
// Cadence des mesures ~5 min (jusqu'à 10 au boot). On relie les points tant que
// l'écart reste sous ce seuil ; au-delà, c'est un vrai silence -> trait coupé.
const CONNECT_MAX_S=20*60;

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
function minMax(vals){let mn=Infinity,mx=-Infinity;for(const v of vals){if(v!==null&&v!==undefined){if(v<mn)mn=v;if(v>mx)mx=v;}}return [mn,mx];}

// Graphe SVG. `series` = [{ts,vals,color,width,opacity,bucket,smin,smax}] : chaque
// série est tracée avec SA propre échelle [smin,smax] (permet de superposer des
// grandeurs d'échelles différentes). `opt.axis` = échelle numérique affichée à
// gauche (mono-grandeur) ; sinon grille sans valeurs (mode "Toutes", normalisé).
function chart(series, opt){
  opt=opt||{};
  const W=760,H=230,pL=opt.axis?54:16,pR=12,pT=12,pB=24;
  let t0=Infinity,t1=-Infinity,any=false;
  series.forEach(s=>{for(let i=0;i<s.vals.length;i++){const v=s.vals[i];
    if(v!==null&&v!==undefined){any=true;const t=s.ts[i];if(t<t0)t0=t;if(t>t1)t1=t;}}});
  if(!any) return '<div class="muted">Pas encore de mesure sur cette période.</div>';
  if(!(t1>t0))t1=t0+1;
  const X=t=>pL+(W-pL-pR)*((t-t0)/Math.max(1,(t1-t0)));
  const Ys=(s,v)=>pT+(H-pT-pB)*(1-(v-s.smin)/Math.max(1e-9,s.smax-s.smin));
  let grid="",labels="";
  [0,.5,1].forEach(f=>{const y=pT+(H-pT-pB)*(1-f);
    grid+='<line class="grid-l" x1="'+pL+'" y1="'+y.toFixed(1)+'" x2="'+(W-pR)+'" y2="'+y.toFixed(1)+'"/>';
    if(opt.axis){const val=opt.axis.min+(opt.axis.max-opt.axis.min)*f;
      labels+='<text class="ax" x="'+(pL-6)+'" y="'+(y+3).toFixed(1)+'" text-anchor="end">'+val.toFixed(opt.axis.dec||0)+'</text>';}});
  const xt0='<text class="ax" x="'+pL+'" y="'+(H-6)+'">'+fmtClock(t0)+'</text>';
  const xt1='<text class="ax" x="'+(W-pR)+'" y="'+(H-6)+'" text-anchor="end">'+fmtClock(t1)+'</text>';
  let paths="";
  series.forEach(s=>{
    // Seuil de coupure PAR SÉRIE : proportionnel à la tranche (pour que les vues
    // longues, où un point = plusieurs heures, restent des courbes) avec un
    // plancher à la cadence (pour les vues courtes, où la tranche < 5 min). On ne
    // coupe donc que sur un écart bien plus grand que l'espacement normal : un
    // VRAI silence du capteur, quelle que soit la période.
    const gapMax=Math.max((s.bucket>0?s.bucket:600)*2.5, CONNECT_MAX_S);
    let d="",prevT=null;
    for(let i=0;i<s.ts.length;i++){const v=s.vals[i];if(v===null||v===undefined)continue;
      const t=s.ts[i];const move=(prevT===null)||((t-prevT)>gapMax);
      d+=(move?"M":"L")+X(t).toFixed(1)+" "+Ys(s,v).toFixed(1)+" ";prevT=t;}
    const op=(s.opacity!==undefined?' stroke-opacity="'+s.opacity+'"':"");
    paths+='<path d="'+d+'" fill="none" stroke="'+s.color+'" stroke-width="'+(s.width||2)+'"'+op+'/>';
    let last=null;for(let i=s.ts.length-1;i>=0;i--){if(s.vals[i]!==null&&s.vals[i]!==undefined){last=[s.ts[i],s.vals[i]];break;}}
    if(last)paths+='<circle cx="'+X(last[0]).toFixed(1)+'" cy="'+Ys(s,last[1]).toFixed(1)+'" r="3" fill="'+s.color+'"'+op+'/>';});
  return '<svg viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none" role="img">'+grid+labels+paths+xt0+xt1+'</svg>';
}

function fetchSeries(ctx, metric){
  return fetch("/meteohub/series?ctx="+ctx+"&metric="+metric+"&hours="+S.hours)
    .then(r=>r.json()).catch(()=>({ts:[],v:[],bucket_s:0}));
}

function nf(v,d){return (v===null||!isFinite(v))?"—":v.toFixed(d);}

// Vue MONO-GRANDEUR : une carte, échelle numérique, IN/OUT en deux couleurs.
function renderSingle(byCtx){
  const md=metricDef(S.metric);
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];
  let allv=[]; ctxs.forEach(c=>{allv=allv.concat((byCtx[c].v||[]).filter(x=>x!==null&&x!==undefined));});
  let [mn,mx]=minMax(allv); if(!isFinite(mn)){mn=0;mx=1;}
  let pad=(mx-mn)*0.08; if(pad<0.5)pad=0.5; const smin=mn-pad, smax=mx+pad;
  const series=ctxs.map(c=>({ts:byCtx[c].ts||[],vals:byCtx[c].v||[],color:SRC_COL[c],width:2,bucket:byCtx[c].bucket_s||0,smin,smax}));
  const legend=ctxs.map(c=>'<span class="k"><span class="sw" style="border-color:'+SRC_COL[c]+'"></span>'+srcLabel(c)+'</span>').join("");
  const title=md[1]+" ("+md[2]+")";
  return '<div class="chart"><h3>'+title+'</h3>'+chart(series,{axis:{min:smin,max:smax,dec:md[3]}})+'<div class="legend">'+legend+'</div></div>';
}

// Vue TOUTES : une seule carte, 3 grandeurs superposées (6 courbes avec IN+OUT),
// chacune normalisée à sa propre échelle. Couleur = grandeur ; l'intérieur est
// plus fin et atténué. Les plages réelles sont rappelées dans la légende.
function renderAll(data){
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];
  let series=[], legend=[];
  METRICS.forEach(m=>{
    const key=m[0], col=m[4];
    let allv=[]; ctxs.forEach(c=>{allv=allv.concat((data[key][c].v||[]).filter(x=>x!==null&&x!==undefined));});
    let [mn,mx]=minMax(allv); if(!isFinite(mn)){return;} // pas de mesure pour cette grandeur
    let pad=(mx-mn)*0.08; if(pad<(m[3]?0.5:1))pad=(m[3]?0.5:1); const smin=mn-pad, smax=mx+pad;
    ctxs.forEach(c=>{
      const inner=(c==="in");
      series.push({ts:data[key][c].ts||[],vals:data[key][c].v||[],color:col,
        width:inner?1.4:2.2, opacity:inner?0.55:1, bucket:data[key][c].bucket_s||0, smin,smax});
    });
    const range=nf(mn,m[3])+"–"+nf(mx,m[3])+" "+m[2];
    ctxs.forEach(c=>{
      const inner=(c==="in");
      legend.push('<span class="k"><span class="sw" style="border-color:'+col+';opacity:'+(inner?0.55:1)+';border-top-width:'+(inner?2:3)+'px"></span>'+
        m[1]+(S.source==="both"?" "+(inner?"(int)":"(ext)"):"")+(c===ctxs[ctxs.length-1]?' · '+range:'')+'</span>');
    });
  });
  const title = S.source==="both" ? "Toutes les grandeurs (Intérieur + Extérieur)" : "Toutes les grandeurs ("+srcLabel(S.source)+")";
  const body = series.length ? chart(series,{}) : '<div class="muted">Pas encore de mesure sur cette période.</div>';
  return '<div class="chart"><h3>'+title+'</h3>'+body+'<div class="legend">'+legend.join("")+'</div></div>';
}

function draw(){
  const charts=$("#charts");
  charts.innerHTML='<p class="muted">Chargement&hellip;</p>';
  const metrics = S.metric==="all" ? METRICS.map(m=>m[0]) : [S.metric];
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];

  const jobs=[];
  metrics.forEach(m=>ctxs.forEach(c=>jobs.push(fetchSeries(c,m).then(r=>({m,c,r})))));
  Promise.all(jobs).then(list=>{
    const data={};
    list.forEach(x=>{(data[x.m]=data[x.m]||{})[x.c]=x.r;});
    metrics.forEach(m=>{const d=data[m]=data[m]||{};["out","in"].forEach(c=>{if(!d[c])d[c]={ts:[],v:[],bucket_s:0};});});

    let html;
    if(S.metric==="all"){
      html=renderAll(data);
      html+='<p class="note">'+(S.source==="both"
        ? "Six courbes sur un axe de temps commun ; chaque grandeur garde sa propre échelle (on compare les formes et le décalage, pas les valeurs absolues). Intérieur en trait plus fin et atténué. Plages réelles dans la légende."
        : "Trois grandeurs sur un axe de temps commun ; chacune à sa propre échelle. Plages réelles dans la légende.")+'</p>';
    } else {
      html=renderSingle(data[S.metric]);
      html+='<p class="note">'+(S.source==="both"
        ? "IN et OUT en deux couleurs. L'écart, le décalage et l'amortissement se lisent d'un coup d'œil ; pour les chiffres, voir « Comportement thermique » et « Modèle d'inertie » dans les <a href=\"/meteohub\">analyses</a>."
        : "Points reliés tant que l'écart reste proche de la cadence ; le trait n'est coupé que sur un vrai silence du capteur.")+'</p>';
    }
    charts.innerHTML=html;
  });
}

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
setInterval(draw, 60000);
</script>
</body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
