#pragma once
#include <Arduino.h>

// Result of a DHT11 read. `valid` is false when the sensor is missing,
// disconnected, or the read failed a checksum/timeout check -- callers can use
// this to decide whether to show real values or an "N/A" placeholder.
struct DhtReading {
  bool valid;
  float temperatureC;
  float humidity;
};

// Initialize the DHT11 sensor. Call once from setup().
void dhtBegin();

// Return the latest reading. Internally throttled to the DHT11's minimum
// sampling interval (~2 s), so callers may call this as often as they like and
// will get the most recent cached sample between real reads.
DhtReading dhtRead();
