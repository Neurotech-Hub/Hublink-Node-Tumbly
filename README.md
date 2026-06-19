# Hublink Node Tumbly

`Hublink-Node-Tumbly` is a hardware interface library for the fixed Hublink Node Tumbly board (ESP32-S3).
It exposes low-level board controls plus optional higher-level helpers for monitoring and data logging.

This library is the successor to `Hublink-Node-Raven`. The core helpers (`DataLoggerHelper`, `MetaConfigEditor`,
`LowBatteryBoot`, `LoggerWorkflow`, `LogFileNaming`) are unchanged in shape; the namespace has moved from
`raven::` to `tumbly::` and the pin map is new.

## Features

- Fixed hardware pin map and rail controls for Hublink Node Tumbly
- Service wrappers for:
  - DS3231 RTC (with separate `~RTC_INT` line)
  - MAX17048 battery gauge (with separate `~FUEL_ALERT` line)
  - VEML7700 ambient light (optional; reports `NotFound` if not populated)
  - BME680 environmental sensing (optional; reports `NotFound` if not populated)
  - SD card storage (SPI) with hardware card-detect (`PIN_SD_DET`)
  - Three momentary buttons with interrupt-driven debounce (`ButtonService`)
  - 5V-gated hobby servo with analog position feedback (`ServoService`, built on ESP32Servo)
  - 128x64 monochrome OLED (HS96L03W2C03, SSD1306 controller, I2C @ `0x3C`) via `ScreenService`, built on Adafruit GFX / Adafruit SSD1306
  - Switchable 5V rail (`set5VPowerEnabled`) and external touch input
  - Dual front/back red status LEDs
- Optional data logging helper that generates CSV rows from a combined sensor reading
- Built-in ULP edge-counting service usable on any RTC-capable pin (default `PIN_AUX_GPIO0`) for deep-sleep counting applications
- `MetaConfigEditor` for USB Serial `meta.json` and SD file maintenance

## Dependencies

Install via Arduino Library Manager (or equivalent) before building examples:

- **Required (library.properties):** RTClib, Adafruit MAX1704X, Adafruit BusIO, Adafruit VEML7700 Library, Adafruit BME680 Library, Adafruit GFX Library, Adafruit SSD1306, ArduinoJson, ESP32Servo
- **Hublink examples only:** [Neurotech-Hub Hublink](https://github.com/Neurotech-Hub/Hublink) — set board **Tools → Bluetooth → NimBLE** for `HubWheelHublink`

## Arduino IDE Setup

- In Arduino IDE, select board `ESP32S3 Dev Module` from the ESP32 board package.
- In Tools, set `USB CDC On Boot` to `Enabled`.
- On Tumbly hardware, it is recommended to enter boot mode before flashing:
  - hold the `Boot` button
  - briefly press `Reset`
  - release `Boot`
- After flashing completes, press `Reset` once to start the new firmware.

## Quick start

```cpp
#include <HublinkNodeTumbly.h>

tumbly::HublinkNode node;

void setup() {
  Serial.begin(115200);
  node.beginHardware();
  node.beginI2C();
  node.rtc().begin();
}

void loop() {
  tumbly::RtcReading rtc = node.rtc().readSample();
  if (rtc.status == tumbly::ServiceStatus::Ok) {
    Serial.println(rtc.now.unixtime());
  }
  delay(1000);
}
```

`#include <HublinkNodeTumbly.h>` pulls in `tumbly::HublinkNode`, sensor/SD services, `ButtonService`,
`ServoService`, `ScreenService`, `DataLoggerHelper`, `LogFileNaming`, `MetaConfigEditor`,
`MetaConfigReader`, and low-battery safeguard helpers.

### Screen quick example

```cpp
node.beginHardware();
node.beginI2C();
if (node.screen().begin()) {
  node.screen().printLines("Hublink", "Tumbly", "ready");
  // For full GFX (custom fonts, shapes, bitmaps), use the underlying surface:
  node.screen().display().drawRect(0, 24, 128, 40, SSD1306_WHITE);
  node.screen().show();
}
```

The screen and DS3231 share the I2C isolator, so `setI2CPowerEnabled(true)` must be active before
`screen().begin()` (this is the default after `beginHardware()`). Call `screen().end()` and then
`screen().begin()` again after toggling I2C power so the SSD1306 controller is re-initialized.

## Examples

| Sketch | Purpose |
| --- | --- |
| `examples/TumblyMVP/TumblyMVP.ino` | Full board MVP: sensors, OLED, servo pulse, multi-button power/sleep tests |
| `examples/BasicHardware/BasicHardware.ino` | Minimal bring-up; optional low-battery safeguard demo |
| `examples/SensorSnapshot/SensorSnapshot.ino` | One-shot sensor readout over Serial |
| `examples/DataLogging/DataLogging.ino` | Masked CSV logging to SD with `LogFilePolicy` |
| `examples/HubWheelMinimal/HubWheelMinimal.ino` | Deep-sleep wheel logger (no Hublink) |
| `examples/HubWheelHublink/HubWheelHublink.ino` | Wheel logger + Hublink gateway (NimBLE) |
| `examples/MetaConfigEditorHold/MetaConfigEditorHold.ino` | USB startup hold + `MetaConfigEditor` shell only |
| `examples/AlertPinTest/AlertPinTest.ino` | Exercise the separate `PIN_RTC_INT` and `PIN_FUEL_ALERT` lines, including an I2C-rail-off stability check |

## Notes

- This library intentionally targets fixed custom hardware. Runtime pin remapping is not supported.
- Defaults are conservative for reliability (`I2C=100kHz`, SD disabled until mounted). Default SD SPI clock is `1 MHz` (`DEFAULT_SD_SPI_CLOCK_HZ` in `TumblyPins.h`), aligned with Hublink SD usage when both share the card.
- The ULP edge-counting service is generic; on Tumbly it defaults to `PIN_AUX_GPIO0` (GPIO1) but any RTC-capable pin can be passed to `MagnetCounterService::begin(pin)`.
- On Tumbly, the RTC and fuel-gauge alert lines are wired separately: `PIN_RTC_INT` (GPIO21) and `PIN_FUEL_ALERT` (GPIO18) are each open-drain active-LOW with internal pull-ups. The library exposes `readRtcInt()` and `readFuelAlert()` for polling, but does not currently configure DS3231 or MAX17048 alert thresholds/masks.
- `readUsbSense()` is backed by `~PGOOD` from the BQ24075 on `PIN_USB_SENSE` (GPIO34) — active LOW when any input power source (USB or other charger) is good. The name is kept for CSV/API compatibility; the `usb_sense` CSV column reflects this PGOOD signal on Tumbly.
- `set5VPowerEnabled(bool)` controls the 5V rail (`PIN_5V_EN`, GPIO6, active HIGH). The rail is off by default after `beginHardware()`.
- `ServoService` only enables the servo rail (`PIN_SRV_EN`, GPIO10, active LOW) while attached; detach to drop both PWM and power.
- `ButtonService` attaches `FALLING` interrupts on `PIN_BNT_0/1/2`; callbacks run in ISR context — keep them short.

## Battery and low-voltage safeguard (voltage-only, millis-based)

- **Defaults:** [`kSafeguardTripVoltsDefault`](src/helpers/LowBatteryBoot.h) (**2.0 V**), `kSafeguardRecoverVoltsDefault` (**2.6 V**), `kSafeguardPollIntervalSecondsDefault` (**600** s), `kSafeguardShutdownWakeupSecondsDefault` (**600** s). No **`meta.json`** keys.
- **Automatic path:** [`maybeAutomaticVoltageSafeguard(node, true)`](src/helpers/LowBatteryBoot.h)—internal millis spacing and **`LowBatteryGateConfig`** defaults (**USB** blocks sleep); same behavior as **[`DataLoggerHelper::begin()`](src/helpers/DataLoggerHelper.cpp)** after **`beginI2C()`**. Call from **`setup()`/`loop()`** on your cadence—the helper still throttles gauge reads. **`maybeAutomaticVoltageSafeguard(node, false)`** is a no-op.
- **Manual path:** [`isCellBelowTripVoltage(node[, tripVolts])`](src/helpers/LowBatteryBoot.h); then optionally [`safeguardShutdown(node, wakeupInSeconds)`](src/helpers/LowBatteryBoot.h) (**LEDs off**, timer deep sleep, does **not** return). USB and policy are sketch-controlled. Example: [`examples/BasicHardware/BasicHardware.ino`](examples/BasicHardware/BasicHardware.ino).
- **Diagnostics:** [`diagnoseVoltageSafeguard(Stream, node, usbPresent)`](src/helpers/LowBatteryBoot.h)—optional fourth argument **`LowBatteryGateConfig`** if you tune reporting.
- **Meta editor:** `sensor safeguard` runs diagnose when `HublinkNode` is passed into `maybeEnterWithFade` / `enterNow`.

## Data Logger

- `RtcService::begin()` now performs a best-effort RTC-to-system-time sync when RTC data is valid. If RTC is unavailable or invalid, initialization continues without failing.
- In the API and examples, `SampleFields` means a single combined sensor reading (time + power + light + environment + GPIO states).

### Selectable CSV Fields

- `DataLoggerHelper::csvHeader()` and `DataLoggerHelper::toCsv(...)` keep default full-field behavior for backward compatibility.
- To log only selected columns, use typed masks with `CsvField` and overloads that accept `CsvFieldMask`.
- Battery percentage is exposed as `batt_per` in CSV output.
- `datetime` is formatted as `YYYY-MM-DD HH:MM:SS` for straightforward parsing in Python (`pandas.to_datetime` or `datetime.strptime`). In `meta.json` `logger.log_fields`, the alias `rtc_text` is also accepted for `datetime`.
- `passes_min` is derived from wake cadence: `magnet_passes * 60 / sleep_time_seconds`.
- Full selectable field list:
  - Runtime: `millis`, `ulp_edges`, `magnet_passes`, `passes_min`, `magnet`, `usb_sense`
  - RTC: `unix`, `datetime`, `rtc_temp_c`
  - Battery: `batt_v`, `batt_per`
  - Light: `lux`, `als`, `white`
  - Environment: `temp_c`, `pressure_hpa`, `humidity_pct`, `gas_kohm`, `alt_m`

```cpp
constexpr tumbly::CsvFieldMask kLogFields = tumbly::csvFields({
  tumbly::CsvField::RtcUnix,
  tumbly::CsvField::UlpEdges,
  tumbly::CsvField::MagnetPasses,
  tumbly::CsvField::PassesPerMin,
  tumbly::CsvField::BattV,
  tumbly::CsvField::BattPer
});

tumbly::SampleFields sample = logger.captureSample();
sample.passesPerMin = tumbly::computePassesPerMinute(sample.magnetPassCount, kSleepTimeSeconds);
String logPath;
if (tumbly::resolveLogFilePath(node.sd(), gLogFilePolicy, sample.rtc, logPath) ==
    tumbly::ServiceStatus::Ok) {
  if (!node.sd().exists(logPath.c_str())) {
    node.sd().appendLine(logPath.c_str(), tumbly::DataLoggerHelper::csvHeader(kLogFields));
  }
  logger.appendCsvSample(logPath.c_str(), sample, kLogFields);
}
```

### Filename Modes

- Base name is required and should use only letters, numbers, `_`, or `-`.
- `inc_on_reboot` controls whether a 3-digit reboot suffix (`XXX`) is auto-managed by the logger; default is `false`.
- `Daily`:
  - `inc_on_reboot=false`: `[base]_YYYYMMDD.csv` (example: `HUBWHEEL_20260429.csv`)
  - `inc_on_reboot=true`: `[base]_YYYYMMDDXXX.csv` (example: `HUBWHEEL_20260429000.csv`)
- `Hourly`:
  - `inc_on_reboot=false`: `[base]_YYYYMMDDHHMM.csv` (example: `HUBWHEEL_202604291045.csv`)
  - `inc_on_reboot=true`: `[base]_YYYYMMDDHHMMXXX.csv` (example: `HUBWHEEL_202604291045000.csv`)
- `Manual`:
  - `inc_on_reboot=false`: continue writing to the last detected `[base]_XXX.csv` file (or start at `_000` if none exist)
  - `inc_on_reboot=true`: choose the first available `[base]_XXX.csv` on startup
- `Disabled`:
  - `inc_on_reboot=false`: `[base].csv` (example: `HUBWHEEL.csv`)
  - `inc_on_reboot=true`: `[base]XXX.csv` (example: `HUBWHEEL000.csv`)
- If the target file does not exist, the logger writes the CSV header first, then appends rows.
- In `Manual` mode, use `tumbly::incrementManualCounter(policy)` when you want to advance to the next file explicitly.

```cpp
constexpr char kLogBaseName[] = "LOGGER";
constexpr tumbly::FileNameMode kLogFileMode = tumbly::FileNameMode::Disabled;
tumbly::LogFilePolicy gLogFilePolicy = {
  kLogBaseName,
  kLogFileMode,
  0,     // manualCounter
  false, // manualCounterInitialized
  false  // incOnReboot
};
```

### meta.json programmatic access

- Firmware can read `/meta.json` without going through `Hublink::getMeta` by using Tumbly helpers (`MetaConfigEditor` already shares the load path internally):
  - `tumbly::loadMetaJson(sd, doc)` reads and parses a JSON object root.
  - Typed dot-path accessors: `metaGetUInt32`, `metaGetLong`, `metaGetBool`, `metaGetString`, `metaGetJsonArray` (`wheel.sleep_time_seconds`, `logger.log_fields`, etc.).
  - Arbitrary lookups: `tumbly::resolveMetaDotPath(doc.as<JsonVariantConst>(), "<dot.path>", &ok)`.
- Hublink still owns BLE/upload configuration from `meta.json` via `hublink.begin()`. Sketch-owned namespaces (such as `wheel.*` / `logger.*`) can instead use the Tumbly APIs so tooling matches the Serial meta editor paths.

### HubWheel + Hublink Example

- Use `examples/HubWheelHublink/HubWheelHublink.ino` when the Hublink library is installed.
- Use `examples/HubWheelMinimal/HubWheelMinimal.ino` for the Hublink-free wheel logger.
- `HubWheelHublink.ino` keeps hardcoded defaults first, then `hublink.begin()` for `hublink.*`; `wheel.*` / `logger.*` are applied from `/meta.json` using `tumbly::loadMetaJson` and typed getters.
- The exact `meta.json` example and key details are documented inline in `HubWheelHublink.ino` so the README stays sketch-agnostic.
- Low-power/deep-sleep sketches rely on sketch-controlled wake scheduling; they cannot rely on Hublink advertise/sync intervals while asleep. This is why wheel examples include explicit `wheel.*` timing settings in addition to `hublink.*` settings.
- `HubWheelHublink.ino` includes an optional USB Serial `meta.json` editor: press `e` during a ~5s startup hold window to enter command mode.
- Top-level commands: `help`, `reboot`, `exit`
  - Sensor commands (when `HublinkNode*` is passed into the editor): `sensor`, `sensor list`, `sensor fuel`, `sensor safeguard`, etc.
  - Meta commands: `meta show`, `meta get <path>`, `meta set <path> <value>`, `meta setjson <path> <json_literal>`, `meta del <path>`, `meta save`, `meta reload`
- File commands (root-only): `file help`, `file list`, `file cat <name>`, `file rm <n>`, `file rm all`
  - `file rm` never allows deleting `meta.json`; saves use atomic temp-file replacement (`/meta.tmp` -> `/meta.json`).
- Example session:
  - `meta get wheel.sleep_time_seconds`
  - `meta set wheel.sleep_time_seconds 15`
  - `meta set logger.inc_on_reboot true`
  - `meta setjson logger.log_fields ["unix","datetime","ulp_edges","magnet_passes","passes_min"]`
  - `meta save`
  - `file list`
  - `file rm 2`
  - `file rm all`

### MetaConfigEditorHold

- `examples/MetaConfigEditorHold/MetaConfigEditorHold.ino` isolates the USB startup hold used by the wheel sketches: after cold boot with USB connected, press `e` during the fade window to enter the same `meta` / `file` / `sensor` shell without running wheel or Hublink logic.

## Power consumption

Bench measurements on Hublink Node Tumbly hardware. **Life on 2,000 mAh** is a rough constant-current estimate
(`2000 mAh ÷ draw`) for a nominal 2,000 mAh LiPo; actual runtime varies with cell age, temperature, and load
transients (servo pulses, SD writes, etc.).

| State | µA | mA | Life on 2,000 mAh |
| --- | ---: | ---: | --- |
| Power off (switch) | 13.6899 | 0.0137 | 16.7 years |
| Deep sleep | 22.7972 | 0.0228 | 10.0 years |
| Light sleep | 1224.72 | 1.225 | 68.0 days |
| MVP example (screen off) | 23688 | 23.69 | 3.5 days |
| MVP example (screen on) | 30732 | 30.73 | 2.7 days |
| Inferred screen current | 7044 | 7.04 | — |

**How these were captured**

- **Power off (switch):** hardware power switch off; MCU and rails unpowered.
- **Deep sleep / light sleep:** [`TumblyMVP`](examples/TumblyMVP/TumblyMVP.ino) multi-button holds — **B0+B2** (deep sleep, 5 s timer wake) and **B1+B2** (light sleep, 5 s timer wake). Both paths tear down SD, I2C, 5V, screen, and servo before sleeping.
- **MVP example (screen off):** `TumblyMVP` running with **B0+B1** held — I2C isolator off (OLED and I2C sensors unpowered); remainder of sketch active.
- **MVP example (screen on):** `TumblyMVP` normal loop with OLED and I2C sensors powered.
- **Inferred screen current:** difference between screen-on and screen-off MVP draws (`30732 µA − 23688 µA`).

Additional load: the 5V rail and servo add significant transient current when `TumblyMVP` pulses the horn each loop; figures above are steady-state averages for the listed modes.
