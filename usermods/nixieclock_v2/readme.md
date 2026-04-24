# NixieClock v2 Usermod

Controls a 6-digit Nixie tube clock driven via SPI shift registers. Displays the current time (HH:MM:SS) synchronized via NTP. Supports an RGB LED backlight, colon dot separators, anti-poisoning cycling, and external control by the [hour_effect_v2](../hour_effect_v2/) usermod.

---

## Hardware

### Supported display hardware

The usermod targets Nixie tube assemblies where digits are selected by asserting a single bit per digit through a shift-register chain. The `SymbolArray` maps digits 0–9 to bit positions `2^0`–`2^9` (values 1, 2, 4 … 512). Index 10 is blank (all bits 0).

Two 32-bit SPI words are transmitted per display refresh, one for each half of the display (digits 0–2 and digits 3–5). The upper two bits of each word carry the dot separators:

| Bit 31 | Bit 30 | Bits 20–29 | Bits 10–19 | Bits 0–9 |
|--------|--------|------------|------------|----------|
| Upper dot | Lower dot | Digit N | Digit N+1 | Digit N+2 |

### MCU

Tested on **ESP32-S3** (16 MB flash, 8 MB PSRAM OPI). Should work on any ESP32 variant with a hardware SPI bus.

### Required pins

| Signal | Default GPIO | Notes |
|--------|-------------|-------|
| SPI SCLK | 12 | Configured globally in WLED → Settings → Usermods → Global SPI GPIOs |
| SPI MOSI | 11 | Configured globally in WLED |
| Latch (CS) | **10** | Configurable in the usermod settings (see below) |
| RGB LED R | 46 | PWM output — set in WLED LED preferences |
| RGB LED G | 3 | PWM output |
| RGB LED B | 17 | PWM output |
| Dots virtual | 2 | Single virtual LED, used as segment on/off switch |
| Nixie virtual | 4 | Single virtual LED, used as segment on/off switch |

> The latch pin is allocated through WLED's `PinManager`; a collision with another usermod will disable the nixie usermod and log an error.

SPI MISO is not used.

---

## WLED Configuration

### LED outputs (Settings → LED Preferences)

Configure three LED outputs. The exact type codes depend on your WLED build; the values below match the reference build:

| Output | Type | Length | GPIO(s) | Purpose |
|--------|------|--------|---------|---------|
| 0 | PWM RGB (type 43) | 1 | 46, 3, 17 | RGB backlight |
| 1 | WS2812 (type 22) | 1 | 2 | Dots on/off |
| 2 | WS2812 (type 22) | 1 | 4 | Nixie tubes on/off |

Enable **"Make a segment for each output"** (`WLED_AUTOSEGMENTS`).

This creates three WLED segments that map 1-to-1 to the three hardware functions. The usermod reads the ON/OFF state of each segment to determine what to drive.

### NTP

NTP **must be enabled** (Settings → Time & Macros → Get time from NTP server). The usermod disables itself at startup if NTP is off or SPI pins are invalid.

---

## Settings (Usermod config panel)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `Usermod_enabled` | bool | `false` | Master enable. Must be set to `true` to activate. |
| `latch_pin` | uint8 | `10` | GPIO used as the SPI latch/CS signal. |
| `Enable_LED` | bool | `false` | Enable the RGB backlight LED. When `false`, WLED brightness is forced to 0. |
| `Enable_Dots` | bool | `true` | Allow dot separators to blink. Segment 1 state is still respected. |
| `Enable_NixieTubes` | bool | `true` | Enable the time display on tubes. When `false`, tubes stay blank. |
| `NTP_force_update` | bool | `true` | Periodically force a fresh NTP sync (sets `ntpLastSyncTime = NTP_NEVER`). |
| `NTP_update_interval_in_min` | uint16 | `30` | How often to force NTP re-sync (1–60 minutes). |

All settings persist in WLED's JSON config and can be changed live through the WLED UI without a reboot.

---

## Segment power control

The three WLED segments act as independent switches:

| Segment | Controls | Effect when OFF |
|---------|---------|-----------------|
| 0 (RGB LED) | RGB backlight power | LED goes dark |
| 1 (Dots) | Dot separator output | Dots blank; tubes continue showing time |
| 2 (Nixie) | Tube digit output | Digits blank; dots continue blinking |

`nixiePower` (segment 2) and `dotsPower` (segment 1) are fully independent — each controls only its own SPI output bit. All four combinations are valid:

| Segment 2 (Nixie) | Segment 1 (Dots) | Result |
|-------------------|-----------------|--------|
| ON | ON | Digits show time + dots blink |
| ON | OFF | Digits show time, dots off |
| OFF | ON | Digits blank, dots blink |
| OFF | OFF | Display fully blank |

Toggling a segment in the WLED UI or via the HTTP/WebSocket API takes effect immediately. Segment states are verified every 5 seconds in the main loop.

---

## Anti-poisoning

Every **2 minutes** the usermod runs a non-blocking anti-poisoning cycle (~3 seconds) that scrolls all digits through 0–9 before returning to the clock. This prevents cathode poisoning on long-lived Nixie tubes.

- Runs only when the nixie segment is on (`nixiePower = true`).
- Time display is paused during cycling and resumes immediately after.
- Dots toggle on each step for a visual effect during the cycle.

---

## Power gating model

Two independent conditions must both be true for the display to update:

| Condition | Source | Controls |
|-----------|--------|---------|
| `bri > 0` | WLED global brightness | Global on/off — follows WLED's main power state |
| `mainState` | `setNixieMainPower()` | External override — controlled by hour_effect_v2 or similar |

`mainState` defaults to `true` and is only changed by `setNixieMainPower()`. It is never synced from `bri`. This means hour_effect can disable the tubes without affecting WLED brightness, and WLED brightness changes will not re-enable tubes that hour_effect has disabled.

---

## Integration with hour_effect_v2

When both usermods are enabled, **hour_effect_v2 acts as the governor** for the display. It calls `setNixieMainPower(bool state)` to externally disable or re-enable the Nixie tubes based on presence detection, lux levels, night mode, and away-from-home state.

### `setNixieMainPower(bool state)`

Public API method on `UsermodNixieClock`:

| Argument | Meaning |
|----------|---------|
| `true` | Disable tubes (blank display, no SPI output) |
| `false` | Re-enable tubes |

`mainState` is the only gate changed by this call. `bri` is untouched — the RGB LED and WLED brightness are unaffected. Tubes re-enable only when `setNixieMainPower(false)` is explicitly called.

### `getLedEnabled()` → `bool`

Returns `UM_LedEnabled`. hour_effect uses this to decide whether to include WLED's own brightness in its backup/restore cycle.

### Build-time enable

Add `-D NIXIECLOCK` to your PlatformIO build flags, or uncomment `#define USERMOD_NIXIECLOCK` in `my_config.h`. This defines `USERMOD_NIXIECLOCK`, which hour_effect_v2 checks at compile time to wire up the integration.

---

## Build instructions (PlatformIO)

1. Add the usermod to your environment in `platformio.ini`:
   ```ini
   custom_usermods = nixieclock_v2
   build_flags = ... -D NIXIECLOCK
   ```
2. Set global SPI GPIOs in WLED (or via `my_config.h`):
   ```c
   #define SPIMOSIPIN 11
   #define SPISCLKPIN 12
   ```
3. Configure LED outputs as described in the WLED Configuration section above.
4. Flash, open the WLED UI, go to **Config → Usermods → NixieClock**, set `Usermod_enabled = true`, and save.

---

## Compile-time defaults

All defaults can be overridden in `my_config.h` or via `-D` flags before including the usermod header:

```c
#define NIXIECLOCK_ENABLED             false  // must opt-in
#define LATCH_PIN                      10
#define NIXIECLOCK_RGB_ENABLED         false
#define NIXIECLOCK_DOTS_ENABLED        true
#define NIXIECLOCK_CLOCK_ENABLED       true
#define NIXIECLOCK_FORCE_NTP_ENABLED   true
#define NIXIECLOCK_UPDATE_NTP_INTERVAL 30     // minutes
```

---

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| Tubes never light up | `Usermod_enabled` is false, or NTP is disabled, or SPI pins are invalid |
| Tubes go blank and stay blank despite bri > 0 | hour_effect_v2 has called `setNixieMainPower(true)`; check hour_effect presence/night-mode state |
| Wrong time displayed | NTP not synced; check Wi-Fi connection and NTP server settings |
| Latch pin allocation failed | Another usermod or LED output is using GPIO 10; change `latch_pin` in the usermod config |
| RGB LED stays off | `Enable_LED` is false, or segment 0 is turned off in the WLED UI |
| Turning off nixie segment also turns off dots | Ensure you are running the latest version; dots and nixie are independently controlled |
| Display flickers every 2 min | Expected — anti-poisoning routine running normally |
