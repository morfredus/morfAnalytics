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
// Rôle de morfAnalytics ici : agréger, distribuer, croiser, comparer, filtrer. La
// donnée reste souveraine chez morfPhoto ; la page ne fait que la lire (via
// /photo/data) et la représenter. Tout le calcul vit côté navigateur : un SEUL jeu
// de filtres pilote TOUTES les vues. Le serveur n'émet qu'un squelette autonome
// (aucune dépendance externe : « natif d'abord », pas de CDN) qui va chercher les
// données en JSON puis fait l'agrégation en JS.
//
// Principe analytique câblé dans les filtres : CORPUS ≠ PRATIQUE. La présence d'un
// fichier n'implique ni appartenance ni usage personnel. On peut restreindre
// (inclure) et surtout EXCLURE des boîtiers pour délimiter le périmètre réel de sa
// pratique ; les exclusions viennent de la config du service (politique) ET du
// navigateur (localStorage). Exclure un boîtier est une INTERPRÉTATION : elle vit
// ici, jamais dans morfPhoto.
//
// Handoff : /photo peut recevoir ?source=<baseUrl morfPhoto> (ouvert depuis
// PhotoHub) ; la page le relaie à /photo/data pour analyser CETTE photothèque.
//
// `snapshot` est ignoré : les données arrivent côté client via /photo/data.
// -----------------------------------------------------------------------------
QByteArray PhotoPage::render(const QJsonObject& /*snapshot*/) {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>morfAnalytics - Photo</title><style>
:root{--bg:#15171b;--card:#1e2126;--line:#2c3037;--ink:#e7e9ec;--muted:#99a1ad;--soft:#c7cdd6;
--accent:#6f9bff;--accent2:#7ee0b8;--warn:#f0b866;--track:#242830;--a:#6f9bff;--b:#7ee0b8}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:1.5rem}
.wrap{max-width:80rem;margin:auto}h1{margin:.2rem 0}h2{font-size:1.05rem;margin:1.4rem 0 .5rem}
.muted{color:var(--muted)}a{color:var(--accent)}
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
.chips{display:flex;flex-wrap:wrap;gap:.4rem;align-items:center}
.chip{background:#243050;border:1px solid #35507f;color:#cdd8f7;border-radius:999px;padding:.15rem .6rem;font-size:.85rem;cursor:pointer}
.chip:hover{background:#2c3c66}.chip b{color:#fff}
.btn{background:#242830;border:1px solid var(--line);color:var(--soft);border-radius:8px;padding:.3rem .7rem;cursor:pointer;font-size:.85rem}
.btn:hover{border-color:var(--accent);color:var(--ink)}
.denom{font-size:.78rem;color:var(--muted);margin:.1rem 0 .5rem}
.mono{font-variant-numeric:tabular-nums}
select{background:#242830;border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.25rem .4rem;font-size:.85rem}
.tl{display:flex;gap:2px;flex:1;align-items:flex-end;height:1.6rem}
.tl span{flex:1;background:var(--track);border-radius:2px}
.tl span.u{background:var(--accent)}
.cmp{display:grid;grid-template-columns:1fr 1fr;gap:1rem}
.cmp h3{margin:.2rem 0;font-size:.95rem}.cmp .A{color:var(--a)}.cmp .B{color:var(--b)}
table{width:100%;border-collapse:collapse;font-size:.88rem}td,th{padding:.2rem .3rem;text-align:left}
th{color:var(--muted);font-weight:600}tr+tr td{border-top:1px solid var(--line)}
.note{font-size:.78rem;color:var(--muted);margin:.3rem 0}
</style></head><body><div class="wrap">
<p><a href="/">&larr; morfAnalytics</a></p>
<h1>Analyse de la phototh&egrave;que</h1>
<p class="muted">Exploration de la pratique &agrave; partir des donn&eacute;es de morfPhoto (source de v&eacute;rit&eacute;).
Un fichier pr&eacute;sent n'implique ni appartenance ni usage&nbsp;: affinez le p&eacute;rim&egrave;tre avec les filtres.</p>
<div id="app"><p class="muted">Chargement des donn&eacute;es&hellip;</p></div>
</div>
<script>
"use strict";
// ---- Etat de filtre UNIQUE : une seule selection pilote toute la page ----------
const LS_KEY="morfanalytics.photo.excludedCameras";
function loadExcluded(){try{return JSON.parse(localStorage.getItem(LS_KEY)||"[]")}catch(e){return []}}
function saveExcluded(s){try{localStorage.setItem(LS_KEY,JSON.stringify([...s]))}catch(e){}}
// Filtres MULTI-CRITÈRES : plusieurs valeurs par dimension (OR à l'intérieur d'une
// dimension : plusieurs boîtiers, plusieurs ISO, plusieurs focales...), combinées
// entre dimensions (AND). Catégoriel = Set de valeurs ; numérique = tableau de
// plages [min,max,libellé].
const S={year:new Set(),month:new Set(),cam:new Set(),lens:new Set(),
         focal:[],aperture:[],iso:[],shutter:[],
         camExcl:new Set()};   // camExcl = boitiers hors de MA pratique (config + local)
const CMP={dim:null,a:null,b:null};   // mode comparaison A vs B
let D=null;   // donnees decodees

const $=s=>document.querySelector(s);
const fr=n=>new Intl.NumberFormat("fr-FR").format(n);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));

// ---- Chargement (transmet ?source= a /photo/data pour le handoff PhotoHub) ------
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
  D=decode(snap);
  // Exclusions effectives = politique du service (config) UNION choix local.
  S.camExcl=new Set([...(D.configExcl||[]),...loadExcluded()]);
  render();
}
// Decodage du format colonnaire+dictionnaires en structures pretes a filtrer.
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
          buckets:snap.focal_buckets||[],
          configExcl:snap.exclude_cameras||[],
          source:snap.source_url||"",fetched:snap.fetched_at||null};
}

// ---- Filtre : un fichier passe-t-il la selection courante ? -------------------
// Une valeur numérique tombe-t-elle dans AU MOINS une des plages sélectionnées ?
function inAnyRange(v,ranges){return v!==null&&ranges.some(r=>v>=r[0]&&v<=r[1]);}
// Une plage est-elle déjà sélectionnée (comparaison sur min/max/libellé) ?
function rangeOn(ranges,k){return ranges.some(x=>x[0]===k[0]&&x[1]===k[1]&&x[2]===k[2]);}
function rmRange(arr,r){const i=arr.findIndex(x=>x[0]===r[0]&&x[1]===r[1]&&x[2]===r[2]);if(i>=0)arr.splice(i,1);}
function matches(i){
  const c=D.cols;
  // Chaque dimension : OR interne (un Set/tableau non vide => la valeur doit
  // correspondre à AU MOINS un critère), AND entre dimensions.
  if(S.year.size && !S.year.has(D.years[i])) return false;
  if(S.month.size && !S.month.has(D.months[i])) return false;
  if(S.cam.size && (c.camera[i]===null || !S.cam.has(c.camera[i]))) return false;
  // Exclusion de boitier : ne retire QUE les boitiers connus et exclus ; une photo
  // sans boitier connu n'est jamais ecartee par ce critere.
  if(S.camExcl.size && c.camera[i]!==null && S.camExcl.has(D.dict.camera[c.camera[i]])) return false;
  if(S.lens.size && (c.lens[i]===null || !S.lens.has(c.lens[i]))) return false;
  if(S.focal.length && !inAnyRange(c.focal_length[i],S.focal)) return false;
  if(S.aperture.length && !inAnyRange(c.aperture[i],S.aperture)) return false;
  if(S.iso.length && !inAnyRange(c.iso[i],S.iso)) return false;
  if(S.shutter.length && !inAnyRange(c.shutter_speed_s[i],S.shutter)) return false;
  return true;
}
function filtered(){const out=[];for(let i=0;i<D.n;i++)if(matches(i))out.push(i);return out;}

// ---- Agregations sur un sous-ensemble d'index --------------------------------
function countDict(idx,colName,dictName){
  const col=D.cols[colName], names=D.dict[dictName]||[];
  const m=new Map(); let known=0;
  for(const i of idx){const k=col[i]; if(k===null)continue; known++; m.set(k,(m.get(k)||0)+1);}
  const rows=[...m.entries()].map(([k,count])=>({key:k,label:names[k]??("#"+k),count}));
  rows.sort((a,b)=>b.count-a.count||String(a.label).localeCompare(b.label));
  return {rows,known};
}
function countBy(idx,arr){
  const m=new Map(); let known=0;
  for(const i of idx){const k=arr[i]; if(k===null||k===undefined)continue; known++; m.set(k,(m.get(k)||0)+1);}
  return {map:m,known};
}
function histo(idx,colName,ranges){
  const col=D.cols[colName]; const counts=ranges.map(()=>0); let known=0;
  for(const i of idx){const v=col[i]; if(v===null)continue; known++;
    for(let r=0;r<ranges.length;r++){if(v>=ranges[r][0]&&v<=ranges[r][1]){counts[r]++;break;}}}
  return {counts,known};
}
// Valeurs brutes non nulles d'une colonne sur un sous-ensemble (pour stats).
function values(idx,colName){const c=D.cols[colName];const out=[];for(const i of idx){const v=c[i];if(v!==null)out.push(v);}return out;}
function median(a){if(!a.length)return null;const s=[...a].sort((x,y)=>x-y);const m=s.length>>1;return s.length%2?s[m]:(s[m-1]+s[m])/2;}
function mean(a){if(!a.length)return null;return a.reduce((x,y)=>x+y,0)/a.length;}
// Focales exactes (arrondies au mm) les plus frequentes : revele qu'un zoom vit
// surtout a ses extremes, sans le presupposer.
function topFocals(idx,limit){
  const c=D.cols.focal_length; const m=new Map();
  for(const i of idx){const f=c[i]; if(f===null)continue; const k=Math.round(f); m.set(k,(m.get(k)||0)+1);}
  return [...m.entries()].map(([k,count])=>({label:k+" mm",count,k:[k-0.5,k+0.5,k+" mm"]}))
                         .sort((a,b)=>b.count-a.count).slice(0,limit||12);
}
function focalGroups(idx){
  const ranges=(D.buckets||[]).map(x=>[x.min,x.max,x.label]);
  const {counts,known}=histo(idx,"focal_length",ranges);
  const rows=ranges.map((r,k)=>({label:r[2],count:counts[k],k:[r[0],r[1],r[2]]})).filter(r=>r.count>0);
  return {rows,known};
}
// Plages usuelles (INTERPRETATION cote analyse, non imposee a la donnee brute).
const ISO_RANGES=[[0,100,"≤100"],[101,200,"125-200"],[201,400,"250-400"],
  [401,800,"500-800"],[801,1600,"1000-1600"],[1601,3200,"2000-3200"],
  [3201,6400,"4000-6400"],[6401,1e9,"≥8000"]];
const APERTURE_RANGES=[[0,1.9,"< f/2"],[1.9,2.9,"f/2-2.8"],[2.9,4.1,"f/2.8-4"],
  [4.1,5.7,"f/4-5.6"],[5.7,8.1,"f/5.6-8"],[8.1,11.1,"f/8-11"],[11.1,16.1,"f/11-16"],[16.1,1e9,"> f/16"]];
const SHUTTER_RANGES=[[0,0.001,"≤1/1000"],[0.001,0.004,"1/1000-1/250"],[0.004,0.008,"1/250-1/125"],
  [0.008,0.0167,"1/125-1/60"],[0.0167,0.033,"1/60-1/30"],[0.033,0.125,"1/30-1/8"],
  [0.125,1,"1/8-1s"],[1,1e9,"> 1s"]];
function fmtShutter(s){if(s===null)return "—";return s<1?("1/"+Math.round(1/s)):(s+" s");}
function fmtF(v,d){return v===null?"—":v.toFixed(d===undefined?1:d);}

// ---- Rendu -------------------------------------------------------------------
function bars(rows,opts){
  opts=opts||{};
  if(!rows.length) return '<p class="muted">aucune donn&eacute;e</p>';
  const max=Math.max(1,...rows.map(r=>r.count));
  return rows.map(r=>{
    const pct=Math.round(r.count*100/max);
    const on=opts.isOn&&opts.isOn(r)?" on":"";
    // data-ex porte le NOM du boitier (stable), pas l'index de dictionnaire.
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
function chip(label,onClear){const id="c"+(chip._n=(chip._n||0)+1);chip._h=chip._h||{};chip._h[id]=onClear;
  return '<span class="chip" data-chip="'+id+'"><b>'+esc(label)+'</b> &times;</span>';}

function activeChips(){
  chip._h={};
  const cs=[];
  const pad=m=>String(m).padStart(2,"0");
  // Un chip PAR valeur sélectionnée (multi-critères) : chacun se retire seul.
  [...S.year].sort((a,b)=>a-b).forEach(y=>cs.push(chip("Année "+y,()=>S.year.delete(y))));
  [...S.month].sort((a,b)=>a-b).forEach(m=>cs.push(chip("Mois "+pad(m),()=>S.month.delete(m))));
  [...S.cam].forEach(k=>cs.push(chip("Boîtier "+(D.dict.camera[k]||k),()=>S.cam.delete(k))));
  [...S.lens].forEach(k=>cs.push(chip("Objectif "+(D.dict.lens[k]||k),()=>S.lens.delete(k))));
  S.focal.forEach(r=>cs.push(chip("Focale "+r[2],()=>rmRange(S.focal,r))));
  S.aperture.forEach(r=>cs.push(chip("Ouverture "+r[2],()=>rmRange(S.aperture,r))));
  S.iso.forEach(r=>cs.push(chip("ISO "+r[2],()=>rmRange(S.iso,r))));
  S.shutter.forEach(r=>cs.push(chip("Vitesse "+r[2],()=>rmRange(S.shutter,r))));
  for(const c of S.camExcl)cs.push(chip("exclu: "+c,()=>{S.camExcl.delete(c);
    const l=new Set(loadExcluded());l.delete(c);saveExcluded(l);}));
  if(!cs.length)return '<span class="muted">aucun filtre &mdash; corpus complet</span>';
  return cs.join("")+' <button class="btn" id="reset">tout réinitialiser</button>';
}

function tiles(idx){
  const c=D.cols;
  let minT=null,maxT=null,exifUsable=0,incomplete=0;
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
  return '<div class="grid">'+
    t(fr(idx.length),"Photos (périmètre courant)")+
    t(esc(period),"Période couverte")+
    t(fr(cams.size),"Boîtiers")+t(fr(lenses.size),"Objectifs")+
    t(fr(focals.size),"Focales distinctes")+
    t(fr(exifUsable),"EXIF exploitable")+
    t(fr(incomplete),"Métadonnées incomplètes")+
    '</div>';
}

// Tuiles de tendance centrale (mediane) : ce vers quoi la pratique gravite.
function statTiles(idx){
  const f=values(idx,"focal_length"), a=values(idx,"aperture"),
        s=values(idx,"iso"), sh=values(idx,"shutter_speed_s");
  const t=(num,lab,kn)=>'<div class="tile"><div class="number mono">'+num+
    '</div><div class="sub">'+lab+' &middot; '+fr(kn)+' conn.</div></div>';
  return '<div class="grid">'+
    t(f.length?Math.round(median(f))+" mm":"—","Focale médiane",f.length)+
    t(a.length?"f/"+fmtF(median(a),1):"—","Ouverture médiane",a.length)+
    t(s.length?fr(Math.round(median(s))):"—","ISO médian",s.length)+
    t(sh.length?fmtShutter(median(sh)):"—","Vitesse médiane",sh.length)+
    '</div>';
}

// Chronologie d'usage par boitier : periode + annees actives. Fait apparaitre
// naturellement les changements de materiel.
function cameraTimeline(idx){
  const c=D.cols;
  let y0=Infinity,y1=-Infinity;
  const per=new Map();   // camIdx -> {count, years:Set, min, max}
  for(const i of idx){
    const k=c.camera[i]; if(k===null)continue;
    const y=D.years[i];
    let e=per.get(k); if(!e){e={count:0,years:new Set(),min:null,max:null};per.set(k,e);}
    e.count++;
    const t=c.taken_at[i];
    if(t){if(e.min===null||t<e.min)e.min=t; if(e.max===null||t>e.max)e.max=t;}
    if(y!==null){e.years.add(y); if(y<y0)y0=y; if(y>y1)y1=y;}
  }
  if(!per.size||!isFinite(y0)) return '<p class="muted">aucune donn&eacute;e datée</p>';
  const rows=[...per.entries()].sort((a,b)=>b[1].count-a[1].count);
  let head='<div class="row"><span class="lab muted">bo&icirc;tier</span><span class="tl">';
  for(let y=y0;y<=y1;y++)head+='<span title="'+y+'" style="background:transparent;color:var(--muted);font-size:.6rem;text-align:center">'+("'"+String(y).slice(2))+'</span>';
  head+='</span><span class="val muted">photos</span></div>';
  const body=rows.map(([k,e])=>{
    let cells="";
    for(let y=y0;y<=y1;y++)cells+='<span class="'+(e.years.has(y)?"u":"")+'" title="'+y+'"></span>';
    const name=D.dict.camera[k]||("#"+k);
    const per=(e.min&&e.max)?(e.min.slice(0,7)+"→"+e.max.slice(0,7)):"";
    return '<div class="row"><span class="lab" title="'+esc(name)+'">'+esc(name)+
      '</span><span class="tl">'+cells+'</span><span class="val mono">'+fr(e.count)+'</span></div>'+
      '<div class="note" style="margin-left:9.6rem">'+esc(per)+'</div>';
  }).join("");
  return head+body;
}

// Panneau comparaison A vs B sur une dimension, dans le perimetre filtre courant.
function compareBlock(idx){
  const dims=[["year","année"],["cam","boîtier"],["lens","objectif"]];
  const valuesFor=dim=>{
    const set=new Map();
    for(const i of idx){
      let v=null,lab=null;
      if(dim==="year"){v=D.years[i];lab=v;}
      else if(dim==="cam"){v=D.cols.camera[i];lab=v===null?null:D.dict.camera[v];}
      else{v=D.cols.lens[i];lab=v===null?null:D.dict.lens[v];}
      if(v===null||v===undefined)continue;
      set.set(String(v),lab);
    }
    return [...set.entries()].sort((a,b)=>String(a[1]).localeCompare(String(b[1])));
  };
  let h='<div class="row" style="gap:.5rem;flex-wrap:wrap"><label class="muted">Comparer par</label>'+
    '<select id="cmpdim"><option value="">—</option>'+
    dims.map(d=>'<option value="'+d[0]+'"'+(CMP.dim===d[0]?" selected":"")+'>'+d[1]+'</option>').join("")+'</select>';
  if(CMP.dim){
    const vals=valuesFor(CMP.dim);
    const sel=(id,cur)=>'<select id="'+id+'"><option value="">—</option>'+
      vals.map(v=>'<option value="'+esc(v[0])+'"'+(cur===v[0]?" selected":"")+'>'+esc(v[1])+'</option>').join("")+'</select>';
    h+='<span class="A">A</span>'+sel("cmpa",CMP.a)+'<span class="B">B</span>'+sel("cmpb",CMP.b);
  }
  h+='</div>';
  if(CMP.dim&&CMP.a!==null&&CMP.b!==null&&CMP.a!==""&&CMP.b!==""){
    const pick=val=>idx.filter(i=>{
      if(CMP.dim==="year")return String(D.years[i])===val;
      if(CMP.dim==="cam"){const k=D.cols.camera[i];return k!==null&&String(k)===val;}
      const k=D.cols.lens[i];return k!==null&&String(k)===val;
    });
    const A=pick(CMP.a),B=pick(CMP.b);
    const labOf=v=>{if(CMP.dim==="year")return v;if(CMP.dim==="cam")return D.dict.camera[+v];return D.dict.lens[+v];};
    const metric=(idx2)=>{
      const f=values(idx2,"focal_length"),a=values(idx2,"aperture"),s=values(idx2,"iso"),sh=values(idx2,"shutter_speed_s");
      const tf=topFocals(idx2,3).map(x=>x.label).join(", ");
      return {n:idx2.length,mf:f.length?Math.round(median(f))+" mm":"—",
        ma:a.length?"f/"+fmtF(median(a),1):"—",mi:s.length?fr(Math.round(median(s))):"—",
        ms:sh.length?fmtShutter(median(sh)):"—",tf:tf||"—"};
    };
    const mA=metric(A),mB=metric(B);
    const rowM=(lab,x,y)=>'<tr><td class="muted">'+lab+'</td><td class="mono">'+x+'</td><td class="mono">'+y+'</td></tr>';
    h+='<table style="margin-top:.6rem"><tr><th></th><th class="A">'+esc(labOf(CMP.a))+'</th><th class="B">'+esc(labOf(CMP.b))+'</th></tr>'+
      rowM("Photos",fr(mA.n),fr(mB.n))+rowM("Focale médiane",mA.mf,mB.mf)+
      rowM("Ouverture médiane",mA.ma,mB.ma)+rowM("ISO médian",mA.mi,mB.mi)+
      rowM("Vitesse médiane",mA.ms,mB.ms)+rowM("Top focales",mA.tf,mB.tf)+'</table>';
  } else {
    h+='<p class="note">Choisir une dimension puis deux valeurs pour comparer deux sous-ensembles (dans le p&eacute;rim&egrave;tre filtr&eacute; courant).</p>';
  }
  return h;
}

function render(){
  const idx=filtered(), total=idx.length;

  const yq=countBy(idx,D.years);
  const yearRows=[...yq.map.entries()].sort((a,b)=>a[0]-b[0]).map(([y,count])=>({label:String(y),count,k:y}));
  const mq=countBy(idx,D.months);
  const monthNames=["","janv","févr","mars","avr","mai","juin","juil","août","sept","oct","nov","déc"];
  const monthRows=[];for(let m=1;m<=12;m++){const n=mq.map.get(m)||0;if(n)monthRows.push({label:monthNames[m],count:n,k:m});}

  const camAgg=countDict(idx,"camera","camera");
  const lensAgg=countDict(idx,"lens","lens");
  const fg=focalGroups(idx);
  const isoH=histo(idx,"iso",ISO_RANGES), apH=histo(idx,"aperture",APERTURE_RANGES), shH=histo(idx,"shutter_speed_s",SHUTTER_RANGES);
  const rowsFromH=(R,H)=>R.map((r,k)=>({label:r[2],count:H.counts[k],k:[r[0],r[1],r[2]]})).filter(r=>r.count>0);
  const isoRows=rowsFromH(ISO_RANGES,isoH), apRows=rowsFromH(APERTURE_RANGES,apH), shRows=rowsFromH(SHUTTER_RANGES,shH);
  const fgRows=fg.rows;
  const topF=topFocals(idx,12);

  let h="";
  h+='<section class="card"><div class="chips" id="chips">'+activeChips()+'</div></section>';
  h+=tiles(idx);
  h+='<h2>Tendances (médianes)</h2>'+statTiles(idx);

  h+='<div class="cols">';
  h+='<div><h2>Par année</h2><div class="card">'+denom(yq.known,total)+
     bars(yearRows,{filterKey:"year",isOn:r=>S.year.has(r.k)})+'</div></div>';
  h+='<div><h2>Par mois</h2><div class="card">'+denom(mq.known,total)+
     bars(monthRows,{filterKey:"month",isOn:r=>S.month.has(r.k)})+'</div></div>';
  h+='</div>';

  h+='<div class="cols">';
  h+='<div><h2>Boîtiers</h2><div class="card">'+denom(camAgg.known,total)+
     '<p class="note">Clic&nbsp;: filtrer &middot; &empty;&nbsp;: exclure de ma pratique (mémorisé).</p>'+
     bars(camAgg.rows,{filterKey:"cam",isOn:r=>S.cam.has(r.key),excludable:true,isExcluded:r=>S.camExcl.has(r.label)})+'</div></div>';
  h+='<div><h2>Objectifs</h2><div class="card">'+denom(lensAgg.known,total)+
     bars(lensAgg.rows,{filterKey:"lens",isOn:r=>S.lens.has(r.key)})+
     (S.lens.size?'<p class="note">Boîtiers associés&nbsp;: '+
        esc(countDict(idx,"camera","camera").rows.map(r=>r.label+" ("+r.count+")").join(", ")||"—")+'</p>':"")+
     '</div></div>';
  h+='</div>';

  h+='<div class="cols">';
  h+='<div><h2>Focales usuelles</h2><div class="card">'+denom(fg.known,total)+
     bars(fgRows,{filterKey:"focal",isOn:r=>rangeOn(S.focal,r.k)})+
     '<p class="note">Regroupement interprété ; valeurs brutes souveraines dans morfPhoto.</p></div></div>';
  h+='<div><h2>Focales — détail (top)</h2><div class="card">'+denom(fg.known,total)+
     bars(topF,{filterKey:"focal",isOn:r=>rangeOn(S.focal,r.k)})+
     '<p class="note">Focales exactes (au mm) les plus fréquentes&nbsp;: révèle les positions réellement utilisées d\'un zoom.</p></div></div>';
  h+='</div>';

  h+='<div class="cols">';
  h+='<div><h2>Sensibilité ISO</h2><div class="card">'+denom(isoH.known,total)+
     bars(isoRows,{filterKey:"iso",isOn:r=>rangeOn(S.iso,r.k)})+'</div></div>';
  h+='<div><h2>Ouvertures</h2><div class="card">'+denom(apH.known,total)+
     bars(apRows,{filterKey:"aperture",isOn:r=>rangeOn(S.aperture,r.k)})+'</div></div>';
  h+='</div>';

  h+='<div class="cols">';
  h+='<div><h2>Vitesses d\'obturation</h2><div class="card">'+denom(shH.known,total)+
     bars(shRows,{filterKey:"shutter",isOn:r=>rangeOn(S.shutter,r.k)})+'</div></div>';
  h+='<div><h2>Boîtiers — chronologie</h2><div class="card">'+cameraTimeline(idx)+'</div></div>';
  h+='</div>';

  h+='<h2>Comparaison</h2><div class="card">'+compareBlock(idx)+'</div>';

  h+='<div class="card"><button class="btn" id="reload">recharger les données</button>'+
     '<div class="denom">'+(D.fetched?('actualisé '+esc(D.fetched)):'')+'<br>source&nbsp;: '+esc(D.source||"")+'</div></div>';

  $("#app").innerHTML=h;
  wire();
}

// Ajoute/retire un critère (multi-critères). Reclic sur un critère actif le retire.
// Catégoriel (year/month/cam/lens) => Set ; numérique (focal/aperture/iso/shutter)
// => tableau de plages.
function toggleFilter(field,val){
  if(field==="year"||field==="month"||field==="cam"||field==="lens"){
    if(S[field].has(val))S[field].delete(val);else S[field].add(val);
  }else{
    rangeOn(S[field],val)?rmRange(S[field],val):S[field].push(val);
  }
}
function wire(){
  document.querySelectorAll("[data-f]").forEach(el=>{
    el.addEventListener("click",ev=>{
      if(ev.target.classList.contains("ex"))return;
      toggleFilter(el.getAttribute("data-f"),JSON.parse(el.getAttribute("data-k"))); render();
    });
  });
  document.querySelectorAll(".ex").forEach(el=>{
    el.addEventListener("click",ev=>{
      ev.stopPropagation();
      const name=el.getAttribute("data-ex");
      const local=new Set(loadExcluded());
      if(S.camExcl.has(name)){S.camExcl.delete(name);local.delete(name);}
      else{S.camExcl.add(name);local.add(name);}
      saveExcluded(local); render();
    });
  });
  document.querySelectorAll("[data-chip]").forEach(el=>{
    el.addEventListener("click",()=>{const fn=chip._h[el.getAttribute("data-chip")];if(fn)fn();render();});
  });
  const rs=$("#reset");if(rs)rs.addEventListener("click",()=>{
    S.year.clear();S.month.clear();S.cam.clear();S.lens.clear();
    S.focal.length=0;S.aperture.length=0;S.iso.length=0;S.shutter.length=0; render();});
  const rl=$("#reload");if(rl)rl.addEventListener("click",reload);
  const cd=$("#cmpdim");if(cd)cd.addEventListener("change",e=>{CMP.dim=e.target.value||null;CMP.a=CMP.b=null;render();});
  const ca=$("#cmpa");if(ca)ca.addEventListener("change",e=>{CMP.a=e.target.value;render();});
  const cb=$("#cmpb");if(cb)cb.addEventListener("change",e=>{CMP.b=e.target.value;render();});
}

reload();
</script>
</body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
