package com.wifimap.radar

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.wifimap.radar.ui.RadarScreen
import com.wifimap.radar.ui.theme.WifimapRadarTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            WifimapRadarTheme {
                RadarScreen()
            }
        }
    }
}
