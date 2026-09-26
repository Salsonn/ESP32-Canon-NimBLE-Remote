/*
 * ESP32-C3 OLED -> Canon R50 BLE timelapse remote (NimBLE port)
 * OLED: 0.42" 72x40 SSD1306, SCL=6 SDA=5 (peff74/ESP32-C3_OLED working example)
 * Button: BOOT = GPIO 8 (RST = GPIO 9 is reset, not usable)
 *
 * Power workflow: remote is only on while preparing/shooting; on = connected.
 *  - Boot: init BLE + auto-connect to stored camera MAC (no scan).
 *  - Long press: scan + pair (only for a new/changed camera).
 *  - Heat levers (in the library): 0 dBm TX, ~1 s conn interval, no bg scan.
 *  - OLED redraws only on state change (+1 Hz while running for the countdown).
 */
#include <U8g2lib.h>
#include "CanonBLERemote.h"

#define OLED_RESET U8X8_PIN_NONE
#define OLED_SDA 5
#define OLED_SCL 6
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RESET, OLED_SCL, OLED_SDA);
const int XOFF = 30, YOFF = 22;

#define BTN 9
const unsigned long LONG_PRESS_MS = 3000;

CanonBLERemote canon_ble("Timelapse C3");

const unsigned long INTERVAL_MS = 5000;
const unsigned long TOTAL_SHOTS = 30;   // 0 = unlimited

bool running = false;
bool pairing = false;
bool bleReady = false;
unsigned long shotsTaken = 0;
unsigned long nextShotAt = 0;

bool btnWasDown = false;
bool longFired = false;
unsigned long btnDownAt = 0;

bool dirty = true;
unsigned long lastDraw = 0;

void markDirty() { dirty = true; }

void ensureBLE() {
  if (!bleReady) {
    canon_ble.init();   // stack + TX power + security config + NVS load
    bleReady = true;
    Serial.println("BLE ready");
  }
}

bool bleConnected()    { return bleReady && canon_ble.isConnected(); }
bool hasStoredCamera() { return bleReady && canon_ble.getPairedAddressString().length() == 17; }

void onShortPress() {
  if (pairing) return;
  if (running) {
    running = false;
  } else {
    shotsTaken = 0;
    nextShotAt = millis();   // fire immediately on start
    running = true;
    // If not yet connected, the first trigger() auto-connects via stored MAC.
  }
  markDirty();
}

void onLongPress() {
  if (pairing) return;
  pairing = true;
  markDirty();
  ensureBLE();
  bool ok = false;
  for (int i = 0; i < 5 && !ok; i++) {
    ok = canon_ble.pair(10);
    if (!ok) delay(1000);
  }
  pairing = false;
  Serial.println(ok ? "Paired" : "Pairing failed");
  markDirty();
}

void drawDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_4x6_tr);
  char line[24];

  const char* status = pairing ? "PAIRING..." : (running ? "RUNNING" : "IDLE");
  u8g2.setCursor(XOFF, YOFF + 10);
  u8g2.print(status);
  if (bleConnected()) {
    u8g2.setCursor(XOFF + 62, YOFF + 10);
    u8g2.print("*");
  }

  u8g2.setCursor(XOFF, YOFF + 20);
  snprintf(line, sizeof(line), "Int %lus", INTERVAL_MS / 1000);
  u8g2.print(line);
  u8g2.setCursor(XOFF + 34, YOFF + 20);
  if (TOTAL_SHOTS == 0)
    snprintf(line, sizeof(line), "%lu/~", shotsTaken);
  else
    snprintf(line, sizeof(line), "%lu/%lu", shotsTaken, TOTAL_SHOTS);
  u8g2.print(line);

  u8g2.setCursor(XOFF, YOFF + 30);
  if (pairing) {
    u8g2.print("Camera: pair mode");
  } else if (running) {
    if (!hasStoredCamera()) {
      u8g2.print("No cam - pair");
    } else {
      unsigned long sLeft = (nextShotAt > millis()) ? (nextShotAt - millis()) / 1000 : 0;
      snprintf(line, sizeof(line), "Next: %lus", sLeft);
      u8g2.print(line);
    }
  } else if (bleConnected()) {
    u8g2.print("Shrt:strt Lng:pair");
  } else if (hasStoredCamera()) {
    u8g2.print("No link: strt=conn");
  } else {
    u8g2.print("No cam: lng=pair");
  }
  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN, INPUT_PULLUP);

  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);
  Serial.println("OLED ready");

  ensureBLE();
  if (hasStoredCamera()) {
    if (canon_ble.connect()) {          // direct connect to stored MAC, no scan
      Serial.println("Connected at boot");
    } else {
      Serial.println("No link yet (camera off?)");
    }
  }

  drawDisplay();
  lastDraw = millis();
  dirty = false;
}

void loop() {
  bool down = (digitalRead(BTN) == LOW);
  if (down && !btnWasDown) {
    btnDownAt = millis();
    longFired = false;
  } else if (!down && btnWasDown) {
    if (!longFired && (millis() - btnDownAt) < LONG_PRESS_MS) onShortPress();
  }
  if (down && !longFired && (millis() - btnDownAt) >= LONG_PRESS_MS) {
    longFired = true;
    onLongPress();
  }
  btnWasDown = down;

  if (running && !pairing && millis() >= nextShotAt) {
    if (canon_ble.trigger()) {          // auto-reconnects via stored MAC if dropped
      shotsTaken++;
      markDirty();
      if (TOTAL_SHOTS != 0 && shotsTaken >= TOTAL_SHOTS) {
        running = false;
      } else {
        nextShotAt = millis() + INTERVAL_MS;
      }
    } else {
      nextShotAt = millis() + 5000;     // trigger failed, retry in 5 s
    }
  }

  if (dirty) {
    drawDisplay();
    dirty = false;
    lastDraw = millis();
  } else if (running && (millis() - lastDraw) >= 1000) {
    drawDisplay();                      // refresh the "Next: Xs" countdown
    lastDraw = millis();
  }

  delay(10);
}
