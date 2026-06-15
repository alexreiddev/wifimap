package com.wifimap.radar

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

data class UiState(
    val host: String = "wifimap.local",
    val connected: Boolean = false,
    val error: String? = null,
    val status: Status = Status(),
)

/**
 * Holds the target host and polls /api/status once per second while running.
 */
class RadarViewModel : ViewModel() {

    private val api = RadarApi()
    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()

    private var pollJob: Job? = null

    fun setHost(host: String) = _state.update { it.copy(host = host) }

    fun start() {
        if (pollJob?.isActive == true) return
        pollJob = viewModelScope.launch {
            while (isActive) {
                val host = _state.value.host
                try {
                    val s = api.fetchStatus(host)
                    _state.update { it.copy(connected = true, error = null, status = s) }
                } catch (e: Exception) {
                    _state.update { it.copy(connected = false, error = e.message) }
                }
                delay(1000)
            }
        }
    }

    fun stop() {
        pollJob?.cancel()
        pollJob = null
    }

    fun recalibrate() = viewModelScope.launch {
        runCatching { api.recalibrate(_state.value.host) }
    }

    fun rescan() = viewModelScope.launch {
        runCatching { api.rescan(_state.value.host) }
    }

    override fun onCleared() {
        stop()
        super.onCleared()
    }
}
