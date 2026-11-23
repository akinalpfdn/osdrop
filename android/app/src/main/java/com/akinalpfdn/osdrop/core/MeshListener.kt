package com.akinalpfdn.osdrop.core

/**
 * What core reports, on its I/O thread: implementations return quickly and never call the mesh
 * from here (post to another thread instead). Text arrives as UTF-8 bytes; the JNI bridge looks
 * the methods up by these exact signatures.
 */
interface MeshListener {
    /** Store [state] in the secure store; [groupId] is the group to advertise, null for none. */
    fun onGroupChanged(state: ByteArray, groupId: ByteArray?)

    fun onLeftGroup(reason: Int)

    /** A first pairing: show [code]; answer with [Mesh.confirmPairing]. */
    fun onPairingCode(connection: Long, code: Int, name: ByteArray, platform: Int)

    fun onPairingFailed(connection: Long, reason: Int)

    fun onPeerConnected(deviceId: ByteArray, name: ByteArray, platform: Int)

    fun onPeerDisconnected(deviceId: ByteArray, reason: Int)

    fun onLog(level: Int, message: ByteArray)

    fun onTransferProgress(transfer: Long, bytesDone: Long, bytesTotal: Long)

    /** A transfer into a folder ended; [paths] are the top-level items it created. */
    fun onTransferFinished(transfer: Long, result: Int, paths: Array<ByteArray>)

    /** A member sent this phone files: fetch each, then answer. */
    fun onSendArrived(deviceId: ByteArray, sendId: Long, names: Array<ByteArray>, sizes: LongArray, folders: BooleanArray)

    fun onSendFinished(deviceId: ByteArray, sendId: Long, outcome: Int)
}
