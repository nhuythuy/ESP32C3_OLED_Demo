#include <Arduino.h>
#include <WiFi.h>

#include "display.h"
#include "clock_page.h"
#include "dht_sensor.h"
#include "analog_page.h"
#include "mqtt_page.h"
#include "mqtt_report.h"

// WiFi credentials
static const char *WIFI_SSID = "WiFi-Guest";  // Replace with your WiFi SSID
static const char *WIFI_PASS = "WifiPass!";

// BOOT button ("BO0"/IO9) on the ESP32-C3. Active low: pressed pulls the line
// to GND, so we enable the internal pull-up and treat LOW as "pressed".
#define BOOT_BTN_PIN 9

// Page-switching timing.
static const unsigned long AUTO_SWITCH_MS = 3000;     // auto-advance interval
static const unsigned long MANUAL_TIMEOUT_MS = 10000; // idle -> back to auto
static const unsigned long RENDER_INTERVAL_MS = 250;  // screen refresh cadence
static const unsigned long DEBOUNCE_MS = 40;          // button debounce window

// The ordered list of pages. To add a sensor page: create a module exposing a
// render function (see dht_sensor.h) and append it here -- auto-switching, the
// BOOT button and the idle timeout then handle the new page automatically.
static const PageRenderFn PAGES[] = {
  renderClockPage,
  renderDhtPage,
  renderAnalogPage,
  renderMqttPage,
};
static const size_t PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);

// Page-switching state machine.
// autoMode == true : pages advance on their own every AUTO_SWITCH_MS.
// autoMode == false: paused on the current page; each BOOT press advances one
//                    page. After MANUAL_TIMEOUT_MS with no press we return to
//                    auto mode.
static size_t currentPage = 0;
static bool autoMode = true;
static unsigned long lastButtonMs = 0;
static unsigned long lastAutoSwitchMs = 0;
static unsigned long lastRenderMs = 0;

static void nextPage() {
  currentPage = (currentPage + 1) % PAGE_COUNT;
}

// BOOT button: debounced falling-edge detector. Returns true exactly once per
// physical press.
static bool buttonPressed() {
  static int lastRaw = HIGH;
  static int stable = HIGH;
  static unsigned long lastChangeMs = 0;

  int raw = digitalRead(BOOT_BTN_PIN);
  unsigned long now = millis();

  if (raw != lastRaw) {
    lastRaw = raw;
    lastChangeMs = now;
  }
  // Accept the new level once it has been stable past the debounce window.
  if ((now - lastChangeMs) >= DEBOUNCE_MS && raw != stable) {
    stable = raw;
    if (stable == LOW) return true;  // just pressed
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n--- NTP Clock + DHT11 on 0.42\" OLED ---"));

  displayBegin();
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  dhtBegin();
  analogBegin();
  mqttReportBegin();

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

  clockUpdate();       // kicks off NTP once WiFi is up
  mqttReportUpdate();  // publishes temp/humidity JSON every 10 s

  // 1. Handle the BOOT button.
  if (buttonPressed()) {
    if (autoMode) {
      autoMode = false;  // first press: stop auto-switching, hold current page
    } else {
      nextPage();        // subsequent presses: advance one page
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
    displayRenderPage(PAGES[currentPage]);
  }

  delay(5);  // small yield; keeps the button poll responsive without busy-spin
}
