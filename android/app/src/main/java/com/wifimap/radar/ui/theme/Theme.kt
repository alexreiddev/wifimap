package com.wifimap.radar.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val RadarColors = darkColorScheme(
    primary = Color(0xFF39FF14),
    secondary = Color(0xFF3CA0FF),
    background = Color(0xFF04110A),
    surface = Color(0xFF06190F),
    onPrimary = Color(0xFF002200),
    onBackground = Color(0xFFBFFFD8),
    onSurface = Color(0xFFBFFFD8),
)

@Composable
fun WifimapRadarTheme(content: @Composable () -> Unit) {
    // Always use the dark radar palette regardless of system setting.
    MaterialTheme(colorScheme = RadarColors, content = content)
}
