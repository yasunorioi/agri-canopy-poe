# agri-canopy-poe

[🇯🇵 日本語](README_ja.md) · **English**

A greenhouse canopy imaging node built on the M5Stack PoECAM-W (ESP32-WROVER
+ W5500 + OV2640). A pared-down design that simply captures images on a
schedule and sends them to a WebDAV server via HTTP PUT. The shared
foundation is
[`agri-node-poe-core`](https://github.com/yasunorioi/agri-node-poe-core);
the capture cycle is gated by the solar elevation computed with the NOAA
solar position formula from the UTC picked up via NTP plus the
latitude/longitude entered in `/config`.

> **In v0.3.0 the ADS1110/PVSS-03 pyranometer path was removed**. It became
> unusable due to on-site ADC noise, so the `InRadiation` transmission and
> UECS-CCM output were removed at the same time. Everything is now
> consolidated into the time-based daylight gate.

## Hardware

- **MCU**: M5Stack PoECAM-W (ESP32-WROVER-B, 8MB PSRAM, 16MB flash)
- **Ethernet (PoE)**: built-in Wiznet W5500 (SPI)
- **Camera**: OV2640 (DVP, SCCB)
- **Flashing**: M5 Writer (USB-C) → programming header

⚠ On the PoECAM, GPIO 27 is the camera XCLK, so the ATOM-family WS2812
status LED (agri::Led) cannot be used. Status is available via the Serial
log only.

## Main features

- **DHCP / PoE plug and play** (W5500 SPI SCK=23 MISO=38 MOSI=13 CS=4)
- **agriha-schema MQTT publisher**
  - `<prefix>/sensor/CamShot`  (URL string, viewing URL of the most recent upload)
- **Camera capture → WebDAV upload**
  - Default 30-minute interval (`cam_interval_s`)
  - Resolution selectable from VGA / SVGA / XGA / HD / SXGA (default) / UXGA
  - JPEG quality 10 (best) to 30 (small)
  - Skips while the solar elevation is at or below `sun_elev_min_deg`
    (default 5°) (`cam_daylight_only`)
  - PUT URL: `<wd_url>/<hostname>/<YYYY>/<MMDDHHMMSS>.jpg` (JST)
  - Publishes an auth-free viewing URL to MQTT `CamShot`
    (base with the trailing `/upload` stripped from `<wd_url>` + tail)
- **Web UI** 3 pages + JSON API — edit camera settings / latitude-longitude / WebDAV credentials
- **ArduinoOTA** over Ethernet (hostname `agri-canopy-XX`)
- **GitHub Release self-update** — one-click update from the Dashboard

## MQTT topics

`<prefix>` is the agriha house partition (e.g. `agriha/2`). All topics are retained.

| Topic | Type / unit | Description |
|---|---|---|
| `<prefix>/sensor/CamShot`     | string `url`  | Viewing URL of the most recent successful WebDAV upload |

The value of `CamShot` is a URL string (in JSON, `"value":"http://..."`).
The ArSprout dashboard is expected to display it by pasting it directly into `<img src>`.

## WebDAV server side

For the receiving nginx configuration and install script, see
[`canopy-webdav-server`](https://github.com/yasunorioi/canopy-webdav-server)
(currently private / unpublished, kept locally at `~/canopy-webdav-server/`).

Overview:

```
PUT  http://<server>:<port>/upload/<node>/<YYYY>/<MMDDHHMMSS>.jpg   (Basic auth)
GET  http://<server>:<port>/<node>/<YYYY>/<MMDDHHMMSS>.jpg          (auth 無し)
```

Intermediate directories are created automatically via nginx-extras'
`ngx_http_dav_module` + `create_full_put_path on`.

## Daylight gate

The NOAA solar position formula (equation of time + declination) is
implemented in `sun.h`. `time(nullptr)` gives the UTC epoch → `gmtime_r` →
day-of-year + hour_utc →
`gamma → eqtime → decl → hour angle → cos(zenith) → elevation`.
It is computed once per minute and held in `g_sun_elev_deg`, shared across
the capture gate, Dashboard, and JSON status.

When SNTP is not yet synced (`time(nullptr) < 1700000000`) it returns NaN.
In this case the gate is treated as "pass" so that captures right after
boot are not stopped (in practice the first shot becomes a test shot).

## Persistent configuration (NVS)

`Preferences` namespace `canopy-cfg`. Edit from `/config`:

- **Common** (`agri::CommonConfig`): Node ID / hostname / MQTT host, port, user,
  pass, topic prefix, interval / CCM enable, interval, room, region, priority, ntype
  (CCM is inert since the current firmware does not publish it)
- **Camera**: `cam_en` / `cam_int_s` / `cam_res` / `cam_jq` / `cam_dl_only`
- **Location**: `lat` / `lon` / `sun_elev` (deg)
- **WebDAV**: `wd_url` / `wd_user` / `wd_pass`

Defaults are: MQTT host empty, wd_url empty, camera ON @ 30 min SXGA Q12,
daylight-only ON @ 5° solar elevation threshold, lat=35.0 / lon=135.0 (needs updating).

## Build and flash

```
pio run -e poecam-canopy -t upload                                   # 初回 USB
pio run -e poecam-canopy -t upload --upload-port agri-canopy-01.local  # OTA
```

> 🛠 **Build environment (shared Windows / Linux) and first-time Linux setup (udev, etc.)** →
> [agri-node-poe-core/docs/cross-platform-build.md](https://github.com/yasunorioi/agri-node-poe-core/blob/main/docs/cross-platform-build.md)

After flashing, access the UI at `http://agri-canopy-01.local/`.
You can update via GitHub Release from the Update button.

## PoECAM pinout (reference)

The finalized pin list, verified in `~/poecam-bringup/`:

**W5500** (SPI): SCK=23, MISO=38, MOSI=13, CS=4, RST=-1, INT=-1

**OV2640** (DVP + SCCB):
SIOD=14, SIOC=12, XCLK=27, PCLK=21, VSYNC=22, HREF=26,
D0=32, D1=35, D2=34, D3=5, D4=39, D5=18, D6=36, D7=19,
RESET=15, PWDN=-1
