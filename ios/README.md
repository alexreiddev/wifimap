# wifimap radar — iOS app (SwiftUI)

A SwiftUI client for the ESP32 `wifimap` radar, with two sources:

- **ESP32 sensor** — polls `/api/status` over HTTP and draws the full radar
  (devices **and** the CSI human-occupancy layer); recalibrate / rescan buttons.
- **This phone** — scans nearby **Bluetooth LE** devices with CoreBluetooth and
  plots them. iOS has **no public WiFi-scan API** and no CSI access, so phone mode
  is BLE-only and the human layer still requires the ESP32.

## Generate & open the project

The Xcode project is described by `project.yml` (no committed `.xcodeproj` to rot):

```bash
brew install xcodegen      # one time
cd ios
xcodegen generate          # creates WifimapRadar.xcodeproj
open WifimapRadar.xcodeproj
```

Then pick a simulator or device and Run. (No XcodeGen? Create a new iOS App in
Xcode and drag the files in `WifimapRadar/` into it; the keys in `project.yml`
under `info.properties` must go into the target's Info.plist.)

## Permissions / networking

`project.yml` (and `Info.plist`) declare:
- `NSAppTransportSecurity → NSAllowsLocalNetworking` so plain HTTP to the board on
  the LAN is allowed.
- `NSLocalNetworkUsageDescription` + `NSBonjourServices = _wifimap._tcp` for local
  discovery.
- `NSBluetoothAlwaysUsageDescription` for BLE scanning in phone mode.

## Using it

- Tap **settings**, enter the board's host (`wifimap.local` or its IP), and the
  radar mirrors the board.
- Switch the source to **This phone** to map BLE devices with no ESP32.

## Status

- Complete SwiftUI scaffold (models, async API client, `Canvas` radar, BLE
  scanner, settings/legend). **Not built here** — this environment has no Xcode.
  Generate and build it on a Mac.
