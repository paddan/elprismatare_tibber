# nordpool-price-display

ESP32 electricity price display for Nord Pool.

This project runs on a FireBeetle ESP32 and shows:
- Current price as large text (`#.## SEK/NOK/EUR/...`) with color based on price level.
- Price bars for today + tomorrow at 15/30/60-minute resolution.
- Current interval with a white downward arrow marker.
- A red error banner if the last Nord Pool fetch failed but old prices are still displayed.

## Hardware

- ESP32 board: `FireBeetle-ESP32` (`board = firebeetle32`)
- Display: one of two supported options (select at build time):

| Environment        | Display                                |
|--------------------|----------------------------------------|
| `ili9488_spi`      | 4.0" ILI9488, 480×320, SPI (default)  |
| `ili9341_parallel` | 2.8" ILI9341, 320×240, 8-bit parallel |

Wiring is documented in `WIRING.md`.

## Software Stack

- PlatformIO
- Arduino framework
- `TFT_eSPI`
- `OpenFontRender`
- `ArduinoJson`
- `WiFiManager`
- Nord Pool Data Portal API (configurable, default: `https://dataportal-api.nordpoolgroup.com/api/DayAheadPriceIndices`)

## Configuration

On boot, the device opens a WiFiManager portal (`ElMeter-<chipid>` / `192.168.4.1`) to configure:

| Field | Description | Default |
|-------|-------------|---------|
| Nord Pool API URL | Full API endpoint URL | `https://dataportal-api.nordpoolgroup.com/api/DayAheadPriceIndices` |
| Nord Pool area | Dropdown: `SE1`–`SE4`, `NO1`–`NO5`, `DK1`–`DK2`, `FI`, `EE`, `LV`, `LT`, `SYS` | `SE3` |
| Currency | Dropdown: `SEK`, `EUR`, `NOK`, `DKK` | `SEK` |
| Resolution (minutes) | Dropdown: `15`, `30`, `60` | `60` |
| VAT (%) | VAT rate | `25` |
| Total fixed cost / kWh (cents) | Fixed cost in minor currency units per kWh | `0` |

All settings are persisted in NVS and reused on future boots.

Reset button:

- Hold the configured reset button for 2 seconds to clear saved Wi-Fi, Nord Pool settings, cached prices, and moving-average history, then restart.
- Configure the button pin with `CONFIG_RESET_PIN` in `platformio.ini` (`-1` disables this feature).
- Set `CONFIG_RESET_ACTIVE_LEVEL` to `LOW` (button to GND) or `HIGH` (button to 3V3).
- Clock resync interval can be tuned with `CONFIG_CLOCK_RESYNC_INTERVAL_SEC` (default `21600`) and retry delay with `CONFIG_CLOCK_RESYNC_RETRY_SEC` (default `600`).

## Build And Upload

```bash
# 4.0" ILI9488 SPI (default)
platformio run -e ili9488_spi
platformio run -e ili9488_spi -t upload

# 2.8" ILI9341 parallel
platformio run -e ili9341_parallel
platformio run -e ili9341_parallel -t upload

# Serial monitor
platformio device monitor -b 115200
```

## Runtime Behavior

- Connects to Wi-Fi at boot using saved credentials.
- If Wi-Fi is unavailable, starts a WiFiManager AP/config portal to configure Wi-Fi and settings.
- While the portal is active, the TFT shows setup instructions.
- Syncs time via NTP using timezone mapped from selected Nord Pool area (`SE/NO/DK/SYS → CET/CEST`, `FI/EE/LV/LT → EET/EEST`).
- Fetches Nord Pool price data at startup.
- Refreshes current interval state from local clock every minute.
- Fetches full price data again daily at 13:00 local time.
- On fetch failure, retries with exponential backoff: 30 s → 60 s → ... → 30 min.
- If old prices are still shown after a failed fetch, a red "Failed to contact Nordpool!" banner is displayed.
- Hardware watchdog (60 s) reboots the device if the main loop stalls.
- Applies configurable price formula in minor currency units, then converts to currency:
  `((energy * 100) * (1 + VAT / 100) + fixed_cost_minor) / 100`.
- Cache stores raw energy prices and recalculates with current VAT/fixed settings before display.
- Moving-average history stores raw energy prices and applies current VAT/fixed settings when calculating displayed levels.
- Nord Pool level mapping uses ratio-based bands against a 72-hour moving average persisted in SPIFFS (`/nordpool_ma.bin`).

## Project Structure

- `src/main.cpp`: app flow and scheduling
- `src/display_ui.cpp`: TFT rendering
- `src/nordpool_client.cpp`: Nord Pool API client
- `src/price_cache.cpp`: SPIFFS cache for price points
- `src/wifi_utils.cpp`: Wi-Fi manager portal + runtime settings storage
- `src/time_utils.cpp`: time/date helpers
- `src/logging_utils.cpp`: serial logging
- `include/*.h`: shared types and interfaces

## Notes

- SPIFFS reports a benign mount error on first boot after flashing — `SPIFFS.begin(true)` formats the partition automatically.
- If the display stays white, verify wiring continuity and that the correct build environment is selected.
