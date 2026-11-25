package com.akinalpfdn.osdrop

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.core.content.IntentCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import com.akinalpfdn.osdrop.app.OsdropApplication
import com.akinalpfdn.osdrop.core.Member
import com.akinalpfdn.osdrop.ui.OsdropTheme
import com.akinalpfdn.osdrop.ui.ShareScreen
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Osdrop in the share sheet: what was shared goes to the computer the user picks. */
class ShareActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val app = application as OsdropApplication
        val uris = sharedUris(intent)
        setContent {
            OsdropTheme {
                val state = app.phone.state.collectAsStateWithLifecycle()
                val sends = app.outbox.sends.collectAsStateWithLifecycle()
                ShareScreen(
                    count = uris.size,
                    state = state.value,
                    sends = sends.value,
                    onPick = { member -> send(app, member, uris) },
                    onDone = ::finish,
                )
            }
        }
    }

    private fun send(app: OsdropApplication, member: Member, uris: List<Uri>) {
        lifecycleScope.launch {
            withContext(Dispatchers.IO) { app.outbox.send(member.deviceId, member.name, uris) }
        }
    }

    private fun sharedUris(intent: Intent): List<Uri> = when (intent.action) {
        Intent.ACTION_SEND -> listOfNotNull(IntentCompat.getParcelableExtra(intent, Intent.EXTRA_STREAM, Uri::class.java))
        Intent.ACTION_SEND_MULTIPLE -> IntentCompat.getParcelableArrayListExtra(intent, Intent.EXTRA_STREAM, Uri::class.java).orEmpty()
        else -> emptyList()
    }
}
