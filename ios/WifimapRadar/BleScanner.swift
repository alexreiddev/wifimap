import CoreBluetooth
import Foundation

// Phone-side BLE mapping (CoreBluetooth). iOS hides hardware MACs behind a
// per-device UUID, and there is no public WiFi-scan API on iOS, so phone mode is
// BLE-only and the human/CSI layer still comes from the ESP32.
final class BleScanner: NSObject, ObservableObject, CBCentralManagerDelegate {

    @Published var devices: [BleDevice] = []

    private var central: CBCentralManager!
    private var table: [UUID: BleDevice] = [:]

    private let txPowerRef = -59.0
    private let pathLossN = 2.5

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    func start() {
        if central.state == .poweredOn {
            central.scanForPeripherals(withServices: nil,
                options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        }
    }

    func stop() { central.stopScan() }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn { start() }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let rssi = RSSI.intValue
        let dist = pow(10.0, (txPowerRef - Double(rssi)) / (10.0 * pathLossN))
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String)
            ?? peripheral.name ?? ""
        let key = peripheral.identifier
        table[key] = BleDevice(
            mac: key.uuidString,
            name: name,
            rssi: rssi,
            distM: max(0.1, dist),
            distErrM: max(0.1, dist) * 0.3,
            bearingDeg: Self.estimatedBearing(key.uuidString),
            confidence: min(1, max(0, (Double(rssi) + 100) / 60))
        )
        devices = Array(table.values)
    }

    private static func estimatedBearing(_ key: String) -> Double {
        var h: UInt32 = 2166136261
        for b in key.utf8 { h = (h ^ UInt32(b)) &* 16777619 }
        let base = Double(h % 3600) / 10.0
        let drift = Double(Int(Date().timeIntervalSince1970) % 360)
        return (base + drift).truncatingRemainder(dividingBy: 360)
    }
}
