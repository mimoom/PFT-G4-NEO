// ============================================================================
//  Arduino Opta – ovladanie rele cez Ethernet (LAN)
//
//  Otvor v prehliadaci http://<ip-adresa-opty>/  a klikaj.
//  IP sa vypise do Serial Monitora po starte.
//
//  API (da sa volat aj z Home Assistant, Node-RED, curl...):
//     GET /api/state                  stav vsetkych rele ako JSON
//     GET /api/relay?ch=3&v=on        zapni rele 3   (v = on | off | toggle)
//     GET /api/alloff                 vypni vsetky
//
//  Kniznice: ArduinoRS485, ArduinoModbus  (PortentaEthernet je v jadre Opty)
//  Doska:    Arduino Mbed OS Opta Boards -> Opta
//  Monitor:  115200 baud
//
//  POZOR NA BEZPECNOST: server nema ziadne overovanie, kym nenastavis
//  API_KEY nizsie. Nechaj ho len v domacej sieti. Nesmeruj nan port
//  z internetu – ovlada silove kontakty 10 A. Na pristup zvonku pouzi
//  VPN, nie presmerovanie portu.
// ============================================================================

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>
#include <PortentaEthernet.h>

// --- nastavenia -------------------------------------------------------------
const int  SLAVE_ID = 1;        // adresa relé modulu na RS485
const int  CHANNELS = 8;
const long MB_BAUD  = 9600;

// Prazdne = bez hesla. Ked vyplnis, kazda poziadavka musi mat &key=...
// Na firemnej sieti to vypln – ovladas silove kontakty.
const char* API_KEY = "";

// --- siet ---
// false = adresu pridelí DHCP (vypise sa do Serial Monitora)
// true  = pevna adresa nizsie; pouzi, ked chces vediet URL dopredu
//         alebo ked v sieti DHCP nie je
const bool USE_STATIC_IP = false;

IPAddress STATIC_IP (192, 168, 88, 200);   // musi byt volna!
IPAddress GATEWAY   (192, 168, 88, 1);     // router
IPAddress SUBNET    (255, 255, 255, 0);
IPAddress DNS_SRV   (192, 168, 88, 1);

// Pomenuj si kanaly, zobrazia sa na stranke
const char* NAMES[CHANNELS] = {
  "Rele 1", "Rele 2", "Rele 3", "Rele 4",
  "Rele 5", "Rele 6", "Rele 7", "Rele 8"
};

EthernetServer server(80);
bool state[CHANNELS] = {false};
bool linkUp = false;

// --- Modbus -----------------------------------------------------------------

bool readState() {
  if (!ModbusRTUClient.requestFrom(SLAVE_ID, COILS, 0, CHANNELS)) return false;
  for (int i = 0; i < CHANNELS; i++) state[i] = ModbusRTUClient.read() != 0;
  return true;
}

bool setRelay(int ch, bool on) {          // ch = 0..7
  if (!ModbusRTUClient.coilWrite(SLAVE_ID, ch, on ? 1 : 0)) return false;
  state[ch] = on;
  return true;
}

bool setAll(bool on) {                    // coil 0x00FF = vsetky naraz
  if (!ModbusRTUClient.coilWrite(SLAVE_ID, 0x00FF, on ? 1 : 0)) return false;
  for (int i = 0; i < CHANNELS; i++) state[i] = on;
  return true;
}

// --- HTTP -------------------------------------------------------------------

String jsonState(bool ok) {
  String s = "{\"ok\":";
  s += ok ? "true" : "false";
  s += ",\"relays\":[";
  for (int i = 0; i < CHANNELS; i++) {
    if (i) s += ',';
    s += state[i] ? '1' : '0';
  }
  s += "]}";
  return s;
}

void sendJson(EthernetClient& c, const String& body) {
  c.println("HTTP/1.1 200 OK");
  c.println("Content-Type: application/json");
  c.println("Cache-Control: no-store");
  c.println("Connection: close");
  c.print("Content-Length: "); c.println((unsigned)body.length());
  c.println();
  c.print(body);
}

void sendStatus(EthernetClient& c, const char* status) {
  c.print("HTTP/1.1 "); c.println(status);
  c.println("Content-Type: text/plain");
  c.println("Connection: close");
  c.println();
  c.println(status);
}

// Stranka je zamerne mala – bezi z pamate Opty.
void sendPage(EthernetClient& c) {
  c.println("HTTP/1.1 200 OK");
  c.println("Content-Type: text/html; charset=utf-8");
  c.println("Connection: close");
  c.println();

  c.print(F(
    "<!DOCTYPE html><html lang=sk><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Rele</title><style>"
    ":root{--bg:#f4f4f5;--card:#fff;--tx:#18181b;--mu:#71717a;--br:#e4e4e7}"
    "@media(prefers-color-scheme:dark){:root{"
    "--bg:#18181b;--card:#27272a;--tx:#fafafa;--mu:#a1a1aa;--br:#3f3f46}}"
    "*{box-sizing:border-box}"
    "body{margin:0;padding:16px;background:var(--bg);color:var(--tx);"
    "font:16px/1.5 system-ui,sans-serif}"
    "h1{font-size:20px;margin:0 0 4px}"
    "p{color:var(--mu);margin:0 0 16px;font-size:14px}"
    ".r{display:flex;align-items:center;gap:12px;background:var(--card);"
    "border:1px solid var(--br);border-radius:10px;padding:12px;margin-bottom:8px}"
    ".n{flex:1;font-weight:500}"
    ".d{width:10px;height:10px;border-radius:50%;background:#a1a1aa}"
    ".d.on{background:#22c55e;box-shadow:0 0 8px #22c55e}"
    "button{font:inherit;padding:8px 16px;border-radius:8px;cursor:pointer;"
    "border:1px solid var(--br);background:var(--card);color:var(--tx)}"
    "button.on{background:#22c55e;border-color:#22c55e;color:#062}"
    "#all{width:100%;margin-top:8px;padding:12px}"
    "</style></head><body>"
    "<h1>Rele</h1><p id=s>nacitavam...</p><div id=l></div>"
    "<button id=all onclick=allOff()>Vypnut vsetky</button>"
    "<script>"
    "const K=new URLSearchParams(location.search).get('key');"
    "const q=p=>p+(K?(p.includes('?')?'&':'?')+'key='+encodeURIComponent(K):'');"
  ));
  // Mena kanalov vlozime ako JS pole
  c.print(F("const N=["));
  for (int i = 0; i < CHANNELS; i++) {
    if (i) c.print(',');
    c.print('"'); c.print(NAMES[i]); c.print('"');
  }
  c.print(F("];"));

  c.print(F(
    "function draw(d){"
    "document.getElementById('s').textContent="
    "d.ok?'spojenie s modulom OK':'modul neodpoveda';"
    "document.getElementById('l').innerHTML=d.relays.map((v,i)=>"
    "'<div class=r><span class=\"d'+(v?' on':'')+'\"></span>"
    "<span class=n>'+N[i]+'</span>"
    "<button class=\"'+(v?'on':'')+'\" onclick=\"t('+i+')\">'"
    "+(v?'ZAP':'VYP')+'</button></div>').join('');}"
    "async function get(u){const r=await fetch(q(u));"
    "if(r.status==401){document.getElementById('s').textContent='chyba: zly kluc';return}"
    "draw(await r.json())}"
    "const t=i=>get('/api/relay?ch='+(i+1)+'&v=toggle');"
    "const allOff=()=>get('/api/alloff');"
    "get('/api/state');setInterval(()=>get('/api/state'),3000);"
    "</script></body></html>"
  ));
}

// Vytiahne hodnotu parametra z query retazca, "" ak nie je
String param(const String& path, const String& key) {
  int i = path.indexOf(key + "=");
  if (i < 0) return "";
  i += key.length() + 1;
  int e = path.indexOf('&', i);
  return path.substring(i, e < 0 ? path.length() : e);
}

bool authorized(const String& path) {
  if (API_KEY[0] == '\0') return true;
  return param(path, "key") == API_KEY;
}

void handle(EthernetClient& c, const String& path) {
  if (path.startsWith("/api/") && !authorized(path)) {
    sendStatus(c, "401 Unauthorized");
    return;
  }

  if (path.startsWith("/api/state")) {
    sendJson(c, jsonState(readState()));

  } else if (path.startsWith("/api/alloff")) {
    bool ok = setAll(false);
    readState();
    sendJson(c, jsonState(ok));

  } else if (path.startsWith("/api/relay")) {
    int ch = param(path, "ch").toInt();          // 1..8
    String v = param(path, "v");
    if (ch < 1 || ch > CHANNELS) { sendStatus(c, "400 Bad Request"); return; }

    bool target;
    if (v == "on")           target = true;
    else if (v == "off")     target = false;
    else if (v == "toggle")  { readState(); target = !state[ch - 1]; }
    else { sendStatus(c, "400 Bad Request"); return; }

    bool ok = setRelay(ch - 1, target);
    readState();                                  // pravda je vzdy z modulu
    sendJson(c, jsonState(ok));

  } else if (path == "/" || path.startsWith("/?")) {
    sendPage(c);

  } else {
    sendStatus(c, "404 Not Found");
  }
}

// --- setup / loop -----------------------------------------------------------

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  // Post-delay musi prekryt cely posledny znak (pri 9600 = 1042 us),
  // inak sa orezu koncove bity ramca. Podrobne v KOMUNIKACIA.md.
  RS485.setDelays(500, 1500);
  ModbusRTUClient.begin(MB_BAUD, SERIAL_8N1);

  Serial.println("\n=== Opta – ovladanie rele cez Ethernet ===");

  Serial.print("Modul: ");
  Serial.println(readState() ? "odpoveda" : "NEODPOVEDA (skontroluj RS485)");

  Serial.print("Ethernet: ");
  if (USE_STATIC_IP) {
    linkUp = Ethernet.begin(STATIC_IP, DNS_SRV, GATEWAY, SUBNET);
  } else {
    linkUp = Ethernet.begin();              // bez parametrov = DHCP
  }

  if (linkUp) {
    Serial.println(USE_STATIC_IP ? "pripojeny (pevna IP)" : "pripojeny (DHCP)");
    Serial.print(">>> otvor http://");
    Serial.print(Ethernet.localIP());
    Serial.println("/");
    server.begin();
  } else {
    Serial.println("CHYBA – je zapojeny kabel? svieti link na RJ45?");
    Serial.println("Ak v sieti nie je DHCP, nastav USE_STATIC_IP = true.");
    Serial.println("Rele sa daju ovladat aj z konzoly: 1-8, 0, s");
  }
}

void loop() {
  // --- web ---
  if (linkUp) {
    EthernetClient client = server.available();
    if (client) {
      client.setTimeout(2000);

      String req = client.readStringUntil('\n');      // "GET /cesta HTTP/1.1"
      while (client.connected()) {                    // preskoc hlavicky
        String line = client.readStringUntil('\n');
        if (line.length() <= 1) break;
      }

      int a = req.indexOf(' ');
      int b = req.indexOf(' ', a + 1);
      if (a >= 0 && b > a) {
        handle(client, req.substring(a + 1, b));
      } else {
        sendStatus(client, "400 Bad Request");
      }

      client.flush();
      client.stop();
    }
  }

  // --- konzola (funguje aj bez siete) ---
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '8') {
      int ch = c - '1';
      readState();
      setRelay(ch, !state[ch]);
      Serial.print("rele "); Serial.print(ch + 1);
      Serial.println(state[ch] ? " ZAP" : " VYP");
    } else if (c == '0') {
      setAll(false);
      Serial.println("vsetky VYP");
    } else if (c == 's') {
      readState();
      Serial.print("stav: ");
      for (int i = 0; i < CHANNELS; i++) {
        Serial.print(i + 1); Serial.print(state[i] ? ":ZAP " : ":vyp ");
      }
      Serial.println();
    }
  }
}
