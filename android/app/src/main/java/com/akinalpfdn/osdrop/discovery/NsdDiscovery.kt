package com.akinalpfdn.osdrop.discovery

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import com.akinalpfdn.osdrop.core.Advertisement
import com.akinalpfdn.osdrop.core.Mesh
import java.util.ArrayDeque
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow

/** An Osdrop computer on the network. */
data class DiscoveredDevice(
    /** The service instance name; identifies the device in [DiscoveryEvent.Lost]. */
    val label: String,
    val advertisement: Advertisement,
    /** An address literal the core connects to. */
    val host: String,
    val port: Int,
)

sealed interface DiscoveryEvent {
    data class Found(val device: DiscoveredDevice) : DiscoveryEvent

    data class Lost(val label: String) : DiscoveryEvent

    /** Browsing stopped (NsdManager error code); the flow ends after this. */
    data class Failed(val code: Int) : DiscoveryEvent
}

/** Bonjour browsing (DEVPLAN: `PeerDiscovering`); a fake in tests. */
interface PeerDiscovering {
    fun browse(): Flow<DiscoveryEvent>
}

/**
 * Browses with Android's NsdManager. Before Android 14 it resolves one service at a time, so
 * resolves wait in a queue.
 */
class NsdDiscovery(context: Context) : PeerDiscovering {
    private val manager = context.getSystemService(NsdManager::class.java)

    override fun browse(): Flow<DiscoveryEvent> = callbackFlow {
        val pending = ArrayDeque<NsdServiceInfo>()
        var resolving = false

        // Called with `pending` locked.
        fun resolveNext() {
            if (resolving) return
            val next = pending.pollFirst() ?: return
            resolving = true
            @Suppress("DEPRECATION") // resolveService: one call per service works on every supported version
            manager.resolveService(
                next,
                object : NsdManager.ResolveListener {
                    override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                        synchronized(pending) {
                            resolving = false
                            resolveNext()
                        }
                    }

                    override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                        device(serviceInfo)?.let { trySend(DiscoveryEvent.Found(it)) }
                        synchronized(pending) {
                            resolving = false
                            resolveNext()
                        }
                    }
                },
            )
        }

        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) = Unit

            override fun onDiscoveryStopped(serviceType: String) = Unit

            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                trySend(DiscoveryEvent.Failed(errorCode))
                close()
            }

            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) = Unit

            override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                synchronized(pending) {
                    pending.addLast(serviceInfo)
                    resolveNext()
                }
            }

            override fun onServiceLost(serviceInfo: NsdServiceInfo) {
                trySend(DiscoveryEvent.Lost(serviceInfo.serviceName))
            }
        }
        manager.discoverServices(Mesh.SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener)
        awaitClose { runCatching { manager.stopServiceDiscovery(listener) } }
    }

    private fun device(info: NsdServiceInfo): DiscoveredDevice? {
        val advertisement = Mesh.parse(info.attributes.mapValues { it.value ?: ByteArray(0) }) ?: return null
        @Suppress("DEPRECATION") // host: the resolved address, available on every supported version
        val address = info.host?.hostAddress ?: return null
        return DiscoveredDevice(info.serviceName, advertisement, address.substringBefore('%'), info.port)
    }
}
