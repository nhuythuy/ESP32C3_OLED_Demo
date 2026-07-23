# ESP32-C3 OLED Demo — Project Conventions

Working conventions for this project. Follow them for new work so they don't
need restating each session.

## What this is

An **ESP32-C3** driving a **0.42" 72×40 SSD1306 OLED** (PlatformIO + Arduino
framework). It shows a multi-page display and publishes sensor data over MQTT:

- **Clock** page — NTP-synced date/time (Europe/Oslo).
- **DHT11** page — temperature / humidity.
- **Analog** page — two ADC input voltages.
- **MQTT** page — publish-rate stats (last minute / last day).
- Publishes DHT11 readings as JSON to a HiveMQ Cloud broker every 10 s.

## Build / flash / monitor

PlatformIO lives at `~/.platformio/penv/bin/pio`:

```bash
~/.platformio/penv/bin/pio run              # build
~/.platformio/penv/bin/pio run -t upload    # flash
~/.platformio/penv/bin/pio device monitor   # serial (115200)
```

**Always build after changing code** to confirm it compiles.

## Principles

### 1. One module per feature ("one file per task")

Each sensor / feature / page is its own file in `src/`, and a module owns **both
its data and its rendering**. Current modules:

| File | Responsibility |
|------|----------------|
| `main.cpp` | **Only** setup + global orchestration (WiFi, button, page-switch state machine). No drawing, no sensor I/O, no time formatting. |
| `display.h` | The `u8g2` panel object, I2C pins, `drawCentered()`, `displayBegin()`, `displayRenderPage()`, the `PageRenderFn` type. |
| `clock_page.h` | NTP setup + clock page. |
| `dht_sensor.h` | DHT11 data + its page. |
| `analog_page.h` | Analog inputs + their page. |
| `mqtt_report.h` | MQTT publishing (TLS to HiveMQ). |
| `mqtt_page.h` | Publish-rate stats + their page. |

### 2. Header-only, single translation unit

The toolchain is `-std=gnu++11` (no C++17 `inline` variables), so:

- The project compiles as a **single `.cpp`** (`main.cpp`). Every other module is
  a **header-only `.h`** included once (guarded by `#pragma once`).
- Globals (`u8g2`, `dht`, `mqttClient`, bucket arrays, …) are defined **directly
  in their headers**; free functions are marked `inline`.
- **Consequence:** do **not** add a second `.cpp` that includes these headers —
  it causes multiple-definition linker errors. If a second `.cpp` ever becomes
  necessary, move the offending global definitions into a `.cpp` first.

### 3. The page system

- A **page** is a `void (*PageRenderFn)()` that draws into an already-cleared
  `u8g2` buffer (type + frame helpers in `display.h`).
- The **active pages and their order** are the `PAGES[]` array in `main.cpp`.
- **To add a page:** (1) create a module exposing a `renderXxxPage()` (plus its
  data), then (2) append it to `PAGES[]`. Auto-switching, the BOOT button, and
  the idle timeout then handle it automatically — nothing else changes.

### 4. Page module shape (mirror the existing ones)

- `xxxBegin()` — one-time init, called from `setup()`.
- A data accessor returning a struct (e.g. `dhtRead()` → `{valid, …}`).
- `renderXxxPage()` — draws the page.
- Optional `xxxUpdate()` — periodic servicing, called every loop from `main`
  (e.g. `clockUpdate()`, `mqttReportUpdate()`).

### 5. Display conventions

- The panel is **monochrome** (SSD1306): pixels are on/off only, **no color**.
  Color requests are not physically possible — use **blinking, inversion, or a
  box** for emphasis instead.
- Layout: header in `u8g2_font_5x7_tf` at baseline `y=8`; two value lines in
  `u8g2_font_6x10_tf` at `y=24` and `y=38`; the big clock uses
  `u8g2_font_logisoso16_tn`. Keep labels ≲12 chars so they fit 72 px.
- Use `drawCentered()` for centering. `main` renders via `displayRenderPage()`
  and never touches `u8g2` directly.

### 6. Non-blocking main loop

- **Never block** in `loop()`. Use `millis()`-based timing, no long `delay()`.
- WiFi connects in the background so pages keep switching while it connects.
- Rendering is throttled to `RENDER_INTERVAL_MS`; pages auto-advance every
  `AUTO_SWITCH_MS`; the manual hold times out after `MANUAL_TIMEOUT_MS`.

### 7. Blinking = the monochrome "alarm" cue

Error / not-connected states blink at ~1 Hz via `(millis() / 500) % 2`.
Examples: DHT11 shows a blinking `N/A`; the MQTT page blinks its header and
shows `Min: 0 / Day: 0` when the broker is disconnected.

### 8. BOOT button

- GPIO9, active-low, `INPUT_PULLUP`, debounced falling-edge detection.
- First press pauses auto-switching on the current page; each further press
  advances one page; 10 s of inactivity resumes auto-switching.

### 9. Pin map (ESP32-C3)

| Pin | Use |
|-----|-----|
| GPIO5 / GPIO6 | OLED I2C SDA / SCL |
| GPIO9 | BOOT button |
| GPIO4 | DHT11 data |
| GPIO0 / GPIO1 | Analog inputs A0 / A1 (ADC1_CH0 / CH1) |

- Avoid strapping pins **GPIO2 / GPIO8 / GPIO9** for new peripherals.
- Only **ADC1 (GPIO0–GPIO4)** works while WiFi is on — ADC2 is claimed by the radio.

### 10. ADC

Use `ADC_11db` attenuation (~0–2.5 V, the C3's hardware maximum) with
`analogReadMilliVolts()` for factory-calibrated volts. Higher inputs need an
external divider; never exceed ~3.3 V on a pin.

### 11. MQTT / networking

- HiveMQ Cloud requires **TLS (port 8883)** and **username/password** auth
  (no anonymous). Uses `WiFiClientSecure` + `PubSubClient`, publishing JSON
  every 10 s to topic `Node1/Location1`.
- `setInsecure()` (skip cert validation) is used for the demo; pin HiveMQ's CA
  with `setCACert(...)` for production.

### 12. Dependencies

Add libraries to `platformio.ini` under `lib_deps`.

## ⚠️ Credentials

WiFi credentials live in `main.cpp` and MQTT credentials in `mqtt_report.h`, as
hardcoded `static const char *` values. **These are secrets** — this repo has a
remote, so avoid committing real values. Prefer placeholders in git and keep
real credentials local (or in a git-ignored config header).
