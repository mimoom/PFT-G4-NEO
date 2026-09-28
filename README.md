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

## Modul

**Waveshare Industrial Modbus RTU 8-ch Relay Module, RS485, multi
isolation** — základná verzia, napájanie **5 V** (verzie *(B)* a *(D)*
majú 7–36 V, táto nie).

| Parameter | Hodnota |
|---|---|
| Napájanie | 5 V |
| Odber v pokoji | 0,18 W ≈ 36 mA |
| Odber so všetkými relé | 2,9 W ≈ **580 mA** |
| Jedna cievka | ~68 mA navyše |
| Kontakty | 10 A 250 V AC / 30 V DC |
| Z výroby | 9600 8N1, adresa 1 |

Zdroj musí utiahnuť aspoň **1 A** a vodiče majú byť krátke a hrubé.
RS485 je na module *izolovaný* a jeho vysielač je napájaný cez DC-DC
z tej istej 5 V vetvy ako cievky — keď zopnutie cievky stiahne napätie,
modul nestihne odpovedať a zápis skončí na timeout, hoci relé zoplo.

Príznak je typický: **čítanie ide spoľahlivo, vypnutie relé ide,
zapnutie hádže timeout.** Pomáha:

- silnejší 5 V zdroj a kratšie vodiče
- elektrolytický kondenzátor 470–1000 µF priamo na svorkách 5 V/GND
- viesť RS485 pár ďalej od relé časti

**Overené na stole:** relé zopne a stav drží — stráca sa naozaj len
odpoveď, nie príkaz. Skica si po zlyhanom zápise načíta skutočný stav
a vypíše `OK (odpoved sa stratila, stav overeny citanim)`. Riadenie
teda funguje aj bez zásahu do napájania.

Napriek tomu to opraviť treba, skôr než sa na kontakty pripojí niečo
skutočné: `a` (všetkých 8 naraz) berie ~544 mA v jednom kroku, a modul,
ktorý si podreže napájanie, môže raz stratiť aj príkaz, nielen odpoveď.

## Časovanie RS485 — dôležité

Vzorec z oficiálneho Arduino príkladu pre Optu
(`bitDuration * 9.6 * 3.5 * 1e6`) dá pri 9600 baud **3500 µs** post-delay
a **s týmto modulom nefunguje**. Namerané hodnoty (`opta_rs485_tune`):

| post delay | výsledok |
|---|---|
| 0 / 50 / 200 µs | 0/8 — modul vôbec neodpovie |
| **800 µs** | **8/8 — funguje** |
| 3500 µs | chyby CRC, rozsynchronizované odpovede |

Je to úzke okno s dvoma rôznymi príčinami:

- **Dolná hranica:** `flush()` sa na Opte vráti skôr, než posledný bajt
  fyzicky odíde z UARTu. Keď DE spadne hneď, koniec rámca sa odreže,
  modul dostane zlé CRC a mlčí → samé timeouty.
- **Horná hranica:** Opta stále drží vysielač, keď modul už začal
  odpovedať. Začiatok odpovede sa zničí → `Invalid CRC`, `Invalid data`
  a `Response not from requested slave` v pravidelnom cykle.

Nastavené je `RS485.setDelays(500, 800)`. Hodnota `pre` nie je kritická
(50 aj 500 µs fungovali rovnako). Pri zmene rýchlosti alebo iného modulu
spusti `opta_rs485_tune` a premeraj to znova.

## Keď komunikácia nejde

Napíš **`diag`**. Pošle surový Modbus rámec mimo knižnice a vypíše presne
to, čo príde späť. Podľa výsledku vieš, kde hľadať:

| Čo `diag` vypíše | Kde je problém |
|---|---|
| `<ticho – neprisiel ani jeden bajt>` | fyzická vrstva: drôty, napájanie, adresa |
| bajty prídu, ale `CRC nesedi` | zlý baud alebo parita → `scanbaud` |
| `CRC OK` + `VYNIMKA 0x02` | komunikácia ide, len sedí iný rozsah adries → `probe` |
| `CRC OK`, funkcia bez chyby | funguje to |

### Keď je ticho, prejdi po rade

1. **Má tvoja Opta vôbec RS485?** *Opta Lite ho nemá* — svorky A/B/COM sú
   len na verziách **Opta RS485** a **Opta WiFi**. Toto je najčastejší
   dôvod, prečo „to nikdy nešlo".
2. **Napájanie modulu.** Základná verzia 5 V, verzie **(B)/(D)** 7–36 V.
   Z RS485 sa modul nenapája.
3. **Spoločná zem.** `COM` na Opte ↔ `GND` modulu. Bez nej to často
   nejde vôbec, alebo len nespoľahlivo.
4. **Prehoď A a B.** Značenie nie je medzi výrobcami jednotné a toto je
   druhá najčastejšia príčina. Skúsiť to trvá 10 sekúnd.
5. **`scanbaud`** — prejde rýchlosti × parity × adresy 1–16. Ak sa niečo
   ozve, máš nastavenia; ak nič, problém je fyzický (body 1–4).
6. **Terminátor 120 Ω** — až pri dlhšom vedení. Waveshare ho má na doske
   jumperom.

### Ručné skúšanie

```
> raw 01 01 00 00 00 08    surový rámec, CRC sa doplní samo
> sniff 10                 10 s len počúva, čo je na linke
> cfg 8e1                  prepne paritu na strane Opty
> mb rh 1 0x4000 1         adresa zariadenia
> mb rh 1 0x8000 1         verzia firmvéru
```

## Ďalší krok – sieť

Príkazy sú spracované v jedinej funkcii `handleCommand(line, out)`, ktorá
píše do ľubovoľného `Print`. Na HTTP/MQTT ovládanie stačí tú istú funkciu
zavolať s telom požiadavky a výstup nechať tiecť do odpovede – logika
relé sa nemusí duplikovať.
