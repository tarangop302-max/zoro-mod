package com.vlither

import android.app.Activity
import android.app.DownloadManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

class MainActivity : Activity() {

    companion object {
        private const val TAG = "VlitherMain"

        private const val CURRENT_VERSION = "4.2"

        private const val VERSION_URL =
            "https://raw.githubusercontent.com/Luckyyt623/Vlither_android/main/version.txt"

        private const val DOWNLOAD_URL_FILE =
            "https://raw.githubusercontent.com/Luckyyt623/Vlither_android/main/download_url.txt"

        private const val PREFS_NAME = "vlither_update_prefs"
        private const val PREF_PENDING_DL_ID = "pending_download_id"

        /*
         * Kept for compatibility with any other app code that may reference
         * these methods. The game is now always unlocked.
         */
        fun getUnlockRemainingMs(context: Context): Long {
            return Long.MAX_VALUE
        }

        @JvmStatic
        fun getUnlockRemainingMsStatic(context: Context): Long {
            return Long.MAX_VALUE
        }

        fun saveUnlock(context: Context) {
            // No action needed. The game is permanently unlocked.
        }
    }

    private lateinit var btnPlay: Button
    private lateinit var btnChangelog: Button

    private lateinit var layoutUpdate: LinearLayout
    private lateinit var tvUpdateMsg: TextView
    private lateinit var btnDownload: Button
    private lateinit var btnLater: Button

    private var latestVersion: String? = null
    private var apkDownloadUrl: String? = null

    private var pendingDownloadId: Long = -1L
    private var receiverRegistered = false

    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    /**
     * Opens the Android installer when an update APK finishes downloading.
     */
    private val downloadCompleteReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val id = intent.getLongExtra(
                DownloadManager.EXTRA_DOWNLOAD_ID,
                -1L
            )

            if (id == -1L || id != pendingDownloadId) {
                return
            }

            handleDownloadFinished(id)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        hideSystemUi()

        buildUi()

        registerDownloadReceiver()
        resumePendingDownloadIfAny()

        checkForUpdate()
    }

    override fun onResume() {
        super.onResume()
        hideSystemUi()
    }

    override fun onDestroy() {
        super.onDestroy()

        if (receiverRegistered) {
            try {
                unregisterReceiver(downloadCompleteReceiver)
            } catch (_: Exception) {
                // Receiver may already be unregistered.
            }

            receiverRegistered = false
        }

        executor.shutdown()
    }

    private fun hideSystemUi() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        )
    }

    // ─────────────────────────────────────────────────────────────
    // Update checker
    // ─────────────────────────────────────────────────────────────

    private fun registerDownloadReceiver() {
        if (receiverRegistered) {
            return
        }

        val filter = IntentFilter(
            DownloadManager.ACTION_DOWNLOAD_COMPLETE
        )

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(
                downloadCompleteReceiver,
                filter,
                Context.RECEIVER_NOT_EXPORTED
            )
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(
                downloadCompleteReceiver,
                filter
            )
        }

        receiverRegistered = true
    }

    /**
     * Handles an update download that was still active when the app
     * was previously closed.
     */
    private fun resumePendingDownloadIfAny() {
        val prefs = getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )

        val savedId = prefs.getLong(
            PREF_PENDING_DL_ID,
            -1L
        )

        if (savedId == -1L) {
            return
        }

        pendingDownloadId = savedId

        executor.execute {
            handleDownloadFinished(savedId)
        }
    }

    private fun checkForUpdate() {
        executor.execute {
            try {
                val latest = fetchText(VERSION_URL).trim()

                if (latest.isEmpty()) {
                    return@execute
                }

                val downloadUrl =
                    fetchText(DOWNLOAD_URL_FILE).trim()

                Log.d(
                    TAG,
                    "Latest: $latest  Current: $CURRENT_VERSION"
                )

                if (isNewerVersion(latest, CURRENT_VERSION)) {
                    latestVersion = latest
                    apkDownloadUrl = downloadUrl

                    mainHandler.post {
                        showUpdatePanel(latest)
                    }
                }
            } catch (e: Exception) {
                Log.w(
                    TAG,
                    "Update check failed: ${e.message}"
                )
            }
        }
    }

    private fun fetchText(urlString: String): String {
        val connection =
            URL(urlString).openConnection() as HttpURLConnection

        connection.connectTimeout = 5000
        connection.readTimeout = 5000

        return try {
            connection.inputStream
                .bufferedReader()
                .readText()
        } finally {
            connection.disconnect()
        }
    }

    private fun isNewerVersion(
        latest: String,
        current: String
    ): Boolean {
        return try {
            val latestParts =
                latest.split(".").map { it.toInt() }

            val currentParts =
                current.split(".").map { it.toInt() }

            val count = maxOf(
                latestParts.size,
                currentParts.size
            )

            for (i in 0 until count) {
                val latestValue =
                    latestParts.getOrElse(i) { 0 }

                val currentValue =
                    currentParts.getOrElse(i) { 0 }

                if (latestValue > currentValue) {
                    return true
                }

                if (latestValue < currentValue) {
                    return false
                }
            }

            false
        } catch (_: Exception) {
            false
        }
    }

    private fun showUpdatePanel(version: String) {
        tvUpdateMsg.text =
            "New update available: v$version"

        layoutUpdate.visibility = View.VISIBLE

        btnChangelog.visibility = View.VISIBLE

        btnPlay.alpha = 0.6f
    }

    // ─────────────────────────────────────────────────────────────
    // Changelog
    // ─────────────────────────────────────────────────────────────

    private fun showChangelog() {
        val message = """
Update 4.2

  • Added separate Transparent Skin opacity and Center Line options.
  • Added responsive UI scaling for different phone resolutions.
  • Added a scrollable Custom Skin panel.
  • Added Android clipboard paste support for nickname, IP, NTL and chat fields.
  • Added draggable and resizable minimap controls.
  • Added custom minimap marker shapes, sizes and colours.
  • Added teammate positions and names on the minimap.
  • Added saved NTL team profiles for Team ID and Auth Key.
  • Added minimizable in-game NTL chat and a separate player list.
  • Added 60/90/120/144 FPS caps and Performance Mode.
  • Improved mobile multitouch so UI buttons work while using the Arrow.
  • Fixed boost so it activates only from the Boost button.
  • Now NTL Chat also available enter your NTL keys to join.
  • Replaced the on-screen Boost button with the new custom image button.
  • Various stability and interface fixes.

Changes made by Lucky
        """.trimIndent()

        android.app.AlertDialog.Builder(this)
            .setTitle("What's New in v4.2")
            .setMessage(message)
            .setPositiveButton("Got it") { dialog, _ ->
                dialog.dismiss()
            }
            .show()
    }

    // ─────────────────────────────────────────────────────────────
    // Update download
    // ─────────────────────────────────────────────────────────────

    private fun onDownloadClicked() {
        val url = apkDownloadUrl

        if (url.isNullOrEmpty()) {
            android.widget.Toast.makeText(
                this,
                "Download link not ready yet.",
                android.widget.Toast.LENGTH_SHORT
            ).show()

            return
        }

        try {
            val request =
                DownloadManager.Request(Uri.parse(url)).apply {

                    setTitle(
                        "Vlither v$latestVersion"
                    )

                    setDescription(
                        "Downloading update..."
                    )

                    setMimeType(
                        "application/vnd.android.package-archive"
                    )

                    setNotificationVisibility(
                        DownloadManager.Request
                            .VISIBILITY_VISIBLE_NOTIFY_COMPLETED
                    )

                    setDestinationInExternalPublicDir(
                        Environment.DIRECTORY_DOWNLOADS,
                        "Vlither-v$latestVersion.apk"
                    )

                    setAllowedOverMetered(true)
                    setAllowedOverRoaming(true)
                }

            val downloadManager =
                getSystemService(
                    Context.DOWNLOAD_SERVICE
                ) as DownloadManager

            val id =
                downloadManager.enqueue(request)

            pendingDownloadId = id

            getSharedPreferences(
                PREFS_NAME,
                Context.MODE_PRIVATE
            )
                .edit()
                .putLong(
                    PREF_PENDING_DL_ID,
                    id
                )
                .apply()

            btnDownload.text =
                "Downloading update..."

            btnDownload.isEnabled = false

            android.widget.Toast.makeText(
                this,
                "Downloading — the install prompt will open automatically when it's done",
                android.widget.Toast.LENGTH_LONG
            ).show()

        } catch (e: Exception) {
            Log.e(
                TAG,
                "Download failed: ${e.message}"
            )

            try {
                startActivity(
                    Intent(
                        Intent.ACTION_VIEW,
                        Uri.parse(url)
                    )
                )
            } catch (fallbackError: Exception) {
                Log.e(
                    TAG,
                    "Browser fallback failed: ${fallbackError.message}"
                )
            }
        }
    }

    private fun handleDownloadFinished(id: Long) {
        val downloadManager =
            getSystemService(
                Context.DOWNLOAD_SERVICE
            ) as DownloadManager

        val query =
            DownloadManager.Query()
                .setFilterById(id)

        val cursor =
            downloadManager.query(query)

        var status = -1

        cursor.use {
            if (it.moveToFirst()) {
                val index =
                    it.getColumnIndex(
                        DownloadManager.COLUMN_STATUS
                    )

                if (index >= 0) {
                    status = it.getInt(index)
                }
            }
        }

        if (
            status == DownloadManager.STATUS_RUNNING ||
            status == DownloadManager.STATUS_PENDING ||
            status == DownloadManager.STATUS_PAUSED
        ) {
            return
        }

        getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )
            .edit()
            .remove(PREF_PENDING_DL_ID)
            .apply()

        pendingDownloadId = -1L

        if (
            status ==
            DownloadManager.STATUS_SUCCESSFUL
        ) {
            promptInstall(
                downloadManager,
                id
            )
        } else {
            mainHandler.post {
                btnDownload.text =
                    "⬇  Download Update"

                btnDownload.isEnabled = true

                android.widget.Toast.makeText(
                    this,
                    "Update download failed. Please try again.",
                    android.widget.Toast.LENGTH_LONG
                ).show()
            }
        }
    }

    private fun promptInstall(
        downloadManager: DownloadManager,
        id: Long
    ) {
        mainHandler.post {
            btnDownload.text =
                "⬇  Download Update"

            btnDownload.isEnabled = true

            try {
                val apkUri =
                    downloadManager
                        .getUriForDownloadedFile(id)

                val installIntent =
                    Intent(
                        Intent.ACTION_VIEW
                    ).apply {

                        setDataAndType(
                            apkUri,
                            "application/vnd.android.package-archive"
                        )

                        addFlags(
                            Intent.FLAG_ACTIVITY_NEW_TASK
                        )

                        addFlags(
                            Intent.FLAG_GRANT_READ_URI_PERMISSION
                        )
                    }

                startActivity(
                    installIntent
                )

            } catch (e: Exception) {
                Log.e(
                    TAG,
                    "Install prompt failed: ${e.message}"
                )

                android.widget.Toast.makeText(
                    this,
                    "Downloaded, but couldn't open the installer automatically. Check your Downloads folder.",
                    android.widget.Toast.LENGTH_LONG
                ).show()
            }
        }
    }

    // ─────────────────────────────────────────────────────────────
    // User interface
    // ─────────────────────────────────────────────────────────────

    private fun buildUi() {
        val root = FrameLayout(this)

        root.setBackgroundColor(
            0xFF0D0E14.toInt()
        )

        val column = LinearLayout(this)

        column.orientation =
            LinearLayout.VERTICAL

        column.setPadding(
            64,
            0,
            64,
            0
        )

        val columnParams =
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            ).also {
                it.gravity =
                    android.view.Gravity.CENTER
            }

        // Title

        val title = TextView(this)

        title.text = "VLITHER"

        title.textSize = 40f

        title.setTextColor(
            0xFF2BAA60.toInt()
        )

        title.typeface =
            android.graphics.Typeface.DEFAULT_BOLD

        title.gravity =
            android.view.Gravity.CENTER

        title.setPadding(
            0,
            0,
            0,
            48
        )

        val buttonParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).also {
                it.setMargins(
                    0,
                    8,
                    0,
                    8
                )
            }

        /*
         * Direct Play button.
         *
         * No ad check.
         * No browser.
         * No unlock timer.
         */
        btnPlay = Button(this)

        btnPlay.text = "▶  PLAY"

        btnPlay.textSize = 18f

        btnPlay.setPadding(
            0,
            28,
            0,
            28
        )

        btnPlay.layoutParams =
            buttonParams

        btnPlay.isEnabled = true

        btnPlay.alpha = 1.0f

        btnPlay.setOnClickListener {
            launchGame()
        }

        // Changelog

        btnChangelog = Button(this)

        btnChangelog.text =
            "📋  What's New"

        btnChangelog.textSize = 13f

        btnChangelog.setPadding(
            0,
            16,
            0,
            16
        )

        btnChangelog.layoutParams =
            buttonParams

        btnChangelog.setTextColor(
            0xFF5DADE2.toInt()
        )

        btnChangelog.alpha = 0.85f

        btnChangelog.visibility =
            View.VISIBLE

        btnChangelog.setOnClickListener {
            showChangelog()
        }

        // Update panel

        layoutUpdate =
            LinearLayout(this)

        layoutUpdate.orientation =
            LinearLayout.VERTICAL

        layoutUpdate.visibility =
            View.GONE

        layoutUpdate.setPadding(
            0,
            16,
            0,
            0
        )

        val divider = View(this)

        divider.setBackgroundColor(
            0xFF2BAA60.toInt()
        )

        val dividerParams =
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                2
            )

        dividerParams.setMargins(
            0,
            8,
            0,
            16
        )

        divider.layoutParams =
            dividerParams

        tvUpdateMsg = TextView(this)

        tvUpdateMsg.textSize = 14f

        tvUpdateMsg.setTextColor(
            0xFF5DADE2.toInt()
        )

        tvUpdateMsg.gravity =
            android.view.Gravity.CENTER

        tvUpdateMsg.setPadding(
            0,
            0,
            0,
            12
        )

        btnDownload = Button(this)

        btnDownload.text =
            "⬇  Download Update"

        btnDownload.textSize = 14f

        btnDownload.setPadding(
            0,
            20,
            0,
            20
        )

        btnDownload.layoutParams =
            buttonParams

        btnDownload.setBackgroundColor(
            0xFF1A5276.toInt()
        )

        btnDownload.setTextColor(
            0xFFADD8E6.toInt()
        )

        btnDownload.setOnClickListener {
            onDownloadClicked()
        }

        btnLater = Button(this)

        btnLater.text =
            "Later — Play Current Version"

        btnLater.textSize = 13f

        btnLater.setPadding(
            0,
            16,
            0,
            16
        )

        btnLater.layoutParams =
            buttonParams

        btnLater.alpha = 0.6f

        btnLater.setOnClickListener {
            layoutUpdate.visibility =
                View.GONE

            btnPlay.alpha = 1.0f
        }

        layoutUpdate.addView(
            divider
        )

        layoutUpdate.addView(
            tvUpdateMsg
        )

        layoutUpdate.addView(
            btnDownload
        )

        layoutUpdate.addView(
            btnLater
        )

        // Add everything to the menu

        column.addView(
            title
        )

        column.addView(
            btnPlay
        )

        column.addView(
            btnChangelog
        )

        column.addView(
            layoutUpdate
        )

        root.addView(
            column,
            columnParams
        )

        setContentView(
            root
        )
    }

    // ─────────────────────────────────────────────────────────────
    // Start game directly
    // ─────────────────────────────────────────────────────────────

    private fun launchGame() {
        try {
            val intent =
                Intent(
                    this,
                    GameActivity::class.java
                )

            intent.addFlags(
                Intent.FLAG_ACTIVITY_SINGLE_TOP
            )

            startActivity(
                intent
            )

        } catch (e: Exception) {
            Log.e(
                TAG,
                "Failed to launch GameActivity: ${e.message}"
            )

            android.widget.Toast.makeText(
                this,
                "Failed to start game: ${e.message}",
                android.widget.Toast.LENGTH_LONG
            ).show()
        }
    }
}