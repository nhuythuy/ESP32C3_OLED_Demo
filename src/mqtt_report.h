#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "dht_sensor.h"
#include "mqtt_page.h"

// Reports the DHT11 temperature/humidity as JSON to a HiveMQ Cloud broker every
// MQTT_PUBLISH_MS. Kept separate from the sensor/display code.

// --- Broker (from the cluster's "Connection Details") ----------------------
// HiveMQ Cloud Free only offers TLS, so we connect on port 8883.
static const char *MQTT_HOST =
    ".s1.eu.hivemq.cloud";
static const uint16_t MQTT_PORT = 8883;

// --- Credentials -----------------------------------------------------------
// HiveMQ Cloud requires username/password auth (no anonymous access). Create a
// credential under the cluster's "Access Management" tab and paste it here.
// Until you do, the broker will refuse the connection.
static const char *MQTT_USER = "----";
static const char *MQTT_PASS = "passHiveMQ";

static const char *MQTT_TOPIC = "Node1/Location1";
static const char *MQTT_CLIENT_ID = "esp32c3-node1";
static const unsigned long MQTT_PUBLISH_MS = 10000;  // publish every 10 s

static WiFiClientSecure mqttTlsClient;
static PubSubClient mqttClient(mqttTlsClient);

// Configure the MQTT client. Call once from setup().
inline void mqttReportBegin() {
  // setInsecure() skips TLS certificate validation -- simplest for a demo. For
  // production, load HiveMQ's CA chain with mqttTlsClient.setCACert(...) instead.
  mqttTlsClient.setInsecure();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
}

// Connect to the broker if we are online but not yet connected. The TLS
// handshake blocks for ~1-2 s; we only attempt it on the publish tick, so it
// never runs more than once per MQTT_PUBLISH_MS.
static bool mqttEnsureConnected() {
  if (mqttClient.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  Serial.print(F("MQTT connecting... "));
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    Serial.println(F("connected"));
    return true;
  }
  Serial.print(F("failed, state="));  // see PubSubClient state() codes
  Serial.println(mqttClient.state());
  return false;
}

// Call every loop. Keeps the connection alive and publishes a JSON reading
// every MQTT_PUBLISH_MS, e.g. {"temperature":23.4,"humidity":45}. When the
// sensor is not connected the fields are published as null.
inline void mqttReportUpdate() {
  mqttClient.loop();  // service keepalive/ACKs -- must run frequently
  mqttStatsSetConnected(mqttClient.connected());  // drives the MQTT page display

  static unsigned long lastPublishMs = 0;
  unsigned long now = millis();
  if ((now - lastPublishMs) < MQTT_PUBLISH_MS) return;
  lastPublishMs = now;

  if (!mqttEnsureConnected()) return;  // retry on the next tick

  DhtReading r = dhtRead();
  char payload[64];
  if (r.valid) {
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.1f,\"humidity\":%.0f}",
             r.temperatureC, r.humidity);
  } else {
    snprintf(payload, sizeof(payload),
             "{\"temperature\":null,\"humidity\":null}");
  }

  bool ok = mqttClient.publish(MQTT_TOPIC, payload);
  if (ok) mqttStatsRecordPublish();  // feeds the MQTT stats page
  Serial.print(F("MQTT publish "));
  Serial.print(ok ? F("OK: ") : F("FAILED: "));
  Serial.println(payload);
}
