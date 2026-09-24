// ============================================================================
//  Konfigurácia – upravuj len tento súbor
// ============================================================================
#pragma once

// ---------------------------------------------------------------------------
//  USB Serial (Serial Monitor)
// ---------------------------------------------------------------------------
#define USB_BAUD 115200

// ---------------------------------------------------------------------------
//  RS485 / Modbus RTU – Waveshare Modbus RTU Relay
//  Z výroby: 9600 8N1, adresa 1. Ak nesedí, použi `scan` / `scanbaud`.
// ---------------------------------------------------------------------------
#define MODBUS_BAUD          9600
#define MODBUS_SERIAL_CONFIG SERIAL_8N1
#define MODBUS_TIMEOUT_MS    300     // timeout bežnej požiadavky
#define SCAN_TIMEOUT_MS      60      // timeout pri skenovaní adries
#define SCANBAUD_MAX_ID      16      // `scanbaud` skúša adresy 1..SCANBAUD_MAX_ID

#define RELAY_MODULE_ID      1       // Modbus adresa (slave ID) relé modulu
#define RELAY_MODULE_COUNT   9       // počet kanálov; `probe` ho zistí a prepíše
#define MAX_EXT_CHANNELS     32      // horný limit (Waveshare má verzie do 32 ch)
#define RELAY_COIL_OFFSET    0       // adresa coilu pre kanál 1 (Waveshare = 0)

// Po štarte vypnúť všetky relé (interné aj externé)
#define ALL_OFF_ON_BOOT      true

// ---------------------------------------------------------------------------
//  Waveshare registre (platia pre Modbus RTU Relay 4/8/16/32CH, aj (B)/(D))
// ---------------------------------------------------------------------------
#define WS_COIL_ALL          0x00FF  // coil "všetky relé naraz"
#define WS_REG_DEVICE_ADDR   0x4000  // FC03 čítaj / FC06 nastav Modbus adresu
#define WS_REG_BAUD          0x2000  // FC06 nastav rýchlosť (0=4800 … 5=115200)
#define WS_REG_VERSION       0x8000  // FC03 verzia firmvéru (nie na každom kuse)

// Vstupy má len varianta (D) – 8 digitálnych vstupov na FC02 od adresy 0.
// Nastav na 0, ak tvoj modul vstupy nemá.
#define WS_DIGITAL_INPUTS    0

// ---------------------------------------------------------------------------
//  Vstupy I1..I8 na Opte (0–10 V analóg / 24 V digitál)
//  Stav ON/OFF sa vyhodnocuje z napätia s hysteréziou.
//  Pozn.: ADC meria do ~10.7 V, 24 V signál sa zobrazí ako ~10.7 V (= ON).
// ---------------------------------------------------------------------------
#define INPUT_ON_V           6.0f
#define INPUT_OFF_V          4.0f
#define WATCH_PERIOD_MS      50      // ako často sa vzorkujú vstupy v režime watch

// ---------------------------------------------------------------------------
//  `walk` – postupné prepínanie relé kvôli fyzickej identifikácii
// ---------------------------------------------------------------------------
#define WALK_ON_MS           1500
#define WALK_GAP_MS          700

// ---------------------------------------------------------------------------
//  Pomenovania – sem dopíš, čo je na čo zapojené (zobrazí sa vo výpisoch).
//  Priebežne udržuj v súlade s MAPOVANIE.md.
// ---------------------------------------------------------------------------
const char* const INTERNAL_RELAY_NAMES[4] = {
  "?",  // R1 (výstup 1)
  "?",  // R2 (výstup 2)
  "?",  // R3 (výstup 3)
  "?",  // R4 (výstup 4)
};

const char* const INPUT_NAMES[8] = {
  "?",  // I1 (A0)
  "?",  // I2 (A1)
  "?",  // I3 (A2)
  "?",  // I4 (A3)
  "?",  // I5 (A4)
  "?",  // I6 (A5)
  "?",  // I7 (A6)
  "?",  // I8 (A7)
};

// Kanály Waveshare modulu. Chýbajúce položky sa vypíšu ako "?".
const char* const EXT_RELAY_NAMES[] = {
  "?",  // X1
  "?",  // X2
  "?",  // X3
  "?",  // X4
  "?",  // X5
  "?",  // X6
  "?",  // X7
  "?",  // X8
  "?",  // X9
};
