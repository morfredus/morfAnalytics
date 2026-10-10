#include "morfanalytics/pages/SiteWatchSettingsPage.h"

namespace morfanalytics::pages {

// Page de configuration des analyses et alertes SiteWatch. Les reglages sont enregistres par
// le service (SQLite, dans son dossier d'etat) : aucun fichier JSON a editer. Toutes les
// valeurs sont bornees cote serveur ; la page ne fait que presenter et envoyer.
QByteArray SiteWatchSettingsPage::render() {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<!--theme-head-->
<title>morfAnalytics - Configuration SiteWatch</title>
<style>
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:1.5rem}
.wrap{max-width:62rem;margin:auto}a{color:var(--accent)}.muted{color:var(--muted)}
.vb{font-size:.8rem;font-weight:600;vertical-align:middle;color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,transparent);border:1px solid color-mix(in srgb,var(--accent) 30%,transparent);border-radius:999px;padding:.1rem .5rem;margin-left:.4rem}
h1{margin:0 0 .3rem}h2{font-size:1rem;margin:0 0 .6rem}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:1rem 1.2rem;margin:1rem 0}
.row{display:grid;grid-template-columns:minmax(12rem,1fr) 2fr;gap:.6rem 1rem;align-items:center;padding:.4rem 0;border-bottom:1px solid var(--line)}
.row:last-child{border-bottom:0}.row small{display:block;color:var(--muted)}
input[type=text],input[type=number],input[type=url],select{background:var(--field);border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.35rem .5rem;font:inherit;width:100%;max-width:28rem}
input[type=number]{max-width:9rem}
button{background:var(--btn);border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.4rem .8rem;cursor:pointer;font:inherit}
button.primary{border-color:var(--accent);color:var(--accent);font-weight:600}button.small{padding:.15rem .5rem;font-size:.8rem}
button.chip.on{border-color:var(--accent);color:var(--accent);font-weight:600}
table{width:100%;border-collapse:collapse;font-size:.9rem}th,td{padding:.4rem .4rem;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}th{color:var(--muted);font-weight:600}
td small{display:block;color:var(--muted)}td input[type=text]{max-width:14rem}
.msg{padding:.6rem .8rem;border-radius:8px;margin:.6rem 0;border:1px solid var(--line)}.ok{border-color:#2e9b57;color:#2e9b57}.ko{border-color:#d6453d;color:#d6453d}
.chips{display:flex;flex-wrap:wrap;gap:.4rem;margin:.4rem 0}.bar{position:sticky;bottom:0;background:var(--bg);padding:.8rem 0;border-top:1px solid var(--line);display:flex;gap:.6rem;flex-wrap:wrap;align-items:center}
.pill{display:inline-block;font-size:.72rem;border:1px solid var(--line);border-radius:999px;padding:.05rem .5rem;color:var(--muted)}
</style></head><body><div class="wrap">
<!--nav-back-->
<h1>Configuration SiteWatch <span id="vb" class="vb"></span><!--theme-toggle--></h1>
<p class="muted"><a href="/sitewatch">&larr; Retour aux analyses</a> · Les réglages sont enregistrés par le service : aucun fichier à éditer. Les valeurs hors limites sont ramenées à des bornes sûres.</p>
<div id="app"><p class="muted">Chargement…</p></div>
</div>
<datalist id="tglist"></datalist>
<script>
"use strict";
const $=id=>document.getElementById(id);
const esc=s=>String(s??"").replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;"}[c]));
const LEVELS=[["","Par défaut"],["info","Information"],["warning","Avertissement"],["error","Erreur"]];
let cfg=null,available=[],dirty=false;
// Modifications non enregistrees : rien ne s'applique tant qu'on n'a pas clique sur Enregistrer, donc on le dit
// (et on previent avant de quitter la page).
function setDirty(v){dirty=v;const d=$("dirty");if(!d)return;
  d.textContent=v?"● Modifications non enregistrées : cliquer sur Enregistrer pour les appliquer":"Les réglages s'appliquent dès l'enregistrement.";
  d.style.color=v?"#e0a030":"";d.style.fontWeight=v?"600":""}
window.addEventListener("beforeunload",e=>{if(dirty){e.preventDefault();e.returnValue=""}});
function onEdit(e){const t=e.target;if(!t.closest||!t.closest("#app"))return;
  if(t.closest("#site")||t.id==="s_site"||t.classList.contains("r_mu"))return;   // le site et la sourdine ont leur propre bouton
  setDirty(true)}
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
  const g=cfg.global,d=cfg.defaults,n=g.notify;
  let h=`<div class="card"><h2>Notifications</h2>
   <div class="row"><div>Envoyer les alertes<small>Coupe tous les envois ; les alertes restent visibles dans l'historique.</small></div><div><label><input type="checkbox" id="n_enabled" ${n.enabled?"checked":""}> Activé</label></div></div>
   <div class="row"><div>Adresse de morfNotify<small>Vide : variable d'environnement MORFNOTIFY_URL, sinon le port du parc. Actuelle : ${esc(cfg.notify_url_effective)}${cfg.notify_url_same_host?" (morfNotify tourne sur la même machine que morfAnalytics)":""}</small></div><div><input type="url" id="n_url" value="${esc(n.url)}" placeholder="${esc(cfg.notify_url_effective)}"></div></div>
   <div class="row"><div>Niveau minimal envoyé<small>Une alerte en dessous de ce niveau est enregistrée sans envoi.</small></div><div><select id="n_min">${["info","warning","error"].map(l=>`<option value="${l}" ${n.min_level===l?"selected":""}>${LEVELS.find(x=>x[0]===l)[1]}</option>`).join("")}</select></div></div>
   <div class="row"><div>Destinations<small>Telegram, mail... selon ce que morfNotify propose. Vide : morfNotify applique ses destinations par défaut.</small></div>
    <div><input type="text" id="n_targets" list="tglist" value="${esc((n.targets||[]).join(", "))}" placeholder="telegram, mail"><div class="chips" id="chips"></div>
    <button id="loadT">Charger les destinations de morfNotify</button> <button id="testN">Envoyer un test</button><div id="tmsg"></div></div></div>
   ${cfg.delivery_backlog>0?`<div class="msg ko">${cfg.delivery_backlog} alerte(s) en attente d'envoi ou en échec : elles seront retentées toutes les heures (5 essais).</div>`:""}
  </div>
  <div class="card"><h2>Détection</h2>`+
   numberRow("sensitivity","Sensibilité","Écart à l'habitude du site au-delà duquel un jour est anormal. Plus c'est haut, moins il y a d'alertes.",g.sensitivity,d.sensitivity,2,10,0.5)+
   numberRow("recent_days","Jours jugés","Nombre de jours complets examinés à chaque analyse.",g.recent_days,d.recent_days,1,7)+
   numberRow("stale_warn_days","Retard de SiteWatch : avertissement (jours)","SiteWatch ne publie plus depuis ce nombre de jours.",g.stale_warn_days,d.stale_warn_days,1,30)+
   numberRow("stale_error_days","Retard de SiteWatch : erreur (jours)","Passe l'alerte de retard en erreur.",g.stale_error_days,d.stale_error_days,1,60)+
   numberRow("e500_error_at","Erreurs 500 : niveau erreur à partir de","Nombre d'erreurs 500 dans la journée (en dessous : avertissement).",g.e500_error_at,d.e500_error_at,1,1000)+
   numberRow("bot_share_points","Part des robots : hausse (points)","Hausse sur 7 jours qui déclenche l'alerte.",g.bot_share_points,d.bot_share_points,5,60)+
   numberRow("ai_growth_factor","Robots IA : facteur de hausse","Multiplication sur 7 jours qui déclenche l'alerte.",g.ai_growth_factor,d.ai_growth_factor,1.2,20,0.1)+
   numberRow("reports_per_site","Rapports conservés par site","Les plus anciens sont supprimés. Les séries quotidiennes ne dépendent pas de cette valeur.",g.retention.reports_per_site,90,5,1000)+
  `</div><div class="card"><h2>Règles d'alerte</h2><table><thead><tr><th>Règle</th><th>Active</th><th>Niveau</th><th>Envoi</th><th>Destinations</th><th>Sourdine</th></tr></thead><tbody>`+
  cfg.rules.map(r=>{
    const s=g.rules[r.id]||{enabled:true,level:"",notify:true,targets:[]};
    const until=g.muted[r.id];const act=until&&until>=new Date().toISOString().slice(0,10);
    return`<tr data-rule="${r.id}"><td><b>${esc(r.label)}</b><small>${esc(r.description)}</small></td>
     <td><input type="checkbox" class="r_en" ${s.enabled?"checked":""}></td>
     <td><select class="r_lv">${LEVELS.map(l=>`<option value="${l[0]}" ${s.level===l[0]?"selected":""}>${l[1]}${l[0]===""?" ("+({info:"info",warning:"avert.",error:"erreur"}[r.default_level])+")":""}</option>`).join("")}</select></td>
     <td><input type="checkbox" class="r_no" ${s.notify?"checked":""}></td>
     <td><input type="text" class="r_tg" list="tglist" value="${esc((s.targets||[]).join(", "))}" placeholder="globales"></td>
     <td><select class="r_mu"><option value="">${act?"jusqu'au "+esc(until):"Aucune"}</option>${[[1,"1 jour"],[7,"7 jours"],[30,"30 jours"],[90,"90 jours"]].map(x=>`<option value="${x[0]}">${x[1]}</option>`).join("")}${act?'<option value="0">Lever la sourdine</option>':""}</select></td></tr>`}).join("")+
  `</tbody></table><p class="muted" style="font-size:.85rem">La sourdine s'applique tout de suite. Une alerte en sourdine reste enregistrée et visible, jamais envoyée.</p></div>
  <div id="gmsg"></div>
  <div class="bar"><button class="primary" id="save">Enregistrer</button><button id="reset">Rétablir les valeurs par défaut</button><span id="dirty" class="muted">Les réglages s'appliquent dès l'enregistrement.</span></div>
  <div class="card"><h2>Réglages par site</h2><p class="muted">Écarts propres à un site, par-dessus les réglages généraux. Un champ vide reprend la valeur générale.</p>
   <div class="row"><div>Site</div><div><select id="s_site">${cfg.sites.map(s=>`<option value="${esc(s.site_id)}">${esc(s.site_label)}</option>`).join("")}</select></div></div><div id="site"></div></div>`;
  $("app").innerHTML=h;
  bindMain();renderSite();renderChips();setDirty(false);
}
function renderChips(){
  const cur=csv($("n_targets").value);
  $("chips").innerHTML=available.map(t=>`<button class="chip small ${cur.includes(t.name)?"on":""}" data-t="${esc(t.name)}" title="${esc(t.type)}">${esc(t.name)} <span class="muted">${esc(t.type)}</span></button>`).join("");
  $("chips").querySelectorAll("button").forEach(b=>b.onclick=()=>{let c=csv($("n_targets").value);c=c.includes(b.dataset.t)?c.filter(x=>x!==b.dataset.t):c.concat(b.dataset.t);$("n_targets").value=c.join(", ");renderChips();setDirty(true)});
  $("tglist").innerHTML=available.map(t=>`<option value="${esc(t.name)}">`).join("");
}
function collect(){
  const rules={};
  document.querySelectorAll("tr[data-rule]").forEach(tr=>{rules[tr.dataset.rule]={enabled:tr.querySelector(".r_en").checked,level:tr.querySelector(".r_lv").value,notify:tr.querySelector(".r_no").checked,targets:csv(tr.querySelector(".r_tg").value)}});
  return{sensitivity:num("sensitivity",3.5),recent_days:num("recent_days",3),stale_warn_days:num("stale_warn_days",2),stale_error_days:num("stale_error_days",3),
    e500_error_at:num("e500_error_at",10),bot_share_points:num("bot_share_points",15),ai_growth_factor:num("ai_growth_factor",2),rules,
    notify:{enabled:$("n_enabled").checked,url:$("n_url").value.trim(),min_level:$("n_min").value,targets:csv($("n_targets").value)},
    retention:{reports_per_site:num("reports_per_site",90)}};
}
function bindMain(){
  $("n_targets").oninput=renderChips;
  $("loadT").onclick=async()=>{msg("tmsg",true,"");const r=await jget("/sitewatch/config/targets");
    if(!r.ok){msg("tmsg",false,"morfNotify injoignable ("+r.url+") : "+r.error);return}
    available=r.targets;renderChips();msg("tmsg",true,r.targets.length+" destination(s) disponible(s) dans morfNotify.")};
  $("testN").onclick=async()=>{msg("tmsg",true,"Envoi en cours…");try{const r=await jpost("/sitewatch/config/test",{targets:csv($("n_targets").value)});
    const dest=r.targets.length?" (destinations : "+r.targets.join(", ")+")":" (destinations par défaut)";
    if(!r.ok)msg("tmsg",false,"Échec : "+(r.error||("HTTP "+r.http))+" ("+r.url+")");
    else if(r.delivery_checked&&r.delivery_failures>0)msg("tmsg",false,"Acceptée par morfNotify"+dest+", mais "+r.delivery_failures+" livraison(s) ont ÉCHOUÉ : la destination n'est pas configurée correctement dans morfNotify (jeton Telegram, SMTP, adresse du webhook…). Détail : journal de morfNotify sur "+r.url.replace(/^https?:\/\/([^:\/]+).*$/,"$1")+".");
    else if(r.delivery_checked)msg("tmsg",true,"Notification de test livrée sans erreur par morfNotify"+dest+".");
    else msg("tmsg",true,"Acceptée par morfNotify"+dest+" (livraison non vérifiable).")}catch(e){msg("tmsg",false,e.message)}};
  $("save").onclick=async()=>{try{cfg=await jpost("/sitewatch/config",{scope:"global",settings:collect()});render();msg("gmsg",true,"Réglages enregistrés.")}catch(e){msg("gmsg",false,e.message)}};
  $("reset").onclick=()=>{const d=cfg.defaults;for(const k of["sensitivity","recent_days","stale_warn_days","stale_error_days","e500_error_at","bot_share_points","ai_growth_factor"])$(k).value=d[k];$("reports_per_site").value=90;
    document.querySelectorAll("tr[data-rule]").forEach(tr=>{tr.querySelector(".r_en").checked=true;tr.querySelector(".r_lv").value="";tr.querySelector(".r_no").checked=true;tr.querySelector(".r_tg").value=""});setDirty(true);msg("gmsg",true,"Valeurs par défaut chargées : cliquer sur Enregistrer pour les appliquer.")};
  document.querySelectorAll("tr[data-rule] .r_mu").forEach(sel=>sel.onchange=async()=>{if(sel.value==="")return;
    if(dirty){msg("gmsg",false,"Enregistrer d'abord les modifications en cours : la sourdine recharge la page.");sel.value="";return}
    const rule=sel.closest("tr").dataset.rule;
    try{cfg=await jpost("/sitewatch/config/mute",{rule,days:parseInt(sel.value)});render()}catch(e){msg("gmsg",false,e.message)}});
  $("s_site").onchange=renderSite;
}
function renderSite(){
  const id=$("s_site")?.value;if(!id){return}
  const s=cfg.sites.find(x=>x.site_id===id)||{overrides:{}},o=s.overrides||{},rules=o.rules||{},muted=o.muted||{};
  const today=new Date().toISOString().slice(0,10);
  $("site").innerHTML=`<div class="row"><div>Sensibilité<small>Vide : générale (${cfg.global.sensitivity}).</small></div><div><input type="number" id="o_sens" min="2" max="10" step="0.5" value="${o.sensitivity??""}"></div></div>
   <div class="row"><div>Jours jugés<small>Vide : générale (${cfg.global.recent_days}).</small></div><div><input type="number" id="o_days" min="1" max="7" value="${o.recent_days??""}"></div></div>
   <div class="row"><div>Règles désactivées pour ce site</div><div>${cfg.rules.map(r=>`<label style="display:block"><input type="checkbox" class="o_off" data-r="${r.id}" ${rules[r.id]&&rules[r.id].enabled===false?"checked":""}> ${esc(r.label)}</label>`).join("")}</div></div>
   <div class="row"><div>Sourdines en cours</div><div>${Object.keys(muted).filter(k=>muted[k]>=today).map(k=>`<span class="pill">${k==="*"?"toutes les règles":esc((cfg.rules.find(r=>r.id===k)||{label:k}).label)} jusqu'au ${esc(muted[k])} <button class="small" data-um="${esc(k)}">lever</button></span>`).join(" ")||'<span class="muted">aucune</span>'}</div></div>
   <div id="smsg"></div><button class="primary" id="saveSite">Enregistrer ce site</button> <button id="clearSite">Effacer les écarts de ce site</button> <button id="muteSite">Sourdine de 7 jours sur toutes les règles</button>`;
  $("saveSite").onclick=async()=>{const set={};if($("o_sens").value!=="")set.sensitivity=parseFloat($("o_sens").value);if($("o_days").value!=="")set.recent_days=parseInt($("o_days").value);
    const r={};document.querySelectorAll(".o_off").forEach(c=>{if(c.checked)r[c.dataset.r]={enabled:false}});if(Object.keys(r).length)set.rules=r;
    if(Object.keys(muted).length)set.muted=muted;
    try{cfg=await jpost("/sitewatch/config",{scope:id,settings:set});render();$("s_site").value=id;renderSite();msg("smsg",true,"Réglages du site enregistrés.")}catch(e){msg("smsg",false,e.message)}};
  $("clearSite").onclick=async()=>{try{cfg=await jpost("/sitewatch/config",{scope:id,settings:{}});render();$("s_site").value=id;renderSite();msg("smsg",true,"Écarts effacés.")}catch(e){msg("smsg",false,e.message)}};
  $("muteSite").onclick=async()=>{try{cfg=await jpost("/sitewatch/config/mute",{site_id:id,rule:"*",days:7});render();$("s_site").value=id;renderSite()}catch(e){msg("smsg",false,e.message)}};
  $("site").querySelectorAll("button[data-um]").forEach(b=>b.onclick=async()=>{cfg=await jpost("/sitewatch/config/mute",{site_id:id,rule:b.dataset.um,days:0});render();$("s_site").value=id;renderSite()});
}
(async function init(){
  fetch("/status").then(r=>r.json()).then(s=>{if(s.version)$("vb").textContent="v"+s.version}).catch(()=>{});
  try{cfg=await jget("/sitewatch/config");render()}catch(e){$("app").innerHTML='<div class="msg ko">'+esc(e.message)+"</div>"}
})();
</script></body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
