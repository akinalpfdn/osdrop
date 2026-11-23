package com.akinalpfdn.osdrop.core

/** Thrown when core refuses a call; [result] is its error code ([CoreResult]). */
class CoreException(val result: Int) : Exception("core error $result")

/**
 * This phone's member of the group: one encrypted session with every member, on core's I/O thread.
 * Every call may come from any thread; [close] ends the sessions and joins the thread.
 */
class Mesh private constructor(private var handle: Long) : AutoCloseable {
    val listenPort: Int get() = NativeCore.meshListenPort(live())

    val deviceId: DeviceId get() = DeviceId(NativeCore.meshDeviceId(live()) ?: throw CoreException(CoreResult.INVALID_STATE))

    /** The group to advertise; null when this phone is in no group. */
    val groupId: DeviceId? get() = NativeCore.meshGroupId(live())?.let(::DeviceId)

    fun peerDiscovered(device: DeviceId, group: DeviceId?, host: String, port: Int) {
        NativeCore.meshPeerDiscovered(live(), device.bytes, group?.bytes, host.utf8Bytes(), port)
    }

    fun peerLost(device: DeviceId) {
        NativeCore.meshPeerLost(live(), device.bytes)
    }

    /** Dials a computer to pair with it; the connection identifies the pairing's callbacks. */
    fun pair(host: String, port: Int): Long {
        val out = LongArray(1)
        check(NativeCore.meshPair(live(), host.utf8Bytes(), port, out))
        return out[0]
    }

    fun confirmPairing(connection: Long, accepted: Boolean) {
        check(NativeCore.meshConfirmPairing(live(), connection, accepted))
    }

    fun members(): List<Member> {
        val fields = NativeCore.meshMembers(live()) ?: return emptyList()
        return fields.toList().chunked(3).map { (id, name, flags) ->
            Member(
                deviceId = DeviceId(id),
                name = name.utf8(),
                platform = Platform.of(flags[0].toInt()),
                isSelf = flags[1].toInt() == 1,
                isConnected = flags[2].toInt() == 1,
                allowsFiles = flags[3].toInt() == 1,
            )
        }
    }

    fun leaveGroup() {
        check(NativeCore.meshLeaveGroup(live()))
    }

    /** Fetches item [index] of a send into [folder]; the transfer's ID. */
    fun receiveSent(device: DeviceId, sendId: Long, index: Int, folder: String): Long {
        val out = LongArray(1)
        check(NativeCore.meshReceiveSent(live(), device.bytes, sendId, index, folder.utf8Bytes(), out))
        return out[0]
    }

    fun cancelTransfer(transfer: Long) {
        NativeCore.meshCancelTransfer(live(), transfer)
    }

    /** Sends files (absolute paths, with their MIME types) to a connected member; the send's ID. */
    fun send(device: DeviceId, files: List<Pair<String, String>>): Long {
        val out = LongArray(1)
        val paths = files.map { it.first.utf8Bytes() }.toTypedArray()
        val types = files.map { it.second.utf8Bytes() }.toTypedArray()
        check(NativeCore.meshSend(live(), device.bytes, paths, types, out))
        return out[0]
    }

    fun answerSend(device: DeviceId, sendId: Long, outcome: SendOutcome) {
        NativeCore.meshAnswerSend(live(), device.bytes, sendId, outcome.code)
    }

    override fun close() {
        val closing = handle
        handle = 0
        if (closing != 0L) NativeCore.meshDestroy(closing)
    }

    private fun live(): Long {
        if (handle == 0L) throw CoreException(CoreResult.INVALID_STATE)
        return handle
    }

    private fun check(result: Int) {
        if (result != CoreResult.OK) throw CoreException(result)
    }

    companion object {
        /** Starts the mesh with this phone's identity and the stored group state (null at first). */
        fun start(secretKey: ByteArray, name: String, groupState: ByteArray?, listener: MeshListener, port: Int = 0): Mesh {
            val out = LongArray(1)
            val result = NativeCore.meshCreate(secretKey, name.utf8Bytes(), groupState, port, listener, out)
            if (result != CoreResult.OK) throw CoreException(result)
            return Mesh(out[0])
        }

        /** The values to put in this phone's TXT record, keyed as core names them. */
        fun advertisement(device: DeviceId, name: String, group: DeviceId?): Map<String, String>? {
            val values = NativeCore.advertisement(device.bytes, name.utf8Bytes(), group?.bytes) ?: return null
            return TXT_KEYS.zip(values.map { it.utf8() }).filter { it.second.isNotEmpty() }.toMap()
        }

        /** A computer's TXT record; null when it is not a valid one. */
        fun parse(record: Map<String, ByteArray>): Advertisement? {
            val values = TXT_KEYS.map { record[it] ?: ByteArray(0) }.toTypedArray()
            val parsed = NativeCore.parseAdvertisement(values)?.map { it.utf8() } ?: return null
            val device = hexBytes(parsed[3]) ?: return null
            return Advertisement(
                protocolVersion = parsed[0].toIntOrNull() ?: return null,
                protocolMinor = parsed[1].toIntOrNull() ?: return null,
                platform = Platform.of(parsed[2].toIntOrNull() ?: 0),
                deviceId = DeviceId(device),
                groupId = parsed[4].takeIf { it.isNotEmpty() }?.let { hexBytes(it) }?.let(::DeviceId),
                name = parsed[5],
            )
        }

        /** OSDROP_SERVICE_TYPE: the Bonjour service every Osdrop advertises. */
        const val SERVICE_TYPE = "_osdrop._tcp"

        /** OSDROP_TXT_KEY_*, in the order the bridge passes the values. */
        val TXT_KEYS = listOf("pv", "pm", "id", "name", "os", "grp")
    }
}
