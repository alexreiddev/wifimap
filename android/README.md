# wifimap radar — Android app

A native **Kotlin + Jetpack Compose** app with two modes:

- **ESP32 sensor** (default) — polls the board's `/api/status` and draws the full
  radar (devices **and** the CSI human-occupancy layer); can recalibrate / rescan.
  Auto-discovers boards on the network via mDNS — usually no typing needed.
- **This phone** — uses the phone's own **BLE + WiFi** radios to map nearby devices
  with no ESP32 at all. The human/CSI layer is unavailable in this mode because
  stock phones don't expose WiFi Channel State Information.

User-friendly touches: mDNS auto-discovery, remembered host (DataStore),
connecting/connected/error states with retry, a legend dialog, a settings dialog,
runtime permission prompts for phone scanning, and an app icon.

## Architecture

```
Android app ──HTTP GET /api/status (1 Hz)──► ESP32  (same WiFi LAN)
            ──HTTP POST /api/recalibrate──►
            ──HTTP POST /api/apscan──────►
```

- `StatusModels.kt` — data classes mirroring the firmware JSON (`main/web_server.c`).
- `RadarApi.kt` — OkHttp client for the three endpoints.
- `RadarViewModel.kt` — holds the host/IP, runs the 1 Hz polling loop, exposes a
  `StateFlow<UiState>`.
- `ui/RadarCanvas.kt` — Compose `Canvas` radar; mirrors the geometry in
  `web/index.html` (log-distance rings, confidence-weighted bearing wedges,
  occupancy band, sweep line).
- `ui/RadarScreen.kt` — host field, status pills, radar, BLE/AP lists, buttons.

## Build & run

Requires the **Android SDK** (via Android Studio or command-line tools) and JDK 17+.

**Easiest:** open the `android/` folder in **Android Studio** (Giraffe or newer),
let it sync, then Run ▶ on a device/emulator.

**Command line:**
```bash
cd android
./gradlew assembleDebug          # build the debug APK
./gradlew installDebug           # build + install on a connected device
```

The Gradle wrapper (`gradlew`, `gradle/wrapper/gradle-wrapper.jar`) is committed,
so no separate Gradle install is needed — the first run downloads Gradle 8.9.

## Using it

**ESP32 mode:** put the phone on the same WiFi as the board. The app
**auto-discovers** it via mDNS — open **settings** and tap the discovered board, or
just leave the default `wifimap.local`. The radar then mirrors the board, and
**recalibrate** / **rescan APs** POST to it.

**Phone mode:** tap **this phone**; grant the Bluetooth/WiFi/location prompts. The
radar fills from the phone's own BLE + WiFi scans (no human layer).

The app talks plain **HTTP** to the board, so `AndroidManifest.xml` sets
`android:usesCleartextTraffic="true"`. That's fine for a LAN device; tighten it
with a network-security-config scoped to your subnet if you prefer.

## Status / limitations

- Fully featured app (discovery, persistence, two scan sources, settings, legend,
  icon). **Not built in CI here** — the authoring environment has no Android SDK
  and no Maven/Google repo access, so `./gradlew assembleDebug` was *not* run.
  Build it in Android Studio.
- Phone mode is **device-mapping only** (BLE + WiFi); the human/CSI layer needs the
  ESP32 (stock phones don't expose WiFi CSI).
