package com.akinalpfdn.osdrop.core

/** Core's result codes (docs/PROTOCOL.md "Error codes") that the app tells apart. */
object CoreResult {
    const val OK = 0
    const val INVALID_ARGUMENT = 1
    const val UNSUPPORTED_VERSION = 5
    const val NOT_TRUSTED = 21
    const val PAIRING_REJECTED = 22
    const val INVALID_STATE = 24
    const val NOT_PERMITTED = 28
    const val GROUP_FULL = 29
    const val IN_ANOTHER_GROUP = 30
    const val CANCELLED = 32
    const val NO_SPACE = 34
    const val NOT_FOUND = 36
}

/** osdrop_platform. */
enum class Platform(val code: Int) {
    MacOS(1),
    Windows(2),
    Android(3),
    ;

    companion object {
        fun of(code: Int): Platform? = entries.firstOrNull { it.code == code }
    }
}

/** How files sent to one member ended (osdrop_send_outcome). */
enum class SendOutcome(val code: Int) {
    Received(1),
    Declined(2),
    Failed(3),
    ;

    companion object {
        fun of(code: Int): SendOutcome = entries.firstOrNull { it.code == code } ?: Failed
    }
}

/** A device ID or group ID, compared by value. */
class DeviceId(bytes: ByteArray) {
    private val value = bytes.copyOf()

    val bytes: ByteArray get() = value.copyOf()

    override fun equals(other: Any?): Boolean = other is DeviceId && other.value.contentEquals(value)

    override fun hashCode(): Int = value.contentHashCode()

    override fun toString(): String = value.joinToString("") { "%02x".format(it) }
}

data class Member(
    val deviceId: DeviceId,
    val name: String,
    val platform: Platform?,
    val isSelf: Boolean,
    val isConnected: Boolean,
    val allowsFiles: Boolean,
)

/** A computer found on the network. */
data class Advertisement(
    val protocolVersion: Int,
    val protocolMinor: Int,
    val platform: Platform?,
    val deviceId: DeviceId,
    val groupId: DeviceId?,
    val name: String,
)

internal fun ByteArray.utf8(): String = toString(Charsets.UTF_8)

internal fun String.utf8Bytes(): ByteArray = toByteArray(Charsets.UTF_8)

internal fun hexBytes(text: String): ByteArray? {
    if (text.length % 2 != 0) return null
    return ByteArray(text.length / 2) { index ->
        text.substring(index * 2, index * 2 + 2).toIntOrNull(16)?.toByte() ?: return null
    }
}
