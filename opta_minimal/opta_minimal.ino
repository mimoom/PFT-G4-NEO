// ============================================================================
//  Arduino Opta + Waveshare Modbus RTU 8-ch Relay – elementarne ovladanie
//
//  V Serial Monitore stlac cislo 1..8 -> dane rele zmeni stav.
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

bool state[CHANNELS] = {false};   // co sme naposledy zapisali

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

  Serial.println("\nStlac 1-8 pre prepnutie rele.");
}

void loop() {
  if (!Serial.available()) return;

  char c = Serial.read();
  if (c < '1' || c > '8') return;

  int ch = c - '1';                 // 0..7
  state[ch] = !state[ch];

  // Zapis jedneho coilu (Modbus funkcia 05). Coil 0 = rele 1.
  bool ok = ModbusRTUClient.coilWrite(SLAVE_ID, ch, state[ch] ? 1 : 0);

  Serial.print("rele ");
  Serial.print(ch + 1);
  Serial.print(state[ch] ? " ZAP  " : " VYP  ");
  Serial.println(ok ? "OK" : ModbusRTUClient.lastError());
}
