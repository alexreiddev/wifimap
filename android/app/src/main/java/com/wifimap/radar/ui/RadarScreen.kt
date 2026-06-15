package com.wifimap.radar.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.wifimap.radar.RadarViewModel

@Composable
fun RadarScreen(vm: RadarViewModel = viewModel()) {
    val state by vm.state.collectAsStateWithLifecycle()

    DisposableEffect(Unit) {
        vm.start()
        onDispose { vm.stop() }
    }

    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Column(Modifier.fillMaxSize().padding(12.dp)) {
            Text(
                "◎ WIFIMAP RADAR",
                color = MaterialTheme.colorScheme.primary,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 18.sp,
            )

            Row(
                Modifier.fillMaxWidth().padding(vertical = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                OutlinedTextField(
                    value = state.host,
                    onValueChange = vm::setHost,
                    label = { Text("ESP32 host / IP") },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                )
            }

            // status pills
            val s = state.status
            Row(
                Modifier.fillMaxWidth().padding(bottom = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Pill(if (state.connected) "link OK" else "link …")
                Pill("presence: ${if (s.presence) "YES" else "no"}", highlight = s.presence)
                Pill("zone: ${s.occupancy}")
                Pill(if (s.csiCalibrated) "calibrated" else "calibrating…")
            }

            Row(
                Modifier.fillMaxWidth().padding(bottom = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Button(onClick = { vm.recalibrate() }) { Text("recalibrate") }
                Button(onClick = { vm.rescan() }) { Text("rescan APs") }
            }

            RadarCanvas(
                status = s,
                modifier = Modifier.fillMaxWidth().aspectRatio(1f).padding(vertical = 8.dp),
            )

            Mono("activity ${"%.2f".format(s.activity)}  conf ${(s.csiConfidence * 100).toInt()}%  " +
                "packets ${s.csiPackets}  uptime ${s.uptime}s")

            state.error?.let { Mono("error: $it", color = MaterialTheme.colorScheme.secondary) }

            LazyColumn(Modifier.fillMaxWidth().weight(1f).padding(top = 8.dp)) {
                item { SectionHeader("BLE DEVICES (${s.devicesBle.size})") }
                items(s.devicesBle.sortedBy { it.distM }) { d ->
                    Mono("${d.name.ifBlank { d.mac }}  ${d.rssi}dBm  " +
                        "${"%.1f".format(d.distM)}±${"%.1f".format(d.distErrM)}m  " +
                        "${(d.confidence * 100).toInt()}%")
                }
                item { SectionHeader("WIFI ACCESS POINTS (${s.aps.size})") }
                items(s.aps.sortedByDescending { it.rssi }) { a ->
                    Mono("${a.ssid.ifBlank { "(hidden)" }}  ch${a.ch}  ${a.rssi}dBm  " +
                        "${"%.1f".format(a.distM)}m")
                }
                item {
                    Mono(
                        "Distance is measured (RSSI); bearing is ESTIMATED. CSI human " +
                            "sensing runs on the ESP32 — this app is the display.",
                        color = MaterialTheme.colorScheme.secondary,
                    )
                }
            }
        }
    }
}

@Composable
private fun Pill(text: String, highlight: Boolean = false) {
    Surface(
        color = if (highlight) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surface,
        contentColor = if (highlight) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface,
        shape = MaterialTheme.shapes.small,
    ) {
        Text(
            text, fontFamily = FontFamily.Monospace, fontSize = 12.sp,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
        )
    }
}

@Composable
private fun SectionHeader(text: String) =
    Text(
        text, color = MaterialTheme.colorScheme.primary,
        fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, fontSize = 13.sp,
        modifier = Modifier.padding(top = 10.dp, bottom = 2.dp),
    )

@Composable
private fun Mono(text: String, color: androidx.compose.ui.graphics.Color = MaterialTheme.colorScheme.onBackground) =
    Text(text, color = color, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
