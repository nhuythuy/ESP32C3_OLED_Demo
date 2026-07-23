#pragma once
#include <Arduino.h>
#include <DHT.h>

#include "display.h"

// DHT11 temperature/humidity sensor: data + its display page, in one file.

// DHT11 data pin. GPIO4 is a safe general-purpose pin on the ESP32-C3: it is
// not a strapping pin (unlike GPIO2/8/9) and is clear of the I2C bus on
// GPIO5/6 and the BOOT button on GPIO9. Change this if you wire it elsewhere.
#define DHT_PIN 4
#define DHT_TYPE DHT11

// The DHT11 can only be sampled about once every 1-2 s; we cache between reads.
static const unsigned long DHT_MIN_INTERVAL_MS = 2000;

// Result of a DHT11 read. `valid` is false when the sensor is missing,
// disconnected, or the read failed a checksum/timeout check -- callers use this
// to decide whether to show real values or an "N/A" placeholder.
struct DhtReading {
  bool valid;
  float temperatureC;
  float humidity;
};

static DHT dht(DHT_PIN, DHT_TYPE);

// Initialize the DHT11 sensor. Call once from setup().
inline void dhtBegin() {
  dht.begin();
}

// Return the latest reading. Internally throttled to the DHT11's minimum
// sampling interval, so callers may call this as often as they like and get the
// most recent cached sample between real reads.
inline DhtReading dhtRead() {
  static DhtReading last = {false, 0.0f, 0.0f};
  static unsigned long lastReadMs = 0;
  static bool firstRead = true;

  unsigned long now = millis();
  if (firstRead || (now - lastReadMs) >= DHT_MIN_INTERVAL_MS) {
    firstRead = false;
    lastReadMs = now;

    float h = dht.readHumidity();
    float t = dht.readTemperature();  // degrees Celsius
    if (isnan(h) || isnan(t)) {
      last.valid = false;  // no response / bad checksum -> treat as N/A
    } else {
      last.valid = true;
      last.temperatureC = t;
      last.humidity = h;
    }
  }
  return last;
}

// Page: DHT11 temperature & humidity. Valid readings show solid; when the
// sensor is not connected, "N/A" blinks (~1 Hz) to flag the error. This panel
// is monochrome (SSD1306) so we can't use colour -- blinking is the alarm cue.
inline void renderDhtPage() {
  DhtReading r = dhtRead();

  // Header stays solid so the page is always identifiable.
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
    // Blink on for 500 ms, off for 500 ms (driven by millis(), non-blocking).
    bool visible = (millis() / 500) % 2 == 0;
    if (visible) {
      drawCentered("T: N/A", 24);
      drawCentered("H: N/A", 38);
    }
  }
}
