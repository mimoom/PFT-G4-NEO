// ============================================================================
//  Arduino Opta – mapovanie a test relé / vstupov cez Serial Monitor
//
//  - 4 interné relé Opty (R1..R4)
//  - 8 vstupov Opty (I1..I8 = A0..A7)
//  - Waveshare Modbus RTU Relay na RS485, kanály X1..Xn
//
//  Knižnice (Library Manager): ArduinoRS485, ArduinoModbus
//  Doska: Arduino Mbed OS Opta Boards -> Opta
//  Serial Monitor: 115200 baud, koniec riadku "Newline" (alebo "Both NL & CR")
//
//  Napíš `help` pre zoznam príkazov.
//
//  Všetky príkazy idú cez handleCommand(line, out) – neskôr ten istý
//  handler použijeme pre sieťové rozhranie (HTTP / MQTT).
// ============================================================================

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>
#include "config.h"

// ---------------------------------------------------------------------------
//  Hardvér Opty
// ---------------------------------------------------------------------------
const int RELAY_PINS[4]  = {D0, D1, D2, D3};
const int RELAY_LEDS[4]  = {LED_D0, LED_D1, LED_D2, LED_D3};
const int INPUT_PINS[8]  = {A0, A1, A2, A3, A4, A5, A6, A7};

const int EXT_NAMES_COUNT = sizeof(EXT_RELAY_NAMES) / sizeof(EXT_RELAY_NAMES[0]);

// ---------------------------------------------------------------------------
//  Stav
// ---------------------------------------------------------------------------
int extCount = RELAY_MODULE_COUNT;   // `probe` alebo `count <n>` to zmení za behu
bool intState[4] = {false, false, false, false};
bool extState[MAX_EXT_CHANNELS] = {false};
bool extKnown = false;          // či extState zodpovedá realite (po úspešnom čítaní/zápise)

bool  inState[8] = {false};
float inVolt[8]  = {0};
bool  btnState  = false;

bool watchMode = false;
unsigned long lastWatch = 0;

unsigned long mbBaud = MODBUS_BAUD;
uint16_t mbConfig = MODBUS_SERIAL_CONFIG;
int  mbId = RELAY_MODULE_ID;
bool mbStarted = false;

char lineBuf[96];
size_t lineLen = 0;

// ---------------------------------------------------------------------------
//  Pomocné
// ---------------------------------------------------------------------------
const char* extName(int i) {  // i = 0-based
  return (i < EXT_NAMES_COUNT) ? EXT_RELAY_NAMES[i] : "?";
}

const char* onOff(bool v) { return v ? "ON " : "OFF"; }

bool isTimeoutError() {
  const char* e = ModbusRTUClient.lastError();
  return e == nullptr || strstr(e, "imed out") != nullptr;
}

void printMbError(Print& out) {
  out.print(F("  ! Modbus chyba: "));
  const char* e = ModbusRTUClient.lastError();
  out.println(e ? e : "(nezname)");
}

// Preruší blokujúcu akciu (walk/scan), ak používateľ niečo pošle
bool abortRequested() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    return true;
  }
  return false;
}

// Čakanie, ktoré sa dá prerušiť; vráti true pri prerušení
bool waitOrAbort(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    if (abortRequested()) return true;
    delay(5);
  }
  return false;
}

// ---------------------------------------------------------------------------
//  Modbus
// ---------------------------------------------------------------------------
// Oneskorenia okolo vysielania – hodnoty namerané, viď komentár v config.h.
// (Vzorec z oficiálneho Arduino príkladu dá pri 9600 až 3500 us a s týmto
// modulom nefunguje – Opta drží linku, keď už modul odpovedá.)
void applyDelays(unsigned long baud) {
  int post = RS485_POST_DELAY_US;
  // Pomalšie linky potrebujú dlhší chvost: dolná hranica rastie s dĺžkou bitu.
  if (baud < 9600) post = (int)((float)RS485_POST_DELAY_US * 9600.0f / baud);
  RS485.setDelays(RS485_PRE_DELAY_US, post);
}

const char* configName(uint16_t cfg) {
  if (cfg == SERIAL_8E1) return "8E1";
  if (cfg == SERIAL_8O1) return "8O1";
  if (cfg == SERIAL_8N2) return "8N2";
  return "8N1";
}

bool modbusBegin(unsigned long baud, uint16_t cfg) {
  if (mbStarted) {
    ModbusRTUClient.end();
    mbStarted = false;
  }
  applyDelays(baud);

  if (!ModbusRTUClient.begin(baud, cfg)) {
    return false;
  }
  mbConfig = cfg;
  ModbusRTUClient.setTimeout(MODBUS_TIMEOUT_MS);
  mbBaud = baud;
  mbStarted = true;
  extKnown = false;
  return true;
}

bool modbusBegin(unsigned long baud) { return modbusBegin(baud, mbConfig); }

bool extWrite(int ch, bool on, Print& out) {  // ch = 1-based
  int addr = RELAY_COIL_OFFSET + ch - 1;
  if (!ModbusRTUClient.coilWrite(mbId, addr, on ? 1 : 0)) {
    out.print(F("  ! X")); out.print(ch); out.println(F(": zapis zlyhal"));
    printMbError(out);
    return false;
  }
  extState[ch - 1] = on;
  return true;
}

bool extReadAll(Print& out) {
  if (!ModbusRTUClient.requestFrom(mbId, COILS, RELAY_COIL_OFFSET, extCount)) {
    out.println(F("  ! Citanie stavu externeho modulu zlyhalo"));
    printMbError(out);
    if (!isTimeoutError()) {
      out.println(F("    (modul odpovedal chybou – mozno ma menej kanalov, skus `probe`)"));
    }
    extKnown = false;
    return false;
  }
  for (int i = 0; i < extCount; i++) {
    extState[i] = ModbusRTUClient.read() != 0;
  }
  extKnown = true;
  return true;
}

// Waveshare: coil 0x00FF ovláda naraz všetky relé (jeden rámec namiesto N)
bool extWriteAll(bool on, Print& out) {
  if (!ModbusRTUClient.coilWrite(mbId, WS_COIL_ALL, on ? 1 : 0)) {
    out.println(F("  ! Hromadny zapis (coil 0x00FF) zlyhal"));
    printMbError(out);
    extKnown = false;
    return false;
  }
  for (int i = 0; i < MAX_EXT_CHANNELS; i++) extState[i] = on;
  extKnown = true;
  return true;
}

// Koľko coilov modul naozaj má: posledný počet, ktorý sa dá prečítať naraz
int extProbeCount() {
  int last = 0;
  for (int n = 1; n <= MAX_EXT_CHANNELS; n++) {
    if (ModbusRTUClient.requestFrom(mbId, COILS, RELAY_COIL_OFFSET, n)) {
      while (ModbusRTUClient.available()) ModbusRTUClient.read();
      last = n;
    } else if (!isTimeoutError()) {
      break;             // modul povedal "neplatna adresa" – sme za koncom
    } else if (last) {
      break;             // prestal odpovedať, ale predtým odpovedal
    }
    delay(5);
  }
  return last;
}

bool extReadReg(uint16_t reg, long* value) {
  if (!ModbusRTUClient.requestFrom(mbId, HOLDING_REGISTERS, reg, 1)) return false;
  *value = ModbusRTUClient.read();
  return true;
}

// ---------------------------------------------------------------------------
//  Interné relé
// ---------------------------------------------------------------------------
void intWrite(int ch, bool on) {  // ch = 1-based
  digitalWrite(RELAY_PINS[ch - 1], on ? HIGH : LOW);
  digitalWrite(RELAY_LEDS[ch - 1], on ? HIGH : LOW);
  intState[ch - 1] = on;
}

// ---------------------------------------------------------------------------
//  Vstupy
// ---------------------------------------------------------------------------
float readInputVoltage(int i) {
  int raw = analogRead(INPUT_PINS[i]);
  // Škálovanie podľa Arduino dokumentácie Opty (12-bit ADC, delič 0.3034)
  return raw * (3.249f / 4095.0f) / 0.3034f;
}

// Vráti bitovú masku vstupov, ktoré zmenili stav (bit 8 = tlačidlo USER)
uint16_t updateInputs() {
  uint16_t changed = 0;
  for (int i = 0; i < 8; i++) {
    float v = readInputVoltage(i);
    inVolt[i] = v;
    bool s = inState[i];
    if (!s && v >= INPUT_ON_V) s = true;
    else if (s && v <= INPUT_OFF_V) s = false;
    if (s != inState[i]) {
      inState[i] = s;
      changed |= (1 << i);
    }
  }
#ifdef BTN_USER
  bool b = digitalRead(BTN_USER) == LOW;  // stlačené = LOW
  if (b != btnState) {
    btnState = b;
    changed |= (1 << 8);
  }
#endif
  return changed;
}

// ---------------------------------------------------------------------------
//  Výpisy
// ---------------------------------------------------------------------------
void printInternal(Print& out) {
  out.println(F("Interne rele (Opta):"));
  for (int i = 0; i < 4; i++) {
    out.print(F("  R")); out.print(i + 1); out.print(F("  "));
    out.print(onOff(intState[i])); out.print(F("  "));
    out.println(INTERNAL_RELAY_NAMES[i]);
  }
}

void printExternal(Print& out, bool refresh) {
  out.print(F("Externy modul (ID ")); out.print(mbId);
  out.print(F(", ")); out.print(mbBaud); out.println(F(" baud):"));
  if (refresh) extReadAll(out);
  for (int i = 0; i < extCount; i++) {
    out.print(F("  X")); out.print(i + 1); out.print(i + 1 < 10 ? F("  ") : F(" "));
    out.print(extKnown ? onOff(extState[i]) : "???"); out.print(F("  "));
    out.println(extName(i));
  }
}

void printInputs(Print& out) {
  updateInputs();
  out.println(F("Vstupy (Opta):"));
  for (int i = 0; i < 8; i++) {
    out.print(F("  I")); out.print(i + 1); out.print(F("  "));
    out.print(onOff(inState[i])); out.print(F("  "));
    out.print(inVolt[i], 2); out.print(F(" V  "));
    out.println(INPUT_NAMES[i]);
  }
#ifdef BTN_USER
  out.print(F("  USER tlacidlo: ")); out.println(btnState ? F("stlacene") : F("uvolnene"));
#endif

#if WS_DIGITAL_INPUTS > 0
  // Varianta Modbus RTU Relay (D) má aj digitálne vstupy (FC02 od adresy 0)
  out.println(F("Vstupy (Waveshare modul):"));
  if (ModbusRTUClient.requestFrom(mbId, DISCRETE_INPUTS, 0, WS_DIGITAL_INPUTS)) {
    for (int i = 0; i < WS_DIGITAL_INPUTS; i++) {
      out.print(F("  DI")); out.print(i + 1); out.print(F("  "));
      out.println(onOff(ModbusRTUClient.read() != 0));
    }
  } else {
    out.println(F("  ! Citanie zlyhalo (ma tvoj modul vstupy? WS_DIGITAL_INPUTS)"));
    printMbError(out);
  }
#endif
}

void printHelp(Print& out) {
  out.println(F(
    "\n=== Opta relay test – prikazy ===\n"
    "  help                      tento zoznam\n"
    "  status                    stav vsetkeho (interne, externe, vstupy)\n"
    "\n"
    "  r <1-4|all> <on|off|t>    interne rele Opty (t = prepni)\n"
    "  r <1-4> p [ms]            impulz (default 500 ms)\n"
    "  x <1-N|all> <on|off|t>    externe rele na Waveshare module\n"
    "  x <1-N> p [ms]            impulz\n"
    "  x                         precitaj stav externeho modulu\n"
    "  off                       VSETKO vypnut (interne aj externe)\n"
    "\n"
    "  in                        vypis vstupov (stav + napatie)\n"
    "  watch                     zap/vyp priebezny vypis zmien vstupov\n"
    "\n"
    "  walk r | walk x           postupne zopne kazde rele (na identifikaciu)\n"
    "                            lubovolny znak = prerusit\n"
    "\n"
    "  KED KOMUNIKACIA NEIDE:\n"
    "  diag                      diagnostika linky + co skontrolovat\n"
    "  raw <hex bajty>           posli surovy ramec, CRC doplni sam\n"
    "  sniff [s]                 pocuvaj linku a vypis surove bajty\n"
    "  scanbaud                  skus rychlosti x parity x adresy 1..16\n"
    "  scan [od] [do]            hladaj adresy na aktualnej rychlosti\n"
    "\n"
    "  probe                     zisti adresu, verziu a pocet kanalov modulu\n"
    "  mb                        aktualne Modbus nastavenia\n"
    "  id <n>                    na akej adrese Opta modul oslovuje\n"
    "  baud <n>                  rychlost RS485 na strane Opty\n"
    "  cfg <8n1|8e1|8o1|8n2>     format ramca na strane Opty\n"
    "  count <n>                 rucne nastav pocet kanalov modulu\n"
    "\n"
    "  Waveshare nastavenia (menia modul natrvalo!):\n"
    "  ws addr                   precitaj adresu ulozenu v module (reg 0x4000)\n"
    "  ws ver                    verzia firmveru (reg 0x8000)\n"
    "  ws setaddr <n>            ZMEN adresu modulu\n"
    "  ws setbaud <n>            ZMEN rychlost modulu (4800..115200)\n"
    "\n"
    "  Surovy Modbus (hodnoty dec alebo 0x hex):\n"
    "  mb rc <id> <adr> [n]      citaj coily          (FC01)\n"
    "  mb ri <id> <adr> [n]      citaj diskretne vst. (FC02)\n"
    "  mb rh <id> <adr> [n]      citaj holding reg.   (FC03)\n"
    "  mb rr <id> <adr> [n]      citaj input reg.     (FC04)\n"
    "  mb wc <id> <adr> <0|1>    zapis coil           (FC05)\n"
    "  mb wh <id> <adr> <val>    zapis holding reg.   (FC06)\n"
  ));
}

// ---------------------------------------------------------------------------
//  Príkazy
// ---------------------------------------------------------------------------
#define MAX_TOKENS 12   // `raw` berie aj celý Modbus rámec po bajtoch

long toNum(const char* s, bool* ok) {
  char* end;
  long v = strtol(s, &end, 0);
  *ok = (s[0] != '\0' && *end == '\0');
  return v;
}

// Spoločná logika pre `r` a `x`
void cmdRelay(char kind, int argc, char** argv, Print& out) {
  bool ext = (kind == 'x');
  int count = ext ? extCount : 4;

  if (ext && argc == 1) {
    printExternal(out, true);
    return;
  }
  if (argc < 3) {
    out.println(F("  Pouzitie: r|x <cislo|all> <on|off|t|p [ms]>"));
    return;
  }

  int from, to;
  if (strcmp(argv[1], "all") == 0) {
    from = 1; to = count;
  } else {
    bool ok;
    long n = toNum(argv[1], &ok);
    if (!ok || n < 1 || n > count) {
      out.print(F("  ! Cislo rele musi byt 1..")); out.println(count);
      return;
    }
    from = to = (int)n;
  }

  const char* act = argv[2];
  bool isPulse = strcmp(act, "p") == 0 || strcmp(act, "pulse") == 0;
  bool isToggle = strcmp(act, "t") == 0 || strcmp(act, "toggle") == 0;
  bool isOn = strcmp(act, "on") == 0 || strcmp(act, "1") == 0;
  bool isOff = strcmp(act, "off") == 0 || strcmp(act, "0") == 0;
  if (!isPulse && !isToggle && !isOn && !isOff) {
    out.println(F("  ! Akcia: on | off | t | p [ms]"));
    return;
  }

  unsigned long pulseMs = 500;
  if (isPulse && argc >= 4) {
    bool ok;
    long ms = toNum(argv[3], &ok);
    if (!ok || ms < 1 || ms > 60000) {
      out.println(F("  ! Dlzka impulzu 1..60000 ms"));
      return;
    }
    pulseMs = ms;
  }

  if (ext && isToggle && !extKnown) extReadAll(out);

  // `x all on|off` vie Waveshare vybaviť jediným rámcom na coile 0x00FF
  if (ext && !isToggle && !isPulse && from == 1 && to == extCount) {
    if (!extWriteAll(isOn, out)) return;
    out.print(F("  X1..X")); out.print(extCount);
    out.print(F(" -> ")); out.println(onOff(isOn));
    return;
  }

  for (int ch = from; ch <= to; ch++) {
    bool cur = ext ? extState[ch - 1] : intState[ch - 1];
    bool target = isOn || isPulse || (isToggle && !cur);
    if (ext) {
      if (!extWrite(ch, target, out)) return;
    } else {
      intWrite(ch, target);
    }
    out.print(F("  ")); out.print(ext ? 'X' : 'R'); out.print(ch);
    out.print(F(" -> ")); out.print(onOff(target)); out.print(F("  "));
    out.println(ext ? extName(ch - 1) : INTERNAL_RELAY_NAMES[ch - 1]);
  }

  if (isPulse) {
    delay(pulseMs);
    for (int ch = from; ch <= to; ch++) {
      if (ext) extWrite(ch, false, out);
      else intWrite(ch, false);
    }
    out.print(F("  impulz ")); out.print(pulseMs); out.println(F(" ms hotovy, OFF"));
  }
}

void allOff(Print& out) {
  for (int ch = 1; ch <= 4; ch++) intWrite(ch, false);
  bool extOk = extWriteAll(false, out);
  out.println(extOk ? F("  Vsetko vypnute.") : F("  Interne vypnute, externy modul neodpoveda."));
}

void cmdWalk(int argc, char** argv, Print& out) {
  if (argc < 2 || (strcmp(argv[1], "r") != 0 && strcmp(argv[1], "x") != 0)) {
    out.println(F("  Pouzitie: walk r | walk x"));
    return;
  }
  bool ext = argv[1][0] == 'x';
  int count = ext ? extCount : 4;
  out.println(F("  Walk start – zapisuj si, co sa zopne. Lubovolny znak = stop."));
  for (int ch = 1; ch <= count; ch++) {
    out.print(F("  >> ")); out.print(ext ? 'X' : 'R'); out.print(ch);
    out.print(F(" ZOPNUTE  (")); out.print(ext ? extName(ch - 1) : INTERNAL_RELAY_NAMES[ch - 1]);
    out.println(F(")"));
    if (ext) { if (!extWrite(ch, true, out)) return; }
    else intWrite(ch, true);

    bool aborted = waitOrAbort(WALK_ON_MS);

    if (ext) extWrite(ch, false, out);
    else intWrite(ch, false);

    if (aborted || waitOrAbort(WALK_GAP_MS)) {
      out.println(F("  Walk preruseny."));
      return;
    }
  }
  out.println(F("  Walk hotovy."));
}

// Vráti true, ak na danej adrese niečo odpovedalo
bool probeId(int id, Print& out, bool verbose) {
  if (ModbusRTUClient.requestFrom(id, COILS, 0, 1)) {
    if (verbose) { out.print(F("  + ID ")); out.print(id); out.println(F(": odpoveda (coily)")); }
    return true;
  }
  bool coilTimeout = isTimeoutError();
  if (ModbusRTUClient.requestFrom(id, HOLDING_REGISTERS, 0, 1)) {
    if (verbose) { out.print(F("  + ID ")); out.print(id); out.println(F(": odpoveda (holding registre)")); }
    return true;
  }
  if (!coilTimeout || !isTimeoutError()) {
    // Niečo prišlo, ale s chybou (výnimka, CRC...) – pravdepodobne zariadenie existuje
    if (verbose) {
      out.print(F("  ? ID ")); out.print(id); out.print(F(": odpoved s chybou: "));
      const char* e = ModbusRTUClient.lastError();
      out.println(e ? e : "?");
    }
    return true;
  }
  return false;
}

void cmdScan(int argc, char** argv, Print& out) {
  int from = 1, to = 247;
  bool ok;
  if (argc >= 2) { long v = toNum(argv[1], &ok); if (ok) from = constrain(v, 1, 247); }
  if (argc >= 3) { long v = toNum(argv[2], &ok); if (ok) to = constrain(v, from, 247); }

  out.print(F("  Skenujem ID ")); out.print(from); out.print(F("..")); out.print(to);
  out.print(F(" @ ")); out.print(mbBaud); out.println(F(" baud (lubovolny znak = stop)"));

  ModbusRTUClient.setTimeout(SCAN_TIMEOUT_MS);
  int found = 0;
  for (int id = from; id <= to; id++) {
    if (abortRequested()) { out.println(F("  Sken preruseny.")); break; }
    if (probeId(id, out, true)) found++;
    delay(5);
  }
  ModbusRTUClient.setTimeout(MODBUS_TIMEOUT_MS);
  out.print(F("  Najdenych: ")); out.println(found);
}

void cmdScanBaud(Print& out) {
  const unsigned long bauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
  const uint16_t configs[] = {SERIAL_8N1, SERIAL_8E1, SERIAL_8O1};
  unsigned long origBaud = mbBaud;
  uint16_t origCfg = mbConfig;

  out.print(F("  Skusam rychlosti x parity, ID 1..")); out.print(SCANBAUD_MAX_ID);
  out.println(F(" (lubovolny znak = stop). Chvilu to potrva."));

  bool aborted = false;
  int hits = 0;
  for (unsigned int c = 0; c < sizeof(configs) / sizeof(configs[0]) && !aborted; c++) {
    for (unsigned int b = 0; b < sizeof(bauds) / sizeof(bauds[0]) && !aborted; b++) {
      if (!modbusBegin(bauds[b], configs[c])) continue;
      ModbusRTUClient.setTimeout(SCAN_TIMEOUT_MS);
      out.print(F("  ")); out.print(bauds[b]); out.print(' ');
      out.print(configName(configs[c])); out.print(F(": "));
      int found = 0;
      for (int id = 1; id <= SCANBAUD_MAX_ID; id++) {
        if (abortRequested()) { aborted = true; break; }
        if (probeId(id, out, false)) {
          out.print(F("ID ")); out.print(id); out.print(F("  "));
          found++;
          hits++;
        }
        delay(5);
      }
      out.println(found ? F("<<< NASIEL") : F("-"));
    }
  }
  modbusBegin(origBaud, origCfg);
  out.print(F("  Obnovene ")); out.print(origBaud); out.print(' ');
  out.println(configName(origCfg));
  if (hits) out.println(F("  Nastav najdene cez `baud <n>`, `cfg <8n1|8e1|8o1>` a `id <n>`."));
  else out.println(F("  Nic sa neozvalo -> sprav `diag`, problem bude v zapojeni."));
}

// ---------------------------------------------------------------------------
//  Nízkoúrovňová diagnostika RS485 (obchádza ArduinoModbus)
//
//  Zmysel: rozlíšiť, či z linky prídu VOBEC nejaké bajty. Ticho = drôty,
//  napájanie alebo adresa. Zmätky = zlý baud/parita. Platný rámec s chybou
//  = komunikácia ide a problém je inde.
// ---------------------------------------------------------------------------
uint16_t modbusCrc(const uint8_t* buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}

void printHexByte(Print& out, uint8_t b) {
  if (b < 0x10) out.print('0');
  out.print((unsigned int)b, HEX);
}

// Prepne port z ArduinoModbus na priamy RS485 prístup
void rawBegin() {
  if (mbStarted) { ModbusRTUClient.end(); mbStarted = false; }
  applyDelays(mbBaud);
  RS485.begin(mbBaud, mbConfig);
  RS485.receive();
}

void rawEnd(Print& out) {
  RS485.noReceive();
  RS485.end();
  if (!modbusBegin(mbBaud, mbConfig)) out.println(F("  ! Modbus sa nepodarilo obnovit"));
}

// Vráti počet prijatých bajtov, vypíše ich v hexe
int rawListen(unsigned long ms, Print& out) {
  uint8_t rx[64];
  int n = 0;
  unsigned long t0 = millis();
  unsigned long lastByte = 0;
  while (millis() - t0 < ms) {
    if (RS485.available()) {
      int c = RS485.read();
      if (c >= 0 && n < (int)sizeof(rx)) rx[n++] = (uint8_t)c;
      lastByte = millis();
    } else if (n && millis() - lastByte > 50) {
      break;  // rámec dojazdil
    }
  }
  if (n == 0) {
    out.println(F("  <ticho – neprisiel ani jeden bajt>"));
    return 0;
  }
  out.print(F("  RX ")); out.print(n); out.print(F(" B: "));
  for (int i = 0; i < n; i++) { printHexByte(out, rx[i]); out.print(' '); }
  out.println();
  if (n >= 4) {
    uint16_t crc = modbusCrc(rx, n - 2);
    uint16_t got = rx[n - 2] | ((uint16_t)rx[n - 1] << 8);
    if (crc == got) {
      out.print(F("  CRC OK – platny ramec od ID ")); out.print(rx[0]);
      if (rx[1] & 0x80) {
        out.print(F(", ale VYNIMKA 0x")); printHexByte(out, rx[2]);
        out.println(F("  (1=zla funkcia, 2=zla adresa registra, 3=zla hodnota)"));
      } else {
        out.print(F(", funkcia 0x")); printHexByte(out, rx[1]); out.println();
      }
    } else {
      out.println(F("  ! CRC nesedi – skor zly baud/parita alebo ruseny signal"));
    }
  }
  return n;
}

// `raw <hex>...` – pošle bajty, CRC doplní sám
void cmdRaw(int argc, char** argv, Print& out) {
  if (argc < 3) {
    out.println(F("  Pouzitie: raw <bajt> <bajt> ...  (hex, CRC sa doplni)"));
    out.println(F("  Napr. `raw 01 01 00 00 00 08` = citaj 8 coilov z ID 1"));
    return;
  }
  uint8_t frame[32];
  int len = 0;
  for (int i = 1; i < argc && len < (int)sizeof(frame) - 2; i++) {
    char* end;
    long v = strtol(argv[i], &end, 16);
    if (*end != '\0' || v < 0 || v > 0xFF) {
      out.print(F("  ! Neplatny bajt: ")); out.println(argv[i]);
      return;
    }
    frame[len++] = (uint8_t)v;
  }
  uint16_t crc = modbusCrc(frame, len);
  frame[len++] = crc & 0xFF;
  frame[len++] = crc >> 8;

  out.print(F("  TX: "));
  for (int i = 0; i < len; i++) { printHexByte(out, frame[i]); out.print(' '); }
  out.println();

  rawBegin();
  RS485.beginTransmission();
  RS485.write(frame, len);
  RS485.endTransmission();
  RS485.receive();
  rawListen(1000, out);
  rawEnd(out);
}

// `sniff [s]` – iba počúva, či na linke niečo je
void cmdSniff(int argc, char** argv, Print& out) {
  unsigned long secs = 5;
  if (argc >= 2) {
    bool ok;
    long v = toNum(argv[1], &ok);
    if (ok && v >= 1 && v <= 60) secs = v;
  }
  out.print(F("  Pocuvam ")); out.print(secs);
  out.print(F(" s @ ")); out.print(mbBaud); out.print(' ');
  out.println(configName(mbConfig));
  rawBegin();
  int n = rawListen(secs * 1000, out);
  rawEnd(out);
  if (n == 0) out.println(F("  (na linke nikto nevysiela – normalne, ak je modul slave)"));
}

// Poskladá diagnostiku dokopy
void cmdDiag(Print& out) {
  out.println(F("\n--- Diagnostika RS485 ---"));
  out.print(F("Opta vysiela @ ")); out.print(mbBaud); out.print(' ');
  out.print(configName(mbConfig)); out.print(F(", oslovuje ID ")); out.println(mbId);

  // 1. Skúsime surový rámec "čítaj 1 coil" a pozrieme sa, či niečo príde
  uint8_t frame[8] = {(uint8_t)mbId, 0x01, 0x00, 0x00, 0x00, 0x01};
  uint16_t crc = modbusCrc(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = crc >> 8;

  out.println(F("\n1) Surovy dotaz (FC01, 1 coil):"));
  out.print(F("  TX: "));
  for (int i = 0; i < 8; i++) { printHexByte(out, frame[i]); out.print(' '); }
  out.println();

  rawBegin();
  RS485.beginTransmission();
  RS485.write(frame, 8);
  RS485.endTransmission();
  RS485.receive();
  int n = rawListen(1000, out);
  rawEnd(out);

  out.println(F("\n2) Co s tym:"));
  if (n == 0) {
    out.println(F("  Ticho. Fyzicka vrstva alebo adresa. Skontroluj po rade:"));
    out.println(F("   - ma tvoja Opta vobec RS485? Opta Lite ho NEMA (len RS485/WiFi verzia)"));
    out.println(F("   - je modul napajany? (zakladna verzia 5 V, verzie B/D 7-36 V)"));
    out.println(F("   - spolocna zem: Opta COM <-> GND modulu (bez nej to casto nejde)"));
    out.println(F("   - PREHOD A a B – najcastejsia pricina"));
    out.println(F("   - `scanbaud` prejde rychlosti aj parity a vsetky adresy"));
  } else {
    out.println(F("  Nieco prislo – fyzicka vrstva FUNGUJE."));
    out.println(F("  Ak CRC nesedi, sprav `scanbaud` (zly baud alebo parita)."));
    out.println(F("  Ak je to vynimka 0x02, modul ma iny rozsah adries – skus `probe`."));
  }
  out.println();
}

// ---------------------------------------------------------------------------
//  Waveshare-špecifické príkazy
// ---------------------------------------------------------------------------
void cmdProbe(Print& out) {
  out.print(F("  Skusam modul na ID ")); out.print(mbId);
  out.print(F(" @ ")); out.print(mbBaud); out.println(F(" baud..."));

  long v;
  if (extReadReg(WS_REG_DEVICE_ADDR, &v)) {
    out.print(F("  Modbus adresa v module (reg 0x4000): ")); out.println(v);
    if (v != mbId) out.println(F("  ! Nesedi s nastavenym ID – oprav cez `id <n>`"));
  } else {
    out.println(F("  Register 0x4000 sa necita (nevadi, nie kazdy kus ho ma)"));
  }
  if (extReadReg(WS_REG_VERSION, &v)) {
    out.print(F("  Verzia firmveru (reg 0x8000): ")); out.println(v);
  }

  int n = extProbeCount();
  if (n == 0) {
    out.println(F("  ! Modul neodpoveda. Skus `scan` / `scanbaud`, prehod A a B."));
    return;
  }
  out.print(F("  Pocet kanalov (coilov): ")); out.println(n);
  if (n != extCount) {
    out.print(F("  Menim nastaveny pocet z ")); out.print(extCount);
    out.print(F(" na ")); out.println(n);
    out.println(F("  (natrvalo: RELAY_MODULE_COUNT v config.h)"));
    extCount = n;
  }
  extReadAll(out);
}

void cmdWs(int argc, char** argv, Print& out) {
  if (argc < 2) {
    out.println(F("  Pouzitie: ws addr | ws ver | ws setaddr <n> | ws setbaud <n>"));
    return;
  }
  const char* op = argv[1];
  long v;

  if (strcmp(op, "addr") == 0) {
    if (extReadReg(WS_REG_DEVICE_ADDR, &v)) { out.print(F("  Adresa modulu: ")); out.println(v); }
    else printMbError(out);

  } else if (strcmp(op, "ver") == 0) {
    if (extReadReg(WS_REG_VERSION, &v)) { out.print(F("  Verzia: ")); out.println(v); }
    else printMbError(out);

  } else if (strcmp(op, "setaddr") == 0) {
    bool ok;
    long n = (argc >= 3) ? toNum(argv[2], &ok) : (ok = false, 0);
    if (!ok || n < 1 || n > 247) { out.println(F("  ! adresa 1..247")); return; }
    if (ModbusRTUClient.holdingRegisterWrite(mbId, WS_REG_DEVICE_ADDR, (uint16_t)n)) {
      mbId = n;
      extKnown = false;
      out.print(F("  Modul ma teraz adresu ")); out.println(n);
      out.println(F("  (zapis aj do RELAY_MODULE_ID v config.h)"));
    } else printMbError(out);

  } else if (strcmp(op, "setbaud") == 0) {
    const unsigned long table[] = {4800, 9600, 19200, 38400, 57600, 115200};
    bool ok;
    long n = (argc >= 3) ? toNum(argv[2], &ok) : (ok = false, 0);
    int idx = -1;
    for (unsigned int i = 0; ok && i < sizeof(table) / sizeof(table[0]); i++) {
      if ((long)table[i] == n) idx = i;
    }
    if (idx < 0) {
      out.println(F("  ! Modul podporuje: 4800 9600 19200 38400 57600 115200"));
      return;
    }
    if (ModbusRTUClient.holdingRegisterWrite(mbId, WS_REG_BAUD, (uint16_t)idx)) {
      out.print(F("  Modul prepnuty na ")); out.print(n); out.println(F(" baud."));
      out.println(F("  Prepinam aj Optu..."));
      delay(200);
      if (modbusBegin(n)) out.println(F("  OK – over cez `x` alebo `probe`."));
      else out.println(F("  ! Optu sa nepodarilo prepnut"));
      out.println(F("  (zapis aj do MODBUS_BAUD v config.h)"));
    } else printMbError(out);

  } else {
    out.println(F("  ! Nezname: addr | ver | setaddr | setbaud"));
  }
}

void cmdMb(int argc, char** argv, Print& out) {
  if (argc == 1) {
    out.print(F("  Modbus: ID ")); out.print(mbId);
    out.print(F(", ")); out.print(mbBaud); out.print(' ');
    out.print(configName(mbConfig)); out.print(F(", kanalov "));
    out.print(extCount); out.print(F(", coil offset ")); out.println(RELAY_COIL_OFFSET);
    return;
  }
  if (argc < 4) {
    out.println(F("  Pouzitie: mb <rc|ri|rh|rr|wc|wh> <id> <adr> [n|hodnota]"));
    return;
  }
  bool ok1, ok2, ok3 = true;
  long id = toNum(argv[2], &ok1);
  long addr = toNum(argv[3], &ok2);
  long val = (argc >= 5) ? toNum(argv[4], &ok3) : 1;
  if (!ok1 || !ok2 || !ok3 || id < 0 || id > 247 || addr < 0 || addr > 0xFFFF) {
    out.println(F("  ! Neplatne cislo"));
    return;
  }

  const char* op = argv[1];
  if (op[0] == 'r') {
    int type;
    switch (op[1]) {
      case 'c': type = COILS; break;
      case 'i': type = DISCRETE_INPUTS; break;
      case 'h': type = HOLDING_REGISTERS; break;
      case 'r': type = INPUT_REGISTERS; break;
      default: out.println(F("  ! Nezname: rc|ri|rh|rr")); return;
    }
    if (val < 1 || val > 64) { out.println(F("  ! n = 1..64")); return; }
    int n = ModbusRTUClient.requestFrom(id, type, addr, val);
    if (!n) { printMbError(out); return; }
    for (int i = 0; i < n; i++) {
      long v = ModbusRTUClient.read();
      out.print(F("  [")); out.print(addr + i); out.print(F("] = ")); out.print(v);
      if (type == HOLDING_REGISTERS || type == INPUT_REGISTERS) {
        out.print(F("  (0x")); out.print(v, HEX); out.print(F(")"));
      }
      out.println();
    }
  } else if (strcmp(op, "wc") == 0) {
    if (argc < 5) { out.println(F("  ! Chyba hodnota 0|1")); return; }
    if (ModbusRTUClient.coilWrite(id, addr, val ? 1 : 0)) out.println(F("  OK"));
    else printMbError(out);
  } else if (strcmp(op, "wh") == 0) {
    if (argc < 5) { out.println(F("  ! Chyba hodnota")); return; }
    if (ModbusRTUClient.holdingRegisterWrite(id, addr, (uint16_t)val)) out.println(F("  OK"));
    else printMbError(out);
  } else {
    out.println(F("  ! Nezname: rc|ri|rh|rr|wc|wh"));
  }
}

void handleCommand(char* line, Print& out) {
  // na malé písmená
  for (char* p = line; *p; p++) *p = tolower(*p);

  char* argv[MAX_TOKENS];
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < MAX_TOKENS; tok = strtok(nullptr, " \t")) {
    argv[argc++] = tok;
  }
  if (argc == 0) return;

  const char* c = argv[0];
  if (strcmp(c, "help") == 0 || strcmp(c, "?") == 0) {
    printHelp(out);
  } else if (strcmp(c, "status") == 0 || strcmp(c, "s") == 0) {
    printInternal(out);
    printExternal(out, true);
    printInputs(out);
  } else if (strcmp(c, "r") == 0 || strcmp(c, "x") == 0) {
    cmdRelay(c[0], argc, argv, out);
  } else if (strcmp(c, "off") == 0) {
    allOff(out);
  } else if (strcmp(c, "in") == 0) {
    printInputs(out);
  } else if (strcmp(c, "watch") == 0 || strcmp(c, "w") == 0) {
    watchMode = !watchMode;
    if (watchMode) updateInputs();  // aby prvá zmena nebola falošná
    out.println(watchMode ? F("  Watch ZAP – vypisujem zmeny vstupov") : F("  Watch VYP"));
  } else if (strcmp(c, "walk") == 0) {
    cmdWalk(argc, argv, out);
  } else if (strcmp(c, "mb") == 0) {
    cmdMb(argc, argv, out);
  } else if (strcmp(c, "probe") == 0) {
    cmdProbe(out);
  } else if (strcmp(c, "diag") == 0) {
    cmdDiag(out);
  } else if (strcmp(c, "raw") == 0) {
    cmdRaw(argc, argv, out);
  } else if (strcmp(c, "sniff") == 0) {
    cmdSniff(argc, argv, out);
  } else if (strcmp(c, "cfg") == 0) {
    uint16_t cfg;
    if (argc < 2) { out.print(F("  Aktualne: ")); out.println(configName(mbConfig)); return; }
    if (strcmp(argv[1], "8n1") == 0) cfg = SERIAL_8N1;
    else if (strcmp(argv[1], "8e1") == 0) cfg = SERIAL_8E1;
    else if (strcmp(argv[1], "8o1") == 0) cfg = SERIAL_8O1;
    else if (strcmp(argv[1], "8n2") == 0) cfg = SERIAL_8N2;
    else { out.println(F("  ! cfg 8n1 | 8e1 | 8o1 | 8n2")); return; }
    if (modbusBegin(mbBaud, cfg)) { out.print(F("  Format = ")); out.println(configName(cfg)); }
    else out.println(F("  ! Nepodarilo sa"));
  } else if (strcmp(c, "ws") == 0) {
    cmdWs(argc, argv, out);
  } else if (strcmp(c, "count") == 0) {
    bool ok;
    long v = (argc >= 2) ? toNum(argv[1], &ok) : (ok = false, 0);
    if (!ok || v < 1 || v > MAX_EXT_CHANNELS) {
      out.print(F("  ! count 1..")); out.println(MAX_EXT_CHANNELS);
      return;
    }
    extCount = v;
    extKnown = false;
    out.print(F("  Pocet externych kanalov = ")); out.println(extCount);
  } else if (strcmp(c, "id") == 0) {
    bool ok;
    long v = (argc >= 2) ? toNum(argv[1], &ok) : (ok = false, 0);
    if (!ok || v < 1 || v > 247) { out.println(F("  ! id 1..247")); return; }
    mbId = v;
    extKnown = false;
    out.print(F("  Modbus ID = ")); out.println(mbId);
  } else if (strcmp(c, "baud") == 0) {
    bool ok;
    long v = (argc >= 2) ? toNum(argv[1], &ok) : (ok = false, 0);
    if (!ok || v < 1200 || v > 115200) { out.println(F("  ! baud 1200..115200")); return; }
    if (modbusBegin(v)) { out.print(F("  RS485 = ")); out.print(v); out.println(F(" baud")); }
    else out.println(F("  ! Modbus sa nepodarilo spustit"));
  } else if (strcmp(c, "scan") == 0) {
    cmdScan(argc, argv, out);
  } else if (strcmp(c, "scanbaud") == 0) {
    cmdScanBaud(out);
  } else {
    out.print(F("  ? Neznamy prikaz: ")); out.print(c); out.println(F("  (help)"));
  }
}

// ---------------------------------------------------------------------------
//  Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  for (int i = 0; i < 4; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    pinMode(RELAY_LEDS[i], OUTPUT);
    intWrite(i + 1, false);
  }
#ifdef BTN_USER
  pinMode(BTN_USER, INPUT);
#endif
  analogReadResolution(12);

  Serial.begin(USB_BAUD);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}  // nečakaj donekonečna (beh bez PC)

  Serial.println(F("\n=== Opta relay test ==="));
  if (modbusBegin(mbBaud)) {
    Serial.print(F("Modbus RTU OK: ")); Serial.print(mbBaud);
    Serial.print(' '); Serial.print(configName(mbConfig));
    Serial.print(F(", modul ID ")); Serial.println(mbId);
  } else {
    Serial.println(F("! Modbus RTU sa nepodarilo spustit"));
  }

  // Overenie počtu kanálov – ak modul má iný počet, než je v config.h,
  // radšej sa riadime tým, čo hlási hardvér.
  if (mbStarted) {
    int n = extProbeCount();
    if (n == 0) {
      Serial.println(F("! Modul neodpoveda – napis `diag`"));
    } else if (n != extCount) {
      Serial.print(F("! Modul hlasi ")); Serial.print(n);
      Serial.print(F(" kanalov (config.h ma ")); Serial.print(extCount);
      Serial.println(F(") – pouzivam hodnotu z modulu"));
      extCount = n;
    } else {
      Serial.print(F("Kanalov na module: ")); Serial.println(extCount);
    }
  }

  if (ALL_OFF_ON_BOOT) allOff(Serial);
  updateInputs();
  Serial.println(F("Napis `help` pre zoznam prikazov."));
  Serial.print(F("> "));
}

void loop() {
  // Čítanie riadku zo Serial Monitora
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        Serial.println(lineBuf);
        handleCommand(lineBuf, Serial);
        lineLen = 0;
        Serial.print(F("> "));
      }
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = ch;
    }
  }

  // Priebežné sledovanie vstupov
  if (watchMode && millis() - lastWatch >= WATCH_PERIOD_MS) {
    lastWatch = millis();
    uint16_t changed = updateInputs();
    for (int i = 0; i < 8; i++) {
      if (changed & (1 << i)) {
        Serial.print(F("\n  [")); Serial.print(millis() / 1000.0f, 1); Serial.print(F(" s] I"));
        Serial.print(i + 1); Serial.print(F(" -> ")); Serial.print(onOff(inState[i]));
        Serial.print(F(" (")); Serial.print(inVolt[i], 2); Serial.print(F(" V)  "));
        Serial.println(INPUT_NAMES[i]);
      }
    }
#ifdef BTN_USER
    if (changed & (1 << 8)) {
      Serial.print(F("\n  USER tlacidlo -> ")); Serial.println(btnState ? F("stlacene") : F("uvolnene"));
    }
#endif
  }
}
