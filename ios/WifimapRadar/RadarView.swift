import SwiftUI

// SwiftUI Canvas radar — mirrors the geometry of web/index.html.
struct RadarView: View {
    let status: Status

    private let maxMetres = 20.0
    private let ringMetres: [Double] = [1, 3, 6, 12, 20]

    var body: some View {
        TimelineView(.animation) { timeline in
            Canvas { ctx, size in
                draw(ctx, size, time: timeline.date.timeIntervalSinceReferenceDate)
            }
        }
        .aspectRatio(1, contentMode: .fit)
        .background(Color(red: 0.02, green: 0.06, blue: 0.04))
        .clipShape(Circle())
    }

    private func distFraction(_ d: Double) -> Double {
        let r = log10(1 + max(0, d)) / log10(1 + maxMetres)
        return min(1, r)
    }

    private func draw(_ ctx: GraphicsContext, _ size: CGSize, time: TimeInterval) {
        let cx = size.width / 2, cy = size.height / 2
        let rMax = min(cx, cy) * 0.92
        let center = CGPoint(x: cx, y: cy)
        let grid = Color(red: 0.05, green: 0.78, blue: 0.35)

        // rings
        for m in ringMetres {
            let r = distFraction(m) * rMax
            let rect = CGRect(x: cx - r, y: cy - r, width: 2 * r, height: 2 * r)
            ctx.stroke(Path(ellipseIn: rect), with: .color(grid.opacity(0.35)), lineWidth: 1)
        }
        ctx.stroke(Path { p in
            p.move(to: CGPoint(x: cx - rMax, y: cy)); p.addLine(to: CGPoint(x: cx + rMax, y: cy))
            p.move(to: CGPoint(x: cx, y: cy - rMax)); p.addLine(to: CGPoint(x: cx, y: cy + rMax))
        }, with: .color(grid.opacity(0.35)), lineWidth: 1)

        // human occupancy band
        if status.occupancy != "none" {
            let zoneM: Double = status.occupancy == "near" ? 2.5 : (status.occupancy == "medium" ? 6 : 12)
            let r = distFraction(zoneM) * rMax
            let pulse = 0.25 + 0.45 * status.zoneScore * (0.6 + 0.4 * sin(time * 4))
            let rect = CGRect(x: cx - r, y: cy - r, width: 2 * r, height: 2 * r)
            ctx.stroke(Path(ellipseIn: rect),
                       with: .color(Color(red: 1, green: 0.42, blue: 0.24).opacity(min(0.85, pulse))),
                       lineWidth: 14)
        }

        // blips
        for a in status.aps { blip(ctx, center, rMax, a.distM, 0, a.bearingDeg, a.confidence,
                                    Color(red: 0.24, green: 0.63, blue: 1.0)) }
        for d in status.devicesBle { blip(ctx, center, rMax, d.distM, d.distErrM, d.bearingDeg, d.confidence,
                                          Color(red: 0.22, green: 1.0, blue: 0.08)) }

        // sweep
        let ang = (time.truncatingRemainder(dividingBy: 4) / 4) * 2 * .pi
        let end = CGPoint(x: cx + rMax * cos(ang), y: cy + rMax * sin(ang))
        ctx.stroke(Path { p in p.move(to: center); p.addLine(to: end) },
                   with: .color(Color(red: 0.22, green: 1, blue: 0.08).opacity(0.5)), lineWidth: 3)
    }

    private func blip(_ ctx: GraphicsContext, _ center: CGPoint, _ rMax: Double,
                      _ distM: Double, _ distErrM: Double, _ bearingDeg: Double,
                      _ confidence: Double, _ color: Color) {
        let r = distFraction(distM) * rMax
        let conf = min(1, max(0.05, confidence))
        let halfW = 0.05 + (1 - conf) * 0.5                 // radians
        let errR = (distFraction(distM + distErrM) - distFraction(distM)) * rMax
        let outer = r + max(4, errR)
        let a = bearingDeg * .pi / 180

        var wedge = Path()
        wedge.move(to: center)
        wedge.addArc(center: center, radius: outer,
                     startAngle: .radians(a - halfW), endAngle: .radians(a + halfW), clockwise: false)
        wedge.closeSubpath()
        ctx.fill(wedge, with: .color(color.opacity(0.10 + 0.25 * conf)))

        let x = center.x + r * cos(a), y = center.y + r * sin(a)
        let dot = CGRect(x: x - 3 - 3 * conf, y: y - 3 - 3 * conf,
                         width: 6 + 6 * conf, height: 6 + 6 * conf)
        ctx.fill(Path(ellipseIn: dot), with: .color(color.opacity(0.95)))
    }
}
