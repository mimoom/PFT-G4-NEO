# PFT-G4-NEO

Arduino Opta + **Waveshare Modbus RTU Relay** na RS485.
Prvá fáza: **zmapovať všetky výstupy a vstupy** a vedieť ich prepínať
z Serial Monitora. Sieťové ovládanie (HTTP/MQTT) príde až potom.

## Čo je v repozitári

```
opta_minimal/       elementárne ovládanie – stlač 1-8, prepne sa relé
opta_relay_test/    plná konzola: mapovanie, vstupy, diagnostika
  config.h            všetko nastaviteľné (baud, Modbus ID, prahy, názvy)
  MAPOVANIE.md        tabuľky na zapísanie, čo je na čo zapojené
opta_rs485_tune/    merač časovania RS485
opta_fix_test/      dôkaz príčiny orezaných rámcov
opta_web/           ovládanie cez Ethernet (web + JSON API)
KOMUNIKACIA.md      ako funguje Modbus RTU na tejto linke
```

**Začni s `opta_minimal`** — je to jedna obrazovka kódu.

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
majú 7–36 V, táto nie). Kontakty 10 A 250 V AC / 30 V DC.
Z výroby: 9600 8N1, adresa 1.

## Časovanie RS485 — dôležité

`RS485.setDelays(500, 1500)`. Post-delay **musí prekryť celý posledný
znak** — pri 9600 baud trvá znak 1042 µs. `flush()` sa vráti skôr, než
bajt fyzicky odíde z UARTu, takže pri kratšom post-delay spadne DE
predčasne a koncové bity rámca sa odrežú.

Príznak je zákerný: UART vysiela LSB first a uvoľnená linka ide do idle
= log. 1, takže **rámec končiaci jednotkami prežije a rámec končiaci
nulami sa zničí**. Podľa hodnoty CRC teda niektoré príkazy chodia a iné
nie — vyzerá to ako logická chyba, hoci je to elektrické:

| Príkaz | posl. bajt | koncové bity | pri post 800 µs |
|---|---|---|---|
| čítaj coily | `0xCC` | 1,1 | prejde |
| relé 1 VYP | `0xCA` | 1,1 | prejde |
| relé 1 ZAP | `0x3A` | 0,0 | zlyhá |
| relé 2 ZAP | `0xFA` | 1,1 | prejde |
| relé 2 VYP | `0x0A` | 0,0 | zlyhá |

Horná hranica okna je tam, kde Opta drží linku ešte v čase, keď modul
odpovedá → `Invalid CRC`, `Invalid data`. Vzorec z oficiálneho Arduino
príkladu dá 3500 µs a je už za ňou.

Podrobne aj s bitovým rozpisom v [KOMUNIKACIA.md](KOMUNIKACIA.md).

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

## Sieť – ovládanie cez Ethernet

`opta_web/` pridáva k RS485 ešte webové rozhranie. Opta má RJ45, netreba
nič dokupovať. Po štarte vypíše do Serial Monitora svoju adresu:

```
Ethernet: pripojeny (DHCP)
>>> otvor http://192.168.88.57/
```

Stránka má osem tlačidiel a obnovuje sa každé 3 sekundy. Funguje aj
z mobilu.

### API

Volateľné z Home Assistant, Node-RED alebo `curl`:

| Požiadavka | Čo urobí |
|---|---|
| `GET /api/state` | stav všetkých relé ako JSON |
| `GET /api/relay?ch=3&v=on` | zapne relé 3 (`v` = `on`, `off`, `toggle`) |
| `GET /api/alloff` | vypne všetky |

Odpoveď je vždy rovnaká:

```json
{"ok":true,"relays":[0,1,0,0,0,0,0,0]}
```

`ok` hovorí, či modul na RS485 odpovedal. Stav sa po každom zápise
načíta z modulu, takže JSON nikdy nie je iba domnienka.

### Pevná IP

DHCP je predvolené. Keď chceš poznať adresu dopredu, v skici nastav
`USE_STATIC_IP = true` a uprav adresy nad tým (prednastavené pre sieť
`192.168.88.x`). Zvolená adresa musí byť mimo rozsahu, ktorý rozdáva
DHCP, inak ju router raz pridelí niekomu inému.

### Bezpečnosť

Server **nemá žiadne overovanie**, kým nevyplníš `API_KEY`. Ovláda
kontakty na 10 A, takže:

- v inej než domácej sieti `API_KEY` vyplň (potom sa ku každej
  požiadavke pridáva `&key=...`, stránka to rieši sama cez `?key=` v URL)
- **nesmeruj naň port z internetu.** Na prístup zvonku použi VPN do tej
  siete; presmerovanie portu vystaví relé celému internetu.
- HTTP nie je šifrované, heslo ide po sieti čitateľné — v rámci LAN to
  stačí, cez internet nie

### Ďalší krok

Na integráciu s domácou automatizáciou je prirodzenejší MQTT: Opta by sa
hlásila brokeru sama a odpadlo by dopytovanie. To isté API sa dá doplniť
bez zásahu do logiky relé.
