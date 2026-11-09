package com.vlither

import android.app.Activity
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.text.Spannable
import android.text.SpannableString
import android.text.style.ForegroundColorSpan
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.FileProvider
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

class MainActivity : Activity() {

    companion object {
        private const val TAG = "VlitherMain"

        /**
         * GitHub repo whose Releases hold your APKs ("owner/repo"). Must be
         * PUBLIC. Upload a new release with a tag like v1.3 and the APK
         * attached, and every player sees NEW UPDATE on this screen.
         */
        private const val UPDATE_REPO = "tarangop302-max/jsr-clan-web"

        /** Comes from `appVersion` in android/app/build.gradle. */
        private const val CURRENT_VERSION = BuildConfig.VERSION_NAME

        private const val COLOR_GREEN = 0xFF3DDC84.toInt()
        private const val COLOR_BLUE = 0xFF5DADE2.toInt()
        private const val COLOR_GREY = 0xFF8A8F9C.toInt()
        private const val COLOR_RED = 0xFFFF6B6B.toInt()
        private const val COLOR_ORANGE = 0xFFFFA726.toInt()
        private const val COLOR_PURPLE = 0xFFB388FF.toInt()

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

    private class UpdateInfo(
        val version: String,
        val notes: String,
        val apkUrl: String,
        val sizeBytes: Long
    )

    private lateinit var tvStatus: TextView
    private var checkFailed = false
    private lateinit var btnPlay: Button
    private lateinit var btnChangelog: TextView

    private lateinit var layoutUpdate: LinearLayout
    private lateinit var tvUpdateMsg: TextView
    private lateinit var tvNotes: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var tvProgress: TextView
    private lateinit var btnDownload: Button
    private lateinit var btnLater: TextView

    private var update: UpdateInfo? = null
    private var downloadedApk: File? = null
    private var awaitingInstallPermission = false

    @Volatile private var downloading = false
    @Volatile private var destroyed = false

    private val executor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        hideSystemUi()
        buildUi()
        checkForUpdate()
    }

    override fun onResume() {
        super.onResume()
        hideSystemUi()

        // Back from the "Install unknown apps" settings page.
        val apk = downloadedApk
        if (awaitingInstallPermission && apk != null && canInstallPackages()) {
            awaitingInstallPermission = false
            installApk(apk)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        destroyed = true
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
    // Update check (GitHub Releases)
    // ─────────────────────────────────────────────────────────────

    private fun checkForUpdate() {
        checkFailed = false
        setStatus("Checking for updates…", COLOR_GREY)

        executor.execute {
            try {
                val json = JSONObject(
                    fetchText("https://api.github.com/repos/$UPDATE_REPO/releases/latest")
                )

                val version = json.optString("tag_name")
                    .trim()
                    .removePrefix("v")
                    .removePrefix("V")
                val notes = json.optString("body").trim()

                var apkUrl = ""
                var size = 0L
                val assets = json.optJSONArray("assets")
                if (assets != null) {
                    for (i in 0 until assets.length()) {
                        val a = assets.getJSONObject(i)
                        if (a.optString("name").endsWith(".apk", ignoreCase = true)) {
                            apkUrl = a.optString("browser_download_url")
                            size = a.optLong("size")
                            break
                        }
                    }
                }

                mainHandler.post {
                    if (!destroyed) onCheckResult(version, notes, apkUrl, size)
                }
            } catch (e: Exception) {
                Log.w(TAG, "Update check failed: ${e.message}")
                mainHandler.post {
                    if (!destroyed) onCheckFailed()
                }
            }
        }
    }

    private fun fetchText(urlString: String): String {
        val connection = URL(urlString).openConnection() as HttpURLConnection
        connection.connectTimeout = 8000
        connection.readTimeout = 8000
        connection.setRequestProperty("Accept", "application/vnd.github+json")
        connection.setRequestProperty("User-Agent", "Vlither-Updater")

        return try {
            connection.inputStream.bufferedReader().readText()
        } finally {
            connection.disconnect()
        }
    }

    private fun onCheckResult(version: String, notes: String, apkUrl: String, size: Long) {
        if (version.isEmpty() || apkUrl.isEmpty() || !isNewerVersion(version, CURRENT_VERSION)) {
            // Nothing newer (or the release has no APK attached).
            cleanupUpdates()
            update = null
            layoutUpdate.visibility = View.GONE
            setStatus("✔  UP TO DATE", COLOR_GREEN)
            return
        }

        val info = UpdateInfo(version, notes, apkUrl, size)
        update = info

        setStatus("●  NEW UPDATE  •  v$version", COLOR_ORANGE)
        tvUpdateMsg.text = "Version v$version is ready to install\nYou have v$CURRENT_VERSION"

        if (notes.isNotEmpty()) {
            tvNotes.text = notes
            tvNotes.visibility = View.VISIBLE
        } else {
            tvNotes.visibility = View.GONE
        }

        // Already downloaded on a previous launch? Offer install right away.
        val existing = apkFileFor(info)
        if (existing.exists() && (info.sizeBytes <= 0 || existing.length() == info.sizeBytes)) {
            downloadedApk = existing
            btnDownload.text = "⬇  Install v$version"
        } else {
            downloadedApk = null
            btnDownload.text = "⬇  Download v$version" + sizeSuffix(info.sizeBytes)
        }

        btnDownload.isEnabled = true
        progressBar.visibility = View.GONE
        tvProgress.visibility = View.GONE
        layoutUpdate.visibility = View.VISIBLE
    }

    private fun onCheckFailed() {
        checkFailed = true
        setStatus("⚠  CAN'T CHECK  •  TAP TO RETRY", COLOR_RED)
    }

    private fun isNewerVersion(latest: String, current: String): Boolean {
        fun parts(v: String): List<Int> =
            v.split(".").map { p -> p.takeWhile { it.isDigit() }.toIntOrNull() ?: 0 }

        val l = parts(latest)
        val c = parts(current)
        for (i in 0 until maxOf(l.size, c.size)) {
            val lv = l.getOrElse(i) { 0 }
            val cv = c.getOrElse(i) { 0 }
            if (lv > cv) return true
            if (lv < cv) return false
        }
        return false
    }

    /** Status pill under the title: text + tinted rounded background. */
    private fun setStatus(text: String, color: Int) {
        tvStatus.text = text
        tvStatus.setTextColor(color)
        tvStatus.background = GradientDrawable().apply {
            cornerRadius = dp(20).toFloat()
            setColor((color and 0x00FFFFFF) or 0x26000000)
            setStroke(dp(1), (color and 0x00FFFFFF) or 0x66000000)
        }
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private fun sizeSuffix(bytes: Long): String =
        if (bytes > 0) "  (%.1f MB)".format(bytes / 1048576.0) else ""

    // ─────────────────────────────────────────────────────────────
    // Changelog
    // ─────────────────────────────────────────────────────────────

    /** One block in the What's New window: a coloured heading + bullet lines. */
    private class ChangeSection(val icon: String, val title: String, val color: Int, val items: List<String>)

    private fun showChangelog() {
        // A newer release is available -> show its GitHub release notes.
        val info = update
        if (info != null && info.notes.isNotEmpty()) {
            showChangelogDialog(
                "v${info.version}",
                "Latest release",
                listOf(ChangeSection("✦", "Release notes", COLOR_ORANGE,
                    info.notes.lines().map { it.trim().trimStart('-', '*', '•', ' ') }.filter { it.isNotEmpty() }))
            )
            return
        }

        val sections = listOf(
            ChangeSection("🎨", "Backgrounds", COLOR_PURPLE, listOf(
                "New Backgrounds gallery: pick the game floor from 47 backgrounds, including the classic Slither ones, with live previews.",
                "Settings > General > Background lets you switch any time."
            )),
            ChangeSection("🌌", "Lobby", COLOR_BLUE, listOf(
                "New animated lobby themes: Stars, Waves, Slash and more.",
                "30 colour palettes to mix with every theme.",
                "Animations can be frozen to save battery."
            )),
            ChangeSection("🏆", "Leaderboard", COLOR_ORANGE, listOf(
                "Text effects: Rainbow, moving Rainbow, Wave and Pulse, with a second gradient colour, speed and direction.",
                "Outline and drop shadow so names stay readable on any background.",
                "Highlight bar behind your own line.",
                "Green / red rank arrows when a player moves up or down.",
                "Smooth slide animation when ranks change."
            )),
            ChangeSection("✨", "HUD themes", COLOR_GREEN, listOf(
                "One-tap HUD presets (Forest, Royal, Rainbow and more) that style the leaderboard, stats, Teammates list, centre ring and turn path.",
                "Every option can still be tweaked afterwards."
            )),
            ChangeSection("🐍", "Skins & NTL tags", COLOR_PURPLE, listOf(
                "Import and Export your saved skins as a .ntlmod file.",
                "NTL tag size slider and a Small NTL tags option.",
                "Free NTL tags are now loaded and shown."
            )),
            ChangeSection("🎮", "Controls & gameplay", COLOR_BLUE, listOf(
                "Team chat can be opened and closed with a hotkey (C by default).",
                "Zoom slider: swap sides and set the zoom speed.",
                "Improved steering model for smoother turns at high ping.",
                "NTL-style bot mode (grow, to-circle, coil).",
                "New Heart pattern for Snakey Rain."
            )),
            ChangeSection("📡", "Connection", COLOR_RED, listOf(
                "Clear messages when the server times out, uses a different protocol, refuses the connection or rejects your spawn."
            ))
        )

        showChangelogDialog("v$CURRENT_VERSION", "Changes made by zoro", sections)
    }

    private fun showChangelogDialog(version: String, subtitle: String, sections: List<ChangeSection>) {
        val dialog = android.app.Dialog(this)
        dialog.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE)

        val dm = resources.displayMetrics
        val panelW = minOf((dm.widthPixels * 0.78f).toInt(), dp(560))
        val bodyMaxH = (dm.heightPixels * 0.55f).toInt()

        val panel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(18), dp(20), dp(14))
            background = GradientDrawable().apply {
                cornerRadius = dp(22).toFloat()
                setColor(0xFF141620.toInt())
                setStroke(dp(1), 0xFF2A2D40.toInt())
            }
        }

        // Header: title + version pill, subtitle underneath.
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        header.addView(TextView(this).apply {
            text = "What's New"
            textSize = 22f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(Color.WHITE)
        })
        header.addView(TextView(this).apply {
            text = version
            textSize = 12f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(COLOR_PURPLE)
            setPadding(dp(10), dp(3), dp(10), dp(3))
            background = GradientDrawable().apply {
                cornerRadius = dp(20).toFloat()
                setColor(0x26B388FF)
                setStroke(dp(1), 0x66B388FF)
            }
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).also { it.leftMargin = dp(10) }
        })
        panel.addView(header)
        panel.addView(TextView(this).apply {
            text = subtitle
            textSize = 12f
            setTextColor(COLOR_GREY)
            setPadding(0, dp(2), 0, dp(10))
        })

        // Scrollable body: one card per section.
        val body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        for (sec in sections) {
            val card = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(14), dp(10), dp(14), dp(10))
                background = GradientDrawable().apply {
                    cornerRadius = dp(14).toFloat()
                    setColor(0xFF1B1E2C.toInt())
                    setStroke(dp(1), (sec.color and 0x00FFFFFF) or 0x40000000)
                }
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).also { it.bottomMargin = dp(8) }
            }
            card.addView(TextView(this).apply {
                text = "${sec.icon}  ${sec.title}"
                textSize = 14f
                typeface = Typeface.DEFAULT_BOLD
                setTextColor(sec.color)
                setPadding(0, 0, 0, dp(4))
            })
            for (line in sec.items) {
                card.addView(TextView(this).apply {
                    text = "•  $line"
                    textSize = 12.5f
                    setTextColor(0xFFD5D8E4.toInt())
                    setLineSpacing(0f, 1.15f)
                    setPadding(0, dp(2), 0, dp(2))
                })
            }
            body.addView(card)
        }
        val scroll = ScrollView(this).apply {
            isVerticalScrollBarEnabled = false
            addView(body)
        }
        panel.addView(
            scroll,
            LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            )
        )
        // Cap the body height so the button always stays on screen.
        scroll.viewTreeObserver.addOnGlobalLayoutListener {
            if (scroll.layoutParams.height != bodyMaxH && body.height > bodyMaxH) {
                scroll.layoutParams.height = bodyMaxH
                scroll.requestLayout()
            }
        }

        panel.addView(
            styledButton(
                "Got it", 15f,
                0xFF7C4DFF.toInt(), 0xFF5B2DDB.toInt(), Color.WHITE, 14
            ).apply { setOnClickListener { dialog.dismiss() } }
        )

        dialog.setContentView(panel, ViewGroup.LayoutParams(panelW, ViewGroup.LayoutParams.WRAP_CONTENT))
        dialog.window?.apply {
            setBackgroundDrawable(android.graphics.drawable.ColorDrawable(Color.TRANSPARENT))
            setLayout(panelW, ViewGroup.LayoutParams.WRAP_CONTENT)
            @Suppress("DEPRECATION")
            decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            )
        }
        dialog.show()
    }

    // ─────────────────────────────────────────────────────────────
    // Download + install
    // ─────────────────────────────────────────────────────────────

    private fun updatesDir(): File = File(getExternalFilesDir(null) ?: cacheDir, "updates")

    private fun apkFileFor(info: UpdateInfo): File =
        File(updatesDir(), "Vlither-v${info.version}.apk")

    private fun cleanupUpdates() {
        try {
            updatesDir().listFiles()?.forEach { it.delete() }
        } catch (_: Exception) {
        }
    }

    private fun onDownloadClicked() {
        val info = update ?: return

        // Already downloaded -> just (re)open the installer.
        val ready = downloadedApk
        if (ready != null && ready.exists()) {
            installApk(ready)
            return
        }

        if (downloading) return
        downloading = true

        btnDownload.isEnabled = false
        btnDownload.text = "Downloading…"
        btnLater.isEnabled = false
        progressBar.progress = 0
        progressBar.isIndeterminate = info.sizeBytes <= 0
        progressBar.visibility = View.VISIBLE
        tvProgress.text = "Starting…"
        tvProgress.visibility = View.VISIBLE

        Thread {
            val dir = updatesDir()
            dir.mkdirs()
            val target = apkFileFor(info)
            val part = File(dir, target.name + ".part")
            var conn: HttpURLConnection? = null

            try {
                // Remove older APKs / leftovers.
                dir.listFiles()?.forEach { if (it != target) it.delete() }

                conn = URL(info.apkUrl).openConnection() as HttpURLConnection
                conn.connectTimeout = 15000
                conn.readTimeout = 30000
                conn.setRequestProperty("User-Agent", "Vlither-Updater")

                val total = if (conn.contentLengthLong > 0) conn.contentLengthLong else info.sizeBytes
                var done = 0L
                var lastUi = 0L

                conn.inputStream.use { input ->
                    part.outputStream().use { out ->
                        val buf = ByteArray(32 * 1024)
                        while (true) {
                            if (destroyed) throw IOException("Cancelled")
                            val n = input.read(buf)
                            if (n < 0) break
                            out.write(buf, 0, n)
                            done += n

                            val now = System.currentTimeMillis()
                            if (now - lastUi > 100) {
                                lastUi = now
                                val d = done
                                mainHandler.post { showProgress(d, total) }
                            }
                        }
                    }
                }

                if (total > 0 && part.length() != total) {
                    throw IOException("Incomplete download")
                }

                // Sanity check: it must be an APK for THIS app.
                val archive = packageManager.getPackageArchiveInfo(part.absolutePath, 0)
                if (archive == null || archive.packageName != packageName) {
                    throw IOException("Downloaded file is not a valid update")
                }

                if (target.exists()) target.delete()
                if (!part.renameTo(target)) throw IOException("Couldn't save the update")

                mainHandler.post { if (!destroyed) onDownloaded(target) }
            } catch (e: Exception) {
                Log.e(TAG, "Download failed: ${e.message}")
                part.delete()
                mainHandler.post { if (!destroyed) onDownloadFailed(e.message) }
            } finally {
                conn?.disconnect()
                downloading = false
            }
        }.start()
    }

    private fun showProgress(done: Long, total: Long) {
        if (total > 0) {
            val pct = (done * 100 / total).toInt().coerceIn(0, 100)
            progressBar.isIndeterminate = false
            progressBar.progress = pct
            tvProgress.text = "%d%%   (%.1f / %.1f MB)".format(
                pct, done / 1048576.0, total / 1048576.0
            )
        } else {
            tvProgress.text = "%.1f MB".format(done / 1048576.0)
        }
    }

    private fun onDownloaded(file: File) {
        downloadedApk = file
        progressBar.visibility = View.GONE
        tvProgress.text = "Download complete"
        btnDownload.isEnabled = true
        btnDownload.text = "⬇  Install v${update?.version ?: ""}"
        btnLater.isEnabled = true
        installApk(file)
    }

    private fun onDownloadFailed(reason: String?) {
        progressBar.visibility = View.GONE
        tvProgress.visibility = View.GONE
        btnDownload.isEnabled = true
        btnDownload.text = "⬇  Retry download"
        btnLater.isEnabled = true
        Toast.makeText(
            this,
            "Update download failed" + (if (reason.isNullOrEmpty()) "" else ": $reason"),
            Toast.LENGTH_LONG
        ).show()
    }

    private fun canInstallPackages(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.O || packageManager.canRequestPackageInstalls()

    private fun installApk(file: File) {
        if (!canInstallPackages()) {
            // Android needs a one-time "allow installs from this app" switch.
            awaitingInstallPermission = true
            Toast.makeText(
                this,
                "Allow installs from this app, then come back to finish updating",
                Toast.LENGTH_LONG
            ).show()
            try {
                startActivity(
                    Intent(
                        Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:$packageName")
                    )
                )
            } catch (e: Exception) {
                Log.e(TAG, "Couldn't open install-permission settings: ${e.message}")
            }
            return
        }

        try {
            val uri = FileProvider.getUriForFile(this, "$packageName.fileprovider", file)
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, "application/vnd.android.package-archive")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            startActivity(intent)
        } catch (e: Exception) {
            Log.e(TAG, "Install prompt failed: ${e.message}")
            Toast.makeText(
                this,
                "Couldn't open the installer: ${e.message}",
                Toast.LENGTH_LONG
            ).show()
        }
    }

    // ─────────────────────────────────────────────────────────────
    // User interface
    // ─────────────────────────────────────────────────────────────

    /** Rounded, gradient, ripple-on-press button (replaces the stock grey one). */
    private fun styledButton(
        label: String,
        sizeSp: Float,
        colorStart: Int,
        colorEnd: Int,
        textColor: Int,
        radiusDp: Int = 14
    ): Button = Button(this).apply {
        text = label
        textSize = sizeSp
        setTextColor(textColor)
        typeface = Typeface.DEFAULT_BOLD
        isAllCaps = false
        stateListAnimator = null
        elevation = 0f
        setPadding(dp(20), dp(12), dp(20), dp(12))
        val fill = GradientDrawable(
            GradientDrawable.Orientation.LEFT_RIGHT,
            intArrayOf(colorStart, colorEnd)
        ).apply { cornerRadius = dp(radiusDp).toFloat() }
        val mask = GradientDrawable().apply {
            cornerRadius = dp(radiusDp).toFloat()
            setColor(Color.WHITE)
        }
        background = RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), fill, mask)
        layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        ).also { it.setMargins(0, dp(6), 0, dp(6)) }
    }

    private fun buildUi() {
        // Scrolls only if the update panel makes the card taller than the screen.
        val scroll = ScrollView(this).apply {
            setBackgroundColor(0xFF0B0C12.toInt())
            isFillViewport = true
            isVerticalScrollBarEnabled = false
        }
        val frame = FrameLayout(this)
        scroll.addView(
            frame,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )

        // The card
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(26), dp(22), dp(26), dp(18))
            background = GradientDrawable().apply {
                cornerRadius = dp(22).toFloat()
                setColor(0xFF141620.toInt())
                setStroke(dp(1), 0xFF2A2D40.toInt())
            }
        }
        val cardWidth = minOf(
            (resources.displayMetrics.widthPixels * 0.62f).toInt(),
            dp(400)
        )
        frame.addView(
            card,
            FrameLayout.LayoutParams(
                cardWidth,
                FrameLayout.LayoutParams.WRAP_CONTENT
            ).also {
                it.gravity = Gravity.CENTER
                it.setMargins(0, dp(12), 0, dp(12))
            }
        )

        // Title:  JSR (purple)  VLITHER (white)
        val title = TextView(this).apply {
            val t = SpannableString("JSR VLITHER")
            t.setSpan(
                ForegroundColorSpan(COLOR_PURPLE), 0, 3,
                Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            t.setSpan(
                ForegroundColorSpan(Color.WHITE), 4, t.length,
                Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            text = t
            textSize = 26f
            typeface = Typeface.DEFAULT_BOLD
            letterSpacing = 0.12f
            gravity = Gravity.CENTER
        }

        val tvVersion = TextView(this).apply {
            text = "v$CURRENT_VERSION"
            textSize = 13f
            setTextColor(COLOR_GREY)
            gravity = Gravity.CENTER
            setPadding(0, dp(2), 0, dp(12))
        }

        // Update status pill (tap to retry when the check failed)
        tvStatus = TextView(this).apply {
            textSize = 12f
            typeface = Typeface.DEFAULT_BOLD
            letterSpacing = 0.05f
            gravity = Gravity.CENTER
            setPadding(dp(16), dp(7), dp(16), dp(7))
            setOnClickListener { if (checkFailed) checkForUpdate() }
        }
        setStatus("Checking for updates…", COLOR_GREY)

        // Update panel (hidden until a newer version is found)
        layoutUpdate = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
            setPadding(0, dp(14), 0, 0)
        }

        tvUpdateMsg = TextView(this).apply {
            textSize = 13f
            setTextColor(0xFFE6E8EF.toInt())
            gravity = Gravity.CENTER
            setPadding(0, 0, 0, dp(6))
        }

        tvNotes = TextView(this).apply {
            textSize = 12f
            setTextColor(COLOR_GREY)
            maxLines = 4
            ellipsize = android.text.TextUtils.TruncateAt.END
            setPadding(0, 0, 0, dp(8))
            visibility = View.GONE
        }

        btnDownload = styledButton(
            "⬇  Download Update", 14f,
            0xFFFF9800.toInt(), 0xFFF57C00.toInt(), Color.WHITE
        ).apply { setOnClickListener { onDownloadClicked() } }

        progressBar = ProgressBar(
            this, null, android.R.attr.progressBarStyleHorizontal
        ).apply {
            max = 100
            visibility = View.GONE
            progressTintList = ColorStateList.valueOf(COLOR_ORANGE)
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).also { it.setMargins(0, dp(4), 0, dp(2)) }
        }

        tvProgress = TextView(this).apply {
            textSize = 12f
            setTextColor(COLOR_GREY)
            gravity = Gravity.CENTER
            visibility = View.GONE
        }

        btnLater = TextView(this).apply {
            text = "Later"
            textSize = 12f
            setTextColor(COLOR_GREY)
            gravity = Gravity.CENTER
            setPadding(0, dp(8), 0, dp(4))
            setOnClickListener {
                layoutUpdate.visibility = View.GONE
            }
        }

        layoutUpdate.addView(tvUpdateMsg)
        layoutUpdate.addView(tvNotes)
        layoutUpdate.addView(btnDownload)
        layoutUpdate.addView(progressBar)
        layoutUpdate.addView(tvProgress)
        layoutUpdate.addView(btnLater)

        btnPlay = styledButton(
            "▶  PLAY", 18f,
            0xFF7C4DFF.toInt(), 0xFF5B2DDB.toInt(), Color.WHITE, 16
        ).apply {
            setPadding(dp(20), dp(14), dp(20), dp(14))
            (layoutParams as LinearLayout.LayoutParams).topMargin = dp(16)
            setOnClickListener { launchGame() }
        }

        btnChangelog = TextView(this).apply {
            text = "What's New"
            textSize = 12f
            setTextColor(COLOR_BLUE)
            gravity = Gravity.CENTER
            setPadding(0, dp(10), 0, dp(2))
            setOnClickListener { showChangelog() }
        }

        card.addView(title)
        card.addView(tvVersion)
        card.addView(tvStatus)
        card.addView(layoutUpdate)
        card.addView(btnPlay)
        card.addView(btnChangelog)

        setContentView(scroll)
    }

    // ─────────────────────────────────────────────────────────────
    // Start game directly
    // ─────────────────────────────────────────────────────────────

    private fun launchGame() {
        try {
            val intent = Intent(this, GameActivity::class.java)
            intent.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
            startActivity(intent)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to launch GameActivity: ${e.message}")
            Toast.makeText(
                this,
                "Failed to start game: ${e.message}",
                Toast.LENGTH_LONG
            ).show()
        }
    }
}
