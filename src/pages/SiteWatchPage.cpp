#include "morfanalytics/pages/SiteWatchPage.h"

namespace morfanalytics::pages {

// Page d'analyse approfondie des sites suivis par SiteWatch. Tout le calcul est fait cote
// serveur (SiteWatchInsights) ; la page ne fait que filtrer, comparer et dessiner ce que
// /sitewatch/insights et /sitewatch/overview renvoient. Les filtres vivent dans l'URL
// (lien partageable). Les reglages sont sur une page a part (/sitewatch/settings).
QByteArray SiteWatchPage::render() {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<!--theme-head-->
<title>morfAnalytics - SiteWatch</title>
<style>
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:1.5rem}
.wrap{max-width:78rem;margin:auto}a{color:var(--accent)}.muted{color:var(--muted)}
.vb{font-size:.8rem;font-weight:600;vertical-align:middle;color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,transparent);border:1px solid color-mix(in srgb,var(--accent) 30%,transparent);border-radius:999px;padding:.1rem .5rem;margin-left:.4rem}
h1{margin:0 0 .3rem}h2{font-size:.8rem;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin:2rem 0 .7rem;font-weight:600}
.bar{display:flex;flex-wrap:wrap;gap:.7rem;align-items:end;margin:1rem 0;position:sticky;top:0;z-index:10;background:var(--bg);padding:.5rem 0;border-bottom:1px solid var(--line)}
label{display:flex;flex-direction:column;gap:.2rem;font-size:.8rem;color:var(--muted)}
select,input{background:var(--field);border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.35rem .5rem;font:inherit}
button,a.btn{background:var(--btn);border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.4rem .8rem;cursor:pointer;font:inherit;text-decoration:none;display:inline-block}
button.on{border-color:var(--accent);color:var(--accent);font-weight:600}button.small{padding:.15rem .5rem;font-size:.78rem}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(10.5rem,1fr));gap:.8rem}
.tile{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:.8rem}
.tile .k{font-size:.72rem;letter-spacing:.05em;text-transform:uppercase;color:var(--muted)}
.tile .n{font-size:1.6rem;font-weight:700;font-variant-numeric:tabular-nums}
.good{color:#2e9b57}.bad{color:#d6453d}.flat{color:var(--muted)}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:1rem;margin:.8rem 0}
.alert{border-left:4px solid var(--line);padding:.5rem .8rem;margin:.4rem 0;background:var(--card);border-radius:0 8px 8px 0}
.alert.error{border-color:#d6453d}.alert.warning{border-color:#e0a030}.alert.info{border-color:#4a90d9}.alert.muted{opacity:.6}
.alert b{display:block}.pill{display:inline-block;font-size:.72rem;border:1px solid var(--line);border-radius:999px;padding:.05rem .5rem;margin-left:.3rem;color:var(--muted)}
.pill.err{border-color:#d6453d;color:#d6453d}.pill.warn{border-color:#e0a030;color:#e0a030}
table{width:100%;border-collapse:collapse;font-size:.9rem}th,td{padding:.35rem .5rem;text-align:left;border-bottom:1px solid var(--line)}
th{color:var(--muted)}td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}
tr.row{cursor:pointer}tr.row:hover{background:color-mix(in srgb,var(--accent) 8%,transparent)}
.two{display:grid;grid-template-columns:repeat(auto-fit,minmax(24rem,1fr));gap:1rem}
.three{display:grid;grid-template-columns:repeat(auto-fit,minmax(16rem,1fr));gap:1rem}
.legend{display:flex;flex-wrap:wrap;gap:.8rem;margin:.4rem 0}.legend label{flex-direction:row;align-items:center;gap:.3rem;color:var(--ink);font-size:.85rem;cursor:pointer}
.sw{width:.8rem;height:.8rem;border-radius:3px;display:inline-block}
#chart{width:100%;height:20rem;display:block}#tip{position:fixed;pointer-events:none;background:var(--card);border:1px solid var(--line);border-radius:8px;padding:.4rem .6rem;font-size:.8rem;display:none;z-index:30;box-shadow:0 4px 14px #0003}
.stack{display:flex;height:.8rem;border-radius:4px;overflow:hidden;margin:.4rem 0}.stack span{display:block;height:100%}
.err{background:var(--err);border:1px solid var(--err-line);padding:.8rem;border-radius:10px}
.cells{display:grid;grid-template-columns:repeat(7,1fr);gap:.4rem;align-items:end;height:8rem}.cells div{display:flex;flex-direction:column;justify-content:flex-end;align-items:center;font-size:.72rem;color:var(--muted);height:100%}.cells i{display:block;width:70%;background:var(--accent);border-radius:4px 4px 0 0;min-height:2px}
.hours{width:100%;height:6rem;display:block}
</style></head><body><div class="wrap">
<!--nav-back-->
<h1>Analyses SiteWatch <span id="vb" class="vb"></span><!--theme-toggle--></h1>
<p class="muted">Série quotidienne consolidée par SiteWatch. Les classements (pages, robots, référents, liens cassés) portent sur la période du rapport, pas sur la fenêtre choisie. <a href="/sitewatch/settings">Configurer les alertes</a></p>
<div class="bar">
  <label>Site<select id="site"></select></label>
  <div><div class="muted" style="font-size:.8rem;margin-bottom:.2rem">Période</div><span id="presets"></span></div>
  <label>Du<input type="date" id="from"></label><label>Au<input type="date" id="to"></label>
  <button id="apply">Appliquer</button>
</div>
<div id="app"><p class="muted">Chargement…</p></div>
<div id="tip"></div>
</div>
<script>
"use strict";
const $=id=>document.getElementById(id);
const esc=s=>String(s??"").replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;"}[c]));
const fmt=n=>new Intl.NumberFormat("fr-FR",{maximumFractionDigits:1}).format(n);
const SER=[["humans","Humains","#2e86de"],["bots","Robots","#8e6bbf"],["ai","Robots IA","#e0457b"],["seo","Robots SEO","#16a085"],
  ["e404","Erreurs 404","#e0a030"],["e403","Refus 403","#c0762b"],["e500","Erreurs 500","#d6453d"],["attacks","Attaques","#7a1f1f"],["normal","Activité WordPress","#7f8c8d"]];
const GOODUP={humans:1,normal:1},NEUTRAL={requests:1,seo:1};
const PRESETS=[[7,"7 j"],[30,"30 j"],[60,"60 j"],[90,"90 j"],[180,"180 j"],[0,"Tout"]];
const CAT={ia:"Robots IA",seo:"SEO",moteur:"Moteurs",social:"Réseaux",autre:"Autres"};
const DELIV={sent:"envoyée",failed:"envoi échoué",pending:"en attente d'envoi",skipped:"enregistrée sans envoi",muted:"en sourdine"};
let sites=[],data=null,visible=new Set(["humans","bots","e404","attacks"]),catFilter=new Set(),textFilter="";
function qs(){return new URLSearchParams(location.search)}
function setQs(o){const p=qs();for(const k in o){o[k]?p.set(k,o[k]):p.delete(k)}history.replaceState(null,"","?"+p.toString())}
async function jget(u){const r=await fetch(u);if(!r.ok)throw new Error(u+" : HTTP "+r.status);return r.json()}
async function jpost(u,o){const r=await fetch(u,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(o)});if(!r.ok)throw new Error(u+" : HTTP "+r.status);return r.json()}
function periodQs(){const p=qs();let s="";if(p.get("from"))s+="&from="+p.get("from");if(p.get("to"))s+="&to="+p.get("to");if(p.get("days"))s+="&days="+p.get("days");return s}

function delta(k,key,suffix){
  const o=k[key];if(!o)return"";const pts=suffix==="pts";
  const d=pts?o.value-o.prev:o.delta_pct;
  if(d===undefined||d===null)return'<span class="flat">-</span>';
  const up=d>0.05,down=d<-0.05;
  // Plus de visiteurs = bien ; plus d'erreurs, d'attaques ou de robots = mal ; le volume brut est neutre.
  const cls=(!up&&!down)||NEUTRAL[key]?"flat":((up&&GOODUP[key])||(down&&!GOODUP[key])?"good":"bad");
  return`<span class="${cls}">${up?"▲":down?"▼":"•"} ${d>0?"+":""}${fmt(d)}${pts?" pts":" %"}</span>`;
}
function tile(label,k,key,suffix){
  const o=k[key];if(!o)return"";
  return`<div class="tile"><div class="k">${label}</div><div class="n">${fmt(o.value)}${suffix==="pts"?" %":""}</div><div class="muted" style="font-size:.8rem">${delta(k,key,suffix)} <span class="muted">vs période précédente</span></div></div>`;
}
function alertHtml(a,withMute){
  const mute=withMute&&!a.muted?` <button class="small" data-mute="${esc(a.rule)}" title="Ne plus envoyer cette règle pendant 7 jours">Sourdine 7 j</button>`:"";
  return`<div class="alert ${esc(a.level)}${a.muted?" muted":""}"><b>${esc(a.title)}<span class="pill">${esc(a.rule)}</span>${a.muted?'<span class="pill">en sourdine</span>':""}${mute}</b>${esc(a.message)}</div>`;
}

// ---- Vue d'ensemble de tous les sites ---------------------------------------
async function overview(){
  const r=await jget("/sitewatch/overview?"+periodQs().replace(/^&/,""));
  if(!r.sites.length){$("app").innerHTML='<div class="card">Aucune synthèse SiteWatch reçue pour l\'instant.</div>';return}
  const w=r.sites[0].window;
  $("from").value=w.from;$("to").value=w.to;
  let h=`<h2>Vue d'ensemble · ${esc(w.from)} → ${esc(w.to)}</h2><div class="card"><table><thead><tr><th>Site</th><th>Données</th><th class="num">Visites humaines</th><th class="num">Part des robots</th><th class="num">Erreurs 500</th><th class="num">Attaques</th><th class="num">Taux d'erreur</th><th>Alertes</th></tr></thead><tbody>`;
  for(const s of r.sites){
    const k=s.kpis,fr=s.freshness,al=s.alerts;
    const lag=fr.lag_days>=2?`<span class="pill ${fr.lag_days>=3?"err":"warn"}">${fr.lag_days} j de retard</span>`:`<span class="muted">${esc(fr.up_to)}</span>`;
    const badges=(al.error?`<span class="pill err">${al.error} erreur</span>`:"")+(al.warning?`<span class="pill warn">${al.warning} avert.</span>`:"")+(al.muted?`<span class="pill">${al.muted} en sourdine</span>`:"")+(!al.error&&!al.warning&&!al.muted?'<span class="muted">aucune</span>':"");
    h+=`<tr class="row" data-site="${esc(s.site_id)}"><td><b>${esc(s.site_label)}</b></td><td>${lag}</td><td class="num">${fmt(k.humans.value)} ${delta(k,"humans")}</td><td class="num">${fmt(k.bot_share_pct.value)} %</td><td class="num ${k.e500.value>0?"bad":""}">${fmt(k.e500.value)}</td><td class="num">${fmt(k.attacks.value)}</td><td class="num">${fmt(k.error_rate_pct.value)} %</td><td>${badges}</td></tr>`;
  }
  $("app").innerHTML=h+`</tbody></table></div><p class="muted">Cliquer sur un site pour ouvrir son analyse détaillée.</p>`;
  $("app").querySelectorAll("tr.row").forEach(tr=>tr.onclick=()=>{$("site").value=tr.dataset.site;setQs({site:tr.dataset.site});load()});
}

// ---- Graphique quotidien -----------------------------------------------------
function chart(d){
  const dates=d.series.dates,W=1000,H=320,L=48,R=10,T=10,B=28;
  const act=SER.filter(s=>visible.has(s[0]));
  let max=1;for(const s of act)for(const v of d.series[s[0]])if(v>max)max=v;
  const X=i=>L+(dates.length<2?0:i*(W-L-R)/(dates.length-1)),Y=v=>T+(H-T-B)*(1-v/max);
  let g="";
  for(let t=0;t<=4;t++){const v=max*t/4,y=Y(v);g+=`<line x1="${L}" x2="${W-R}" y1="${y}" y2="${y}" stroke="var(--line)"/><text x="${L-6}" y="${y+4}" text-anchor="end" font-size="11" fill="var(--muted)">${fmt(Math.round(v))}</text>`}
  const step=Math.max(1,Math.ceil(dates.length/8));
  dates.forEach((dt,i)=>{if(i%step===0)g+=`<text x="${X(i)}" y="${H-8}" text-anchor="middle" font-size="11" fill="var(--muted)">${dt.slice(5)}</text>`});
  for(const s of act){const pts=d.series[s[0]].map((v,i)=>X(i)+","+Y(v)).join(" ");g+=`<polyline fill="none" stroke="${s[2]}" stroke-width="2" points="${pts}"/>`}
  for(const a of d.anomalies){const i=dates.indexOf(a.day),s=SER.find(x=>x[0]===a.series);
    if(i>=0&&visible.has(a.series))g+=`<circle cx="${X(i)}" cy="${Y(d.series[a.series][i])}" r="5" fill="none" stroke="${s[2]}" stroke-width="2"><title>${esc(s[1])} ${esc(a.day)} : ${fmt(a.value)} (anomalie)</title></circle>`}
  g+=`<line id="cur" y1="${T}" y2="${H-B}" stroke="var(--muted)" stroke-dasharray="3" style="display:none"/>`;
  return`<svg id="chart" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">${g}<rect id="hit" x="${L}" y="${T}" width="${W-L-R}" height="${H-T-B}" fill="transparent"/></svg>`;
}
function bindChart(d){
  const svg=$("chart"),hit=$("hit"),tip=$("tip"),cur=$("cur");if(!svg)return;
  const n=d.series.dates.length,L=48,R=10,W=1000;
  hit.onmousemove=e=>{
    const r=svg.getBoundingClientRect(),x=(e.clientX-r.left)/r.width*W;
    const i=Math.max(0,Math.min(n-1,Math.round((x-L)/(W-L-R)*(n-1))));
    const cx=L+(n<2?0:i*(W-L-R)/(n-1));cur.setAttribute("x1",cx);cur.setAttribute("x2",cx);cur.style.display="";
    tip.innerHTML="<b>"+esc(d.series.dates[i])+"</b><br>"+SER.filter(s=>visible.has(s[0])).map(s=>`<span class="sw" style="background:${s[2]}"></span> ${s[1]} : ${fmt(d.series[s[0]][i])}`).join("<br>");
    tip.style.display="block";tip.style.left=Math.min(innerWidth-200,e.clientX+14)+"px";tip.style.top=(e.clientY+14)+"px";
  };
  hit.onmouseleave=()=>{tip.style.display="none";cur.style.display="none"};
}
function hoursChart(hours,key,color,label){
  const max=Math.max(1,...hours.map(h=>h[key])),tot=hours.reduce((a,h)=>a+h[key],0);
  const peak=hours.reduce((a,h)=>h[key]>a[key]?h:a,hours[0]);
  const bars=hours.map(h=>{const bh=h[key]/max*60;return`<rect x="${h.hour*10+1}" y="${64-bh}" width="8" height="${bh}" fill="${color}"><title>${h.hour} h : ${fmt(h[key])}</title></rect>`}).join("");
  const lab=[0,6,12,18,23].map(x=>`<text x="${x*10+5}" y="76" font-size="8" text-anchor="middle" fill="var(--muted)">${x} h</text>`).join("");
  return`<div class="card"><b>${label}</b><div class="muted" style="font-size:.8rem">${tot?"Pic à "+peak.hour+" h ("+fmt(peak[key]*100/tot)+" % du total)":"Aucune donnée"}</div><svg class="hours" viewBox="0 0 240 80" preserveAspectRatio="none">${bars}${lab}</svg></div>`;
}
function topTable(title,list,opts={}){
  const f=textFilter.toLowerCase();
  let rows=(list||[]).filter(r=>(!f||r.name.toLowerCase().includes(f))&&(!opts.cat||!catFilter.size||catFilter.has(r.category)));
  const total=rows.reduce((a,r)=>a+r.count,0);
  return`<div class="card"><b>${title}</b> <span class="muted">(${rows.length})</span>${opts.note?`<div class="muted" style="font-size:.8rem">${opts.note}</div>`:""}<table><thead><tr><th>Nom</th>${opts.cat?"<th>Famille</th>":""}<th class="num">Nombre</th><th class="num">Part</th></tr></thead><tbody>`+
    rows.slice(0,25).map(r=>`<tr><td style="word-break:break-all">${esc(r.name)}</td>${opts.cat?`<td>${esc(CAT[r.category]||r.category)}</td>`:""}<td class="num">${fmt(r.count)}</td><td class="num">${total?fmt(r.count*100/total)+" %":""}</td></tr>`).join("")+
    `</tbody></table></div>`;
}
function moversTable(title,list,sign){
  return`<div class="card"><b>${title}</b><table><thead><tr><th>Page</th><th class="num">7 derniers jours</th><th class="num">7 précédents</th><th class="num">Écart</th></tr></thead><tbody>`+
    ((list||[]).length?list.map(r=>`<tr><td style="word-break:break-all">${esc(r.name)}</td><td class="num">${fmt(r.recent)}</td><td class="num">${fmt(r.prior)}</td><td class="num ${sign>0?"good":"bad"}">${r.delta>0?"+":""}${fmt(r.delta)}</td></tr>`).join(""):'<tr><td colspan="4" class="muted">Aucun mouvement notable.</td></tr>')+`</tbody></table></div>`;
}

function render(){
  const d=data,k=d.kpis,app=$("app");
  if(!d.has_data){app.innerHTML='<div class="card">Aucune donnée quotidienne pour ce site : lancer une analyse dans SiteWatch (ou sitewatch-publish).</div>';return}
  const fr=d.freshness;let h="";
  if(fr.lag_days>=2)h+=`<div class="alert ${fr.lag_days>=3?"error":"warning"}"><b>Données en retard</b>Dernières données du ${esc(fr.up_to)} : ${fr.lag_days} jours de retard. SiteWatch ne publie plus.</div>`;
  else h+=`<p class="muted">Données jusqu'au ${esc(fr.up_to)}${fr.received_at?" · synthèse reçue le "+new Date(fr.received_at*1000).toLocaleString("fr-FR"):""}</p>`;
  h+=`<h2>Alertes</h2>`+(d.alerts.length?d.alerts.map(a=>alertHtml(a,true)).join(""):'<p class="muted">Aucune alerte sur les derniers jours.</p>')+
     `<p><a href="#" id="hist">Historique des alertes et de leur envoi</a></p><div id="histbox"></div>`;
  const q=periodQs().replace(/^&/,"");
  h+=`<h2>Indicateurs · ${esc(d.window.from)} → ${esc(d.window.to)} (${d.window.days} j)</h2><p><a class="btn" download href="/sitewatch/export?site=${encodeURIComponent($("site").value)}&format=csv&${q}">Exporter CSV</a> <a class="btn" download href="/sitewatch/export?site=${encodeURIComponent($("site").value)}&format=json&${q}">Exporter JSON</a></p><div class="grid">`+
     tile("Requêtes",k,"requests")+tile("Visites humaines",k,"humans")+tile("Robots",k,"bots")+tile("Robots IA",k,"ai")+tile("Robots SEO",k,"seo")+
     tile("Erreurs 404",k,"e404")+tile("Erreurs 500",k,"e500")+tile("Refus 403",k,"e403")+tile("Attaques",k,"attacks")+
     tile("Part des robots",k,"bot_share_pct","pts")+tile("Taux d'erreur",k,"error_rate_pct","pts")+tile("IA parmi les robots",k,"ai_share_of_bots_pct","pts")+`</div>`;
  h+=`<h2>Évolution quotidienne</h2><div class="card"><div class="legend">`+SER.map(s=>`<label><input type="checkbox" data-s="${s[0]}" ${visible.has(s[0])?"checked":""}><span class="sw" style="background:${s[2]}"></span>${s[1]}</label>`).join("")+`</div>`+chart(d)+`<p class="muted" style="font-size:.8rem">Cercles = jours anormaux (écart robuste à la médiane de l'historique).</p></div>`;
  h+=`<div class="two"><div><h2>Jours anormaux</h2><div class="card">`+(d.anomalies.length?`<table><thead><tr><th>Jour</th><th>Série</th><th class="num">Valeur</th><th class="num">Habituel</th></tr></thead><tbody>`+d.anomalies.filter(a=>visible.has(a.series)).map(a=>{const s=SER.find(x=>x[0]===a.series);return`<tr><td>${esc(a.day)}</td><td><span class="sw" style="background:${s[2]}"></span> ${s[1]} ${a.direction==="baisse"?"▼":"▲"}</td><td class="num">${fmt(a.value)}</td><td class="num">${fmt(a.baseline)}</td></tr>`}).join("")+`</tbody></table><p class="muted" style="font-size:.8rem">Séries affichées sur le graphique uniquement.</p>`:'<span class="muted">Aucun jour anormal sur la fenêtre.</span>')+`</div></div>`;
  const wd=d.weekday,wm=Math.max(1,...wd.map(x=>x.humans)),names=["Lun","Mar","Mer","Jeu","Ven","Sam","Dim"];
  h+=`<div><h2>Profil de la semaine (visites humaines)</h2><div class="card"><div class="cells">`+wd.map((x,i)=>`<div title="${fmt(x.humans)} en moyenne"><i style="height:${x.humans/wm*100}%"></i>${names[i]}<br>${fmt(Math.round(x.humans))}</div>`).join("")+`</div></div></div></div>`;
  h+=`<h2>Profil horaire · période du rapport</h2><div class="three">`+hoursChart(d.hourly,"humans","#2e86de","Visites humaines")+hoursChart(d.hourly,"bots","#8e6bbf","Robots")+hoursChart(d.hourly,"attacks","#7a1f1f","Tentatives d'attaque")+`</div>`;
  const sc=d.status_classes||{},st=(sc["2xx"]||0)+(sc["3xx"]||0)+(sc["4xx"]||0)+(sc["5xx"]||0);
  if(st)h+=`<h2>Codes HTTP (période du rapport)</h2><div class="card"><div class="stack">`+[["2xx","#2e9b57"],["3xx","#4a90d9"],["4xx","#e0a030"],["5xx","#d6453d"]].map(c=>`<span style="width:${(sc[c[0]]||0)*100/st}%;background:${c[1]}" title="${c[0]} : ${fmt(sc[c[0]]||0)}"></span>`).join("")+`</div><span class="muted">`+["2xx","3xx","4xx","5xx"].map(c=>`${c} : ${fmt(sc[c]||0)} (${fmt((sc[c]||0)*100/st)} %)`).join(" · ")+`</span></div>`;
  const pm=d.page_movers;
  h+=`<h2>Pages en mouvement · 7 derniers jours contre les 7 précédents</h2><div class="two">`+moversTable("Pages en hausse",pm.rising,1)+moversTable("Pages en baisse",pm.falling,-1)+`</div>`;
  if(d.new_bots.length)h+=`<h2>Nouveaux robots</h2><div class="card"><table><thead><tr><th>Robot</th><th>Famille</th><th class="num">Requêtes (7 j)</th></tr></thead><tbody>`+d.new_bots.map(b=>`<tr><td>${esc(b.name)}</td><td>${esc(CAT[b.category]||b.category)}</td><td class="num">${fmt(b.count)}</td></tr>`).join("")+`</tbody></table></div>`;
  h+=`<h2>Classements · rapport du ${esc(d.report_period.from)} au ${esc(d.report_period.to)}</h2><div class="bar" style="position:static;border:0;padding:0"><label>Filtrer les noms<input id="tf" value="${esc(textFilter)}" placeholder="texte…"></label><div><div class="muted" style="font-size:.8rem;margin-bottom:.2rem">Familles de robots</div>`+Object.keys(CAT).map(c=>`<button data-c="${c}" class="${catFilter.has(c)?"on":""}">${CAT[c]}</button>`).join(" ")+`</div></div>`;
  const t=d.tops,bc=t.bots.by_category||{};
  h+=`<p class="muted">Robots par famille : `+Object.keys(CAT).filter(c=>bc[c]).map(c=>`${CAT[c]} ${fmt(bc[c])}`).join(" · ")+`</p>`;
  h+=`<div class="two">`+topTable("Liens cassés probables",t.broken,{note:"Pages introuvables (404) qui ne sont pas des scans : à corriger ou rediriger."})+topTable("Pages les plus visitées",t.pages)+topTable("URL les plus attaquées",t.attacked)+topTable("Robots",t.bots.list,{cat:1})+topTable("Provenance (référents)",t.referers)+topTable("Types d'attaque",t.attack_activity)+topTable("Activité WordPress",t.normal_activity)+`</div>`;
  app.innerHTML=h;
  bindChart(d);
  app.querySelectorAll("input[data-s]").forEach(i=>i.onchange=()=>{i.checked?visible.add(i.dataset.s):visible.delete(i.dataset.s);setQs({s:[...visible].join(",")});render()});
  app.querySelectorAll("button[data-c]").forEach(b=>b.onclick=()=>{catFilter.has(b.dataset.c)?catFilter.delete(b.dataset.c):catFilter.add(b.dataset.c);render()});
  app.querySelectorAll("button[data-mute]").forEach(b=>b.onclick=async()=>{await jpost("/sitewatch/config/mute",{site_id:$("site").value,rule:b.dataset.mute,days:7});load()});
  const tf=$("tf");tf.oninput=()=>{textFilter=tf.value;const pos=tf.selectionStart;render();const n=$("tf");n.focus();n.setSelectionRange(pos,pos)};
  $("hist").onclick=async e=>{e.preventDefault();const r=await jget("/sitewatch/alerts?site="+encodeURIComponent($("site").value)+"&limit=50");
    $("histbox").innerHTML=r.alerts.length?r.alerts.map(a=>alertHtml(a,false)+`<div class="muted" style="font-size:.75rem;margin:-.2rem 0 .4rem .8rem">${new Date(a.created_at*1000).toLocaleString("fr-FR")} · ${esc(DELIV[a.delivery]||a.delivery)}${a.attempts?" ("+a.attempts+" essai(s))":""}${a.last_error?" · "+esc(a.last_error):""}</div>`).join(""):'<p class="muted">Aucune alerte enregistrée.</p>'};
}
async function load(){
  const site=$("site").value;
  document.querySelectorAll("#presets button").forEach(b=>b.classList.toggle("on",!qs().get("from")&&!qs().get("to")&&(b.dataset.d===(qs().get("days")||"30"))));
  try{
    if(!site){await overview();return}
    data=await jget("/sitewatch/insights?site="+encodeURIComponent(site)+periodQs());
    if(data.window){$("from").value=data.window.from;$("to").value=data.window.to}render();
  }catch(e){$("app").innerHTML='<div class="err">'+esc(e.message)+"</div>"}
}
(async function init(){
  fetch("/status").then(r=>r.json()).then(s=>{if(s.version)$("vb").textContent="v"+s.version}).catch(()=>{});
  const p=qs();if(p.get("s"))visible=new Set(p.get("s").split(",").filter(Boolean));
  $("presets").innerHTML=PRESETS.map(x=>`<button data-d="${x[0]||"all"}">${x[1]}</button>`).join(" ");
  document.querySelectorAll("#presets button").forEach(b=>b.onclick=()=>{setQs({days:b.dataset.d,from:"",to:""});load()});
  $("apply").onclick=()=>{setQs({from:$("from").value,to:$("to").value,days:""});load()};
  $("site").onchange=()=>{setQs({site:$("site").value});load()};
  try{
    const l=await jget("/sitewatch/insights");sites=l.sites||[];
    if(!sites.length){$("app").innerHTML='<div class="card">Aucune synthèse SiteWatch reçue pour l\'instant.</div>';return}
    $("site").innerHTML='<option value="">Tous les sites</option>'+sites.map(s=>`<option value="${esc(s.site_id)}">${esc(s.site_label)}${s.lag_days>=2?" ("+s.lag_days+" j de retard)":""}</option>`).join("");
    if(p.get("site")&&sites.some(s=>s.site_id===p.get("site")))$("site").value=p.get("site");
    load();
  }catch(e){$("app").innerHTML='<div class="err">'+esc(e.message)+"</div>"}
})();
</script></body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
