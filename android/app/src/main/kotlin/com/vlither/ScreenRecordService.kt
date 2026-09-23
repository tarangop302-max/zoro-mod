package com.vlither

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder

/**
 * Exists purely to satisfy the platform requirement that MediaProjection
 * screen recording runs alongside a foreground service (mandatory
 * android:foregroundServiceType="mediaProjection" declaration on Android
 * 14+, expected on 10-13 too). It doesn't do any of the actual recording
 * work itself -- that's all in GameActivity's companion object (the
 * MediaProjection/VirtualDisplay/MediaRecorder live there, tied to the
 * activity). This service is started right before a recording begins and
 * stopped right after it ends; its only job while alive is to keep a
 * visible notification up, which is what makes the foreground service
 * (and therefore the recording) legal in the eyes of the OS.
 */
class ScreenRecordService : Service() {
    companion object {
        private const val CHANNEL_ID = "vlither_screen_record"
        private const val NOTIFICATION_ID = 4201
    }

    override fun onCreate() {
        super.onCreate()
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Screen recording",
                NotificationManager.IMPORTANCE_LOW
            )
            channel.setShowBadge(false)
            manager?.createNotificationChannel(channel)
        }

        val notification: Notification = Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("Vlither is recording your screen")
            .setSmallIcon(android.R.drawable.presence_video_online)
            .setOngoing(true)
            .build()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Nothing dynamic to do here -- onCreate() already did the one
        // thing this service exists for (posting the foreground
        // notification). The actual recording session is started and
        // stopped independently from GameActivity.
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null
}
