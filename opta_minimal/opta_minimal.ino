// ============================================================================
//  Arduino Opta + Waveshare Modbus RTU 8-ch Relay – elementarne ovladanie
//
//  V Serial Monitore:
//     1..8   dane rele zmeni stav
//     0      vypne vsetky
//     s      nacita a vypise skutocny stav z modulu
//
//  Kniznice: ArduinoRS485, ArduinoModbus
//  Doska:    Arduino Mbed OS Opta Boards -> Opta
//  Monitor:  115200 baud
//  Zapojenie: Opta A -> modul A, Opta B -> modul B, Opta COM -> modul GND
// ============================================================================

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

const int SLAVE_ID = 1;       // adresa modulu na zbernici
const int CHANNELS = 8;

// Modul si stav rele pamata sam – prezije reset Opty aj nahratie noveho
// kodu. Preto sa pri starte stav CITA z modulu, nie predpoklada.
// Ak chces, aby sa pri kazdom starte vsetko vyplo, daj sem true.
const bool START_ALL_OFF = false;

bool state[CHANNELS] = {false};   // kopia stavu modulu

// ---------------------------------------------------------------------------

// Nacita skutocny stav vsetkych rele z modulu (Modbus funkcia 01).
bool readState() {
  if (!ModbusRTUClient.requestFrom(SLAVE_ID, COILS, 0, CHANNELS)) return false;
  for (int i = 0; i < CHANNELS; i++) state[i] = ModbusRTUClient.read() != 0;
  return true;
}

void showState() {
  if (!readState()) {
    Serial.print("stav sa neda precitat: ");
    Serial.println(ModbusRTUClient.lastError());
    return;
  }
  Serial.print("stav rele: ");
  for (int i = 0; i < CHANNELS; i++) {
    Serial.print(i + 1);
    Serial.print(state[i] ? ":ZAP " : ":vyp ");
  }
  Serial.println();
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  // Ako dlho po odoslani este drzat vysielac zapnuty.
  // MUSI byt dlhsie nez jeden znak (pri 9600 = 1042 us), lebo flush() sa
  // vrati skor, nez posledny bajt fyzicky odide. Ked DE spadne predcasne,
  // orezu sa posledne bity a modul dostane zly CRC – mlci.
  // Naopak prilis dlho (3500 us) uz prekrici zaciatok jeho odpovede.
  RS485.setDelays(500, 1500);

  ModbusRTUClient.begin(9600, SERIAL_8N1);

  Serial.println("\nStlac 1-8 = prepni rele, 0 = vypni vsetky, s = stav.");

  // Coil 0x00FF je u Waveshare "vsetky rele naraz" – jeden ramec.
  if (START_ALL_OFF) ModbusRTUClient.coilWrite(SLAVE_ID, 0x00FF, 0);

  showState();     // zosynchronizuj sa s realitou, nech toggle sedi hned
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c >= '1' && c <= '8') {
    int ch = c - '1';                 // 0..7
    state[ch] = !state[ch];

    // Zapis jedneho coilu (Modbus funkcia 05). Coil 0 = rele 1.
    bool ok = ModbusRTUClient.coilWrite(SLAVE_ID, ch, state[ch] ? 1 : 0);

    Serial.print("rele ");
    Serial.print(ch + 1);
    Serial.print(state[ch] ? " ZAP  " : " VYP  ");
    Serial.println(ok ? "OK" : ModbusRTUClient.lastError());

    // Zapis zlyhal -> nasa kopia uz nemusi sediet, radsej ju obnovime.
    if (!ok) showState();

  } else if (c == '0') {
    bool ok = ModbusRTUClient.coilWrite(SLAVE_ID, 0x00FF, 0);
    Serial.print("vsetky VYP  ");
    Serial.println(ok ? "OK" : ModbusRTUClient.lastError());
    showState();

  } else if (c == 's') {
    showState();
  }
}
