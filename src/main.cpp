#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <time.h>

#include "dht_sensor.h"

// I2C pins for 0.42" ESP32-C3 Board
#define SDA_PIN 5
#define SCL_PIN 6

// BOOT button ("BO0"/IO9) on the ESP32-C3. Active low: pressed pulls the line
// to GND, so we enable the internal pull-up and treat LOW as "pressed".
#define BOOT_BTN_PIN 9

// WiFi credentials
static const char *WIFI_SSID = "WiFi-Guest";  // Replace with your WiFi SSID
static const char *WIFI_PASS = "WifiPass!";

// NTP configuration.
// TZ string for Europe/Oslo: CET (UTC+1) with CEST (UTC+2) DST, switching on
// the last Sunday of March and October. This lets the C library handle DST for
// us so the displayed time is always correct local time.
static const char *TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";
static const char *NTP_1 = "pool.ntp.org";
static const char *NTP_2 = "time.nist.gov";

// Display / page-switching timing.
static const unsigned long AUTO_SWITCH_MS = 2000;    // auto-advance interval
static const unsigned long MANUAL_TIMEOUT_MS = 10000; // idle -> back to auto
static const unsigned long RENDER_INTERVAL_MS = 250;  // screen refresh cadence
static const unsigned long DEBOUNCE_MS = 40;          // button debounce window

// Hardware I2C Constructor specifically tailored for the 0.42" 72x40 SSD1306 screen
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL_PIN, /* data=*/ SDA_PIN);

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

// Draws a string horizontally centered on the 72px-wide panel at baseline y.
static void drawCentered(const char *s, int y) {
  int w = u8g2.getStrWidth(s);
  int x = (72 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawStr(x, y, s);
}

// ---------------------------------------------------------------------------
// Pages
//
// A page is just a function that draws content into the (already-cleared)
// u8g2 buffer. To add another sensor page later: read its data in a dedicated
// module (see dht_sensor.*), write a render function here, and add it to the
// PAGES[] array below. Everything else (auto-switching, the BOOT button, the
// idle timeout) then works for the new page automatically.
// ---------------------------------------------------------------------------

// Page 1: NTP clock. Shows the date, big HH:MM and seconds once time is synced.
// Before that it shows the WiFi "searching" animation (if not connected) or a
// "Syncing NTP" notice (connected but time not yet obtained).
static void renderClockPage() {
  struct tm timeinfo;
  bool haveTime = getLocalTime(&timeinfo, 0);  // 0 ms: check once, don't block

  if (!haveTime) {
    u8g2.setFont(u8g2_font_6x10_tf);
    if (WiFi.status() != WL_CONNECTED) {
      // WiFi searching: animate a growing row of dots.
      char waiting[5] = {0};
      int dots = (millis() / 500) % 4;
      for (int i = 0; i < dots; i++) waiting[i] = '.';
      drawCentered("WiFi", 18);
      drawCentered(waiting, 32);
    } else {
      drawCentered("Syncing", 18);
      drawCentered("NTP...", 32);
    }
    return;
  }

  char timeStr[6];   // "HH:MM"
  char secStr[3];    // "SS"
  char dateStr[11];  // "DD.MM.YYYY"
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  strftime(secStr, sizeof(secStr), "%S", &timeinfo);
  strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);

  // Date across the top (small font).
  u8g2.setFont(u8g2_font_5x7_tf);
  drawCentered(dateStr, 8);

  // Big HH:MM in the middle.
  u8g2.setFont(u8g2_font_logisoso16_tn);
  int hmWidth = u8g2.getStrWidth(timeStr);
  int hmX = (72 - hmWidth) / 2;
  if (hmX < 0) hmX = 0;
  u8g2.drawStr(hmX, 30, timeStr);

  // Seconds bottom-right (small font).
  u8g2.setFont(u8g2_font_5x7_tf);
  int secWidth = u8g2.getStrWidth(secStr);
  u8g2.drawStr(72 - secWidth, 39, secStr);
}

// Page 2: DHT11 temperature & humidity. Shows "N/A" when the sensor is not
// connected / not responding.
static void renderDhtPage() {
  DhtReading r = dhtRead();

  u8g2.setFont(u8g2_font_5x7_tf);
  drawCentered("DHT11", 8);

  u8g2.setFont(u8g2_font_6x10_tf);
  char line[16];
  if (r.valid) {
    snprintf(line, sizeof(line), "T: %.1f C", r.temperatureC);
    drawCentered(line, 24);
    snprintf(line, sizeof(line), "H: %.0f %%", r.humidity);
    drawCentered(line, 38);
  } else {
    drawCentered("T: N/A", 24);
    drawCentered("H: N/A", 38);
  }
}

// The ordered list of pages. Append new render functions here to add pages.
typedef void (*PageRenderFn)();
static const PageRenderFn PAGES[] = {
  renderClockPage,
  renderDhtPage,
};
static const size_t PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);

// ---------------------------------------------------------------------------
// Page-switching state machine
//
// autoMode == true : pages advance on their own every AUTO_SWITCH_MS.
// autoMode == false: paused on the current page; each BOOT press advances one
//                    page. After MANUAL_TIMEOUT_MS with no press we return to
//                    auto mode.
// ---------------------------------------------------------------------------
static size_t currentPage = 0;
static bool autoMode = true;
static unsigned long lastButtonMs = 0;
static unsigned long lastAutoSwitchMs = 0;
static unsigned long lastRenderMs = 0;

// Whether NTP has been started for the current WiFi session.
static bool timeSyncStarted = false;

static void nextPage() {
  currentPage = (currentPage + 1) % PAGE_COUNT;
}

static void renderCurrentPage() {
  u8g2.clearBuffer();
  PAGES[currentPage]();
  u8g2.sendBuffer();
}

// ---------------------------------------------------------------------------
// BOOT button: debounced falling-edge detector. Returns true exactly once per
// physical press.
// ---------------------------------------------------------------------------
static int btnLastRaw = HIGH;
static int btnStable = HIGH;
static unsigned long btnLastChangeMs = 0;

static bool buttonPressed() {
  int raw = digitalRead(BOOT_BTN_PIN);
  unsigned long now = millis();

  if (raw != btnLastRaw) {
    btnLastRaw = raw;
    btnLastChangeMs = now;
  }

  // Accept the new level once it has been stable past the debounce window.
  if ((now - btnLastChangeMs) >= DEBOUNCE_MS && raw != btnStable) {
    btnStable = raw;
    if (btnStable == LOW) return true;  // just pressed
  }
  return false;
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n--- NTP Clock + DHT11 on 0.42\" OLED ---"));

  u8g2.begin();
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  dhtBegin();

  // Start WiFi non-blocking so the pages keep switching while it connects.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long now = millis();
  lastAutoSwitchMs = now;
  lastRenderMs = now;
}

void loop() {
  unsigned long now = millis();

  // Kick off NTP once, the first time WiFi comes up. Time then keeps running on
  // the ESP32's internal RTC even if WiFi later drops.
  if (!timeSyncStarted && WiFi.status() == WL_CONNECTED) {
    configTzTime(TZ_INFO, NTP_1, NTP_2);
    timeSyncStarted = true;
    Serial.print(F("WiFi connected, IP: "));
    Serial.println(WiFi.localIP());
  }

  // 1. Handle the BOOT button.
  if (buttonPressed()) {
    if (autoMode) {
      // First press: stop auto-switching and hold the current page.
      autoMode = false;
    } else {
      // Subsequent presses: advance one page.
      nextPage();
    }
    lastButtonMs = now;
  }

  // 2. Idle timeout: no press for a while -> resume auto-switching.
  if (!autoMode && (now - lastButtonMs) >= MANUAL_TIMEOUT_MS) {
    autoMode = true;
    lastAutoSwitchMs = now;  // show the current page a full interval first
  }

  // 3. Auto-switch pages on the timer.
  if (autoMode && (now - lastAutoSwitchMs) >= AUTO_SWITCH_MS) {
    nextPage();
    lastAutoSwitchMs = now;
  }

  // 4. Refresh the screen periodically (updates seconds / dot animation).
  if ((now - lastRenderMs) >= RENDER_INTERVAL_MS) {
    lastRenderMs = now;
    renderCurrentPage();
  }

  delay(5);  // small yield; keeps the button poll responsive without busy-spin
}
