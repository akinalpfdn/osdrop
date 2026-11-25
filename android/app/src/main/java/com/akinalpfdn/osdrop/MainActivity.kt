package com.akinalpfdn.osdrop

import android.Manifest
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import com.akinalpfdn.osdrop.app.OsdropApplication
import com.akinalpfdn.osdrop.core.DeviceId
import com.akinalpfdn.osdrop.core.Member
import com.akinalpfdn.osdrop.discovery.DiscoveredDevice
import com.akinalpfdn.osdrop.inbox.IncomingFiles
import com.akinalpfdn.osdrop.ui.HomeActions
import com.akinalpfdn.osdrop.ui.HomeScreen
import com.akinalpfdn.osdrop.ui.OsdropTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {
    private val notifications = registerForActivityResult(ActivityResultContracts.RequestPermission()) { }
    private val picker = registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments(), ::picked)

    /** The computer the picker chooses files for; kept while the picker covers this activity. */
    private var pickingFor: Pair<DeviceId, String>? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            notifications.launch(Manifest.permission.POST_NOTIFICATIONS) // what arrived, and APKs to install
        }
        savedInstanceState?.getByteArray(PICKING_DEVICE)?.let { device ->
            pickingFor = DeviceId(device) to savedInstanceState.getString(PICKING_NAME).orEmpty()
        }
        val app = application as OsdropApplication
        val actions = object : HomeActions {
            override fun pair(device: DiscoveredDevice) = app.phone.pair(device)

            override fun answerPairing(accepted: Boolean) = app.phone.answerPairing(accepted)

            override fun leaveGroup() = app.phone.leaveGroup()

            override fun sendFiles(member: Member) {
                pickingFor = member.deviceId to member.name
                app.hold() // the picker is another app: stay connected to the computer meanwhile
                picker.launch(arrayOf("*/*"))
            }

            override fun cancelIncoming(files: IncomingFiles) = app.inbox.cancel(files.device, files.sendId)
        }
        setContent {
            OsdropTheme {
                val state = app.phone.state.collectAsStateWithLifecycle()
                val incoming = app.inbox.incoming.collectAsStateWithLifecycle()
                val sends = app.outbox.sends.collectAsStateWithLifecycle()
                HomeScreen(state.value, incoming.value, sends.value, actions)
            }
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        pickingFor?.let { (device, name) ->
            outState.putByteArray(PICKING_DEVICE, device.bytes)
            outState.putString(PICKING_NAME, name)
        }
    }

    private fun picked(uris: List<Uri>) {
        val app = application as OsdropApplication
        app.release()
        val (device, name) = pickingFor ?: return
        pickingFor = null
        if (uris.isEmpty()) return
        lifecycleScope.launch {
            withContext(Dispatchers.IO) { app.outbox.send(device, name, uris) }
        }
    }

    private companion object {
        const val PICKING_DEVICE = "pickingDevice"
        const val PICKING_NAME = "pickingName"
    }
}
