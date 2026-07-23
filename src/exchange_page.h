#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "display.h"

// Exchange-rate page: shows NOK->VND (N/V) and USD->NOK (U/N).
//
// The HTTPS GET blocks for a couple of seconds, so it runs in a background
// FreeRTOS task (never in loop()); the page just renders the latest cached
// values. Source: open.er-api.com -- free, no API key, includes VND, updates
// about once a day.

static const char *EXCHANGE_URL = "https://open.er-api.com/v6/latest/NOK";
static const unsigned long EXCHANGE_FETCH_MS = 30UL * 60UL * 1000UL;  // 30 min
static const unsigned long EXCHANGE_RETRY_MS = 5000;                  // after a failure

// Shared with the render code. Written only by the background task; a float and
// a bool are read/written atomically on this 32-bit core, and `valid` is set
// last, so the reader never sees half-updated rates. Once valid, we keep the
// last good values even if a later refresh fails.
static volatile bool exchangeValid = false;
static volatile float exchangeNokVnd = 0.0f;  // 1 NOK in VND
static volatile float exchangeUsdNok = 0.0f;  // 1 USD in NOK

// One blocking HTTPS fetch + parse. Runs only inside the background task.
static bool exchangeFetch() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("FX: WiFi down, skip"));
    return false;
  }

  Serial.print(F("FX: fetching (free heap "));
  Serial.print(ESP.getFreeHeap());
  Serial.println(F(")"));

  WiFiClientSecure client;
  client.setInsecure();  // skip cert validation (demo); pin the CA for production
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, EXCHANGE_URL)) {
    Serial.println(F("FX: begin() failed"));
    return false;
  }

  bool ok = false;
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    // Read the fully-decoded body. getString() handles chunked transfer
    // encoding (which open.er-api.com uses); parsing the raw stream would not.
    String body = http.getString();

    // Filter: keep only the two rates we need, so the parsed doc stays tiny
    // even though the full response lists ~160 currencies.
    JsonDocument filter;
    filter["result"] = true;
    filter["rates"]["VND"] = true;
    filter["rates"]["USD"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));

    if (err) {
      Serial.print(F("FX: JSON error: "));
      Serial.println(err.c_str());
    } else if (doc["result"] != "success") {
      Serial.println(F("FX: result != success"));
    } else {
      float vndPerNok = doc["rates"]["VND"] | 0.0f;
      float usdPerNok = doc["rates"]["USD"] | 0.0f;
      if (vndPerNok > 0.0f && usdPerNok > 0.0f) {
        exchangeNokVnd = vndPerNok;
        exchangeUsdNok = 1.0f / usdPerNok;
        exchangeValid = true;  // publish last -> reader sees a consistent pair
        ok = true;
        Serial.print(F("FX: N/V="));
        Serial.print(exchangeNokVnd);
        Serial.print(F(" U/N="));
        Serial.println(exchangeUsdNok);
      }
    }
  } else {
    Serial.print(F("FX: HTTP GET failed, code="));
    Serial.println(code);
  }
  http.end();
  return ok;
}

// Background task: fetch once WiFi is up, then refresh every EXCHANGE_FETCH_MS.
static void exchangeTask(void *arg) {
  unsigned long lastFetch = 0;
  bool everFetched = false;
  for (;;) {
    unsigned long now = millis();
    if (!everFetched || (now - lastFetch) >= EXCHANGE_FETCH_MS) {
      if (exchangeFetch()) {
        lastFetch = now;
        everFetched = true;
        Serial.println(F("FX: rates updated"));
      } else {
        vTaskDelay(pdMS_TO_TICKS(EXCHANGE_RETRY_MS));  // offline/failed -> retry soon
        continue;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Start the background fetch task. Call once from setup(). The generous stack
// covers the TLS handshake, which is stack-hungry.
inline void exchangeBegin() {
  xTaskCreate(exchangeTask, "exchange", 16384, nullptr, 1, nullptr);
}

// Page: exchange rates. N/V = NOK->VND, U/N = USD->NOK. Blinks "--" until the
// first successful fetch (the monochrome "no data" cue).
inline void renderExchangePage() {
  u8g2.setFont(u8g2_font_5x7_tf);
  drawCentered("FX", 8);

  u8g2.setFont(u8g2_font_6x10_tf);
  char line[16];
  if (exchangeValid) {
    snprintf(line, sizeof(line), "N/V: %.0f", exchangeNokVnd);
    drawCentered(line, 24);
    snprintf(line, sizeof(line), "U/N: %.2f", exchangeUsdNok);
    drawCentered(line, 38);
  } else if ((millis() / 500) % 2 == 0) {
    drawCentered("N/V: --", 24);
    drawCentered("U/N: --", 38);
  }
}
