#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <time.h>

// I2C pins for 0.42" ESP32-C3 Board
#define SDA_PIN 5
#define SCL_PIN 6

// WiFi credentials
static const char *WIFI_SSID = "WiFi-Guest";  // Replace with your WiFi SSID
static const char *WIFI_PASS = "WifiPass!";

// NTP configuration.
// TZ string for Europe/Oslo: CET (UTC+1) with CEST (UTC+2) DST, switching on
// the last Sunday of March and October. This lets the C library handle DST for
// us so the displayed time is always correct local time.
static const char *TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";
static const char *NTP_1 = "pool.ntp.org";
static const char *NTP_2 = "time.nist.gov";

// Hardware I2C Constructor specifically tailored for the 0.42" 72x40 SSD1306 screen
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL_PIN, /* data=*/ SDA_PIN);

// Draws a string horizontally centered on the 72px-wide panel at baseline y.
static void drawCentered(const char *s, int y) {
  int w = u8g2.getStrWidth(s);
  int x = (72 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawStr(x, y, s);
}

// Shows a two-line status message (used while connecting / syncing).
static void showStatus(const char *line1, const char *line2) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  drawCentered(line1, 18);
  if (line2 && line2[0]) drawCentered(line2, 32);
  u8g2.sendBuffer();
}

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("Connecting to WiFi"));

  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    // Animate the waiting screen with a growing row of dots.
    char waiting[8] = {0};
    for (int i = 0; i < (dots % 4); i++) waiting[i] = '.';
    showStatus("WiFi", waiting);

    Serial.print('.');
    delay(500);
    dots++;
  }

  Serial.print(F("\nConnected, IP: "));
  Serial.println(WiFi.localIP());
  showStatus("WiFi OK", WiFi.localIP().toString().c_str());
  delay(1000);
}

static void syncTime() {
  configTzTime(TZ_INFO, NTP_1, NTP_2);
  showStatus("Syncing", "NTP...");
  Serial.print(F("Waiting for NTP time"));

  struct tm timeinfo;
  // getLocalTime blocks up to the timeout waiting for a valid (post-2016) time.
  while (!getLocalTime(&timeinfo, 1000)) {
    Serial.print('.');
    showStatus("Syncing", "NTP...");
  }

  Serial.println(F("\nTime synchronized."));
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n--- NTP Clock on 0.42\" OLED ---"));

  u8g2.begin();

  connectWiFi();
  syncTime();
}

void loop() {
  // Reconnect WiFi if the guest network dropped us; time keeps running on the
  // ESP32's internal RTC in the meantime, and re-sync happens automatically.
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    syncTime();
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    showStatus("No time", "");
    delay(500);
    return;
  }

  char timeStr[6];   // "HH:MM"
  char secStr[3];    // "SS"
  char dateStr[11];  // "DD.MM.YYYY"
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  strftime(secStr, sizeof(secStr), "%S", &timeinfo);
  strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);

  u8g2.clearBuffer();

  // Date across the top (small font).
  u8g2.setFont(u8g2_font_5x7_tf);
  drawCentered(dateStr, 8);

  // Big HH:MM in the middle.
  u8g2.setFont(u8g2_font_logisoso16_tn);
  int hmWidth = u8g2.getStrWidth(timeStr);
  int hmX = (72 - hmWidth) / 2;
  if (hmX < 0) hmX = 0;
  int hmBaseline = 30;
  u8g2.drawStr(hmX, hmBaseline, timeStr);

  // Seconds bottom-right (small font).
  u8g2.setFont(u8g2_font_5x7_tf);
  int secWidth = u8g2.getStrWidth(secStr);
  u8g2.drawStr(72 - secWidth, 39, secStr);

  u8g2.sendBuffer();
  delay(200);
}
