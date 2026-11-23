package com.akinalpfdn.osdrop.inbox

import android.util.Log
import com.akinalpfdn.osdrop.app.MeshTransfers
import com.akinalpfdn.osdrop.core.CoreResult
import com.akinalpfdn.osdrop.core.DeviceId
import com.akinalpfdn.osdrop.core.Mesh
import com.akinalpfdn.osdrop.core.SendOutcome
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** Files on their way to this phone, for the screen. */
data class IncomingFiles(val sendId: Long, val from: String, val names: List<String>, val bytesDone: Long, val bytesTotal: Long)

/** What arrived, for a notification: the sender's name and the files as published. */
fun interface Arrivals {
    fun arrived(from: String, files: List<Published>, failed: Boolean)
}

/**
 * Takes every send a member makes to this phone: its items are fetched into the app's cache, then
 * published to Download/Osdrop, and the sender gets the answer. The mesh is never called while
 * [lock] is held: core calls back on its own thread, and a call waits for that thread.
 */
class Inbox(
    private val cacheRoot: File,
    private val publisher: Publishing,
    private val arrivals: Arrivals,
    private val scope: CoroutineScope,
) : MeshTransfers {
    private val lock = Any()
    private var mesh: Mesh? = null
    private val sends = mutableMapOf<Long, Receiving>() // by transfer ID
    private val early = mutableMapOf<Long, Pair<Int, List<String>>>() // finished before they were recorded
    private val mutableIncoming = MutableStateFlow<List<IncomingFiles>>(emptyList())
    val incoming: StateFlow<List<IncomingFiles>> = mutableIncoming.asStateFlow()

    private class Receiving(
        val device: DeviceId,
        val sendId: Long,
        val from: String,
        val names: List<String>,
        val total: Long,
        val folder: File,
    ) {
        val done = mutableMapOf<Long, Long>() // transfer → bytes
        var pending = names.size // items not finished yet, counted from the start
        var failed = false
        var completed = false
        val published = mutableListOf<Published>()
    }

    override fun attach(mesh: Mesh) {
        synchronized(lock) { this.mesh = mesh }
    }

    override fun detach() {
        synchronized(lock) {
            mesh = null
            sends.clear()
            early.clear()
        }
        mutableIncoming.value = emptyList()
        File(cacheRoot, INCOMING).deleteRecursively()
    }

    override fun onSendArrived(device: DeviceId, sendId: Long, names: List<String>, sizes: List<Long>) {
        scope.launch {
            val current = synchronized(lock) { mesh } ?: return@launch
            val from = runCatching { current.members() }.getOrDefault(emptyList()).firstOrNull { it.deviceId == device }?.name.orEmpty()
            val folder = File(cacheRoot, "$INCOMING/$sendId").apply {
                deleteRecursively()
                mkdirs()
            }
            val receiving = Receiving(device, sendId, from, names, sizes.sum(), folder)
            mutableIncoming.update { it + IncomingFiles(sendId, from, names, 0, receiving.total) }
            for (index in names.indices) {
                val transfer = runCatching { current.receiveSent(device, sendId, index, folder.absolutePath) }
                    .onFailure { Log.w(TAG, "An item could not be fetched", it) }
                    .getOrNull()
                if (transfer == null) {
                    val last = synchronized(lock) {
                        receiving.failed = true
                        receiving.pending -= 1
                        receiving.pending == 0
                    }
                    if (last) complete(receiving)
                    continue
                }
                val finishedEarly = synchronized(lock) {
                    sends[transfer] = receiving
                    early.remove(transfer)
                }
                if (finishedEarly != null) finish(transfer, finishedEarly.first, finishedEarly.second)
            }
        }
    }

    override fun onTransferProgress(transfer: Long, bytesDone: Long, bytesTotal: Long) {
        val receiving = synchronized(lock) {
            val found = sends[transfer] ?: return
            found.done[transfer] = bytesDone
            found
        }
        val done = synchronized(lock) { receiving.done.values.sum() }
        mutableIncoming.update { list -> list.map { if (it.sendId == receiving.sendId) it.copy(bytesDone = done) else it } }
    }

    override fun onTransferFinished(transfer: Long, result: Int, paths: List<String>) {
        val known = synchronized(lock) {
            if (transfer in sends) {
                true
            } else {
                early[transfer] = result to paths
                false
            }
        }
        if (known) scope.launch { finish(transfer, result, paths) }
    }

    override fun onSendFinished(device: DeviceId, sendId: Long, outcome: Int) = Unit // sends from this phone: the share screen

    private fun finish(transfer: Long, result: Int, paths: List<String>) {
        val receiving = synchronized(lock) { sends.remove(transfer) } ?: return
        val published = if (result == CoreResult.OK) paths.flatMap { publisher.publish(File(it)) } else emptyList()
        val last = synchronized(lock) {
            receiving.published += published
            if (result != CoreResult.OK) receiving.failed = true
            receiving.pending -= 1
            receiving.pending == 0
        }
        if (last) complete(receiving)
    }

    private fun complete(receiving: Receiving) {
        val current = synchronized(lock) {
            if (receiving.completed) return
            receiving.completed = true
            mesh
        }
        val outcome = if (receiving.failed) SendOutcome.Failed else SendOutcome.Received
        runCatching { current?.answerSend(receiving.device, receiving.sendId, outcome) }
        receiving.folder.deleteRecursively()
        mutableIncoming.update { list -> list.filterNot { it.sendId == receiving.sendId } }
        arrivals.arrived(receiving.from, receiving.published.toList(), receiving.failed)
    }

    private companion object {
        const val TAG = "Osdrop"
        const val INCOMING = "incoming"
    }
}
