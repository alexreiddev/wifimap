package com.wifimap.radar.data

import android.content.Context
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.dataStore by preferencesDataStore(name = "wifimap")

/** Remembers the last ESP32 host and the chosen data source across launches. */
class HostStore(private val context: Context) {

    private val hostKey = stringPreferencesKey("host")
    private val sourceKey = intPreferencesKey("source")   // 0 = ESP32, 1 = phone

    val host: Flow<String> = context.dataStore.data.map { it[hostKey] ?: "wifimap.local" }
    val sourceOrdinal: Flow<Int> = context.dataStore.data.map { it[sourceKey] ?: 0 }

    suspend fun setHost(host: String) =
        context.dataStore.edit { it[hostKey] = host }

    suspend fun setSource(ordinal: Int) =
        context.dataStore.edit { it[sourceKey] = ordinal }
}
