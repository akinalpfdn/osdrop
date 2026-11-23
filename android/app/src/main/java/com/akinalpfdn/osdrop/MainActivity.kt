package com.akinalpfdn.osdrop

import android.Manifest
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.akinalpfdn.osdrop.app.OsdropApplication
import com.akinalpfdn.osdrop.discovery.DiscoveredDevice
import com.akinalpfdn.osdrop.ui.HomeActions
import com.akinalpfdn.osdrop.ui.HomeScreen
import com.akinalpfdn.osdrop.ui.OsdropTheme

class MainActivity : ComponentActivity() {
    private val notifications = registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        notifications.launch(Manifest.permission.POST_NOTIFICATIONS) // what arrived, and APKs to install
        val app = application as OsdropApplication
        val actions = object : HomeActions {
            override fun pair(device: DiscoveredDevice) = app.phone.pair(device)

            override fun answerPairing(accepted: Boolean) = app.phone.answerPairing(accepted)

            override fun leaveGroup() = app.phone.leaveGroup()
        }
        setContent {
            OsdropTheme {
                val state = app.phone.state.collectAsStateWithLifecycle()
                val incoming = app.inbox.incoming.collectAsStateWithLifecycle()
                HomeScreen(state.value, incoming.value, actions)
            }
        }
    }
}
