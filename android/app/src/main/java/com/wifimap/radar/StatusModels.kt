package com.wifimap.radar

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * Mirrors the JSON produced by the ESP32 firmware in `main/web_server.c`
 * (`status_get`). Unknown keys are ignored by the Json config in [RadarApi] so the
 * firmware can add fields without breaking the app.
 */
@Serializable
data class Status(
    val presence: Boolean = false,
    val activity: Float = 0f,
    val occupancy: String = "none",
    @SerialName("zone_score") val zoneScore: Float = 0f,
    @SerialName("csi_confidence") val csiConfidence: Float = 0f,
    @SerialName("csi_calibrated") val csiCalibrated: Boolean = false,
    @SerialName("csi_packets") val csiPackets: Long = 0,
    val uptime: Long = 0,
    @SerialName("devices_ble") val devicesBle: List<BleDevice> = emptyList(),
    val aps: List<AccessPoint> = emptyList(),
)

@Serializable
data class BleDevice(
    val mac: String = "",
    val name: String = "",
    val rssi: Int = 0,
    @SerialName("dist_m") val distM: Float = 0f,
    @SerialName("dist_err_m") val distErrM: Float = 0f,
    @SerialName("bearing_deg") val bearingDeg: Float = 0f,
    val confidence: Float = 0f,
)

@Serializable
data class AccessPoint(
    val ssid: String = "",
    val bssid: String = "",
    val rssi: Int = 0,
    val ch: Int = 0,
    @SerialName("dist_m") val distM: Float = 0f,
    @SerialName("bearing_deg") val bearingDeg: Float = 0f,
    val confidence: Float = 0f,
)
