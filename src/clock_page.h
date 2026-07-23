#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "display.h"

// Clock page: shows an NTP-synced date + time. Owns its NTP/time-zone setup.

// NTP configuration.
// TZ string for Europe/Oslo: CET (UTC+1) with CEST (UTC+2) DST, switching on
// the last Sunday of March and October. This lets the C library handle DST for
// us so the displayed time is always correct local time.
static const char *TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";
static const char *NTP_1 = "pool.ntp.org";
static const char *NTP_2 = "time.nist.gov";

// Start NTP once, the first time WiFi comes up. Time then keeps running on the
// ESP32's internal RTC even if WiFi later drops. Call every loop from main.
inline void clockUpdate() {
  static bool ntpStarted = false;
  if (!ntpStarted && WiFi.status() == WL_CONNECTED) {
    configTzTime(TZ_INFO, NTP_1, NTP_2);
    ntpStarted = true;
    Serial.print(F("WiFi connected, IP: "));
    Serial.println(WiFi.localIP());
  }
}

// Page: NTP clock (date / big HH:MM / seconds). Before time syncs it shows the
// WiFi "searching" animation (if not connected) or a "Syncing NTP" notice.
inline void renderClockPage() {
  struct tm timeinfo;
  bool haveTime = getLocalTime(&timeinfo, 0);  // 0 ms: check once, don't block

  if (!haveTime) {
    u8g2.setFont(u8g2_font_6x10_tf);
    if (WiFi.status() != WL_CONNECTED) {
      // WiFi searching: animate a growing row of dots.
      char waiting[5] = {0};
      int dots = (millis() / 500) % 4;
      for (int i = 0; i < dots; i++) waiting[i] = '.';
      drawCentered("WiFi", 18);
      drawCentered(waiting, 32);
    } else {
      drawCentered("Syncing", 18);
      drawCentered("NTP...", 32);
    }
    return;
  }

  char timeStr[6];   // "HH:MM"
  char secStr[3];    // "SS"
  char dateStr[11];  // "DD.MM.YYYY"
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  strftime(secStr, sizeof(secStr), "%S", &timeinfo);
  strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);

  // Date across the top (small font).
  u8g2.setFont(u8g2_font_5x7_tf);
  drawCentered(dateStr, 8);

  // Big HH:MM in the middle.
  u8g2.setFont(u8g2_font_logisoso16_tn);
  int hmWidth = u8g2.getStrWidth(timeStr);
  int hmX = (72 - hmWidth) / 2;
  if (hmX < 0) hmX = 0;
  u8g2.drawStr(hmX, 30, timeStr);

  // Seconds bottom-right (small font).
  u8g2.setFont(u8g2_font_5x7_tf);
  int secWidth = u8g2.getStrWidth(secStr);
  u8g2.drawStr(72 - secWidth, 39, secStr);
}
