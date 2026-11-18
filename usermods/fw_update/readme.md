# fw_update

Gives every build a timestamp build ID and lets the device update itself from firmware builds stored in a Gitea repository.

- **Build ID:** each `pio run` sets `FW_UPDATE_BUILD_ID` = local build time `YYMMDDHHMMSS` (e.g. `261117194410`). Only `fw_update.cpp` is recompiled for it. WLED's own `VERSION`/`vid` is not changed.
- **Publishing (optional):** after the build, `firmware.bin` is copied to `<repo clone>/<WLED_RELEASE_NAME>/<build id>.bin`. Only the newest 10 builds per device are kept. The files are committed to branch `staging` and pushed, and a pull request `staging` → `main` is opened if none is open. Merging the PR makes the builds available to the devices.
- **On the device:** the Info panel shows the running build, the newest build on `main` and the status. You can install any listed build (newer or older) with one click. Optionally, newer builds are installed automatically as soon as a check finds them.

ESP32 family only (ESP32, S2, S3, C3). On ESP8266, or with `WLED_DISABLE_OTA`, the usermod only shows the build ID.

## Setup

### 1. Firmware repository
Create an empty repository, for example `KrX/wled-firmware` on Gitea, and clone it on the PC you build on. A public repository needs no token on the devices.

### 2. Build
Add `fw_update` to `custom_usermods` of each device env. To publish builds, point the script to the clone with **one** of these:

```ini
; platformio_override.ini, in the env or in [env]
custom_fw_update_repo = C:/Users/me/git/wled-firmware
```
or set the environment variable `WLED_FW_REPO` to that path.

Requirements: `git` in PATH and a stored credential for the repo (`git credential fill` must return a token or password; the token is also used to open the PR). Each env needs a `WLED_RELEASE_NAME` made of `A-Z a-z 0-9 . _ -`, because it is used as the folder name. If publishing fails, a yellow warning is printed and the build still succeeds.

### 3. Device settings (Config → Usermods → fw_update)

| Setting | Default | Meaning |
|---|---|---|
| enabled | off | |
| baseUrl | `http://192.168.90.70:3000` | Gitea base URL, `http://` only |
| repo | `KrX/wled-firmware` | owner/repo |
| branch | `main` | |
| token | empty | Optional read-only token for a private repo. It is stored in `cfg.json`, which anyone on the LAN can read via `/edit`. |
| intervalMin | 60 | Check interval in minutes, 0 = only with the Check button. The first check runs 1 minute after boot. |
| autoInstall | off | Install a newer build as soon as a check finds it. Never installs older builds, and never retries a build that failed. |

## Info panel
- **FW build**: build ID of the running firmware
- **FW newest**: newest build on the branch
- **FW status**: up to date / update available / local is newer / no builds published / unreachable / downloading / failed + reason
- **FW last update**: result of the last install (installed or failed), kept until you press **Dismiss**
- **FW install**: picker with the listed builds (newer / running / older), **Install** and **Check** buttons

The UI does not refresh the Info panel by itself; reopen it to see a new status.

## How an install is checked
1. The file must be listed in `<release name>/` on the branch, and the download size must match the listing.
2. Before anything is written to flash, the WLED metadata in the file must match this device's release name (same check as a web upload).
3. While downloading, the git blob SHA-1 is computed and compared with the `sha` that Gitea lists.
4. ESP-IDF verifies the image before switching to it. After the reboot the device compares the running build ID with the one it installed.

An install is refused while **OTA lock** is enabled in the Security settings. To go back to the previous firmware, use **Revert** on the update page, or install an older build from the picker.

## Repository size
Builds removed from `main` stay in the git history, and Gitea keeps every PR's commits, so the repository keeps growing: about 1.5 MB per build. To start over, delete the repository in Gitea, create it again empty, and build again. Devices only need the files that are on `main`.

## JSON API
`POST /json/state` with `{"fw_update":{"check":true}}`, `{"fw_update":{"install":"261117194410"}}` or `{"fw_update":{"dismiss":true}}`.
