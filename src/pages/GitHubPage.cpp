#include "morfanalytics/pages/GitHubPage.h"

namespace morfanalytics::pages {

QByteArray GitHubPage::render() {
    static const char* kPage = R"PAGE(<!doctype html><html lang="fr"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>morfAnalytics - GitHub</title>
<style>
:root{--bg:#15171b;--card:#1e2126;--line:#2c3037;--ink:#e7e9ec;--muted:#99a1ad;--accent:#6f9bff}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,sans-serif;padding:1.5rem}
.wrap{max-width:78rem;margin:auto}a{color:var(--accent)}.muted{color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(10rem,1fr));gap:.8rem;margin:1rem 0}
.tile{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:.9rem}
.tile .k{font-size:.72rem;letter-spacing:.05em;text-transform:uppercase;color:var(--muted)}
.tile .n{font-size:1.5rem;font-weight:700;font-variant-numeric:tabular-nums}
table{width:100%;border-collapse:collapse;font-size:.9rem}th,td{padding:.35rem .5rem;text-align:left;border-bottom:1px solid var(--line)}
th{cursor:pointer;user-select:none;color:var(--muted)}th:hover{color:var(--ink)}
.filters{display:flex;flex-wrap:wrap;gap:.7rem;align-items:end;margin:1rem 0}
label{display:flex;flex-direction:column;gap:.2rem;font-size:.8rem;color:var(--muted)}
select,input{background:#242830;border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.3rem .5rem}
button{background:#2a3344;border:1px solid var(--line);color:var(--ink);border-radius:8px;padding:.4rem .8rem;cursor:pointer}
.err{background:#3a1f24;border:1px solid #6b3038;padding:.8rem;border-radius:10px}
</style></head><body><div class="wrap">
<p><a href="/">&larr; morfAnalytics</a></p>
<h1>Analyses GitHub</h1>
<p class="muted">Memoire des metriques publiees par SiteWatch. Les visiteurs uniques quotidiens ne s'additionnent pas.
Les pages et referents sont un classement glissant de 14 jours, pas un historique journalier.
Les relations avec une publication restent des correlations, jamais des causalites.</p>
<div id="app"><p class="muted">Chargement…</p></div>
</div>
<script>
"use strict";
function qs(){return new URLSearchParams(location.search)}
function esc(s){return String(s??"").replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;"}[c]))}
function sortTable(table,col,numeric){
  const rows=[...table.tBodies[0].rows];
  const dir=table.dataset.sortCol===String(col)&&table.dataset.sortDir==="asc"?"desc":"asc";
  rows.sort((a,b)=>{
    const av=a.cells[col].dataset.sort??a.cells[col].textContent;
    const bv=b.cells[col].dataset.sort??b.cells[col].textContent;
    let cmp=numeric?(Number(av)-Number(bv)):String(av).localeCompare(String(bv),"fr");
    return dir==="asc"?cmp:-cmp;
  });
  rows.forEach(r=>table.tBodies[0].appendChild(r));
  table.dataset.sortCol=String(col);table.dataset.sortDir=dir;
}
function bindSort(table,numericCols){
  [...table.tHead.rows[0].cells].forEach((th,i)=>{
    th.onclick=()=>sortTable(table,i,numericCols.includes(i));
  });
}
async function load(){
  const p=qs();
  const repo=p.get("repo")||"";
  const from=p.get("from")||"";
  const to=p.get("to")||"";
  const metric=p.get("metric")||"";
  const dataUrl="/github/data"+(from||to?("?from="+encodeURIComponent(from)+"&to="+encodeURIComponent(to)+(repo?"&repo="+encodeURIComponent(repo):"")):"");
  const ov=await (await fetch(dataUrl||"/github/data")).json();
  if(ov.error){
    document.getElementById("app").innerHTML=`<div class="err"><strong>${esc(ov.error)}</strong><p class="muted">${esc(ov.last_error||ov.detail||"")} Redemarrez morfAnalytics apres upgrade : le module github est cree meme si /etc ne le declare pas. SiteWatch publie ensuite POST /github/ingest.</p></div>`;
    return;
  }
  let html="";
  html+='<div class="grid">';
  [["Depots",ov.repositories],["Vues (jours connus)",ov.views_total],["Clones",ov.clones_total],
   ["Telechargements (deltas)",ov.downloads_delta],["Jours connus",ov.days_known],
   ["Moyenne vues/jour",ov.avg_views_per_day]].forEach(([k,v])=>{
    html+=`<div class="tile"><div class="k">${k}</div><div class="n">${v??0}</div></div>`;
  });
  html+=`<div class="tile"><div class="k">Jour le plus actif</div><div class="n">${esc(ov.busiest_day||"—")}</div><div class="muted">${ov.busiest_views??0} vues</div></div>`;
  html+='</div>';
  html+='<form class="filters" method="get" action="/github">';
  html+='<label>Depot <select name="repo"><option value="">(vue globale)</option>';
  (ov.repos||[]).forEach(r=>{
    html+=`<option ${repo===r.full_name?"selected":""} value="${esc(r.full_name)}">${esc(r.full_name)}</option>`;
  });
  html+=`</select></label><label>Du <input type="date" name="from" value="${esc(from)}"></label>`;
  html+=`<label>Au <input type="date" name="to" value="${esc(to)}"></label>`;
  html+='<label>Metrique <select name="metric">';
  [["","Toutes"],["views","Vues"],["clones","Clones"]].forEach(([v,l])=>{
    html+=`<option ${metric===v?"selected":""} value="${v}">${l}</option>`;
  });
  html+='</select></label><button type="submit">Filtrer</button></form>';
  if(repo && !from && !to){
    const d=await (await fetch("/github/data?repo="+encodeURIComponent(repo))).json();
    const w=d.window||{};
    html+=`<h2>${esc(d.full_name||repo)}</h2>`;
    html+=`<p class="muted">Fenetre GitHub : ${w.views_count||0} vues, ${w.views_uniques||0} visiteurs uniques annonces, ${w.clones_count||0} clones. ${esc(d.uniques_note||"")}</p>`;
    html+='<h3>Trafic quotidien</h3><table id="t-daily"><thead><tr><th>Jour</th><th>Metrique</th><th>Count</th><th>Uniques du jour</th></tr></thead><tbody>';
    (d.daily||[]).filter(x=>!metric||x.metric===metric).forEach(x=>{
      html+=`<tr><td>${esc(x.day)}</td><td>${esc(x.metric)}</td><td data-sort="${x.count||0}">${x.count||0}</td><td data-sort="${x.uniques||0}">${x.uniques||0}</td></tr>`;
    });
    html+='</tbody></table><h3>Pages populaires (snapshot 14 j)</h3><table id="t-paths"><thead><tr><th>Chemin</th><th>Vues</th></tr></thead><tbody>';
    (d.popular_paths||[]).forEach(x=>{html+=`<tr><td>${esc(x.path)}</td><td data-sort="${x.count||0}">${x.count||0}</td></tr>`;});
    html+='</tbody></table><h3>Referents (snapshot 14 j)</h3><table id="t-ref"><thead><tr><th>Source</th><th>Vues</th></tr></thead><tbody>';
    (d.referrers||[]).forEach(x=>{html+=`<tr><td>${esc(x.referrer)}</td><td data-sort="${x.count||0}">${x.count||0}</td></tr>`;});
    html+='</tbody></table><h3>Telechargements par asset</h3><table id="t-assets"><thead><tr><th>Asset</th><th>Plateforme</th><th>Arch</th><th>Delta</th></tr></thead><tbody>';
    (d.assets||[]).forEach(x=>{html+=`<tr><td>${esc(x.name)}</td><td>${esc(x.platform)}</td><td>${esc(x.architecture)}</td><td data-sort="${x.downloads_delta||0}">${x.downloads_delta||0}</td></tr>`;});
    html+="</tbody></table>";
  } else {
    html+='<h2>Depots</h2><table id="t-repos"><thead><tr><th>Depot</th><th>Vues</th><th>Clones</th><th>Etoiles</th><th>Release</th></tr></thead><tbody>';
    (ov.repos||[]).forEach(r=>{
      html+=`<tr><td><a href="/github?repo=${encodeURIComponent(r.full_name)}">${esc(r.full_name)}</a></td><td data-sort="${r.views||0}">${r.views||0}</td><td data-sort="${r.clones||0}">${r.clones||0}</td><td data-sort="${r.stars||0}">${r.stars||0}</td><td>${esc(r.last_release||"—")}</td></tr>`;
    });
    html+='</tbody></table><h3>Evolution quotidienne</h3><table id="t-daily"><thead><tr><th>Jour</th><th>Vues</th><th>Clones</th></tr></thead><tbody>';
    (ov.daily||[]).forEach(x=>{
      if(metric==="views") html+=`<tr><td>${esc(x.day)}</td><td data-sort="${x.views||0}">${x.views||0}</td><td data-sort="0">—</td></tr>`;
      else if(metric==="clones") html+=`<tr><td>${esc(x.day)}</td><td data-sort="0">—</td><td data-sort="${x.clones||0}">${x.clones||0}</td></tr>`;
      else html+=`<tr><td>${esc(x.day)}</td><td data-sort="${x.views||0}">${x.views||0}</td><td data-sort="${x.clones||0}">${x.clones||0}</td></tr>`;
    });
    html+='</tbody></table><h3>Plateformes</h3><table id="t-plat"><thead><tr><th>Plateforme</th><th>Telechargements</th></tr></thead><tbody>';
    (ov.platforms||[]).forEach(x=>{html+=`<tr><td>${esc(x.platform)}</td><td data-sort="${x.downloads||0}">${x.downloads||0}</td></tr>`;});
    html+="</tbody></table>";
  }
  document.getElementById("app").innerHTML=html;
  const t=id=>document.getElementById(id);
  if(t("t-repos")) bindSort(t("t-repos"),[1,2,3]);
  if(t("t-daily")) bindSort(t("t-daily"),[2,3,1]);
  if(t("t-paths")) bindSort(t("t-paths"),[1]);
  if(t("t-ref")) bindSort(t("t-ref"),[1]);
  if(t("t-assets")) bindSort(t("t-assets"),[3]);
  if(t("t-plat")) bindSort(t("t-plat"),[1]);
}
load().catch(err=>{document.getElementById("app").textContent=String(err);});
</script></body></html>)PAGE";
    return QByteArray(kPage);
}

} // namespace morfanalytics::pages
