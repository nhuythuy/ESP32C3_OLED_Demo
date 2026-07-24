#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// Display module for the 0.42" 72x40 SSD1306 OLED on hardware I2C.
//
// NOTE: these page/sensor modules are header-only and assume a single
// translation unit (this project compiles just main.cpp). Globals like the
// display object are defined here so all display code lives in one file; if you
// ever split the build into several .cpp files, include this header from only
// one of them (or move the definitions into a .cpp).

// I2C pins for the 0.42" ESP32-C3 board.
#define SDA_PIN 5
#define SCL_PIN 6

// A page is a function that draws one screen's worth of content into the
// (already-cleared) buffer. See renderClockPage() / renderDhtPage().
typedef void (*PageRenderFn)();

// The panel. Hardware I2C constructor tailored to the 0.42" 72x40 module.
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE,
                                    /* clock=*/ SCL_PIN, /* data=*/ SDA_PIN);

// Initialize the display. Call once from setup().
inline void displayBegin() {
  u8g2.begin();
}

// Draw a string horizontally centered on the 72px-wide panel at baseline y.
inline void drawCentered(const char *s, int y) {
  int w = u8g2.getStrWidth(s);
  int x = (72 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawStr(x, y, s);
}

// Draw a page title: bold and larger than the body text, centered in the top
// band at a fixed baseline. Centralized here so every page's title looks the
// same -- change the font/position once to restyle all page titles.
inline void drawTitle(const char *s) {
  u8g2.setFont(u8g2_font_7x13B_tf);
  int w = u8g2.getStrWidth(s);
  int x = (72 - w) / 2;
  if (x < 0) x = 0;
  u8g2.drawStr(x, 11, s);
}

// Render one page as a complete frame (clear -> draw -> flush to the panel).
inline void displayRenderPage(PageRenderFn render) {
  u8g2.clearBuffer();
  render();
  u8g2.sendBuffer();
}
