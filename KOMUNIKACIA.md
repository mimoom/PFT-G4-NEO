# Ako to komunikuje

## Dve rôzne linky, nepliesť si ich

```
   ty ──USB, 115200──▶ Opta ──RS485, 9600──▶ relé modul
      (Serial Monitor)        (Modbus RTU)
```

- **USB Serial** je len na to, aby si videl výpisy a stláčal čísla.
  S relé modulom nemá nič spoločné.
- **RS485** je fyzická linka: dva vodiče (A, B) + spoločná zem.
  Prenáša sa po nej protokol **Modbus RTU**.

Keď zmeníš `Serial.begin(115200)`, meníš len okno na monitore.
Rýchlosť voči modulu je v `ModbusRTUClient.begin(9600, SERIAL_8N1)`.

## Kto sa koho pýta

Modbus je **master – slave**. Opta je master, relé modul je slave.

> Slave **nikdy nezačne sám**. Len odpovedá na otázku a potom mlčí.

Jedna výmena vyzerá vždy takto:

```
Opta:  01 05 00 00 FF 00 8C 3A     "zapni relé 1"
modul: 01 05 00 00 FF 00 8C 3A     "urobené" (doslovná ozvena)
```

Ak odpoveď nepríde do timeoutu, knižnica vráti chybu. **Neznamená to,
že príkaz neprešiel** — môže sa stratiť len odpoveď.

## Ako vyzerá rámec

Vždy tá istá štruktúra, líši sa len obsah:

```
 01      05        00 00        FF 00      8C 3A
 └adresa └funkcia  └čo          └hodnota   └CRC
```

| Pole | Význam |
|---|---|
| **adresa** | ktorý slave na zbernici (náš modul má 1) |
| **funkcia** | čo s ním chceme (05 = zapíš jeden coil) |
| **čo** | číslo coilu / registra, 2 bajty |
| **hodnota** | 2 bajty, pri coile `FF 00` = zap, `00 00` = vyp |
| **CRC** | kontrolný súčet celého rámca, 2 bajty, počíta knižnica |

Adresa je preto, že na jednej dvojlinke môže visieť viac zariadení.
Každé má svoje číslo a ozve sa len na svoje.

## Coily = relé

„Coil" je v Modbuse jednobitový zapisovateľný výstup. Tu je to relé:

| Coil | Relé |
|---|---|
| 0 | 1 |
| 1 | 2 |
| … | … |
| 7 | 8 |
| `0x00FF` | všetky naraz (špecialita Waveshare) |

Pozor na ten posun o jednotku: **relé 1 = coil 0**. Preto je v kóde
`ch = c - '1'`.

## Funkcie, ktoré potrebuješ

| Kód | Čo robí | Volanie v knižnici |
|---|---|---|
| **05** | zapíš jeden coil | `ModbusRTUClient.coilWrite(1, coil, 0/1)` |
| **01** | prečítaj stav coilov | `ModbusRTUClient.requestFrom(1, COILS, 0, 8)` |
| 03 | prečítaj holding register | `requestFrom(1, HOLDING_REGISTERS, reg, n)` |
| 06 | zapíš holding register | `holdingRegisterWrite(1, reg, hodnota)` |

Funkcie 03/06 potrebuješ len na nastavenia modulu: register `0x4000`
je jeho adresa, `0x2000` rýchlosť.

## Hotové rámce na skúšanie

Overené CRC, dajú sa poslať aj z PC cez USB-RS485 dongle:

| Čo | Rámec |
|---|---|
| relé 1 zap | `01 05 00 00 FF 00 8C 3A` |
| relé 1 vyp | `01 05 00 00 00 00 CD CA` |
| relé 2 zap | `01 05 00 01 FF 00 DD FA` |
| relé 8 zap | `01 05 00 07 FF 00 3D FB` |
| všetky zap | `01 05 00 FF FF 00 BC 0A` |
| všetky vyp | `01 05 00 FF 00 00 FD FA` |
| čítaj 8 relé | `01 01 00 00 00 08 3D CC` |

Odpoveď na čítanie je kratšia a nesie dáta:

```
01 01 01 05 91 8B
│  │  │  └ bity: 0000 0101 = relé 1 a 3 sú zapnuté
│  │  └ počet dátových bajtov
│  └ funkcia 01
└ adresa
```

## Prečo tam je to `setDelays`

RS485 je **polovičný duplex** — jeden pár vodičov, buď vysielaš alebo
počúvaš. Prepína sa signálom DE (driver enable):

```
DE ▔▔▔▔▔▔▔▔▔╲__________________
   │ vysielam │  počúvam
   ├─pre─┬────┤
         └─post─┤
```

- **pre** — po zapnutí vysielača chvíľu čakať, nech sa linka ustáli.
- **post** — po odoslaní ešte chvíľu držať vysielač zapnutý.

`post` je citlivé a má úzke okno:

- **príliš krátke** → vysielač sa vypne skôr, než posledný bajt fyzicky
  odíde z UARTu. Koniec rámca sa odreže, CRC nesedí, modul mlčí.
- **príliš dlhé** → Opta ešte drží linku, keď už modul odpovedá.
  Prekričí mu začiatok odpovede → `Invalid CRC`, `Invalid data`.

Nastavené `RS485.setDelays(500, 1500)`. Vzorec z oficiálneho Arduino
príkladu dá 3500 µs a je už **za** horným okrajom.

### Prečo krátky post-delay vyzerá ako „niektoré príkazy nechodia"

Toto stojí za pochopenie, lebo príznak je zákerný. UART vysiela **LSB
first**, takže ako úplne posledné idú bity 7 a 6 posledného bajtu. Keď
DE spadne predčasne, tieto bity sa stratia a linka ide do **idle, čo je
logická 1**.

```
posledny bajt 0xCA = 11001010
vysiela sa:  start 0 1 0 1 0 0 1 1 stop
                            └─┴─ posledne bity = 1,1
             ak DE spadne teraz, idle je tiez 1 -> bajt prezije

posledny bajt 0x3A = 00111010
vysiela sa:  start 0 1 0 1 1 1 0 0 stop
                            └─┴─ posledne bity = 0,0
             ak DE spadne teraz, idle ich prepise na 1 -> CRC nesedi
```

Pri 9600 trvá jeden znak **1042 µs** (10 bitov). S `post = 800 µs` sa
orežú približne posledné 2 bity — a či to vadí, závisí **od hodnoty CRC
daného rámca**:

| Príkaz | posl. bajt | koncové bity | výsledok |
|---|---|---|---|
| čítaj coily | `0xCC` | 1,1 | prejde |
| relé 1 VYP | `0xCA` | 1,1 | prejde |
| relé 1 ZAP | `0x3A` | 0,0 | zlyhá |
| relé 2 ZAP | `0xFA` | 1,1 | prejde |
| relé 2 VYP | `0x0A` | 0,0 | zlyhá |

Vyzerá to ako logická chyba („zapínanie nejde, vypínanie áno"), pritom
je to čisto elektrické a závisí od náhodnej hodnoty kontrolného súčtu.

**Ponaučenie:** časovanie sa nedá odmerať jedným typom rámca. Prvá
verzia `opta_rs485_tune` iba čítala, dostala 8/8 a potvrdila nastavenie,
ktoré v skutočnosti polovicu zápisov ničilo. Teraz testuje čítanie aj
zápis končiaci nulami.

## Kde sa to dá pokaziť

| Príznak | Kde hľadať |
|---|---|
| ticho, samé timeouty | drôty, napájanie modulu, prehodené A/B |
| `Invalid CRC`, `Invalid data` | rýchlosť, parita alebo `post` delay |
| `Response not from requested slave` | zlá adresa, alebo odpovede sú rozsynchronizované |
| ide čítanie, zápis hádže timeout | modulu klesá napájanie pri zopnutí cievky |
