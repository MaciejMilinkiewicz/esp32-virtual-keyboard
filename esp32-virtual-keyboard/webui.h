#pragma once
#include <Arduino.h>

const char PAGE_SETUP[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Konfiguracja</title>
<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}input,button{width:100%;padding:.6em;margin:.3em 0;box-sizing:border-box}</style></head><body>
<h2>Konfiguracja WiFi</h2>
<form method="POST" action="/setup">
<label>SSID</label><input name="ssid" required>
<label>Hasło WiFi</label><input name="pass" type="password">
<label>Hasło do WebUI (puste = brak)</label><input name="webpw" type="password">
<button>Zapisz i uruchom ponownie</button></form></body></html>)HTML";

const char PAGE_MAIN[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Wirtualna klawiatura</title>
<style>
body{font-family:sans-serif;max-width:720px;margin:1em auto;padding:0 1em}
nav button{padding:.6em 1em;margin-right:.2em}nav button.on{background:#06c;color:#fff}
textarea,input{width:100%;box-sizing:border-box;padding:.5em;margin:.3em 0;font-family:monospace}
textarea{height:14em}.sec{display:none}.sec.on{display:block}
li{margin:.3em 0}a{cursor:pointer;color:#06c;margin-left:.6em}#msg{color:#080;min-height:1.2em}
</style></head><body>
<nav><button data-t="m">Makro</button><button data-t="k">Klawiatura</button><button data-t="h">Harmonogram</button><button data-t="i">Wejścia</button><button data-t="s">Ustawienia</button></nav>
<div id="msg"></div>

<div id="m" class="sec">
<h3>Makra</h3><ul id="list"></ul>
<h4>Edycja</h4>
<input id="mn" placeholder="nazwa (A-Z a-z 0-9 _ -)">
<textarea id="mb" placeholder="ctrl+c&#10;T&#10;a&#10;k&#10;alt+ctrl+del&#10;pause 2"></textarea>
<button onclick="saveM()">Zapisz</button> <button onclick="clearM()">Nowe</button>
<details><summary>Format</summary><pre>Jedna linia = jeden znak lub kombinacja (łączone znakiem +).
ctrl sh alt win bs sp cr tab esc up down left right home end del ins pgup pgdn
f1..f24 power sleep
pin39up / pin39down - stan wyjścia GPIO 39 37 35 33 18 16
pause N  - zwłoka w sekundach
Linia dłuższa bez + i nie będąca klawiszem jest wpisywana jako tekst.
Wywołanie: /run/NAZWA</pre></details>
</div>

<div id="k" class="sec">
<h3>Klawiatura</h3>
<textarea id="kt" placeholder="Tekst do wpisania w komputerze"></textarea>
<button onclick="typeK()">Wyślij</button>
</div>

<div id="h" class="sec">
<h3>Harmonogram (cron: min godz dzień mies dzień_tyg)</h3><ul id="hl"></ul>
<input id="hc" placeholder="np. */5 8-16 * * 1-5">
<select id="hm"></select> <button onclick="addH()">Dodaj</button>
</div>

<div id="i" class="sec">
<h3>Wejścia</h3><pre id="ist"></pre><ul id="il"></ul>
<select id="is"></select>
<select id="ie"><option value="fall">zbocze opadające (1→0)</option><option value="rise">zbocze narastające (0→1)</option><option value="change">każda zmiana</option></select>
<select id="im"></select> <button onclick="addI()">Dodaj</button>
</div>

<div id="s" class="sec">
<h3>Ustawienia</h3>
<label>WiFi SSID</label><input id="ssid">
<label>Hasło WiFi (puste = bez zmian)</label><input id="pass" type="password">
<label>Hasło WebUI (puste = bez zmian)</label><input id="webpw" type="password">
<label>Serwer NTP</label><input id="ntp">
<label>Strefa czasowa (POSIX TZ)</label><input id="tz">
<label><input type="checkbox" id="nopw" style="width:auto"> Usuń hasło WebUI</label><br>
<button onclick="saveS()">Zapisz ustawienia i uruchom ponownie</button>
<h4>Konfiguracja w pliku</h4>
<a href="/api/export" download="config.txt">Pobierz konfigurację</a><br><br>
<input type="file" id="imp"><button onclick="impF()">Wgraj konfigurację</button>
</div>

<script>
const $=i=>document.getElementById(i);
function say(t){$('msg').textContent=t;setTimeout(()=>$('msg').textContent='',4000)}
function tab(t){for(const s of document.querySelectorAll('.sec'))s.classList.toggle('on',s.id==t);
 for(const b of document.querySelectorAll('nav button'))b.classList.toggle('on',b.dataset.t==t)}
for(const b of document.querySelectorAll('nav button'))b.onclick=()=>tab(b.dataset.t);
async function post(u,o){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(o)});return r}
async function load(){
 const t=await (await fetch('/api/macros')).text();const ul=$('list');ul.innerHTML='';
 for(const n of t.split('\n').filter(x=>x)){
  const li=document.createElement('li');const s=document.createElement('span');s.textContent=n;li.append(s);
  for(const [l,f] of [['uruchom',()=>run(n)],['edytuj',()=>edit(n)],['usuń',()=>del(n)]]){
   const a=document.createElement('button');a.type='button';a.textContent=l;a.onclick=f;li.append(a)}
  ul.append(li)}
}
async function run(n){const r=await fetch('/run/'+encodeURIComponent(n));say(r.ok?'Wykonano':'Błąd')}
async function edit(n){$('mn').value=n;$('mb').value=await (await fetch('/api/macro?n='+encodeURIComponent(n))).text()}
async function del(n){if(confirm('Usunąć '+n+'?')){await post('/api/macro/del',{n});load()}}
function clearM(){$('mn').value='';$('mb').value=''}
async function saveM(){const r=await post('/api/macro',{n:$('mn').value,b:$('mb').value});say(r.ok?'Zapisano':'Błąd nazwy');load()}
async function typeK(){const r=await post('/api/type',{t:$('kt').value});say(r.ok?'Wysłano':'Błąd')}
async function saveS(){await post('/api/settings',{ntp:$('ntp').value,tz:$('tz').value,ssid:$('ssid').value,pass:$('pass').value,webpw:$('webpw').value,nopw:$('nopw').checked?1:0});say('Zapisano, restart...')}
async function impF(){const f=$('imp').files[0];if(!f)return;const r=await post('/api/import',{d:await f.text()});say(r.ok?'Wgrano, restart...':'Błąd')}
function rows(t){return t.split('\n').filter(x=>x)}
function fillSel(id,vals){$(id).innerHTML='';for(const v of vals){const o=document.createElement('option');o.textContent=v;o.value=v;$(id).append(o)}}
function listRows(ul,lines,delFn){ul.innerHTML='';lines.forEach((l,i)=>{const li=document.createElement('li');const s=document.createElement('span');s.textContent=l.split('|').join('  ');li.append(s);const a=document.createElement('a');a.textContent='usuń';a.onclick=()=>delFn(i);li.append(a);ul.append(li)})}
async function loadH(){fillSel('hm',rows(await (await fetch('/api/macros')).text()));listRows($('hl'),rows(await (await fetch('/api/sched')).text()),async i=>{await post('/api/sched/del',{i});loadH()})}
async function addH(){const r=await post('/api/sched/add',{c:$('hc').value,m:$('hm').value});say(r.ok?'Dodano':'Błędny cron lub makro');loadH()}
async function loadI(){fillSel('is',['3','5','7','9','11','12','scroll']);fillSel('im',rows(await (await fetch('/api/macros')).text()));
 $('ist').textContent=rows(await (await fetch('/api/inputs/state')).text()).join('   ');
 listRows($('il'),rows(await (await fetch('/api/inputs')).text()),async i=>{await post('/api/inputs/del',{i});loadI()})}
async function addI(){const r=await post('/api/inputs/add',{s:$('is').value,e:$('ie').value,m:$('im').value});say(r.ok?'Dodano':'Błąd');loadI()}
const _tab=tab;tab=function(t){_tab(t);if(t=='h')loadH();if(t=='i')loadI()};
(async()=>{tab('m');load();const nt=(await (await fetch('/api/ntp')).text()).split('\n');$('ntp').value=nt[0];$('tz').value=nt[1]||'';$('ssid').value=await (await fetch('/api/ssid')).text()})();
</script></body></html>)HTML";
