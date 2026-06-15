# wifimap

Turn a single **ESP32 dev board** into a live "radar" of the electronic devices —
and the humans — around it, using only the chip's built-in **WiFi** and
**Bluetooth** radios.

- **Electronic devices** are found by scanning WiFi access points and BLE
  advertisements (phones, watches, earbuds, trackers). Distance is estimated from
  signal strength (RSSI).
- **Humans** are sensed with experimental **WiFi Channel State Information (CSI)**.
  WiFi passes through walls (*permeability*) and a moving body changes how the
  signal arrives (*attenuation/amplification*), so CSI flags motion/occupancy that
  device scans can't see.
- Everything is shown on a self-hosted **radar dashboard** (open the ESP32's IP in
  a browser): range rings in metres, device blips, and a pulsing human-occupancy
  band.

```
   browser ──HTTP──► ESP32 ──WiFi STA──► your router (ping → steady CSI)
                       │
                       ├── CSI rx callback → motion / occupancy zones (humans)
                       ├── BLE passive scan → nearby devices + distance
                       └── WiFi scan        → nearby access points
```

## What it can and cannot do (read this)

| Capability | Status |
|---|---|
| Detect & count nearby BLE / WiFi **devices** | ✅ reliable |
| Estimate **distance** to each device (RSSI) | ✅ rough (±, log-distance model) |
| Detect **human motion / presence** via CSI | ⚠️ experimental, needs calibration |
| Coarse **through-wall occupancy zone** (near/medium/far) | ⚠️ heuristic |
| True **direction / bearing** to a target | ❌ not possible with one antenna |
| Exact **X/Y positions** / head-count | ❌ needs multiple nodes / an array |

The radar's **distance is measured; bearing is estimated/animated** and clearly
labelled as such in the UI. Real angle-of-arrival would need an antenna array or
several ESP32 nodes (trilateration) — see *Roadmap*.

## Requirements

- A classic **ESP32** dev board (e.g. ESP32-WROOM-32) with WiFi + BLE.
- **ESP-IDF v5.x** installed ([setup guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)).
- A 2.4 GHz WiFi network for the board to join (used both to serve the dashboard
  and to generate the ping traffic that keeps CSI flowing).

## Configure

Edit `main/app_config.h`:

```c
#define WIFI_SSID  "your-ssid"
#define WIFI_PASS  "your-password"
```

Other tunables (calibration time, CSI thresholds, RSSI distance model, scan
cadence) live in the same file with comments.

## Build, flash, run

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor      # use your serial port
```

On boot the serial log prints the dashboard URL, e.g.:

```
I (…) wifimap: ready — open the radar at http://192.168.1.42/
I (…) wifimap: calibrating CSI baseline for 10 s; keep the area still
```

**Keep the area still for the first ~10 seconds** so the empty-room CSI baseline
is learned. Then open that URL on a phone/laptop on the same network.

## Using the radar

- **Green blips** = electronic devices (BLE green, WiFi APs blue). They sit on the
  ring matching their distance; the wedge widens when confidence is low.
- **Orange band** = CSI-detected human motion zone (near/medium/far), pulsing with
  intensity. This is the through-wall human layer.
- **recalibrate** button — re-learn the empty-room baseline (do this after moving
  the board or if false motion creeps in).
- **rescan APs** button — force an immediate WiFi access-point scan.

### Tuning human sensing
CSI is environment-sensitive. If you get false positives/negatives, adjust in
`app_config.h`:
- `CSI_PRESENCE_K` — higher = less sensitive.
- `CSI_ZONE_*_Z` — thresholds for the near/medium/far bands.
- `CSI_CALIB_SECONDS`, `CSI_WINDOW_LEN` — calibration length / smoothing.

## How it works (brief)

- `wifi_csi.c` joins your WiFi, enables CSI (`esp_wifi_set_csi`), and pings the
  gateway every `PING_INTERVAL_MS` so RX packets — and CSI samples — keep arriving.
- `csi_process.c` computes per-packet subcarrier amplitude, tracks its sliding-
  window variance ("activity"), learns an empty-room baseline, then flags
  presence and maps the perturbation magnitude to occupancy zones.
- `ble_scan.c` runs a NimBLE passive scan, maintaining a device table with RSSI
  distance, confidence (from signal strength + stability) and an estimated bearing.
- `wifi_apscan.c` periodically scans nearby APs.
- `web_server.c` serves the embedded `web/index.html` radar and a `/api/status`
  JSON feed polled once per second.

## Roadmap (not implemented)

- Multiple ESP32 nodes → trilateration for **real X/Y positions**.
- Antenna array / phase processing → **real bearing (AoA)**.
- MQTT / cloud export of the device + occupancy stream.

## Legal / ethical note

Passive scanning of WiFi/BLE and CSI sensing observe radio signals in your own
space. Respect local laws and others' privacy: use it on networks/areas you own
or are authorized to monitor, and don't use it to track individuals without
consent.
