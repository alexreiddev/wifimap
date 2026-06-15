package com.wifimap.radar

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.net.wifi.WifiManager
import android.os.SystemClock
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.math.pow
import kotlin.math.sqrt

/**
 * Standalone mode: maps nearby devices using the PHONE's own BLE + WiFi radios,
 * producing the same [Status] the ESP32 would. No CSI/human layer here — phones
 * don't expose WiFi CSI — so [Status.occupancy] stays "none".
 *
 * The caller must hold the scan permissions (see [requiredPermissions]).
 */
class LocalScanner(private val context: Context) {

    private val _status = MutableStateFlow(Status(ssid = "this phone", connected = true))
    val status: StateFlow<Status> = _status

    private val bleDevices = LinkedHashMap<String, BleDevice>()
    private var scanJob: Job? = null

    private val bluetooth by lazy {
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }
    private val wifi by lazy {
        context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    }

    private val bleCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val mac = result.device.address ?: return
            val name = runCatching { result.scanRecord?.deviceName }.getOrNull() ?: ""
            bleDevices[mac] = BleDevice(
                mac = mac,
                name = name,
                rssi = result.rssi,
                distM = rssiToDistance(result.rssi),
                distErrM = rssiToDistance(result.rssi) * 0.3f,
                bearingDeg = estimatedBearing(mac),
                confidence = confidenceFor(result.rssi),
            )
        }
    }

    @SuppressLint("MissingPermission")
    fun start(scope: CoroutineScope) {
        if (scanJob?.isActive == true) return
        if (has(Manifest.permission.BLUETOOTH_SCAN) || has(Manifest.permission.ACCESS_FINE_LOCATION)) {
            runCatching {
                bluetooth?.bluetoothLeScanner?.startScan(
                    null,
                    ScanSettings.Builder()
                        .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),
                    bleCallback,
                )
            }
        }
        scanJob = scope.launch {
            while (isActive) {
                @Suppress("DEPRECATION") runCatching { wifi.startScan() }
                val aps = readAccessPoints()
                // Expire BLE entries not seen recently is left to the OS callback churn;
                // here we just publish the current view.
                _status.value = _status.value.copy(
                    devicesBle = bleDevices.values.toList(),
                    aps = aps,
                    occupancy = "none",
                    connected = true,
                    uptime = SystemClock.elapsedRealtime() / 1000,
                )
                delay(1500)
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        scanJob?.cancel(); scanJob = null
        runCatching { bluetooth?.bluetoothLeScanner?.stopScan(bleCallback) }
    }

    @SuppressLint("MissingPermission")
    private fun readAccessPoints(): List<AccessPoint> = runCatching {
        wifi.scanResults.map { r ->
            @Suppress("DEPRECATION") val ssid = r.SSID ?: ""
            AccessPoint(
                ssid = ssid,
                bssid = r.BSSID ?: "",
                rssi = r.level,
                ch = frequencyToChannel(r.frequency),
                distM = rssiToDistance(r.level),
                bearingDeg = estimatedBearing(r.BSSID ?: ssid),
                confidence = confidenceFor(r.level),
            )
        }
    }.getOrDefault(emptyList())

    private fun has(p: String) =
        ContextCompat.checkSelfPermission(context, p) == PackageManager.PERMISSION_GRANTED

    companion object {
        /** Permissions to request before standalone scanning works. */
        val requiredPermissions: Array<String>
            get() = buildList {
                if (android.os.Build.VERSION.SDK_INT >= 31) add(Manifest.permission.BLUETOOTH_SCAN)
                if (android.os.Build.VERSION.SDK_INT >= 33) add(Manifest.permission.NEARBY_WIFI_DEVICES)
                add(Manifest.permission.ACCESS_FINE_LOCATION)
            }.toTypedArray()

        private const val TX_POWER_REF = -59f
        private const val PATH_LOSS_N = 2.5f

        private fun rssiToDistance(rssi: Int): Float {
            val d = 10f.pow((TX_POWER_REF - rssi) / (10f * PATH_LOSS_N))
            return d.coerceAtLeast(0.1f)
        }

        private fun confidenceFor(rssi: Int): Float =
            ((rssi + 100f) / 60f).coerceIn(0f, 1f)

        private fun estimatedBearing(key: String): Float {
            var h = 2166136261u
            for (c in key) h = (h xor c.code.toUInt()) * 16777619u
            val base = (h % 3600u).toFloat() / 10f
            val drift = (SystemClock.elapsedRealtime() / 1000 % 360).toFloat()
            return (base + drift) % 360f
        }

        private fun frequencyToChannel(freq: Int): Int = when {
            freq == 2484 -> 14
            freq in 2412..2472 -> (freq - 2412) / 5 + 1
            freq in 5170..5825 -> (freq - 5170) / 5 + 34
            else -> 0
        }

        @Suppress("unused")
        private fun std(values: List<Int>): Float {
            if (values.size < 2) return 0f
            val m = values.average()
            return sqrt(values.sumOf { (it - m).pow(2) } / (values.size - 1)).toFloat()
        }
    }
}
