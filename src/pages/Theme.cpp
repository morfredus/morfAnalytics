#include "morfanalytics/pages/Theme.h"

namespace morfanalytics::pages {

// Palette : les mêmes noms dans les deux thèmes, seules les valeurs changent.
// Les pages ne définissent plus aucune couleur de base ; elles n'emploient que
// ces variables (CSS) ou mfaColor("--nom") (JavaScript des graphiques).
//   Base      : --bg --card --line --ink (--fg, alias) --muted --soft --faint
//   Sens      : --accent --accent2 --ok --warn --bad --warm --cold
//   Surfaces  : --track --field --btn --sel --sel-ink --tip --zebra --hover
//               --pick --pick-line --chip* --excl* --err* --b-warn --b-bad --b-ok
//   Séries    : --s-temp --s-hum --s-pres --s-out --s-in --s-regime --s-susp
//               --s-cpu --s-mem --s-load --s-inc --s-a --s-b --glint
// Les couleurs de séries du thème clair sont plus sombres que celles du thème
// sombre : un vert menthe ou un orange pâle deviennent illisibles sur fond blanc.
static const char* kHead = R"THEME(<style>
:root,:root[data-theme="dark"]{color-scheme:dark;
--bg:#15171b;--card:#1e2126;--line:#2c3037;--ink:#e7e9ec;--fg:var(--ink);--muted:#99a1ad;--soft:#c7cdd6;--faint:#5a6472;
--accent:#6f9bff;--accent2:#7ee0b8;--ok:#2e8b57;--warn:#e6a54e;--bad:#c8483a;--warm:#e0662b;--cold:#2f7fd6;
--track:#242830;--field:#242830;--btn:#2a3344;--sel:#2a3350;--sel-ink:#ffffff;--tip:#0e1013;--zebra:#1a1d22;--hover:#232733;
--pick:#1c2c3a;--pick-line:#4a90d9;--chip:#243050;--chip-line:#35507f;--chip-ink:#cdd8f7;--chip-hover:#2c3c66;
--excl:#3a2f1e;--excl-line:#6a5330;--excl-ink:#f0d9b0;--err:#3a1f24;--err-line:#6b3038;
--b-warn:#f0c073;--b-bad:#f0a093;--b-ok:#8fe0b0;
--s-temp:#e6a54e;--s-hum:#7ee0b8;--s-pres:#c58bf2;--s-out:#6f9bff;--s-in:#e6a54e;--s-regime:#9aa7ff;--s-susp:#8a929e;
--s-cpu:#6f9bff;--s-mem:#7ee0b8;--s-load:#a487f2;--s-inc:#e0836f;--s-a:#6f9bff;--s-b:#7ee0b8;--glint:#ffffff}
:root[data-theme="light"]{color-scheme:light;
--bg:#f4f5f7;--card:#ffffff;--line:#dde1e6;--ink:#1b1d21;--muted:#5f6b7a;--soft:#3d4652;--faint:#8a94a3;
--accent:#2f6fed;--accent2:#138a63;--ok:#2e8b57;--warn:#b8691a;--bad:#c0392b;--warm:#d4561f;--cold:#2f7fd6;
--track:#e7eaef;--field:#ffffff;--btn:#eef1f5;--sel:#dbe6ff;--sel-ink:#10244d;--tip:#ffffff;--zebra:#f7f8fa;--hover:#eef2f8;
--pick:#e3eefb;--pick-line:#2f6fed;--chip:#e3ebfd;--chip-line:#a9c0f2;--chip-ink:#1d3a78;--chip-hover:#d3e0fb;
--excl:#fbf0dc;--excl-line:#d9b977;--excl-ink:#6b4a12;--err:#fdecee;--err-line:#e3a2ab;
--b-warn:#8a5a00;--b-bad:#a1302a;--b-ok:#1d6b43;
--s-temp:#c7761a;--s-hum:#16926b;--s-pres:#8a4fd0;--s-out:#2f6fed;--s-in:#c7761a;--s-regime:#5563d6;--s-susp:#6b7280;
--s-cpu:#2f6fed;--s-mem:#16926b;--s-load:#7a5bd6;--s-inc:#c9553a;--s-a:#2f6fed;--s-b:#16926b;--glint:#ffffff}
.theme-toggle{font:inherit;font-size:.8rem;line-height:1.3;vertical-align:middle;margin-left:.4rem;cursor:pointer;
color:var(--ink);background:transparent;border:1px solid var(--line);border-radius:999px;padding:.1rem .5rem}
.theme-toggle:hover{border-color:var(--accent)}
:root[data-theme="light"] .theme-toggle .tt-sun,:root:not([data-theme="light"]) .theme-toggle .tt-moon{display:none}
.backnav{display:flex;flex-wrap:wrap;gap:.4rem 1.2rem;margin:0 0 .6rem;font-size:.9rem}
.backnav a{color:var(--accent);text-decoration:none}.backnav a:hover{text-decoration:underline}
</style>
<script>
(function(){var t=null;try{t=localStorage.getItem("morfanalytics.theme");}catch(e){}
document.documentElement.setAttribute("data-theme",t==="light"?"light":"dark");})();
function mfaColor(name,fallback){var v=getComputedStyle(document.documentElement).getPropertyValue(name).trim();return v||fallback||"";}
function mfaToggleTheme(){var next=document.documentElement.getAttribute("data-theme")==="light"?"dark":"light";var saved=false;
try{localStorage.setItem("morfanalytics.theme",next);saved=localStorage.getItem("morfanalytics.theme")===next;}catch(e){}
if(saved){location.reload();}else{document.documentElement.setAttribute("data-theme",next);}}
function mfaSafeUrl(u){try{var x=new URL(u,location.href);return(x.protocol==="http:"||x.protocol==="https:")?x.href:"";}catch(e){return "";}}
function mfaSetAppBack(url,label){var a=document.getElementById("mfa-app-back");var u=mfaSafeUrl(url);
if(!a||!u)return;a.href=u;a.textContent="← Retour à "+(label||"l'application");a.hidden=false;}
function mfaInitBack(){var p=new URLSearchParams(location.search),b=null;
if(p.get("back")&&mfaSafeUrl(p.get("back"))){b={url:p.get("back"),label:p.get("back_label")||""};try{sessionStorage.setItem("morfanalytics.back",JSON.stringify(b));}catch(e){}}
else{try{b=JSON.parse(sessionStorage.getItem("morfanalytics.back")||"null");}catch(e){}}
if(b&&b.url)mfaSetAppBack(b.url,b.label);}
function mfaMeteoHubBack(){fetch("/modules").then(function(r){return r.json();}).then(function(m){
var c=(m.modules||[]).map(function(x){return x.status&&x.status.collector;}).filter(function(x){return x&&x.source;})[0];
if(c)mfaSetAppBack(c.source,"MeteoHub");}).catch(function(){});}
document.addEventListener("DOMContentLoaded",mfaInitBack);
</script>
)THEME";

// Le soleil propose le thème clair (affiché en sombre), la lune le thème sombre.
static const char* kToggle =
    R"THEME(<button type="button" class="theme-toggle" onclick="mfaToggleTheme()" title="Basculer thème clair / sombre" aria-label="Basculer thème clair / sombre"><span class="tt-sun">&#9728;&#xFE0E;</span><span class="tt-moon">&#9790;</span></button>)THEME";

// Bandeau de retour, identique sur toutes les pages (sauf l'accueil) : toujours
// vers morfAnalytics, et vers l'application d'origine quand elle est connue.
// Une application Web qui ouvre une page peut ajouter ?back=<url>&back_label=<nom>
// au lien ; le retour est gardé pour la visite (sessionStorage), onglets compris.
// Les pages Météo le fixent d'office sur la station collectée (mfaMeteoHubBack).
// Une application de bureau (PhotoHub, SiteWatch) n'a pas de retour possible par
// un lien : fermer l'onglet suffit, l'application est restée ouverte.
static const char* kNav =
    R"THEME(<nav class="backnav"><a href="/">&larr; morfAnalytics</a><a id="mfa-app-back" href="#" hidden></a></nav>)THEME";

QByteArray Theme::headBlock() { return QByteArray(kHead); }

QByteArray Theme::navBlock() { return QByteArray(kNav); }

QByteArray Theme::toggleButton() { return QByteArray(kToggle); }

QByteArray Theme::apply(QByteArray page) {
    page.replace("<!--theme-head-->", headBlock());
    page.replace("<!--theme-toggle-->", toggleButton());
    page.replace("<!--nav-back-->", navBlock());
    return page;
}

}  // namespace morfanalytics::pages
