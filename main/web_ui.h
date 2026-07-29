#pragma once

static const char WEB_PAGE[] = R"HTML(<!doctype html><html lang="es"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP Power Guardian</title><style>
:root{color-scheme:dark;--bg:#07111d;--panel:#121e2c;--card:#091523;--line:#2c4056;--text:#f4f8fc;--muted:#a9bfd4;--accent:#51e39b;--accent2:#7dd8ff;--panel-radius:6px;--card-radius:6px;--control-radius:7px;--panel-gap:12px;--panel-padding:18px;--content-gap:10px;--card-padding:12px;--scale:1;--font:Nunito,system-ui,sans-serif}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;background:var(--bg);color:var(--text);font-family:var(--font);font-size:calc(16px * var(--scale))}
body{overflow-y:auto}main{max-width:1200px;margin:auto;padding:14px}.header{display:flex;align-items:center;justify-content:space-between;gap:12px}
h1{font-size:1.8rem;margin:0 0 12px}h2{font-size:1.35rem;margin:0 0 18px}h3{margin:0;font-size:1rem}.tabs{display:flex;gap:8px;margin-bottom:var(--panel-gap)}
button,select,input{font:inherit}.tab,.button,.choice{border:0;border-radius:var(--control-radius);background:#283c53;color:var(--text);padding:12px 18px;font-weight:800;cursor:pointer}
.tab.active,.choice.active{background:#fff;color:#07111d;outline:2px solid #777;outline-offset:1px}.page{display:none}.page.active{display:block}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:var(--panel-radius);padding:var(--panel-padding);margin-bottom:var(--panel-gap)}
.cards{display:grid;grid-template-columns:repeat(3,minmax(180px,1fr));gap:var(--content-gap)}.cards.four{grid-template-columns:repeat(4,minmax(150px,1fr))}
.card{background:var(--card);border-radius:var(--card-radius);padding:var(--card-padding);min-height:62px}.label{font-weight:800}.value{margin-top:4px;color:var(--accent2);overflow-wrap:anywhere}
.flow{display:grid;grid-template-columns:1fr 90px 1.35fr 90px 1fr;align-items:center;min-height:300px}.node{border:2px solid var(--accent);border-radius:var(--card-radius);padding:18px;text-align:center;min-height:125px;display:grid;place-content:center}
.node.ups{min-height:270px}.duo{font-size:2.6rem;filter:drop-shadow(7px 7px 0 color-mix(in srgb,var(--accent2) 40%,transparent))}.wire{height:6px;background:var(--line);position:relative}.wire:after{content:"";position:absolute;height:100%;width:32%;background:#fff;animation:energy 1.5s linear infinite}@keyframes energy{from{left:-32%}to{left:100%}}
.meters{display:grid;gap:18px;margin-top:14px;text-align:left}.meter{display:grid;grid-template-columns:76px 1fr;align-items:center;gap:12px}.ring{width:70px;height:70px;border-radius:50%;display:grid;place-items:center;background:conic-gradient(var(--accent) var(--pct),var(--line) 0);position:relative;font-weight:900}.ring:before{content:"";position:absolute;inset:9px;border-radius:50%;background:var(--panel)}.ring span{position:relative}
.summary-row{display:grid;grid-template-columns:1fr 1fr;gap:var(--content-gap)}.history{margin:0;padding-left:20px}.history li{margin:8px 0}
.controls{display:grid;grid-template-columns:repeat(2,minmax(250px,1fr));gap:var(--content-gap)}.appearance-item{padding:var(--panel-padding);display:flex;flex-direction:column;gap:10px}.appearance-title{padding-left:10px;border-left:3px solid var(--text);min-height:35px;display:flex;align-items:center}
.segmented{display:grid;grid-template-columns:1fr 1fr;gap:8px}.swatches{display:flex;gap:9px;flex-wrap:wrap}.swatch{width:58px;height:48px;border:0;border-radius:var(--control-radius);cursor:pointer}.swatch.active{outline:3px solid #fff;outline-offset:2px}
select,input[type=text],input[type=password],input[type=file]{width:100%;padding:11px;border:1px solid var(--line);border-radius:var(--control-radius);background:var(--card);color:var(--text)}
.range-row{display:grid;grid-template-columns:1fr 54px;align-items:center;gap:10px}.range-row input{width:100%}.color-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}.color-row{background:var(--card);padding:12px;border-radius:var(--card-radius);display:flex;align-items:center;justify-content:space-between}.color-row input{width:62px;height:38px}
.wifi-row{display:grid;grid-template-columns:1fr auto;gap:8px}.notice{color:var(--muted)}footer{display:flex;justify-content:space-between;color:var(--accent2);padding:3px 0}footer a{color:inherit;text-decoration:none}
[data-tip]{cursor:help}@media(max-width:850px){.cards,.cards.four{grid-template-columns:repeat(2,1fr)}.flow{grid-template-columns:1fr}.wire{width:6px;height:28px;margin:auto}.controls{grid-template-columns:1fr}}
@media(max-width:520px){.tabs{overflow:auto}.cards,.cards.four,.summary-row,.color-grid{grid-template-columns:1fr}main{padding:9px}}
body.light{color-scheme:light;--bg:#f4f7fb;--panel:#fff;--card:#e9f0f7;--line:#708399;--text:#101820;--muted:#42566a}
body[data-theme=forest]{--accent:#55d58a;--accent2:#8bdab2}body[data-theme=violet]{--accent:#9a7cff;--accent2:#c5b5ff}body[data-theme=mono]{--accent:#fff;--accent2:#fff}
body.light[data-theme=mono]{--accent:#000;--accent2:#000}body[data-theme=gray]{--accent:#c5c5c5;--accent2:#e0e0e0}body.light[data-theme=gray]{--accent:#303030;--accent2:#4a4a4a}
</style></head><body><main>
<div class="header"><h1>ESP Power Guardian</h1></div>
<nav class="tabs"><button class="tab active" data-page="summary">Resumen</button><button class="tab" data-page="technical">Datos técnicos</button><button class="tab" data-page="admin">Administración</button></nav>

<section id="summary" class="page active">
<div class="panel"><h3>Flujo de energía</h3><div class="flow">
<div class="node"><div class="duo">⚡</div><strong>Red eléctrica</strong><span id="sumInput">— V</span></div><div class="wire"></div>
<div class="node ups"><div class="duo">🛡️</div><h2>SAI</h2><div class="meters">
<div class="meter"><div class="ring" id="loadRing" style="--pct:0%"><span id="loadPct">0%</span></div><div><b id="sumLoad">Carga: 0 %</b><br><small id="sumPower">Potencia estimada: 0 W</small></div></div>
<div class="meter"><div class="ring" id="batteryRing" style="--pct:0%"><span id="batteryPct">0%</span></div><div><b id="sumBattery">Batería: 0 %</b><br><small id="sumRuntime">Tiempo estimado: — min</small></div></div>
</div></div><div class="wire"></div><div class="node"><div class="duo">🔌</div><strong>Salida protegida</strong><span id="sumOutput">— V</span></div></div></div>
<div class="summary-row"><div class="card"><div class="label">⚡ Alimentación</div><div id="sumCondition" class="value">Consultando el dispositivo…</div></div>
<div class="card"><div class="label">🕘 Últimos datos recibidos</div><div id="sumLastData" class="value">Esperando datos…</div></div></div>
<div class="panel"><h3>Historial de cortes</h3><p class="notice">No hay cortes registrados durante este arranque.</p></div>
</section>

<section id="technical" class="page">
<div class="panel"><h2>⚙️ Dispositivo</h2><div class="cards"><div class="card"><div class="label" title="Versión actualmente instalada">🏷️ Versión del firmware</div><div id="tFirmware" class="value">—</div></div><div class="card"><div class="label">⏱️ Tiempo encendido</div><div id="tUptime" class="value">—</div></div><div class="card"><div class="label">👁️ Tiempo monitorizando el SAI</div><div id="tMonitoring" class="value">—</div></div></div></div>
<div class="panel"><h2>📡 Conectividad</h2><div class="cards four"><div class="card"><div class="label">📶 Wi‑Fi</div><div id="tWifi" class="value">—</div></div><div class="card"><div class="label">🌐 Dirección IP</div><div id="tIp" class="value">—</div></div><div class="card"><div class="label">📊 Señal Wi‑Fi</div><div id="tRssi" class="value">—</div></div><div class="card"><div class="label">🔌 Servidor NUT</div><div id="tNut" class="value">—</div></div></div></div>
<div class="panel"><h2>🔗 SAI y USB</h2><div class="cards four"><div class="card"><div class="label">🛡️ Conexión con el SAI</div><div id="tUps" class="value">—</div></div><div class="card"><div class="label">🔌 Estado USB</div><div id="tUsb" class="value">—</div></div><div class="card"><div class="label">🏭 Fabricante USB (VID)</div><div id="tVid" class="value">—</div></div><div class="card"><div class="label">🔢 Producto USB (PID)</div><div id="tPid" class="value">—</div></div><div class="card"><div class="label">🧠 Protocolo Qx</div><div id="tQx" class="value">—</div></div></div></div>
<div class="panel"><h2>⚡ Entrada y salida</h2><div class="cards four"><div class="card"><div class="label">⚡ Alimentación</div><div id="tCondition" class="value">—</div></div><div class="card"><div class="label">➡️ Tensión de entrada</div><div id="tInput" class="value">—</div></div><div class="card"><div class="label">⬅️ Tensión de salida</div><div id="tOutput" class="value">—</div></div><div class="card"><div class="label">📈 Carga del SAI</div><div id="tLoad" class="value">—</div></div><div class="card"><div class="label">〰️ Frecuencia</div><div id="tFrequency" class="value">—</div></div></div></div>
<div class="panel"><h2>🔋 Batería</h2><div class="cards four"><div class="card"><div class="label">🔋 Estado de batería</div><div id="tBatteryState" class="value">—</div></div><div class="card"><div class="label">⚡ Tensión de batería</div><div id="tBatteryVoltage" class="value">—</div></div><div class="card"><div class="label">🔋 Batería estimada</div><div id="tBattery" class="value">—</div></div><div class="card"><div class="label">⌛ Autonomía estimada</div><div id="tRuntime" class="value">—</div></div></div></div>
<div class="panel"><h2>🩺 Diagnóstico</h2><div class="cards"><div class="card"><div class="label">🕘 Últimos datos recibidos</div><div id="tLastData" class="value">—</div></div><div class="card"><div class="label">📟 Estado NUT</div><div id="tNutStatus" class="value">—</div></div><div class="card"><div class="label">🧠 Diagnóstico Qx</div><div id="tQxDiag" class="value">—</div></div></div></div>
</section>

<section id="admin" class="page">
<div class="panel"><h2>Apariencia</h2><div class="controls">
<div class="appearance-item"><h3 class="appearance-title">Modo</h3><div class="segmented"><button id="modeDark" class="choice" onclick="setMode('dark')">Oscuro</button><button id="modeLight" class="choice" onclick="setMode('light')">Claro</button></div></div>
<div class="appearance-item"><h3 class="appearance-title">Color</h3><div class="swatches"><button class="swatch" data-theme="ocean" style="background:#20a8c8"></button><button class="swatch" data-theme="forest" style="background:#39a875"></button><button class="swatch" data-theme="violet" style="background:#8668f4"></button><button class="swatch" data-theme="mono" style="background:linear-gradient(135deg,#000 50%,#fff 50%)"></button><button class="swatch" data-theme="gray" style="background:linear-gradient(135deg,#555 50%,#bbb 50%)"></button></div></div>
<div class="appearance-item"><h3 class="appearance-title">Tamaño de texto</h3><select id="fontScale"><option value=".78">Muy compacto</option><option value=".9">Compacto</option><option value="1">Normal</option><option value="1.15">Grande</option><option value="1.3">Muy grande</option></select></div>
<div class="appearance-item"><h3 class="appearance-title">Tipo de letra</h3><select id="fontFamily"><option>Nunito</option><option>Inter</option><option>Roboto</option><option>Open Sans</option><option>Lato</option><option>Montserrat</option><option>Poppins</option><option>Raleway</option><option>Ubuntu</option><option>Merriweather</option></select></div>
<div class="appearance-item"><h3 class="appearance-title">Redondeado de paneles</h3><div class="range-row"><input id="panelRadius" type="range" min="0" max="28"><span id="panelRadiusV"></span></div></div>
<div class="appearance-item"><h3 class="appearance-title">Redondeado de tarjetas</h3><div class="range-row"><input id="cardRadius" type="range" min="0" max="24"><span id="cardRadiusV"></span></div></div>
<div class="appearance-item"><h3 class="appearance-title">Redondeado de controles</h3><div class="range-row"><input id="controlRadius" type="range" min="0" max="20"><span id="controlRadiusV"></span></div></div>
<div class="appearance-item"><h3 class="appearance-title">Separación entre paneles</h3><div class="range-row"><input id="panelGap" type="range" min="0" max="24"><span id="panelGapV"></span></div></div>
<div class="appearance-item"><h3 class="appearance-title">Relleno interior de paneles</h3><div class="range-row"><input id="panelPadding" type="range" min="4" max="30"><span id="panelPaddingV"></span></div></div>
<div class="appearance-item"><h3 class="appearance-title">Separación entre elementos</h3><div class="range-row"><input id="contentGap" type="range" min="4" max="24"><span id="contentGapV"></span></div></div>
<div class="appearance-item"><h3 class="appearance-title">Relleno interior de tarjetas</h3><div class="range-row"><input id="cardPadding" type="range" min="4" max="24"><span id="cardPaddingV"></span></div></div>
</div></div>
<div class="panel"><h2>LED de estado</h2><div class="color-grid">
<div class="color-row"><span>Iniciando</span><input id="starting" type="color"></div><div class="color-row"><span>Alimentación de red</span><input id="online" type="color"></div>
<div class="color-row"><span>Funcionando con batería</span><input id="on_battery" type="color"></div><div class="color-row"><span>Batería baja</span><input id="low_battery" type="color"></div>
<div class="color-row"><span>Alarma</span><input id="fault" type="color"></div><div class="color-row"><span>SAI desconectado</span><input id="disconnected" type="color"></div></div>
<div class="appearance-item"><h3 class="appearance-title">Brillo</h3><div class="range-row"><input id="brightness" type="range" min="1" max="255"><span id="brightnessV"></span></div></div><button class="button" onclick="saveLed()">Guardar LED</button></div>
<div class="panel"><h2>Red y dirección IP</h2><label>Red (SSID)</label><div class="wifi-row"><input id="ssid" type="text"><button class="button" onclick="scanWifi()">Buscar redes</button></div><div id="networks" class="notice"></div>
<label>Contraseña</label><input id="password" type="password"><h3 class="appearance-title">Asignación de dirección</h3><div class="segmented"><button class="choice active">DHCP (automática)</button><button class="choice" disabled title="Se incorporará en la siguiente revisión limpia">IP fija</button></div>
<p><button class="button" onclick="saveWifi()">Guardar y reiniciar</button></p></div>
<div class="panel"><h2>Actualización OTA</h2><input id="firmwareFile" type="file" accept=".bin"><p><button class="button" onclick="ota()">Instalar firmware</button></p></div>
</section>
<footer><span>Made by <a href="https://github.com/rafasanz">@rafasanz</a></span><span id="footerVersion">Versión: —</span></footer>
</main><script>
const $=id=>document.getElementById(id),ledIds=['starting','online','on_battery','low_battery','fault','disconnected'];
function api(url,options={}){return new Promise((resolve,reject)=>{const xhr=new XMLHttpRequest();xhr.open(options.method||'GET',url);Object.entries(options.headers||{}).forEach(([key,value])=>xhr.setRequestHeader(key,value));xhr.onload=()=>{if(xhr.status>=200&&xhr.status<300)resolve({json:()=>Promise.resolve(JSON.parse(xhr.responseText)),text:()=>Promise.resolve(xhr.responseText)});else reject(new Error('HTTP '+xhr.status))};xhr.onerror=()=>reject(new Error('Error de red'));xhr.send(options.body||null)})}
document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tab,.page').forEach(x=>x.classList.remove('active'));b.classList.add('active');$(b.dataset.page).classList.add('active')});
const fmt=s=>{s=Math.max(0,s||0);let h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h?`${h} h ${m} min`:`${m} min ${s%60} s`};
function text(id,v){$(id).textContent=v}
async function refresh(){try{let s=await api('/api/status').then(r=>r.json());window.guardian=s;
text('footerVersion','Versión: '+s.firmware);text('sumCondition',s.condition);text('sumInput',s.data_valid?s.input_voltage.toFixed(1)+' V':'— V');text('sumOutput',s.data_valid?s.output_voltage.toFixed(1)+' V':'— V');
text('sumLoad',`Carga: ${s.load} %`);text('sumPower',`Potencia estimada: ${Math.round(700*s.load/100)} W`);text('loadPct',s.load+'%');$('loadRing').style.setProperty('--pct',s.load+'%');
text('sumBattery',`Batería: ${s.battery} %`);text('batteryPct',s.battery+'%');$('batteryRing').style.setProperty('--pct',s.battery+'%');text('sumRuntime',`Tiempo estimado: ${Math.round(s.runtime_s/60)} min`);
text('sumLastData',s.last_data_age_s>=0?`hace ${s.last_data_age_s} s`:'Aún no se han recibido datos Qx');
text('tFirmware',s.firmware);text('tUptime',fmt(s.uptime_s));text('tMonitoring',s.data_valid?fmt(s.uptime_s):'0 s');text('tWifi',s.wifi);text('tIp',s.ip);text('tRssi',s.rssi+' dBm');text('tNut',`${s.ip}:${s.nut_port} · guardian`);
text('tUps',s.usb_vid==='0000'?'sin detectar':'conectado');text('tUsb',s.usb_vid==='0000'?'esperando dispositivo':'HID detectado');text('tVid',s.usb_vid);text('tPid',s.usb_pid);text('tQx',s.qx_status);
text('tCondition',s.condition);text('tInput',s.data_valid?s.input_voltage.toFixed(1)+' V':'—');text('tOutput',s.data_valid?s.output_voltage.toFixed(1)+' V':'—');text('tLoad',s.data_valid?s.load+' %':'—');text('tFrequency',s.data_valid?s.frequency.toFixed(1)+' Hz':'—');
text('tBatteryState',s.battery<20?'baja':s.data_valid?'normal':'sin datos');text('tBatteryVoltage',s.data_valid?s.battery_voltage.toFixed(1)+' V':'—');text('tBattery',s.data_valid?s.battery+' %':'—');text('tRuntime',s.data_valid?Math.round(s.runtime_s/60)+' min':'—');
text('tLastData',s.last_data_age_s>=0?`hace ${s.last_data_age_s} s`:'sin datos');text('tNutStatus',s.ups_status);text('tQxDiag',s.qx_status);
}catch(e){text('sumCondition','No se pudo consultar el dispositivo');text('tQxDiag',e.message)}}
async function loadLed(){let p=await api('/api/led').then(r=>r.json());ledIds.forEach(k=>$(k).value=p[k]);$('brightness').value=p.brightness;text('brightnessV',p.brightness)}
async function saveLed(){let p={brightness:+$('brightness').value};ledIds.forEach(k=>p[k]=$(k).value);alert(await api('/api/led',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)}).then(r=>r.text()));await loadLed()}
async function saveWifi(){let body=new URLSearchParams({ssid:$('ssid').value,password:$('password').value});alert(await api('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString()}).then(r=>r.text()))}
async function scanWifi(){text('networks','Buscando redes…');try{let n=await api('/api/wifi/scan').then(r=>r.json());$('networks').innerHTML=n.map(x=>`<button class="choice" onclick="document.getElementById('ssid').value='${x.ssid.replaceAll("'","")}';">${x.ssid} (${x.rssi} dBm)</button>`).join(' ')}catch(e){text('networks','No se pudo completar la búsqueda.')}}
async function ota(){let f=$('firmwareFile').files[0];if(!f)return alert('Selecciona un archivo .bin');alert(await api('/api/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f}).then(r=>r.text()))}
const defaults={mode:'dark',theme:'ocean',fontScale:'1',fontFamily:'Nunito',panelRadius:6,cardRadius:6,controlRadius:7,panelGap:12,panelPadding:18,contentGap:10,cardPadding:12};
let appearance={...defaults,...JSON.parse(localStorage.getItem('appearance')||'{}')};function applyAppearance(){document.body.classList.toggle('light',appearance.mode==='light');document.body.dataset.theme=appearance.theme;document.documentElement.style.setProperty('--scale',appearance.fontScale);document.documentElement.style.setProperty('--font',appearance.fontFamily+',system-ui,sans-serif');
['panelRadius','cardRadius','controlRadius','panelGap','panelPadding','contentGap','cardPadding'].forEach(k=>{let css='--'+k.replace(/[A-Z]/g,m=>'-'+m.toLowerCase());document.documentElement.style.setProperty(css,appearance[k]+'px');$(k).value=appearance[k];text(k+'V',appearance[k]+' px')});
$('fontScale').value=appearance.fontScale;$('fontFamily').value=appearance.fontFamily;$('modeDark').classList.toggle('active',appearance.mode==='dark');$('modeLight').classList.toggle('active',appearance.mode==='light');document.querySelectorAll('.swatch').forEach(x=>x.classList.toggle('active',x.dataset.theme===appearance.theme));localStorage.setItem('appearance',JSON.stringify(appearance))}
function setMode(v){appearance.mode=v;applyAppearance()}document.querySelectorAll('.swatch').forEach(x=>x.onclick=()=>{appearance.theme=x.dataset.theme;applyAppearance()});
$('fontScale').onchange=e=>{appearance.fontScale=e.target.value;applyAppearance()};$('fontFamily').onchange=e=>{appearance.fontFamily=e.target.value;applyAppearance()};
['panelRadius','cardRadius','controlRadius','panelGap','panelPadding','contentGap','cardPadding'].forEach(k=>$(k).oninput=e=>{appearance[k]=+e.target.value;applyAppearance()});$('brightness').oninput=e=>text('brightnessV',e.target.value);
applyAppearance();loadLed();refresh();setInterval(refresh,2000);
</script></body></html>)HTML";
