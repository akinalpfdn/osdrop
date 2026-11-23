package com.akinalpfdn.osdrop

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.akinalpfdn.osdrop.core.NativeCore
import com.akinalpfdn.osdrop.ui.HomeScreen
import com.akinalpfdn.osdrop.ui.OsdropTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val coreVersion = NativeCore.coreVersion()
        setContent {
            OsdropTheme {
                HomeScreen(coreVersion = coreVersion)
            }
        }
    }
}
