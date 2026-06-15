import Foundation

// Minimal async client for the ESP32 endpoints.
struct RadarApi {

    private func baseURL(_ host: String) -> URL? {
        var h = host.trimmingCharacters(in: .whitespaces)
        if h.hasSuffix("/") { h.removeLast() }
        if !h.hasPrefix("http://") && !h.hasPrefix("https://") { h = "http://" + h }
        return URL(string: h)
    }

    func fetchStatus(host: String) async throws -> Status {
        guard let url = baseURL(host)?.appendingPathComponent("api/status") else {
            throw URLError(.badURL)
        }
        var req = URLRequest(url: url)
        req.timeoutInterval = 2
        let (data, resp) = try await URLSession.shared.data(for: req)
        guard let http = resp as? HTTPURLResponse, http.statusCode == 200 else {
            throw URLError(.badServerResponse)
        }
        return try JSONDecoder().decode(Status.self, from: data)
    }

    func post(host: String, path: String) async {
        guard let url = baseURL(host)?.appendingPathComponent(path) else { return }
        var req = URLRequest(url: url)
        req.httpMethod = "POST"
        req.timeoutInterval = 2
        _ = try? await URLSession.shared.data(for: req)
    }

    func recalibrate(host: String) async { await post(host: host, path: "api/recalibrate") }
    func rescan(host: String) async { await post(host: host, path: "api/apscan") }
}
