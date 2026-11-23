package com.akinalpfdn.osdrop.inbox

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.provider.MediaStore
import android.webkit.MimeTypeMap
import java.io.File

/** A received file as it now lives in shared storage. */
data class Published(val uri: Uri, val name: String, val mimeType: String)

/** Moves received files where the user finds them; [DownloadsPublisher] or a fake. */
fun interface Publishing {
    /** Copies [root] (a file, or a folder with everything in it) and returns what was created. */
    fun publish(root: File): List<Published>
}

/**
 * Copies received files into Download/Osdrop through MediaStore, keeping folders as subfolders.
 * MediaStore files images and videos by their type, so they also show up in the gallery.
 */
class DownloadsPublisher(context: Context) : Publishing {
    private val resolver = context.contentResolver

    override fun publish(root: File): List<Published> {
        val base = root.parentFile ?: return emptyList()
        return root.walkTopDown().filter { it.isFile }.mapNotNull { file ->
            val relative = file.parentFile?.relativeTo(base)?.path.orEmpty()
            copy(file, listOf(FOLDER, relative).filter { it.isNotEmpty() }.joinToString("/"))
        }.toList()
    }

    private fun copy(file: File, folder: String): Published? {
        val mimeType = mimeTypeOf(file.name)
        val values = ContentValues().apply {
            put(MediaStore.Downloads.DISPLAY_NAME, file.name)
            put(MediaStore.Downloads.MIME_TYPE, mimeType)
            put(MediaStore.Downloads.RELATIVE_PATH, folder)
            put(MediaStore.Downloads.IS_PENDING, 1)
        }
        val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values) ?: return null
        val copied = runCatching {
            resolver.openOutputStream(uri)?.use { output -> file.inputStream().use { it.copyTo(output) } }
        }.isSuccess
        if (!copied) {
            resolver.delete(uri, null, null)
            return null
        }
        resolver.update(uri, ContentValues().apply { put(MediaStore.Downloads.IS_PENDING, 0) }, null, null)
        return Published(uri, file.name, mimeType)
    }

    private companion object {
        const val FOLDER = "Download/Osdrop"

        fun mimeTypeOf(name: String): String =
            MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substringAfterLast('.', "").lowercase())
                ?: "application/octet-stream"
    }
}
