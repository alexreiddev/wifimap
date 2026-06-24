import SwiftUI

struct ContentView: View {
    @StateObject private var model = RadarModel()
    @State private var showSettings = false
    @State private var showLegend = false

    private let accent = Color(red: 0.22, green: 1, blue: 0.08)

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("◎ WIFIMAP RADAR").font(.system(.headline, design: .monospaced))
                    .foregroundColor(accent)
                Spacer()
                Button("legend") { showLegend = true }
                Button("settings") { showSettings = true }
            }

            Picker("Source", selection: Binding(
                get: { model.source },
                set: { model.setSource($0) })) {
                Text("ESP32 sensor").tag(RadarModel.Source.esp32)
                Text("This phone").tag(RadarModel.Source.phone)
            }
            .pickerStyle(.segmented)

            HStack(spacing: 8) {
                pill("presence: \(model.status.presence ? "YES" : "no")", on: model.status.presence)
                pill("zone: \(model.status.occupancy)", on: false)
                if model.source == .esp32 {
                    pill(model.status.csiCalibrated ? "calibrated" : "calibrating…",
                         on: !model.status.csiCalibrated)
                } else {
                    pill("human layer: needs ESP32", on: false)
                }
            }
            .font(.system(.caption, design: .monospaced))

            connBanner

            if model.source == .esp32 {
                HStack {
                    Button("recalibrate") { model.recalibrate() }.buttonStyle(.bordered)
                    Button("rescan APs") { model.rescan() }.buttonStyle(.bordered)
                }
            }

            RadarView(status: model.status)

            List {
                Section("Devices — BLE (\(model.status.devicesBle.count))") {
                    ForEach(model.status.devicesBle.sorted { $0.distM < $1.distM }) { d in
                        Text("\(d.name.isEmpty ? d.mac : d.name)  \(d.rssi)dBm  \(String(format: "%.1f", d.distM))m")
                            .font(.system(.caption, design: .monospaced))
                    }
                }
                if model.source == .esp32 {
                    Section("WiFi access points (\(model.status.aps.count))") {
                        ForEach(model.status.aps.sorted { $0.rssi > $1.rssi }) { a in
                            Text("\(a.ssid.isEmpty ? "(hidden)" : a.ssid)  ch\(a.ch)  \(a.rssi)dBm")
                                .font(.system(.caption, design: .monospaced))
                        }
                    }
                }
            }
            .listStyle(.plain)
        }
        .padding()
        .background(Color(red: 0.02, green: 0.07, blue: 0.04).ignoresSafeArea())
        .foregroundColor(Color(red: 0.75, green: 1, blue: 0.85))
        .onAppear { model.start() }
        .onDisappear { model.stop() }
        .sheet(isPresented: $showSettings) { settingsSheet }
        .alert("How to read the radar", isPresented: $showLegend) {
            Button("got it", role: .cancel) {}
        } message: {
            Text("Green = BLE device, blue = WiFi AP, orange band = human motion (ESP32 CSI). "
                + "Ring = distance (measured). Bearing is ESTIMATED — one antenna can't measure direction.")
        }
    }

    private var connBanner: some View {
        let txt: String
        switch model.source {
        case .phone: txt = "Scanning with this phone (BLE)"
        default:
            switch model.connState {
            case "connected": txt = "Connected to sensor"
            case "connecting": txt = "Connecting…"
            case "error": txt = "Can't reach the sensor — retrying…"
            default: txt = "Idle"
            }
        }
        return Text(txt).font(.system(.caption, design: .monospaced))
            .foregroundColor(model.connState == "error" && model.source == .esp32 ? .red : .secondary)
    }

    private var settingsSheet: some View {
        NavigationView {
            Form {
                Section("Sensor host") {
                    TextField("wifimap.local or IP", text: Binding(
                        get: { model.host }, set: { model.setHost($0) }))
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                }
                Section {
                    Text("On the same WiFi, the board is usually reachable at wifimap.local. "
                        + "Phone mode maps BLE devices without any ESP32.")
                        .font(.caption).foregroundColor(.secondary)
                }
            }
            .navigationTitle("Settings")
        }
    }

    private func pill(_ text: String, on: Bool) -> some View {
        Text(text)
            .padding(.horizontal, 8).padding(.vertical, 4)
            .background(on ? accent.opacity(0.9) : Color.white.opacity(0.06))
            .foregroundColor(on ? .black : Color(red: 0.75, green: 1, blue: 0.85))
            .clipShape(Capsule())
    }
}

#Preview {
    ContentView()
}
