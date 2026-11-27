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
<!--theme-head-->
<title>morfAnalytics - Photo</title><style>
:root{--a:var(--s-a);--b:var(--s-b)}
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
.row.click:hover{background:var(--hover)}.row.on{background:var(--sel)}
.lab{width:9rem;text-align:right;color:var(--soft);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-size:.9rem}
.bar{flex:1;background:var(--track);border-radius:6px;overflow:hidden}
.bar i{display:block;height:.85rem;background:var(--accent)}
.val{width:5.5rem;color:var(--muted);font-size:.85rem;text-align:right}
.ex{width:1.6rem;text-align:center;cursor:pointer;color:var(--faint);font-weight:700;user-select:none}
.ex:hover{color:var(--warn)}.ex.excluded{color:var(--warn)}
.fg{margin:.2rem 0}.fg b{color:var(--soft);font-size:.85rem;margin-right:.4rem}
.chip{display:inline-block;background:var(--chip);border:1px solid var(--chip-line);color:var(--chip-ink);border-radius:999px;padding:.1rem .55rem;font-size:.82rem;cursor:pointer;margin:.15rem .2rem .15rem 0}
.chip:hover{background:var(--chip-hover)}.chip.excl{background:var(--excl);border-color:var(--excl-line);color:var(--excl-ink)}
.btn{background:var(--field);border:1px solid var(--line);color:var(--soft);border-radius:8px;padding:.3rem .7rem;cursor:pointer;font-size:.85rem;margin:.1rem .2rem 0 0}
.btn:hover{border-color:var(--accent);color:var(--ink)}
.denom{font-size:.78rem;color:var(--muted);margin:.1rem 0 .5rem}
.mono{font-variant-numeric:tabular-nums}
#map{width:100%;height:auto;border:1px solid var(--line);border-radius:8px;background:var(--card);cursor:crosshair}
svg.lc{width:100%;height:auto;max-height:240px}
table.heat td.h{background:transparent}table.heat td.c{text-align:center;font-size:.78rem;min-width:2.6rem}
tr.click{cursor:pointer}tr.click:hover{background:var(--hover)}tr.on{background:var(--sel)}
td.n,th.n{text-align:right}
select,input[type=number]{background:var(--field);border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.25rem .4rem;font-size:.85rem}
.sources{margin:.3rem 0 .9rem;line-height:1.9}
.srcchip{display:inline-block;margin:.1rem .3rem;padding:.15rem .5rem;border:1px solid var(--line);border-radius:8px;cursor:pointer;font-size:.85rem;user-select:none}
.srcchip.on{border-color:var(--pick-line);background:var(--pick)}
.srcchip.off{opacity:.55}
.srcchip input{vertical-align:middle;margin-right:.35rem}
.tabs{display:flex;gap:.4rem;margin:.2rem 0 1rem;border-bottom:1px solid var(--line)}
.tab{padding:.45rem .9rem;border:1px solid var(--line);border-bottom:none;border-radius:8px 8px 0 0;background:transparent;color:var(--muted);cursor:pointer;font-size:.95rem}
.tab.on{background:var(--pick);color:var(--ink);border-color:var(--pick-line);font-weight:600}
.ownlist{max-height:min(60vh,26rem);overflow:auto;border:1px solid var(--line);border-radius:8px;padding:.4rem .6rem;margin:.5rem 0;columns:2;column-gap:1.4rem}
.ownlist label{display:block;break-inside:avoid;padding:.12rem 0;cursor:pointer;font-size:.9rem}
.ownlist input{vertical-align:middle;margin-right:.45rem}
@media(max-width:640px){.ownlist{columns:1}}
.sec{border:1px solid var(--line);border-radius:10px;margin:.7rem 0}
.sec>summary{cursor:pointer;padding:.6rem .9rem;font-size:1.15rem;font-weight:600;list-style:none}
.sec>summary::-webkit-details-marker{display:none}
.sec>summary::before{content:"\25B8";display:inline-block;margin-right:.5rem;color:var(--muted)}
.sec[open]>summary::before{content:"\25BE"}
.sec>summary:hover{color:var(--pick-line)}
.secbody{padding:0 .9rem .8rem}
.secbody h3{margin:.9rem 0 .3rem;font-size:1rem}
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
<!--nav-back-->
<h1>Analyse de la phototh&egrave;que <span id="vb" class="vb"></span><!--theme-toggle--></h1>
<p class="muted">Poser des questions au corpus&nbsp;: croiser boîtiers, focales, ISO, ouvertures,
vitesses, périodes&hellip; La donnée reste souveraine dans morfPhoto ; ici on l'explore.</p>
<div id="sources" class="sources"><span class="muted">Recherche des postes&hellip;</span></div>
<div class="tabs" id="tabs">
  <button class="tab on" data-tab="explore">Exploration</button>
  <button class="tab" id="tablib" data-tab="library" style="display:none">Tri et retouche</button>
  <button class="tab" data-tab="config">Configuration</button>
</div>
<div id="app"><p class="muted">Chargement des donn&eacute;es&hellip;</p></div>
</div>
<script>
"use strict";
const LS_KEY="morfanalytics.photo.excludedCameras";
function loadExcluded(){try{return JSON.parse(localStorage.getItem(LS_KEY)||"[]")}catch(e){return []}}
function saveExcluded(s){try{localStorage.setItem(LS_KEY,JSON.stringify([...s]))}catch(e){}}

// Boîtiers POSSÉDÉS (liste blanche) : on choisit explicitement son matériel dans l'onglet
// Configuration, plutôt que d'exclure ceux qu'on n'a pas. Vide = aucun filtre (tous les
// boîtiers). Non vide = seules ces photos comptent (les autres boîtiers -- smartphone,
// boîtier emprunté à un mariage... -- sont écartés). Persisté, et enregistré dans les vues.
const LS_OWNED="morfanalytics.photo.ownedCameras";
function loadOwned(){try{return JSON.parse(localStorage.getItem(LS_OWNED)||"[]")}catch(e){return []}}
function saveOwned(s,persist){try{localStorage.setItem(LS_OWNED,JSON.stringify([...s]))}catch(e){}
  if(persist!==false) postPractice([...s]);}
function postPractice(list){
  fetch("/photo/practice",{method:"POST",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({owned_cameras:list})})
    .then(r=>r.json()).then(d=>{
      const el=$("#ownsave"); if(!el) return;
      if(d&&d.ok) el.textContent="Enregistré : "+fr((d.owned_cameras||list).length)+" boîtier(s) dans la configuration du service.";
      else el.textContent="Échec d'enregistrement : "+(d&&d.error?d.error:"réponse inattendue");
    }).catch(e=>{const el=$("#ownsave"); if(el) el.textContent="Échec d'enregistrement : "+e;});
}
function reloadPractice(){
  fetch("/photo/practice").then(r=>r.json()).then(d=>{
    S.camOwned=new Set(d.owned_cameras||[]);
    saveOwned(S.camOwned,false);
    render();
    const el=$("#ownsave"); if(el) el.textContent=S.camOwned.size
      ? ("Rechargé : "+fr(S.camOwned.size)+" boîtier(s).")
      : "Rechargé : aucun boîtier enregistré (tous comptent).";
  }).catch(e=>{const el=$("#ownsave"); if(el) el.textContent="Relecture impossible : "+e;});
}
let TAB="explore";   // onglet courant : "explore" | "library" | "config"

// --- Vues enregistrées : un jeu de filtres nommé, réappliquable d'un clic --------
// Cas type : « ma pratique réelle » qui exclut smartphones et boîtiers jamais possédés
// (photos d'autres photographes récupérées). On sérialise par VALEUR (libellés, pas
// index de dictionnaire) pour que la vue survive à un changement de postes analysés.
const LS_VIEWS="morfanalytics.photo.views";
function loadViews(){try{return JSON.parse(localStorage.getItem(LS_VIEWS)||"{}")||{}}catch(e){return {}}}
function saveViews(v){try{localStorage.setItem(LS_VIEWS,JSON.stringify(v))}catch(e){}}
function serializeFilters(){
  const names=d=>[...S[d]].map(k=>DIMS[d].lab(k));   // index -> libellé (boîtier, objectif, type, dossier)
  return {
    camExcl:[...S.camExcl], camOwned:[...S.camOwned],
    cam:names("cam"), lens:names("lens"), type:names("type"), folder:names("folder"),
    ctx:names("ctx"), subj:names("subj"),
    rate:names("rate"), rej:names("rej"), pick:names("pick"), edit:names("edit"), src:names("src"), lbl:names("lbl"), kw:names("kw"),
    year:[...S.year], month:[...S.month],
    focal:S.focal.slice(), aperture:S.aperture.slice(), iso:S.iso.slice(), shutter:S.shutter.slice(),
    period:{from:S.period.from,to:S.period.to}
  };
}
// Libellé -> clé dans le dataset COURANT (les libellés absents sont simplement ignorés).
function labelToKey(d,label){
  if(d==="cam")  return D.dict.camera.indexOf(label);
  if(d==="lens") return D.dict.lens.indexOf(label);
  if(d==="type") return D.dict.file_type.indexOf(label);
  if(d==="folder"){for(const k in D.folders)if(D.folders[k]===label)return isNaN(+k)?k:+k;return -1;}
  if(d==="ctx")  return D.dict.context?D.dict.context.indexOf(label):-1;
  if(d==="subj") return D.dict.subject?D.dict.subject.indexOf(label):-1;
  // Bibliothèque : les libellés sont ceux affichés, on retrouve la clé du dataset courant.
  if(d==="rate"){const m=/^(\d+) ★$/.exec(label);return m?+m[1]:-1;}
  if(d==="rej") return label==="rejetée"?1:(label==="non rejetée"?0:-1);
  if(d==="edit")return label==="retouchée"?1:(label==="non retouchée"?0:-1);
  if(d==="pick")return label==="retenue"?1:(label==="non retenue"?0:-1);
  if(d==="src"){const a=D.dict.source||[];for(let k=0;k<a.length;k++)if(a[k]===label||SRC_FR[a[k]]===label)return k;return -1;}
  if(d==="lbl"){const a=D.dict.color_label||[];for(let k=0;k<a.length;k++)if(a[k]===label||LABEL_FR[a[k]]===label)return k;return -1;}
  if(d==="kw")  return D.dict.keyword?D.dict.keyword.indexOf(label):-1;
  return -1;
}
// Persiste les exclusions LOCALES (hors périmètre imposé par la config) comme périmètre
// courant : elles survivent alors au rechargement, comme le ∅ des barres.
function persistExcl(){
  const cfg=new Set(D.configExcl||[]);
  saveExcluded(new Set([...S.camExcl].filter(c=>!cfg.has(c))));
}
function applyView(v){
  Object.assign(S,emptyFilters());
  S.camExcl=new Set(v.camExcl||[]);
  S.camOwned=new Set(v.camOwned||[]);
  saveOwned(S.camOwned,false);
  persistExcl();
  ["cam","lens","type","folder","ctx","subj","rate","rej","pick","edit","src","lbl","kw"].forEach(d=>(v[d]||[]).forEach(lab=>{
    const k=labelToKey(d,lab); if(k!==-1&&k!==null&&k!==undefined)S[d].add(k);
  }));
  (v.year||[]).forEach(y=>S.year.add(y));
  (v.month||[]).forEach(m=>S.month.add(m));
  S.focal=(v.focal||[]).slice(); S.aperture=(v.aperture||[]).slice();
  S.iso=(v.iso||[]).slice(); S.shutter=(v.shutter||[]).slice();
  S.period={from:(v.period&&v.period.from!==undefined)?v.period.from:null,
            to:(v.period&&v.period.to!==undefined)?v.period.to:null};
  render();
}
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
function fmtShutter(s){if(s===null||s===undefined)return "-";return s<1?("1/"+Math.round(1/s)):(s+" s");}
function fmtF(v,d){return (v===null||v===undefined)?"-":v.toFixed(d===undefined?1:d);}

// ---- Filtres : état multi-critères ------------------------------------------
// Catégoriel = Set de clés ; numérique = tableau de plages [min,max,libellé].
// period = fenêtre continue d'années (from..to) sur taken_at. camExcl = périmètre.
function emptyFilters(){return {cam:new Set(),lens:new Set(),type:new Set(),folder:new Set(),
  ctx:new Set(),subj:new Set(),
  rate:new Set(),rej:new Set(),pick:new Set(),edit:new Set(),src:new Set(),lbl:new Set(),kw:new Set(),place:new Set(),
  year:new Set(),month:new Set(),focal:[],aperture:[],iso:[],shutter:[],
  period:{from:null,to:null},camExcl:new Set(),camOwned:new Set()};}
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
    ctx:   {label:"Contexte", kind:"cat", col:"context", key:i=>(D.cols.context?D.cols.context[i]:null), lab:k=>((D.dict.context&&D.dict.context[k])||("#"+k))},
    subj:  {label:"Sujet",    kind:"cat", col:"subject", key:i=>(D.cols.subject?D.cols.subject[i]:null), lab:k=>((D.dict.subject&&D.dict.subject[k])||("#"+k))},
    year:  {label:"Année",    kind:"cat", key:i=>D.years[i],  lab:k=>String(k)},
    month: {label:"Mois",     kind:"cat", key:i=>D.months[i], lab:k=>MONTHS[k]||("#"+k)},
    // Bibliothèque (morfphoto-library/1) : null = inconnu, jamais une valeur par défaut.
    // `edit` s'appuie sur edited_any (une des versions retouchée) pour ne pas compter les copies.
    rate:  {label:"Note",     kind:"cat", key:i=>libVal("rating",i),   lab:k=>k+" ★"},
    rej:   {label:"Rejet",    kind:"cat", key:i=>libBool("rejected",i), lab:k=>k?"rejetée":"non rejetée"},
    // Retenue : drapeau posé dans Lightcraft (« pick »). Inconnu si aucun sidecar n'en parle.
    pick:  {label:"Sélection", kind:"cat", key:i=>libBool("picked",i), lab:k=>k?"retenue":"non retenue"},
    edit:  {label:"Retouche", kind:"cat", key:i=>libBool("edited_any",i), lab:k=>k?"retouchée":"non retouchée"},
    src:   {label:"Logiciel", kind:"cat", key:i=>libVal("source",i),   lab:k=>{const n=(D.dict.source||[])[k];return SRC_FR[n]||n||("#"+k);}},
    lbl:   {label:"Étiquette",kind:"cat", multi:true, keys:i=>libVal("color_labels",i), lab:k=>{const n=(D.dict.color_label||[])[k];return LABEL_FR[n]||n||("#"+k);}},
    kw:    {label:"Mot-clé",  kind:"cat", multi:true, keys:i=>libVal("keywords",i),     lab:k=>(D.dict.keyword&&D.dict.keyword[k])??("#"+k)},
    // Lieu : case de la grille géographique (taille réglable), null si la photo n'a pas de position.
    place: {label:"Lieu",     kind:"cat", key:i=>D.placeKeys?D.placeKeys[i]:null, lab:placeLab},
    focal: {label:"Focale",   kind:"range", col:"focal_length", ranges:()=>(D.buckets||[]).map(b=>[b.min,b.max,b.label])},
    aperture:{label:"Ouverture",kind:"range", col:"aperture", ranges:()=>APERTURE_RANGES},
    iso:   {label:"ISO",      kind:"range", col:"iso", ranges:()=>ISO_RANGES},
    shutter:{label:"Vitesse", kind:"range", col:"shutter_speed_s", ranges:()=>SHUTTER_RANGES},
  };
}
const CAT_DIMS=["cam","lens","type","folder","ctx","subj","year","month"];
const LIB_DIMS=["rate","rej","pick","edit","src","lbl","kw"];
const FDIMS=CAT_DIMS.concat(LIB_DIMS,["place"]);   // toutes les dimensions catégorielles filtrables
const LABEL_FR={red:"rouge",yellow:"jaune",green:"vert",blue:"bleu",purple:"violet"};
const SRC_FR={darktable:"darktable",xmp:"XMP générique",dxo:"DxO PhotoLab",lightcraft:"Lightcraft",file:"XMP embarqué"};
// Lecture d'une colonne de la bibliothèque : null si ce morfPhoto ne la fournit pas.
function libVal(col,i){const c=D.cols[col];if(!c)return null;const v=c[i];return v===undefined?null:v;}
function libBool(col,i){const v=libVal(col,i);return v===null?null:(v?1:0);}
const RANGE_DIMS=["focal","aperture","iso","shutter"];

// ---- Sélecteur de postes (multi-sources) -------------------------------------
// Chaque morfPhoto a sa base par machine ; on choisit lesquelles inclure. La liste
// vient de /photo/sources (déclarés + découverts par beacon). L'analyse combine les
// postes cochés et DÉDOUBLONNE côté service (une photo indexée sur deux postes ne
// compte qu'une fois). Changer la sélection recharge en temps réel.
let SOURCES=[];        // [{url,host,online,configured,local,aliases}]
let SEL=new Set();     // urls cochées

function hostnameOf(u){
  try{return new URL(u).hostname.toLowerCase();}catch(e){return "";}
}
function isLoopHost(h){
  return !h||h==="localhost"||h==="::1"||h.startsWith("127.");
}
function isIpv4(h){return /^\d{1,3}(?:\.\d{1,3}){3}$/.test(h);}
function prettyHost(s){
  const raw=String(s.host||"");
  const name=raw.replace(/\s*\(local\)\s*$/i,"").replace(/\.local$/i,"");
  if(name && !isIpv4(name) && name!=="localhost")
    return s.local ? (name+" (local)") : name;
  const h=hostnameOf(s.url);
  if(h && !isIpv4(h) && !isLoopHost(h)) return h;
  return h||s.url;
}
function sameMachine(a,bUrl){
  const ua=a.url, ub=bUrl;
  if(ua===ub) return true;
  const ha=hostnameOf(ua), hb=hostnameOf(ub);
  if(ha && ha===hb) return true;
  const aliases=a.aliases||[];
  if(aliases.some(x=>x===ub||hostnameOf(x)===hb)) return true;
  // Handoff PhotoHub = IP LAN de CETTE machine, déjà listée en « local ».
  if(a.local && (isLoopHost(hb)||isIpv4(hb))) return true;
  if((isLoopHost(ha)||a.local) && isLoopHost(hb)) return true;
  return false;
}

function sourcesParam(){return [...SEL].map(encodeURIComponent).join(",");}

function renderSources(){
  const box=$("#sources"); if(!box) return;
  if(!SOURCES.length){box.innerHTML='<span class="muted">Aucun poste morfPhoto découvert. '+
    'Vérifiez qu’un morfPhoto tourne et s’annonce (capacité photo_index).</span>';return;}
  const chips=SOURCES.map(s=>{
    const on=SEL.has(s.url);
    const lab=esc(prettyHost(s))+(s.online?"":" (hors ligne)");
    return '<label class="srcchip'+(on?" on":"")+(s.online?"":" off")+'">'+
      '<input type="checkbox" data-url="'+esc(s.url)+'"'+(on?" checked":"")+'>'+lab+'</label>';
  }).join("");
  box.innerHTML='<b>Postes analysés :</b> '+chips+'<span id="srcinfo" class="muted"></span>';
  box.querySelectorAll('input[type=checkbox]').forEach(cb=>{
    cb.addEventListener("change",()=>{
      const u=cb.getAttribute("data-url");
      if(cb.checked)SEL.add(u);else SEL.delete(u);
      renderSources();          // reflète l'état coché tout de suite
      resetCatFilters();        // les index de dictionnaire changent avec l'ensemble des sources
      reload();                 // temps réel
    });
  });
}

// Les filtres CATÉGORIELS (boîtier, objectif, type, dossier) portent des index de
// dictionnaire qui n'ont de sens que pour un ensemble de sources donné : on les remet
// à zéro au changement de sélection. Les plages, l'année, le périmètre restent.
function resetCatFilters(){
  S.cam=new Set();S.lens=new Set();S.type=new Set();S.folder=new Set();
  S.ctx=new Set();S.subj=new Set();
}

function updateSrcInfo(snap){
  const el=$("#srcinfo"); if(!el) return;
  const parts=[];
  const dup=snap.duplicates_removed||0;
  if(dup) parts.push(dup+" doublon"+(dup>1?"s":"")+" écarté"+(dup>1?"s":""));
  (snap.sources||[]).forEach(s=>{ if(s.reachable===false) parts.push(esc(s.host||s.url)+" injoignable"); });
  el.textContent = parts.length ? ("  -  "+parts.join(" · ")) : "";
}

function loadSources(){
  fetch("/photo/sources").then(r=>r.json()).then(d=>{
    SOURCES=(d.items||[]);
    const handoff=new URLSearchParams(location.search).get("source");
    SEL=new Set();
    if(handoff){
      const hit=SOURCES.find(s=>sameMachine(s,handoff));
      if(hit) SEL.add(hit.url);
      else {
        let h=handoff; try{h=new URL(handoff).hostname;}catch(e){}
        SOURCES.unshift({url:handoff,host:h,online:true,configured:false,local:false,aliases:[handoff]});
        SEL.add(handoff);
      }
    } else {
      SOURCES.filter(s=>s.online).forEach(s=>SEL.add(s.url));
      if(!SEL.size&&SOURCES.length) SEL.add(SOURCES[0].url);
    }
    renderSources();
    reload();
  }).catch(()=>{ reload(); });
}

// ---- Chargement --------------------------------------------------------------
// Garde de séquence : cocher/décocher vite lance plusieurs requêtes ; la fusion est
// synchrone côté service, donc les réponses peuvent revenir dans le désordre. On ne rend
// QUE la réponse de la dernière requête émise (les périmées sont ignorées).
let reqSeq=0;
function reload(){
  // Requête pilotée par les postes cochés ; à défaut, on retombe sur ?source= (handoff)
  // ou la source périodique configurée (location.search éventuellement vide).
  const qs = SEL.size ? ("?sources="+sourcesParam()) : location.search;
  const my=++reqSeq;
  $("#app").innerHTML='<p class="muted">Chargement des donn&eacute;es&hellip;</p>';
  fetch("/photo/data"+qs).then(r=>r.json()).then(snap=>{ if(my===reqSeq)init(snap); }).catch(e=>{
    if(my!==reqSeq)return;
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
    updateSrcInfo(snap||{});
    return;
  }
  D=decode(snap); computePlaces(); buildDims();
  // L'onglet « Tri et retouche » n'existe que si au moins un morfPhoto fournit la
  // bibliotheque ; sinon un filtre ou un onglet vide laisserait croire a « aucune note ».
  $("#tablib").style.display=D.lib?"":"none";
  if(!D.lib&&TAB==="library")setTab("explore");
  const fromService=(D.ownedCameras||[]);
  S.camOwned=new Set(fromService.length?fromService:loadOwned());
  if(fromService.length) saveOwned(S.camOwned,false);
  S.camExcl=new Set([...(D.configExcl||[]),...loadExcluded()]);
  render();
  updateSrcInfo(snap);
}
function decode(snap){
  const ds=snap.dataset||{}, cols=ds.columns||{}, dict=ds.dictionaries||{};
  const nCols=(cols.taken_at||[]).length;
  const n=Math.max(ds.count||0,nCols);
  const years=new Array(n), months=new Array(n);
  const ta=cols.taken_at||[];
  for(let i=0;i<n;i++){
    const t=ta[i];
    if(typeof t==="string"&&t.length>=7){years[i]=+t.slice(0,4);months[i]=+t.slice(5,7);}
    else{years[i]=null;months[i]=null;}
  }
  return {n,cols,dict,years,months,lib:Array.isArray(cols.rating),gps:Array.isArray(cols.latitude),
          buckets:snap.focal_buckets||[], folders:ds.folders||{},
          configExcl:snap.exclude_cameras||[],
          ownedCameras:snap.owned_cameras||[],
          sources:snap.sources||[],
          duplicates:snap.duplicates_removed||0,
          source:snap.source_url||"", fetched:snap.fetched_at||null};
}

// ---- Moteur de filtre --------------------------------------------------------
function inAnyRange(v,ranges){return v!==null&&v!==undefined&&ranges.some(r=>v>=r[0]&&v<=r[1]);}
function rangeOn(ranges,k){return ranges.some(x=>x[0]===k[0]&&x[1]===k[1]&&x[2]===k[2]);}
function rmRange(arr,r){const i=arr.findIndex(x=>x[0]===r[0]&&x[1]===r[1]&&x[2]===r[2]);if(i>=0)arr.splice(i,1);}
// Un fichier passe-t-il un jeu de filtres F ? (OR interne, AND entre dimensions)
function matchesWith(i,F){
  for(const d of FDIMS){
    if(!F[d].size)continue;
    if(DIMS[d].multi){const a=DIMS[d].keys(i); if(!a||!a.some(k=>F[d].has(k)))return false;}   // l'un des mots-clés / étiquettes
    else{const k=DIMS[d].key(i); if(k===null||k===undefined||!F[d].has(k))return false;}
  }
  if(F.camExcl.size){const k=D.cols.camera[i]; if(k!==null&&F.camExcl.has(D.dict.camera[k]))return false;}
  // Boîtiers possédés (liste blanche) : si définie, un boîtier CONNU hors liste est écarté.
  // Les photos sans boîtier identifié (EXIF absent) passent toujours (souvent les vôtres).
  if(F.camOwned&&F.camOwned.size){const k=D.cols.camera[i]; if(k!==null&&!F.camOwned.has(D.dict.camera[k]))return false;}
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
function filteredWith(F){const o=[];for(let i=0;i<D.n;i++)if(matchesWith(i,F))o.push(i);return o;}
function filtered(){return filteredWith(S);}

// ---- Agrégations -------------------------------------------------------------
function countCat(idx,dim){
  const D_=DIMS[dim]; const m=new Map(); let known=0;
  for(const i of idx){
    if(D_.multi){   // une photo compte une fois par valeur ; liste vide = connue, sans valeur
      const a=D_.keys(i); if(a===null||a===undefined)continue; known++;
      for(const k of a)m.set(k,(m.get(k)||0)+1); continue;}
    const k=D_.key(i); if(k===null||k===undefined)continue; known++; m.set(k,(m.get(k)||0)+1);}
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
function values(idx,col){const c=D.cols[col];const out=[];for(const i of idx){const v=c[i];if(v!==null&&v!==undefined)out.push(v);}return out;}
function median(a){if(!a.length)return null;const s=[...a].sort((x,y)=>x-y);const m=s.length>>1;return s.length%2?s[m]:(s[m-1]+s[m])/2;}
function topFocals(idx,limit){
  const c=D.cols.focal_length,m=new Map();
  for(const i of idx){const f=c[i];if(f===null)continue;const k=Math.round(f);m.set(k,(m.get(k)||0)+1);}
  return [...m.entries()].map(([k,count])=>({label:k+" mm",count,k:[k-0.5,k+0.5,k+" mm"]}))
                         .sort((a,b)=>b.count-a.count).slice(0,limit||12);
}
function topLenses(idx,limit){return countCat(idx,"lens").rows.slice(0,limit||3).map(r=>r.label+" ("+r.count+")").join(", ")||"-";}

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
         ' photo(s) - '+fr(total-known)+' sans cette donn&eacute;e</div>';
}
function chip(label,cls,onClear){const id="c"+(chip._n=(chip._n||0)+1);chip._h=chip._h||{};chip._h[id]=onClear;
  return '<span class="chip'+(cls?" "+cls:"")+'" data-chip="'+id+'"><b>'+esc(label)+'</b> &times;</span>';}

// Sections génériques (même moteur, mêmes interactions partout).
function catCard(dim,extra){
  const idx=filtered(), a=countCat(idx,dim);
  return '<div class="card">'+denom(a.known,idx.length)+
    bars(a.rows,{filterKey:dim,isOn:r=>S[dim].has(r.key)})+(extra||'')+'</div>';
}
function rangeCard(dim){
  const idx=filtered(), a=histoRows(idx,dim);
  return '<div class="card">'+denom(a.known,idx.length)+
    bars(a.rows,{filterKey:dim,isOn:r=>rangeOn(S[dim],r.k)})+'</div>';
}

// ---- Presets d'analyse (contexte / sujet) ------------------------------------
// Raccourcis de filtres, JAMAIS une regle codee dans les donnees : un preset pose
// une combinaison context/subject dans les filtres, ensuite combinable et comparable
// comme n'importe quel autre filtre. « Focale naturelle » = DECOUVERTE + GENERAL est
// le preset de reference, a comparer aux autres (LIBRE+ANIMAUX, SPECIALISEE+ANIMAUX...).
// Le PERIMETRE de pratique (boitiers possedes/exclus) n'est pas un filtre : on le garde.
function applyPreset(ctxLabel,subjLabel){
  const owned=new Set(S.camOwned), excl=new Set(S.camExcl);
  Object.assign(S,emptyFilters());
  S.camOwned=owned; S.camExcl=excl;
  if(ctxLabel){const k=D.dict.context?D.dict.context.indexOf(ctxLabel):-1; if(k>=0)S.ctx.add(k);}
  if(subjLabel){const k=D.dict.subject?D.dict.subject.indexOf(subjLabel):-1; if(k>=0)S.subj.add(k);}
  render();
}
function presetsBar(){
  const P=[
    ["Focale naturelle","DECOUVERTE","GENERAL"],
    ["Découverte","DECOUVERTE",""],
    ["Libre","LIBRE",""],
    ["Animalier spontané","LIBRE","ANIMAUX"],
    ["Animalier préparé","SPECIALISEE","ANIMAUX"],
    ["Événements","EVENEMENT",""],
    ["Spectacles","SPECTACLE",""],
    ["Missions","MISSION",""],
  ];
  const btns=P.map(p=>'<button class="btn preset" data-pctx="'+esc(p[1])+'" data-psubj="'+esc(p[2])+'">'+esc(p[0])+'</button>').join("");
  return '<div class="fg"><b>Analyses prêtes</b> '+btns+
    ' <button class="btn" id="presetclr">tout le périmètre</button>'+
    '<p class="note">Raccourcis de filtres (contexte / sujet), ensuite combinables avec boîtier, '+
    'focale, ISO… « Focale naturelle » = DECOUVERTE + GENERAL, à comparer aux autres pour voir '+
    'si une focale est une préférence générale ou liée à un sujet. Enregistrez une combinaison '+
    'comme « Vue » pour la retrouver.</p></div>';
}

// ---- Filtres actifs (périmètre séparé, groupés par dimension) -----------------
function filtersPanel(){
  chip._h={};
  // Vues enregistrées : mémoriser le jeu de filtres courant pour le réappliquer.
  const views=loadViews(); const vnames=Object.keys(views).sort();
  let vh='<div class="fg"><b>Vues enregistrées</b> '
    +'<select id="viewsel"><option value="">- choisir -</option>'
    +vnames.map(n=>'<option value="'+esc(n)+'">'+esc(n)+'</option>').join("")+'</select> '
    +'<button class="btn" id="viewapply">Appliquer</button> '
    +'<button class="btn" id="viewsave">Enregistrer la sélection actuelle…</button> '
    +'<button class="btn" id="viewdel">Supprimer</button>'
    +(vnames.length?'':' <span class="muted">aucune pour l’instant</span>')
    +'</div>';
  // Périmètre de pratique : boîtiers possédés (réglés dans l'onglet Configuration).
  const total=(D.dict.camera||[]).length;
  let per=vh+'<div class="fg"><b>Périmètre</b> ';
  if(S.camOwned.size)
    per+='mon matériel &middot; <b>'+fr(S.camOwned.size)+'</b> boîtier(s) possédé(s) sur '+fr(total)
        +' &middot; <a href="#" id="gotoconf">Configuration</a>';
  else
    per+='<span class="muted">tous les boîtiers (aucun matériel déclaré) &middot; </span><a href="#" id="gotoconf">choisir mes boîtiers</a>';
  // Rappel des exclusions héritées (config serveur / anciennes vues), s'il y en a.
  if(S.camExcl.size) per+=' &middot; <span class="muted">exclus&nbsp;: '+fr(S.camExcl.size)+'</span>';
  per+='</div>';

  // Filtres analytiques, groupés par dimension.
  const lines=[];
  const pad=m=>String(m).padStart(2,"0");
  for(const d of FDIMS){
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
  if(!lines.length) filt+='<span class="muted">aucun - tout le périmètre</span></div>';
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
  const period=(minT&&maxT)?(minT.slice(0,10)+" → "+maxT.slice(0,10)):"-";
  const t=(num,lab)=>'<div class="tile"><div class="number mono">'+num+'</div><div class="sub">'+lab+'</div></div>';
  return '<div class="grid">'+t(fr(idx.length),"Photos (périmètre courant)")+t(esc(period),"Période couverte")+
    t(fr(cams.size),"Boîtiers")+t(fr(lenses.size),"Objectifs")+t(fr(focals.size),"Focales distinctes")+
    t(fr(exifUsable),"EXIF exploitable")+t(fr(incomplete),"Métadonnées incomplètes")+'</div>';
}
function statTiles(idx){
  const f=values(idx,"focal_length"),a=values(idx,"aperture"),s=values(idx,"iso"),sh=values(idx,"shutter_speed_s");
  const t=(num,lab,kn)=>'<div class="tile"><div class="number mono">'+num+'</div><div class="sub">'+lab+' &middot; '+fr(kn)+' conn.</div></div>';
  return '<div class="grid">'+
    t(f.length?Math.round(median(f))+" mm":"-","Focale médiane",f.length)+
    t(a.length?"f/"+fmtF(median(a),1):"-","Ouverture médiane",a.length)+
    t(s.length?fr(Math.round(median(s))):"-","ISO médian",s.length)+
    t(sh.length?fmtShutter(median(sh)):"-","Vitesse médiane",sh.length)+'</div>';
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
  mediso:"ISO médian",medshutter:"Vitesse médiane",avgrate:"Note moyenne",edrate:"Taux de retouche"};
const MX_DIMS=["cam","lens","type","folder","ctx","subj","year","month","focal","aperture","iso","shutter","rate","rej","pick","edit","src"];
const LIB_MX=["rate","rej","pick","edit","src"], LIB_MEASURES=["avgrate","edrate"];
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
  if(measure==="medfocal"){const m=median(values(idx,"focal_length"));return m===null?"-":Math.round(m)+"";}
  if(measure==="medaperture"){const m=median(values(idx,"aperture"));return m===null?"-":"f/"+fmtF(m,1);}
  if(measure==="mediso"){const m=median(values(idx,"iso"));return m===null?"-":fr(Math.round(m));}
  if(measure==="medshutter"){const m=median(values(idx,"shutter_speed_s"));return fmtShutter(m);}
  if(measure==="avgrate"){const r=values(idx,"rating");return r.length?(r.reduce((a,b)=>a+b,0)/r.length).toFixed(1)+" ★":"-";}
  if(measure==="edrate"){const e=values(idx,"edited_any");return e.length?Math.round(e.filter(x=>x).length*100/e.length)+" %":"-";}
  return "";
}
function matrixBlock(){
  const mxd=MX_DIMS.filter(d=>D.lib||!LIB_MX.includes(d));
  const opts=(cur)=>mxd.map(d=>'<option value="'+d+'"'+(cur===d?" selected":"")+'>'+DIMS[d].label+'</option>').join("");
  const mopts=Object.keys(MEASURES).filter(k=>MEASURES[k]&&(D.lib||!LIB_MEASURES.includes(k))).map(k=>'<option value="'+k+'"'+(MX.measure===k?" selected":"")+'>'+MEASURES[k]+'</option>').join("");
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
  ctx:new Set(S.ctx),subj:new Set(S.subj),
  rate:new Set(S.rate),rej:new Set(S.rej),pick:new Set(S.pick),edit:new Set(S.edit),src:new Set(S.src),lbl:new Set(S.lbl),kw:new Set(S.kw),place:new Set(S.place),
  year:new Set(S.year),month:new Set(S.month),focal:S.focal.map(r=>r.slice()),aperture:S.aperture.map(r=>r.slice()),
  iso:S.iso.map(r=>r.slice()),shutter:S.shutter.map(r=>r.slice()),period:{...S.period},camExcl:new Set(S.camExcl),camOwned:new Set(S.camOwned)};}
function describeFilters(F){
  const parts=[];
  for(const d of FDIMS)if(F[d].size)parts.push(DIMS[d].label+": "+[...F[d]].map(k=>DIMS[d].lab(k)).join("/"));
  for(const d of RANGE_DIMS)if(F[d].length)parts.push(DIMS[d].label+": "+F[d].map(r=>r[2]).join("/"));
  if(F.period.from!==null||F.period.to!==null)parts.push("Période: "+(F.period.from??"…")+"–"+(F.period.to??"…"));
  return parts.length?parts.join(" · "):"tout le périmètre";
}
function groupMetrics(F){
  const idx=filteredWith(F);
  const f=values(idx,"focal_length"),a=values(idx,"aperture"),s=values(idx,"iso"),sh=values(idx,"shutter_speed_s");
  return {n:idx.length,
    mf:f.length?Math.round(median(f))+" mm":"-", ma:a.length?"f/"+fmtF(median(a),1):"-",
    mi:s.length?fr(Math.round(median(s))):"-", ms:sh.length?fmtShutter(median(sh)):"-",
    tf:topFocals(idx,3).map(x=>x.label).join(", ")||"-", tl:topLenses(idx,3)};
}
// Lignes « bibliothèque » d'un tableau A/B (comparaison rapide ET groupes de filtres) : même
// calcul, mêmes dénominateurs. Une valeur inconnue n'entre jamais dans un taux ni dans une
// moyenne ; le nombre de photos connues est affiché à côté de chaque chiffre.
function libCompareRows(iA,iB){
  if(!D.lib) return "";
  const a=libStats(iA), b=libStats(iB);
  const r=(l,x,y)=>'<tr><td class="muted">'+l+'</td><td>'+x+'</td><td>'+y+'</td></tr>';
  const avg=s=>s.avg===null?"-":s.avg.toFixed(1).replace(".",",")+" ★ ("+fr(s.rated)+" notées)";
  const rate=(n,k)=>pctOf(n,k)+(k?" ("+fr(k)+" conn.)":"");
  const top=(idx,dim)=>countCat(idx,dim).rows.slice(0,3).map(x=>x.label+" ("+fr(x.count)+")").join(", ")||"-";
  return r("Note moyenne",avg(a),avg(b))+
    r("Retouchées",rate(a.ed,a.edK),rate(b.ed,b.edK))+
    r("Rejetées",rate(a.rej,a.rejK),rate(b.rej,b.rejK))+
    r("Retenues",rate(a.pick,a.pickK),rate(b.pick,b.pickK))+
    r("Avec mot-clé",rate(a.kwN,a.kwK),rate(b.kwN,b.kwK))+
    r("Étiquettes les plus fréquentes",top(iA,"lbl"),top(iB,"lbl"))+
    r("Mots-clés les plus utilisés",top(iA,"kw"),top(iB,"kw"))+
    r("Logiciels",top(iA,"src"),top(iB,"src"));
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
      r("Objectifs les plus utilisés",A.tl,B.tl)+libCompareRows(filteredWith(GRP.a),filteredWith(GRP.b))+'</table>';
  } else h+='<p class="note">Construisez un sous-ensemble avec les filtres, capturez-le comme Groupe A, changez les filtres, capturez le Groupe B.</p>';
  return h;
}

// ---- Comparaison rapide (une dimension, A vs B) ------------------------------
function quickCompare(idx){
  // Années, matériel, contexte et sujet photographiques ; avec la bibliothèque, aussi le logiciel,
  // l'état de retouche et la note (« mes photos à 5 étoiles contre celles à 3 »).
  const dims=[["year","année"],["cam","boîtier"],["lens","objectif"],["ctx","contexte"],["subj","sujet"]]
    .concat(D.lib?[["src","logiciel"],["edit","retouche"],["rate","note"]]:[]);
  const valuesFor=dim=>{const set=new Map();
    for(const i of idx){let v=null,lab=null;
      if(dim==="year"){v=D.years[i];lab=v;}else{v=DIMS[dim].key(i);lab=v===null?null:DIMS[dim].lab(v);}
      if(v===null||v===undefined)continue;set.set(String(v),lab);}
    return [...set.entries()].sort((a,b)=>String(a[1]).localeCompare(String(b[1])));};
  let h='<div class="controls"><label class="muted">Comparer par</label><select id="qcd"><option value="">-</option>'+
    dims.map(d=>'<option value="'+d[0]+'"'+(QC.dim===d[0]?" selected":"")+'>'+d[1]+'</option>').join("")+'</select>';
  if(QC.dim){const vals=valuesFor(QC.dim);
    const sel=(id,cur)=>'<select id="'+id+'"><option value="">-</option>'+
      vals.map(v=>'<option value="'+esc(v[0])+'"'+(cur===v[0]?" selected":"")+'>'+esc(v[1])+'</option>').join("")+'</select>';
    h+='<span class="A">A</span>'+sel("qca",QC.a)+'<span class="B">B</span>'+sel("qcb",QC.b);}
  h+='</div>';
  if(QC.dim&&QC.a&&QC.b){
    const pick=val=>idx.filter(i=>{if(QC.dim==="year")return String(D.years[i])===val;const k=DIMS[QC.dim].key(i);return k!==null&&String(k)===val;});
    const A=pick(QC.a),B=pick(QC.b);
    const labOf=v=>QC.dim==="year"?v:DIMS[QC.dim].lab(+v);
    const met=idx2=>{const f=values(idx2,"focal_length"),a=values(idx2,"aperture"),s=values(idx2,"iso"),sh=values(idx2,"shutter_speed_s");
      return {n:idx2.length,mf:f.length?Math.round(median(f))+" mm":"-",ma:a.length?"f/"+fmtF(median(a),1):"-",
        mi:s.length?fr(Math.round(median(s))):"-",ms:sh.length?fmtShutter(median(sh)):"-",tf:topFocals(idx2,3).map(x=>x.label).join(", ")||"-"};};
    const mA=met(A),mB=met(B),r=(l,x,y)=>'<tr><td class="muted">'+l+'</td><td>'+x+'</td><td>'+y+'</td></tr>';
    h+='<table><tr><th></th><th class="A">'+esc(labOf(QC.a))+'</th><th class="B">'+esc(labOf(QC.b))+'</th></tr>'+
      r("Photos",fr(mA.n),fr(mB.n))+r("Focale médiane",mA.mf,mB.mf)+r("Ouverture médiane",mA.ma,mB.ma)+
      r("ISO médian",mA.mi,mB.mi)+r("Vitesse médiane",mA.ms,mB.ms)+r("Top focales",mA.tf,mB.tf)+libCompareRows(A,B)+'</table>';
  }
  return h;
}
const QC={dim:null,a:null,b:null};

// ---- Onglet Configuration ----------------------------------------------------
// Boîtiers POSSÉDÉS : choix explicite de son matériel. Paramètre rarement touché, donc
// hors de la page d'exploration. La liste est une vraie liste de cases (pas un <select
// multiple> qui « remonte » au clic) dans un conteneur défilable : cocher une case ne
// re-rend PAS le panneau (le défilement reste en place), on met juste à jour l'état, la
// persistance et le compteur.
function configPanel(){
  const cams=(D.dict.camera||[]).slice().sort((a,b)=>String(a).localeCompare(String(b)));
  let h='<section class="card"><h2>Boîtiers possédés</h2>'
    +'<p class="muted">Cochez le matériel que vous possédez réellement. Les analyses ne '
    +'compteront alors que vos boîtiers ; les autres (smartphone, boîtier emprunté à un '
    +'mariage, photos d\'autres photographes…) sont écartés. <b>Aucune coche = tous les '
    +'boîtiers</b> (pas de filtre). Réglage mémorisé et enregistré dans les vues.</p>';
  if(!cams.length){
    h+='<p class="muted">Aucun boîtier dans le corpus courant.</p>';
    h+=coherencePanel();
    h+='</section>';
    return h;
  }
  h+='<div class="controls"><button class="btn" id="ownall">Tout cocher</button>'
    +'<button class="btn" id="ownnone">Tout décocher</button>'
    +'<button class="btn" id="ownsavebtn">Enregistrer</button>'
    +'<button class="btn" id="ownreload">Recharger</button>'
    +'<span class="muted" id="owncount"></span></div>';
  h+='<p class="muted" id="ownsave">Cochez, puis Enregistrer : la sélection est rappelée au prochain chargement, sur n\'importe quel navigateur.</p>';
  h+='<div class="ownlist" id="ownlist">'
    +cams.map(c=>'<label><input type="checkbox" data-cam="'+esc(c)+'"'
      +(S.camOwned.has(c)?" checked":"")+'>'+esc(c)+'</label>').join("")
    +'</div>';
  h+='<p class="muted">Astuce&nbsp;: une fois vos boîtiers cochés et enregistrés, vous pouvez encore les modifier ici, puis ré-enregistrer.</p>';
  h+=coherencePanel();
  h+='</section>';
  return h;
}
function coherencePanel(){
  const srcs=D.sources||[];
  const rows=[];
  srcs.forEach(s=>{
    const key=prettyHost({host:s.host,url:s.url,local:!!s.local});
    const i=rows.findIndex(x=>x.key===key);
    if(i<0) rows.push({key,s});
  });
  let h='<h2>Cohérence PhotoHub / analyse</h2>'
    +'<p class="muted">PhotoHub (morfPhoto) expose plusieurs compteurs. L\'analyse lit l\'export compact <code>dataset</code>. '
    +'Un écart entre « liste » et « dataset » vient de morfPhoto ; un dataset plus petit que PhotoHub signalait souvent un timeout trop court côté analyse.</p>';
  if(!rows.length){
    h+='<p class="muted">Ouvrez cette page avec au moins un poste coché pour comparer les compteurs.</p>';
    return h;
  }
  h+='<table><tr><th>Poste</th><th>Liste PhotoHub</th><th>Résumé (présents)</th><th>Export dataset</th><th>Gardées ici</th></tr>';
  rows.forEach(r=>{
    const s=r.s;
    const warn=(a,b)=> (a>=0&&b>=0&&a!==b)?' class="A"':'';
    h+='<tr><td>'+esc(r.key)+'</td>'
      +'<td'+warn(s.list_total,s.count)+'>'+fmtCount(s.list_total)+'</td>'
      +'<td'+warn(s.files_present,s.count)+'>'+fmtCount(s.files_present)+'</td>'
      +'<td>'+fmtCount(s.count)+'</td>'
      +'<td>'+fmtCount(s.kept)+'</td></tr>';
  });
  h+='</table>';
  if(D.duplicates) h+='<p class="muted">'+fr(D.duplicates)+' doublon(s) écarté(s) entre postes.</p>';
  h+='<p class="muted">Photos dans l\'analyse courante&nbsp;: <b>'+fr(D.n)+'</b>.</p>';
  return h;
}
function fmtCount(v){return (v===undefined||v===null||v<0)?"-":fr(v);}
function ownCountText(){
  const total=(D.dict.camera||[]).length;
  return S.camOwned.size? (fr(S.camOwned.size)+' / '+fr(total)+' boîtier(s) possédé(s)')
                        : ('0 coché - tous les boîtiers comptent ('+fr(total)+')');
}
function wireConfig(){
  const el=$("#owncount"); if(el)el.textContent=ownCountText();
  const list=$("#ownlist");
  if(list) list.addEventListener("change",e=>{
    const cb=e.target.closest("input[type=checkbox]"); if(!cb)return;
    const name=cb.getAttribute("data-cam");
    if(cb.checked)S.camOwned.add(name); else S.camOwned.delete(name);
    saveOwned(S.camOwned,false);
    const c=$("#owncount"); if(c)c.textContent=ownCountText();   // pas de re-render : défilement conservé
  });
  const cams=(D.dict.camera||[]);
  on("ownall","click",()=>{S.camOwned=new Set(cams);saveOwned(S.camOwned,false);render();});
  on("ownnone","click",()=>{S.camOwned=new Set();saveOwned(S.camOwned,false);render();});
  on("ownsavebtn","click",()=>saveOwned(S.camOwned,true));
  on("ownreload","click",()=>reloadPractice());
}

// ---- Rendu principal ---------------------------------------------------------
// État déplié/replié des sections, conservé d'un render() à l'autre. Sans cela,
// toute interaction interne (changer un croisement, cliquer une barre dans les
// réglages...) reconstruit #app et refermerait la section qu'on est en train de
// consulter. La clé est le titre de la section (unique). `open` reste la valeur
// par défaut au tout premier rendu, tant que l'utilisateur n'a rien basculé.
let SEC_OPEN={};
// Section repliable : titre cliquable + corps. `open` = dépliée d'emblée.
function sec(title,inner,open){
  const isOpen=(title in SEC_OPEN)?SEC_OPEN[title]:open;
  return '<details class="sec" data-sec="'+esc(title)+'"'+(isOpen?' open':'')+'><summary>'+title+'</summary>'+
    '<div class="secbody">'+inner+'</div></details>';
}
function render(){
  // Mémoriser l'état des sections encore présentes avant de reconstruire #app.
  document.querySelectorAll("#app details.sec").forEach(d=>{SEC_OPEN[d.getAttribute("data-sec")]=d.open;});
  if(TAB==="config"){ $("#app").innerHTML=configPanel(); wireConfig(); return; }
  if(TAB==="library"&&D.lib){ $("#app").innerHTML=libraryPanel(); wire(); return; }
  const idx=filtered();
  let h="";
  h+=filtersPanel();

  // Lecture progressive : l'essentiel est ouvert, l'avancé replié. Un non-initié voit
  // d'abord la vue d'ensemble, le temporel et le matériel ; les réglages fins et les
  // analyses croisées ne se déplient que si on les demande.
  h+=sec("Vue d'ensemble",
      tiles(idx)+'<h3>Tendances (médianes)</h3>'+statTiles(idx), true);

  h+=sec("Contexte et sujet",
      presetsBar()+
      '<div class="cols"><div><h3>Contexte</h3>'+catCard("ctx")+'</div>'+
      '<div><h3>Sujet</h3>'+catCard("subj")+'</div></div>', true);

  h+=sec("Quand - années et mois",
      '<div class="cols"><div><h3>Par année</h3>'+catCard("year",
        '<div class="controls" style="margin-top:.5rem"><label class="muted">Période</label>'+
        '<input type="number" id="pfrom" placeholder="de" value="'+(S.period.from??"")+'">'+
        '<input type="number" id="pto" placeholder="à" value="'+(S.period.to??"")+'">'+
        '<button class="btn" id="pset">appliquer</button><button class="btn" id="pclr">effacer</button></div>')+'</div>'+
      '<div><h3>Par mois</h3>'+catCard("month")+'</div></div>', true);

  h+=sec("Avec quoi - matériel",
      '<div class="cols"><div><h3>Boîtiers</h3>'+catCard("cam")+'</div>'+
      '<div><h3>Objectifs</h3>'+catCard("lens",
        (S.lens.size?'<p class="note">Boîtiers associés&nbsp;: '+esc(countCat(idx,"cam").rows.map(r=>r.label+" ("+r.count+")").join(", ")||"-")+'</p>':''))+'</div></div>'+
      '<h3>Boîtiers - chronologie</h3><div class="card">'+cameraTimeline(idx)+'</div>', true);

  if(D.gps)h+=sec("Où - lieux",mapPanel(),false);

  h+=sec("Réglages - focales, ISO, ouvertures, vitesses",
      '<div class="cols"><div><h3>Focales usuelles</h3>'+rangeCard("focal")+'</div>'+
      '<div><h3>Focales - détail (top)</h3><div class="card">'+
        bars(topFocals(idx,12),{filterKey:"focal",isOn:r=>rangeOn(S.focal,r.k)})+
        '<p class="note">Focales exactes (au mm) les plus fréquentes.</p></div></div></div>'+
      '<div class="cols"><div><h3>Sensibilité ISO</h3>'+rangeCard("iso")+'</div>'+
      '<div><h3>Ouvertures</h3>'+rangeCard("aperture")+'</div></div>'+
      '<div class="cols"><div><h3>Vitesses d\'obturation</h3>'+rangeCard("shutter")+'</div>'+
      '<div><h3>Type de fichier</h3>'+catCard("type")+'</div></div>', false);

  h+=sec("Analyses croisées",
      '<h3>Matrice analytique</h3><div class="card">'+matrixBlock()+'</div>'+
      '<h3>Comparaison rapide (une dimension)</h3><div class="card cmp">'+quickCompare(idx)+'</div>'+
      '<h3>Comparaison de groupes de filtres</h3><div class="card cmp">'+groupsBlock()+'</div>', false);

  h+=sec("Dossiers", catCard("folder"), false);

  h+='<div class="card"><button class="btn" id="reload">recharger les données</button>'+
     '<div class="denom">'+(D.fetched?('actualisé '+esc(D.fetched)):'')+'<br>source&nbsp;: '+esc(D.source||"")+'</div></div>';

  $("#app").innerHTML=h;
  wire();
}

// ---- Lieux : carte de chaleur sans fond de carte -------------------------------
// La position vient de morfPhoto (EXIF du fichier, à défaut un sidecar). On regroupe les photos
// en cases d'une grille géographique ; une case est un filtre comme les autres (dimension
// « Lieu »), donc un clic sur la carte filtre TOUTE la page. Aucun fond de carte n'est chargé :
// la page ne contacte aucun serveur extérieur, les positions de la photothèque restent ici.
let PLACE_STEP=0.1;
function placeDigits(){return PLACE_STEP>=1?0:(PLACE_STEP>=0.1?1:2);}
function fmtDeg(v,pos,neg){return Math.abs(v).toFixed(placeDigits()).replace(".",",")+"° "+(v>=0?pos:neg);}
function placeLab(k){const p=String(k).split(",");return fmtDeg(+p[0]*PLACE_STEP,"N","S")+" · "+fmtDeg(+p[1]*PLACE_STEP,"E","O");}   // coin sud-ouest de la case
function computePlaces(){
  const la=D.cols.latitude, lo=D.cols.longitude; D.placeKeys=null;
  if(!la||!lo)return;
  const out=new Array(D.n);
  for(let i=0;i<D.n;i++){const a=la[i],b=lo[i];
    out[i]=(a===null||a===undefined||b===null||b===undefined)?null:(Math.floor(a/PLACE_STEP)+","+Math.floor(b/PLACE_STEP));}
  D.placeKeys=out;
}
// Photos du périmètre SANS le filtre de lieu : les autres cases restent visibles pour en choisir plusieurs.
function mapIdx(){const F=snapFilters();F.place=new Set();return filteredWith(F);}
function mapCells(idx){const m=new Map();for(const i of idx){const k=D.placeKeys?D.placeKeys[i]:null;if(k===null||k===undefined)continue;m.set(k,(m.get(k)||0)+1);}return m;}
let MAPST=null;
// Contours des pays (Natural Earth 1:110m, embarqués dans le service) : le fond de la carte, sans aucun
// appel extérieur. Grossier à dessein (dizaines de km d'erreur) : il situe les amas, il ne cadastre rien.
let OUTLINE=null, OUTLINE_REQ=false;
function loadOutline(){
  if(OUTLINE||OUTLINE_REQ)return; OUTLINE_REQ=true;
  fetch("/photo/outline.json").then(r=>r.json()).then(rings=>{
    OUTLINE=rings.map(r=>{let a=1e9,b=-1e9,c=1e9,d=-1e9;
      for(let i=0;i<r.length;i+=2){a=Math.min(a,r[i]);b=Math.max(b,r[i]);c=Math.min(c,r[i+1]);d=Math.max(d,r[i+1]);}
      return {p:r,lon0:a,lon1:b,lat0:c,lat1:d};});
    if(document.getElementById("map"))drawMap();
  }).catch(()=>{OUTLINE=[];});
}
function drawOutline(ctx,X,Y,W,H,pad,lat0,lat1,lon0,lon1,color){
  if(!OUTLINE)return;
  ctx.save(); ctx.beginPath(); ctx.rect(pad,pad,W-2*pad,H-2*pad); ctx.clip();
  ctx.strokeStyle=color; ctx.lineWidth=1;
  for(const r of OUTLINE){
    if(r.lon1<lon0||r.lon0>lon1||r.lat1<lat0||r.lat0>lat1)continue;   // contour hors de la vue
    ctx.beginPath(); ctx.moveTo(X(r.p[0]),Y(r.p[1]));
    for(let i=2;i<r.p.length;i+=2)ctx.lineTo(X(r.p[i]),Y(r.p[i+1]));
    ctx.stroke();
  }
  ctx.restore();
}
function mapPanel(){
  const idx=mapIdx(), cells=mapCells(idx); let located=0; cells.forEach(v=>{located+=v;});
  const steps=[[1,"1° (≈ 110 km)"],[0.1,"0,1° (≈ 11 km)"],[0.01,"0,01° (≈ 1 km)"]];
  const sel=steps.map(s=>'<option value="'+s[0]+'"'+(s[0]===PLACE_STEP?" selected":"")+'>'+s[1]+'</option>').join("");
  const top=countCat(idx,"place");
  return '<div class="controls"><label class="muted">Taille des cases</label><select id="mapstep">'+sel+'</select>'+
    (S.place.size?'<button class="btn" id="mapclr">effacer le lieu</button>':'')+'</div>'+
    denom(located,idx.length)+
    (cells.size?'<canvas id="map" width="900" height="380"></canvas>':'<p class="muted">aucune photo géolocalisée dans ce périmètre</p>')+
    '<div class="note" id="mapinfo">Un clic sur une case filtre toute la page sur ce lieu ; plusieurs cases se cumulent.</div>'+
    '<h3>Lieux les plus photographiés</h3><div class="card">'+bars(top.rows.slice(0,12),{filterKey:"place",isOn:r=>S.place.has(r.key)})+'</div>'+
    '<p class="note">Carte de chaleur avec un fond de contours de pays embarqué (grossier : il situe, il ne cadastre pas) : aucune image n\'est chargée depuis l\'extérieur. '+
    'La position vient du fichier (EXIF) ou, à défaut, d\'un sidecar. Une case est désignée par son coin sud-ouest.</p>';
}
function drawMap(){
  const cv=document.getElementById("map"); if(!cv||!D.placeKeys)return;
  const cells=mapCells(mapIdx()); if(!cells.size)return;
  const cs=getComputedStyle(document.body), col=(n,f)=>(cs.getPropertyValue(n).trim()||f);
  const ctx=cv.getContext("2d"), W=cv.width, H=cv.height; ctx.clearRect(0,0,W,H);
  let a0=1e9,a1=-1e9,b0=1e9,b1=-1e9,max=0;
  for(const [k,n] of cells){const p=k.split(",").map(Number);a0=Math.min(a0,p[0]);a1=Math.max(a1,p[0]);b0=Math.min(b0,p[1]);b1=Math.max(b1,p[1]);max=Math.max(max,n);}
  const lat0=(a0-1)*PLACE_STEP, lat1=(a1+2)*PLACE_STEP, lon0=(b0-1)*PLACE_STEP, lon1=(b1+2)*PLACE_STEP;
  const kx=Math.max(0.2,Math.cos(((lat0+lat1)/2)*Math.PI/180));
  const wDeg=(lon1-lon0)*kx, hDeg=lat1-lat0, pad=34;
  const sc=Math.min((W-2*pad)/wDeg,(H-2*pad)/hDeg), ox=(W-wDeg*sc)/2, oy=(H-hDeg*sc)/2;
  const X=lon=>ox+(lon-lon0)*kx*sc, Y=lat=>oy+(lat1-lat)*sc;
  MAPST={cells,lat1,lon0,kx,sc,ox,oy};
  loadOutline();
  drawOutline(ctx,X,Y,W,H,pad,lat0,lat1,lon0,lon1,col("--muted","#888"));
  // graduations : le pas le plus fin qui laisse au moins 70 px entre deux traits
  const nice=[0.01,0.02,0.05,0.1,0.2,0.5,1,2,5,10,20,30]; let gs=nice[nice.length-1];
  for(const g of nice)if(g*sc>=70&&g*sc*kx>=70){gs=g;break;}
  ctx.font="11px sans-serif"; ctx.fillStyle=col("--muted","#888"); ctx.strokeStyle=col("--line","#444"); ctx.lineWidth=1;
  for(let la=Math.ceil(lat0/gs)*gs;la<=lat1;la+=gs){ctx.beginPath();ctx.moveTo(pad,Y(la));ctx.lineTo(W-pad,Y(la));ctx.stroke();ctx.fillText(la.toFixed(gs<0.1?2:(gs<1?1:0))+"°",2,Y(la)+4);}
  for(let lo=Math.ceil(lon0/gs)*gs;lo<=lon1;lo+=gs){ctx.beginPath();ctx.moveTo(X(lo),pad);ctx.lineTo(X(lo),H-pad);ctx.stroke();ctx.fillText(lo.toFixed(gs<0.1?2:(gs<1?1:0))+"°",X(lo)-10,H-10);}
  for(const [k,n] of cells){
    const p=k.split(",").map(Number), x=X(p[1]*PLACE_STEP), y=Y((p[0]+1)*PLACE_STEP);
    const w=Math.max(3,PLACE_STEP*kx*sc), h=Math.max(3,PLACE_STEP*sc), t=Math.log(1+n)/Math.log(1+max);
    ctx.fillStyle="rgba(90,155,255,"+(0.25+0.75*t).toFixed(2)+")"; ctx.fillRect(x,y,w,h);
    if(S.place.has(k)){ctx.strokeStyle="#e0a030";ctx.lineWidth=2;ctx.strokeRect(x,y,w,h);ctx.lineWidth=1;}
  }
}
function mapCellAt(ev){
  const cv=document.getElementById("map"); if(!cv||!MAPST)return null;
  const r=cv.getBoundingClientRect(), px=(ev.clientX-r.left)*cv.width/r.width, py=(ev.clientY-r.top)*cv.height/r.height;
  const lon=MAPST.lon0+((px-MAPST.ox)/MAPST.sc)/MAPST.kx, lat=MAPST.lat1-(py-MAPST.oy)/MAPST.sc;
  const k=Math.floor(lat/PLACE_STEP)+","+Math.floor(lon/PLACE_STEP);
  return MAPST.cells.has(k)?k:null;
}
function wireMap(){
  const cv=document.getElementById("map");
  if(cv){
    drawMap();
    cv.addEventListener("mousemove",ev=>{const k=mapCellAt(ev),i=document.getElementById("mapinfo");
      if(i)i.textContent=k?(placeLab(k)+" : "+fr(MAPST.cells.get(k))+" photo(s)"):"Un clic sur une case filtre toute la page sur ce lieu ; plusieurs cases se cumulent.";});
    cv.addEventListener("click",ev=>{const k=mapCellAt(ev); if(k===null)return; if(S.place.has(k))S.place.delete(k);else S.place.add(k); render();});
  }
  on("mapstep","change",e=>{PLACE_STEP=+e.target.value;S.place.clear();computePlaces();render();});
  on("mapclr","click",()=>{S.place.clear();render();});
}

// ---- Dérive sur le temps long (par année) -------------------------------------
function yearStats(idx){
  const c=D.cols, ra=c.rating||[], ea=c.edited_any||[], rj=c.rejected||[], m=new Map();
  for(const i of idx){const y=D.years[i]; if(y===null||y===undefined)continue;
    let e=m.get(y); if(!e){e={y,n:0,rt:0,rs:0,edK:0,ed:0,rjK:0,rj:0};m.set(y,e);}
    e.n++;
    const r=ra[i]; if(r!==null&&r!==undefined){e.rt++;e.rs+=r;}
    const x=ea[i]; if(x!==null&&x!==undefined){e.edK++;if(x)e.ed++;}
    const j=rj[i]; if(j!==null&&j!==undefined){e.rjK++;if(j)e.rj++;}}
  return [...m.values()].sort((a,b)=>a.y-b.y);
}
// Courbe par année. Un point creux = moins de 5 photos connues cette année-là : la valeur est fragile.
function lineChart(title,rows,series,opts){
  const W=640,H=210,L=44,R=14,T=14,B=30, ys=rows.map(r=>r.y), x0=Math.min(...ys), x1=Math.max(...ys);
  const X=y=>L+(x1===x0?(W-L-R)/2:(y-x0)*(W-L-R)/(x1-x0)), Y=v=>T+(1-v/opts.max)*(H-T-B);
  let s='<svg viewBox="0 0 '+W+' '+H+'" class="lc" role="img" aria-label="'+esc(title)+'">';
  for(let k=0;k<=4;k++){const v=opts.max*k/4;
    s+='<line x1="'+L+'" x2="'+(W-R)+'" y1="'+Y(v)+'" y2="'+Y(v)+'" stroke="var(--line)"/><text x="'+(L-6)+'" y="'+(Y(v)+4)+'" text-anchor="end" font-size="10" fill="var(--muted)">'+opts.fmt(v)+'</text>';}
  ys.forEach(y=>{s+='<text x="'+X(y)+'" y="'+(H-10)+'" text-anchor="middle" font-size="10" fill="var(--muted)">'+y+'</text>';});
  for(const se of series){
    const pts=rows.filter(r=>se.val(r)!==null);
    if(pts.length>1)s+='<polyline fill="none" stroke="'+se.color+'" stroke-width="2" points="'+pts.map(r=>X(r.y)+','+Y(se.val(r))).join(" ")+'"/>';
    for(const r of pts){const k=se.known(r), weak=k<5;
      s+='<circle cx="'+X(r.y)+'" cy="'+Y(se.val(r))+'" r="4" fill="'+(weak?"var(--card)":se.color)+'" stroke="'+se.color+'" stroke-width="2"><title>'+
        esc(se.name+" "+r.y+" : "+opts.fmt(se.val(r))+" ("+fr(k)+" photos connues)")+'</title></circle>';}
  }
  const legend=series.map(se=>'<span style="color:'+se.color+'">&#9632;</span> '+esc(se.name)).join(" &nbsp; ");
  return s+'</svg><div class="note">'+legend+' &middot; point creux&nbsp;: moins de 5 photos connues cette année-là (valeur fragile)</div>';
}
// Mots-clés par année : part des photos DE CETTE ANNÉE (qui ont une liste de mots-clés connue) portant le mot-clé.
function keywordDrift(idx,years){
  const kw=D.cols.keywords||[], tot=new Map(), per=new Map(), known=new Map();
  for(const i of idx){const y=D.years[i], a=kw[i]; if(y===null||y===undefined||a===null||a===undefined)continue;
    known.set(y,(known.get(y)||0)+1);
    for(const k of a){tot.set(k,(tot.get(k)||0)+1); const key=k+"|"+y; per.set(key,(per.get(key)||0)+1);}}
  const top=[...tot.entries()].sort((a,b)=>b[1]-a[1]).slice(0,10).map(e=>e[0]);
  if(!top.length)return '<p class="muted">aucun mot-clé connu dans ce périmètre</p>';
  let h='<table class="heat"><tr><th>Mot-clé</th>'+years.map(y=>'<th class="n">'+y+'</th>').join("")+'</tr>';
  for(const k of top){
    h+='<tr><td class="h">'+esc(DIMS.kw.lab(k))+'</td>';
    for(const y of years){const kn=known.get(y)||0, n=per.get(k+"|"+y)||0, p=kn?n/kn:null;
      h+='<td class="c" style="'+(p===null?"":"background:rgba(90,155,255,"+(0.08+0.8*p).toFixed(2)+")")+'" title="'+esc(DIMS.kw.lab(k)+" "+y+" : "+n+" / "+kn+" photos avec une liste connue")+'">'+(p===null?"·":Math.round(p*100)+"%")+'</td>';}
    h+='</tr>';}
  return h+'</table><p class="note">Part des photos de l\'année portant le mot-clé, parmi celles dont les mots-clés sont connus.</p>';
}
function driftPanel(idx){
  const rows=yearStats(idx);
  if(rows.length<2)return '<p class="muted">Au moins deux années datées sont nécessaires pour une dérive.</p>';
  const avg=r=>r.rt?r.rs/r.rt:null, pc=(a,b)=>b?a*100/b:null, B="#5a9bff", O="#e0a030", G="#4cc38a";
  const fmtStar=v=>v.toFixed(1).replace(".",",")+" ★", fmtPct=v=>Math.round(v)+" %";
  return '<p class="note">Ce que les sidecars disent, année après année. Chaque point est calculé sur les photos de l\'année dont la valeur est connue.</p>'+
    '<div class="cols"><div><h3>Note moyenne</h3><div class="card">'+
      lineChart("Note moyenne par année",rows,[{name:"note moyenne",color:B,val:avg,known:r=>r.rt}],{max:5,fmt:fmtStar})+'</div></div>'+
    '<div><h3>Part retouchée et part rejetée</h3><div class="card">'+
      lineChart("Retouche et rejet par année",rows,[
        {name:"retouchées",color:O,val:r=>pc(r.ed,r.edK),known:r=>r.edK},
        {name:"rejetées",color:G,val:r=>pc(r.rj,r.rjK),known:r=>r.rjK}],{max:100,fmt:fmtPct})+'</div></div></div>'+
    '<h3>Mots-clés par année</h3><div class="card" style="overflow-x:auto">'+keywordDrift(idx,rows.map(r=>r.y))+'</div>';
}

// ---- Onglet « Tri et retouche » (bibliothèque photo) -------------------------
// Même moteur, mêmes filtres : un clic sur une note, un logiciel ou un mot-clé filtre TOUTE
// la page, y compris l'onglet Exploration. Aucune interprétation n'est codée ici : on
// compte ce que les sidecars disent, avec le dénominateur de ce qui est connu.
function pctOf(a,b){return b?(Math.round(a*1000/b)/10).toString().replace(".",",")+" %":"-";}
function libStats(idx){
  const c=D.cols, ra=c.rating||[], rj=c.rejected||[], ea=c.edited_any||[], ve=c.versions||[], kw=c.keywords||[], pk=c.picked||[];
  let rated=0,sum=0,rej=0,rejK=0,ed=0,edK=0,lib=0,kwN=0,kwK=0,pick=0,pickK=0;
  for(const i of idx){
    const r=ra[i]; if(r!==null&&r!==undefined){rated++;sum+=r;}
    const j=rj[i]; if(j!==null&&j!==undefined){rejK++; if(j)rej++;}
    const e=ea[i]; if(e!==null&&e!==undefined){edK++; if(e)ed++;}
    const q=pk[i]; if(q!==null&&q!==undefined){pickK++; if(q)pick++;}
    const v=ve[i]; if(v!==null&&v!==undefined&&v>0)lib++;   // 0 version = lue, sans sidecar ni XMP
    const k=kw[i]; if(k!==null&&k!==undefined){kwK++; if(k.length)kwN++;}
  }
  return {n:idx.length,rated,avg:rated?sum/rated:null,rej,rejK,ed,edK,lib,kwN,kwK,pick,pickK};
}
function libTiles(idx){
  const s=libStats(idx);
  const t=(num,lab,sub)=>'<div class="tile"><div class="number mono">'+num+'</div><div class="sub">'+lab+(sub?' &middot; '+sub:'')+'</div></div>';
  return '<div class="grid">'+
    t(fr(s.n),"Photos (périmètre courant)")+
    t(pctOf(s.lib,s.n),"Avec sidecar ou XMP",fr(s.lib)+" / "+fr(s.n))+
    t(s.avg===null?"-":s.avg.toFixed(1).replace(".",",")+" ★","Note moyenne",fr(s.rated)+" notées")+
    t(pctOf(s.rej,s.rejK),"Rejetées",fr(s.rejK)+" conn.")+
    t(pctOf(s.pick,s.pickK),"Retenues",fr(s.pickK)+" conn.")+
    t(pctOf(s.ed,s.edK),"Retouchées",fr(s.edK)+" conn.")+
    t(pctOf(s.kwN,s.kwK),"Avec mot-clé",fr(s.kwK)+" conn.")+'</div>'+
    '<p class="note">Une valeur absente d\'un sidecar reste inconnue : elle n\'entre ni dans le taux ni dans la moyenne '+
    '(une photo sans note n\'est pas « 0 étoile »). Les copies d\'une même photo ne sont pas comptées à part.</p>';
}
// Une ligne par valeur d'une dimension : photos, part retouchée, note moyenne. Les lignes
// sont cliquables : elles posent le même filtre que les barres de l'onglet Exploration.
function libTable(idx,dim,title,opts){
  opts=opts||{};
  const D_=DIMS[dim], c=D.cols, ea=c.edited_any||[], ra=c.rating||[], m=new Map();
  for(const i of idx){
    let keys;
    if(D_.multi){keys=D_.keys(i); if(!keys||!keys.length)continue;}
    else{const k=D_.key(i); if(k===null||k===undefined)continue; keys=[k];}
    for(const k of keys){
      let e=m.get(k); if(!e){e={n:0,ed:0,edK:0,rt:0,rs:0,rj:0,rjK:0};m.set(k,e);}
      e.n++;
      const x=ea[i]; if(x!==null&&x!==undefined){e.edK++; if(x)e.ed++;}
      const j=(c.rejected||[])[i]; if(j!==null&&j!==undefined){e.rjK++; if(j)e.rj++;}
      const r=ra[i]; if(r!==null&&r!==undefined){e.rt++; e.rs+=r;}
    }
  }
  let rows=[...m.entries()];
  if(opts.byKey)rows.sort((a,b)=>(+a[0])-(+b[0]));
  else rows.sort((a,b)=>b[1].n-a[1].n||String(D_.lab(a[0])).localeCompare(String(D_.lab(b[0]))));
  const more=rows.length>(opts.limit||15); rows=rows.slice(0,opts.limit||15);
  if(!rows.length)return '<p class="muted">aucune donn&eacute;e</p>';
  const body=rows.map(([k,e])=>'<tr class="click'+(S[dim].has(k)?" on":"")+'" data-f="'+dim+'" data-k="'+esc(JSON.stringify(k))+'">'+
    '<td>'+esc(D_.lab(k))+'</td><td class="n mono">'+fr(e.n)+'</td><td class="n mono">'+pctOf(e.ed,e.edK)+
    '</td><td class="n mono">'+pctOf(e.rj,e.rjK)+'</td><td class="n mono">'+(e.rt?(e.rs/e.rt).toFixed(1).replace(".",",")+" ★":"-")+'</td></tr>').join("");
  return '<table><tr><th>'+esc(title)+'</th><th class="n">Photos</th><th class="n">Retouchées</th><th class="n">Rejetées</th><th class="n">Note moy.</th></tr>'+body+'</table>'+
    (more?'<p class="note">Les '+(opts.limit||15)+' plus fréquents ; affinez les filtres pour en voir d\'autres.</p>':'');
}
function libraryPanel(){
  document.querySelectorAll("#app details.sec").forEach(d=>{SEC_OPEN[d.getAttribute("data-sec")]=d.open;});
  const idx=filtered();
  let h=filtersPanel();
  h+=sec("Vue d'ensemble",libTiles(idx),true);
  h+=sec("Notes, rejets, étiquettes",
    '<div class="cols"><div><h3>Notes</h3>'+catCard("rate")+'</div><div><h3>Rejet</h3>'+catCard("rej")+'</div></div>'+
    '<div class="cols"><div><h3>Sélection (drapeau « retenue »)</h3>'+catCard("pick")+'</div><div></div></div>'+
    '<div class="cols"><div><h3>Étiquettes de couleur</h3>'+catCard("lbl")+'</div><div><h3>Logiciel</h3>'+catCard("src")+'</div></div>',true);
  h+=sec("Retouche et notes, par année et par matériel",
    '<div class="cols"><div><h3>Par année</h3><div class="card">'+libTable(idx,"year","Année",{byKey:true,limit:40})+'</div></div>'+
    '<div><h3>Par boîtier</h3><div class="card">'+libTable(idx,"cam","Boîtier")+'</div></div></div>'+
    '<div class="cols"><div><h3>Par objectif</h3><div class="card">'+libTable(idx,"lens","Objectif")+'</div></div>'+
    '<div><h3>Par contexte</h3><div class="card">'+libTable(idx,"ctx","Contexte")+'</div></div></div>',true);
  h+=sec("Contexte et sujet photographiques",
    '<p class="note">Ce que l\'on fait des photos selon la séance (contexte) et ce que l\'on photographie (sujet) : '+
    'part retouchée, rejetée et note moyenne. Les photos d\'un dossier non qualifié n\'y figurent pas '+
    '(les qualifier se fait dans PhotoHub).</p>'+
    '<div class="cols"><div><h3>Par contexte</h3><div class="card">'+libTable(idx,"ctx","Contexte",{limit:20})+'</div></div>'+
    '<div><h3>Par sujet</h3><div class="card">'+libTable(idx,"subj","Sujet",{limit:20})+'</div></div></div>',true);
  h+=sec("Comparer deux ensembles",
    '<p class="note">Deux années, deux logiciels, deux niveaux de note, deux contextes : mêmes mesures de part et d\'autre, '+
    'avec le nombre de photos connues. Pour comparer deux périodes de notation, filtrer sur des années '+
    '(clic dans « Par année » ou champ « Période » de l\'onglet Exploration), capturer le groupe A, puis le groupe B.</p>'+
    '<h3>Comparaison rapide (une dimension)</h3><div class="card cmp">'+quickCompare(idx)+'</div>'+
    '<h3>Comparaison de groupes de filtres</h3><div class="card cmp">'+groupsBlock()+'</div>',false);
  h+=sec("Dérive sur le temps long",driftPanel(idx),true);
  h+=sec("Mots-clés",'<div class="card">'+libTable(idx,"kw","Mot-clé",{limit:30})+
    '<p class="note">Les mots-clés hiérarchiques sont comptés avec leur chemin complet (« Lieux | France »). '+
    'Un clic filtre sur ce mot-clé.</p></div>',false);
  h+=sec("Croiser avec le matériel et les réglages",
    '<p class="note">La matrice de l\'onglet Exploration accepte la note, la retouche, le rejet et le logiciel comme lignes ou colonnes, '+
    'et « note moyenne » ou « taux de retouche » comme mesure.</p>',false);
  h+='<div class="card"><button class="btn" id="reload">recharger les données</button>'+
     '<div class="denom">'+(D.fetched?('actualisé '+esc(D.fetched)):'')+'<br>source&nbsp;: '+esc(D.source||"")+'</div></div>';
  return h;
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
  document.querySelectorAll("[data-chip]").forEach(el=>{
    el.addEventListener("click",()=>{const fn=chip._h[el.getAttribute("data-chip")];if(fn)fn();render();});});
  on("resetf","click",()=>{const ex=S.camExcl;const f=emptyFilters();f.camExcl=ex;Object.assign(S,f);render();});
  on("reload","click",reload);
  // Presets contexte/sujet (raccourcis de filtres).
  document.querySelectorAll(".preset").forEach(el=>el.addEventListener("click",
    ()=>applyPreset(el.getAttribute("data-pctx"),el.getAttribute("data-psubj"))));
  on("presetclr","click",()=>{const owned=new Set(S.camOwned),excl=new Set(S.camExcl);
    Object.assign(S,emptyFilters());S.camOwned=owned;S.camExcl=excl;render();});
  // Vues enregistrées
  on("viewsave","click",()=>{
    const name=(prompt("Nom de la vue à enregistrer :","")||"").trim();
    if(!name)return;
    const v=loadViews(); v[name]=serializeFilters(); saveViews(v); render();});
  on("viewapply","click",()=>{const n=$("#viewsel").value; if(!n)return;
    const v=loadViews()[n]; if(v)applyView(v);});
  on("viewdel","click",()=>{const n=$("#viewsel").value; if(!n)return;
    const v=loadViews(); delete v[n]; saveViews(v); render();});
  // Lien « Configuration » depuis le résumé de périmètre.
  on("gotoconf","click",e=>{e.preventDefault();setTab("config");});
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
  wireMap();
}

// Badge de version : lit /status (comme la page MeteoHub) pour afficher la version
// du service courant a cote du titre. Silencieux si /status est indisponible.
// Onglets Exploration / Configuration : bascule sans recharger les données (le dataset
// est déjà en mémoire). Câblés une seule fois, hors de #app qui est reconstruit à chaque
// rendu. setTab est aussi appelé par le lien « Configuration » du résumé de périmètre.
function setTab(t){
  TAB=t;
  document.querySelectorAll("#tabs .tab").forEach(b=>b.classList.toggle("on",b.getAttribute("data-tab")===t));
  if(D)render();   // rien à rendre tant que les données ne sont pas chargées
}
document.querySelectorAll("#tabs .tab").forEach(b=>{
  b.addEventListener("click",()=>setTab(b.getAttribute("data-tab")));
});

fetch("/status").then(r=>r.json()).then(s=>{const b=document.getElementById("vb");if(b)b.textContent=s.version?"v"+s.version:"";}).catch(()=>{});
loadSources();
</script>
</body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
