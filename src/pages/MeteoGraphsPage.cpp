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
//   - Grandeurs à afficher : sélection LIBRE par cases à cocher (une seule, un
//     couple T+hum, hum+pression, les trois...). Une seule cochée -> vue mono
//     (IN/OUT en deux couleurs). Plusieurs -> superposées, chacune son axe et sa
//     couleur (la première sélectionnée à gauche, les autres à droite),
//     l'intérieur en trait plus fin et atténué.
//   - Échelles de valeurs à GAUCHE et à DROITE (dynamiques).
//   - Survol : ligne-guide + infobulle donnant, à l'instant pointé, la valeur de
//     chaque courbe.
//   - Période : glissante (6 h ... 30 j, fin = maintenant) ou LIBRE (jour + heure
//     de début et de fin, pas de 5 min) pour revenir consulter un moment passé.
//   - Qualité : les points écartés par MeteoQuality (pic isolé, hors bornes,
//     période annotée « sonde hors conditions ») ne font pas partie des courbes
//     ni des échelles ; ils sont montrés en croix grises, motif au survol, avec
//     un bilan sous le graphique. Rien n'est effacé : c'est la donnée brute.
//
// Page autonome (SVG côté navigateur, sans CDN), données via /meteohub/series
// et /meteohub/events (croisements, tendances, régimes : source commune).
// -----------------------------------------------------------------------------
QByteArray MeteoGraphsPage::render() {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<!--theme-head-->
<title>morfAnalytics - Graphiques météo</title><style>
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:1.5rem}
.wrap{max-width:72rem;margin:auto}h1{margin:.2rem 0}
/* En-tete + filtres collants : restent accessibles quel que soit le defilement,
   et changer un filtre ne renvoie plus en haut de la page. */
/* #tbh = support collant de hauteur FIGEE (celle de l'en-tete déployé) : l'en-tete
   peut se resserrer au defilement sans jamais changer la hauteur du document, donc
   sans decalage du contenu ni boucle defilement/mise en page (le clignotement). */
#tbh{position:sticky;top:0;z-index:10;margin:-1.5rem -1.5rem 0;pointer-events:none}
.topbar{pointer-events:auto;background:var(--bg);border-bottom:1px solid var(--line);
  padding:1rem 1.5rem .6rem}
.topbar .wrap{padding:0}
.muted{color:var(--muted)}a{color:var(--accent)}
.vb{font-size:.8rem;font-weight:600;vertical-align:middle;color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,transparent);border:1px solid color-mix(in srgb,var(--accent) 30%,transparent);border-radius:999px;padding:.1rem .5rem;margin-left:.4rem}
.tabs{margin:.3rem 0 1rem}.tabs .tab{display:inline-block;padding:.25rem .7rem;border:1px solid var(--line);border-radius:999px;margin-right:.4rem;font-size:.9rem;color:var(--soft);text-decoration:none}
.tabs .tab.on{background:var(--sel);border-color:var(--accent);color:var(--sel-ink)}
.controls{display:flex;flex-wrap:wrap;gap:.5rem 1.2rem;align-items:center;margin:.6rem 0}
/* Defilement : l'en-tete collant se resserre (description masquee, marges reduites)
   pour laisser la place aux graphes. */
.topbar.compact{padding-top:.4rem;padding-bottom:.3rem}
.topbar.compact .desc{display:none}
.topbar.compact h1{font-size:1.15rem;margin:0}
.topbar.compact .tabs{margin:.2rem 0 .3rem}
.topbar.compact .controls{margin:.3rem 0 0}
label{font-size:.9rem;color:var(--soft)}
select{background:var(--field);border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.3rem .5rem;font-size:.9rem}
.metricsel{display:inline-flex;align-items:center;gap:.5rem;flex-wrap:wrap}
.metricsel .mslabel{font-size:.9rem;color:var(--soft)}
.metricsel .mk{display:inline-flex;align-items:center;gap:.3rem;background:var(--field);border:1px solid var(--line);border-radius:8px;padding:.25rem .55rem;font-size:.85rem;color:var(--soft);cursor:pointer}
.metricsel .mk:hover{border-color:var(--accent);color:var(--ink)}
.metricsel .mk input{accent-color:var(--accent);margin:0}
.metricsel .mk.on{background:var(--sel);border-color:var(--accent);color:var(--sel-ink)}
.periods{display:flex;gap:.3rem;flex-wrap:wrap}
.pbtn{background:var(--field);border:1px solid var(--line);color:var(--soft);border-radius:8px;padding:.3rem .7rem;cursor:pointer;font-size:.85rem}
.pbtn:hover{border-color:var(--accent);color:var(--ink)}.pbtn.on{background:var(--sel);border-color:var(--accent);color:var(--sel-ink)}
/* Période libre : deux champs jour + heure (pas de 5 min), repliés tant que
   l'on reste sur les périodes glissantes. */
.custom{display:flex;flex-wrap:wrap;gap:.5rem .8rem;align-items:center}
.custom[hidden]{display:none}
/* Les deux champs de la période libre disent déjà la période : pas de deuxième ligne. */
#custom:not([hidden])~.rangelbl{display:none}
.custom input{background:var(--field);border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.25rem .45rem;font-size:.85rem}
.custom .err{color:var(--warn);font-size:.82rem}
.rangelbl{color:var(--muted);font-size:.85rem;font-variant-numeric:tabular-nums}
.qual{color:var(--muted);font-size:.82rem;margin-top:.35rem}
.qual b{color:var(--soft);font-weight:600}
.chart{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:.9rem 1.1rem;margin:1rem 0}
.chart h3{margin:0 0 .1rem;font-size:1.05rem}
.plot{position:relative;margin-top:.5rem}
.plot svg{display:block;width:100%;height:auto}
.ax{fill:var(--muted);font-size:11px}.grid-l{stroke:var(--line);stroke-width:1}
.legend{display:flex;gap:1.1rem;flex-wrap:wrap;margin-top:.5rem;font-size:.85rem;color:var(--soft)}
.legend .k{display:inline-flex;align-items:center;gap:.4rem}
.legend .sw{width:1.4rem;height:0;border-top:3px solid;display:inline-block}
.note{color:var(--muted);font-size:.82rem;margin-top:.4rem}
.tip{position:absolute;pointer-events:none;background:var(--tip);border:1px solid var(--line);border-radius:8px;padding:.4rem .55rem;font-size:.82rem;color:var(--ink);box-shadow:0 4px 14px rgba(0,0,0,.45);z-index:5;white-space:nowrap}
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
</style></head><body>
<div id="tbh"><div class="topbar"><div class="wrap">
<!--nav-back-->
<h1>Météo <span id="vb" class="vb"></span><!--theme-toggle--></h1>
<div class="tabs"><a class="tab" href="/meteohub">Analyses</a><span class="tab on">Graphiques</span></div>
<p class="muted desc">Montre ce que font réellement les données dans le temps. Les analyses, elles, disent ce que ça signifie.</p>
<div class="controls">
  <span class="metricsel"><span class="mslabel">Afficher</span><span id="metricsel"></span></span>
  <span class="periods" id="layoutsel" title="Plusieurs grandeurs : un graphe chacune, ou toutes sur le même"></span>
  <label>Source&nbsp;<select id="source"></select></label>
  <label class="mk" title="Points écartés des analyses (pic isolé, hors bornes, démarrage à froid, sonde hors conditions)"><input type="checkbox" id="showsusp"> Points écartés</label>
  <div class="periods" id="periods"></div>
  <div class="custom" id="custom" hidden>
    <label>Du&nbsp;<input type="datetime-local" id="cfrom" step="300"></label>
    <label>au&nbsp;<input type="datetime-local" id="cto" step="300"></label>
    <button class="pbtn" id="capply" type="button">Afficher</button>
    <button class="pbtn" id="cnow" type="button" title="Termine la période à l'instant présent">Jusqu'à maintenant</button>
    <span class="err" id="cerr"></span>
  </div>
  <span class="rangelbl" id="rangelbl"></span>
  <span class="periods">
    <button class="pbtn" id="expview" type="button" title="CSV de la période et de la source affichées : valeurs brutes, état de chaque point (valide, écarté auto/manuel, réintégré) et motif">Exporter la période (CSV)</button>
    <button class="pbtn" id="expall" type="button" title="CSV de tout l'historique du cache, intérieur et extérieur">Export complet (CSV)</button>
  </span>
</div>
</div></div></div>
<div class="wrap">
<div id="charts"><p class="muted">Chargement&hellip;</p></div>
</div>
<script>
"use strict";
const LS="morfanalytics.graphs.";
// [clé, libellé, unité, décimales, couleur (mode "Toutes")]
// La détection des événements (croisements, tendances, régimes) et sa bande morte
// vivent côté serveur (MeteoEvents) : source commune avec la page Analyse.
const METRICS=[
  ["temp","Température","°C",1,mfaColor("--s-temp")],
  ["hum","Humidité","%",1,mfaColor("--s-hum")],
  ["pres","Pression","hPa",1,mfaColor("--s-pres")]
];
const METRIC_KEYS=METRICS.map(m=>m[0]);
const SOURCES=[["out","Extérieur"],["in","Intérieur"],["both","Intérieur + Extérieur"]];
const PERIODS=[["6 h",6],["12 h",12],["24 h",24],["3 j",72],["7 j",168],["30 j",720]];
// Période libre : bornes jour + heure alignées sur 5 min (la cadence de la sonde),
// au plus un an (même plafond que le serveur).
const STEP_S=300;
const MAX_SPAN_S=366*86400;
// Couleur = grandeur, quel que soit le filtre (orange Temp, vert Hum, violet Pres) ;
// IN et OUT se distinguent par l'épaisseur et l'opacité (jamais de pointillés).
// Cadence ~5 min (10 au boot) : plancher de connexion des points.
const CONNECT_MIN_S=20*60;
const AXIS_MUTED=mfaColor("--muted");

// Grandeurs affichées : sélection LIBRE (cases à cocher). On peut afficher n'importe
// quel sous-ensemble (une seule, un couple T+hum, hum+pression, les trois...).
// Migration depuis l'ancien menu déroulant mono : "all" -> les trois, sinon la seule.
function loadMetrics(){
  const raw=localStorage.getItem(LS+"metrics");
  if(raw){ try{const a=JSON.parse(raw);
    if(Array.isArray(a)){const f=METRIC_KEYS.filter(k=>a.indexOf(k)>=0); if(f.length)return f;}
  }catch(e){} }
  const old=localStorage.getItem(LS+"metric");
  if(old==="all") return METRIC_KEYS.slice();
  if(old&&METRIC_KEYS.indexOf(old)>=0) return [old];
  return ["temp"];
}
// Période libre mémorisée (une consultation passée reste affichée au rechargement).
function loadRange(){
  try{const r=JSON.parse(localStorage.getItem(LS+"range")||"null");
    if(r&&r.from>0&&r.to-r.from>=300)return {from:+r.from,to:+r.to};}catch(e){}
  return null;
}
let S={
  metrics:loadMetrics(),  // sous-ensemble de METRIC_KEYS, dans l'ordre de METRICS
  source:localStorage.getItem(LS+"source")||"out",
  hours:+(localStorage.getItem(LS+"hours")||24),
  range:loadRange(),   // null = période glissante ; sinon {from,to} en secondes epoch
  showSuspects:localStorage.getItem(LS+"susp")!=="0",  // croix grises (défaut : visibles)
  layout:localStorage.getItem(LS+"layout")==="overlay"?"overlay":"stack"  // plusieurs grandeurs : empilées ou superposées
};
// Motifs de qualification (MeteoQuality) -> libellés.
const QUAL_LABEL={pic:"pic isolé",bornes:"hors bornes",exclusion:"sonde hors conditions",demarrage:"démarrage à froid"};
let GS=[]; // géométrie + séries de chaque graphe affiché (un par grandeur en vue empilée), pour le survol

const $=s=>document.querySelector(s);
function metricDef(k){return METRICS.find(m=>m[0]===k)||METRICS[0];}
function srcLabel(k){return (SOURCES.find(s=>s[0]===k)||["","",""])[1];}
function fmtClock(ts){const d=new Date(ts*1000);const sameDay=(new Date()*1-d)<86400000;
  return d.toLocaleString("fr-FR",sameDay?{hour:"2-digit",minute:"2-digit"}:{day:"2-digit",month:"2-digit",hour:"2-digit",minute:"2-digit"});}
function fmtFull(ts){return new Date(ts*1000).toLocaleString("fr-FR",{day:"2-digit",month:"2-digit",hour:"2-digit",minute:"2-digit"});}
function minMax(vals){let mn=Infinity,mx=-Infinity;for(const v of vals){if(v!==null&&v!==undefined){if(v<mn)mn=v;if(v>mx)mx=v;}}return [mn,mx];}
function nf(v,d){return (v===null||!isFinite(v))?"-":v.toFixed(d);}
function fmtNum(v,d){return v.toLocaleString("fr-FR",{minimumFractionDigits:d,maximumFractionDigits:d});}

// --- Événements temporels (source commune : endpoint /meteohub/events) --------
// Le calcul (croisements IN/OUT, changements de tendance, changements de régime)
// vit en C++ (MeteoEvents) et est partagé avec la page Analyse : la page
// Graphiques ne recalcule rien, elle consomme les événements et les matérialise.
//   - Croisement : deux séries de MÊME grandeur (Int/Ext) deviennent égales ;
//     instant et valeur interpolés entre les mesures encadrantes (estimation).
//   - Changement de régime : plusieurs tendances basculent dans une fenêtre
//     rapprochée, même entre grandeurs différentes (relation TEMPORELLE, jamais
//     déduite d'une proximité graphique).
const REGIME_COL=mfaColor("--s-regime");
let EV={crossings:[],trend_changes:[],regime_changes:[]}; // dernier lot d'événements
// Paramètres de fenêtre communs aux deux endpoints : bornes explicites en période
// libre, sinon durée glissante (le serveur ancre alors la fin sur « maintenant »).
function windowQuery(){
  return S.range ? "from="+S.range.from+"&to="+S.range.to : "hours="+S.hours;
}
function metricColor(k){return metricDef(k)[4];}
function fetchEvents(){
  return fetch("/meteohub/events?"+windowQuery())
    .then(r=>r.json()).catch(()=>({crossings:[],trend_changes:[],regime_changes:[]}));
}

// Fusionne croisements (des grandeurs affichées) et changements de régime en une
// timeline chronologique numérotée, partagée par les marqueurs et l'encart.
// `showCross` : les deux courbes d'une grandeur sont tracées (source « both »).
// `showRegime` : l'extérieur est visible (les régimes portent sur la météo).
function assembleEvents(metricsShown, showCross, showRegime){
  const list=[];
  if(showCross) (EV.crossings||[]).forEach(c=>{ if(metricsShown.indexOf(c.metric)>=0)
    list.push({kind:"cross",ts:c.ts,metric:c.metric,value:c.value,unit:c.unit,dec:c.dec,
               metricName:c.metric_name,outRising:c.out_rising}); });
  if(showRegime) (EV.regime_changes||[]).forEach(r=>list.push({kind:"regime",ts:r.ts,parts:r.parts||[]}));
  // Variation brutale locale : constat de mesure (T et HR en sens opposés, pression
  // stable), cause volontairement non tranchée. Même visibilité que les régimes.
  if(showRegime) (EV.sudden_changes||[]).forEach(s=>list.push({kind:"sudden",ts:s.ts,start:s.start_ts,
    dT:s.d_temp,dH:s.d_hum,dP:s.d_pres,dD:s.d_dew,causes:s.causes||[]}));
  list.sort((a,b)=>a.ts-b.ts);
  list.forEach((e,i)=>{e.n=i+1;e.color=e.kind==="cross"?metricColor(e.metric):e.kind==="sudden"?mfaColor("--bad"):REGIME_COL;});
  return list;
}

// Encart « Événements détectés » listé chronologiquement sous le graphique.
function eventsBox(list){
  const intro="Les croisements comparent deux séries de même grandeur physique (Intérieur et "+
    "Extérieur) : leur heure et leur valeur sont interpolées entre les mesures qui encadrent "+
    "l'égalité (une estimation, pas une mesure). Les changements de régime signalent le "+
    "basculement rapproché de plusieurs tendances, même entre grandeurs différentes. Une variation "+
    "brutale locale est un constat (température et humidité partent en sens opposés en quelques "+
    "minutes, pression stable) : sa cause n'est pas tranchée.";
  if(!list.length){
    return '<div class="cross"><h4>Événements détectés</h4><p class="cintro">'+intro+'</p>'+
      '<p class="muted">Aucun événement sur la période affichée.</p></div>';
  }
  const rows=list.map(e=>{
    const cn='<span class="cn" style="color:'+e.color+'">'+e.n+'</span>';
    if(e.kind==="cross"){
      return '<li>'+cn+'<div class="cev"><div class="ct">'+fmtClock(e.ts)+' - Croisement de '+e.metricName+'</div>'+
        '<div class="cd">Extérieur rejoint Intérieur à '+fmtNum(e.value,e.dec)+' '+e.unit+'.</div></div></li>';
    }
    if(e.kind==="sudden"){
      const sg=(v,d)=>(v>0?"+":"")+fmtNum(v,d);
      return '<li>'+cn+'<div class="cev"><div class="ct">'+fmtClock(e.ts)+' - Variation brutale locale</div>'+
        '<div class="cd">En '+Math.max(1,Math.round((e.ts-e.start)/60))+' min : température '+sg(e.dT,1)+' °C, humidité '+
        sg(e.dH,1)+' %, pression '+sg(e.dP,1)+' hPa, point de rosée '+sg(e.dD,1)+' °C.<br>'+
        '<span class="muted">Cause indéterminée : '+(e.causes||[]).join(", ")+'.</span></div></div></li>';
    }
    const lines=(e.parts||[]).map(p=>p.metric_name+' : '+p.from+' → '+p.to+'.').join('<br>');
    return '<li>'+cn+'<div class="cev"><div class="ct">'+fmtClock(e.ts)+' - Changement de régime</div>'+
      '<div class="cd">'+lines+'</div></div></li>';
  }).join("");
  return '<div class="cross"><h4>Événements détectés</h4><p class="cintro">'+intro+
    '</p><ul class="clist">'+rows+'</ul></div>';
}

// Graphe SVG. `series` = [{ts,vals,color,width,opacity,bucket,smin,smax,label,unit,dec}].
// `axes` = [{min,max,dec,side:'L'|'R',col}] : échelles de valeurs affichées à
// gauche/droite (dynamiques). Chaque série est tracée avec SA propre [smin,smax].
// `events` (optionnel) = timeline numérotée [{kind,ts,n,color,metric,value,parts}].
// `scaleByMetric` = {metric:{smin,smax}} pour placer un croisement à SA hauteur.
// `idx` = rang du graphe (vue empilée : plusieurs graphes) ; `tr` = [t0,t1] imposé
// pour que tous les graphes empilés partagent le même axe de temps (null : plage
// propre aux séries). Un graphe empilé est plus bas.
function buildChart(series, axes, events, scaleByMetric, idx, tr){
  idx=idx||0;
  const W=760,H=tr?170:240,pT=12,pB=24;
  const lefts=axes.filter(a=>a.side==='L'), rights=axes.filter(a=>a.side==='R');
  const pL = lefts.length ? 48 : 16;
  const pR = 14 + rights.length*46;
  let t0=Infinity,t1=-Infinity,any=false;
  series.forEach(s=>{for(let i=0;i<s.vals.length;i++){const v=s.vals[i];
    if(v!==null&&v!==undefined){any=true;const t=s.ts[i];if(t<t0)t0=t;if(t>t1)t1=t;}}});
  if(tr){t0=tr[0];t1=tr[1];}
  if(!any){GS[idx]=null;return '<div class="muted">Pas encore de mesure sur cette période.</div>';}
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

  // Points écartés par la qualification : croix grises, à leur valeur d'origine
  // (ramenée dans le cadre si elle sort de l'échelle, qui ne les prend pas en
  // compte). Motif au survol (infobulle native du SVG).
  let susp="";
  if(S.showSuspects){
    series.forEach(s=>{(s.suspects||[]).forEach(p=>{
      const t=p[0],v=p[1],why=QUAL_LABEL[p[2]]||p[2];
      if(t<t0||t>t1)return;
      const x=X(t);let y=Ys(s,v);
      y=Math.max(pT+3,Math.min(H-pB-3,y));
      susp+='<g stroke="'+mfaColor("--s-susp")+'" stroke-width="1.6"><title>'+fmtFull(t)+' · '+s.label+' '+
        v.toFixed(s.dec)+' '+s.unit+' écarté : '+why+'</title>'+
        '<line x1="'+(x-3.5).toFixed(1)+'" y1="'+(y-3.5).toFixed(1)+'" x2="'+(x+3.5).toFixed(1)+'" y2="'+(y+3.5).toFixed(1)+'"/>'+
        '<line x1="'+(x-3.5).toFixed(1)+'" y1="'+(y+3.5).toFixed(1)+'" x2="'+(x+3.5).toFixed(1)+'" y2="'+(y-3.5).toFixed(1)+'"/>'+
        '<rect x="'+(x-5).toFixed(1)+'" y="'+(y-5).toFixed(1)+'" width="10" height="10" fill="transparent" stroke="none"/></g>';
    });});
  }

  // Marqueurs d'événements : croisement = guide vertical + losange à la valeur
  // estimée ; changement de régime = ligne pleine hauteur en pointillés. Le numéro
  // relie chaque marqueur à sa ligne dans l'encart « Événements détectés ».
  let marks="";
  (events||[]).forEach(e=>{
    if(e.ts<t0||e.ts>t1)return;
    const x=X(e.ts);
    if(e.kind==="cross"){
      const sc=(scaleByMetric||{})[e.metric];
      marks+='<line x1="'+x.toFixed(1)+'" y1="'+pT+'" x2="'+x.toFixed(1)+'" y2="'+(H-pB)+
        '" stroke="'+e.color+'" stroke-opacity="0.45" stroke-dasharray="3 3" stroke-width="1"/>';
      if(sc){
        const yv=pT+(H-pT-pB)*(1-(e.value-sc.smin)/Math.max(1e-9,sc.smax-sc.smin));
        const r=4.5;
        marks+='<path d="M'+x.toFixed(1)+' '+(yv-r).toFixed(1)+' L'+(x+r).toFixed(1)+' '+yv.toFixed(1)+
          ' L'+x.toFixed(1)+' '+(yv+r).toFixed(1)+' L'+(x-r).toFixed(1)+' '+yv.toFixed(1)+
          ' Z" fill="'+e.color+'" stroke="'+mfaColor("--tip")+'" stroke-width="1.2"/>';
        marks+='<text class="cnum" x="'+(x+6).toFixed(1)+'" y="'+(yv-6).toFixed(1)+'" fill="'+e.color+'">'+e.n+'</text>';
      } else {
        marks+='<text class="cnum" x="'+(x+4).toFixed(1)+'" y="'+(pT+11)+'" fill="'+e.color+'">'+e.n+'</text>';
      }
    } else if(e.kind==="sudden"){ // variation brutale : bande sur la fenêtre mesurée
      const xs=X(Math.max(t0,e.start));
      marks+='<rect x="'+xs.toFixed(1)+'" y="'+pT+'" width="'+Math.max(3,x-xs).toFixed(1)+'" height="'+(H-pT-pB)+
        '" fill="'+e.color+'" fill-opacity="0.16"/>';
      marks+='<text class="cnum" x="'+(x+4).toFixed(1)+'" y="'+(pT+11)+'" fill="'+e.color+'">'+e.n+'</text>';
    } else { // changement de régime : repère temporel pleine hauteur
      marks+='<line x1="'+x.toFixed(1)+'" y1="'+pT+'" x2="'+x.toFixed(1)+'" y2="'+(H-pB)+
        '" stroke="'+e.color+'" stroke-opacity="0.6" stroke-dasharray="1 3" stroke-width="1.4"/>';
      marks+='<text class="cnum" x="'+(x+4).toFixed(1)+'" y="'+(pT+11)+'" fill="'+e.color+'">'+e.n+'</text>';
    }
  });

  // Contexte de survol : tout ce qu'il faut pour retrouver un point depuis la souris.
  GS[idx]={W,H,pL,pR,pT,pB,t0,t1,series};

  return '<svg id="gsvg'+idx+'" viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none" role="img">'+
    grid+labels+paths+susp+marks+'<g id="hoverg'+idx+'"></g>'+xt0+xt1+'</svg>';
}

function fetchSeries(ctx, metric){
  return fetch("/meteohub/series?ctx="+ctx+"&metric="+metric+"&"+windowQuery())
    .then(r=>r.json()).catch(()=>({ts:[],v:[],bucket_s:0}));
}

// Un graphe d'UNE grandeur : IN/OUT en deux couleurs, échelle numérique à gauche ET
// à droite. `events` = timeline déjà assemblée (les croisements d'autres grandeurs
// en sont exclus par l'appelant). `tr`/`withNote` : voir renderStack.
function singleChart(byCtx, key, events, idx, tr, withNote){
  const md=metricDef(key);
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];
  let allv=[]; ctxs.forEach(c=>{allv=allv.concat((byCtx[c].v||[]).filter(x=>x!==null&&x!==undefined));});
  let [mn,mx]=minMax(allv); if(!isFinite(mn)){mn=0;mx=1;}
  let pad=(mx-mn)*0.08; if(pad<(md[3]?0.5:1))pad=(md[3]?0.5:1); const smin=mn-pad, smax=mx+pad;
  const series=ctxs.map(c=>({ts:byCtx[c].ts||[],vals:byCtx[c].v||[],color:md[4],width:c==="in"?1.4:2.2,opacity:c==="in"?0.55:1,
    bucket:byCtx[c].bucket_s||0,smin,smax,label:srcLabel(c),unit:md[2],dec:md[3],
    suspects:byCtx[c].suspects||[]}));
  const axes=[{min:smin,max:smax,dec:md[3],side:'L',col:AXIS_MUTED},
              {min:smin,max:smax,dec:md[3],side:'R',col:AXIS_MUTED}];
  const legend=ctxs.map(c=>'<span class="k"><span class="sw" style="border-color:'+md[4]+';opacity:'+(c==="in"?0.55:1)+';border-top-width:'+(c==="in"?2:3)+'px"></span>'+srcLabel(c)+'</span>').join("");
  const scaleByMetric={}; scaleByMetric[key]={smin,smax};
  return '<div class="chart"><h3>'+md[1]+" ("+md[2]+")"+'</h3><div class="plot">'+buildChart(series,axes,events,scaleByMetric,idx,tr)+
    '<div class="tip" hidden></div></div><div class="legend">'+legend+'</div>'+qualityLine(series)+(withNote?noteFor(false):"")+'</div>';
}

// Croisements : seulement si Int ET Ext tracés. Régimes et variations brutales :
// dès que l'Ext est visible.
function eventFlags(){
  return {cross:S.source==="both", regime:(S.source==="out"||S.source==="both")};
}

// Vue MONO-GRANDEUR.
function renderSingle(byCtx, key){
  const f=eventFlags();
  const events = assembleEvents([key], f.cross, f.regime);
  const box = (f.cross||f.regime) ? eventsBox(events) : "";
  return singleChart(byCtx,key,events,0,null,true)+box;
}

// Vue EMPILÉE : un graphe par grandeur, même axe de temps, survol synchronisé.
// Chaque grandeur garde sa propre échelle, sans que l'une écrase les autres ; les
// événements sont listés une seule fois, sous le dernier graphe.
function renderStack(data, metrics){
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];
  let t0=Infinity,t1=-Infinity;
  metrics.forEach(k=>ctxs.forEach(c=>{const d=data[k][c]||{};(d.ts||[]).forEach((t,i)=>{
    const v=(d.v||[])[i]; if(v!==null&&v!==undefined){if(t<t0)t0=t;if(t>t1)t1=t;}});}));
  const tr = isFinite(t0) ? [t0, t1>t0?t1:t0+1] : [0,1];
  const f=eventFlags();
  const events = assembleEvents(metrics, f.cross, f.regime);
  const charts = metrics.map((k,i)=>singleChart(data[k],k,
    events.filter(e=>e.kind!=="cross"||e.metric===k), i, tr, i===metrics.length-1)).join("");
  return charts+((f.cross||f.regime)?eventsBox(events):"");
}

// Vue MULTI-GRANDEURS : un seul graphe, les grandeurs SÉLECTIONNÉES superposées
// (2 courbes par grandeur avec IN+OUT). Chaque grandeur a son axe : la première
// sélectionnée à gauche, les suivantes à droite.
function renderAll(data, metrics){
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];
  let series=[], axes=[], legend=[], shownKeys=[], drawn=0; const scaleByMetric={};
  metrics.forEach((key)=>{
    const m=metricDef(key), col=m[4];
    let allv=[]; ctxs.forEach(c=>{allv=allv.concat((data[key][c].v||[]).filter(x=>x!==null&&x!==undefined));});
    let [mn,mx]=minMax(allv); if(!isFinite(mn))return;
    let pad=(mx-mn)*0.08; if(pad<(m[3]?0.5:1))pad=(m[3]?0.5:1); const smin=mn-pad, smax=mx+pad;
    axes.push({min:smin,max:smax,dec:m[3],side: drawn===0?'L':'R',col:col}); drawn++;
    scaleByMetric[key]={smin,smax}; shownKeys.push(key);
    ctxs.forEach(c=>{const inner=(c==="in");
      series.push({ts:data[key][c].ts||[],vals:data[key][c].v||[],color:col,
        width:inner?1.4:2.2,opacity:inner?0.55:1,bucket:data[key][c].bucket_s||0,smin,smax,
        label:m[1]+(S.source==="both"?(inner?" (int)":" (ext)"):""),unit:m[2],dec:m[3],
        suspects:data[key][c].suspects||[]});});
    const range=nf(mn,m[3])+"–"+nf(mx,m[3])+" "+m[2];
    ctxs.forEach(c=>{const inner=(c==="in");
      legend.push('<span class="k"><span class="sw" style="border-color:'+col+';opacity:'+(inner?0.55:1)+';border-top-width:'+(inner?2:3)+'px"></span>'+
        m[1]+(S.source==="both"?" "+(inner?"(int)":"(ext)"):"")+(c===ctxs[ctxs.length-1]?' · '+range:'')+'</span>');});
  });
  const {cross:showCross, regime:showRegime} = eventFlags();
  const events = assembleEvents(shownKeys, showCross, showRegime);
  const names = metrics.map(k=>metricDef(k)[1]).join(" + ");
  const title = names+(S.source==="both" ? " (Intérieur + Extérieur)" : " ("+srcLabel(S.source)+")");
  const body = series.length ? buildChart(series,axes,events,scaleByMetric,0,null) : '<div class="muted">Pas encore de mesure sur cette période.</div>';
  const box = (series.length && (showCross||showRegime)) ? eventsBox(events) : "";
  return '<div class="chart"><h3>'+title+'</h3><div class="plot">'+body+'<div class="tip" hidden></div></div>'+
    '<div class="legend">'+legend.join("")+'</div>'+qualityLine(series)+noteFor(true)+'</div>'+box;
}

// Bilan qualité de la période : points écartés par motif (toutes courbes).
function qualityLine(series){
  const n={pic:0,bornes:0,exclusion:0};let total=0;
  series.forEach(s=>(s.suspects||[]).forEach(p=>{total++;n[p[2]]=(n[p[2]]||0)+1;}));
  if(!total) return '<p class="qual">Qualité : aucun point écarté sur la période.</p>';
  const parts=Object.keys(n).filter(k=>n[k]).map(k=>n[k]+' '+(QUAL_LABEL[k]||k));
  return '<p class="qual">Qualité : <b>'+total+' point'+(total>1?'s':'')+' écarté'+(total>1?'s':'')+
    '</b> des courbes et des analyses ('+parts.join(', ')+'). Données brutes conservées'+
    (S.showSuspects?' ; croix grises sur le graphique.':'.')+'</p>';
}

function noteFor(multi){
  if(multi){
    return '<p class="note">'+(S.source==="both"
      ? "Plusieurs grandeurs sur un axe de temps commun ; chacune a sa propre échelle (la première sélectionnée à gauche, les autres à droite). Intérieur en trait plus fin et atténué. Survolez pour lire les valeurs."
      : "Plusieurs grandeurs sur un axe de temps commun, chacune à son échelle. Survolez pour lire les valeurs.")+'</p>';
  }
  return '<p class="note">'+(S.source==="both"
    ? "Intérieur en trait plus fin et atténué ; échelle à gauche et à droite. Pour les chiffres d'inertie, voir « Comportement thermique » et « Réactivité du bâtiment » dans les <a href=\"/meteohub\">analyses</a>."
    : "Échelle à gauche et à droite. Points reliés tant que l'écart reste proche de la cadence ; coupé seulement sur un vrai silence du capteur.")+'</p>';
}

// --- Survol : ligne-guide + infobulle des valeurs à l'instant pointé ----------
// Vue empilée : tous les graphes partagent l'axe de temps, donc le curseur d'un
// graphe est reporté sur les autres (ligne-guide et infobulle de chacun).
function hoverAt(i,t,pos){
  const g=GS[i], svg=$("#gsvg"+i), hg=$("#hoverg"+i); if(!g||!svg||!hg)return;
  const plot=svg.closest(".plot"), tip=plot.querySelector(".tip");
  if(t===null){tip.hidden=true;hg.innerHTML="";return;}
  const X=tt=>g.pL+(g.W-g.pL-g.pR)*((tt-g.t0)/Math.max(1,(g.t1-g.t0)));
  const Ys=(s,v)=>g.pT+(g.H-g.pT-g.pB)*(1-(v-s.smin)/Math.max(1e-9,s.smax-s.smin));
  let dots="",rows="";
  g.series.forEach(s=>{
    let best=-1,bd=Infinity;
    for(let k=0;k<s.ts.length;k++){const v=s.vals[k];if(v===null||v===undefined)continue;
      const d=Math.abs(s.ts[k]-t);if(d<bd){bd=d;best=k;}}
    if(best<0)return;
    const gapMax=Math.max((s.bucket>0?s.bucket:600)*2.5, CONNECT_MIN_S);
    if(bd>gapMax)return; // point trop loin (vrai trou) : on ne l'invente pas
    const px=X(s.ts[best]),py=Ys(s,s.vals[best]);
    dots+='<circle cx="'+px.toFixed(1)+'" cy="'+py.toFixed(1)+'" r="3.6" fill="'+s.color+'" stroke="'+mfaColor("--tip")+'" stroke-width="1.2"/>';
    rows+='<div class="tr"><span class="sw" style="background:'+s.color+(s.opacity!==undefined?';opacity:'+s.opacity:'')+'"></span>'+
      s.label+' : <b>'+s.vals[best].toFixed(s.dec)+' '+s.unit+'</b></div>';});
  const sx=X(t);
  hg.innerHTML='<line x1="'+sx.toFixed(1)+'" y1="'+g.pT+'" x2="'+sx.toFixed(1)+'" y2="'+(g.H-g.pB)+
    '" stroke="'+mfaColor("--ink")+'" stroke-opacity="0.22" stroke-width="1"/>'+dots;
  if(!rows){tip.hidden=true;return;}
  tip.innerHTML='<div class="th">'+fmtFull(t)+'</div>'+rows;
  tip.hidden=false;
  const pr=plot.getBoundingClientRect();
  // Graphe survolé : l'infobulle suit la souris ; les autres se calent sur la ligne-guide.
  let left=pos?pos.x-pr.left+14:sx/g.W*pr.width+14, top=pos?pos.y-pr.top+14:4;
  if(left+tip.offsetWidth>pr.width) left=left-tip.offsetWidth-28;
  if(top+tip.offsetHeight>pr.height) top=pr.height-tip.offsetHeight-4;
  if(top<0)top=4;
  tip.style.left=left+"px";tip.style.top=top+"px";
}
function attachHover(){
  GS.forEach((g,i)=>{
    const svg=$("#gsvg"+i); if(!g||!svg)return;
    const all=(t,pos)=>GS.forEach((_,k)=>hoverAt(k,t,k===i?pos:null));
    svg.addEventListener("mousemove",ev=>{
      const r=svg.getBoundingClientRect();
      const sx=(ev.clientX-r.left)/r.width*g.W;
      if(sx<g.pL||sx>g.W-g.pR){all(null);return;}
      all(g.t0+(sx-g.pL)/Math.max(1,(g.W-g.pL-g.pR))*(g.t1-g.t0),{x:ev.clientX,y:ev.clientY});
    });
    svg.addEventListener("mouseleave",()=>all(null));
  });
}

function draw(){
  const charts=$("#charts");
  const metrics = METRIC_KEYS.filter(k=>S.metrics.indexOf(k)>=0); // ordre METRICS stable
  if(!metrics.length){
    charts.innerHTML='<p class="muted">Cochez au moins une grandeur à afficher.</p>';
    return;
  }
  charts.innerHTML='<p class="muted">Chargement&hellip;</p>';
  const ctxs = S.source==="both" ? ["out","in"] : [S.source];

  const jobs=[];
  metrics.forEach(m=>ctxs.forEach(c=>jobs.push(fetchSeries(c,m).then(r=>({m,c,r})))));
  // Les événements (source commune) sont demandés en parallèle des séries.
  Promise.all([Promise.all(jobs), fetchEvents()]).then(([list,ev])=>{
    EV=ev||{crossings:[],trend_changes:[],regime_changes:[]};
    const data={};
    list.forEach(x=>{(data[x.m]=data[x.m]||{})[x.c]=x.r;});
    metrics.forEach(m=>{const d=data[m]=data[m]||{};["out","in"].forEach(c=>{if(!d[c])d[c]={ts:[],v:[],bucket_s:0,suspects:[]};});});
    GS=[];
    charts.innerHTML = (metrics.length===1) ? renderSingle(data[metrics[0]], metrics[0])
      : (S.layout==="overlay" ? renderAll(data, metrics) : renderStack(data, metrics));
    attachHover();
  });
}

// Cases à cocher des grandeurs (sélection libre : une, un couple, les trois...).
function renderMetricSel(){
  $("#metricsel").innerHTML=METRICS.map(m=>{
    const on=S.metrics.indexOf(m[0])>=0;
    return '<label class="mk'+(on?" on":"")+'"><input type="checkbox" data-m="'+m[0]+'"'+(on?" checked":"")+'>'+m[1]+'</label>';
  }).join("");
}
renderMetricSel();
$("#layoutsel").innerHTML=[["stack","Empilés"],["overlay","Superposés"]].map(l=>
  '<button class="pbtn'+(S.layout===l[0]?" on":"")+'" data-l="'+l[0]+'">'+l[1]+'</button>').join("");
$("#layoutsel").addEventListener("click",e=>{const b=e.target.closest(".pbtn");if(!b)return;
  S.layout=b.dataset.l;localStorage.setItem(LS+"layout",S.layout);
  document.querySelectorAll("#layoutsel .pbtn").forEach(x=>x.classList.toggle("on",x===b));draw();});
$("#source").innerHTML=SOURCES.map(s=>'<option value="'+s[0]+'"'+(s[0]===S.source?" selected":"")+'>'+s[1]+'</option>').join("");
$("#periods").innerHTML=PERIODS.map(p=>'<button class="pbtn" data-h="'+p[1]+'">'+p[0]+'</button>').join("")+
  '<button class="pbtn" data-h="custom">Période libre</button>';

// --- Période libre (jour + heure, pas de 5 min) ------------------------------
// Les champs datetime-local travaillent en heure LOCALE sans fuseau
// ("2026-09-24T14:35") : conversion explicite dans les deux sens, jamais via
// toISOString() qui repasserait en UTC et décalerait l'affichage.
function floor5(ts){return Math.floor(ts/STEP_S)*STEP_S;}
function toLocalInput(ts){const d=new Date(ts*1000);const p=n=>String(n).padStart(2,"0");
  return d.getFullYear()+"-"+p(d.getMonth()+1)+"-"+p(d.getDate())+"T"+p(d.getHours())+":"+p(d.getMinutes());}
function fromLocalInput(v){if(!v)return NaN;const d=new Date(v);return isNaN(d)?NaN:Math.floor(d.getTime()/1000);}
function fmtRange(r){const o={weekday:"short",day:"2-digit",month:"2-digit",year:"numeric",hour:"2-digit",minute:"2-digit"};
  return new Date(r.from*1000).toLocaleString("fr-FR",o)+" → "+new Date(r.to*1000).toLocaleString("fr-FR",o);}

// Pré-remplit les champs : la période libre en cours, sinon la fenêtre glissante
// affichée (on part de ce que l'on voit pour l'ajuster).
function fillCustom(){
  const now=floor5(Date.now()/1000)+STEP_S;
  const r=S.range||{from:floor5(now-S.hours*3600),to:now};
  $("#cfrom").value=toLocalInput(r.from); $("#cto").value=toLocalInput(r.to);
  $("#cfrom").max=$("#cto").max=toLocalInput(now);
  $("#cerr").textContent="";
}
function syncPeriodUI(){
  document.querySelectorAll("#periods .pbtn").forEach(x=>x.classList.toggle("on",
    S.range ? x.dataset.h==="custom" : +x.dataset.h===S.hours));
  $("#rangelbl").textContent = S.range ? "Période affichée : "+fmtRange(S.range) : "";
}
// Export CSV : lien temporaire avec attribut download (nom de fichier horodaté).
function exportCsv(all){
  // Horodatage LOCAL à la seconde : plusieurs exports dans la journée ne s'écrasent pas.
  const ctx=all?"both":S.source, d=new Date(), p=n=>String(n).padStart(2,"0"),
    stamp=d.getFullYear()+"-"+p(d.getMonth()+1)+"-"+p(d.getDate())+"_"+p(d.getHours())+p(d.getMinutes())+p(d.getSeconds());
  const a=document.createElement("a");
  a.href="/meteohub/export?ctx="+ctx+"&"+(all?"all=1":windowQuery());
  // ensemble = tout l'historique ; periode = fenêtre affichée (source dans le nom).
  a.download="meteo-"+(all?"ensemble":"periode-"+ctx)+"-"+stamp+".csv";
  document.body.appendChild(a);a.click();a.remove();
}
$("#expview").addEventListener("click",()=>exportCsv(false));
$("#expall").addEventListener("click",()=>exportCsv(true));
function applyCustom(){
  const f=fromLocalInput($("#cfrom").value), t=fromLocalInput($("#cto").value);
  const err=$("#cerr");
  if(!isFinite(f)||!isFinite(t)){err.textContent="Renseignez le jour et l'heure de début et de fin.";return;}
  const from=floor5(f), to=floor5(t);
  if(to-from<STEP_S){err.textContent="La fin doit être au moins 5 min après le début.";return;}
  if(to-from>MAX_SPAN_S){err.textContent="La période est limitée à un an.";return;}
  err.textContent="";
  S.range={from,to}; localStorage.setItem(LS+"range",JSON.stringify(S.range));
  syncPeriodUI(); draw();
}
if(S.range){ $("#custom").hidden=false; fillCustom(); }
syncPeriodUI();

$("#metricsel").addEventListener("change",e=>{
  const cb=e.target.closest("input[data-m]"); if(!cb)return;
  // Reconstruit la sélection dans l'ordre de METRICS d'après les cases cochées.
  S.metrics=METRIC_KEYS.filter(k=>{const el=document.querySelector('#metricsel input[data-m="'+k+'"]');return el&&el.checked;});
  localStorage.setItem(LS+"metrics",JSON.stringify(S.metrics));
  renderMetricSel();
  draw();
});
$("#source").addEventListener("change",e=>{S.source=e.target.value;localStorage.setItem(LS+"source",S.source);draw();});
$("#showsusp").checked=S.showSuspects;
$("#showsusp").addEventListener("change",e=>{S.showSuspects=e.target.checked;
  localStorage.setItem(LS+"susp",S.showSuspects?"1":"0");draw();});
$("#periods").addEventListener("click",e=>{const b=e.target.closest(".pbtn");if(!b)return;
  if(b.dataset.h==="custom"){
    // Ouvre (ou referme) le choix des bornes ; rien n'est redessiné avant « Afficher ».
    const c=$("#custom"); c.hidden=!c.hidden; if(!c.hidden) fillCustom();
    return;
  }
  // Retour à une période glissante : la période libre est oubliée.
  S.hours=+b.dataset.h; S.range=null;
  localStorage.setItem(LS+"hours",S.hours); localStorage.removeItem(LS+"range");
  $("#custom").hidden=true; syncPeriodUI(); draw();});
$("#capply").addEventListener("click",applyCustom);
$("#custom").addEventListener("keydown",e=>{if(e.key==="Enter")applyCustom();});
$("#cnow").addEventListener("click",()=>{$("#cto").value=toLocalInput(floor5(Date.now()/1000)+STEP_S);applyCustom();});

fetch("/status").then(r=>r.json()).then(s=>{const b=$("#vb");if(b)b.textContent=s.version?"v"+s.version:"";}).catch(()=>{});
// Domaine météo : retour possible vers la station collectée, en plus de morfAnalytics.
mfaMeteoHubBack();

// En-tete compact des que l'on descend. Le support #tbh garde la hauteur de l'en-tete
// deploye (mesuree, y compris quand la periode libre s'ouvre) : le resserrement ne
// deplace donc rien dans la page.
(function(){const h=document.getElementById("tbh"),tb=document.querySelector(".topbar");
  new ResizeObserver(()=>{if(!tb.classList.contains("compact"))h.style.height=tb.offsetHeight+"px";}).observe(tb);
  h.style.height=tb.offsetHeight+"px";
  const sync=()=>tb.classList.toggle("compact",scrollY>40);
  addEventListener("scroll",sync,{passive:true});sync();})();

draw();
// Rafraîchissement : utile tant que la fenêtre touche le présent. Une période
// libre entièrement passée ne bouge plus, inutile de la recharger chaque minute.
setInterval(()=>{ if(!S.range || S.range.to>Date.now()/1000-STEP_S) draw(); }, 60000);
</script>
</body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
