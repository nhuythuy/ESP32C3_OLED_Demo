#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// I2C pins for 0.42" ESP32-C3 Board
#define SDA_PIN 5
#define SCL_PIN 6

// Hardware I2C Constructor specifically tailored for the 0.42" 72x40 SSD1306 screen
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL_PIN, /* data=*/ SDA_PIN);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n--- Starting 0.42\" OLED Test ---"));

  // Initialize display
  u8g2.begin();

  // Clear internal memory
  u8g2.clearBuffer();

  // Set font (u8g2_font_ncenB08_tr is compact and fits 72x40)
  u8g2.setFont(u8g2_font_ncenB08_tr);

  // Draw text (keep X within 0..71, Y within 0..39)
  u8g2.drawStr(0, 12, "ESP32-C3");
  u8g2.drawStr(0, 26, "0.42 OLED");
  u8g2.drawStr(0, 38, "Working!");

  // Send buffer to OLED screen
  u8g2.sendBuffer();
  delay(5000);

  Serial.println(F("Screen refreshed successfully!"));
}

void loop() {
  // Simple frame blink or counter demo
  static int counter = 0;
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont11_tf);
  u8g2.drawStr(2, 12, "Count:");
  u8g2.setCursor(45, 12);
  u8g2.print(counter++);
  
  // Draw a small border box around the 72x40 area
  u8g2.drawFrame(0, 0, 72, 40);
  
  u8g2.sendBuffer();
  delay(1000);
}
