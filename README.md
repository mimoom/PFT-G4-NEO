# PFT-G4-NEO

Arduino Opta + externý relé modul na RS485 (Modbus RTU).
Prvá fáza: **zmapovať všetky výstupy a vstupy** a vedieť ich prepínať
z Serial Monitora. Sieťové ovládanie (HTTP/MQTT) príde až potom.

## Čo je v repozitári

```
opta_relay_test/
  opta_relay_test.ino   skica – príkazová konzola cez Serial Monitor
  config.h              všetko nastaviteľné (baud, Modbus ID, prahy, názvy)
  MAPOVANIE.md          tabuľky na zapísanie, čo je na čo zapojené
```

## Príprava

**Arduino IDE** (alebo `arduino-cli`):

1. Boards Manager → **Arduino Mbed OS Opta Boards**, vyber dosku *Opta*.
2. Library Manager → **ArduinoRS485** a **ArduinoModbus**.
3. Otvor `opta_relay_test/opta_relay_test.ino`, nahraj cez USB-C.
4. Serial Monitor: **115200 baud**, zakončenie riadku **Newline**.

Cez `arduino-cli`:

```bash
arduino-cli core install arduino:mbed_opta
arduino-cli lib install ArduinoRS485 ArduinoModbus
arduino-cli compile -b arduino:mbed_opta:opta opta_relay_test
arduino-cli upload  -b arduino:mbed_opta:opta -p /dev/ttyACM0 opta_relay_test
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

## Zapojenie RS485

Opta má svorky **A / B / COM** (modely RS485 a WiFi). Prepoj `A→A`, `B→B`,
`COM→GND` s relé modulom. Na koncoch dlhšej linky daj **120 Ω terminátor**
(Opta ho má riešený cez `RS485.begin()` knižnice – ak máš problémy s
komunikáciou na dlhom vedení, skús externý odpor). Relé modul potrebuje
vlastné napájanie (zvyčajne 12/24 V).

Ak komunikácia nejde, prehoď A a B – rôzni výrobcovia ich značia opačne.

## Rýchly štart

```
> scanbaud          nájde baud aj Modbus adresu modulu
> baud 9600
> id 1
> status            stav interných relé, externých relé a vstupov
> walk r            postupne zopne R1..R4 – zapíš si, čo cvakne
> walk x            to isté pre externý modul
> watch             vypisuje zmeny vstupov, kým prechádzaš tlačidlá/snímače
> r 2 on            zapni interné relé 2
> x 5 t             prepni externé relé 5
> x 3 p 2000        impulz 2 s na externom relé 3
> off               všetko vypnúť
```

Celý zoznam príkazov: `help`.

Keď vieš, čo je na čo zapojené, dopíš názvy do `config.h`
(`INTERNAL_RELAY_NAMES`, `INPUT_NAMES`, `EXT_RELAY_NAMES`) a tabuľky
v `opta_relay_test/MAPOVANIE.md`. Výpisy potom budú pomenované.

## Keď modul neodpovedá

- `scan` prejde adresy 1..247 na aktuálnom baude, `scanbaud` skúsi bežné
  rýchlosti pre adresy 1..16.
- Surové Modbus príkazy na skúmanie neznámeho modulu:
  `mb rc 1 0 8` (čítaj 8 coilov), `mb wc 1 0 1` (zapíš coil 0),
  `mb rh 1 0 4` (holding registre), `mb wh 1 0 1`.
- Niektoré moduly sa neovládajú coilmi ale holding registrami – zisti to
  cez `mb rh` a uprav `extWrite()` v skici.

## Ďalší krok – sieť

Príkazy sú spracované v jedinej funkcii `handleCommand(line, out)`, ktorá
píše do ľubovoľného `Print`. Na HTTP/MQTT ovládanie stačí tú istú funkciu
zavolať s telom požiadavky a výstup nechať tiecť do odpovede – logika
relé sa nemusí duplikovať.
