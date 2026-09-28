// ============================================================================
//  NAJJEDNODUCHSI TEST – Arduino Opta + Waveshare Modbus RTU Relay 8CH
//
//  Nerobi nic ine, len postupne zapina rele 1..8 (kazde 1 s) a po kazdom
//  zapise vypise, ci sa to podarilo. Ziadne prikazy, ziadne nastavenia.
//
//  Kniznice: ArduinoRS485, ArduinoModbus
//  Doska:    Arduino Mbed OS Opta Boards -> Opta
//  Monitor:  115200 baud
//
//  Zapojenie:
//    Opta A   -> modul A
//    Opta B   -> modul B
//    Opta COM -> modul GND
//    modul musi mat vlastne napajanie (zakladna verzia 5 V, B/D 7-36 V)
//
//  Ak to nejde: prehod A a B. Je to najcastejsia pricina.
// ============================================================================

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

const int  SLAVE_ID = 1;      // Waveshare ma z vyroby adresu 1
const int  CHANNELS = 8;      // 8-kanalovy modul
const long BAUD     = 9600;   // Waveshare ma z vyroby 9600 8N1

int current = 0;              // ktory kanal je prave zapnuty (0 = ziadny)

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}

  Serial.println("\n=== Opta + Waveshare 8CH – minimalny test ===");

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
  Serial.println("Teraz by mali rele postupne cvakat...\n");
}

// Zapise jeden coil a vypise vysledok. Vrati true pri uspechu.
bool setRelay(int channel, bool on) {      // channel = 1..8
  bool ok = ModbusRTUClient.coilWrite(SLAVE_ID, channel - 1, on ? 1 : 0);

  Serial.print("rele ");
  Serial.print(channel);
  Serial.print(on ? " ZAP  -> " : " VYP  -> ");

  if (ok) {
    Serial.println("OK");
  } else {
    Serial.print("CHYBA: ");
    const char* err = ModbusRTUClient.lastError();
    Serial.println(err ? err : "(neznama)");
  }
  return ok;
}

void loop() {
  if (current > 0) setRelay(current, false);   // vypni predchadzajuce

  current = (current % CHANNELS) + 1;          // 1,2,...,8,1,2,...
  setRelay(current, true);

  if (current == CHANNELS) Serial.println();   // prazdny riadok po kazdom kole
  delay(1000);
}
