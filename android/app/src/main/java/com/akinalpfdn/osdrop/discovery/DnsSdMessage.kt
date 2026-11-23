package com.akinalpfdn.osdrop.discovery

import java.io.ByteArrayOutputStream

/** One computer from an mDNS answer: its instance, port, TXT values and IPv4 address. */
data class DnsSdService(
    /** The instance label, the same name NsdManager reports. */
    val instance: String,
    val port: Int,
    val txt: Map<String, ByteArray>,
    val ipv4: String,
)

/** What one mDNS answer says about a service type. */
data class DnsSdAnswer(
    /** Services whose PTR, SRV, TXT and A records all came in this message. */
    val services: List<DnsSdService>,
    /** Instances the sender withdrew (a PTR record with TTL 0). */
    val goodbyes: List<String>,
)

/**
 * The few DNS-SD messages Osdrop's own browsing needs (RFC 6762/6763): a PTR question and the
 * answer a responder sends back. Every read is bounds-checked; the input comes from the network.
 */
object DnsSdMessage {
    private const val HEADER_SIZE = 12
    private const val TYPE_A = 1
    private const val TYPE_PTR = 12
    private const val TYPE_TXT = 16
    private const val TYPE_SRV = 33
    private const val CLASS_IN = 1
    private const val CLASS_MASK = 0x7FFF // the top bit is mDNS's cache-flush flag
    private const val FLAG_RESPONSE = 0x8000
    private const val MAX_NAME_LENGTH = 255
    private const val MAX_POINTER_JUMPS = 16

    /** A PTR question for [serviceType] ("_osplay._tcp") in the local domain. */
    fun query(serviceType: String): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(ByteArray(HEADER_SIZE).also { it[5] = 1 }) // id 0, flags 0, one question
        writeName(out, "$serviceType.local")
        out.writeShort(TYPE_PTR)
        out.writeShort(CLASS_IN)
        return out.toByteArray()
    }

    /** The services of [serviceType] in an mDNS response; null for a question or a malformed message. */
    fun parse(message: ByteArray, length: Int, serviceType: String): DnsSdAnswer? {
        val reader = Reader(message, minOf(length, message.size))
        return runCatching { parse(reader, "$serviceType.local".lowercase()) }.getOrNull()
    }

    private fun parse(reader: Reader, typeName: String): DnsSdAnswer? {
        if (reader.limit < HEADER_SIZE) return null
        reader.offset = 2
        if (reader.u16() and FLAG_RESPONSE == 0) return null
        val questions = reader.u16()
        val records = reader.u16() + reader.u16() + reader.u16()
        repeat(questions) {
            reader.name()
            reader.skip(4)
        }

        val instances = linkedSetOf<String>()
        val goodbyes = mutableListOf<String>()
        val srv = mutableMapOf<String, Pair<Int, String>>()
        val txt = mutableMapOf<String, Map<String, ByteArray>>()
        val ipv4 = mutableMapOf<String, String>()
        repeat(records) {
            val name = reader.name().lowercase()
            val type = reader.u16()
            val recordClass = reader.u16() and CLASS_MASK
            val ttl = reader.u32()
            val dataLength = reader.u16()
            val end = reader.offset + dataLength
            if (end > reader.limit) throw IllegalArgumentException("record past the message")
            if (recordClass == CLASS_IN) {
                when (type) {
                    TYPE_PTR -> if (name == typeName) {
                        val instance = reader.name()
                        if (ttl == 0L) goodbyes += instance else instances += instance
                    }
                    TYPE_SRV -> {
                        reader.skip(4) // priority, weight
                        val port = reader.u16()
                        srv[name] = port to reader.name().lowercase()
                    }
                    TYPE_TXT -> txt[name] = reader.txt(end)
                    TYPE_A -> if (dataLength == 4) ipv4[name] = reader.ipv4()
                }
            }
            reader.offset = end
        }

        val services = instances.mapNotNull { instance ->
            val key = instance.lowercase()
            val (port, target) = srv[key] ?: return@mapNotNull null
            val values = txt[key] ?: return@mapNotNull null
            val address = ipv4[target] ?: return@mapNotNull null
            DnsSdService(label(instance, typeName) ?: return@mapNotNull null, port, values, address)
        }
        return DnsSdAnswer(services, goodbyes.mapNotNull { label(it, typeName) })
    }

    /** "Name._osplay._tcp.local" → "Name"; null for an instance of another type. */
    private fun label(instance: String, typeName: String): String? {
        val suffix = ".$typeName"
        if (!instance.lowercase().endsWith(suffix)) return null
        return instance.dropLast(suffix.length).takeIf { it.isNotEmpty() }
    }

    private fun writeName(out: ByteArrayOutputStream, name: String) {
        for (label in name.split('.')) {
            val bytes = label.encodeToByteArray()
            out.write(bytes.size)
            out.write(bytes)
        }
        out.write(0)
    }

    private fun ByteArrayOutputStream.writeShort(value: Int) {
        write(value ushr 8 and 0xFF)
        write(value and 0xFF)
    }

    private class Reader(val bytes: ByteArray, val limit: Int) {
        var offset = 0

        fun u8(): Int {
            if (offset >= limit) throw IllegalArgumentException("truncated")
            return bytes[offset++].toInt() and 0xFF
        }

        fun u16(): Int = (u8() shl 8) or u8()

        fun u32(): Long = (u16().toLong() shl 16) or u16().toLong()

        fun skip(count: Int) {
            if (offset + count > limit) throw IllegalArgumentException("truncated")
            offset += count
        }

        fun ipv4(): String = List(4) { u8() }.joinToString(".")

        /** "key=value" strings up to [end]; a key without "=" maps to an empty value. */
        fun txt(end: Int): Map<String, ByteArray> {
            val values = mutableMapOf<String, ByteArray>()
            while (offset < end) {
                val size = u8()
                if (offset + size > end) throw IllegalArgumentException("TXT string past the record")
                val entry = bytes.copyOfRange(offset, offset + size)
                offset += size
                val equals = entry.indexOf('='.code.toByte())
                val key = (if (equals < 0) entry else entry.copyOfRange(0, equals)).decodeToString().lowercase()
                if (key.isNotEmpty() && key !in values) {
                    values[key] = if (equals < 0) ByteArray(0) else entry.copyOfRange(equals + 1, entry.size)
                }
            }
            return values
        }

        /** A name at the current offset, following compression pointers (RFC 1035 4.1.4). */
        fun name(): String {
            val labels = mutableListOf<String>()
            var length = 0
            var position = offset
            var resumeAt = -1
            var jumps = 0
            while (true) {
                if (position >= limit) throw IllegalArgumentException("truncated name")
                val size = bytes[position].toInt() and 0xFF
                when {
                    size == 0 -> {
                        position++
                        break
                    }
                    size and 0xC0 == 0xC0 -> {
                        if (position + 1 >= limit) throw IllegalArgumentException("truncated pointer")
                        if (++jumps > MAX_POINTER_JUMPS) throw IllegalArgumentException("pointer loop")
                        if (resumeAt < 0) resumeAt = position + 2
                        position = ((size and 0x3F) shl 8) or (bytes[position + 1].toInt() and 0xFF)
                    }
                    size and 0xC0 != 0 -> throw IllegalArgumentException("unknown label type")
                    else -> {
                        if (position + 1 + size > limit) throw IllegalArgumentException("truncated label")
                        length += size + 1
                        if (length > MAX_NAME_LENGTH) throw IllegalArgumentException("name too long")
                        labels += bytes.decodeToString(position + 1, position + 1 + size)
                        position += 1 + size
                    }
                }
            }
            offset = if (resumeAt >= 0) resumeAt else position
            return labels.joinToString(".")
        }
    }
}
