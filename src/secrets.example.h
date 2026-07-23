#pragma once
#include <Arduino.h>

// TEMPLATE for src/secrets.h (which is git-ignored). Copy this file and fill in
// your real WiFi and MQTT credentials:
//   cp src/secrets.example.h src/secrets.h
// It is included by wifi_manager.h and mqtt_report.h.

// --- WiFi access points to try, most-likely first. Blank ssid "" = skip. ---
struct WifiCred {
  const char *ssid;
  const char *pass;
};
static const WifiCred WIFI_APS[] = {
  {"YourSSID", "YourPassword"},
  {"", ""},
  {"", ""},
};
static const size_t WIFI_AP_COUNT = sizeof(WIFI_APS) / sizeof(WIFI_APS[0]);

// --- HiveMQ Cloud broker + credentials (from the Access Management tab) -----
static const char *MQTT_HOST = "your-cluster.s1.eu.hivemq.cloud";
static const char *MQTT_USER = "your-mqtt-username";
static const char *MQTT_PASS = "your-mqtt-password";
