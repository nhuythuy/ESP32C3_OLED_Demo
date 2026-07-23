#pragma once
#include <Arduino.h>
#include <WiFi.h>

// Non-blocking multi-AP WiFi manager. Tries each access point in WIFI_APS in
// turn, giving each WIFI_ATTEMPT_MS to associate before moving on, and keeps
// cycling until one connects. It never blocks, so the display keeps switching
// pages and the BOOT button stays responsive while it searches.

struct WifiCred {
  const char *ssid;
  const char *pass;
};

// Access points to try, in order -- put your most-likely networks first. Add or
// remove entries freely; the manager adapts to the array size. Leave a row's
// ssid empty ("") to skip it, so unused template rows cost no connection time.
static const WifiCred WIFI_APS[] = {
  {"Moen Marin Guest", "WifiPass!"},  // <- replace with your networks
  {"BNHOME_2G", "PlanetEarth"},
  {"", ""},
};
static const size_t WIFI_AP_COUNT = sizeof(WIFI_APS) / sizeof(WIFI_APS[0]);

// How long to wait for one AP to connect before trying the next.
static const unsigned long WIFI_ATTEMPT_MS = 8000;

static size_t wifiIdx = 0;
static unsigned long wifiAttemptMs = 0;

// Next AP after `from` (wrapping) whose SSID is non-empty, so blank template
// rows are skipped instead of wasting an attempt on them.
static size_t wifiNextValid(size_t from) {
  for (size_t k = 1; k <= WIFI_AP_COUNT; k++) {
    size_t i = (from + k) % WIFI_AP_COUNT;
    if (WIFI_APS[i].ssid[0] != '\0') return i;
  }
  return from;  // nothing valid; keep the current index
}

static void wifiStartAttempt(size_t idx) {
  wifiIdx = idx;
  wifiAttemptMs = millis();
  Serial.print(F("WiFi: connecting to \""));
  Serial.print(WIFI_APS[idx].ssid);
  Serial.println(F("\"..."));
  WiFi.begin(WIFI_APS[idx].ssid, WIFI_APS[idx].pass);
}

// Start connecting. Call once from setup().
inline void wifiBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // this manager owns (re)connection, not the stack
  size_t first = (WIFI_APS[0].ssid[0] != '\0') ? 0 : wifiNextValid(0);
  wifiStartAttempt(first);
}

// Drive the connection. Call every loop from main. Non-blocking: while
// disconnected, once the current attempt times out it moves to the next AP
// (this also re-establishes the link if a connected AP later drops).
inline void wifiUpdate() {
  if (WiFi.status() == WL_CONNECTED) return;  // connected; nothing to do
  if ((millis() - wifiAttemptMs) >= WIFI_ATTEMPT_MS) {
    wifiStartAttempt(wifiNextValid(wifiIdx));
  }
}
