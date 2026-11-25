package com.akinalpfdn.osdrop.outbox

import android.content.ContentResolver
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Log
import com.akinalpfdn.osdrop.core.DeviceId
import com.akinalpfdn.osdrop.core.Mesh
import com.akinalpfdn.osdrop.core.SendOutcome
import java.io.File
import java.util.UUID
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/** A send from this phone: on its way while [outcome] is null. */
data class OutgoingSend(val id: Long, val to: String, val count: Int, val outcome: SendOutcome? = null) {
    /** Unique in the home screen's list, beside the sends arriving from computers. */
    val listKey: String get() = "out-$id"
}

/**
 * Sends what was shared to Osdrop: core reads files from paths, so each shared item is copied into
 * the cache first, and the copies go once the computer answers.
 */
class Outbox(private val resolver: ContentResolver, private val cacheRoot: File) {
    private val lock = Any()
    private var mesh: Mesh? = null
    private val folders = mutableMapOf<Long, File>()
    private val mutableSends = MutableStateFlow<List<OutgoingSend>>(emptyList())
    val sends: StateFlow<List<OutgoingSend>> = mutableSends.asStateFlow()

    fun attach(mesh: Mesh) {
        synchronized(lock) { this.mesh = mesh }
    }

    fun detach() {
        synchronized(lock) {
            mesh = null
            folders.clear()
        }
        File(cacheRoot, OUTGOING).deleteRecursively()
    }

    /** Copies [uris] and sends them to [device]; false when nothing could be sent. Off the main thread. */
    fun send(device: DeviceId, name: String, uris: List<Uri>): Boolean {
        val current = synchronized(lock) { mesh } ?: return false
        val folder = File(cacheRoot, "$OUTGOING/${UUID.randomUUID()}").apply { mkdirs() }
        val files = uris.mapNotNull { uri -> copy(uri, folder) }
        if (files.isEmpty()) {
            folder.deleteRecursively()
            return false
        }
        val id = runCatching { current.send(device, files) }
            .onFailure { Log.w(TAG, "The files could not be sent", it) }
            .getOrNull()
        if (id == null) {
            folder.deleteRecursively()
            return false
        }
        synchronized(lock) { folders[id] = folder }
        mutableSends.update { it + OutgoingSend(id, name, files.size) }
        return true
    }

    /** From core's I/O thread. */
    fun onSendFinished(sendId: Long, outcome: Int) {
        val folder = synchronized(lock) { folders.remove(sendId) }
        folder?.deleteRecursively()
        mutableSends.update { list -> list.map { if (it.id == sendId) it.copy(outcome = SendOutcome.of(outcome)) else it } }
    }

    /** The shared item as a file in [folder], with its MIME type; null when it cannot be read. */
    private fun copy(uri: Uri, folder: File): Pair<String, String>? {
        val name = displayName(uri)?.replace('/', '_')?.takeIf { it.isNotBlank() } ?: return null
        var target = File(folder, name)
        var number = 2
        while (target.exists()) target = File(folder, "${name.substringBeforeLast('.')} (${number++})${name.substringAfterLast('.', "").let { if (it.isEmpty() || it == name) "" else ".$it" }}")
        val copied = runCatching {
            resolver.openInputStream(uri)?.use { input -> target.outputStream().use { input.copyTo(it) } }
        }.getOrNull()
        if (copied == null) return null
        return target.absolutePath to utiOf(resolver.getType(uri))
    }

    private fun displayName(uri: Uri): String? = runCatching {
        resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0) else null
        }
    }.getOrNull() ?: uri.lastPathSegment

    companion object {
        private const val TAG = "Osdrop"
        private const val OUTGOING = "outgoing"

        /** The protocol names types as Uniform Type Identifiers; core treats unknown ones as data. */
        private val UTIS = mapOf(
            "image/jpeg" to "public.jpeg",
            "image/png" to "public.png",
            "image/heic" to "public.heic",
            "image/heif" to "public.heif",
            "image/gif" to "com.compuserve.gif",
            "image/webp" to "org.webmproject.webp",
            "video/mp4" to "public.mpeg-4",
            "video/quicktime" to "com.apple.quicktime-movie",
            "audio/mpeg" to "public.mp3",
            "application/pdf" to "com.adobe.pdf",
            "application/zip" to "public.zip-archive",
            "application/vnd.android.package-archive" to "com.android.package-archive",
            "text/plain" to "public.plain-text",
        )

        fun utiOf(mimeType: String?): String = UTIS[mimeType?.lowercase()] ?: when {
            mimeType?.startsWith("image/") == true -> "public.image"
            mimeType?.startsWith("video/") == true -> "public.movie"
            mimeType?.startsWith("audio/") == true -> "public.audio"
            else -> "public.data"
        }
    }
}
