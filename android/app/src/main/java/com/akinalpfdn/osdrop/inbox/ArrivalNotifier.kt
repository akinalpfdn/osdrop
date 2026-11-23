package com.akinalpfdn.osdrop.inbox

import android.Manifest
import android.app.DownloadManager
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.akinalpfdn.osdrop.R

/** Tells the user what arrived; a single APK installs from the notification. */
class ArrivalNotifier(private val context: Context) : Arrivals {
    init {
        val channel = NotificationChannel(CHANNEL, context.getString(R.string.channel_arrivals), NotificationManager.IMPORTANCE_DEFAULT)
        context.getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    override fun arrived(from: String, files: List<Published>, failed: Boolean) {
        val allowed = Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU ||
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED
        if (!allowed || (files.isEmpty() && !failed)) return
        val title = if (failed) {
            context.getString(R.string.arrival_failed, from)
        } else {
            context.resources.getQuantityString(R.plurals.arrival_received, files.size, files.size, from)
        }
        val apk = files.singleOrNull()?.takeIf { it.mimeType == APK }
        val open = apk?.let { install(it) } ?: Intent(DownloadManager.ACTION_VIEW_DOWNLOADS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        val notification = NotificationCompat.Builder(context, CHANNEL)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(title)
            .setContentText(files.joinToString(", ") { it.name })
            .setAutoCancel(true)
            .setContentIntent(PendingIntent.getActivity(context, 0, open, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT))
            .apply {
                if (apk != null) {
                    addAction(0, context.getString(R.string.arrival_install), PendingIntent.getActivity(context, 1, install(apk), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT))
                }
            }
            .build()
        @Suppress("MissingPermission") // checked above
        NotificationManagerCompat.from(context).notify(nextId++, notification)
    }

    private fun install(apk: Published): Intent = Intent(Intent.ACTION_VIEW)
        .setDataAndType(apk.uri, APK)
        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)

    private companion object {
        const val CHANNEL = "arrivals"
        const val APK = "application/vnd.android.package-archive"
        var nextId = 1
    }
}
