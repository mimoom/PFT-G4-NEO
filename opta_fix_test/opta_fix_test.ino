// ============================================================================
//  DOKAZ PRICINY: prilis kratky post-delay orezava koniec ramca
//
//  UART vysiela LSB first, takze ako posledne idu bity 7 a 6 posledneho
//  bajtu (horny bajt CRC). Pri 9600 trva jeden znak 1042 us, ale flush()
//  sa vrati skor, nez znak fyzicky odide. S post-delay 800 us spadne DE
//  asi 2 bity predcasne a tie sa strati – linka ide do idle, co je log. 1.
//
//    ramec konciaci jednotkami -> prezije (idle je tiez 1)
//    ramec konciaci nulami     -> znici sa, CRC nesedi, modul mlci
//
//  Preto zlyhava "rele 1 ZAP" (CRC konci 0x3A = ...00), ale "rele 1 VYP"
//  (0xCA = ...11) prejde. A preto mal tuner s citanim 8/8 – citaci ramec
//  konci 0xCC = ...11.
//
//  Test spusti tie iste styri prikazy dvakrat: raz so zlym post-delay,
//  raz so spravnym. Pri 800 us musi rele 2 ist ZAPNUT ale nie VYPNUT,
//  teda presne naopak nez rele 1. Ak to tak vyjde, pricina je dokazana.
//
//  Rele 1 a 2 budu pocas testu cvakat.
// ============================================================================

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

const int  SLAVE_ID = 1;
const long BAUD     = 9600;

bool started = false;

void useDelay(int postUs) {
  if (started) ModbusRTUClient.end();
  RS485.setDelays(500, postUs);
  started = ModbusRTUClient.begin(BAUD, SERIAL_8N1);
  ModbusRTUClient.setTimeout(400);
  delay(50);
}

// Vypise vysledok vedla predpovede. Vrati true, ak sa zhoduju.
bool step(int relay, bool on, bool expectOk) {
  bool ok = ModbusRTUClient.coilWrite(SLAVE_ID, relay - 1, on ? 1 : 0);

  Serial.print("  rele ");
  Serial.print(relay);
  Serial.print(on ? " ZAP  " : " VYP  ");
  Serial.print("predpoved: ");
  Serial.print(expectOk ? "prejde" : "zlyha ");
  Serial.print("   realita: ");
  Serial.print(ok ? "prejde" : "zlyha ");
  Serial.println(ok == expectOk ? "   <- sedi" : "   <- NESEDI");

  delay(300);
  return ok == expectOk;
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}

  Serial.println("\n=== Dokaz: orezany koniec ramca ===");

  // --- Zly post-delay: 800 us < 1042 us (dlzka znaku pri 9600) ----------
  Serial.println("\npost-delay 800 us (kratsi nez znak – koniec sa oreze):");
  useDelay(800);
  int sedi = 0;
  sedi += step(1, true,  false);   // CRC 0x3A = ...00  -> ma zlyhat
  sedi += step(2, true,  true);    // CRC 0xFA = ...11  -> ma prejst
  sedi += step(1, false, true);    // CRC 0xCA = ...11  -> ma prejst
  sedi += step(2, false, false);   // CRC 0x0A = ...00  -> ma zlyhat

  // --- Spravny post-delay: 1500 us > 1042 us ----------------------------
  Serial.println("\npost-delay 1500 us (cely znak stihne odist):");
  useDelay(1500);
  int okAll = 0;
  okAll += step(1, true,  true);
  okAll += step(2, true,  true);
  okAll += step(1, false, true);
  okAll += step(2, false, true);

  // --- Zaver ------------------------------------------------------------
  Serial.println("\n================ ZAVER ================");
  Serial.print("Pri 800 us sedeli ");
  Serial.print(sedi);
  Serial.println("/4 predpovede.");
  Serial.print("Pri 1500 us preslo ");
  Serial.print(okAll);
  Serial.println("/4 prikazov.");

  if (okAll == 4) {
    Serial.println("\nOPRAVENE. Pouzi RS485.setDelays(500, 1500).");
    if (sedi == 4) {
      Serial.println("Aj predpovede pri 800 us sedeli – pricina potvrdena:");
      Serial.println("post-delay musi byt dlhsi nez jeden znak (pri 9600 = 1042 us).");
    }
  } else {
    Serial.println("\n1500 us nestacilo. Skus 2000 us; ak ani to,");
    Serial.println("spusti opta_rs485_tune a premeraj cele okno.");
  }
  Serial.println("=======================================");

  ModbusRTUClient.coilWrite(SLAVE_ID, 0, 0);
  ModbusRTUClient.coilWrite(SLAVE_ID, 1, 0);
}

void loop() {}
