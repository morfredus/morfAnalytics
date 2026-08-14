/*
 * morfAnalytics
 * Copyright (C) 2026 morfredus
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "morfanalytics/pages/PhotoPage.h"

#include <QByteArray>

namespace morfanalytics::pages {

// -----------------------------------------------------------------------------
// Page /photo : interface d'EXPLORATION de la pratique photographique.
//
// Rôle de morfAnalytics : agréger, croiser, comparer, filtrer, représenter. La
// donnée reste souveraine chez morfPhoto ; la page ne fait que la lire (via
// /photo/data) et la représenter. Tout le calcul vit côté navigateur.
//
// Moteur de filtres UNIQUE et multi-critères : chaque dimension accepte plusieurs
// valeurs (OR à l'intérieur d'une dimension, AND entre dimensions). Le même moteur
// alimente TOUTES les vues (indicateurs, distributions, chronologie, matrice
// analytique, comparaison de groupes). Les graphiques alimentent les filtres : un
// clic ajoute un critère, un reclic le retire, jamais de remplacement automatique.
//
// CORPUS ≠ PRATIQUE : la présence d'un fichier n'implique ni appartenance ni usage.
// Le PÉRIMÈTRE de pratique (boîtiers exclus) est persistant (config service +
// localStorage) et distinct des filtres analytiques ; « réinitialiser les filtres »
// ne le touche pas. Exclure est une interprétation : elle vit ici, jamais dans
// morfPhoto.
//
// `snapshot` est ignoré : les données arrivent côté client via /photo/data (avec
// ?source= pour le handoff depuis PhotoHub).
// -----------------------------------------------------------------------------
QByteArray PhotoPage::render(const QJsonObject& /*snapshot*/) {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>morfAnalytics - Photo</title><style>
:root{--bg:#15171b;--card:#1e2126;--line:#2c3037;--ink:#e7e9ec;--muted:#99a1ad;--soft:#c7cdd6;
--accent:#6f9bff;--accent2:#7ee0b8;--warn:#f0b866;--track:#242830;--a:#6f9bff;--b:#7ee0b8}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:1.5rem}
.wrap{max-width:82rem;margin:auto}h1{margin:.2rem 0}h2{font-size:1.05rem;margin:1.4rem 0 .5rem}
.muted{color:var(--muted)}a{color:var(--accent)}
.vb{font-size:.8rem;font-weight:600;vertical-align:middle;color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,transparent);border:1px solid color-mix(in srgb,var(--accent) 30%,transparent);border-radius:999px;padding:.1rem .5rem;margin-left:.4rem}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:1rem 1.25rem;margin:1rem 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(9.5rem,1fr));gap:.8rem}
.tile{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:.9rem}
.tile .number{font-size:1.55rem;font-weight:700}.tile .sub{font-size:.78rem;color:var(--muted);margin-top:.2rem}
.cols{display:grid;grid-template-columns:repeat(auto-fit,minmax(20rem,1fr));gap:1rem}
.row{display:flex;align-items:center;gap:.6rem;margin:.2rem 0}
.row.click{cursor:pointer;border-radius:6px;padding:.05rem .2rem}
.row.click:hover{background:#232733}.row.on{background:#2a3350}
.lab{width:9rem;text-align:right;color:var(--soft);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-size:.9rem}
.bar{flex:1;background:var(--track);border-radius:6px;overflow:hidden}
.bar i{display:block;height:.85rem;background:var(--accent)}
.val{width:5.5rem;color:var(--muted);font-size:.85rem;text-align:right}
.ex{width:1.6rem;text-align:center;cursor:pointer;color:#5a6472;font-weight:700;user-select:none}
.ex:hover{color:var(--warn)}.ex.excluded{color:var(--warn)}
.fg{margin:.2rem 0}.fg b{color:var(--soft);font-size:.85rem;margin-right:.4rem}
.chip{display:inline-block;background:#243050;border:1px solid #35507f;color:#cdd8f7;border-radius:999px;padding:.1rem .55rem;font-size:.82rem;cursor:pointer;margin:.15rem .2rem .15rem 0}
.chip:hover{background:#2c3c66}.chip.excl{background:#3a2f1e;border-color:#6a5330;color:#f0d9b0}
.btn{background:#242830;border:1px solid var(--line);color:var(--soft);border-radius:8px;padding:.3rem .7rem;cursor:pointer;font-size:.85rem;margin:.1rem .2rem 0 0}
.btn:hover{border-color:var(--accent);color:var(--ink)}
.denom{font-size:.78rem;color:var(--muted);margin:.1rem 0 .5rem}
.mono{font-variant-numeric:tabular-nums}
select,input[type=number]{background:#242830;border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.25rem .4rem;font-size:.85rem}
input[type=number]{width:5.5rem}
.tl{display:flex;gap:2px;flex:1;align-items:flex-end;height:1.6rem}
.tl span{flex:1;background:var(--track);border-radius:2px}.tl span.u{background:var(--accent)}
.cmp .A{color:var(--a)}.cmp .B{color:var(--b)}
table{width:100%;border-collapse:collapse;font-size:.86rem}td,th{padding:.2rem .35rem;text-align:left;white-space:nowrap}
th{color:var(--muted);font-weight:600}tr+tr td{border-top:1px solid var(--line)}
.mx{overflow-x:auto}.mx td.n{text-align:right;font-variant-numeric:tabular-nums}
.mx th.h{position:sticky;left:0;background:var(--card)}
.note{font-size:.78rem;color:var(--muted);margin:.3rem 0}
.controls{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center;margin-bottom:.5rem}
</style></head><body><div class="wrap">
<p><a href="/">&larr; morfAnalytics</a></p>
<h1>Analyse de la phototh&egrave;que <span id="vb" class="vb"></span></h1>
<p class="muted">Poser des questions au corpus&nbsp;: croiser boîtiers, focales, ISO, ouvertures,
vitesses, périodes&hellip; La donnée reste souveraine dans morfPhoto ; ici on l'explore.</p>
<div id="app"><p class="muted">Chargement des donn&eacute;es&hellip;</p></div>
</div>
<script>
"use strict";
const LS_KEY="morfanalytics.photo.excludedCameras";
function loadExcluded(){try{return JSON.parse(localStorage.getItem(LS_KEY)||"[]")}catch(e){return []}}
function saveExcluded(s){try{localStorage.setItem(LS_KEY,JSON.stringify([...s]))}catch(e){}}
const MONTHS=["","janv","févr","mars","avr","mai","juin","juil","août","sept","oct","nov","déc"];
const ISO_RANGES=[[0,100,"≤100"],[101,200,"125-200"],[201,400,"250-400"],[401,800,"500-800"],
  [801,1600,"1000-1600"],[1601,3200,"2000-3200"],[3201,6400,"4000-6400"],[6401,1e9,"≥8000"]];
const APERTURE_RANGES=[[0,1.9,"< f/2"],[1.9,2.9,"f/2-2.8"],[2.9,4.1,"f/2.8-4"],[4.1,5.7,"f/4-5.6"],
  [5.7,8.1,"f/5.6-8"],[8.1,11.1,"f/8-11"],[11.1,16.1,"f/11-16"],[16.1,1e9,"> f/16"]];
const SHUTTER_RANGES=[[0,0.001,"≤1/1000"],[0.001,0.004,"1/1000-1/250"],[0.004,0.008,"1/250-1/125"],
  [0.008,0.0167,"1/125-1/60"],[0.0167,0.033,"1/60-1/30"],[0.033,0.125,"1/30-1/8"],[0.125,1,"1/8-1s"],[1,1e9,"> 1s"]];

const $=s=>document.querySelector(s);
const fr=n=>new Intl.NumberFormat("fr-FR").format(n);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
function fmtShutter(s){if(s===null||s===undefined)return "—";return s<1?("1/"+Math.round(1/s)):(s+" s");}
function fmtF(v,d){return (v===null||v===undefined)?"—":v.toFixed(d===undefined?1:d);}

// ---- Filtres : état multi-critères ------------------------------------------
// Catégoriel = Set de clés ; numérique = tableau de plages [min,max,libellé].
// period = fenêtre continue d'années (from..to) sur taken_at. camExcl = périmètre.
function emptyFilters(){return {cam:new Set(),lens:new Set(),type:new Set(),folder:new Set(),
  year:new Set(),month:new Set(),focal:[],aperture:[],iso:[],shutter:[],
  period:{from:null,to:null},camExcl:new Set()};}
const S=emptyFilters();
const MX={row:"cam",col:"focal",measure:"count"};   // matrice analytique
const GRP={a:null,b:null};                           // comparaison par groupes
let D=null;

// ---- Modèle de DIMENSIONS (moteur commun à toutes les vues) ------------------
let DIMS={};
function buildDims(){
  DIMS={
    cam:   {label:"Boîtier",  kind:"cat", col:"camera",    key:i=>D.cols.camera[i],   lab:k=>D.dict.camera[k]??("#"+k)},
    lens:  {label:"Objectif", kind:"cat", col:"lens",      key:i=>D.cols.lens[i],     lab:k=>D.dict.lens[k]??("#"+k)},
    type:  {label:"Type",     kind:"cat", col:"file_type", key:i=>D.cols.file_type[i],lab:k=>D.dict.file_type[k]??("#"+k)},
    folder:{label:"Dossier",  kind:"cat", col:"folder_id", key:i=>D.cols.folder_id[i],lab:k=>D.folders[k]??("#"+k)},
    year:  {label:"Année",    kind:"cat", key:i=>D.years[i],  lab:k=>String(k)},
    month: {label:"Mois",     kind:"cat", key:i=>D.months[i], lab:k=>MONTHS[k]||("#"+k)},
    focal: {label:"Focale",   kind:"range", col:"focal_length", ranges:()=>(D.buckets||[]).map(b=>[b.min,b.max,b.label])},
    aperture:{label:"Ouverture",kind:"range", col:"aperture", ranges:()=>APERTURE_RANGES},
    iso:   {label:"ISO",      kind:"range", col:"iso", ranges:()=>ISO_RANGES},
    shutter:{label:"Vitesse", kind:"range", col:"shutter_speed_s", ranges:()=>SHUTTER_RANGES},
  };
}
const CAT_DIMS=["cam","lens","type","folder","year","month"];
const RANGE_DIMS=["focal","aperture","iso","shutter"];

// ---- Chargement --------------------------------------------------------------
function reload(){
  fetch("/photo/data"+location.search).then(r=>r.json()).then(init).catch(e=>{
    $("#app").innerHTML='<section class="card"><strong>Donn&eacute;es indisponibles.</strong>'+
      '<p class="muted">'+esc(e)+'</p></section>';
  });
}
function init(snap){
  if(!snap||!snap.reachable){
    const src=snap&&snap.source_url?esc(snap.source_url):"(non configur&eacute;e)";
    const err=snap&&snap.last_error?esc(snap.last_error):"";
    $("#app").innerHTML='<section class="card"><strong>morfPhoto injoignable.</strong>'+
      '<p class="muted">Source&nbsp;: '+src+'<br>'+err+'</p></section>';
    return;
  }
  D=decode(snap); buildDims();
  S.camExcl=new Set([...(D.configExcl||[]),...loadExcluded()]);
  render();
}
function decode(snap){
  const ds=snap.dataset||{}, cols=ds.columns||{}, dict=ds.dictionaries||{};
  const n=ds.count||0;
  const years=new Array(n), months=new Array(n);
  const ta=cols.taken_at||[];
  for(let i=0;i<n;i++){
    const t=ta[i];
    if(typeof t==="string"&&t.length>=7){years[i]=+t.slice(0,4);months[i]=+t.slice(5,7);}
    else{years[i]=null;months[i]=null;}
  }
  return {n,cols,dict,years,months,
          buckets:snap.focal_buckets||[], folders:ds.folders||{},
          configExcl:snap.exclude_cameras||[],
          source:snap.source_url||"", fetched:snap.fetched_at||null};
}

// ---- Moteur de filtre --------------------------------------------------------
function inAnyRange(v,ranges){return v!==null&&v!==undefined&&ranges.some(r=>v>=r[0]&&v<=r[1]);}
function rangeOn(ranges,k){return ranges.some(x=>x[0]===k[0]&&x[1]===k[1]&&x[2]===k[2]);}
function rmRange(arr,r){const i=arr.findIndex(x=>x[0]===r[0]&&x[1]===r[1]&&x[2]===r[2]);if(i>=0)arr.splice(i,1);}
// Un fichier passe-t-il un jeu de filtres F ? (OR interne, AND entre dimensions)
function matchesWith(i,F){
  for(const d of CAT_DIMS){
    if(F[d].size){const k=DIMS[d].key(i); if(k===null||k===undefined||!F[d].has(k))return false;}
  }
  if(F.camExcl.size){const k=D.cols.camera[i]; if(k!==null&&F.camExcl.has(D.dict.camera[k]))return false;}
  for(const d of RANGE_DIMS){
    if(F[d].length){const v=D.cols[DIMS[d].col][i]; if(!inAnyRange(v,F[d]))return false;}
  }
  if(F.period.from!==null||F.period.to!==null){
    const y=D.years[i]; if(y===null)return false;
    if(F.period.from!==null&&y<F.period.from)return false;
    if(F.period.to!==null&&y>F.period.to)return false;
  }
  return true;
}
function matches(i){return matchesWith(i,S);}
function filteredWith(F){const o=[];for(let i=0;i<D.n;i++)if(matchesWith(i,F))o.push(i);return o;}
function filtered(){return filteredWith(S);}

// ---- Agrégations -------------------------------------------------------------
function countCat(idx,dim){
  const D_=DIMS[dim]; const m=new Map(); let known=0;
  for(const i of idx){const k=D_.key(i); if(k===null||k===undefined)continue; known++; m.set(k,(m.get(k)||0)+1);}
  const rows=[...m.entries()].map(([k,count])=>({key:k,label:D_.lab(k),count}));
  rows.sort((a,b)=>b.count-a.count||String(a.label).localeCompare(String(b.label)));
  return {rows,known};
}
function histoRows(idx,dim){
  const D_=DIMS[dim], ranges=D_.ranges(), col=D_.col, counts=ranges.map(()=>0); let known=0;
  for(const i of idx){const v=D.cols[col][i]; if(v===null)continue; known++;
    for(let r=0;r<ranges.length;r++)if(v>=ranges[r][0]&&v<=ranges[r][1]){counts[r]++;break;}}
  const rows=ranges.map((r,k)=>({label:r[2],count:counts[k],k:[r[0],r[1],r[2]]})).filter(r=>r.count>0);
  return {rows,known};
}
function countBy(idx,arr){const m=new Map();let known=0;for(const i of idx){const k=arr[i];if(k===null||k===undefined)continue;known++;m.set(k,(m.get(k)||0)+1);}return {map:m,known};}
function values(idx,col){const c=D.cols[col];const out=[];for(const i of idx){const v=c[i];if(v!==null&&v!==undefined)out.push(v);}return out;}
function median(a){if(!a.length)return null;const s=[...a].sort((x,y)=>x-y);const m=s.length>>1;return s.length%2?s[m]:(s[m-1]+s[m])/2;}
function topFocals(idx,limit){
  const c=D.cols.focal_length,m=new Map();
  for(const i of idx){const f=c[i];if(f===null)continue;const k=Math.round(f);m.set(k,(m.get(k)||0)+1);}
  return [...m.entries()].map(([k,count])=>({label:k+" mm",count,k:[k-0.5,k+0.5,k+" mm"]}))
                         .sort((a,b)=>b.count-a.count).slice(0,limit||12);
}
function topLenses(idx,limit){return countCat(idx,"lens").rows.slice(0,limit||3).map(r=>r.label+" ("+r.count+")").join(", ")||"—";}

// ---- Rendu de base -----------------------------------------------------------
function bars(rows,opts){
  opts=opts||{};
  if(!rows.length) return '<p class="muted">aucune donn&eacute;e</p>';
  const max=Math.max(1,...rows.map(r=>r.count));
  return rows.map(r=>{
    const pct=Math.round(r.count*100/max);
    const on=opts.isOn&&opts.isOn(r)?" on":"";
    const exCol=opts.excludable
      ? '<span class="ex'+(opts.isExcluded(r)?" excluded":"")+'" data-ex="'+esc(r.label)+
        '" title="Inclure/exclure ce bo&icirc;tier de ma pratique">&empty;</span>':"";
    const attr=opts.filterKey?' data-f="'+opts.filterKey+'" data-k="'+esc(JSON.stringify(r.k!==undefined?r.k:r.key))+'"':"";
    return '<div class="row click'+on+'"'+attr+'><span class="lab" title="'+esc(r.label)+'">'+esc(r.label)+
      '</span><span class="bar"><i style="width:'+pct+'%"></i></span>'+
      '<span class="val mono">'+fr(r.count)+'</span>'+exCol+'</div>';
  }).join("");
}
function denom(known,total){
  if(known>=total) return '<div class="denom">calcul&eacute; sur '+fr(total)+' photo(s)</div>';
  return '<div class="denom">calcul&eacute; sur '+fr(known)+' / '+fr(total)+
         ' photo(s) &mdash; '+fr(total-known)+' sans cette donn&eacute;e</div>';
}
function chip(label,cls,onClear){const id="c"+(chip._n=(chip._n||0)+1);chip._h=chip._h||{};chip._h[id]=onClear;
  return '<span class="chip'+(cls?" "+cls:"")+'" data-chip="'+id+'"><b>'+esc(label)+'</b> &times;</span>';}

// Sections génériques (même moteur, mêmes interactions partout).
function catCard(dim,extra){
  const idx=filtered(), a=countCat(idx,dim);
  const isCam=dim==="cam";
  return '<div class="card">'+denom(a.known,idx.length)+
    (isCam?'<p class="note">Clic&nbsp;: filtrer &middot; &empty;&nbsp;: exclure de ma pratique (mémorisé).</p>':'')+
    bars(a.rows,{filterKey:dim,isOn:r=>S[dim].has(r.key),
      excludable:isCam,isExcluded:r=>S.camExcl.has(r.label)})+(extra||'')+'</div>';
}
function rangeCard(dim){
  const idx=filtered(), a=histoRows(idx,dim);
  return '<div class="card">'+denom(a.known,idx.length)+
    bars(a.rows,{filterKey:dim,isOn:r=>rangeOn(S[dim],r.k)})+'</div>';
}

// ---- Filtres actifs (périmètre séparé, groupés par dimension) -----------------
function filtersPanel(){
  chip._h={};
  // Périmètre de pratique (persistant, distinct des filtres).
  let per='<div class="fg"><b>Périmètre</b> ';
  if(S.camExcl.size){
    per+='ma pratique &middot; exclus&nbsp;: '+[...S.camExcl].map(c=>chip(c,"excl",()=>{
      S.camExcl.delete(c);const l=new Set(loadExcluded());l.delete(c);saveExcluded(l);})).join("");
  } else per+='<span class="muted">corpus complet</span>';
  per+='</div>';

  // Filtres analytiques, groupés par dimension.
  const lines=[];
  const pad=m=>String(m).padStart(2,"0");
  for(const d of CAT_DIMS){
    if(!S[d].size)continue;
    let vals=[...S[d]];
    if(d==="year"||d==="month")vals.sort((a,b)=>a-b);
    const cs=vals.map(k=>chip(DIMS[d].lab(k),null,()=>S[d].delete(k))).join("");
    lines.push('<div class="fg"><b>'+DIMS[d].label+'</b> '+cs+'</div>');
  }
  for(const d of RANGE_DIMS){
    if(!S[d].length)continue;
    const cs=S[d].map(r=>chip(r[2],null,()=>rmRange(S[d],r))).join("");
    lines.push('<div class="fg"><b>'+DIMS[d].label+'</b> '+cs+'</div>');
  }
  if(S.period.from!==null||S.period.to!==null)
    lines.push('<div class="fg"><b>Période</b> '+
      chip((S.period.from??"…")+"–"+(S.period.to??"…"),null,()=>{S.period={from:null,to:null};})+'</div>');

  let filt='<div class="fg" style="margin-top:.4rem"><b>Filtres actifs</b> ';
  if(!lines.length) filt+='<span class="muted">aucun &mdash; tout le périmètre</span></div>';
  else filt+='</div>'+lines.join("")+'<button class="btn" id="resetf">réinitialiser les filtres</button>';
  return '<section class="card">'+per+filt+'</section>';
}

// ---- Indicateurs -------------------------------------------------------------
function tiles(idx){
  const c=D.cols; let minT=null,maxT=null,exifUsable=0,incomplete=0;
  const cams=new Set(),lenses=new Set(),focals=new Set();
  for(const i of idx){
    const t=c.taken_at[i]; if(t){if(minT===null||t<minT)minT=t; if(maxT===null||t>maxT)maxT=t;}
    if(c.camera[i]!==null)cams.add(c.camera[i]);
    if(c.lens[i]!==null)lenses.add(c.lens[i]);
    if(c.focal_length[i]!==null)focals.add(c.focal_length[i]);
    const hasExp=c.focal_length[i]!==null||c.aperture[i]!==null||c.iso[i]!==null;
    if(c.camera[i]!==null&&hasExp)exifUsable++;
    if(c.focal_length[i]===null||c.aperture[i]===null||c.iso[i]===null)incomplete++;
  }
  const period=(minT&&maxT)?(minT.slice(0,10)+" → "+maxT.slice(0,10)):"—";
  const t=(num,lab)=>'<div class="tile"><div class="number mono">'+num+'</div><div class="sub">'+lab+'</div></div>';
  return '<div class="grid">'+t(fr(idx.length),"Photos (périmètre courant)")+t(esc(period),"Période couverte")+
    t(fr(cams.size),"Boîtiers")+t(fr(lenses.size),"Objectifs")+t(fr(focals.size),"Focales distinctes")+
    t(fr(exifUsable),"EXIF exploitable")+t(fr(incomplete),"Métadonnées incomplètes")+'</div>';
}
function statTiles(idx){
  const f=values(idx,"focal_length"),a=values(idx,"aperture"),s=values(idx,"iso"),sh=values(idx,"shutter_speed_s");
  const t=(num,lab,kn)=>'<div class="tile"><div class="number mono">'+num+'</div><div class="sub">'+lab+' &middot; '+fr(kn)+' conn.</div></div>';
  return '<div class="grid">'+
    t(f.length?Math.round(median(f))+" mm":"—","Focale médiane",f.length)+
    t(a.length?"f/"+fmtF(median(a),1):"—","Ouverture médiane",a.length)+
    t(s.length?fr(Math.round(median(s))):"—","ISO médian",s.length)+
    t(sh.length?fmtShutter(median(sh)):"—","Vitesse médiane",sh.length)+'</div>';
}
function cameraTimeline(idx){
  const c=D.cols; let y0=Infinity,y1=-Infinity; const per=new Map();
  for(const i of idx){const k=c.camera[i]; if(k===null)continue; const y=D.years[i];
    let e=per.get(k); if(!e){e={count:0,years:new Set(),min:null,max:null};per.set(k,e);}
    e.count++; const t=c.taken_at[i];
    if(t){if(e.min===null||t<e.min)e.min=t; if(e.max===null||t>e.max)e.max=t;}
    if(y!==null){e.years.add(y); if(y<y0)y0=y; if(y>y1)y1=y;}}
  if(!per.size||!isFinite(y0)) return '<p class="muted">aucune donn&eacute;e datée</p>';
  const rows=[...per.entries()].sort((a,b)=>b[1].count-a[1].count);
  let head='<div class="row"><span class="lab muted">bo&icirc;tier</span><span class="tl">';
  for(let y=y0;y<=y1;y++)head+='<span style="background:transparent;color:var(--muted);font-size:.6rem;text-align:center">'+("'"+String(y).slice(2))+'</span>';
  head+='</span><span class="val muted">photos</span></div>';
  const body=rows.map(([k,e])=>{let cells="";for(let y=y0;y<=y1;y++)cells+='<span class="'+(e.years.has(y)?"u":"")+'" title="'+y+'"></span>';
    const name=D.dict.camera[k]||("#"+k); const p=(e.min&&e.max)?(e.min.slice(0,7)+"→"+e.max.slice(0,7)):"";
    return '<div class="row"><span class="lab" title="'+esc(name)+'">'+esc(name)+'</span><span class="tl">'+cells+
      '</span><span class="val mono">'+fr(e.count)+'</span></div><div class="note" style="margin-left:9.6rem">'+esc(p)+'</div>';}).join("");
  return head+body;
}

// ---- Matrice analytique (croisement libre X × Y × mesure) --------------------
const MEASURES={count:"Nombre de photos",medfocal:"Focale médiane (mm)",medaperture:"Ouverture médiane",
  mediso:"ISO médian",medshutter:"Vitesse médiane"};
const MX_DIMS=["cam","lens","type","folder","year","month","focal","aperture","iso","shutter"];
// Étiquette de bucket d'une photo pour une dimension (null si non classable).
function bucketLabel(dim,i){
  const D_=DIMS[dim];
  if(D_.kind==="cat"){const k=D_.key(i);return (k===null||k===undefined)?null:D_.lab(k);}
  const v=D.cols[D_.col][i]; if(v===null||v===undefined)return null;
  const rs=D_.ranges(); for(const r of rs)if(v>=r[0]&&v<=r[1])return r[2]; return null;
}
// Ordre d'affichage des étiquettes d'une dimension.
function orderLabels(dim,set){
  const D_=DIMS[dim], arr=[...set];
  if(dim==="year"||dim==="month")return arr.sort((a,b)=>(+a)-(+b));
  if(D_.kind==="range"){const order=D_.ranges().map(r=>r[2]);return arr.sort((a,b)=>order.indexOf(a)-order.indexOf(b));}
  return arr.sort((a,b)=>String(a).localeCompare(String(b)));
}
function measureVal(idx,measure){
  if(!idx.length)return "";
  if(measure==="count")return fr(idx.length);
  if(measure==="medfocal"){const m=median(values(idx,"focal_length"));return m===null?"—":Math.round(m)+"";}
  if(measure==="medaperture"){const m=median(values(idx,"aperture"));return m===null?"—":"f/"+fmtF(m,1);}
  if(measure==="mediso"){const m=median(values(idx,"iso"));return m===null?"—":fr(Math.round(m));}
  if(measure==="medshutter"){const m=median(values(idx,"shutter_speed_s"));return fmtShutter(m);}
  return "";
}
function matrixBlock(){
  const opts=(cur)=>MX_DIMS.map(d=>'<option value="'+d+'"'+(cur===d?" selected":"")+'>'+DIMS[d].label+'</option>').join("");
  const mopts=Object.keys(MEASURES).filter(k=>MEASURES[k]).map(k=>'<option value="'+k+'"'+(MX.measure===k?" selected":"")+'>'+MEASURES[k]+'</option>').join("");
  let h='<div class="controls"><label class="muted">Lignes</label><select id="mxrow">'+opts(MX.row)+'</select>'+
    '<label class="muted">Colonnes</label><select id="mxcol">'+opts(MX.col)+'</select>'+
    '<label class="muted">Mesure</label><select id="mxmea">'+mopts+'</select></div>';
  if(MX.row===MX.col){h+='<p class="note">Choisir deux dimensions différentes.</p>';return h;}
  const idx=filtered();
  const rowSet=new Set(),colSet=new Set(),cell=new Map();
  for(const i of idx){
    const r=bucketLabel(MX.row,i), c=bucketLabel(MX.col,i);
    if(r===null||c===null)continue;
    rowSet.add(r);colSet.add(c);
    const key=r+""+c; let arr=cell.get(key); if(!arr){arr=[];cell.set(key,arr);} arr.push(i);
  }
  if(!rowSet.size){h+='<p class="muted">aucune donn&eacute;e pour ce croisement</p>';return h;}
  const rows=orderLabels(MX.row,rowSet), colsL=orderLabels(MX.col,colSet);
  const cap=40;   // garde-fou d'affichage
  const rowsC=rows.slice(0,cap), colsC=colsL.slice(0,cap);
  let t='<div class="mx"><table><tr><th class="h">'+esc(DIMS[MX.row].label)+' \\ '+esc(DIMS[MX.col].label)+'</th>';
  for(const c of colsC)t+='<th class="n">'+esc(c)+'</th>';
  t+='</tr>';
  for(const r of rowsC){
    t+='<tr><td class="h">'+esc(r)+'</td>';
    for(const c of colsC){const arr=cell.get(r+""+c)||[];t+='<td class="n">'+(arr.length?measureVal(arr,MX.measure):'·')+'</td>';}
    t+='</tr>';
  }
  t+='</table></div>';
  if(rows.length>cap||colsL.length>cap)t+='<p class="note">Affichage limité à '+cap+' lignes/colonnes ; affinez les filtres.</p>';
  return h+t;
}

// ---- Comparaison par groupes de filtres --------------------------------------
function snapFilters(){return {cam:new Set(S.cam),lens:new Set(S.lens),type:new Set(S.type),folder:new Set(S.folder),
  year:new Set(S.year),month:new Set(S.month),focal:S.focal.map(r=>r.slice()),aperture:S.aperture.map(r=>r.slice()),
  iso:S.iso.map(r=>r.slice()),shutter:S.shutter.map(r=>r.slice()),period:{...S.period},camExcl:new Set(S.camExcl)};}
function describeFilters(F){
  const parts=[];
  for(const d of CAT_DIMS)if(F[d].size)parts.push(DIMS[d].label+": "+[...F[d]].map(k=>DIMS[d].lab(k)).join("/"));
  for(const d of RANGE_DIMS)if(F[d].length)parts.push(DIMS[d].label+": "+F[d].map(r=>r[2]).join("/"));
  if(F.period.from!==null||F.period.to!==null)parts.push("Période: "+(F.period.from??"…")+"–"+(F.period.to??"…"));
  return parts.length?parts.join(" · "):"tout le périmètre";
}
function groupMetrics(F){
  const idx=filteredWith(F);
  const f=values(idx,"focal_length"),a=values(idx,"aperture"),s=values(idx,"iso"),sh=values(idx,"shutter_speed_s");
  return {n:idx.length,
    mf:f.length?Math.round(median(f))+" mm":"—", ma:a.length?"f/"+fmtF(median(a),1):"—",
    mi:s.length?fr(Math.round(median(s))):"—", ms:sh.length?fmtShutter(median(sh)):"—",
    tf:topFocals(idx,3).map(x=>x.label).join(", ")||"—", tl:topLenses(idx,3)};
}
function groupsBlock(){
  let h='<div class="controls"><button class="btn" id="capA">capturer les filtres → Groupe A</button>'+
    '<button class="btn" id="capB">→ Groupe B</button><button class="btn" id="capClr">vider</button></div>';
  h+='<div class="cmp"><p class="note"><span class="A">■</span> A&nbsp;: '+(GRP.a?esc(describeFilters(GRP.a)):"<span class='muted'>non défini</span>")+
     '<br><span class="B">■</span> B&nbsp;: '+(GRP.b?esc(describeFilters(GRP.b)):"<span class='muted'>non défini</span>")+'</p></div>';
  if(GRP.a&&GRP.b){
    const A=groupMetrics(GRP.a),B=groupMetrics(GRP.b);
    const r=(lab,x,y)=>'<tr><td class="muted">'+lab+'</td><td>'+x+'</td><td>'+y+'</td></tr>';
    h+='<table><tr><th></th><th class="A">Groupe A</th><th class="B">Groupe B</th></tr>'+
      r("Photos",fr(A.n),fr(B.n))+r("Focale médiane",A.mf,B.mf)+r("Ouverture médiane",A.ma,B.ma)+
      r("ISO médian",A.mi,B.mi)+r("Vitesse médiane",A.ms,B.ms)+r("Top focales",A.tf,B.tf)+
      r("Objectifs les plus utilisés",A.tl,B.tl)+'</table>';
  } else h+='<p class="note">Construisez un sous-ensemble avec les filtres, capturez-le comme Groupe A, changez les filtres, capturez le Groupe B.</p>';
  return h;
}

// ---- Comparaison rapide (une dimension, A vs B) ------------------------------
function quickCompare(idx){
  const dims=[["year","année"],["cam","boîtier"],["lens","objectif"]];
  const valuesFor=dim=>{const set=new Map();
    for(const i of idx){let v=null,lab=null;
      if(dim==="year"){v=D.years[i];lab=v;}else{v=DIMS[dim].key(i);lab=v===null?null:DIMS[dim].lab(v);}
      if(v===null||v===undefined)continue;set.set(String(v),lab);}
    return [...set.entries()].sort((a,b)=>String(a[1]).localeCompare(String(b[1])));};
  let h='<div class="controls"><label class="muted">Comparer par</label><select id="qcd"><option value="">—</option>'+
    dims.map(d=>'<option value="'+d[0]+'"'+(QC.dim===d[0]?" selected":"")+'>'+d[1]+'</option>').join("")+'</select>';
  if(QC.dim){const vals=valuesFor(QC.dim);
    const sel=(id,cur)=>'<select id="'+id+'"><option value="">—</option>'+
      vals.map(v=>'<option value="'+esc(v[0])+'"'+(cur===v[0]?" selected":"")+'>'+esc(v[1])+'</option>').join("")+'</select>';
    h+='<span class="A">A</span>'+sel("qca",QC.a)+'<span class="B">B</span>'+sel("qcb",QC.b);}
  h+='</div>';
  if(QC.dim&&QC.a&&QC.b){
    const pick=val=>idx.filter(i=>{if(QC.dim==="year")return String(D.years[i])===val;const k=DIMS[QC.dim].key(i);return k!==null&&String(k)===val;});
    const A=pick(QC.a),B=pick(QC.b);
    const labOf=v=>QC.dim==="year"?v:DIMS[QC.dim].lab(+v);
    const met=idx2=>{const f=values(idx2,"focal_length"),a=values(idx2,"aperture"),s=values(idx2,"iso"),sh=values(idx2,"shutter_speed_s");
      return {n:idx2.length,mf:f.length?Math.round(median(f))+" mm":"—",ma:a.length?"f/"+fmtF(median(a),1):"—",
        mi:s.length?fr(Math.round(median(s))):"—",ms:sh.length?fmtShutter(median(sh)):"—",tf:topFocals(idx2,3).map(x=>x.label).join(", ")||"—"};};
    const mA=met(A),mB=met(B),r=(l,x,y)=>'<tr><td class="muted">'+l+'</td><td>'+x+'</td><td>'+y+'</td></tr>';
    h+='<table><tr><th></th><th class="A">'+esc(labOf(QC.a))+'</th><th class="B">'+esc(labOf(QC.b))+'</th></tr>'+
      r("Photos",fr(mA.n),fr(mB.n))+r("Focale médiane",mA.mf,mB.mf)+r("Ouverture médiane",mA.ma,mB.ma)+
      r("ISO médian",mA.mi,mB.mi)+r("Vitesse médiane",mA.ms,mB.ms)+r("Top focales",mA.tf,mB.tf)+'</table>';
  }
  return h;
}
const QC={dim:null,a:null,b:null};

// ---- Rendu principal ---------------------------------------------------------
function render(){
  const idx=filtered();
  let h="";
  h+=filtersPanel();
  h+=tiles(idx);
  h+='<h2>Tendances (médianes)</h2>'+statTiles(idx);

  h+='<div class="cols">';
  h+='<div><h2>Par année</h2>'+catCard("year",
      '<div class="controls" style="margin-top:.5rem"><label class="muted">Période</label>'+
      '<input type="number" id="pfrom" placeholder="de" value="'+(S.period.from??"")+'">'+
      '<input type="number" id="pto" placeholder="à" value="'+(S.period.to??"")+'">'+
      '<button class="btn" id="pset">appliquer</button><button class="btn" id="pclr">effacer</button></div>')+'</div>';
  h+='<div><h2>Par mois</h2>'+catCard("month")+'</div>';
  h+='</div>';

  h+='<div class="cols">';
  h+='<div><h2>Boîtiers</h2>'+catCard("cam")+'</div>';
  h+='<div><h2>Objectifs</h2>'+catCard("lens",
      (S.lens.size?'<p class="note">Boîtiers associés&nbsp;: '+esc(countCat(idx,"cam").rows.map(r=>r.label+" ("+r.count+")").join(", ")||"—")+'</p>':''))+'</div>';
  h+='</div>';

  h+='<div class="cols">';
  h+='<div><h2>Focales usuelles</h2>'+rangeCard("focal")+'</div>';
  h+='<div><h2>Focales — détail (top)</h2><div class="card">'+
     bars(topFocals(idx,12),{filterKey:"focal",isOn:r=>rangeOn(S.focal,r.k)})+
     '<p class="note">Focales exactes (au mm) les plus fréquentes.</p></div></div>';
  h+='</div>';

  h+='<div class="cols">';
  h+='<div><h2>Sensibilité ISO</h2>'+rangeCard("iso")+'</div>';
  h+='<div><h2>Ouvertures</h2>'+rangeCard("aperture")+'</div>';
  h+='</div>';

  h+='<div class="cols">';
  h+='<div><h2>Vitesses d\'obturation</h2>'+rangeCard("shutter")+'</div>';
  h+='<div><h2>Type de fichier</h2>'+catCard("type")+'</div>';
  h+='</div>';

  h+='<div class="cols">';
  h+='<div><h2>Dossiers</h2>'+catCard("folder")+'</div>';
  h+='<div><h2>Boîtiers — chronologie</h2><div class="card">'+cameraTimeline(idx)+'</div></div>';
  h+='</div>';

  h+='<h2>Matrice analytique</h2><div class="card">'+matrixBlock()+'</div>';
  h+='<h2>Comparaison rapide (une dimension)</h2><div class="card cmp">'+quickCompare(idx)+'</div>';
  h+='<h2>Comparaison de groupes de filtres</h2><div class="card cmp">'+groupsBlock()+'</div>';

  h+='<div class="card"><button class="btn" id="reload">recharger les données</button>'+
     '<div class="denom">'+(D.fetched?('actualisé '+esc(D.fetched)):'')+'<br>source&nbsp;: '+esc(D.source||"")+'</div></div>';

  $("#app").innerHTML=h;
  wire();
}

// ---- Interactions ------------------------------------------------------------
function toggleFilter(field,val){
  if(DIMS[field].kind==="cat"){if(S[field].has(val))S[field].delete(val);else S[field].add(val);}
  else{rangeOn(S[field],val)?rmRange(S[field],val):S[field].push(val);}
}
function on(id,ev,fn){const el=$("#"+id);if(el)el.addEventListener(ev,fn);}
function wire(){
  document.querySelectorAll("[data-f]").forEach(el=>{
    el.addEventListener("click",e=>{if(e.target.classList.contains("ex"))return;
      toggleFilter(el.getAttribute("data-f"),JSON.parse(el.getAttribute("data-k")));render();});});
  document.querySelectorAll(".ex").forEach(el=>{
    el.addEventListener("click",e=>{e.stopPropagation();const name=el.getAttribute("data-ex");
      const local=new Set(loadExcluded());
      if(S.camExcl.has(name)){S.camExcl.delete(name);local.delete(name);}else{S.camExcl.add(name);local.add(name);}
      saveExcluded(local);render();});});
  document.querySelectorAll("[data-chip]").forEach(el=>{
    el.addEventListener("click",()=>{const fn=chip._h[el.getAttribute("data-chip")];if(fn)fn();render();});});
  on("resetf","click",()=>{const ex=S.camExcl;const f=emptyFilters();f.camExcl=ex;Object.assign(S,f);render();});
  on("reload","click",reload);
  // Période
  on("pset","click",()=>{const a=$("#pfrom").value,b=$("#pto").value;
    S.period={from:a===""?null:+a,to:b===""?null:+b};render();});
  on("pclr","click",()=>{S.period={from:null,to:null};render();});
  // Matrice
  on("mxrow","change",e=>{MX.row=e.target.value;render();});
  on("mxcol","change",e=>{MX.col=e.target.value;render();});
  on("mxmea","change",e=>{MX.measure=e.target.value;render();});
  // Comparaison rapide
  on("qcd","change",e=>{QC.dim=e.target.value||null;QC.a=QC.b=null;render();});
  on("qca","change",e=>{QC.a=e.target.value||null;render();});
  on("qcb","change",e=>{QC.b=e.target.value||null;render();});
  // Groupes
  on("capA","click",()=>{GRP.a=snapFilters();render();});
  on("capB","click",()=>{GRP.b=snapFilters();render();});
  on("capClr","click",()=>{GRP.a=GRP.b=null;render();});
}

// Badge de version : lit /status (comme la page MeteoHub) pour afficher la version
// du service courant a cote du titre. Silencieux si /status est indisponible.
fetch("/status").then(r=>r.json()).then(s=>{const b=document.getElementById("vb");if(b)b.textContent=s.version?"v"+s.version:"";}).catch(()=>{});
reload();
</script>
</body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
