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
<nav><button data-t="m">Makro</button><button data-t="k">Klawiatura</button><button data-t="s">Ustawienia</button></nav>
<div id="msg"></div>

<div id="m" class="sec">
<h3>Makra</h3><ul id="list"></ul>
<h4>Edycja</h4>
<input id="mn" placeholder="nazwa (A-Z a-z 0-9 _ -)">
<textarea id="mb" placeholder="ctrl+c&#10;T&#10;a&#10;k&#10;alt+ctrl+del&#10;pause 2"></textarea>
<button onclick="saveM()">Zapisz</button> <button onclick="clearM()">Nowe</button>
<details><summary>Format</summary><pre>Jedna linia = jeden znak lub kombinacja (łączone znakiem +).
ctrl sh alt win bs sp cr tab esc up down left right home end del ins pgup pgdn
f1..f12 power sleep
pause N  - zwłoka w sekundach
Linia dłuższa bez + i nie będąca klawiszem jest wpisywana jako tekst.
Wywołanie: /run/NAZWA</pre></details>
</div>

<div id="k" class="sec">
<h3>Klawiatura</h3>
<textarea id="kt" placeholder="Tekst do wpisania w komputerze"></textarea>
<button onclick="typeK()">Wyślij</button>
</div>

<div id="s" class="sec">
<h3>Ustawienia</h3>
<label>WiFi SSID</label><input id="ssid">
<label>Hasło WiFi (puste = bez zmian)</label><input id="pass" type="password">
<label>Hasło WebUI (puste = bez zmian)</label><input id="webpw" type="password">
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
   const a=document.createElement('a');a.textContent=l;a.onclick=f;li.append(a)}
  ul.append(li)}
}
async function run(n){const r=await fetch('/run/'+encodeURIComponent(n));say(r.ok?'Wykonano':'Błąd')}
async function edit(n){$('mn').value=n;$('mb').value=await (await fetch('/api/macro?n='+encodeURIComponent(n))).text()}
async function del(n){if(confirm('Usunąć '+n+'?')){await post('/api/macro/del',{n});load()}}
function clearM(){$('mn').value='';$('mb').value=''}
async function saveM(){const r=await post('/api/macro',{n:$('mn').value,b:$('mb').value});say(r.ok?'Zapisano':'Błąd nazwy');load()}
async function typeK(){const r=await post('/api/type',{t:$('kt').value});say(r.ok?'Wysłano':'Błąd')}
async function saveS(){await post('/api/settings',{ssid:$('ssid').value,pass:$('pass').value,webpw:$('webpw').value,nopw:$('nopw').checked?1:0});say('Zapisano, restart...')}
async function impF(){const f=$('imp').files[0];if(!f)return;const r=await post('/api/import',{d:await f.text()});say(r.ok?'Wgrano, restart...':'Błąd')}
(async()=>{tab('m');load();$('ssid').value=await (await fetch('/api/ssid')).text()})();
</script></body></html>)HTML";
