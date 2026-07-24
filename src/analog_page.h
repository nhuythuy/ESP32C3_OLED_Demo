#pragma once
#include <Arduino.h>

#include "display.h"

// Analog-input page: reads the first two available ADC channels and shows their
// voltage.
//
// On the ESP32-C3 only ADC1 (GPIO0..GPIO4) is usable while WiFi is running --
// ADC2 is claimed by the radio. GPIO4 is used by the DHT11, so the first two
// free analog inputs are ADC1_CH0 and ADC1_CH1 on GPIO0 and GPIO1.
#define AIN0_PIN 0  // ADC1_CH0
#define AIN1_PIN 1  // ADC1_CH1

// Input range: ADC_11db is the widest attenuation and on the ESP32-C3 spans
// roughly 0..2500 mV, i.e. 2.5 V full scale. That covers the requested 2.5 V
// ceiling and is also this chip's hardware maximum -- reading higher voltages
// would need an external resistor divider.
#define AIN_ATTEN ADC_11db

struct AnalogReading {
  float volts0;
  float volts1;
};

// Configure the ADC. Call once from setup().
inline void analogBegin() {
  analogReadResolution(12);                     // raw range 0..4095
  analogSetPinAttenuation(AIN0_PIN, AIN_ATTEN); // ~0..2.5 V
  analogSetPinAttenuation(AIN1_PIN, AIN_ATTEN);
}

// Read both channels as calibrated volts. analogReadMilliVolts() applies the
// chip's factory calibration, so the result is real millivolts rather than raw
// counts.
inline AnalogReading analogReadChannels() {
  AnalogReading r;
  r.volts0 = analogReadMilliVolts(AIN0_PIN) / 1000.0f;
  r.volts1 = analogReadMilliVolts(AIN1_PIN) / 1000.0f;
  return r;
}

// Page: the two analog input voltages.
inline void renderAnalogPage() {
  AnalogReading r = analogReadChannels();

  drawTitle("Analog");

  u8g2.setFont(u8g2_font_6x10_tf);
  char line[16];
  snprintf(line, sizeof(line), "A0: %.2f V", r.volts0);
  drawCentered(line, 24);
  snprintf(line, sizeof(line), "A1: %.2f V", r.volts1);
  drawCentered(line, 38);
}
