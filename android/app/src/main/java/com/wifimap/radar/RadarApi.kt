package com.wifimap.radar

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.util.concurrent.TimeUnit

/**
 * Thin HTTP client for the ESP32 radar endpoints:
 *   GET  /api/status
 *   POST /api/recalibrate
 *   POST /api/apscan
 *
 * [host] is the board's IP or hostname (e.g. "192.168.1.42" or "wifimap.local").
 */
class RadarApi {

    private val client = OkHttpClient.Builder()
        .connectTimeout(2, TimeUnit.SECONDS)
        .readTimeout(2, TimeUnit.SECONDS)
        .build()

    private val json = Json { ignoreUnknownKeys = true }

    private fun base(host: String): String {
        val h = host.trim().removeSuffix("/")
        return if (h.startsWith("http://") || h.startsWith("https://")) h else "http://$h"
    }

    suspend fun fetchStatus(host: String): Status = withContext(Dispatchers.IO) {
        val req = Request.Builder().url("${base(host)}/api/status").build()
        client.newCall(req).execute().use { resp ->
            if (!resp.isSuccessful) error("HTTP ${resp.code}")
            val body = resp.body?.string() ?: error("empty body")
            json.decodeFromString(Status.serializer(), body)
        }
    }

    suspend fun recalibrate(host: String) = post(host, "/api/recalibrate")
    suspend fun rescan(host: String) = post(host, "/api/apscan")

    private suspend fun post(host: String, path: String) = withContext(Dispatchers.IO) {
        val req = Request.Builder()
            .url("${base(host)}$path")
            .post(ByteArray(0).toRequestBody())
            .build()
        client.newCall(req).execute().use { /* fire-and-forget */ }
        Unit
    }
}
