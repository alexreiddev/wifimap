import Combine
import Foundation

@MainActor
final class RadarModel: ObservableObject {
    enum Source { case esp32, phone }

    @Published var host: String = UserDefaults.standard.string(forKey: "host") ?? "wifimap.local"
    @Published var source: Source = .esp32
    @Published var status = Status()
    @Published var connState: String = "idle"

    private let api = RadarApi()
    private let ble = BleScanner()
    private var pollTask: Task<Void, Never>?
    private var cancellables = Set<AnyCancellable>()

    init() {
        ble.$devices
            .receive(on: RunLoop.main)
            .sink { [weak self] devs in
                guard let self, self.source == .phone else { return }
                var s = Status()
                s.devicesBle = devs
                s.connected = true
                s.ssid = "this phone"
                self.status = s
            }
            .store(in: &cancellables)
    }

    func start() {
        stop()
        if source == .esp32 {
            startEsp32()
        } else {
            ble.start()
            connState = "scanning"
        }
    }

    private func startEsp32() {
        connState = "connecting"
        pollTask = Task { [weak self] in
            guard let self else { return }
            while !Task.isCancelled {
                do {
                    let s = try await self.api.fetchStatus(host: self.host)
                    self.status = s
                    self.connState = "connected"
                } catch {
                    self.connState = "error"
                }
                try? await Task.sleep(nanoseconds: 1_000_000_000)
            }
        }
    }

    func stop() {
        pollTask?.cancel(); pollTask = nil
        ble.stop()
    }

    func setHost(_ h: String) {
        host = h
        UserDefaults.standard.set(h, forKey: "host")
        if source == .esp32 { start() }
    }

    func setSource(_ s: Source) {
        guard s != source else { return }
        source = s
        status = Status()
        start()
    }

    func recalibrate() { Task { await api.recalibrate(host: host) } }
    func rescan() { Task { await api.rescan(host: host) } }
}
