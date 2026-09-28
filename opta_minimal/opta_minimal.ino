// ============================================================================
//  JEDNODUCHY TEST – Arduino Opta WiFi + Waveshare Modbus RTU Relay 8CH
//
//  Po starte sam postupne prepina rele 1..8 (kazdu sekundu jedno), aby bolo
//  hned pocut, ci komunikacia ide. Ako len nieco napises, prepne sa do
//  rucneho rezimu.
//
//  Prikazy v Serial Monitore (staci jeden znak, Enter netreba):
//     1..8   prepne stav daneho rele (toggle)
//     0      vypne vsetky
//     a      zapne vsetky
//     w      zapne/vypne automaticke prepinanie dokola
//     s      vypise stav nacitany priamo z modulu
//
//  Kniznice: ArduinoRS485, ArduinoModbus
//  Doska:    Arduino Mbed OS Opta Boards -> Opta
//  Monitor:  115200 baud
//
//  Zapojenie:  Opta A -> modul A,  Opta B -> modul B,  Opta COM -> modul GND
//              modul musi mat vlastne napajanie (zakladna verzia 5 V, B/D 7-36 V)
// ============================================================================

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

const int  SLAVE_ID = 1;      // Waveshare ma z vyroby adresu 1
const int  CHANNELS = 8;      // 8-kanalovy modul
const long BAUD     = 9600;   // Waveshare ma z vyroby 9600 8N1
const int  WALK_MS  = 1000;   // ako rychlo bezi automaticke prepinanie

bool state[CHANNELS] = {false};   // co sme naposledy nastavili
bool walking = true;              // bezi automaticke prepinanie?
int  current = 0;                 // ktore rele je zapnute pri automatike
unsigned long lastStep = 0;

// ---------------------------------------------------------------------------
//  Modbus
// ---------------------------------------------------------------------------

// Zapise jeden coil. channel = 1..8. Vypise vysledok, vrati true pri uspechu.
bool setRelay(int channel, bool on) {
  bool ok = ModbusRTUClient.coilWrite(SLAVE_ID, channel - 1, on ? 1 : 0);

  Serial.print("rele ");
  Serial.print(channel);
  Serial.print(on ? " ZAP  -> " : " VYP  -> ");

  if (ok) {
    state[channel - 1] = on;
    Serial.println("OK");
  } else {
    Serial.print("CHYBA: ");
    const char* err = ModbusRTUClient.lastError();
    Serial.println(err ? err : "(neznama)");
  }
  return ok;
}

// Waveshare: coil 0x00FF ovlada naraz vsetky rele
bool setAll(bool on) {
  bool ok = ModbusRTUClient.coilWrite(SLAVE_ID, 0x00FF, on ? 1 : 0);

  Serial.print(on ? "vsetky ZAP -> " : "vsetky VYP -> ");
  if (ok) {
    for (int i = 0; i < CHANNELS; i++) state[i] = on;
    Serial.println("OK");
  } else {
    Serial.print("CHYBA: ");
    const char* err = ModbusRTUClient.lastError();
    Serial.println(err ? err : "(neznama)");
  }
  return ok;
}

// Nacita skutocny stav priamo z modulu (nie to, co sme si pamatali)
void printState() {
  if (!ModbusRTUClient.requestFrom(SLAVE_ID, COILS, 0, CHANNELS)) {
    Serial.print("citanie stavu CHYBA: ");
    const char* err = ModbusRTUClient.lastError();
    Serial.println(err ? err : "(neznama)");
    return;
  }
  Serial.print("stav z modulu: ");
  for (int i = 0; i < CHANNELS; i++) {
    bool on = ModbusRTUClient.read() != 0;
    state[i] = on;
    Serial.print(i + 1);
    Serial.print(on ? ":ZAP " : ":vyp ");
  }
  Serial.println();
}

// ---------------------------------------------------------------------------
//  Prikazy
// ---------------------------------------------------------------------------
void handleChar(char c) {
  if (c >= '1' && c <= '0' + CHANNELS) {
    if (walking) {                       // prvy prikaz zastavi automatiku
      walking = false;
      Serial.println("-- automatika VYP, si v rucnom rezime --");
    }
    int ch = c - '0';
    setRelay(ch, !state[ch - 1]);        // toggle

  } else if (c == '0') {
    walking = false;
    setAll(false);

  } else if (c == 'a') {
    walking = false;
    setAll(true);

  } else if (c == 's') {
    printState();

  } else if (c == 'w') {
    walking = !walking;
    Serial.println(walking ? "-- automatika ZAP --" : "-- automatika VYP --");
    if (walking) lastStep = 0;           // nech sa pohne hned

  } else if (c == '\n' || c == '\r' || c == ' ') {
    // ignoruj

  } else {
    Serial.println("? prikazy: 1-8 = prepni rele, 0 = vsetky vyp, "
                   "a = vsetky zap, s = stav, w = automatika");
  }
}

// ---------------------------------------------------------------------------
//  Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}

  Serial.println("\n=== Opta + Waveshare 8CH ===");

  // Oneskorenia okolo vysielania. Namerane (opta_rs485_tune) pri 9600 8N1:
  //   post 0 / 50 / 200 us -> modul vobec neodpovie (odrezany koniec ramca)
  //   post 800 us          -> funguje
  //   post 3500 us         -> chyby CRC (Opta drzi linku, ked modul odpoveda)
  // Vzorec z oficialneho Arduino prikladu dava 3500 us a s tymto modulom
  // NEFUNGUJE. Ak zmenis BAUD, over hodnoty sketchom opta_rs485_tune.
  RS485.setDelays(500, 800);

  if (!ModbusRTUClient.begin(BAUD, SERIAL_8N1)) {
    Serial.println("CHYBA: Modbus sa nepodarilo spustit – stop.");
    while (true) delay(1000);
  }
  ModbusRTUClient.setTimeout(500);

  Serial.print("Modbus bezi: ");
  Serial.print(BAUD);
  Serial.print(" 8N1, modul ID ");
  Serial.println(SLAVE_ID);
  Serial.println("Prikazy: 1-8 = prepni rele, 0 = vsetky vyp, a = vsetky zap,");
  Serial.println("         s = stav z modulu, w = automatika");
  Serial.println("Teraz bezi automatika – napis cokolvek a zastavi sa.\n");

  setAll(false);
}

void loop() {
  while (Serial.available()) {
    handleChar(Serial.read());
  }

  if (walking && millis() - lastStep >= WALK_MS) {
    lastStep = millis();

    if (current > 0) setRelay(current, false);   // vypni predchadzajuce
    current = (current % CHANNELS) + 1;          // 1,2,...,8,1,2,...
    setRelay(current, true);

    if (current == CHANNELS) Serial.println();   // prazdny riadok po kole
  }
}
