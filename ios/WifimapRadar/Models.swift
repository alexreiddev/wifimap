import Foundation

// Mirrors the ESP32 /api/status JSON (see main/web_server.c). Defaults make every
// field optional-safe so firmware additions don't break decoding.
struct Status: Codable {
    var presence: Bool = false
    var activity: Double = 0
    var occupancy: String = "none"
    var zoneScore: Double = 0
    var csiConfidence: Double = 0
    var csiCalibrated: Bool = false
    var csiPackets: Int = 0
    var uptime: Int = 0
    var connected: Bool = false
    var ssid: String = ""
    var devicesBle: [BleDevice] = []
    var aps: [AccessPoint] = []

    enum CodingKeys: String, CodingKey {
        case presence, activity, occupancy, uptime, connected, ssid, aps
        case zoneScore = "zone_score"
        case csiConfidence = "csi_confidence"
        case csiCalibrated = "csi_calibrated"
        case csiPackets = "csi_packets"
        case devicesBle = "devices_ble"
    }

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        presence = (try? c.decode(Bool.self, forKey: .presence)) ?? false
        activity = (try? c.decode(Double.self, forKey: .activity)) ?? 0
        occupancy = (try? c.decode(String.self, forKey: .occupancy)) ?? "none"
        zoneScore = (try? c.decode(Double.self, forKey: .zoneScore)) ?? 0
        csiConfidence = (try? c.decode(Double.self, forKey: .csiConfidence)) ?? 0
        csiCalibrated = (try? c.decode(Bool.self, forKey: .csiCalibrated)) ?? false
        csiPackets = (try? c.decode(Int.self, forKey: .csiPackets)) ?? 0
        uptime = (try? c.decode(Int.self, forKey: .uptime)) ?? 0
        connected = (try? c.decode(Bool.self, forKey: .connected)) ?? false
        ssid = (try? c.decode(String.self, forKey: .ssid)) ?? ""
        devicesBle = (try? c.decode([BleDevice].self, forKey: .devicesBle)) ?? []
        aps = (try? c.decode([AccessPoint].self, forKey: .aps)) ?? []
    }
}

struct BleDevice: Codable, Identifiable {
    var id: String { mac }
    var mac: String = ""
    var name: String = ""
    var rssi: Int = 0
    var distM: Double = 0
    var distErrM: Double = 0
    var bearingDeg: Double = 0
    var confidence: Double = 0

    enum CodingKeys: String, CodingKey {
        case mac, name, rssi
        case distM = "dist_m"
        case distErrM = "dist_err_m"
        case bearingDeg = "bearing_deg"
        case confidence
    }
}

struct AccessPoint: Codable, Identifiable {
    var id: String { bssid }
    var ssid: String = ""
    var bssid: String = ""
    var rssi: Int = 0
    var ch: Int = 0
    var distM: Double = 0
    var bearingDeg: Double = 0
    var confidence: Double = 0

    enum CodingKeys: String, CodingKey {
        case ssid, bssid, rssi, ch
        case distM = "dist_m"
        case bearingDeg = "bearing_deg"
        case confidence
    }
}
