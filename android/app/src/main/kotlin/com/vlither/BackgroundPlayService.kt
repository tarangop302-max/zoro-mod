package com.vlither

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log

/**
 * Background play: keeps the process and CPU awake while a LIVE run
 * continues with the app in the background / screen off. The native side
 * (app/src/main.c bgplay_tick) keeps ticking the game without drawing.
 *
 * Started by GameActivity.onStop only if a run is active, stopped by
 * GameActivity.onStart (back in front) or as soon as native reports the
 * run is over (death, disconnect, lobby) via GameActivity.setRunActive(0).
 */
class BackgroundPlayService : Service() {
    private var wakeLock: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        runCatching {
            val n = notification()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
            } else {
                startForeground(NOTIFICATION_ID, n)
            }
        }.onFailure { Log.w(TAG, "foreground refused", it) }
        if (wakeLock == null) {
            wakeLock = (getSystemService(Context.POWER_SERVICE) as? PowerManager)
                ?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Vlither:background-play")
                ?.apply { setReferenceCounted(false); acquire(MAX_HOLD_MS) }
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        runCatching { wakeLock?.takeIf { it.isHeld }?.release() }
        wakeLock = null
        super.onDestroy()
    }

    private fun notification(): Notification {
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O &&
            manager?.getNotificationChannel(CHANNEL) == null) {
            manager?.createNotificationChannel(
                NotificationChannel(CHANNEL, "Playing in the background",
                    NotificationManager.IMPORTANCE_LOW).apply { setShowBadge(false) })
        }
        val open = PendingIntent.getActivity(
            this, 0,
            Intent(this, GameActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val b = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            Notification.Builder(this, CHANNEL) else @Suppress("DEPRECATION") Notification.Builder(this)
        return b.setSmallIcon(android.R.drawable.presence_online)
            .setContentTitle("Your run is still going")
            .setContentText("Tap to come back.")
            .setOngoing(true)
            .setContentIntent(open)
            .build()
    }

    companion object {
        private const val TAG = "VlitherBgPlay"
        private const val CHANNEL = "vlither_background_play"
        private const val NOTIFICATION_ID = 4202
        /** A wake lock always has an end: 2 hours. */
        private const val MAX_HOLD_MS = 2L * 60 * 60 * 1000

        @JvmStatic
        fun start(context: Context) {
            runCatching {
                val i = Intent(context, BackgroundPlayService::class.java)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) context.startForegroundService(i)
                else context.startService(i)
            }.onFailure { Log.w(TAG, "could not start", it) }
        }

        @JvmStatic
        fun stop(context: Context) {
            runCatching { context.stopService(Intent(context, BackgroundPlayService::class.java)) }
        }
    }
}
