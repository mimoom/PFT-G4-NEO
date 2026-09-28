// ============================================================================
//  LADENIE CASOVANIA RS485 – Arduino Opta + Waveshare Modbus RTU Relay
//
//  Preco: ked z linky chodia "Invalid CRC" / "Invalid data" / "Response not
//  from requested slave", fyzicka vrstva FUNGUJE a chyba je v casovani.
//  Podozrivy je post-delay: ArduinoRS485 pocas neho este drzi vysielac
//  zapnuty, takze ak slave odpovie skor, Opta mu prekrici zaciatok odpovede.
//
//  Tento sketch nehada – vyskusa kombinacie pre/post delay a spocita, kolko
//  dotazov z 8 preslo. Na konci vypise riadok, ktory si vlozis do kodu.
//
//  POZOR na to, CO sa meria. Prva verzia tohto sketchu iba citala coily
//  a ukazala post 800 us ako bezchybny – lenze citaci ramec konci bajtom
//  0xCC, ktoreho posledne vysielane bity su jednotky, a te prezivaju aj
//  ked DE spadne predcasne. Zapis "rele 1 ZAP" konci 0x3A (koncove bity
//  su nuly) a pri tom istom nastaveni zlyhaval.
//  Preto sa teraz v kazdom pokuse testuje CITANIE aj taky ZAPIS, a pokus
//  plati len ked prejdu oba. Rele 1 pocas merania cvaka.
//
//  Kniznice: ArduinoRS485, ArduinoModbus
//  Doska:    Arduino Mbed OS Opta Boards -> Opta
//  Monitor:  115200 baud
// ============================================================================

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

const int  SLAVE_ID = 1;
const long BAUD     = 9600;   // z vyroby ma Waveshare 9600
const int  TRIES    = 8;      // kolko dotazov na jednu kombinaciu
const int  COILS_N  = 8;

const int preList[]  = {50, 500, 1000, 3500};
const int postList[] = {0, 50, 200, 800, 3500};

const int PRE_N  = sizeof(preList) / sizeof(preList[0]);
const int POST_N = sizeof(postList) / sizeof(postList[0]);

bool started = false;

int bestOk = 0, bestPre = 0, bestPost = 0;
long bestBaud = BAUD;

// Nastavi port a vrati pocet uspesnych dotazov z TRIES
int testCombo(long baud, int pre, int post) {
  if (started) ModbusRTUClient.end();
  started = false;

  RS485.setDelays(pre, post);
  if (!ModbusRTUClient.begin(baud, SERIAL_8N1)) return -1;
  started = true;
  ModbusRTUClient.setTimeout(400);
  delay(50);

  int ok = 0;
  for (int i = 0; i < TRIES; i++) {
    // 1) citanie – ramec konci jednotkami, znasa aj orezany koniec
    bool rd = ModbusRTUClient.requestFrom(SLAVE_ID, COILS, 0, COILS_N);
    if (rd) while (ModbusRTUClient.available()) ModbusRTUClient.read();
    delay(40);

    // 2) zapis "rele 1 ZAP" – ramec konci nulami, orezanie ho znici,
    //    takze prave on odhali prikratky post-delay
    bool wr = ModbusRTUClient.coilWrite(SLAVE_ID, 0, 1);
    delay(40);
    ModbusRTUClient.coilWrite(SLAVE_ID, 0, 0);   // upratanie, vysledok nezaujima

    if (rd && wr) ok++;
    delay(40);   // nech ma linka pokoj medzi pokusmi
  }
  return ok;
}

void report(long baud, int pre, int post, int ok) {
  Serial.print("  baud ");
  Serial.print(baud);
  Serial.print("  pre ");
  Serial.print(pre);
  Serial.print(" us  post ");
  Serial.print(post);
  Serial.print(" us  ->  ");
  Serial.print(ok);
  Serial.print("/");
  Serial.print(TRIES);
  if (ok == TRIES)      Serial.println("   <<< PLNY USPECH");
  else if (ok > 0)      Serial.println("   (ciastocne)");
  else                  Serial.println();

  if (ok > bestOk) { bestOk = ok; bestPre = pre; bestPost = post; bestBaud = baud; }
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}

  Serial.println("\n=== Ladenie casovania RS485 ===");
  Serial.print("Modul ID ");
  Serial.print(SLAVE_ID);
  Serial.println(", citam coily aj zapisujem rele 1. Rele bude cvakat.\n");

  // --- Faza 1: kombinacie oneskoreni pri 9600 -----------------------------
  Serial.println("Faza 1 – oneskorenia pri 9600 8N1:");
  for (int p = 0; p < POST_N; p++) {
    for (int q = 0; q < PRE_N; q++) {
      report(BAUD, preList[q], postList[p], testCombo(BAUD, preList[q], postList[p]));
    }
  }

  // --- Faza 2: ak nic nepreslo, skusime ine rychlosti ---------------------
  if (bestOk == 0) {
    Serial.println("\nPri 9600 nepreslo nic – skusam ine rychlosti:");
    const long bauds[] = {4800, 19200, 38400, 57600, 115200};
    for (unsigned int b = 0; b < sizeof(bauds) / sizeof(bauds[0]); b++) {
      report(bauds[b], 1000, 0,    testCombo(bauds[b], 1000, 0));
      report(bauds[b], 1000, 3500, testCombo(bauds[b], 1000, 3500));
    }
  }

  // --- Vysledok -----------------------------------------------------------
  Serial.println("\n--- VYSLEDOK ---");
  if (bestOk == 0) {
    Serial.println("Neprelo nic. Modul odpoveda, ale nevieme sa dohodnut.");
    Serial.println("Skus inu adresu (ID 2..16) alebo paritu 8E1/8O1.");
    while (true) delay(1000);
  }

  Serial.print("Najlepsie: baud ");
  Serial.print(bestBaud);
  Serial.print(", pre ");
  Serial.print(bestPre);
  Serial.print(" us, post ");
  Serial.print(bestPost);
  Serial.print(" us  (");
  Serial.print(bestOk);
  Serial.print("/");
  Serial.print(TRIES);
  Serial.println(")");

  Serial.println("\nVloz si do kodu tieto dva riadky:");
  Serial.print("  RS485.setDelays(");
  Serial.print(bestPre);
  Serial.print(", ");
  Serial.print(bestPost);
  Serial.println(");");
  Serial.print("  ModbusRTUClient.begin(");
  Serial.print(bestBaud);
  Serial.println(", SERIAL_8N1);");

  if (bestOk < TRIES) {
    Serial.println("\nPozor: nepreslo to na 100 %. Linka je nespolahliva –");
    Serial.println("skrat kable, pridaj terminator 120 ohm, over spolocnu zem.");
  }

  // Prepni na najlepsie najdene nastavenie a nechaj bezat zivy test
  testCombo(bestBaud, bestPre, bestPost);
  Serial.println("\nTeraz bezi zivy test – rele 1 sa prepina kazde 2 s.\n");
}

void loop() {
  static bool on = false;
  on = !on;

  bool ok = ModbusRTUClient.coilWrite(SLAVE_ID, 0, on ? 1 : 0);
  Serial.print("rele 1 ");
  Serial.print(on ? "ZAP  -> " : "VYP  -> ");
  if (ok) {
    Serial.println("OK");
  } else {
    Serial.print("CHYBA: ");
    const char* err = ModbusRTUClient.lastError();
    Serial.println(err ? err : "(neznama)");
  }
  delay(2000);
}
