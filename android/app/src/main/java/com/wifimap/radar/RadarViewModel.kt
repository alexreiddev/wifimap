package com.wifimap.radar

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.wifimap.radar.data.HostStore
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

enum class Source { ESP32, PHONE }
enum class Conn { Idle, Connecting, Connected, Error }

data class UiState(
    val host: String = "wifimap.local",
    val source: Source = Source.ESP32,
    val conn: Conn = Conn.Idle,
    val error: String? = null,
    val status: Status = Status(),
    val discovered: List<DiscoveredBoard> = emptyList(),
    val needsPermissions: Boolean = false,
)

class RadarViewModel(app: Application) : AndroidViewModel(app) {

    private val api = RadarApi()
    private val store = HostStore(app)
    private val scanner = LocalScanner(app)

    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()

    private var pollJob: Job? = null
    private var scanCollectJob: Job? = null

    init {
        viewModelScope.launch {
            val host = store.host.first()
            val src = if (store.sourceOrdinal.first() == 1) Source.PHONE else Source.ESP32
            _state.update { it.copy(host = host, source = src) }
            startDiscovery()
            start()
        }
    }

    private fun startDiscovery() = viewModelScope.launch {
        discoverBoards(getApplication()).collect { boards ->
            _state.update { it.copy(discovered = boards) }
        }
    }

    fun setHost(host: String) {
        _state.update { it.copy(host = host) }
        viewModelScope.launch { store.setHost(host) }
        if (_state.value.source == Source.ESP32) restart()
    }

    fun setSource(source: Source) {
        if (source == _state.value.source) return
        _state.update { it.copy(source = source, status = Status(), error = null) }
        viewModelScope.launch { store.setSource(source.ordinal) }
        restart()
    }

    fun onPermissionsGranted() {
        _state.update { it.copy(needsPermissions = false) }
        if (_state.value.source == Source.PHONE) restart()
    }

    fun requestPermissionsNeeded() = _state.update { it.copy(needsPermissions = true) }

    private fun restart() { stop(); start() }

    fun start() {
        when (_state.value.source) {
            Source.ESP32 -> startEsp32()
            Source.PHONE -> startPhone()
        }
    }

    private fun startEsp32() {
        if (pollJob?.isActive == true) return
        _state.update { it.copy(conn = Conn.Connecting) }
        pollJob = viewModelScope.launch {
            while (isActive) {
                val host = _state.value.host
                try {
                    val s = api.fetchStatus(host)
                    _state.update { it.copy(conn = Conn.Connected, error = null, status = s) }
                } catch (e: Exception) {
                    _state.update { it.copy(conn = Conn.Error, error = e.message) }
                }
                delay(1000)
            }
        }
    }

    private fun startPhone() {
        scanner.start(viewModelScope)
        _state.update { it.copy(conn = Conn.Connected) }
        scanCollectJob = viewModelScope.launch {
            scanner.status.collect { s -> _state.update { it.copy(status = s) } }
        }
    }

    fun stop() {
        pollJob?.cancel(); pollJob = null
        scanCollectJob?.cancel(); scanCollectJob = null
        scanner.stop()
    }

    fun recalibrate() = viewModelScope.launch {
        if (_state.value.source == Source.ESP32) runCatching { api.recalibrate(_state.value.host) }
    }

    fun rescan() = viewModelScope.launch {
        if (_state.value.source == Source.ESP32) runCatching { api.rescan(_state.value.host) }
    }

    override fun onCleared() { stop(); super.onCleared() }
}
