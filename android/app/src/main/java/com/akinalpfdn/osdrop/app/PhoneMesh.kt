package com.akinalpfdn.osdrop.app

import android.util.Log
import com.akinalpfdn.osdrop.core.DeviceId
import com.akinalpfdn.osdrop.core.Member
import com.akinalpfdn.osdrop.core.Mesh
import com.akinalpfdn.osdrop.core.MeshListener
import com.akinalpfdn.osdrop.core.NativeCore
import com.akinalpfdn.osdrop.core.Platform
import com.akinalpfdn.osdrop.core.SentReceiving
import com.akinalpfdn.osdrop.core.utf8
import com.akinalpfdn.osdrop.discovery.DiscoveredDevice
import com.akinalpfdn.osdrop.discovery.DiscoveryEvent
import com.akinalpfdn.osdrop.discovery.PeerDiscovering
import com.akinalpfdn.osdrop.outbox.Outbox
import com.akinalpfdn.osdrop.storage.SecureStoring
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** Registers this phone on the network; [NsdAdvertiser][com.akinalpfdn.osdrop.discovery.NsdAdvertiser] or a fake. */
interface Advertising {
    fun start(name: String, port: Int, txt: Map<String, String>)

    fun stop()
}

/** A pairing code to show, and the computer it pairs with. */
data class PairingPrompt(val connection: Long, val code: Int, val name: String)

data class MeshState(
    val running: Boolean = false,
    /** This phone's group, itself included; empty in no group. */
    val members: List<Member> = emptyList(),
    /** Computers on the network that are not in this phone's group. */
    val nearby: List<DiscoveredDevice> = emptyList(),
    val pairing: PairingPrompt? = null,
    /** Core's result code of the last pairing that failed; null when none did. */
    val pairingFailure: Int? = null,
)

/**
 * This phone's member of the group, while Osdrop is open (user decision 2026-11-09, kept
 * 2026-11-23): [start] when the app comes to the front, [stop] when it leaves. Core reports on its
 * own thread; everything that calls back into the mesh runs on [scope].
 */
class PhoneMesh(
    private val store: SecureStoring,
    private val discovery: PeerDiscovering,
    private val advertiser: Advertising,
    private val deviceName: () -> String,
    private val scope: CoroutineScope,
) : MeshListener {
    private val mutableState = MutableStateFlow(MeshState())
    val state: StateFlow<MeshState> = mutableState.asStateFlow()

    /** Gets files sent to this phone and reports on sends from it; set before [start]. */
    var transfers: MeshTransfers? = null

    /** Sends what was shared to Osdrop; set before [start]. */
    var outbox: Outbox? = null

    @Volatile private var mesh: Mesh? = null
    private var browsing: Job? = null
    private val discovered = mutableMapOf<String, DiscoveredDevice>() // label → device

    fun start() {
        if (mesh != null) return
        val started = try {
            val secret = store.secretKey { NativeCore.generateIdentity()?.copyOfRange(0, KEY_BYTES) ?: error("no randomness") }
            Mesh.start(secret, deviceName(), store.groupState(), this)
        } catch (error: Exception) {
            Log.e(TAG, "The mesh did not start", error)
            return
        }
        mesh = started
        transfers?.attach(started)
        outbox?.attach(started)
        mutableState.update { it.copy(running = true) }
        advertise()
        refreshMembers()
        browsing = scope.launch {
            discovery.browse().collect { event -> onDiscovery(event) }
        }
    }

    fun stop() {
        browsing?.cancel()
        browsing = null
        advertiser.stop()
        transfers?.detach()
        outbox?.detach()
        mesh?.close()
        mesh = null
        synchronized(discovered) { discovered.clear() }
        mutableState.update { MeshState() }
    }

    fun pair(device: DiscoveredDevice) {
        val current = mesh ?: return
        scope.launch {
            mutableState.update { it.copy(pairingFailure = null) }
            runCatching { current.pair(device.host, device.port) }
                .onFailure { Log.w(TAG, "Pairing could not start", it) }
        }
    }

    fun answerPairing(accepted: Boolean) {
        val prompt = state.value.pairing ?: return
        mutableState.update { it.copy(pairing = null) }
        val current = mesh ?: return
        scope.launch { runCatching { current.confirmPairing(prompt.connection, accepted) } }
    }

    fun leaveGroup() {
        val current = mesh ?: return
        scope.launch { runCatching { current.leaveGroup() } }
    }

    private fun onDiscovery(event: DiscoveryEvent) {
        val current = mesh ?: return
        when (event) {
            is DiscoveryEvent.Found -> {
                val device = event.device
                if (device.advertisement.platform?.isPhone == true) return // phones do not pair with phones
                synchronized(discovered) { discovered[device.label] = device }
                current.peerDiscovered(device.advertisement.deviceId, device.advertisement.groupId, device.host, device.port)
            }
            is DiscoveryEvent.Lost -> {
                val device = synchronized(discovered) { discovered.remove(event.label) } ?: return
                current.peerLost(device.advertisement.deviceId)
            }
            is DiscoveryEvent.Failed -> Log.w(TAG, "Browsing failed: ${event.code}")
        }
        refreshNearby()
    }

    private fun advertise() {
        val current = mesh ?: return
        val txt = Mesh.advertisement(current.deviceId, deviceName(), current.groupId) ?: return
        advertiser.start(deviceName(), current.listenPort, txt)
    }

    private fun refreshMembers() {
        val current = mesh ?: return
        scope.launch {
            val members = runCatching { current.members() }.getOrDefault(emptyList())
            mutableState.update { it.copy(members = members) }
            refreshNearby()
        }
    }

    private fun refreshNearby() {
        val inGroup = state.value.members.map { it.deviceId }.toSet()
        val devices = synchronized(discovered) { discovered.values.toList() }
        val nearby = devices.filter { it.advertisement.deviceId !in inGroup }.distinctBy { it.advertisement.deviceId }
        mutableState.update { it.copy(nearby = nearby) }
    }

    // --- MeshListener, on core's I/O thread ---

    override fun onGroupChanged(state: ByteArray, groupId: ByteArray?) {
        store.saveGroupState(state)
        scope.launch { advertise() }
        refreshMembers()
    }

    override fun onLeftGroup(reason: Int) = refreshMembers()

    override fun onPairingCode(connection: Long, code: Int, name: ByteArray, platform: Int) {
        mutableState.update { it.copy(pairing = PairingPrompt(connection, code, name.utf8())) }
    }

    override fun onPairingFailed(connection: Long, reason: Int) {
        mutableState.update { current ->
            current.copy(pairing = current.pairing?.takeIf { it.connection != connection }, pairingFailure = reason)
        }
    }

    override fun onPeerConnected(deviceId: ByteArray, name: ByteArray, platform: Int) = refreshMembers()

    override fun onPeerDisconnected(deviceId: ByteArray, reason: Int) = refreshMembers()

    override fun onLog(level: Int, message: ByteArray) {
        Log.println(if (level >= LOG_WARNING) Log.WARN else Log.INFO, TAG, message.utf8())
    }

    override fun onTransferProgress(transfer: Long, bytesDone: Long, bytesTotal: Long) {
        transfers?.onTransferProgress(transfer, bytesDone, bytesTotal)
    }

    override fun onTransferFinished(transfer: Long, result: Int, paths: Array<ByteArray>) {
        transfers?.onTransferFinished(transfer, result, paths.map { it.utf8() })
    }

    override fun onSendArrived(deviceId: ByteArray, sendId: Long, names: Array<ByteArray>, sizes: LongArray, folders: BooleanArray) {
        transfers?.onSendArrived(DeviceId(deviceId), sendId, names.map { it.utf8() }, sizes.toList())
    }

    override fun onSendFinished(deviceId: ByteArray, sendId: Long, outcome: Int) {
        transfers?.onSendFinished(DeviceId(deviceId), sendId, outcome)
        outbox?.onSendFinished(sendId, outcome)
    }

    private companion object {
        const val TAG = "Osdrop"
        const val KEY_BYTES = 32
        const val LOG_WARNING = 3 // OSDROP_LOG_WARNING
    }
}

/** What happens to files sent to this phone, and to sends from it; [Inbox] or a fake. */
interface MeshTransfers {
    fun attach(mesh: SentReceiving)

    fun detach()

    fun onSendArrived(device: DeviceId, sendId: Long, names: List<String>, sizes: List<Long>)

    fun onSendFinished(device: DeviceId, sendId: Long, outcome: Int)

    fun onTransferProgress(transfer: Long, bytesDone: Long, bytesTotal: Long)

    fun onTransferFinished(transfer: Long, result: Int, paths: List<String>)
}
