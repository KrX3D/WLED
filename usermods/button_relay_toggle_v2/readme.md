# ButtonRelayToggle v2 Usermod

Manages up to **4 independent button/relay groups**. A short press on a hardware button toggles the associated relay/MOSFET. Relay states can also be controlled and monitored via MQTT, with optional Home Assistant auto-discovery.

---

## Features

- Up to 4 button + relay pairs, each independently configurable
- Hardware button debounce with toggle lockout (prevents double-triggers)
- Active-low or active-high relay support per group
- MQTT publish of button and relay state changes
- Home Assistant MQTT discovery (optional)
- Relay command subscription (`/Relay/set`) for remote control from HA or any MQTT client
- Startup suppression: ignores button events for 1 second after boot to avoid false triggers

---

## Hardware

Each group consists of:
- **Button**: any momentary switch connected between a GPIO and GND (with internal pullup), or between GPIO and 3.3 V (without pullup)
- **Relay / MOSFET**: output GPIO driving a relay module or MOSFET gate

### Wiring (typical, active-high relay, pullup button)

```
Button:  GPIO ──[button]── GND        (INPUT_PULLUP, pressed = LOW)
Relay:   GPIO ──[relay IN]── ...      (HIGH = relay ON)
```

For active-low relay modules (common opto-isolated boards):
```
Relay:   GPIO ──[relay IN]── ...      (LOW = relay ON → set active_low = true)
```

---

## WLED Configuration

### LED outputs

This usermod does **not** use WLED LED segments. Configure LED outputs only for your actual LEDs.

### Build

Add the usermod to your PlatformIO environment:

```ini
custom_usermods = button_relay_toggle_v2
```

Optional compile-time defaults (override in `my_config.h` or via `-D` flags):

```c
#define BUTTON_RELAY_TOGGLE_ENABLED     false
#define BUTTON_RELAY_TOGGLE_HA_DISCOVERY false

#define BUTTON_1_PIN        -1      // GPIO for button 1 (-1 = disabled)
#define BUTTON_1_PULLUP     true    // true = INPUT_PULLUP
#define RELAY_1_PIN         -1      // GPIO for relay 1 (-1 = disabled)
#define RELAY_1_ACTIVE_LOW  false   // true = LOW turns relay ON

// ... same pattern for groups 2, 3, 4
```

---

## Settings (Usermod config panel / JSON)

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `enabled` | bool | `false` | Master enable |
| `ha_discovery` | bool | `false` | Enable Home Assistant MQTT auto-discovery |

Per group (group_1 … group_4):

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `button_pin` | int | `-1` | GPIO for the button (-1 = disabled) |
| `button_pullup` | bool | `true` | Enable internal pull-up on button pin |
| `relay_pin` | int | `-1` | GPIO for the relay/MOSFET (-1 = disabled) |
| `relay_active_low` | bool | `false` | Relay is ON when GPIO is LOW |

---

## Button behavior

1. Button pressed → debounce (50 ms stable) → press registered
2. Button released → debounce (50 ms stable) → relay toggled
3. A **500 ms lockout** prevents further toggles immediately after, covering mechanical bounce and flutter
4. Button events are suppressed for **1000 ms after boot** to avoid startup false-triggers

The toggle fires on **release**, not on press.

---

## MQTT topics

All topics are relative to the WLED device topic (e.g. `wled/<device>/Button_Relay_Toggle`).

### State topics (device → broker, retained)

| Topic | Payload | Description |
|-------|---------|-------------|
| `.../group_N/Button` | `PRESSED` / `RELEASED` | Button event |
| `.../group_N/Relay` | `ON` / `OFF` | Relay current state |
| `.../config` | JSON | Full configuration snapshot |

### Command topics (broker → device)

| Topic | Payload | Description |
|-------|---------|-------------|
| `.../group_N/Relay/set` | `ON` / `OFF` | Set relay state |

> **Important:** The device never publishes to `/Relay/set`. Publishing retained messages to a command topic causes MQTT loops where the broker re-delivers the retained state as a new command. Only external clients (e.g. Home Assistant) should write to `/set` topics.

---

## Home Assistant integration

When `ha_discovery = true` and MQTT is connected, the usermod publishes HA auto-discovery payloads to `homeassistant/...` topics. HA will automatically create:

- Diagnostic sensors for each configured pin and setting
- Binary sensor for each button state
- Switch entity for each relay (controllable from HA)

Discovery is re-triggered on every MQTT reconnect.

The relay switch in HA uses:
- **State topic**: `.../group_N/Relay`
- **Command topic**: `.../group_N/Relay/set`

---

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| Relay never toggles on button press | Button pin is `-1`, wrong GPIO, or usermod not enabled |
| Double-trigger on button press | Mechanical bounce lasting > 500 ms; try a different button or add a hardware capacitor |
| Relay turns on/off rapidly in a loop | Old retained `/Relay/set` messages on the broker; clear retained messages or ensure only external clients publish to `/set` |
| HA switch shows "unavailable" | MQTT not connected, or HA discovery not enabled |
| Relay state resets to OFF on reboot | Expected — relay is always initialized to OFF at startup for safety |
| Button triggers immediately after boot | Expected behavior if button is held at power-on; startup suppression (1 s) prevents most cases |
