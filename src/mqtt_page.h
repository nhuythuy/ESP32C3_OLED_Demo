#pragma once
#include <Arduino.h>

#include "display.h"

// MQTT page: shows how many payloads were published in the last minute (M) and
// the last day (D). Same principle as the other page modules -- state + render
// in one file. mqtt_report.h calls mqttStatsRecordPublish() after each
// successful publish; this module keeps the sliding-window counts and draws it.

// Sliding windows built from time buckets with lazy reset:
//   - minute: 60 buckets x 1 second   = last 60 s
//   - day:    1440 buckets x 1 minute = last 24 h
// On each publish we bump the bucket for "now"; when we revisit a bucket in a
// later window we reset it first, so stale counts drop off automatically. When
// summing, a bucket only counts while its timestamp is still inside the window.
// Time base is millis(); counts glitch briefly if millis() wraps (~49 days up).
static const uint32_t MQTT_SEC_BUCKETS = 60;
static const uint32_t MQTT_MIN_BUCKETS = 1440;

static uint16_t mqttSecCount[MQTT_SEC_BUCKETS];
static uint32_t mqttSecStamp[MQTT_SEC_BUCKETS];  // second-index held by bucket
static uint16_t mqttMinCount[MQTT_MIN_BUCKETS];
static uint32_t mqttMinStamp[MQTT_MIN_BUCKETS];  // minute-index held by bucket

// Broker connection status, pushed in by mqtt_report.h each loop. When false,
// the page blinks its header and shows zero counts to flag "not reporting".
static bool mqttConnectedFlag = false;
inline void mqttStatsSetConnected(bool connected) { mqttConnectedFlag = connected; }

// Record one published payload at the current time.
inline void mqttStatsRecordPublish() {
  uint32_t nowSec = millis() / 1000UL;
  uint32_t nowMin = nowSec / 60UL;

  uint32_t si = nowSec % MQTT_SEC_BUCKETS;
  if (mqttSecStamp[si] != nowSec) {  // bucket belongs to an older second -> reset
    mqttSecStamp[si] = nowSec;
    mqttSecCount[si] = 0;
  }
  mqttSecCount[si]++;

  uint32_t mi = nowMin % MQTT_MIN_BUCKETS;
  if (mqttMinStamp[mi] != nowMin) {  // bucket belongs to an older minute -> reset
    mqttMinStamp[mi] = nowMin;
    mqttMinCount[mi] = 0;
  }
  mqttMinCount[mi]++;
}

// Payloads published in the last 60 seconds. (Empty buckets hold count 0, so
// including a stale-but-empty one in the sum is harmless.)
inline uint32_t mqttStatsLastMinute() {
  uint32_t nowSec = millis() / 1000UL;
  uint32_t sum = 0;
  for (uint32_t i = 0; i < MQTT_SEC_BUCKETS; i++) {
    if ((nowSec - mqttSecStamp[i]) < MQTT_SEC_BUCKETS) sum += mqttSecCount[i];
  }
  return sum;
}

// Payloads published in the last 24 hours.
inline uint32_t mqttStatsLastDay() {
  uint32_t nowMin = (millis() / 1000UL) / 60UL;
  uint32_t sum = 0;
  for (uint32_t i = 0; i < MQTT_MIN_BUCKETS; i++) {
    if ((nowMin - mqttMinStamp[i]) < MQTT_MIN_BUCKETS) sum += mqttMinCount[i];
  }
  return sum;
}

// Page: MQTT publish counts -- Min = last minute, Day = last day. When the
// broker connection is down, the "MQTT" header blinks (~1 Hz) and both counts
// read 0 to make "not reporting" obvious.
inline void renderMqttPage() {
  bool connected = mqttConnectedFlag;

  // Header: solid when connected, blinking when the connection failed.
  bool showHeader = connected || ((millis() / 500) % 2 == 0);
  u8g2.setFont(u8g2_font_5x7_tf);
  if (showHeader) drawCentered("MQTT", 8);

  uint32_t minCount = connected ? mqttStatsLastMinute() : 0;
  uint32_t dayCount = connected ? mqttStatsLastDay() : 0;

  u8g2.setFont(u8g2_font_6x10_tf);
  char line[16];
  snprintf(line, sizeof(line), "Min: %lu", (unsigned long)minCount);
  drawCentered(line, 24);
  snprintf(line, sizeof(line), "Day: %lu", (unsigned long)dayCount);
  drawCentered(line, 38);
}
