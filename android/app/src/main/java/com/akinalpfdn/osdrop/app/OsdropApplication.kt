package com.akinalpfdn.osdrop.app

import android.app.Application
import android.os.Build
import android.provider.Settings
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import com.akinalpfdn.osdrop.discovery.CombinedDiscovery
import com.akinalpfdn.osdrop.discovery.MulticastDiscovery
import com.akinalpfdn.osdrop.discovery.NsdAdvertiser
import com.akinalpfdn.osdrop.discovery.NsdDiscovery
import com.akinalpfdn.osdrop.inbox.ArrivalNotifier
import com.akinalpfdn.osdrop.inbox.DownloadsPublisher
import com.akinalpfdn.osdrop.inbox.Inbox
import com.akinalpfdn.osdrop.outbox.Outbox
import com.akinalpfdn.osdrop.storage.AndroidSecureStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

/**
 * The composition root. The phone is a member of the group only while Osdrop is in front (user
 * decision 2026-11-09, kept 2026-11-23): the mesh starts when the app comes to the front and stops
 * when it leaves.
 */
class OsdropApplication : Application() {
    lateinit var phone: PhoneMesh
        private set
    lateinit var inbox: Inbox
        private set
    lateinit var outbox: Outbox
        private set

    override fun onCreate() {
        super.onCreate()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        inbox = Inbox(cacheDir, DownloadsPublisher(this), ArrivalNotifier(this), scope)
        outbox = Outbox(contentResolver, cacheDir)
        phone = PhoneMesh(
            store = AndroidSecureStore(this),
            discovery = CombinedDiscovery(listOf(NsdDiscovery(this), MulticastDiscovery(this))),
            advertiser = NsdAdvertiser(this),
            deviceName = ::deviceName,
            scope = scope,
        ).also {
            it.transfers = inbox
            it.outbox = outbox
        }
        ProcessLifecycleOwner.get().lifecycle.addObserver(object : DefaultLifecycleObserver {
            override fun onStart(owner: LifecycleOwner) = phone.start()

            override fun onStop(owner: LifecycleOwner) = phone.stop()
        })
    }

    /** The phone's name as the user set it, cut to the 64 bytes the protocol allows. */
    private fun deviceName(): String {
        val name = Settings.Global.getString(contentResolver, Settings.Global.DEVICE_NAME)?.takeIf { it.isNotBlank() } ?: Build.MODEL
        var cut = name.trim()
        while (cut.toByteArray(Charsets.UTF_8).size > MAX_NAME_BYTES) cut = cut.dropLast(1)
        return cut
    }

    private companion object {
        const val MAX_NAME_BYTES = 64
    }
}
