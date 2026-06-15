# wifimap radar — Android app

A native **Kotlin + Jetpack Compose** app that acts as a **display / controller**
for the ESP32 `wifimap` radar. It does **not** sense anything itself — it polls the
ESP32's `/api/status` JSON feed once per second and draws the same radar (range
rings, device blips, pulsing human-occupancy band) natively, and can trigger the
board's recalibrate / AP-rescan actions.

> Why display-only? Stock Android/iOS phones do **not** expose WiFi Channel State
> Information (CSI), so the human-sensing has to stay on the ESP32. A phone *can*
> scan WiFi APs and BLE devices on its own, but that's a different app — see the
> repo root README "Roadmap". This app keeps the ESP32 as the sensor.

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

1. Flash and power the ESP32 (`../README.md`) and note the IP it prints on serial.
2. Put the phone on the **same WiFi network** as the ESP32.
3. Launch the app, type the ESP32's **IP** (e.g. `192.168.1.42`) in the host field.
   - If you add mDNS to the firmware (optional), `wifimap.local` works too.
4. The radar mirrors the board's web dashboard; **recalibrate** / **rescan APs**
   buttons POST to the board.

The app talks plain **HTTP** to the board, so `AndroidManifest.xml` sets
`android:usesCleartextTraffic="true"`. That's fine for a LAN device; tighten it
with a network-security-config scoped to your subnet if you prefer.

## Status / limitations

- This is a **runnable skeleton**: project structure, networking, models, and the
  native radar are implemented.
- **Not built in CI here** — the authoring environment has no Android SDK and no
  access to the Maven/Google plugin repos, so `./gradlew assembleDebug` was *not*
  run. Build it locally in Android Studio.
- Display only: no WiFi/BLE scanning from the phone, no CSI on the phone.
