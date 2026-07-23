#include "dht_sensor.h"
#include <DHT.h>

// DHT11 data pin. GPIO4 is a safe general-purpose pin on the ESP32-C3: it is
// not a strapping pin (unlike GPIO2/8/9) and is clear of the I2C bus on
// GPIO5/6 and the BOOT button on GPIO9. Change this if you wire it elsewhere.
#define DHT_PIN 4
#define DHT_TYPE DHT11

// The DHT11 can only be sampled about once every 1-2 s; we cache between reads.
static const unsigned long DHT_MIN_INTERVAL_MS = 2000;

static DHT dht(DHT_PIN, DHT_TYPE);
static DhtReading lastReading = {false, 0.0f, 0.0f};
static unsigned long lastReadMs = 0;
static bool firstRead = true;

void dhtBegin() {
  dht.begin();
}

DhtReading dhtRead() {
  unsigned long now = millis();
  if (firstRead || (now - lastReadMs) >= DHT_MIN_INTERVAL_MS) {
    firstRead = false;
    lastReadMs = now;

    float h = dht.readHumidity();
    float t = dht.readTemperature();  // degrees Celsius

    if (isnan(h) || isnan(t)) {
      // No response / bad checksum -> treat as "not connected".
      lastReading.valid = false;
    } else {
      lastReading.valid = true;
      lastReading.temperatureC = t;
      lastReading.humidity = h;
    }
  }
  return lastReading;
}
