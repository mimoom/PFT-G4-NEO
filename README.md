# PFT-G4-NEO

Arduino Opta + **Waveshare Modbus RTU Relay** na RS485.
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

Opta má svorky **A / B / COM** (modely RS485 a WiFi). Waveshare modul má
**A / B** (a GND). Prepoj `A→A`, `B→B`, `COM→GND`. Na koncoch dlhšej linky
patrí **120 Ω terminátor** – Waveshare ho má zvyčajne na doske jumperom.
Modul potrebuje vlastné napájanie: základná verzia 5 V, verzie **(B)/(D)**
7–36 V.

Ak komunikácia nejde, prehoď A a B – býva to najčastejšia príčina.

## Waveshare protokol – čo skica používa

| Čo | Modbus |
|---|---|
| Relé 1..N | coily `0x0000`+ (FC01 čítanie, FC05 zápis) |
| Všetky relé naraz | coil `0x00FF` (`off`, `x all on/off` pošle jeden rámec) |
| Digitálne vstupy (varianta D) | discrete inputs `0x0000`+ (FC02) |
| Adresa zariadenia | holding register `0x4000` (FC03/FC06) |
| Rýchlosť | holding register `0x2000`, `0`=4800 … `5`=115200 (FC06) |
| Verzia firmvéru | holding register `0x8000` (FC03) |

Z výroby má modul **9600 8N1, adresu 1**.

Zmena nastavení priamo z konzoly (mení modul natrvalo, v `config.h` to
potom oprav aj ty):

```
> ws addr            adresa uložená v module
> ws setaddr 3       zmení adresu modulu na 3
> ws setbaud 115200  zmení rýchlosť modulu (a hneď prepne aj Optu)
```

## Rýchly štart

```
> probe             adresa modulu, verzia firmvéru, počet kanálov
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

## Počet kanálov

V `config.h` je `RELAY_MODULE_COUNT 9`, ale skica si počet **overí sama**
pri štarte aj pri `probe` (číta coily, kým modul neohlási neplatnú adresu)
a riadi sa tým, čo hlási hardvér. Ak vypíše iné číslo, než máš v configu,
prepíš `RELAY_MODULE_COUNT` na hlásenú hodnotu. Ručne sa dá nastaviť aj
príkazom `count <n>`.

## Keď modul neodpovedá

- `scan` prejde adresy 1..247 na aktuálnom baude, `scanbaud` skúsi bežné
  rýchlosti pre adresy 1..16.
- Prehoď A/B, over napájanie modulu a spoločnú zem.
- Surové Modbus príkazy na skúmanie:
  `mb rc 1 0 8` (čítaj 8 coilov), `mb wc 1 0 1` (zapíš coil 0),
  `mb rh 1 0x4000 1` (adresa zariadenia), `mb rh 1 0x8000 1` (verzia).

## Ďalší krok – sieť

Príkazy sú spracované v jedinej funkcii `handleCommand(line, out)`, ktorá
píše do ľubovoľného `Print`. Na HTTP/MQTT ovládanie stačí tú istú funkciu
zavolať s telom požiadavky a výstup nechať tiecť do odpovede – logika
relé sa nemusí duplikovať.
