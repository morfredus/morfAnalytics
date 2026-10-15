#include "morfanalytics/pages/GitHubSettingsPage.h"

namespace morfanalytics::pages {

// Configuration des alertes GitHub : seuils, regles et sourdines par depot. L'envoi (adresse de
// morfNotify, niveau minimal, destinations par defaut) est COMMUN a SiteWatch et GitHub : il se regle
// sur la page de configuration SiteWatch, ici on le rappelle seulement. Toutes les valeurs sont bornees
// cote serveur ; la page ne fait que presenter et envoyer.
QByteArray GitHubSettingsPage::render() {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<!--theme-head-->
<title>morfAnalytics - Configuration GitHub</title>
<style>
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:1.5rem}
.wrap{max-width:62rem;margin:auto}a{color:var(--accent)}.muted{color:var(--muted)}
.vb{font-size:.8rem;font-weight:600;vertical-align:middle;color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,transparent);border:1px solid color-mix(in srgb,var(--accent) 30%,transparent);border-radius:999px;padding:.1rem .5rem;margin-left:.4rem}
h1{margin:0 0 .3rem}h2{font-size:1rem;margin:0 0 .6rem}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:1rem 1.2rem;margin:1rem 0}
.row{display:grid;grid-template-columns:minmax(12rem,1fr) 2fr;gap:.6rem 1rem;align-items:center;padding:.4rem 0;border-bottom:1px solid var(--line)}
.row:last-child{border-bottom:0}.row small{display:block;color:var(--muted)}
input[type=text],input[type=number],select{background:var(--field);border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.35rem .5rem;font:inherit;width:100%;max-width:28rem}
input[type=number]{max-width:9rem}
button{background:var(--btn);border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.4rem .8rem;cursor:pointer;font:inherit}
button.primary{border-color:var(--accent);color:var(--accent);font-weight:600}button.small{padding:.15rem .5rem;font-size:.8rem}
table{width:100%;border-collapse:collapse;font-size:.9rem}th,td{padding:.4rem .4rem;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}th{color:var(--muted);font-weight:600}
td small{display:block;color:var(--muted)}td input[type=text]{max-width:14rem}
.msg{padding:.6rem .8rem;border-radius:8px;margin:.6rem 0;border:1px solid var(--line)}.ok{border-color:#2e9b57;color:#2e9b57}.ko{border-color:#d6453d;color:#d6453d}
.bar{position:sticky;bottom:0;background:var(--bg);padding:.8rem 0;border-top:1px solid var(--line);display:flex;gap:.6rem;flex-wrap:wrap;align-items:center}
.pill{display:inline-block;font-size:.72rem;border:1px solid var(--line);border-radius:999px;padding:.05rem .5rem;color:var(--muted)}
</style></head><body><div class="wrap">
<!--nav-back-->
<h1>Configuration GitHub <span id="vb" class="vb"></span><!--theme-toggle--></h1>
<p class="muted"><a href="/github">&larr; Retour aux analyses</a> · Les réglages sont enregistrés par le service : aucun fichier à éditer. Les valeurs hors limites sont ramenées à des bornes sûres.</p>
<div id="app"><p class="muted">Chargement…</p></div>
</div>
<script>
"use strict";
const $=id=>document.getElementById(id);
const esc=s=>String(s??"").replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;"}[c]));
const LEVELS=[["","Par défaut"],["info","Information"],["warning","Avertissement"],["error","Erreur"]];
let cfg=null,dirty=false;
// Rien ne s'applique tant qu'on n'a pas clique sur Enregistrer : on le dit, et on previent avant de quitter.
function setDirty(v){dirty=v;const d=$("dirty");if(!d)return;
  d.textContent=v?"● Modifications non enregistrées : cliquer sur Enregistrer pour les appliquer":"Les réglages s'appliquent dès l'enregistrement.";
  d.style.color=v?"#e0a030":"";d.style.fontWeight=v?"600":""}
window.addEventListener("beforeunload",e=>{if(dirty){e.preventDefault();e.returnValue=""}});
function onEdit(e){const t=e.target;if(!t.closest||!t.closest("#app"))return;if(t.classList.contains("r_mu")||t.id==="m_repo")return;setDirty(true)}
document.addEventListener("input",onEdit);document.addEventListener("change",onEdit);
async function jget(u){const r=await fetch(u);if(!r.ok)throw new Error(u+" : HTTP "+r.status);return r.json()}
async function jpost(u,o){const r=await fetch(u,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(o)});const j=await r.json().catch(()=>({}));if(!r.ok)throw new Error(j.error||("HTTP "+r.status));return j}
const csv=s=>String(s||"").split(",").map(x=>x.trim()).filter(Boolean);
function msg(id,ok,text){const e=$(id);if(e)e.innerHTML=text?`<div class="msg ${ok?"ok":"ko"}">${esc(text)}</div>`:""}
const num=(id,def)=>{const v=parseFloat($(id).value);return isNaN(v)?def:v};
function numberRow(id,label,hint,value,def,min,max,step){
  return`<div class="row"><div>${label}<small>${hint} Défaut : ${def}.</small></div><div><input type="number" id="${id}" value="${value}" min="${min}" max="${max}" step="${step||1}"></div></div>`;
}
function render(){
  const g=cfg.global,d=cfg.defaults,n=cfg.notify;
  const dest=(n.targets||[]).length?n.targets.join(", "):"destinations par défaut de morfNotify";
  let h=`<div class="card"><h2>Notifications</h2>
   <p class="muted" style="margin:0">L'envoi est commun à SiteWatch et à GitHub : ${n.enabled?"activé":"<b>désactivé</b>"}, niveau minimal « ${esc(n.min_level)} », vers ${esc(dest)}.
   Adresse de morfNotify, destinations et test d'envoi se règlent sur la <a href="/sitewatch/settings">configuration SiteWatch</a>.</p>
   ${cfg.delivery_backlog>0?`<div class="msg ko">${cfg.delivery_backlog} alerte(s) en attente d'envoi ou en échec : elles seront retentées toutes les heures (5 essais).</div>`:""}
  </div>
  <div class="card"><h2>Détection</h2>`+
   numberRow("drop_pct","Chute des vues (%)","Baisse des vues sur 7 jours, comparée aux 7 jours précédents, qui déclenche l'alerte.",g.drop_pct,d.drop_pct,10,95,5)+
   numberRow("spike_factor","Pic : facteur","Un jour au-dessus de ce multiple de l'habitude (médiane des 14 jours précédents) est un pic.",g.spike_factor,d.spike_factor,1.5,20,0.5)+
   numberRow("min_views","Volume plancher","En dessous de ce nombre de vues ou de clones, une variation n'est que du bruit.",g.min_views,d.min_views,5,1000,5)+
   numberRow("stale_warn_days","Retard de publication : avertissement (jours)","SiteWatch ne publie plus pour le dépôt depuis ce nombre de jours.",g.stale_warn_days,d.stale_warn_days,1,30)+
   numberRow("stale_error_days","Retard de publication : erreur (jours)","Passe l'alerte de retard en erreur.",g.stale_error_days,d.stale_error_days,1,60)+
  `</div><div class="card"><h2>Règles d'alerte</h2><table><thead><tr><th>Règle</th><th>Active</th><th>Niveau</th><th>Envoi</th><th>Destinations</th><th>Sourdine</th></tr></thead><tbody>`+
  cfg.rules.map(r=>{
    const s=g.rules[r.id]||{enabled:true,level:"",notify:true,targets:[]};
    const until=g.muted[r.id];const act=until&&until>=new Date().toISOString().slice(0,10);
    return`<tr data-rule="${r.id}"><td><b>${esc(r.label)}</b><small>${esc(r.description)}</small></td>
     <td><input type="checkbox" class="r_en" ${s.enabled?"checked":""}></td>
     <td><select class="r_lv">${LEVELS.map(l=>`<option value="${l[0]}" ${s.level===l[0]?"selected":""}>${l[1]}${l[0]===""?" ("+({info:"info",warning:"avert.",error:"erreur"}[r.default_level])+")":""}</option>`).join("")}</select></td>
     <td><input type="checkbox" class="r_no" ${s.notify?"checked":""}></td>
     <td><input type="text" class="r_tg" value="${esc((s.targets||[]).join(", "))}" placeholder="globales"></td>
     <td><select class="r_mu"><option value="">${act?"jusqu'au "+esc(until):"Aucune"}</option>${[[1,"1 jour"],[7,"7 jours"],[30,"30 jours"],[90,"90 jours"]].map(x=>`<option value="${x[0]}">${x[1]}</option>`).join("")}${act?'<option value="0">Lever la sourdine</option>':""}</select></td></tr>`}).join("")+
  `</tbody></table><p class="muted" style="font-size:.85rem">La sourdine s'applique tout de suite. Une alerte en sourdine reste enregistrée et visible, jamais envoyée.</p></div>
  <div id="gmsg"></div>
  <div class="bar"><button class="primary" id="save">Enregistrer</button><button id="reset">Rétablir les valeurs par défaut</button><span id="dirty" class="muted">Les réglages s'appliquent dès l'enregistrement.</span></div>
  <div class="card"><h2>Sourdine par dépôt</h2><p class="muted">Couper les alertes d'un seul dépôt (par exemple un projet que l'on ne suit plus de près).</p>
   <div class="row"><div>Dépôt</div><div><select id="m_repo">${cfg.repos.map(r=>`<option value="${esc(r.full_name)}">${esc(r.full_name)}</option>`).join("")}</select></div></div><div id="repo"></div></div>`;
  $("app").innerHTML=h;bindMain();renderRepo();setDirty(false);
}
function collect(){
  const rules={};
  document.querySelectorAll("tr[data-rule]").forEach(tr=>{rules[tr.dataset.rule]={enabled:tr.querySelector(".r_en").checked,level:tr.querySelector(".r_lv").value,notify:tr.querySelector(".r_no").checked,targets:csv(tr.querySelector(".r_tg").value)}});
  return{drop_pct:num("drop_pct",50),spike_factor:num("spike_factor",3),min_views:num("min_views",30),stale_warn_days:num("stale_warn_days",3),stale_error_days:num("stale_error_days",7),rules};
}
function bindMain(){
  $("save").onclick=async()=>{try{cfg=await jpost("/github/config",{settings:collect()});render();msg("gmsg",true,"Réglages enregistrés.")}catch(e){msg("gmsg",false,e.message)}};
  $("reset").onclick=()=>{const d=cfg.defaults;for(const k of["drop_pct","spike_factor","min_views","stale_warn_days","stale_error_days"])$(k).value=d[k];
    document.querySelectorAll("tr[data-rule]").forEach(tr=>{tr.querySelector(".r_en").checked=true;tr.querySelector(".r_lv").value="";tr.querySelector(".r_no").checked=true;tr.querySelector(".r_tg").value=""});
    setDirty(true);msg("gmsg",true,"Valeurs par défaut chargées : cliquer sur Enregistrer pour les appliquer.")};
  document.querySelectorAll("tr[data-rule] .r_mu").forEach(sel=>sel.onchange=async()=>{if(sel.value==="")return;
    if(dirty){msg("gmsg",false,"Enregistrer d'abord les modifications en cours : la sourdine recharge la page.");sel.value="";return}
    try{cfg=await jpost("/github/config/mute",{rule:sel.closest("tr").dataset.rule,days:parseInt(sel.value)});render()}catch(e){msg("gmsg",false,e.message)}});
  $("m_repo").onchange=renderRepo;
}
function renderRepo(){
  const id=$("m_repo")?.value;if(!id){$("repo").innerHTML='<p class="muted">Aucun dépôt reçu pour l\'instant.</p>';return}
  const r=cfg.repos.find(x=>x.full_name===id)||{muted:{}},today=new Date().toISOString().slice(0,10),muted=r.muted||{};
  $("repo").innerHTML=`<div class="row"><div>Sourdines en cours</div><div>${Object.keys(muted).filter(k=>muted[k]>=today).map(k=>`<span class="pill">${k==="*"?"toutes les règles":esc((cfg.rules.find(x=>x.id===k)||{label:k}).label)} jusqu'au ${esc(muted[k])} <button class="small" data-um="${esc(k)}">lever</button></span>`).join(" ")||'<span class="muted">aucune</span>'}</div></div>
   <div id="rmsg"></div><button id="muteRepo">Sourdine de 7 jours sur toutes les règles de ce dépôt</button>`;
  const keep=()=>{render();$("m_repo").value=id;renderRepo()};
  $("muteRepo").onclick=async()=>{try{cfg=await jpost("/github/config/mute",{repo:id,rule:"*",days:7});keep()}catch(e){msg("rmsg",false,e.message)}};
  $("repo").querySelectorAll("button[data-um]").forEach(b=>b.onclick=async()=>{cfg=await jpost("/github/config/mute",{repo:id,rule:b.dataset.um,days:0});keep()});
}
(async function init(){
  fetch("/status").then(r=>r.json()).then(s=>{if(s.version)$("vb").textContent="v"+s.version}).catch(()=>{});
  try{cfg=await jget("/github/config");render()}catch(e){$("app").innerHTML='<div class="msg ko">'+esc(e.message)+"</div>"}
})();
</script></body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
