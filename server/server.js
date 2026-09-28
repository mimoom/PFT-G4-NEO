// ============================================================================
//  Zber stavov relé z Opty + dashboard
//
//  Zámerne bez závislostí – iba vstavané moduly Node.js. Menej pohyblivých
//  častí, menší image, žiadny `npm install` pri nasadení.
//
//  Opta sem posiela POST /api/report každých pár sekúnd. Server si drží
//  posledný stav a krátku históriu zmien, dashboard ho ukazuje.
// ============================================================================

import http from 'node:http';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

// --- konfigurácia -----------------------------------------------------------

const PORT = Number(process.env.PORT || 8080);
const DATA_DIR = process.env.DATA_DIR || './data';

// Token, ktorým sa hlási Opta. MUSÍ byť nastavený.
const DEVICE_TOKEN = process.env.DEVICE_TOKEN || '';

// Prihlásenie do dashboardu:
//   none     – bez prihlásenia (len na lokálnu sieť pri ladení)
//   password – jedno spoločné heslo z ADMIN_PASSWORD
//   Sem neskôr pribudne 'sso'. Rozhranie je v requireAuth() a authRoutes().
const AUTH_MODE = process.env.AUTH_MODE || 'password';
const ADMIN_PASSWORD = process.env.ADMIN_PASSWORD || '';
const SESSION_SECRET = process.env.SESSION_SECRET || crypto.randomBytes(32).toString('hex');

// Ako dlho bez hlásenia považovať zariadenie za offline
const OFFLINE_AFTER_MS = Number(process.env.OFFLINE_AFTER_MS || 15000);

const EXT_CHANNELS = 8;   // relé na Waveshare module
const INT_CHANNELS = 4;   // relé priamo na Opte
const HISTORY_MAX = 500;

// --- kontrola nastavenia pri štarte -----------------------------------------

const fatal = [];
if (!DEVICE_TOKEN) fatal.push('DEVICE_TOKEN nie je nastavený – Opta by sa nemala ako overiť.');
if (AUTH_MODE === 'password' && !ADMIN_PASSWORD) {
  fatal.push('AUTH_MODE=password, ale ADMIN_PASSWORD je prázdne.');
}
if (fatal.length) {
  console.error('Chyba konfigurácie:');
  for (const f of fatal) console.error('  - ' + f);
  console.error('Skopíruj .env.example do .env a vyplň ho.');
  process.exit(1);
}
if (AUTH_MODE === 'none') {
  console.warn('POZOR: AUTH_MODE=none – dashboard je verejný. Len pre lokálnu sieť.');
}

// --- úložisko ---------------------------------------------------------------

const statePath = path.join(DATA_DIR, 'state.json');

let store = { devices: {}, history: [] };

try {
  fs.mkdirSync(DATA_DIR, { recursive: true });
  if (fs.existsSync(statePath)) {
    store = JSON.parse(fs.readFileSync(statePath, 'utf8'));
    store.devices ||= {};
    store.history ||= [];
  }
} catch (e) {
  console.error('Stav sa nedal načítať, začínam s prázdnym:', e.message);
}

let saveTimer = null;
function saveSoon() {
  if (saveTimer) return;
  saveTimer = setTimeout(() => {
    saveTimer = null;
    // Zapíš cez dočasný súbor, nech výpadok uprostred nezanechá zmrzačený JSON
    const tmp = statePath + '.tmp';
    try {
      fs.writeFileSync(tmp, JSON.stringify(store));
      fs.renameSync(tmp, statePath);
    } catch (e) {
      console.error('Stav sa nedal uložiť:', e.message);
    }
  }, 2000);
}

// --- pomocné ----------------------------------------------------------------

const timingSafeEqual = (a, b) => {
  const ba = Buffer.from(String(a));
  const bb = Buffer.from(String(b));
  if (ba.length !== bb.length) return false;
  return crypto.timingSafeEqual(ba, bb);
};

function sign(value) {
  const mac = crypto.createHmac('sha256', SESSION_SECRET).update(value).digest('base64url');
  return `${value}.${mac}`;
}

function verify(signed) {
  if (typeof signed !== 'string') return null;
  const i = signed.lastIndexOf('.');
  if (i < 0) return null;
  const value = signed.slice(0, i);
  if (!timingSafeEqual(signed, sign(value))) return null;
  return value;
}

function parseCookies(req) {
  const out = {};
  for (const part of (req.headers.cookie || '').split(';')) {
    const i = part.indexOf('=');
    if (i > 0) out[part.slice(0, i).trim()] = decodeURIComponent(part.slice(i + 1).trim());
  }
  return out;
}

function loggedIn(req) {
  if (AUTH_MODE === 'none') return true;
  const raw = parseCookies(req).sid;
  const value = verify(raw);
  if (!value) return false;
  const [, expiry] = value.split('|');
  return Number(expiry) > Date.now();
}

function readBody(req, limit = 64 * 1024) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', (c) => {
      size += c.length;
      if (size > limit) { reject(new Error('telo je príliš veľké')); req.destroy(); return; }
      chunks.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

function json(res, code, body) {
  const s = JSON.stringify(body);
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    'Content-Length': Buffer.byteLength(s),
  });
  res.end(s);
}

function html(res, code, body) {
  res.writeHead(code, {
    'Content-Type': 'text/html; charset=utf-8',
    'Cache-Control': 'no-store',
    'Content-Security-Policy': "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'",
    'X-Content-Type-Options': 'nosniff',
    'Content-Length': Buffer.byteLength(body),
  });
  res.end(body);
}

// Pole 0/1 danej dĺžky, čokoľvek iné zahodíme
function normalizeRelays(value, count) {
  if (!Array.isArray(value)) return null;
  if (value.length !== count) return null;
  return value.map((v) => (v ? 1 : 0));
}

// --- príjem hlásení z Opty --------------------------------------------------

async function handleReport(req, res) {
  const token = req.headers['x-device-token'];
  if (!token || !timingSafeEqual(token, DEVICE_TOKEN)) {
    return json(res, 401, { ok: false, error: 'zlý alebo chýbajúci token' });
  }

  let body;
  try {
    body = JSON.parse(await readBody(req));
  } catch {
    return json(res, 400, { ok: false, error: 'telo nie je platný JSON' });
  }

  const ext = normalizeRelays(body.ext, EXT_CHANNELS);
  const int = normalizeRelays(body.int, INT_CHANNELS);
  if (!ext || !int) {
    return json(res, 400, {
      ok: false,
      error: `očakávam ext[${EXT_CHANNELS}] a int[${INT_CHANNELS}] ako polia 0/1`,
    });
  }

  const id = String(body.device || 'opta').slice(0, 40).replace(/[^\w.-]/g, '');
  const now = Date.now();
  const prev = store.devices[id];

  // Do histórie ukladáme len zmeny, nie každé hlásenie
  const changed = !prev ||
    prev.ext.join('') !== ext.join('') ||
    prev.int.join('') !== int.join('');

  store.devices[id] = {
    ext,
    int,
    lastSeen: now,
    modbusOk: body.modbusOk !== false,
    ip: req.socket.remoteAddress,
  };

  if (changed) {
    store.history.push({ t: now, device: id, ext, int });
    if (store.history.length > HISTORY_MAX) {
      store.history.splice(0, store.history.length - HISTORY_MAX);
    }
  }

  saveSoon();
  json(res, 200, { ok: true, t: now });
}

// --- stav pre dashboard -----------------------------------------------------

function devicesView() {
  const now = Date.now();
  return Object.entries(store.devices).map(([id, d]) => ({
    id,
    ext: d.ext,
    int: d.int,
    modbusOk: d.modbusOk !== false,
    lastSeen: d.lastSeen,
    secondsAgo: Math.round((now - d.lastSeen) / 1000),
    online: now - d.lastSeen < OFFLINE_AFTER_MS,
  }));
}

// --- prihlásenie ------------------------------------------------------------

async function authRoutes(req, res, url) {
  if (url.pathname === '/login' && req.method === 'GET') {
    return html(res, 200, loginPage(url.searchParams.get('e') ? 'Nesprávne heslo.' : ''));
  }

  if (url.pathname === '/login' && req.method === 'POST') {
    const body = new URLSearchParams(await readBody(req, 4096));
    if (ADMIN_PASSWORD && timingSafeEqual(body.get('password') || '', ADMIN_PASSWORD)) {
      const sid = sign(`user|${Date.now() + 12 * 3600 * 1000}`);
      res.writeHead(302, {
        'Set-Cookie': `sid=${encodeURIComponent(sid)}; HttpOnly; SameSite=Lax; Path=/; Max-Age=43200`,
        Location: '/',
      });
      return res.end();
    }
    res.writeHead(302, { Location: '/login?e=1' });
    return res.end();
  }

  if (url.pathname === '/logout') {
    res.writeHead(302, { 'Set-Cookie': 'sid=; Path=/; Max-Age=0', Location: '/login' });
    return res.end();
  }
  return false;
}

// --- HTTP -------------------------------------------------------------------

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

  try {
    if (url.pathname === '/healthz') return json(res, 200, { ok: true });

    // Opta sa hlási tokenom, nie prihlásením
    if (url.pathname === '/api/report' && req.method === 'POST') {
      return handleReport(req, res);
    }

    if (['/login', '/logout'].includes(url.pathname)) {
      const handled = await authRoutes(req, res, url);
      if (handled !== false) return;
    }

    if (!loggedIn(req)) {
      if (url.pathname.startsWith('/api/')) return json(res, 401, { ok: false, error: 'neprihlásený' });
      res.writeHead(302, { Location: '/login' });
      return res.end();
    }

    if (url.pathname === '/api/state') {
      return json(res, 200, { ok: true, devices: devicesView(), offlineAfterMs: OFFLINE_AFTER_MS });
    }
    if (url.pathname === '/api/history') {
      return json(res, 200, { ok: true, history: store.history.slice(-100).reverse() });
    }
    if (url.pathname === '/') return html(res, 200, dashboardPage());

    return json(res, 404, { ok: false, error: 'nenájdené' });
  } catch (e) {
    console.error(e);
    return json(res, 500, { ok: false, error: 'chyba servera' });
  }
});

server.listen(PORT, () => {
  console.log(`Server beží na http://0.0.0.0:${PORT}`);
  console.log(`Prihlásenie: ${AUTH_MODE}`);
  console.log(`Opta má posielať POST na /api/report s hlavičkou X-Device-Token`);
});

// --- stránky ----------------------------------------------------------------

const STYLE = `
:root{--bg:#f4f4f5;--card:#fff;--tx:#18181b;--mu:#71717a;--br:#e4e4e7;
--on:#22c55e;--off:#a1a1aa;--bad:#ef4444}
@media(prefers-color-scheme:dark){:root{--bg:#18181b;--card:#27272a;
--tx:#fafafa;--mu:#a1a1aa;--br:#3f3f46}}
*{box-sizing:border-box}
body{margin:0;padding:24px 16px;background:var(--bg);color:var(--tx);
font:16px/1.5 system-ui,-apple-system,sans-serif}
.w{max-width:760px;margin:0 auto}
h1{font-size:22px;margin:0 0 4px}
.sub{color:var(--mu);font-size:14px;margin:0 0 24px}
.card{background:var(--card);border:1px solid var(--br);border-radius:12px;
padding:16px;margin-bottom:16px}
.hd{display:flex;align-items:center;gap:8px;margin-bottom:14px}
.hd h2{font-size:16px;margin:0;flex:1}
.tag{font-size:12px;padding:3px 9px;border-radius:99px;border:1px solid var(--br);
color:var(--mu)}
.tag.on{background:var(--on);border-color:var(--on);color:#052e16}
.tag.off{background:var(--bad);border-color:var(--bad);color:#fff}
.grp{font-size:12px;text-transform:uppercase;letter-spacing:.05em;
color:var(--mu);margin:12px 0 8px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(84px,1fr));gap:8px}
.r{border:1px solid var(--br);border-radius:8px;padding:10px;text-align:center}
.r .n{font-size:12px;color:var(--mu)}
.r .v{font-weight:600;margin-top:2px}
.r.on{border-color:var(--on)}
.r.on .v{color:var(--on)}
.r.off .v{color:var(--off)}
.empty{color:var(--mu);text-align:center;padding:32px 0}
form{max-width:320px;margin:15vh auto}
input{width:100%;padding:10px;border-radius:8px;border:1px solid var(--br);
background:var(--card);color:var(--tx);font:inherit;margin-bottom:8px}
button{width:100%;padding:10px;border-radius:8px;border:0;background:#2563eb;
color:#fff;font:inherit;font-weight:500;cursor:pointer}
.err{color:var(--bad);font-size:14px;margin-bottom:8px}
a{color:var(--mu);font-size:13px}
`;

function loginPage(error) {
  return `<!DOCTYPE html><html lang=sk><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Prihlásenie</title><style>${STYLE}</style></head><body>
<form method=post action=/login>
<h1>Stavy relé</h1>
<p class=sub>Zadaj heslo.</p>
${error ? `<p class=err>${error}</p>` : ''}
<input type=password name=password placeholder="Heslo" autofocus required>
<button type=submit>Prihlásiť</button>
</form></body></html>`;
}

function dashboardPage() {
  return `<!DOCTYPE html><html lang=sk><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Stavy relé</title><style>${STYLE}</style></head><body><div class=w>
<h1>Stavy relé</h1>
<p class=sub>Obnovuje sa každé 2 s &middot; <a href=/logout>odhlásiť</a></p>
<div id=out><p class=empty>načítavam…</p></div>
</div>
<script>
const EXT=${EXT_CHANNELS}, INT=${INT_CHANNELS};
function tile(name,v){
  return '<div class="r '+(v?'on':'off')+'"><div class=n>'+name+
         '</div><div class=v>'+(v?'ZAP':'VYP')+'</div></div>';
}
function card(d){
  const age = d.online ? 'pred '+d.secondsAgo+' s'
                       : 'nehlási sa '+d.secondsAgo+' s';
  return '<div class=card><div class=hd><h2>'+d.id+'</h2>'+
    '<span class="tag '+(d.online?'on':'off')+'">'+(d.online?'online':'offline')+'</span>'+
    (d.modbusOk?'':'<span class="tag off">RS485</span>')+
    '</div><div class=sub style="margin:0">'+age+'</div>'+
    '<div class=grp>Opta – interné relé</div><div class=grid>'+
    d.int.map((v,i)=>tile('R'+(i+1),v)).join('')+'</div>'+
    '<div class=grp>Modul na RS485</div><div class=grid>'+
    d.ext.map((v,i)=>tile('X'+(i+1),v)).join('')+'</div></div>';
}
async function tick(){
  try{
    const r = await fetch('/api/state');
    if(r.status===401){location.href='/login';return}
    const d = await r.json();
    document.getElementById('out').innerHTML = d.devices.length
      ? d.devices.map(card).join('')
      : '<p class=empty>Zatiaľ sa neohlásilo žiadne zariadenie.</p>';
  }catch(e){}
}
tick(); setInterval(tick,2000);
</script></body></html>`;
}
