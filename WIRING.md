# FireBeetle ESP32 Wiring

Two display options are supported. Select the matching build environment in PlatformIO.

---

## 4.0" ILI9488 SPI (`env:ili9488_spi` — default)

480×320, SPI interface.

### ILI9488 power

| Display pin | Connect to |
| --- | --- |
| VDD | ESP32 3V3 |
| GND | ESP32 GND |
| BL (backlight) | ESP32 3V3 |

### ILI9488 SPI and control pins

| Display pin | ESP32 GPIO |
| --- | --- |
| CS | GPIO14 |
| RST | GPIO27 |
| D/C | GPIO13 |
| SDI (MOSI) | GPIO23 |
| SCK | GPIO18 |
| SDO (MISO) | not connected |

### ILI9488 notes

- BL has no PWM dimming — connecting directly to 3V3 keeps the backlight on at full brightness.
- SDO/MISO is not used; leave it unconnected.
- The display module may include a separate XPT2046 touch controller block (TCS, TCK, TDI, TDO, PEN). Leave all touch pins unconnected — touch is not used.

---

## 2.8" ILI9341 parallel (`env:ili9341_parallel`)

320×240, 8-bit parallel interface.

The AZDelivery listing says "SPI", but this shield layout (`LCD_D0..D7`, `LCD_WR`, `LCD_RD`) is a parallel TFT interface. Use jumper wires; this shield does not plug directly into ESP32 headers.

### ILI9341 power

| Shield pin | Connect to |
| --- | --- |
| GND | ESP32 GND |
| 5V | ESP32 5V / USB VIN |
| 3V3 | leave unconnected (power input, not used here) |

Do not use 5V logic on ESP32 signal pins.

### ILI9341 control and data pins

| Shield pin | ESP32 GPIO |
| --- | --- |
| LCD_CS | GPIO14 |
| LCD_RS (D/C) | GPIO13 |
| LCD_RST | GPIO27 |
| LCD_WR | GPIO22 |
| LCD_RD | ESP32 3V3 (fixed HIGH, write-only mode) |
| LCD_D0 | GPIO26 |
| LCD_D1 | GPIO25 |
| LCD_D2 | GPIO21 |
| LCD_D3 | GPIO23 |
| LCD_D4 | GPIO19 |
| LCD_D5 | GPIO18 |
| LCD_D6 | GPIO5 |
| LCD_D7 | GPIO15 |

### ILI9341 notes

- `LCD_RD` must be held HIGH at 3.3V. If the shield has no 3.3V pin, use ESP32 `3V3`.
- Avoid `GPIO0`/`GPIO2` on the data bus — they are boot-strapping pins and can cause reset loops.
- Avoid `GPIO1`/`GPIO3` (UART TX/RX) — flashing can fail with "serial TX path seems down".
- SD card pins on this shield are not configured.

---

## Optional reset button

Wire a button to clear saved Wi-Fi and Nord Pool settings:

- One side → configured `CONFIG_RESET_PIN`
- Other side → GND (if `CONFIG_RESET_ACTIVE_LEVEL=LOW`) or 3V3 (if `HIGH`)

The default `platformio.ini` sets `CONFIG_RESET_PIN=17` (active HIGH). Hold the button for 2 seconds during runtime or boot to clear all saved config and restart.

---

## General notes

- Pin assignments must match the `TFT_*` build flags in `platformio.ini`. If you change wiring, update both.
- `GPIO32`/`GPIO33` are not used because they are not exposed on many FireBeetle layouts.
- If the screen stays white, verify wiring continuity and that the correct build environment is selected.
