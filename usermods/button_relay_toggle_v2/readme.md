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
- Per-relay boot state: Default (feature disabled, original behavior), On, or Off
- Retained `/set` commands are cleared on subscribe, so a stale command can't undo the relay state

---

## Hardware

Each group consists of:
- **Button**: any momentary switch connected between a GPIO and GND (with internal pullup), or between GPIO and 3.3 V (without pullup)
- **Relay / MOSFET**: output GPIO driving a relay module or MOSFET gate

### Wiring (typical, active-high relay, pullup button)

```
Button:  GPIO --[button]-- GND        (INPUT_PULLUP, pressed = LOW)
Relay:   GPIO --[relay IN]-- ...      (HIGH = relay ON)
```

For active-low relay modules (common opto-isolated boards):
```
Relay:   GPIO --[relay IN]-- ...      (LOW = relay ON -> set active_low = true)
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
| `relay_boot_state` | int | `0` | Boot behavior: `0`=Default (feature disabled, original behavior), `1`=On, `2`=Off |

---

## Relay boot state

Each relay independently chooses what it does when the ESP boots (or when this setting is
saved without a full reboot):

- **Default (`0`)** — this feature is off; unchanged from before it existed. The relay is always
  driven to its **logical OFF** level, i.e. whatever `relay_active_low` says OFF is. This is
  *not* automatically "the light is physically off" — it only matches your expectation if
  `relay_active_low` (and the pullup/wiring it goes with) is set correctly for your hardware. If
  Default ends up powering something on that you expect to be off, the fix is to correct
  `relay_active_low` for that group, not to avoid Default.
- **On (`1`)** — the relay is forced to its logical ON state at boot.
- **Off (`2`)** — the relay is forced to its logical OFF state at boot (same physical result as
  Default given the same `relay_active_low`, but explicit).

On and Off use the exact same `relay_active_low`-relative logic as everything else in this
usermod (button toggle, MQTT `/set`) — they don't bypass it. Get `relay_active_low` right for
your wiring first; On/Off then reliably mean what they say.

`relay_boot_state` only decides what to physically drive at the moment of a genuine
(re)initialization: cold boot, the usermod being newly enabled, the relay's GPIO being
reassigned, or the boot-state setting itself being changed. A plain settings save that doesn't
touch any of that leaves the relay exactly as it is — including if it was toggled on since boot
by the button or by HA — and only republishes the actual state to MQTT/HA, it never forces the
relay back to the configured boot value on every save.

The setting is exposed as a dropdown (Disabled/On/Off) on the usermod settings page.

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
| `.../group_N/Relay/set` | `ON` / `OFF` | Set relay state (other payloads are ignored) |

> **Important:** Commands to `/Relay/set` must not be retained. A retained command is redelivered by the broker on every (re)subscribe (boot, MQTT reconnect, settings save) and would be applied again. The HA discovery payload therefore does not set `retain`, and the device clears any retained `/set` message (empty retained publish) right before it subscribes. Apart from that, the device never publishes to `/set`.

---

## Home Assistant integration

When `ha_discovery = true` and MQTT is connected, the usermod publishes HA auto-discovery payloads to `homeassistant/...` topics. HA will automatically create:

- Diagnostic sensors for each configured pin and setting
- Binary sensor for each button state
- Switch entity for each relay (controllable from HA)

Discovery is re-triggered on every MQTT reconnect.

The relay switch in HA uses:
- **State topic**: `.../group_N/Relay`
- **Command topic**: `.../group_N/Relay/set` (not retained)

---

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| Relay never toggles on button press | Button pin is `-1`, wrong GPIO, or usermod not enabled |
| Double-trigger on button press | Mechanical bounce lasting > 500 ms; try a different button or add a hardware capacitor |
| Relay turns on/off rapidly in a loop | Another client publishes retained messages to `/Relay/set`; the device clears them on subscribe, but only external clients should publish to `/set`, and never retained |
| HA switch shows "unavailable" | MQTT not connected, or HA discovery not enabled |
| Relay resets to a state you didn't expect on reboot | Expected if `relay_boot_state` is Default for that group - it drives logical OFF, not necessarily "physically off"; check `relay_active_low` matches your wiring - see "Relay boot state" above |
| Forced boot On/Off gets overridden by MQTT right after boot | An HA automation or other client sent a live command after boot, or publishes `/set` retained (check its `retain` setting); retained `/set` messages are cleared before subscribing |
| Button triggers immediately after boot | Expected behavior if button is held at power-on; startup suppression (1 s) prevents most cases |
