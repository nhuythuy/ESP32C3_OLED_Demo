#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include "display.h"

// Weather page (Open-Meteo -- free, no API key). Shows today's sunrise & sunset
// plus the next precipitation event today: "Rain HH:00" or "Snow HH:00". If no
// rain/snow is left today, shows "Cloudy" or "Sunny" from the current cloud
// cover. Like the FX page, the blocking HTTPS GET runs in a background task.

// Location -- default Oslo, matching the clock's Europe/Oslo timezone. If you
// change this, keep the timezone= parameter below in sync with the device tz,
// so the hourly forecast lines up with the local clock.
#define WEATHER_LAT "59.9139"
#define WEATHER_LON "10.7522"

static const char *WEATHER_URL =
    "https://api.open-meteo.com/v1/forecast?latitude=" WEATHER_LAT
    "&longitude=" WEATHER_LON
    "&daily=sunrise,sunset&hourly=precipitation,snowfall,cloud_cover"
    "&timezone=Europe%2FOslo&forecast_days=1";

static const unsigned long WEATHER_FETCH_MS = 15UL * 60UL * 1000UL;  // 15 min
static const unsigned long WEATHER_RETRY_MS = 5000;                  // after a failure

static const float WEATHER_RAIN_TH = 0.1f;  // mm/h below which it's "dry"
static const float WEATHER_SNOW_TH = 0.1f;  // cm/h below which it's "dry"
static const int WEATHER_CLOUDY_PCT = 50;   // >= this cloud cover -> "Cloudy"

enum WeatherKind { WX_NONE = 0, WX_RAIN, WX_SNOW, WX_SUNNY, WX_CLOUDY };

// Shared with render. Written by the background task; scalars only (atomic on
// this 32-bit core), and weatherValid is set last so the reader never sees a
// half-updated set. Once valid, last-known values persist even if a refresh fails.
static volatile bool weatherValid = false;
static volatile int weatherSunriseMin = 0;  // minutes since local midnight
static volatile int weatherSunsetMin = 0;
static volatile int weatherKind = WX_NONE;
static volatile int weatherHour = 0;  // hour of the next precip (WX_RAIN/WX_SNOW)

// Minutes-since-midnight from an ISO datetime like "2026-07-24T04:39".
static int isoTimeToMinutes(const char *iso) {
  const char *t = strchr(iso, 'T');
  if (!t) return 0;
  return atoi(t + 1) * 60 + atoi(t + 4);
}

// One blocking HTTPS fetch + parse. Runs only inside the background task.
static bool weatherFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;

  struct tm nowTm;
  if (!getLocalTime(&nowTm, 0)) return false;  // need the clock to know "next"

  Serial.print(F("WX: fetching (free heap "));
  Serial.print(ESP.getFreeHeap());
  Serial.println(F(")"));

  WiFiClientSecure client;
  client.setInsecure();  // skip cert validation (demo); pin the CA for production
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, WEATHER_URL)) return false;

  bool ok = false;
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String body = http.getString();  // decode chunked before parsing

    JsonDocument filter;
    filter["daily"]["sunrise"] = true;
    filter["daily"]["sunset"] = true;
    filter["hourly"]["precipitation"] = true;
    filter["hourly"]["snowfall"] = true;
    filter["hourly"]["cloud_cover"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
      Serial.print(F("WX: JSON error: "));
      Serial.println(err.c_str());
    } else {
      const char *sr = doc["daily"]["sunrise"][0] | "";
      const char *ss = doc["daily"]["sunset"][0] | "";
      JsonArray precip = doc["hourly"]["precipitation"];
      JsonArray snow = doc["hourly"]["snowfall"];
      JsonArray cloud = doc["hourly"]["cloud_cover"];

      if (sr[0] && ss[0] && !precip.isNull()) {
        weatherSunriseMin = isoTimeToMinutes(sr);
        weatherSunsetMin = isoTimeToMinutes(ss);

        // Next precipitation hour from the current hour through 23:00 today.
        // (hourly index i == hour i, since the API is queried in local time.)
        int kind = WX_NONE, hh = 0;
        for (int i = nowTm.tm_hour; i < (int)precip.size() && i < 24; i++) {
          if ((snow[i] | 0.0f) >= WEATHER_SNOW_TH) { kind = WX_SNOW; hh = i; break; }
          if ((precip[i] | 0.0f) >= WEATHER_RAIN_TH) { kind = WX_RAIN; hh = i; break; }
        }
        if (kind == WX_NONE) {  // dry rest of today -> current sky
          kind = ((cloud[nowTm.tm_hour] | 0) >= WEATHER_CLOUDY_PCT) ? WX_CLOUDY : WX_SUNNY;
        }

        weatherKind = kind;
        weatherHour = hh;
        weatherValid = true;  // publish last
        ok = true;
        Serial.print(F("WX: rise=")); Serial.print(weatherSunriseMin);
        Serial.print(F(" set=")); Serial.print(weatherSunsetMin);
        Serial.print(F(" kind=")); Serial.print(kind);
        Serial.print(F(" nextHour=")); Serial.println(hh);
      }
    }
  } else {
    Serial.print(F("WX: HTTP GET failed, code="));
    Serial.println(code);
  }
  http.end();
  return ok;
}

// Background task: fetch once WiFi + clock are ready, then every WEATHER_FETCH_MS.
static void weatherTask(void *arg) {
  unsigned long lastFetch = 0;
  bool ever = false;
  for (;;) {
    unsigned long nowMs = millis();
    if (!ever || (nowMs - lastFetch) >= WEATHER_FETCH_MS) {
      if (weatherFetch()) {
        lastFetch = nowMs;
        ever = true;
        Serial.println(F("WX: updated"));
      } else {
        vTaskDelay(pdMS_TO_TICKS(WEATHER_RETRY_MS));
        continue;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Start the background fetch task. Call once from setup().
inline void weatherBegin() {
  xTaskCreate(weatherTask, "weather", 16384, nullptr, 1, nullptr);
}

// Page: sunrise / sunset / next-precip-or-current-sky. Blinks until first fetch.
inline void renderWeatherPage() {
  if (!weatherValid) {
    if ((millis() / 500) % 2 == 0) drawTitle("Weather");
    return;
  }

  // Weather status as the page title.
  char line[16];
  switch (weatherKind) {
    case WX_RAIN:   snprintf(line, sizeof(line), "Rain %02d:00", weatherHour); break;
    case WX_SNOW:   snprintf(line, sizeof(line), "Snow %02d:00", weatherHour); break;
    case WX_CLOUDY: snprintf(line, sizeof(line), "Cloudy"); break;
    case WX_SUNNY:  snprintf(line, sizeof(line), "Sunny"); break;
    default:        snprintf(line, sizeof(line), "--"); break;
  }
  drawTitle(line);

  // Sunrise / sunset below.
  u8g2.setFont(u8g2_font_6x10_tf);
  snprintf(line, sizeof(line), "Rise %02d:%02d",
           weatherSunriseMin / 60, weatherSunriseMin % 60);
  drawCentered(line, 24);
  snprintf(line, sizeof(line), "Set  %02d:%02d",
           weatherSunsetMin / 60, weatherSunsetMin % 60);
  drawCentered(line, 38);
}
