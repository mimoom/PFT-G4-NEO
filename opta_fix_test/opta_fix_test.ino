// ============================================================================
//  Co presne sposobuje timeout pri zapinani rele?
//
//  Zapis ON hadze timeout, zapis OFF prejde, citanie ide spolahlivo
//  a rele pritom naozaj zopne a drzi. Su dve mozne priciny a kazda sa
//  opravuje inak:
//
//    A) odpoved sa STRATI  – modulu klesne napajanie pri rozbehu cievky
//                            -> oprava na hardveri (kondenzator, zdroj)
//    B) odpoved len MESKA  – modul odpoveda az po zopnuti rele
//                            -> oprava v kode (dlhsi timeout)
//
//  Tento sketch spusti tri pokusy a povie, ktora to je. Nic nekupuj,
//  kym to nedobehne.
//
//  Rele 1 pocas testu niekolkokrat cvakne. Nic ine sa nedeje.
// ============================================================================

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

const int SLAVE_ID = 1;
const int COIL     = 0;       // rele 1

// Zapise coil, zmeria ako dlho to trvalo, vypise vysledok. Vrati true = OK.
bool tryWrite(bool on, int timeoutMs, const char* popis) {
  ModbusRTUClient.setTimeout(timeoutMs);

  unsigned long t0 = millis();
  bool ok = ModbusRTUClient.coilWrite(SLAVE_ID, COIL, on ? 1 : 0);
  unsigned long ms = millis() - t0;

  Serial.print("  ");
  Serial.print(popis);
  Serial.print(" (timeout ");
  Serial.print(timeoutMs);
  Serial.print(" ms) -> ");
  if (ok) {
    Serial.print("OK za ");
    Serial.print(ms);
    Serial.println(" ms");
  } else {
    Serial.print("CHYBA po ");
    Serial.print(ms);
    Serial.print(" ms: ");
    const char* e = ModbusRTUClient.lastError();
    Serial.println(e ? e : "(neznama)");
  }
  return ok;
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) {}

  Serial.println("\n=== Preco zlyhava zapinanie? ===\n");

  RS485.setDelays(500, 800);
  if (!ModbusRTUClient.begin(9600, SERIAL_8N1)) {
    Serial.println("Modbus sa nepodarilo spustit – stop.");
    while (true) delay(1000);
  }

  // --- Vychodzi stav: rele VYP ------------------------------------------
  Serial.println("Priprava:");
  tryWrite(false, 500, "vypnut rele");
  delay(400);

  // --- Pokus 1: zapnutie, kratky timeout (takto to zlyhava) -------------
  Serial.println("\nPokus 1 – zapnutie (cievka sa rozbieha):");
  bool p1 = tryWrite(true, 500, "zapnut");
  delay(400);

  // --- Pokus 2: zapnutie rele, ktore UZ JE zapnute ----------------------
  //     Rovnaky ramec, rovnaka funkcia, rovnaka hodnota – ale cievka uz
  //     bezi, takze ziadny rozbehovy prud. Ak toto prejde a pokus 1 nie,
  //     problem je viazany na rozbeh cievky, nie na obsah prikazu.
  Serial.println("\nPokus 2 – to iste na uz zapnutom rele (bez rozbehu):");
  bool p2 = tryWrite(true, 500, "zapnut znovu");
  delay(400);

  // --- Pokus 3: zapnutie s velmi dlhym timeoutom ------------------------
  Serial.println("\nPokus 3 – zapnutie, ale cakame az 3 s:");
  tryWrite(false, 500, "najprv vypnut");
  delay(400);
  bool p3 = tryWrite(true, 3000, "zapnut");

  // --- Zaver ------------------------------------------------------------
  Serial.println("\n================ ZAVER ================");

  if (p1) {
    Serial.println("Pokus 1 presiel – chyba sa prave teraz neprejavila.");
    Serial.println("Nechaj bezat opta_minimal a skus to znovu.");
  } else if (p3) {
    Serial.println("Odpoved iba MESKALA – prisla, len neskoro.");
    Serial.println("OPRAVA: v kode zvys timeout, hardver netreba:");
    Serial.println("   ModbusRTUClient.setTimeout(1000);");
  } else if (p2) {
    Serial.println("Bez rozbehu cievky to ide, s rozbehom nie,");
    Serial.println("a ani dlhe cakanie nepomoze -> odpoved sa naozaj STRATI.");
    Serial.println("OPRAVA na napajani modulu:");
    Serial.println("   1. zmeraj 5 V priamo na svorkach, ked je rele zopnute");
    Serial.println("   2. elektrolyt 470-1000 uF na svorky 5 V / GND");
    Serial.println("   3. silnejsi zdroj (min 1 A) a kratsie hrubsie vodice");
  } else {
    Serial.println("Zlyhalo aj zapnutie bez rozbehu cievky.");
    Serial.println("Potom to nesuvisi s cievkou – problem je v komunikacii");
    Serial.println("samotnej. Spusti opta_relay_test a napis tam `diag`.");
  }
  Serial.println("=======================================");

  tryWrite(false, 1000, "\nupratanie: vypnut rele");
}

void loop() {}
