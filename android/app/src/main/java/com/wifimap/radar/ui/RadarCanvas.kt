package com.wifimap.radar.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import com.wifimap.radar.AccessPoint
import com.wifimap.radar.BleDevice
import com.wifimap.radar.Status
import kotlin.math.cos
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

private val Grid = Color(0xFF0CC85A)
private val Blip = Color(0xFF39FF14)
private val ApBlip = Color(0xFF3CA0FF)
private val Human = Color(0xFFFF6B3D)

private const val MAX_METRES = 20f
private val RING_METRES = listOf(1f, 3f, 6f, 12f, 20f)

/** Log-distance → fraction of the outer radius (matches web/index.html distToR). */
private fun distFraction(d: Float): Float {
    val r = log10(1f + max(0f, d)) / log10(1f + MAX_METRES)
    return min(1f, r)
}

@Composable
fun RadarCanvas(status: Status, modifier: Modifier = Modifier) {
    val transition = rememberInfiniteTransition(label = "radar")
    val sweep by transition.animateFloat(
        initialValue = 0f, targetValue = 360f,
        animationSpec = infiniteRepeatable(tween(4000, easing = LinearEasing), RepeatMode.Restart),
        label = "sweep",
    )
    val pulse by transition.animateFloat(
        initialValue = 0f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(900, easing = LinearEasing), RepeatMode.Reverse),
        label = "pulse",
    )

    Canvas(modifier = modifier) {
        val cx = size.width / 2f
        val cy = size.height / 2f
        val rMax = min(cx, cy) * 0.92f
        val center = Offset(cx, cy)

        // range rings + labels
        for (m in RING_METRES) {
            val r = distFraction(m) * rMax
            drawCircle(Grid.copy(alpha = 0.35f), r, center, style = Stroke(width = 1.5f))
            drawContext.canvas.nativeCanvas.drawText(
                "${m.toInt()}m", cx + 4f, cy - r + 28f,
                android.graphics.Paint().apply {
                    color = android.graphics.Color.argb(130, 120, 255, 180)
                    textSize = 26f
                },
            )
        }
        // crosshairs
        drawLine(Grid.copy(alpha = 0.35f), Offset(cx - rMax, cy), Offset(cx + rMax, cy))
        drawLine(Grid.copy(alpha = 0.35f), Offset(cx, cy - rMax), Offset(cx, cy + rMax))

        // CSI human occupancy band
        if (status.occupancy != "none") {
            val zoneR = when (status.occupancy) {
                "near" -> distFraction(2.5f)
                "medium" -> distFraction(6f)
                else -> distFraction(12f)
            } * rMax
            val a = (0.25f + 0.5f * status.zoneScore * (0.6f + 0.4f * pulse)).coerceIn(0f, 0.85f)
            drawCircle(Human.copy(alpha = a), zoneR, center, style = Stroke(width = 28f))
        }

        // device blips (APs then BLE on top)
        status.aps.forEach { drawBlip(center, rMax, it.distM, 0f, it.bearingDeg, it.confidence, ApBlip) }
        status.devicesBle.forEach {
            drawBlip(center, rMax, it.distM, it.distErrM, it.bearingDeg, it.confidence, Blip)
        }

        // sweep line
        val rad = Math.toRadians(sweep.toDouble())
        val end = Offset(cx + rMax * cos(rad).toFloat(), cy + rMax * sin(rad).toFloat())
        drawLine(
            brush = Brush.linearGradient(
                listOf(Blip.copy(alpha = 0.5f), Color.Transparent),
                start = center, end = end,
            ),
            start = center, end = end, strokeWidth = 4f,
        )
    }
}

private fun DrawScope.drawBlip(
    center: Offset, rMax: Float,
    distM: Float, distErrM: Float, bearingDeg: Float, confidence: Float, color: Color,
) {
    val r = distFraction(distM) * rMax
    val a = Math.toRadians(bearingDeg.toDouble())
    val conf = confidence.coerceIn(0.05f, 1f)
    // bearing uncertainty: wide wedge when confidence is low (ESTIMATED bearing).
    val halfW = (0.05f + (1f - conf) * 0.5f)        // radians
    val errR = (distFraction(distM + distErrM) - distFraction(distM)) * rMax

    val sweepDeg = Math.toDegrees((halfW * 2).toDouble()).toFloat()
    val startDeg = bearingDeg - Math.toDegrees(halfW.toDouble()).toFloat()
    val outer = r + max(4f, errR)
    val path = Path().apply {
        moveTo(center.x, center.y)
        arcTo(
            rect = Rect(center.x - outer, center.y - outer, center.x + outer, center.y + outer),
            startAngleDegrees = startDeg, sweepAngleDegrees = sweepDeg, forceMoveTo = false,
        )
        close()
    }
    drawPath(path, color.copy(alpha = 0.10f + 0.25f * conf))

    val x = center.x + r * cos(a).toFloat()
    val y = center.y + r * sin(a).toFloat()
    drawCircle(color.copy(alpha = 0.95f), 3f + 3f * conf, Offset(x, y))
}
