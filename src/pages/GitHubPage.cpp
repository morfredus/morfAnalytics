#include "morfanalytics/pages/GitHubPage.h"

namespace morfanalytics::pages {

// Page d'analyse du trafic GitHub, alimentee par SiteWatch (POST /github/ingest). Le calcul des
// totaux est fait cote serveur (/github/data) ; la page filtre, compare a la periode precedente et
// dessine. Les filtres vivent dans l'URL (lien partageable). Les alertes se reglent sur
// /github/settings.
QByteArray GitHubPage::render() {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<!--theme-head-->
<title>morfAnalytics - GitHub</title>
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
.tile .n{font-size:1.6rem;font-weight:700;font-variant-numeric:tabular-nums}.tile .s{font-size:.8rem}
.good{color:#2e9b57}.bad{color:#d6453d}.flat{color:var(--muted)}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:1rem;margin:.8rem 0}
.alert{border-left:4px solid var(--line);padding:.5rem .8rem;margin:.4rem 0;background:var(--card);border-radius:0 8px 8px 0}
.alert.error{border-color:#d6453d}.alert.warning{border-color:#e0a030}.alert.info{border-color:#4a90d9}.alert.muted{opacity:.6}
.alert b{display:block}.alert small{display:block;color:var(--muted)}
.pill{display:inline-block;font-size:.72rem;border:1px solid var(--line);border-radius:999px;padding:.05rem .5rem;margin-left:.3rem;color:var(--muted)}
.pill.err{border-color:#d6453d;color:#d6453d}.pill.warn{border-color:#e0a030;color:#e0a030}
table{width:100%;border-collapse:collapse;font-size:.9rem}th,td{padding:.4rem .5rem;text-align:left;border-bottom:1px solid var(--line)}
th{color:var(--muted);user-select:none}th.s{cursor:pointer}th.s:hover{color:var(--ink)}td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}
tr.row{cursor:pointer}tr.row:hover{background:color-mix(in srgb,var(--accent) 8%,transparent)}
.two{display:grid;grid-template-columns:repeat(auto-fit,minmax(24rem,1fr));gap:1rem}
.legend{display:flex;flex-wrap:wrap;gap:.8rem;margin:.4rem 0}.legend label{flex-direction:row;align-items:center;gap:.3rem;color:var(--ink);font-size:.85rem;cursor:pointer}
.sw{width:.8rem;height:.8rem;border-radius:3px;display:inline-block}
#chart{width:100%;height:18rem;display:block}#tip{position:fixed;pointer-events:none;background:var(--card);border:1px solid var(--line);border-radius:8px;padding:.4rem .6rem;font-size:.8rem;display:none;z-index:30;box-shadow:0 4px 14px #0003}
.err{background:var(--err);border:1px solid var(--err-line);padding:.8rem;border-radius:10px}
.brow{display:grid;grid-template-columns:minmax(6rem,14rem) 1fr 4rem;gap:.6rem;align-items:center;padding:.2rem 0;font-size:.88rem}
.brow .bl{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.brow .bt{height:.6rem;background:color-mix(in srgb,var(--line) 60%,transparent);border-radius:4px;overflow:hidden}
.brow .bt i{display:block;height:100%;background:var(--accent);border-radius:4px}.brow .bv{text-align:right;font-variant-numeric:tabular-nums}
.cells{display:grid;grid-template-columns:repeat(7,1fr);gap:.4rem;align-items:end;height:8rem}.cells div{display:flex;flex-direction:column;justify-content:flex-end;align-items:center;font-size:.72rem;color:var(--muted);height:100%}.cells i{display:block;width:70%;background:var(--accent);border-radius:4px 4px 0 0;min-height:2px}
.spark{width:90px;height:22px;display:block}.spark polyline{fill:none;stroke:var(--accent);stroke-width:1.5}
.kebab{position:relative;width:2rem;text-align:center}.kebab>button{padding:.1rem .45rem;line-height:1;font-size:1.1rem;background:none;border-color:transparent}.kebab>button:hover{border-color:var(--line)}
.menu{position:absolute;right:0;top:100%;z-index:20;min-width:17rem;background:var(--card);border:1px solid var(--line);border-radius:8px;box-shadow:0 4px 14px #0003;padding:.25rem;text-align:left}
.menu button{display:block;width:100%;text-align:left;background:none;border:0}.menu button:hover{background:color-mix(in srgb,var(--accent) 10%,transparent)}.menu button.danger{color:#d6453d}
</style></head><body><div class="wrap">
<!--nav-back-->
<h1>Analyses GitHub <span id="vb" class="vb"></span><!--theme-toggle--></h1>
<p class="muted">Trafic des dépôts publié par SiteWatch. Les visiteurs uniques d'un jour ne s'additionnent pas ; les pages et origines sont un classement glissant de 14 jours. Une corrélation avec une publication n'est jamais une causalité. <a href="/github/settings">Configurer les alertes</a></p>
<div class="bar">
  <label>Dépôt<select id="repo"></select></label>
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
const fmt=n=>new Intl.NumberFormat("fr-FR",{maximumFractionDigits:1}).format(n||0);
const iso=d=>d.getFullYear()+"-"+String(d.getMonth()+1).padStart(2,"0")+"-"+String(d.getDate()).padStart(2,"0");
const addDays=(s,n)=>{const d=new Date(s+"T00:00:00");d.setDate(d.getDate()+n);return iso(d)};
const diffDays=(a,b)=>Math.round((new Date(b+"T00:00:00")-new Date(a+"T00:00:00"))/864e5);
const SER=[["views","Vues","#2e86de"],["clones","Clones","#8e6bbf"]];
const PRESETS=[[7,"7 j"],[14,"14 j"],[30,"30 j"],[90,"90 j"],[0,"Tout"]];
const PLAT={windows:"Windows",linux:"Linux",firmware:"Firmware",other:"Autres"};
const DELIV={sent:"envoyée",failed:"envoi échoué",pending:"en attente d'envoi",skipped:"enregistrée sans envoi",muted:"en sourdine"};
let visible=new Set(["views","clones"]),repos=[];
function qs(){return new URLSearchParams(location.search)}
function setQs(o){const p=qs();for(const k in o){o[k]?p.set(k,o[k]):p.delete(k)}history.replaceState(null,"","?"+p.toString())}
async function jget(u){const r=await fetch(u);if(!r.ok)throw new Error(u+" : HTTP "+r.status);return r.json()}
async function jpost(u,o){const r=await fetch(u,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(o)});const j=await r.json().catch(()=>({}));if(!r.ok)throw new Error(j.error||(u+" : HTTP "+r.status));return j}

// Periode choisie : dates libres, sinon N derniers jours (30 par defaut), 0 = tout l'historique.
function period(){
  const p=qs(),f=p.get("from"),t=p.get("to");
  if(f||t)return{from:f||"1970-01-01",to:t||"2999-12-31",shown:[f||"",t||""],prev:f&&t?{from:addDays(f,-(diffDays(f,t)+1)),to:addDays(f,-1)}:null};
  const days=p.has("days")?parseInt(p.get("days")):30,today=iso(new Date());
  if(!days)return{from:"1970-01-01",to:"2999-12-31",shown:["",""],prev:null,all:true};
  const from=addDays(today,-(days-1));
  return{from,to:today,days,shown:[from,today],prev:{from:addDays(from,-days),to:addDays(from,-1)}};
}
const dataUrl=(repo,from,to)=>"/github/data?from="+from+"&to="+to+(repo?"&repo="+encodeURIComponent(repo):"");

function delta(cur,prev,neutral){
  if(prev===undefined||prev===null||!(prev>0))return"";
  const d=(cur-prev)*100/prev,up=d>0.5,down=d<-0.5;
  const cls=(!up&&!down)||neutral?"flat":(up?"good":"bad");
  return`<span class="${cls}">${up?"▲":down?"▼":"•"} ${d>0?"+":""}${fmt(d)} %</span> <span class="muted">vs période précédente</span>`;
}
function tile(k,n,sub){return`<div class="tile"><div class="k">${k}</div><div class="n">${n}</div><div class="s muted">${sub||"&nbsp;"}</div></div>`}
function dense(daily){
  if(!daily.length)return[];const out=[];
  for(let d=daily[0].day,n=0;d<=daily[daily.length-1].day&&n<1500;d=addDays(d,1),n++)out.push(d);
  return out;
}
function series(dates,daily){
  const m={};daily.forEach(x=>m[x.day]=x);
  return{views:dates.map(d=>m[d]?m[d].views:0),clones:dates.map(d=>m[d]?m[d].clones:0)};
}
function barRows(list,unit){
  if(!list||!list.length)return'<p class="muted">Aucune donnée.</p>';
  const max=Math.max(1,...list.map(x=>x.count));
  return list.map(x=>`<div class="brow"><span class="bl" title="${esc(x.name)}">${esc(x.name)}</span><span class="bt"><i style="width:${x.count*100/max}%"></i></span><span class="bv">${fmt(x.count)}${unit||""}</span></div>`).join("");
}
function weekProfile(dates,vals){
  const sum=Array(7).fill(0),cnt=Array(7).fill(0);
  dates.forEach((d,i)=>{const w=(new Date(d+"T00:00:00").getDay()+6)%7;sum[w]+=vals[i];cnt[w]++});
  const avg=sum.map((s,i)=>cnt[i]?s/cnt[i]:0),mx=Math.max(1,...avg),names=["Lun","Mar","Mer","Jeu","Ven","Sam","Dim"];
  return`<div class="cells">`+avg.map((a,i)=>`<div title="${fmt(a)} vues en moyenne"><i style="height:${a/mx*100}%"></i>${names[i]}<br>${fmt(Math.round(a))}</div>`).join("")+`</div>`;
}
function spark(arr,dates){
  const m={};(arr||[]).forEach(([d,v])=>m[d]=v);const v=dates.map(d=>m[d]||0),mx=Math.max(1,...v),w=90,h=22;
  return`<svg class="spark" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none"><polyline points="${v.map((x,i)=>(i*w/Math.max(1,v.length-1)).toFixed(1)+","+(h-2-(h-4)*x/mx).toFixed(1)).join(" ")}"/></svg>`;
}
function alertsHtml(list){
  if(!list.length)return'<p class="muted">Aucune alerte enregistrée.</p>';
  return list.map(a=>`<div class="alert ${esc(a.level)}"><b>${esc(a.title)}<span class="pill">${esc(a.rule)}</span></b>${esc(a.message)}<small>${new Date(a.created_at*1000).toLocaleString("fr-FR")} · ${esc(DELIV[a.delivery]||a.delivery)}${a.last_error?" · "+esc(a.last_error):""}</small></div>`).join("");
}

// ---- Graphique quotidien -----------------------------------------------------
let chartState=null;
function chart(dates,s){
  chartState={dates,s};
  const W=1000,H=300,L=48,R=10,T=10,B=26,act=SER.filter(x=>visible.has(x[0]));
  let max=1;for(const x of act)for(const v of s[x[0]])if(v>max)max=v;
  const X=i=>L+(dates.length<2?(W-L-R)/2:i*(W-L-R)/(dates.length-1)),Y=v=>T+(H-T-B)*(1-v/max);
  let g="";
  for(let t=0;t<=4;t++){const v=max*t/4,y=Y(v);g+=`<line x1="${L}" x2="${W-R}" y1="${y}" y2="${y}" stroke="var(--line)"/><text x="${L-6}" y="${y+4}" text-anchor="end" font-size="11" fill="var(--muted)">${fmt(Math.round(v))}</text>`}
  const step=Math.max(1,Math.ceil(dates.length/8));
  dates.forEach((dt,i)=>{if(i%step===0)g+=`<text x="${X(i)}" y="${H-8}" text-anchor="${X(i)>W-40?"end":"middle"}" font-size="11" fill="var(--muted)">${dt.slice(5)}</text>`});
  for(const x of act){g+=`<polyline fill="none" stroke="${x[2]}" stroke-width="2" points="${s[x[0]].map((v,i)=>X(i)+","+Y(v)).join(" ")}"/>`;
    if(dates.length<=45)s[x[0]].forEach((v,i)=>{g+=`<circle cx="${X(i)}" cy="${Y(v)}" r="2.5" fill="${x[2]}"/>`})}
  g+=`<line id="cur" y1="${T}" y2="${H-B}" stroke="var(--muted)" stroke-dasharray="3" style="display:none"/>`;
  return`<svg id="chart" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">${g}<rect id="hit" x="${L}" y="${T}" width="${W-L-R}" height="${H-T-B}" fill="transparent"/></svg>`;
}
function bindChart(){
  const svg=$("chart"),hit=$("hit"),tip=$("tip"),cur=$("cur");if(!svg||!chartState)return;
  const {dates,s}=chartState,n=dates.length,L=48,R=10,W=1000;
  hit.onmousemove=e=>{
    const r=svg.getBoundingClientRect(),x=(e.clientX-r.left)/r.width*W;
    const i=Math.max(0,Math.min(n-1,Math.round((x-L)/(W-L-R)*(n-1))));
    const cx=L+(n<2?(W-L-R)/2:i*(W-L-R)/(n-1));cur.setAttribute("x1",cx);cur.setAttribute("x2",cx);cur.style.display="";
    tip.innerHTML="<b>"+esc(dates[i])+"</b><br>"+SER.filter(x=>visible.has(x[0])).map(x=>`<span class="sw" style="background:${x[2]}"></span> ${x[1]} : ${fmt(s[x[0]][i])}`).join("<br>");
    tip.style.display="block";tip.style.left=Math.min(innerWidth-200,e.clientX+14)+"px";tip.style.top=(e.clientY+14)+"px";
  };
  hit.onmouseleave=()=>{tip.style.display="none";cur.style.display="none"};
}
function chartCard(dates,s){
  return`<div class="card"><div class="legend">`+SER.map(x=>`<label><input type="checkbox" data-s="${x[0]}" ${visible.has(x[0])?"checked":""}><span class="sw" style="background:${x[2]}"></span>${x[1]}</label>`).join("")+`</div>`+(dates.length?chart(dates,s):'<p class="muted">Aucune donnée sur la période.</p>')+`</div>`;
}
function sortTable(table,col,numeric){
  const rows=[...table.tBodies[0].rows],dir=table.dataset.sortCol===String(col)&&table.dataset.sortDir==="asc"?"desc":"asc";
  rows.sort((a,b)=>{const av=a.cells[col].dataset.sort??a.cells[col].textContent,bv=b.cells[col].dataset.sort??b.cells[col].textContent;
    const c=numeric?(Number(av)-Number(bv)):String(av).localeCompare(String(bv),"fr");return dir==="asc"?c:-c});
  rows.forEach(r=>table.tBodies[0].appendChild(r));table.dataset.sortCol=String(col);table.dataset.sortDir=dir;
}
function bindSort(table,numCols){if(table)[...table.tHead.rows[0].cells].forEach((th,i)=>{if(th.classList.contains("s"))th.onclick=()=>sortTable(table,i,numCols.includes(i))})}
// Graphique + cases a cocher : cocher une serie redessine le bloc puis rebranche tout.
function wireChart(dates,s){
  const wire=()=>{bindChartToggles(()=>{$("chartbox").innerHTML=chartCard(dates,s);wire()});bindChart()};wire();
}
function bindChartToggles(rerender){
  document.querySelectorAll("input[data-s]").forEach(i=>i.onchange=()=>{i.checked?visible.add(i.dataset.s):visible.delete(i.dataset.s);rerender()});
}

// ---- Vue globale -------------------------------------------------------------
function globalView(ov,prev,alerts,per){
  const dates=dense(ov.daily),s=series(dates,ov.daily),rs=ov.repos||[];
  const stars=rs.reduce((a,r)=>a+(r.stars||0),0),active=rs.filter(r=>r.views>0||r.clones>0).length;
  let h=`<h2>Indicateurs · ${esc(per.shown[0]||"début")} → ${esc(per.shown[1]||"aujourd'hui")}</h2><div class="grid">`+
    tile("Vues",fmt(ov.views_total),delta(ov.views_total,prev&&prev.views_total))+
    tile("Clones",fmt(ov.clones_total),delta(ov.clones_total,prev&&prev.clones_total,true))+
    tile("Téléchargements",fmt(ov.downloads_delta),"depuis le suivi (écarts entre relevés)")+
    tile("Étoiles",fmt(stars),rs.length+" dépôt(s)")+
    tile("Dépôts actifs",active+" / "+rs.length,"au moins une vue ou un clone")+
    tile("Moyenne de vues / jour",fmt(ov.avg_views_per_day),ov.days_known+" jour(s) connus")+
    tile("Jour le plus actif",esc(ov.busiest_day||"-"),fmt(ov.busiest_views||0)+" vues")+`</div>`;
  h+=`<h2>Alertes récentes</h2>`+alertsHtml(alerts);
  h+=`<h2>Évolution quotidienne</h2><div id="chartbox">${chartCard(dates,s)}</div>`;
  h+=`<h2>Dépôts</h2><div class="card"><table id="t-repos"><thead><tr><th class="s">Dépôt</th><th class="s num">Vues</th><th>Tendance</th><th class="s num">Clones</th><th class="s num">Téléchargements</th><th class="s num">Étoiles</th><th class="s">Release</th><th class="s">Publication</th><th></th></tr></thead><tbody>`;
  const now=Date.now()/1000;
  for(const r of rs){
    const lag=r.last_imported_at?Math.floor((now-r.last_imported_at)/86400):null;
    const pub=lag===null?'<span class="muted">-</span>':lag>=3?`<span class="pill ${lag>=7?"err":"warn"}">${lag} j de retard</span>`:`<span class="muted">${lag<1?"aujourd'hui":"il y a "+lag+" j"}</span>`;
    h+=`<tr class="row" data-repo="${esc(r.full_name)}"><td><b>${esc(r.full_name)}</b></td><td class="num" data-sort="${r.views||0}">${fmt(r.views)}</td><td>${spark((ov.repo_daily||{})[r.full_name],dates)}</td><td class="num" data-sort="${r.clones||0}">${fmt(r.clones)}</td><td class="num" data-sort="${r.downloads||0}">${fmt(r.downloads)}</td><td class="num" data-sort="${r.stars||0}">${fmt(r.stars)}</td><td>${esc(r.last_release||"-")}</td><td data-sort="${lag===null?9999:lag}">${pub}</td><td class="kebab"><button class="small" data-menu title="Actions" aria-label="Actions pour ${esc(r.full_name)}">⋮</button></td></tr>`;
  }
  h+=`</tbody></table></div><p class="muted">Cliquer sur un dépôt pour ouvrir son analyse détaillée.</p>`;
  const plat=(ov.platforms||[]).filter(p=>p.downloads>0).map(p=>({name:PLAT[p.platform]||p.platform,count:p.downloads}));
  h+=`<div class="two"><div><h2>Profil de la semaine (vues par jour)</h2><div class="card">${weekProfile(dates,s.views)}</div></div>
    <div><h2>Téléchargements par plateforme</h2><div class="card">${barRows(plat)}</div></div></div>
    <div class="two"><div><h2>Pages populaires · 14 jours</h2><div class="card">${barRows(ov.top_paths)}</div></div>
    <div><h2>Origine du trafic · 14 jours</h2><div class="card">${barRows(ov.top_referrers)}</div></div></div>`;
  return{html:h,after(){
    wireChart(dates,s);bindSort($("t-repos"),[1,3,4,5]);
    document.querySelectorAll("tr.row").forEach(tr=>tr.onclick=()=>{$("repo").value=tr.dataset.repo;setQs({repo:tr.dataset.repo});load()});
    bindMenus();
  }};
}

// Menu d'actions de la ligne : un seul ouvert a la fois, sans declencher l'ouverture du depot.
function closeMenus(){document.querySelectorAll(".menu").forEach(m=>m.remove())}
function bindMenus(){
  document.querySelectorAll("button[data-menu]").forEach(b=>b.onclick=e=>{
    e.stopPropagation();const open=b.parentElement.querySelector(".menu");closeMenus();if(open)return;
    const repo=b.closest("tr").dataset.repo,m=document.createElement("div");
    m.className="menu";m.innerHTML=`<button data-act="refresh">Mettre à jour</button><button data-act="mute">Sourdine des alertes 7 jours</button><button data-act="delete" class="danger">Supprimer le dépôt et ses données</button>`;
    m.onclick=async ev=>{ev.stopPropagation();const a=ev.target.dataset.act;if(!a)return;closeMenus();
      if(a==="refresh")return load();
      if(a==="mute"){try{await jpost("/github/config/mute",{repo,rule:"*",days:7});load()}catch(err){alert("Sourdine impossible : "+err.message)}return}
      if(!confirm(`Supprimer « ${repo} » et TOUTES ses données (trafic, releases, téléchargements, alertes) ?\nLes autres dépôts ne sont pas touchés. Action irréversible.`))return;
      try{await jpost("/github/delete",{repo});await loadRepos();await load()}catch(err){alert("Suppression impossible : "+err.message)}};
    b.parentElement.appendChild(m)});
  document.onclick=closeMenus;
}

// ---- Vue d'un depot ----------------------------------------------------------
function repoView(repo,ov,prev,d,alerts,per){
  const dates=dense(ov.daily),s=series(dates,ov.daily),w=d.window||{};
  const sumU=k=>ov.daily.reduce((a,x)=>a+(x[k]||0),0);
  let h=`<p><a href="#" id="back">← Tous les dépôts</a> · <a href="https://github.com/${esc(repo)}" target="_blank" rel="noopener">Ouvrir sur GitHub</a></p><h2 style="margin-top:.6rem">${esc(d.full_name||repo)}</h2>`;
  h+=`<div class="grid">`+
    tile("Vues",fmt(ov.views_total),delta(ov.views_total,prev&&prev.views_total))+
    tile("Clones",fmt(ov.clones_total),delta(ov.clones_total,prev&&prev.clones_total,true))+
    tile("Téléchargements",fmt(ov.downloads_delta),"écarts entre relevés")+
    tile("Étoiles",fmt(d.stars),fmt(d.forks)+" fork(s)")+
    tile("Dernière release",esc(d.last_release||"-"),"")+
    tile("Moyenne de vues / jour",fmt(ov.avg_views_per_day),ov.days_known+" jour(s) connus")+
    tile("Jour le plus actif",esc(ov.busiest_day||"-"),fmt(ov.busiest_views||0)+" vues")+
    tile("Visiteurs uniques (14 j)",fmt(w.views_uniques),"annoncés par GitHub sur "+(w.window_days||14)+" j")+`</div>
    <p class="muted" style="font-size:.85rem">Somme des uniques quotidiens sur la période : ${fmt(sumU("views_uniques"))} (ce n'est pas un nombre de personnes : un même visiteur compte chaque jour).</p>`;
  h+=`<h2>Alertes</h2>`+alertsHtml(alerts);
  h+=`<h2>Évolution quotidienne</h2><div id="chartbox">${chartCard(dates,s)}</div>`;
  const assets=(d.assets||[]).slice().sort((a,b)=>(b.downloads_delta||0)-(a.downloads_delta||0));
  h+=`<div class="two"><div><h2>Profil de la semaine (vues par jour)</h2><div class="card">${weekProfile(dates,s.views)}</div></div>
    <div><h2>Téléchargements par asset</h2><div class="card">${assets.length?`<table id="t-assets"><thead><tr><th class="s">Asset</th><th class="s">Plateforme</th><th class="s num">Téléchargements</th></tr></thead><tbody>`+assets.map(a=>`<tr><td style="word-break:break-all">${esc(a.name)}</td><td>${esc(PLAT[a.platform]||a.platform)}${a.architecture&&a.architecture!=="none"?" · "+esc(a.architecture):""}</td><td class="num" data-sort="${a.downloads_delta||0}">${fmt(a.downloads_delta)}</td></tr>`).join("")+`</tbody></table>`:'<p class="muted">Aucune release suivie.</p>'}</div></div></div>
    <div class="two"><div><h2>Pages populaires · 14 jours</h2><div class="card">${barRows(ov.top_paths)}</div></div>
    <div><h2>Origine du trafic · 14 jours</h2><div class="card">${barRows(ov.top_referrers)}</div></div></div>`;
  return{html:h,after(){
    wireChart(dates,s);bindSort($("t-assets"),[2]);
    $("back").onclick=e=>{e.preventDefault();$("repo").value="";setQs({repo:""});load()};
  }};
}

async function loadRepos(){
  const l=await jget("/github/data");repos=(l.repos||[]).map(r=>r.full_name);
  $("repo").innerHTML='<option value="">Tous les dépôts</option>'+repos.map(r=>`<option value="${esc(r)}">${esc(r)}</option>`).join("");
  const r=qs().get("repo");if(r&&repos.includes(r))$("repo").value=r;
}
async function load(){
  const per=period(),repo=$("repo").value,app=$("app");
  document.querySelectorAll("#presets button").forEach(b=>b.classList.toggle("on",!qs().get("from")&&!qs().get("to")&&(b.dataset.d===(qs().get("days")||"30"))));
  $("from").value=per.shown[0];$("to").value=per.shown[1];
  try{
    const calls=[jget(dataUrl(repo,per.from,per.to)),per.prev?jget(dataUrl(repo,per.prev.from,per.prev.to)):Promise.resolve(null),
      jget("/github/alerts?limit=5"+(repo?"&repo="+encodeURIComponent(repo):""))];
    if(repo)calls.push(jget("/github/data?repo="+encodeURIComponent(repo)));
    const [ov,prev,al,d]=await Promise.all(calls);
    if(ov.error){app.innerHTML=`<div class="err"><strong>${esc(ov.error)}</strong><p class="muted">${esc(ov.last_error||ov.detail||"")} SiteWatch publie ensuite POST /github/ingest.</p></div>`;return}
    if(!(ov.repos||[]).length&&!repo){app.innerHTML='<div class="card">Aucune donnée GitHub reçue pour l\'instant : SiteWatch publie vers POST /github/ingest.</div>';return}
    const v=repo?repoView(repo,ov,prev,d,al.alerts||[],per):globalView(ov,prev,al.alerts||[],per);
    app.innerHTML=v.html;v.after();
  }catch(e){app.innerHTML='<div class="err">'+esc(e.message)+"</div>"}
}
(async function init(){
  fetch("/status").then(r=>r.json()).then(s=>{if(s.version)$("vb").textContent="v"+s.version}).catch(()=>{});
  $("presets").innerHTML=PRESETS.map(x=>`<button data-d="${x[0]||"0"}">${x[1]}</button>`).join(" ");
  document.querySelectorAll("#presets button").forEach(b=>b.onclick=()=>{setQs({days:b.dataset.d,from:"",to:""});load()});
  $("apply").onclick=()=>{setQs({from:$("from").value,to:$("to").value,days:""});load()};
  $("repo").onchange=()=>{setQs({repo:$("repo").value});load()};
  try{await loadRepos();load()}catch(e){$("app").innerHTML='<div class="err">'+esc(e.message)+"</div>"}
})();
</script></body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
