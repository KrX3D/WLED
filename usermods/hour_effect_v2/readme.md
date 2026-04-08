# HourEffect v2 Usermod (KrX MQTT Commander)

MQTT-driven automation layer for WLED. Handles presence detection, lux/illuminance-based control, night mode scheduling, away-from-home state, hourly LED effects, a 3D-printer finished blink, and arbitrary MQTT notification effects — all without touching WLED's core logic.

---

## Features

- **Night mode**: Automatically power LEDs on/off at configured hours
- **NotHome**: Suppress LEDs when away (MQTT-controlled)
- **Presence detection**: Turn LEDs on/off based on motion/presence sensors (simple topic or advanced multi-sensor JSON config with boolean logic)
- **Lux/illuminance**: Turn LEDs on/off based on ambient light threshold
- **Trigger modes**: 6 modes combining presence and lux
- **Presence blocker**: Suppress presence triggers (e.g., when a light switch is in manual mode)
- **3D printer finished**: Blink green when print finishes
- **Notification effect**: Trigger any WLED effect from MQTT with configurable color, duration, and target
- **Hourly effect**: Play a WLED effect at the top of every hour
- **MQTT lamp control**: Mirror WLED on/off state to external MQTT devices (Zigbee lamps, etc.)
- **Physical sensor pin**: Direct GPIO input for presence (no MQTT required)
- **NTP sanity check**: Detects and recovers from NTP clock jumps
- **Nixie clock / SSDR integration**: Controls external display usermods via their power API

---

## Build

```ini
custom_usermods = hour_effect_v2
```

Optional compile-time defaults (`my_config.h` or `-D` flags):

```c
#define HOUR_EFFECT_ENABLED_USERMOD          false
#define HOUR_EFFECT_ENABLED_3D_BLINK         false
#define HOUR_EFFECT_ENABLED_HOUR_EFFECT      false
#define HOUR_EFFECT_ENABLE_NIGHT_MODE_POWER_OFF   false
#define HOUR_EFFECT_ENABLED_NIGHT_MODE_POWER_ON   false
#define HOUR_EFFECT_ENABLE_PRESENCE_DURING_NIGHT_MODE false
#define HOUR_EFFECT_NIGHT_MODE_ON            1     // hour (0-23)
#define HOUR_EFFECT_NIGHT_MODE_OFF           8     // hour (0-23)
#define HOUR_EFFECT_MQTT_PRESENCE            ""    // topic or JSON
#define HOUR_EFFECT_MQTT_PRESENCE_BLOCKER    ""
#define HOUR_EFFECT_MQTT_LUX                 ""
#define HOUR_EFFECT_LUX_THRESHOLD            30
#define HOUR_EFFECT_TRIGGER_MODE             0
#define HOUR_EFFECT_INPUT_PIN                -1
#define HOUR_EFFECT_INPUT_ACTIVE_LOW         false
#define HOUR_EFFECT_MQTT_LAMPS               ""
#define MIN_3D_TRIGGER_MS                    3000UL
```

For Nixie clock integration also add `-D NIXIECLOCK` (see nixieclock_v2 readme).

---

## Settings (WLED Config → Usermods → KrX MQTT Commander)

### Main Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `Enable Usermod` | bool | `false` | Master on/off switch |

### Effect Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `3d finished blink` | bool | `false` | Enable green blink when `/3dPrinterFinshed` MQTT message is `true` |
| `Notification MQTT Effect` | bool | `false` | Enable custom notification effects via `/NotificationEffect` |
| `Effect every Hour` | bool | `false` | Play the configured WLED effect at HH:00:00 every hour |

### Night Mode Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `Enable Power off when NightMode starts` | bool | `false` | Turn LEDs off when night mode starts at `NightMode On at` |
| `Enable Power on when NightMode finished` | bool | `false` | Turn LEDs on when night mode ends at `NightMode Off at` |
| `Enable Presence during NightMode` | bool | `false` | Allow presence detection to override night mode power-off |
| `NightMode On at` | 0-23 | `1` | Hour when night mode activates (01:00) |
| `NightMode Off at` | 0-23 | `8` | Hour when night mode deactivates (08:00) |

### MQTT Sensor Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `Mqtt Presence Path` | string | `""` | Simple: MQTT topic(s) for presence. Advanced: JSON sensor config (see below) |
| `Block Presence On/Off` | string | `""` | Simple: MQTT topic. Advanced: JSON sensor config. When active, blocks presence/lux triggers |
| `Mqtt Lux/Illuminance Path` | string | `""` | Simple: MQTT topic(s). Advanced: JSON sensor config |
| `Lux Threshold` | int | `30` | Lux value below which LEDs turn on |
| `Trigger Mode` | 0-5 | `0` | Controls which sensors drive on/off (see Trigger Modes) |

### Physical Sensor Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `Sensor Pin` | GPIO | `-1` | GPIO input treated as direct presence signal |
| `Sensor Active Low` | bool | `false` | Pin is active (presence = true) when LOW |

### MQTT Lamp Control

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `MQTT Lamps (comma-separated)` | string | `""` | Comma-separated list of MQTT topics. When WLED turns on/off, publishes `{"state":"ON"}` / `{"state":"OFF"}` to each topic |

---

## Trigger Modes

| Mode | Name | Behavior |
|------|------|----------|
| 0 | None | No automatic LED control |
| 1 | Presence only | LEDs on when presence=true, off when presence=false |
| 2 | Lux only (no turn off) | LEDs on when lux < threshold; never turns off automatically |
| 3 | Lux only (with turn off) | LEDs on when lux < threshold, off when lux ≥ threshold |
| 4 | Lux + Presence (off on no presence) | Both conditions must be true to turn on; off when presence=false |
| 5 | Lux + Presence (off on both false) | Both conditions must be true to turn on; off only when BOTH false |

Modes 2-5 are skipped when `NightMode` or `NotHome` is active (unless `Enable Presence during NightMode` overrides it for mode 4/5).

---

## MQTT Topics

All topics use the WLED group topic (`mqttGroupTopic`). Subscribe path: `<groupTopic>/<suffix>`.

### Command topics (broker → device)

| Suffix | Payload | Description |
|--------|---------|-------------|
| `/NightMode` | `true` / `false` | Manually set night mode state |
| `/NotHome` | `true` / `false` | Set away-from-home state; `true` turns LEDs off |
| `/NewEffect` | integer | Set the effect index used for the hourly effect |
| `/3dPrinterFinshed` | `true` | Trigger green blink effect (requires `3d finished blink` enabled) |
| `/NotificationEffect` | JSON | Trigger custom notification effect (see below) |

### State topics (device → broker, retained)

Published to `<deviceTopic>/config/Options/<suffix>`:

| Suffix | Payload |
|--------|---------|
| `NightMode` | `true (DD.MM.YYYY HH:MM:SS)` |
| `NotHome` | `true/false (timestamp)` |
| `NewEffect` | effect index |
| `3dPrinterFinshed` | timestamp string |
| `NotificationEffect` | timestamp string |

---

## Notification Effect Payload

Published to `<groupTopic>/NotificationEffect` as JSON:

```json
{
  "active": true,
  "r": 0, "g": 255, "b": 0, "w": 0,
  "effect": 1,
  "speed": 128,
  "intensity": 128,
  "palette": 0,
  "durationMs": 10000,
  "target": "ALL"
}
```

| Field | Description |
|-------|-------------|
| `active` / `trigger` / `state` | Boolean — set to `false` to ignore the message |
| `r`, `g`, `b`, `w` | Color (0-255 each) |
| `effect` / `mode` | WLED effect index (0-255) |
| `speed` | Effect speed (0-255) |
| `intensity` | Effect intensity (0-255) |
| `palette` | Palette index (0-255) |
| `durationMs` / `duration` | Effect hold time in ms (100–600000, default 10000) |
| `target` | `"ALL"` or the device's `serverDescription` name |

After `durationMs` the previous LED state is restored automatically.

---

## Presence / Blocker / Lux: Simple vs Advanced Mode

Each of the three sensor inputs (presence, blocker, lux) supports two modes, selected automatically by the content of the configuration field.

### Simple mode

Enter one or more comma-separated MQTT topics directly in the field.

**Presence**: All topics must be ON for presence to be true (AND logic).  
**Blocker / Lux**: First matching topic wins.

Simple presence payloads accepted:
- `true` / `false`
- `1` / `0`
- `on` / `off`
- JSON: `{"presence": true}` or `{"illuminance": 42.5}` / `{"lux": 42.5}`

### Advanced mode (JSON config)

Enter a JSON object directly into the field (or use the HTML builder tool). The field auto-detects JSON when the value starts with `{` and contains `"sensors"`.

```json
{
  "sensors": [
    {
      "id": "kitchen_wave",
      "topic": "zigbee2mqtt/Kitchen mWave",
      "path": "presence",
      "on_values": "on,true,1"
    },
    {
      "id": "dinnerroom_wave",
      "topic": "zigbee2mqtt/Dinnerroom mWave",
      "path": "presence",
      "on_values": "on,true,1"
    }
  ],
  "logic_true": "kitchen_wave OR dinnerroom_wave",
  "logic_false": "!kitchen_wave AND !dinnerroom_wave"
}
```

#### Sensor fields

| Field | Required | Description |
|-------|----------|-------------|
| `id` | yes | Unique identifier used in logic expressions (lowercase, a-z0-9_) |
| `topic` | yes | Full MQTT topic to subscribe to |
| `path` | no | JSON key to extract from the payload (e.g. `"presence"`, `"occupancy"`) |
| `on_values` | yes | Comma-separated list of values that mean "true" (exact match, case-insensitive) |

#### Logic expressions

Boolean expressions over sensor IDs using `AND`, `OR`, `!` (NOT), and parentheses.

| Operator | Example |
|----------|---------|
| `AND` | `sensor_a AND sensor_b` |
| `OR` | `sensor_a OR sensor_b` |
| `!` (NOT) | `!sensor_a` |
| Parentheses | `(sensor_a OR sensor_b) AND !sensor_c` |
| Literals | `true`, `false` |

`logic_true`: expression that evaluates to "presence detected" (LEDs on).  
`logic_false` (optional): expression that evaluates to "no presence" (LEDs off). If omitted, `NOT logic_true` is used. If neither expression is satisfied, the current state is preserved.

---

## HTML Builder Tool (`wled-sensor-config.html`)

A standalone web tool for building the advanced JSON sensor configuration visually. Open it in any browser — no server needed.

### How to use

1. Open `wled-sensor-config.html` in your browser.
2. **Add sensors** (section 1):
   - Enter `id` (auto-sanitized to lowercase underscores)
   - Enter the full MQTT `topic`
   - Enter `path` if the payload is JSON (the key to extract, e.g. `presence`)
   - Enter `on_values` (comma-separated values that mean "true", default `on,true,1`)
   - Click **Add sensor**
3. **Build logic** (section 2 — Palette & Build logic):
   - Drag sensor tokens from the palette into the **logic_true** zone
   - Drag `AND`, `OR`, `(`, `)`, `true`, `false` operator tokens to compose your expression
   - Drop `!` onto an existing sensor token to toggle NOT (`!sensor_id`)
   - Double-click any token in the palette to append it to logic_true
   - Drag tokens within a zone to reorder, or drag between zones
   - Drag tokens to the **REMOVE** box to delete them
   - The **logic_false** zone is optional
4. **Generate JSON** (section 3):
   - Click **Generate JSON** to produce the output
   - Use **Pretty / Minify toggle**, **Copy to clipboard**, or **Download .json**
5. **Import existing config** (Import / Export card):
   - Paste a previously generated JSON or load a `.json` file to edit it

### Paste into WLED

Copy the generated (minified) JSON and paste it into the `Mqtt Presence Path`, `Block Presence On/Off`, or `Mqtt Lux/Illuminance Path` field in the WLED usermod settings. Save — the device auto-detects the JSON format and switches to advanced mode.

---

## Night Mode Logic

Night mode is an internal flag, set either by the time-based scheduler or by the `/NightMode` MQTT topic.

```
NightModeOn  hour reached → NightMode = true  → (if enableNightModePowerOff) turn LEDs off
NightModeOff hour reached → NightMode = false → (if enabledNightModePowerOn) turn LEDs on
```

When `Enable Presence during NightMode` is true, presence detection remains active during night mode: a detected presence can keep or turn LEDs on, overriding the night mode power-off.

Night mode works across midnight: if `NightModeOn=23` and `NightModeOff=7`, the mode is active from 23:00 to 07:00.

---

## NotHome State

Set via MQTT topic `<groupTopic>/NotHome`:
- `true`: LEDs turn off immediately, all presence/lux triggers suppressed
- `false`: Normal operation resumes (does NOT automatically turn LEDs back on)

---

## Integration with other usermods

### nixieclock_v2

When both usermods are built with `-D NIXIECLOCK`, hour_effect calls `nixie->setNixieMainPower(bool)` to blank/restore the Nixie tubes in sync with NightMode and NotHome state, independently of WLED brightness.

### seven_segment_display_reloaded_v2 (SSDR)

When built with `-D USERMOD_SSDR`, calls `ssdr->disableOutputFunction(bool)` in the same way.

---

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| Nothing happens on presence MQTT | `Enable Usermod` is false, or trigger mode is 0 |
| LEDs turn on but never off | Trigger mode 2 selected (lux only, no turn off) — use mode 3 |
| Presence true but LEDs stay off | `NotHome` or `NightMode` active and blocking; check `/NotHome` retained messages |
| Advanced JSON not loading | JSON too large (>4 KB), parse error, or `&quot;` escaping issue — check serial log |
| Logic expression gives wrong result | Check operator spacing (`" AND "`, `" OR "` need spaces); use parentheses for complex expressions |
| Notification effect fires on wrong device | `target` field must match `serverDescription` exactly (case-insensitive) |
| 3D printer effect fires repeatedly | Broker has retained `true` on `/3dPrinterFinshed`; clear retained message |
| NTP time jumps / wrong hourly effect time | Time sanity check forces NTP re-sync; normal after reconnect |
| Presence blocker not working | Check `Block Presence On/Off` topic or JSON is configured; verify payload matches `on_values` |
