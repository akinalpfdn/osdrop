package com.akinalpfdn.osdrop.core

/**
 * The shared core, through the JNI bridge in `src/main/cpp`. Text crosses as UTF-8 byte arrays;
 * results are core's error codes ([CoreResult]). Use [Mesh] rather than the mesh calls here.
 */
object NativeCore {
    init {
        System.loadLibrary("osdrop_jni")
    }

    /** The linked core's version, "major.minor.patch". */
    @JvmStatic external fun coreVersion(): String

    /** A new identity: 32 bytes of secret key, then 32 of public key; null without randomness. */
    @JvmStatic external fun generateIdentity(): ByteArray?

    /** The TXT values to advertise (pv, pm, id, name, os, grp); null for a bad argument. */
    @JvmStatic external fun advertisement(deviceId: ByteArray, name: ByteArray, groupId: ByteArray?): Array<ByteArray>?

    /** A peer's TXT values as [version, minor, platform, device ID, group ID, name]; null if invalid. */
    @JvmStatic external fun parseAdvertisement(values: Array<ByteArray>): Array<ByteArray>?

    @JvmStatic external fun meshCreate(
        secretKey: ByteArray,
        name: ByteArray,
        groupState: ByteArray?,
        port: Int,
        listener: MeshListener,
        outHandle: LongArray,
    ): Int

    @JvmStatic external fun meshDestroy(handle: Long)

    @JvmStatic external fun meshListenPort(handle: Long): Int

    @JvmStatic external fun meshDeviceId(handle: Long): ByteArray?

    @JvmStatic external fun meshGroupId(handle: Long): ByteArray?

    @JvmStatic external fun meshPeerDiscovered(handle: Long, deviceId: ByteArray, groupId: ByteArray?, host: ByteArray, port: Int): Int

    @JvmStatic external fun meshPeerLost(handle: Long, deviceId: ByteArray): Int

    @JvmStatic external fun meshPair(handle: Long, host: ByteArray, port: Int, outConnection: LongArray): Int

    @JvmStatic external fun meshConfirmPairing(handle: Long, connection: Long, accepted: Boolean): Int

    /** Three byte arrays per member: device ID, name, and [platform, self, connected, files]. */
    @JvmStatic external fun meshMembers(handle: Long): Array<ByteArray>?

    @JvmStatic external fun meshLeaveGroup(handle: Long): Int

    @JvmStatic external fun meshReceiveSent(
        handle: Long,
        deviceId: ByteArray,
        sendId: Long,
        index: Int,
        destination: ByteArray,
        outTransfer: LongArray,
    ): Int

    @JvmStatic external fun meshCancelTransfer(handle: Long, transfer: Long): Int

    @JvmStatic external fun meshSend(
        handle: Long,
        deviceId: ByteArray,
        paths: Array<ByteArray>,
        types: Array<ByteArray>,
        outSend: LongArray,
    ): Int

    @JvmStatic external fun meshAnswerSend(handle: Long, deviceId: ByteArray, sendId: Long, outcome: Int): Int

    @JvmStatic external fun meshCloseAll(handle: Long): Int
}
