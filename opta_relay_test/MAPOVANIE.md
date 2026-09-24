# Mapovanie I/O – Arduino Opta + Waveshare Modbus RTU Relay

Tento súbor je **pracovný záznam**. Vypĺňaj ho počas testovania a to isté
zapíš do `config.h` (polia `INTERNAL_RELAY_NAMES`, `INPUT_NAMES`,
`EXT_RELAY_NAMES`), aby výpisy v Serial Monitore dávali zmysel.

## Hardvér

| Časť | Popis |
|---|---|
| Opta model | (Lite / RS485 / WiFi) — doplň |
| Externý modul | Waveshare Modbus RTU Relay, presný typ: … |
| Počet kanálov | … (skica si ho zistí sama príkazom `probe`) |
| RS485 | baud: … , 8N1, Modbus ID: … |

## Interné relé Opty (R1–R4)

Relé sú **NO, bezpotenciálové** (max 250 V AC / 6 A). Piny `D0–D3`,
stavové LED `LED_D0–LED_D3`.

| Kanál | Pin | Zapojené na | Poznámka |
|---|---|---|---|
| R1 | D0 | | |
| R2 | D1 | | |
| R3 | D2 | | |
| R4 | D3 | | |

## Vstupy Opty (I1–I8)

Každý vstup vie 0–10 V analóg aj 24 V digitál. ADC je 12-bit a meria
zhruba do 10,7 V, takže 24 V signál sa číta ako ~10,7 V (t. j. ON).
Prah ON/OFF nastavuješ v `config.h` (`INPUT_ON_V` / `INPUT_OFF_V`).

| Vstup | Pin | Typ (digi/0–10 V) | Zapojené na | Poznámka |
|---|---|---|---|---|
| I1 | A0 | | | |
| I2 | A1 | | | |
| I3 | A2 | | | |
| I4 | A3 | | | |
| I5 | A4 | | | |
| I6 | A5 | | | |
| I7 | A6 | | | |
| I8 | A7 | | | |

## Waveshare modul (X1–Xn)

Relé sú coily od adresy 0. Kontakty 10 A / 250 V AC.

| Kanál | Modbus coil | Zapojené na | Poznámka |
|---|---|---|---|
| X1 | 0 | | |
| X2 | 1 | | |
| X3 | 2 | | |
| X4 | 3 | | |
| X5 | 4 | | |
| X6 | 5 | | |
| X7 | 6 | | |
| X8 | 7 | | |
| X9 | 8 | | |

Ak má modul aj digitálne vstupy (varianta *D*), zapíš ich sem a nastav
`WS_DIGITAL_INPUTS` v `config.h`:

| Vstup | Modbus discrete input | Zapojené na |
|---|---|---|
| DI1 | 0 | |
| DI2 | 1 | |

## Postup mapovania

1. Nahraj skicu, otvor Serial Monitor (115200, Newline).
2. `probe` → adresa modulu, verzia firmvéru, počet kanálov. Ak modul
   neodpovedá, `scanbaud` nájde baud aj ID; nastav `baud <n>` a `id <n>`
   (a potom to isté natrvalo do `config.h`).
3. `walk r` → sleduj, čo cvakne/zopne; zapíš do tabuľky R1–R4.
4. `walk x` → to isté pre externý modul.
5. `watch` → prechádzaj fyzické vstupy (tlačidlá, snímače) a zapisuj I1–I8.
6. Doplň názvy do `config.h` a znova nahraj – výpisy budú už pomenované.
