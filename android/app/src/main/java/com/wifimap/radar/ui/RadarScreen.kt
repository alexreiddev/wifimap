package com.wifimap.radar.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.wifimap.radar.Conn
import com.wifimap.radar.LocalScanner
import com.wifimap.radar.RadarViewModel
import com.wifimap.radar.Source

@Composable
fun RadarScreen(vm: RadarViewModel = viewModel()) {
    val state by vm.state.collectAsStateWithLifecycle()
    var showSettings by remember { mutableStateOf(false) }
    var showLegend by remember { mutableStateOf(false) }

    val permLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { vm.onPermissionsGranted() }

    DisposableEffect(Unit) {
        vm.start()
        onDispose { vm.stop() }
    }

    Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Column(Modifier.fillMaxSize().padding(12.dp)) {

            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "◎ WIFIMAP RADAR", color = MaterialTheme.colorScheme.primary,
                    fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,
                    fontSize = 18.sp, modifier = Modifier.weight(1f),
                )
                TextButton(onClick = { showLegend = true }) { Text("legend") }
                TextButton(onClick = { showSettings = true }) { Text("settings") }
            }

            // data source toggle
            Row(Modifier.padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(
                    selected = state.source == Source.ESP32,
                    onClick = { vm.setSource(Source.ESP32) },
                    label = { Text("ESP32 sensor") },
                    colors = FilterChipDefaults.filterChipColors(),
                )
                FilterChip(
                    selected = state.source == Source.PHONE,
                    onClick = {
                        permLauncher.launch(LocalScanner.requiredPermissions)
                        vm.setSource(Source.PHONE)
                    },
                    label = { Text("this phone") },
                )
            }

            ConnBanner(state.source, state.conn, state.error) { vm.start() }

            // status pills
            val s = state.status
            FlowRow(
                Modifier.fillMaxWidth().padding(vertical = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Pill("presence: ${if (s.presence) "YES" else "no"}", highlight = s.presence)
                Pill("zone: ${s.occupancy}")
                if (state.source == Source.ESP32) {
                    Pill(if (s.csiCalibrated) "calibrated"
                         else "hold still — calibrating ${(10L - s.uptime).coerceAtLeast(0L)}s",
                         highlight = !s.csiCalibrated)
                } else {
                    Pill("human layer: needs ESP32")
                }
            }

            if (state.source == Source.ESP32) {
                Row(Modifier.padding(bottom = 4.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { vm.recalibrate() }) { Text("recalibrate") }
                    Button(onClick = { vm.rescan() }) { Text("rescan APs") }
                }
            }

            RadarCanvas(
                status = s,
                modifier = Modifier.fillMaxWidth().aspectRatio(1f).padding(vertical = 8.dp),
            )

            Mono(
                if (state.source == Source.ESP32)
                    "activity ${"%.2f".format(s.activity)}  conf ${(s.csiConfidence * 100).toInt()}%  packets ${s.csiPackets}"
                else "scanning with this phone's BLE + WiFi radios"
            )

            LazyColumn(Modifier.fillMaxWidth().weight(1f).padding(top = 8.dp)) {
                item { SectionHeader("DEVICES — BLE (${s.devicesBle.size})") }
                if (s.devicesBle.isEmpty()) item { Mono("  (none in range yet)", muted()) }
                items(s.devicesBle.sortedBy { it.distM }) { d ->
                    Mono("${d.name.ifBlank { d.mac }}  ${d.rssi}dBm  " +
                        "${"%.1f".format(d.distM)}±${"%.1f".format(d.distErrM)}m  ${(d.confidence * 100).toInt()}%")
                }
                item { SectionHeader("WIFI ACCESS POINTS (${s.aps.size})") }
                if (s.aps.isEmpty()) item { Mono("  (none yet)", muted()) }
                items(s.aps.sortedByDescending { it.rssi }) { a ->
                    Mono("${a.ssid.ifBlank { "(hidden)" }}  ch${a.ch}  ${a.rssi}dBm  ${"%.1f".format(a.distM)}m")
                }
                item {
                    Mono("Distance is measured (RSSI); bearing is ESTIMATED. " +
                        "Human/CSI sensing requires the ESP32.", muted())
                }
            }
        }
    }

    if (showSettings) SettingsDialog(state.host, state.discovered, onHost = { vm.setHost(it) }) { showSettings = false }
    if (showLegend) LegendDialog { showLegend = false }
}

@Composable
private fun ConnBanner(source: Source, conn: Conn, error: String?, onRetry: () -> Unit) {
    val (text, color) = when {
        source == Source.PHONE -> "Scanning with this phone" to MaterialTheme.colorScheme.surface
        conn == Conn.Connected -> "Connected to sensor" to MaterialTheme.colorScheme.surface
        conn == Conn.Connecting -> "Connecting…" to MaterialTheme.colorScheme.surface
        else -> "Can't reach the sensor (${error ?: "no response"})" to Color(0xFF3A0D0D)
    }
    Surface(color = color, modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Row(Modifier.padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Mono(text, Modifier.weight(1f))
            if (source == Source.ESP32 && conn == Conn.Error) {
                TextButton(onClick = onRetry) { Text("retry") }
            }
        }
    }
}

@Composable
private fun SettingsDialog(
    host: String,
    discovered: List<com.wifimap.radar.DiscoveredBoard>,
    onHost: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var text by remember { mutableStateOf(host) }
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = { onHost(text.trim()); onDismiss() }) { Text("save") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("cancel") } },
        title = { Text("Sensor connection") },
        text = {
            Column {
                OutlinedTextField(
                    value = text, onValueChange = { text = it },
                    label = { Text("ESP32 host / IP") }, singleLine = true,
                )
                if (discovered.isNotEmpty()) {
                    Text("Discovered on this network:", modifier = Modifier.padding(top = 12.dp))
                    discovered.forEach { b ->
                        TextButton(onClick = { text = b.host }) {
                            Text("• ${b.name}  (${b.host})")
                        }
                    }
                } else {
                    Text("Searching the network for boards…",
                        modifier = Modifier.padding(top = 12.dp), color = muted())
                }
            }
        },
    )
}

@Composable
private fun LegendDialog(onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = onDismiss) { Text("got it") } },
        title = { Text("How to read the radar") },
        text = {
            Column {
                Mono("● green = BLE device")
                Mono("● blue = WiFi access point")
                Mono("◓ orange band = human motion zone (ESP32 CSI)")
                Mono("")
                Mono("ring  = distance (measured from RSSI)")
                Mono("wedge = bearing uncertainty")
                Mono("brightness = confidence")
                Mono("")
                Mono("Bearing is ESTIMATED — a single antenna can't measure direction.", muted())
            }
        },
    )
}

@Composable private fun muted() = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.55f)

@Composable
private fun Pill(text: String, highlight: Boolean = false) {
    Surface(
        color = if (highlight) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surface,
        contentColor = if (highlight) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface,
        shape = MaterialTheme.shapes.small,
    ) {
        Text(text, fontFamily = FontFamily.Monospace, fontSize = 12.sp,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp))
    }
}

@Composable
private fun SectionHeader(text: String) = Text(
    text, color = MaterialTheme.colorScheme.primary,
    fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, fontSize = 13.sp,
    modifier = Modifier.padding(top = 10.dp, bottom = 2.dp),
)

@Composable
private fun Mono(text: String, color: Color = MaterialTheme.colorScheme.onBackground) =
    Text(text, color = color, fontFamily = FontFamily.Monospace, fontSize = 12.sp)

@Composable
private fun Mono(text: String, modifier: Modifier, color: Color = MaterialTheme.colorScheme.onBackground) =
    Text(text, modifier = modifier, color = color, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
