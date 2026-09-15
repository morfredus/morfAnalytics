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
// « Montre-moi ce qui s'est réellement passé » (complément visuel des analyses).
//   - Mesures ~toutes les 5 min : on relie les points ; le trait n'est coupé que
//     sur un vrai silence (seuil par série = max(2,5 x tranche, 20 min)).
//   - Échelles de valeurs à GAUCHE et à DROITE (dynamiques). En « Toutes », chaque
//     grandeur a son propre axe (temp à gauche, humidité et pression à droite),
//     couleur = grandeur ; l'intérieur en trait plus fin et atténué.
//   - Survol : ligne-guide + infobulle donnant, à l'instant pointé, la valeur de
//     chaque courbe.
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
.plot{position:relative;margin-top:.5rem}
.plot svg{display:block;width:100%;height:auto}
.ax{fill:var(--muted);font-size:11px}.grid-l{stroke:var(--line);stroke-width:1}
.legend{display:flex;gap:1.1rem;flex-wrap:wrap;margin-top:.5rem;font-size:.85rem;color:var(--soft)}
.legend .k{display:inline-flex;align-items:center;gap:.4rem}
.legend .sw{width:1.4rem;height:0;border-top:3px solid;display:inline-block}
.note{color:var(--muted);font-size:.82rem;margin-top:.4rem}
.tip{position:absolute;pointer-events:none;background:#0e1013;border:1px solid var(--line);border-radius:8px;padding:.4rem .55rem;font-size:.82rem;color:var(--ink);box-shadow:0 4px 14px rgba(0,0,0,.45);z-index:5;white-space:nowrap}
.tip .th{color:var(--muted);margin-bottom:.2rem;font-variant-numeric:tabular-nums}
.tip .tr{display:flex;align-items:center;gap:.4rem;font-variant-numeric:tabular-nums}
.tip .sw{width:.7rem;height:.7rem;border-radius:2px;display:inline-block}
/* Marqueurs de croisement dessinés dans le SVG (numéro relié à l'encart). */
.cnum{font:700 11px system-ui,sans-serif}
/* Encart « Croisements détectés », sous le graphique. */
.cross{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:.8rem 1.1rem;margin:.4rem 0 1.2rem}
.cross h4{margin:0 0 .3rem;font-size:1rem}
.cintro{color:var(--muted);font-size:.82rem;margin:.2rem 0 .7rem}
.clist{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:.55rem}
.clist li{display:flex;gap:.6rem;align-items:flex-start}
.clist .cn{font-weight:700;min-width:1.2rem;text-align:right;font-variant-numeric:tabular-nums}
.ct{font-weight:600;font-variant-numeric:tabular-nums}
.cd{color:var(--soft);font-size:.9rem}
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
// [clé, libellé, unité, décimales, couleur (mode "Toutes"), bande morte croisement]
// La bande morte (eps) évite de compter comme événements de simples oscillations
// autour de l'égalité : on ne valide un croisement que si l'écart repart franchement
// de l'autre côté (au-delà de eps, dans l'unité de la grandeur).
const METRICS=[
  ["temp","Température","°C",1,"#e6a54e",0.2],
  ["hum","Humidité","%",0,"#7ee0b8",1.0],
  ["pres","Pression","hPa",1,"#c58bf2",0.3]
];
const METRIC_OPTS=METRICS.map(m=>[m[0],m[1]]).concat([["all","Toutes"]]);
const SOURCES=[["out","Extérieur"],["in","Intérieur"],["both","Intérieur + Extérieur"]];
const PERIODS=[["6 h",6],["12 h",12],["24 h",24],["3 j",72],["7 j",168],["30 j",720]];
// Mono-grandeur : IN et OUT en deux couleurs distinctes (jamais de pointillés).
const SRC_COL={out:"#6f9bff",in:"#e6a54e"};
// Cadence ~5 min (10 au boot) : plancher de connexion des points.
const CONNECT_MIN_S=20*60;
const AXIS_MUTED="#99a1ad";

let S={
  metric:localStorage.getItem(LS+"metric")||"temp",
  source:localStorage.getItem(LS+"source")||"out",
  hours:+(localStorage.getItem(LS+"hours")||24)
};
let G=null; // géométrie + séries du graphe courant, pour le survol

const $=s=>document.querySelector(s);
function metricDef(k){return METRICS.find(m=>m[0]===k)||METRICS[0];}
function srcLabel(k){return (SOURCES.find(s=>s[0]===k)||["","",""])[1];}
function fmtClock(ts){const d=new Date(ts*1000);const sameDay=(new Date()*1-d)<86400000;
  return d.toLocaleString("fr-FR",sameDay?{hour:"2-digit",minute:"2-digit"}:{day:"2-digit",month:"2-digit",hour:"2-digit",minute:"2-digit"});}
function fmtFull(ts){return new Date(ts*1000).toLocaleString("fr-FR",{day:"2-digit",month:"2-digit",hour:"2-digit",minute:"2-digit"});}
function minMax(vals){let mn=Infinity,mx=-Infinity;for(const v of vals){if(v!==null&&v!==undefined){if(v<mn)mn=v;if(v>mx)mx=v;}}return [mn,mx];}
function nf(v,d){return (v===null||!isFinite(v))?"—":v.toFixed(d);}
function fmtNum(v,d){return v.toLocaleString("fr-FR",{minimumFractionDigits:d,maximumFractionDigits:d});}

// --- Croisements de séries comparables (même grandeur, même unité) -----------
// D(t) = Extérieur(t) - Intérieur(t). Un croisement = changement de signe de D
// entre deux mesures. Comme les deux séries sont sous-échantillonnées séparément,
// on projette chacune sur une horloge commune (union des instants) par
// interpolation linéaire, sans jamais inventer une valeur au travers d'un vrai
// trou de mesure (au-delà du plancher de connexion). L'instant et la valeur du
// croisement sont eux aussi interpolés : ce sont des estimations, pas des mesures.

// Valeur interpolée d'une série à l'instant t ; null hors plage ou en travers
// d'un silence du capteur (écart entre échantillons > gapMax).
function interpAt(ts,vals,t,gapMax){
  const n=ts.length; if(!n) return null;
  if(t<ts[0]||t>ts[n-1]) return null;
  let i=0; while(i<n-1 && ts[i+1]<t) i++;
  if(ts[i]===t) return (vals[i]!==null&&vals[i]!==undefined)?vals[i]:null;
  if(i>=n-1) return null;
  const t0=ts[i],t1=ts[i+1],v0=vals[i],v1=vals[i+1];
  if(v0===null||v0===undefined||v1===null||v1===undefined) return null;
  if(t1-t0>gapMax) return null; // en travers d'un vrai trou : on n'invente pas
  return v0+(v1-v0)*(t-t0)/(t1-t0);
}

// Détecte les croisements entre la série extérieure (o) et intérieure (inn).
// `eps` = bande morte : on ne valide un croisement que si D repart au-delà de eps
// du côté opposé, ce qui écarte le bruit et les recroisements immédiats.
// Retourne [{ts, value}] (instant et valeur estimés à l'égalité).
function computeCrossings(o,inn,eps){
  const goMax=Math.max((o.bucket>0?o.bucket:600)*2.5, CONNECT_MIN_S);
  const giMax=Math.max((inn.bucket>0?inn.bucket:600)*2.5, CONNECT_MIN_S);
  const tset=new Set();
  o.ts.forEach((t,i)=>{if(o.vals[i]!==null&&o.vals[i]!==undefined)tset.add(t);});
  inn.ts.forEach((t,i)=>{if(inn.vals[i]!==null&&inn.vals[i]!==undefined)tset.add(t);});
  const T=[...tset].sort((a,b)=>a-b);
  const cr=[];
  // On suit le dernier point à écart NON nul (lT,lD,lA) : le changement de signe
  // se lit entre deux tels points, ce qui reste correct même si une mesure tombe
  // pile sur l'égalité (D=0) et évite de rater le croisement.
  let sign=0, cand=null, lT=null, lD=null, lA=null;
  for(const t of T){
    const a=interpAt(o.ts,o.vals,t,goMax);
    const b=interpAt(inn.ts,inn.vals,t,giMax);
    if(a===null||b===null){lT=null;lD=null;lA=null;continue;} // trou : on ne franchit pas
    const d=a-b;
    if(d!==0){
      if(lD!==null && lD*d<0){ // changement de signe entre deux points encadrants
        const tc=lT+(t-lT)*lD/(lD-d);
        const vc=lA+(a-lA)*(tc-lT)/(t-lT);
        cand={ts:tc,value:vc};
      }
      lT=t;lD=d;lA=a;
    }
    if(Math.abs(d)>=eps){ // côté franchement établi : on tranche
      const ns=d>0?1:-1;
      if(ns!==sign && sign!==0 && cand) cr.push(cand);
      if(ns!==sign) sign=ns;
      cand=null; // oscillation sous eps oubliée : un nouveau croisement devra survenir
    }
  }
  return cr;
}

// Encart « Croisements détectés » listé chronologiquement sous le graphique.
function crossingsBox(list){
  const intro="Les croisements sont calculés uniquement entre séries représentant la même "+
    "grandeur physique. L'heure et la valeur du croisement sont interpolées entre les mesures "+
    "qui encadrent l'égalité.";
  if(!list.length){
    return '<div class="cross"><h4>Croisements détectés</h4><p class="cintro">'+intro+'</p>'+
      '<p class="muted">Aucun croisement sur la période affichée.</p></div>';
  }
  const rows=list.map(c=>'<li><span class="cn" style="color:'+c.color+'">'+c.n+'</span>'+
    '<div class="cev"><div class="ct">'+fmtClock(c.ts)+' - '+c.metricName+'</div>'+
    '<div class="cd">Extérieur rejoint Intérieur à '+fmtNum(c.value,c.dec)+' '+c.unit+'.</div>'+
    '</div></li>').join("");
  return '<div class="cross"><h4>Croisements détectés</h4><p class="cintro">'+intro+
    '</p><ul class="clist">'+rows+'</ul></div>';
}

// Graphe SVG. `series` = [{ts,vals,color,width,opacity,bucket,smin,smax,label,unit,dec}].
// `axes` = [{min,max,dec,side:'L'|'R',col}] : échelles de valeurs affichées à
// gauche/droite (dynamiques). Chaque série est tracée avec SA propre [smin,smax].
// `crossings` (optionnel) = [{ts,value,smin,smax,color,n}] : marqueurs des
// croisements de séries comparables, chacun dessiné à SA propre échelle.
function buildChart(series, axes, crossings){
  const W=760,H=240,pT=12,pB=24;
  const lefts=axes.filter(a=>a.side==='L'), rights=axes.filter(a=>a.side==='R');
  const pL = lefts.length ? 48 : 16;
  const pR = 14 + rights.length*46;
  let t0=Infinity,t1=-Infinity,any=false;
  series.forEach(s=>{for(let i=0;i<s.vals.length;i++){const v=s.vals[i];
    if(v!==null&&v!==undefined){any=true;const t=s.ts[i];if(t<t0)t0=t;if(t>t1)t1=t;}}});
  if(!any){G=null;return '<div class="muted">Pas encore de mesure sur cette période.</div>';}
  if(!(t1>t0))t1=t0+1;
  const X=t=>pL+(W-pL-pR)*((t-t0)/Math.max(1,(t1-t0)));
  const Ys=(s,v)=>pT+(H-pT-pB)*(1-(v-s.smin)/Math.max(1e-9,s.smax-s.smin));

  let grid="",labels="";
  [0,.5,1].forEach(f=>{const y=pT+(H-pT-pB)*(1-f);
    grid+='<line class="grid-l" x1="'+pL+'" y1="'+y.toFixed(1)+'" x2="'+(W-pR)+'" y2="'+y.toFixed(1)+'"/>';
    lefts.forEach(a=>{const val=a.min+(a.max-a.min)*f;
      labels+='<text class="ax" x="'+(pL-6)+'" y="'+(y+3).toFixed(1)+'" text-anchor="end" fill="'+a.col+'">'+val.toFixed(a.dec||0)+'</text>';});
    rights.forEach((a,ri)=>{const val=a.min+(a.max-a.min)*f;const xr=(W-pR)+10+ri*46;
      labels+='<text class="ax" x="'+xr+'" y="'+(y+3).toFixed(1)+'" text-anchor="start" fill="'+a.col+'">'+val.toFixed(a.dec||0)+'</text>';});});
  const xt0='<text class="ax" x="'+pL+'" y="'+(H-6)+'">'+fmtClock(t0)+'</text>';
  const xt1='<text class="ax" x="'+(W-pR)+'" y="'+(H-6)+'" text-anchor="end">'+fmtClock(t1)+'</text>';

  let paths="";
  series.forEach(s=>{
    const gapMax=Math.max((s.bucket>0?s.bucket:600)*2.5, CONNECT_MIN_S);
    let d="",prevT=null;
    for(let i=0;i<s.ts.length;i++){const v=s.vals[i];if(v===null||v===undefined)continue;
      const t=s.ts[i];const move=(prevT===null)||((t-prevT)>gapMax);
      d+=(move?"M":"L")+X(t).toFixed(1)+" "+Ys(s,v).toFixed(1)+" ";prevT=t;}
    const op=(s.opacity!==undefined?' stroke-opacity="'+s.opacity+'"':"");
    paths+='<path d="'+d+'" fill="none" stroke="'+s.color+'" stroke-width="'+(s.width||2)+'"'+op+'/>';
    let last=null;for(let i=s.ts.length-1;i>=0;i--){if(s.vals[i]!==null&&s.vals[i]!==undefined){last=[s.ts[i],s.vals[i]];break;}}
    if(last)paths+='<circle cx="'+X(last[0]).toFixed(1)+'" cy="'+Ys(s,last[1]).toFixed(1)+'" r="3" fill="'+s.color+'"'+op+'/>';});

  // Marqueurs de croisement : guide vertical + losange à la valeur estimée, avec
  // le numéro qui relie le marqueur à la ligne correspondante de l'encart.
  let marks="";
  (crossings||[]).forEach(c=>{
    if(c.ts<t0||c.ts>t1)return;
    const x=X(c.ts);
    const yv=pT+(H-pT-pB)*(1-(c.value-c.smin)/Math.max(1e-9,c.smax-c.smin));
    const r=4.5;
    marks+='<line x1="'+x.toFixed(1)+'" y1="'+pT+'" x2="'+x.toFixed(1)+'" y2="'+(H-pB)+
      '" stroke="'+c.color+'" stroke-opacity="0.45" stroke-dasharray="3 3" stroke-width="1"/>';
    marks+='<path d="M'+x.toFixed(1)+' '+(yv-r).toFixed(1)+' L'+(x+r).toFixed(1)+' '+yv.toFixed(1)+
      ' L'+x.toFixed(1)+' '+(yv+r).toFixed(1)+' L'+(x-r).toFixed(1)+' '+yv.toFixed(1)+
      ' Z" fill="'+c.color+'" stroke="#0e1013" stroke-width="1.2"/>';
    marks+='<text class="cnum" x="'+(x+6).toFixed(1)+'" y="'+(yv-6).toFixed(1)+'" fill="'+c.color+'">'+c.n+'</text>';
  });

  // Contexte de survol : tout ce qu'il faut pour retrouver un point depuis la souris.
  G={W,H,pL,pR,pT,pB,t0,t1,series};

  return '<svg id="gsvg" viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none" role="img">'+
    grid+labels+paths+marks+'<g id="hoverg"></g>'+xt0+xt1+'</svg>';
}

function fetchSeries(ctx, metric){
  return fetch("/meteohub/series?ctx="+ctx+"&metric="+metric+"&hours="+S.hours)
    .then(r=>r.json()).catch(()=>({ts:[],v:[],bucket_s:0}));
}

// Vue MONO-GRANDEUR : IN/OUT en deux couleurs, échelle numérique à gauche ET à droite.
function renderSingle(byCtx){
  const md=metricDef(S.metric);
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];
  let allv=[]; ctxs.forEach(c=>{allv=allv.concat((byCtx[c].v||[]).filter(x=>x!==null&&x!==undefined));});
  let [mn,mx]=minMax(allv); if(!isFinite(mn)){mn=0;mx=1;}
  let pad=(mx-mn)*0.08; if(pad<(md[3]?0.5:1))pad=(md[3]?0.5:1); const smin=mn-pad, smax=mx+pad;
  const series=ctxs.map(c=>({ts:byCtx[c].ts||[],vals:byCtx[c].v||[],color:SRC_COL[c],width:2,
    bucket:byCtx[c].bucket_s||0,smin,smax,label:srcLabel(c),unit:md[2],dec:md[3]}));
  const axes=[{min:smin,max:smax,dec:md[3],side:'L',col:AXIS_MUTED},
              {min:smin,max:smax,dec:md[3],side:'R',col:AXIS_MUTED}];
  const legend=ctxs.map(c=>'<span class="k"><span class="sw" style="border-color:'+SRC_COL[c]+'"></span>'+srcLabel(c)+'</span>').join("");
  // Croisements : seulement quand Intérieur ET Extérieur d'une même grandeur sont tracés.
  let crossings=[];
  if(S.source==="both"){
    const o={ts:byCtx.out.ts||[],vals:byCtx.out.v||[],bucket:byCtx.out.bucket_s||0};
    const inn={ts:byCtx.in.ts||[],vals:byCtx.in.v||[],bucket:byCtx.in.bucket_s||0};
    crossings=computeCrossings(o,inn,md[5]).map((c,i)=>({ts:c.ts,value:c.value,smin,smax,
      color:md[4],n:i+1,metricName:md[1],unit:md[2],dec:md[3]}));
  }
  const box = S.source==="both" ? crossingsBox(crossings) : "";
  return '<div class="chart"><h3>'+md[1]+" ("+md[2]+")"+'</h3><div class="plot">'+buildChart(series,axes,crossings)+
    '<div class="tip" hidden></div></div><div class="legend">'+legend+'</div>'+noteFor()+'</div>'+box;
}

// Vue TOUTES : un seul graphe, 3 grandeurs superposées (6 courbes avec IN+OUT).
// Chaque grandeur a son axe : température à gauche, humidité et pression à droite.
function renderAll(data){
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];
  let series=[], axes=[], legend=[], rightCount=0, allCross=[];
  METRICS.forEach((m,mi)=>{
    const key=m[0], col=m[4];
    let allv=[]; ctxs.forEach(c=>{allv=allv.concat((data[key][c].v||[]).filter(x=>x!==null&&x!==undefined));});
    let [mn,mx]=minMax(allv); if(!isFinite(mn))return;
    let pad=(mx-mn)*0.08; if(pad<(m[3]?0.5:1))pad=(m[3]?0.5:1); const smin=mn-pad, smax=mx+pad;
    axes.push({min:smin,max:smax,dec:m[3],side: mi===0?'L':'R',col:col});
    ctxs.forEach(c=>{const inner=(c==="in");
      series.push({ts:data[key][c].ts||[],vals:data[key][c].v||[],color:col,
        width:inner?1.4:2.2,opacity:inner?0.55:1,bucket:data[key][c].bucket_s||0,smin,smax,
        label:m[1]+(S.source==="both"?(inner?" (int)":" (ext)"):""),unit:m[2],dec:m[3]});});
    const range=nf(mn,m[3])+"–"+nf(mx,m[3])+" "+m[2];
    ctxs.forEach(c=>{const inner=(c==="in");
      legend.push('<span class="k"><span class="sw" style="border-color:'+col+';opacity:'+(inner?0.55:1)+';border-top-width:'+(inner?2:3)+'px"></span>'+
        m[1]+(S.source==="both"?" "+(inner?"(int)":"(ext)"):"")+(c===ctxs[ctxs.length-1]?' · '+range:'')+'</span>');});
    // Croisements de CETTE grandeur (à son échelle), seulement si int + ext présents.
    if(S.source==="both"){
      const o={ts:data[key].out.ts||[],vals:data[key].out.v||[],bucket:data[key].out.bucket_s||0};
      const inn={ts:data[key].in.ts||[],vals:data[key].in.v||[],bucket:data[key].in.bucket_s||0};
      computeCrossings(o,inn,m[5]).forEach(c=>allCross.push({ts:c.ts,value:c.value,smin,smax,
        color:col,metricName:m[1],unit:m[2],dec:m[3]}));
    }
  });
  // Timeline unifiée : tous les croisements dans l'ordre, une numérotation commune
  // partagée entre les marqueurs du graphe et l'encart.
  allCross.sort((a,b)=>a.ts-b.ts);
  allCross.forEach((c,i)=>{c.n=i+1;});
  const title = S.source==="both" ? "Toutes les grandeurs (Intérieur + Extérieur)" : "Toutes les grandeurs ("+srcLabel(S.source)+")";
  const body = series.length ? buildChart(series,axes,allCross) : '<div class="muted">Pas encore de mesure sur cette période.</div>';
  const box = S.source==="both" ? crossingsBox(allCross) : "";
  return '<div class="chart"><h3>'+title+'</h3><div class="plot">'+body+'<div class="tip" hidden></div></div>'+
    '<div class="legend">'+legend.join("")+'</div>'+noteFor()+'</div>'+box;
}

function noteFor(){
  if(S.metric==="all"){
    return '<p class="note">'+(S.source==="both"
      ? "Six courbes sur un axe de temps commun ; chaque grandeur a sa propre échelle (temp à gauche, humidité et pression à droite). Intérieur en trait plus fin et atténué. Survolez pour lire les valeurs."
      : "Trois grandeurs sur un axe de temps commun, chacune à son échelle. Survolez pour lire les valeurs.")+'</p>';
  }
  return '<p class="note">'+(S.source==="both"
    ? "IN et OUT en deux couleurs ; échelle à gauche et à droite. Pour les chiffres d'inertie, voir « Comportement thermique » et « Modèle d'inertie » dans les <a href=\"/meteohub\">analyses</a>."
    : "Échelle à gauche et à droite. Points reliés tant que l'écart reste proche de la cadence ; coupé seulement sur un vrai silence du capteur.")+'</p>';
}

// --- Survol : ligne-guide + infobulle des valeurs à l'instant pointé ----------
function attachHover(){
  const svg=$("#gsvg"); const tip=document.querySelector(".plot .tip");
  const hg=$("#hoverg"); if(!svg||!tip||!hg||!G)return;
  const plot=svg.closest(".plot");
  function move(ev){
    const r=svg.getBoundingClientRect();
    const sx=(ev.clientX-r.left)/r.width*G.W;
    if(sx<G.pL||sx>G.W-G.pR){leave();return;}
    const t=G.t0+(sx-G.pL)/Math.max(1,(G.W-G.pL-G.pR))*(G.t1-G.t0);
    const X=tt=>G.pL+(G.W-G.pL-G.pR)*((tt-G.t0)/Math.max(1,(G.t1-G.t0)));
    const Ys=(s,v)=>G.pT+(G.H-G.pT-G.pB)*(1-(v-s.smin)/Math.max(1e-9,s.smax-s.smin));
    let dots="",rows="";
    G.series.forEach(s=>{
      let best=-1,bd=Infinity;
      for(let i=0;i<s.ts.length;i++){const v=s.vals[i];if(v===null||v===undefined)continue;
        const d=Math.abs(s.ts[i]-t);if(d<bd){bd=d;best=i;}}
      if(best<0)return;
      const gapMax=Math.max((s.bucket>0?s.bucket:600)*2.5, CONNECT_MIN_S);
      if(bd>gapMax)return; // point trop loin (vrai trou) : on ne l'invente pas
      const px=X(s.ts[best]),py=Ys(s,s.vals[best]);
      dots+='<circle cx="'+px.toFixed(1)+'" cy="'+py.toFixed(1)+'" r="3.6" fill="'+s.color+'" stroke="#0e1013" stroke-width="1.2"/>';
      rows+='<div class="tr"><span class="sw" style="background:'+s.color+(s.opacity!==undefined?';opacity:'+s.opacity:'')+'"></span>'+
        s.label+' : <b>'+s.vals[best].toFixed(s.dec)+' '+s.unit+'</b></div>';});
    hg.innerHTML='<line x1="'+sx.toFixed(1)+'" y1="'+G.pT+'" x2="'+sx.toFixed(1)+'" y2="'+(G.H-G.pB)+
      '" stroke="#ffffff" stroke-opacity="0.22" stroke-width="1"/>'+dots;
    if(!rows){tip.hidden=true;return;}
    tip.innerHTML='<div class="th">'+fmtFull(t)+'</div>'+rows;
    tip.hidden=false;
    const pr=plot.getBoundingClientRect();
    let left=ev.clientX-pr.left+14, top=ev.clientY-pr.top+14;
    if(left+tip.offsetWidth>pr.width) left=ev.clientX-pr.left-tip.offsetWidth-14;
    if(top+tip.offsetHeight>pr.height) top=pr.height-tip.offsetHeight-4;
    if(top<0)top=4;
    tip.style.left=left+"px";tip.style.top=top+"px";
  }
  function leave(){tip.hidden=true;hg.innerHTML="";}
  svg.addEventListener("mousemove",move);
  svg.addEventListener("mouseleave",leave);
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
    charts.innerHTML = (S.metric==="all") ? renderAll(data) : renderSingle(data[S.metric]);
    attachHover();
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
