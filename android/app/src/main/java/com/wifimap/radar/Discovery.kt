package com.wifimap.radar

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.util.Log
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow

data class DiscoveredBoard(val name: String, val host: String, val port: Int)

private const val SERVICE_TYPE = "_wifimap._tcp."

/**
 * Browses the LAN for wifimap boards advertised via mDNS (`_wifimap._tcp`) and
 * emits the running list as they're found/resolved. Cancel the collector to stop.
 */
fun discoverBoards(context: Context): Flow<List<DiscoveredBoard>> = callbackFlow {
    val nsd = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    val found = LinkedHashMap<String, DiscoveredBoard>()

    fun emit() = trySend(found.values.toList())

    val resolveListener = object : NsdManager.ResolveListener {
        override fun onServiceResolved(info: NsdServiceInfo) {
            val host = info.host?.hostAddress ?: return
            found[info.serviceName] = DiscoveredBoard(info.serviceName, host, info.port)
            emit()
        }
        override fun onResolveFailed(info: NsdServiceInfo, errorCode: Int) {
            Log.w("Discovery", "resolve failed: $errorCode")
        }
    }

    val discoveryListener = object : NsdManager.DiscoveryListener {
        override fun onServiceFound(info: NsdServiceInfo) {
            @Suppress("DEPRECATION")
            runCatching { nsd.resolveService(info, resolveListener) }
        }
        override fun onServiceLost(info: NsdServiceInfo) {
            found.remove(info.serviceName); emit()
        }
        override fun onDiscoveryStarted(serviceType: String) {}
        override fun onDiscoveryStopped(serviceType: String) {}
        override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
            Log.w("Discovery", "start failed: $errorCode")
        }
        override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
    }

    runCatching {
        nsd.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, discoveryListener)
    }

    awaitClose {
        runCatching { nsd.stopServiceDiscovery(discoveryListener) }
    }
}
