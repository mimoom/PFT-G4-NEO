// ============================================================================
//  Arduino Opta – hlási stavy všetkých 12 relé na server
//
//     4 interné relé Opty       (R1..R4, piny D0..D3)
//     8 externých na RS485      (X1..X8, Waveshare Modbus RTU 8CH)
//
//  Opta je KLIENT: sama posiela POST na server. Preto funguje aj z inej
//  siete a netreba presmerovať žiadny port ani verejnú IP.
//
//  Ovládanie z konzoly (kým to vie server len zobrazovať):
//     1..8   prepni externé relé
//     q w e r  prepni interné relé R1..R4
//     s      vypíš stav
//     p      pošli hlásenie hneď
//
//  Knižnice: ArduinoRS485, ArduinoModbus  (PortentaEthernet je v jadre Opty)
//  Doska:    Arduino Mbed OS Opta Boards -> Opta
// ============================================================================

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>
#include <PortentaEthernet.h>
#include <EthernetSSLClient.h>

// --- server -----------------------------------------------------------------
// Teraz lokálna IP. Keď pribudne doména, staci sem napísať napr.
// "relay.mojafirma.sk" – DNS rieši Opta sama.
const char* SERVER_HOST = "192.168.88.10";
const int   SERVER_PORT = 8080;
const char* REPORT_PATH = "/api/report";

// V lokálnej sieti stačí HTTP. Akonáhle bude server na doméne a dostupný
// z internetu, prepni na true a SERVER_PORT na 443 – inak by DEVICE_TOKEN
// cestoval po sieti čitateľný.
const bool USE_HTTPS = false;

// Musí sedieť s DEVICE_TOKEN v .env na serveri
const char* DEVICE_TOKEN = "zmen-ma";

// Pod týmto menom sa zariadenie ukáže na dashboarde
const char* DEVICE_NAME = "opta-hala";

const unsigned long REPORT_EVERY_MS = 5000;

// --- sieť -------------------------------------------------------------------
const bool USE_STATIC_IP = false;          // true = pevná adresa nižšie
IPAddress STATIC_IP (192, 168, 88, 200);
IPAddress GATEWAY   (192, 168, 88, 1);
IPAddress SUBNET    (255, 255, 255, 0);
IPAddress DNS_SRV   (192, 168, 88, 1);

// --- RS485 ------------------------------------------------------------------
const int  SLAVE_ID = 1;
const long MB_BAUD  = 9600;

const int EXT_CHANNELS = 8;
const int INT_CHANNELS = 4;
const int INT_PINS[INT_CHANNELS] = {D0, D1, D2, D3};
const int INT_LEDS[INT_CHANNELS] = {LED_D0, LED_D1, LED_D2, LED_D3};

bool extState[EXT_CHANNELS] = {false};
bool intState[INT_CHANNELS] = {false};
bool modbusOk = false;
bool linkUp   = false;

unsigned long lastReport = 0;

// --- relé -------------------------------------------------------------------

// Externý modul si stav pamätá sám, preto ho čítame, nepredpokladáme.
bool readExt() {
  if (!ModbusRTUClient.requestFrom(SLAVE_ID, COILS, 0, EXT_CHANNELS)) {
    modbusOk = false;
    return false;
  }
  for (int i = 0; i < EXT_CHANNELS; i++) extState[i] = ModbusRTUClient.read() != 0;
  modbusOk = true;
  return true;
}

void setExt(int ch, bool on) {                 // ch = 0..7
  modbusOk = ModbusRTUClient.coilWrite(SLAVE_ID, ch, on ? 1 : 0);
  if (modbusOk) extState[ch] = on;
}

void setInt(int ch, bool on) {                 // ch = 0..3
  digitalWrite(INT_PINS[ch], on ? HIGH : LOW);
  digitalWrite(INT_LEDS[ch], on ? HIGH : LOW);
  intState[ch] = on;
}

void printState() {
  Serial.print("interne  ");
  for (int i = 0; i < INT_CHANNELS; i++) {
    Serial.print('R'); Serial.print(i + 1);
    Serial.print(intState[i] ? ":ZAP " : ":vyp ");
  }
  Serial.print("\nexterne  ");
  for (int i = 0; i < EXT_CHANNELS; i++) {
    Serial.print('X'); Serial.print(i + 1);
    Serial.print(extState[i] ? ":ZAP " : ":vyp ");
  }
  Serial.println();
}

// --- hlásenie na server -----------------------------------------------------

String buildJson() {
  String s = "{\"device\":\"";
  s += DEVICE_NAME;
  s += "\",\"modbusOk\":";
  s += modbusOk ? "true" : "false";
  s += ",\"ext\":[";
  for (int i = 0; i < EXT_CHANNELS; i++) { if (i) s += ','; s += extState[i] ? '1' : '0'; }
  s += "],\"int\":[";
  for (int i = 0; i < INT_CHANNELS; i++) { if (i) s += ','; s += intState[i] ? '1' : '0'; }
  s += "]}";
  return s;
}

void report() {
  if (!linkUp) return;

  readExt();                       // pravda je vždy z modulu, nie z pamäte
  String body = buildJson();

  // Oba typy klientov dedia z Client, tak si vyberieme až za behu
  EthernetClient plain;
  EthernetSSLClient secure;
  Client* client = USE_HTTPS ? (Client*)&secure : (Client*)&plain;

  client->setTimeout(3000);

  if (!client->connect(SERVER_HOST, SERVER_PORT)) {
    Serial.print("server nedostupny: ");
    Serial.print(SERVER_HOST); Serial.print(':'); Serial.println(SERVER_PORT);
    return;
  }

  client->print("POST "); client->print(REPORT_PATH); client->println(" HTTP/1.1");
  client->print("Host: "); client->print(SERVER_HOST);
  client->print(':'); client->println(SERVER_PORT);
  client->print("X-Device-Token: "); client->println(DEVICE_TOKEN);
  client->println("Content-Type: application/json");
  client->print("Content-Length: "); client->println(body.length());
  client->println("Connection: close");
  client->println();
  client->print(body);

  // Zaujíma nás len stavový riadok: "HTTP/1.1 200 OK"
  String status = client->readStringUntil('\n');
  client->stop();

  if (status.indexOf("200") > 0) {
    Serial.println("hlasenie OK");
  } else {
    Serial.print("server odmietol: ");
    Serial.println(status.length() ? status : "(bez odpovede)");
    if (status.indexOf("401") > 0) Serial.println("  -> nesedi DEVICE_TOKEN");
  }
}

// --- setup / loop -----------------------------------------------------------

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  for (int i = 0; i < INT_CHANNELS; i++) {
    pinMode(INT_PINS[i], OUTPUT);
    pinMode(INT_LEDS[i], OUTPUT);
    setInt(i, false);
  }

  // Post-delay musí prekryť celý posledný znak (pri 9600 = 1042 us),
  // inak sa orežú koncové bity rámca. Podrobne v KOMUNIKACIA.md.
  RS485.setDelays(500, 1500);
  ModbusRTUClient.begin(MB_BAUD, SERIAL_8N1);

  Serial.println("\n=== Opta – hlasenie stavov rele ===");
  Serial.print("RS485 modul: ");
  Serial.println(readExt() ? "odpoveda" : "NEODPOVEDA");

  Serial.print("Ethernet: ");
  linkUp = USE_STATIC_IP ? Ethernet.begin(STATIC_IP, DNS_SRV, GATEWAY, SUBNET)
                         : Ethernet.begin();
  if (linkUp) {
    Serial.print("IP ");
    Serial.println(Ethernet.localIP());
    Serial.print("hlasim na http://");
    Serial.print(SERVER_HOST); Serial.print(':'); Serial.print(SERVER_PORT);
    Serial.print(REPORT_PATH);
    Serial.print(" kazdych "); Serial.print(REPORT_EVERY_MS / 1000);
    Serial.println(" s");
  } else {
    Serial.println("CHYBA – kabel? DHCP? (skus USE_STATIC_IP = true)");
  }

  printState();
  Serial.println("\n1-8 externe, q w e r interne, s stav, p posli hned");
}

void loop() {
  if (millis() - lastReport >= REPORT_EVERY_MS) {
    lastReport = millis();
    report();
  }

  if (!Serial.available()) return;
  char c = Serial.read();

  if (c >= '1' && c <= '8') {
    int ch = c - '1';
    readExt();
    setExt(ch, !extState[ch]);
    Serial.print("X"); Serial.print(ch + 1);
    Serial.println(extState[ch] ? " ZAP" : " VYP");

  } else if (c == 'q' || c == 'w' || c == 'e' || c == 'r') {
    int ch = (c == 'q') ? 0 : (c == 'w') ? 1 : (c == 'e') ? 2 : 3;
    setInt(ch, !intState[ch]);
    Serial.print("R"); Serial.print(ch + 1);
    Serial.println(intState[ch] ? " ZAP" : " VYP");

  } else if (c == 's') {
    readExt();
    printState();

  } else if (c == 'p') {
    report();
  }
}
