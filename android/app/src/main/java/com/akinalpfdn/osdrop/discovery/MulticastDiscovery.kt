package com.akinalpfdn.osdrop.discovery

import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import android.util.Log
import com.akinalpfdn.osdrop.core.Advertisement
import com.akinalpfdn.osdrop.core.Mesh
import java.io.IOException
import java.net.DatagramPacket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.MulticastSocket
import java.net.NetworkInterface
import java.net.SocketTimeoutException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.ProducerScope
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Osdrop's own mDNS browsing on the Wi-Fi network, next to [NsdDiscovery]. On a PC's own network
 * (TAB-04, Windows Wi-Fi Direct legacy mode) Android 16's NsdManager logs its queries as sent but
 * none reach the PC, while the same query from an ordinary socket does and the PC answers it.
 */
class MulticastDiscovery(context: Context) : PeerDiscovering {
    private val connectivity = context.getSystemService(ConnectivityManager::class.java)
    private val wifi = context.applicationContext.getSystemService(WifiManager::class.java)

    override fun browse(): Flow<DiscoveryEvent> = callbackFlow {
        val lock = wifi.createMulticastLock(LOCK_TAG).apply { setReferenceCounted(false) }
        var browsing: Job? = null
        var browsed: Pair<Network, String>? = null

        // Called on ConnectivityManager's thread; one Wi-Fi network is browsed at a time.
        fun follow(network: Network?, link: LinkProperties?) = synchronized(lock) {
            val wanted = network?.let { n -> link?.interfaceName?.let { n to it } }
            if (wanted == browsed) return@synchronized
            browsing?.cancel()
            browsed = wanted
            browsing = wanted?.let { (net, name) -> launch(Dispatchers.IO) { browseOn(net, name, lock) } }
        }

        val callback = object : ConnectivityManager.NetworkCallback() {
            private var current: Network? = null

            override fun onLinkPropertiesChanged(network: Network, linkProperties: LinkProperties) {
                current = network
                follow(network, linkProperties)
            }

            override fun onLost(network: Network) {
                if (network == current) {
                    current = null
                    follow(null, null)
                }
            }
        }
        val request = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build()
        connectivity.registerNetworkCallback(request, callback)
        awaitClose {
            connectivity.unregisterNetworkCallback(callback)
            follow(null, null)
        }
    }

    /** Queries on [network] until cancelled; reports computers as they answer and go quiet. */
    private suspend fun ProducerScope<DiscoveryEvent>.browseOn(
        network: Network,
        interfaceName: String,
        lock: WifiManager.MulticastLock,
    ) {
        val seen = mutableMapOf<String, Int>() // label → query round it last answered
        val socket = try {
            open(network, interfaceName)
        } catch (error: IOException) {
            Log.w(TAG, "Own mDNS browsing unavailable on $interfaceName: ${error.message}")
            return
        }
        lock.acquire()
        try {
            val query = DnsSdMessage.query(Mesh.SERVICE_TYPE)
            val group = InetSocketAddress(InetAddress.getByName(GROUP), PORT)
            val buffer = ByteArray(MAX_MESSAGE_SIZE)
            var round = 0
            var nextQueryAt = 0L
            while (isActive) {
                val now = System.currentTimeMillis()
                if (now >= nextQueryAt) {
                    // Computers that missed several rounds have left.
                    seen.filterValues { round - it > ROUNDS_BEFORE_LOST }.keys.forEach {
                        seen.remove(it)
                        trySend(DiscoveryEvent.Lost(it))
                    }
                    round++
                    runCatching { socket.send(DatagramPacket(query, query.size, group)) }
                        .onFailure { Log.w(TAG, "mDNS query failed: ${it.message}") }
                    nextQueryAt = now + QUERY_INTERVALS_MILLIS.getOrElse(round - 1) { QUERY_INTERVALS_MILLIS.last() }
                }
                val packet = DatagramPacket(buffer, buffer.size)
                try {
                    socket.receive(packet)
                } catch (_: SocketTimeoutException) {
                    continue
                }
                val answer = DnsSdMessage.parse(packet.data, packet.length, Mesh.SERVICE_TYPE) ?: continue
                answer.goodbyes.forEach { if (seen.remove(it) != null) trySend(DiscoveryEvent.Lost(it)) }
                for (service in answer.services) {
                    val advertisement = Mesh.parse(service.txt) ?: continue
                    seen[service.instance] = round
                    trySend(DiscoveryEvent.Found(DiscoveredDevice(service.instance, advertisement, service.ipv4, service.port)))
                }
            }
        } finally {
            socket.close()
            lock.release()
            seen.keys.forEach { trySend(DiscoveryEvent.Lost(it)) }
        }
    }

    /** A socket on [network] that sends from and listens on 5353, so answers come back by multicast. */
    private fun open(network: Network, interfaceName: String): MulticastSocket {
        val socket = MulticastSocket(null)
        try {
            socket.reuseAddress = true
            socket.bind(InetSocketAddress(PORT))
            network.bindSocket(socket)
            val networkInterface = NetworkInterface.getByName(interfaceName)
                ?: throw IOException("no interface $interfaceName")
            socket.networkInterface = networkInterface
            socket.joinGroup(InetSocketAddress(InetAddress.getByName(GROUP), PORT), networkInterface)
            socket.timeToLive = MULTICAST_TTL
            socket.soTimeout = RECEIVE_TIMEOUT_MILLIS
            return socket
        } catch (error: IOException) {
            socket.close()
            throw error
        }
    }

    private companion object {
        const val TAG = "Osdrop"
        const val LOCK_TAG = "osplay-discovery"
        const val GROUP = "224.0.0.251"
        const val PORT = 5353
        const val MULTICAST_TTL = 255 // RFC 6762 section 11
        const val MAX_MESSAGE_SIZE = 9000
        const val RECEIVE_TIMEOUT_MILLIS = 500
        /** Quick answers after joining a network, then a slower steady pace. */
        val QUERY_INTERVALS_MILLIS = longArrayOf(1_000, 2_000, 4_000, 10_000)
        /** A computer is gone after missing this many queries in a row (about 30 s at the steady pace). */
        const val ROUNDS_BEFORE_LOST = 3
    }
}
