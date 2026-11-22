package com.vlither

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.app.Activity
import android.app.Dialog
import android.app.NativeActivity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.graphics.Color
import android.graphics.Typeface
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.MediaRecorder
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.MediaStore
import android.util.Log
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import android.text.Editable
import android.text.InputFilter
import android.text.InputType
import android.text.TextWatcher
import android.view.Display
import android.view.animation.AccelerateDecelerateInterpolator
import android.view.animation.LinearInterpolator
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicInteger

open class GameActivity : NativeActivity() {

    companion object {
        private const val TAG = "VlitherGame"

        /* Background play: set by native (android_jni_set_run_active) while a
           live run is in progress. onStop starts the service only if true. */
        @Volatile private var runActive = false

        /** Signature must stay (Activity, Int)V (looked up from android_jni.c). */
        @JvmStatic
        fun setRunActive(activity: Activity, active: Int) {
            runActive = active != 0
            // Run ended (death / disconnect / lobby): nothing left to keep alive.
            if (!runActive) BackgroundPlayService.stop(activity)
        }

        /* Weak ref to the overlay so the static JNI callback can reach it */
        private var overlayRef: FrameLayout? = null
        private var scanAnimator: ObjectAnimator? = null

        /* ---- Beeps for SOS / quick messages (called from native code) ----
           Signatures must stay (Activity, Int)V: android_jni.c looks them up
           with GetStaticMethodID(..., "(Landroid/app/Activity;I)V"). */
        private var toneGen: android.media.ToneGenerator? = null
        private val toneLock = Any()

        private fun beep(tone: Int, durationMs: Int) {
            Thread {
                try {
                    synchronized(toneLock) {
                        if (toneGen == null) {
                            toneGen = android.media.ToneGenerator(
                                android.media.AudioManager.STREAM_MUSIC, 100)
                        }
                        toneGen?.startTone(tone, durationMs)
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "beep error: ${e.message}")
                    synchronized(toneLock) {
                        try { toneGen?.release() } catch (_: Exception) {}
                        toneGen = null
                    }
                }
            }.start()
        }

        /** kind 0 = urgent alert (teammate sent SOS), 1 = short confirm. */
        @JvmStatic
        fun playSosBeep(activity: Activity, kind: Int) {
            if (kind == 0) beep(android.media.ToneGenerator.TONE_CDMA_EMERGENCY_RINGBACK, 600)
            else beep(android.media.ToneGenerator.TONE_PROP_BEEP2, 200)
        }

        /** Quick message sent; id = slot number, used to vary the pitch. */
        @JvmStatic
        fun playQuickSound(activity: Activity, id: Int) {
            val tones = intArrayOf(
                android.media.ToneGenerator.TONE_PROP_BEEP,
                android.media.ToneGenerator.TONE_PROP_ACK,
                android.media.ToneGenerator.TONE_PROP_PROMPT,
                android.media.ToneGenerator.TONE_DTMF_1,
                android.media.ToneGenerator.TONE_DTMF_5,
                android.media.ToneGenerator.TONE_DTMF_9)
            beep(tones[((id % tones.size) + tones.size) % tones.size], 150)
        }

        private const val IME_EVENT_TEXT: Byte = 1
        private const val IME_EVENT_KEY: Byte = 2
        private const val IME_EVENT_COMPOSITION: Byte = 3
        private const val MAX_IME_EVENTS = 512
        private const val MAX_IME_TEXT_BYTES = 65536
        private val imeEvents = ConcurrentLinkedQueue<ByteArray>()
        private val imeEventCount = AtomicInteger(0)

        private fun enqueueImePacket(packet: ByteArray) {
            synchronized(imeEvents) {
                while (imeEventCount.get() >= MAX_IME_EVENTS) {
                    if (imeEvents.poll() == null) break
                    imeEventCount.decrementAndGet()
                }
                imeEvents.offer(packet)
                imeEventCount.incrementAndGet()
            }
        }

        private fun safeUtf8Length(utf8: ByteArray): Int {
            if (utf8.size <= MAX_IME_TEXT_BYTES) return utf8.size
            var length = MAX_IME_TEXT_BYTES
            while (length > 0 && (utf8[length].toInt() and 0xC0) == 0x80) {
                length--
            }
            return length
        }

        private fun enqueueImeText(text: String) {
            if (text.isEmpty()) return
            val utf8 = text.toByteArray(Charsets.UTF_8)
            val length = safeUtf8Length(utf8)
            val packet = ByteArray(length + 1)
            packet[0] = IME_EVENT_TEXT
            utf8.copyInto(packet, destinationOffset = 1, endIndex = length)
            enqueueImePacket(packet)
        }

        private fun putIntLe(packet: ByteArray, offset: Int, value: Int) {
            packet[offset] = value.toByte()
            packet[offset + 1] = (value ushr 8).toByte()
            packet[offset + 2] = (value ushr 16).toByte()
            packet[offset + 3] = (value ushr 24).toByte()
        }

        private fun enqueueImeKey(keyCode: Int, action: Int, metaState: Int) {
            val packet = ByteArray(13)
            packet[0] = IME_EVENT_KEY
            putIntLe(packet, 1, keyCode)
            putIntLe(packet, 5, action)
            putIntLe(packet, 9, metaState)
            enqueueImePacket(packet)
        }

        private fun enqueueImeComposition(replaceCodePoints: Int, text: String) {
            val utf8 = text.toByteArray(Charsets.UTF_8)
            val length = safeUtf8Length(utf8)
            val packet = ByteArray(length + 5)
            packet[0] = IME_EVENT_COMPOSITION
            putIntLe(packet, 1, replaceCodePoints.coerceIn(0, 4096))
            utf8.copyInto(packet, destinationOffset = 5, endIndex = length)
            enqueueImePacket(packet)
        }

        /**
         * Native code polls this queue from the render thread. Android's IME
         * thread never calls into the native library directly, preventing a
         * missing/mismatched JNI callback from terminating the process.
         */
        @JvmStatic
        fun pollImeEvent(activity: Activity): ByteArray? {
            if (activity !is GameActivity) return null
            synchronized(imeEvents) {
                val packet = imeEvents.poll() ?: return null
                imeEventCount.decrementAndGet()
                return packet
            }
        }

        @JvmStatic
        fun clearImeEvents(activity: Activity) {
            if (activity !is GameActivity) return
            synchronized(imeEvents) {
                imeEvents.clear()
                imeEventCount.set(0)
            }
        }

        /** Queue clipboard text through the same safe IME channel. */
        @JvmStatic
        fun enqueueClipboardPaste(activity: Activity): Boolean {
            if (activity !is GameActivity) return false
            activity.runOnUiThread {
                val text = getClipboardText(activity)
                if (text.isNotEmpty()) enqueueImeText(text)
            }
            return true
        }

        @JvmStatic
        fun getUnlockRemainingMs(activity: Activity): Long {
            return try {
                MainActivity.getUnlockRemainingMsStatic(activity.applicationContext)
            } catch (e: Exception) {
                Log.e(TAG, "getUnlockRemainingMs error: ${e.message}")
                -1L
            }
        }

        /** Read Android's primary clipboard for the native ImGui backend. */
        @JvmStatic
        fun getClipboardText(activity: Activity): String {
            return try {
                val clipboard = activity.getSystemService(Context.CLIPBOARD_SERVICE)
                    as? ClipboardManager ?: return ""
                val clip = clipboard.primaryClip ?: return ""
                if (clip.itemCount <= 0) return ""
                clip.getItemAt(0).coerceToText(activity)?.toString() ?: ""
            } catch (e: Exception) {
                Log.e(TAG, "getClipboardText error: ${e.message}")
                ""
            }
        }

        /** Write text selected in ImGui to Android's primary clipboard. */
        @JvmStatic
        fun setClipboardText(activity: Activity, text: String) {
            try {
                val clipboard = activity.getSystemService(Context.CLIPBOARD_SERVICE)
                    as? ClipboardManager ?: return
                clipboard.setPrimaryClip(ClipData.newPlainText("Vlither text", text))
            } catch (e: Exception) {
                Log.e(TAG, "setClipboardText error: ${e.message}")
            }
        }

        /** UTF-8 JNI variants avoid modified-UTF-8 corruption for emoji. */
        @JvmStatic
        fun getClipboardUtf8(activity: Activity): ByteArray =
            getClipboardText(activity).toByteArray(Charsets.UTF_8)

        @JvmStatic
        fun setClipboardUtf8(activity: Activity, utf8: ByteArray) {
            setClipboardText(activity, utf8.toString(Charsets.UTF_8))
        }

        /**
         * Called from C via JNI (android_jni.c) once the player picks
         * which captured kills to keep on the post-match review screen
         * (screenshot_run_save() in screenshot.c) -- not on every kill
         * anymore, so there's no render-thread stall to avoid on the
         * native side. Still runs its own work on a background thread
         * here regardless: PNG-encoding (twice) plus disk I/O plus a
         * MediaStore insert is real work, and the JNI call itself still
         * blocks whatever native thread invoked it until this method
         * returns, so keeping that fast matters even off the hot path.
         * Writes the raw RGBA8 pixels out as a PNG twice: once into the
         * app-private "Pictures/kills" directory (which native code
         * reads back for the in-app Kill Shots gallery -- see
         * android_path.h / android_build_kills_dir), and once into the
         * system MediaStore Images collection so it also shows up in
         * the phone's own Gallery app.
         * Signature used in android_jni.c:
         * (Landroid/app/Activity;[BIILjava/lang/String;)V
         */
        /**
         * Called from C via JNI (android_jni.c) once the player picks
         * which captured kills to keep on the post-match review screen
         * (screenshot_run_save() in screenshot.c). App-private only --
         * this does NOT touch the phone's own Gallery app. The in-app
         * Kill Shots gallery reads this back directly; pushing a copy to
         * the phone's Gallery only happens if the player explicitly taps
         * Save on that item afterward (see saveImageToGallery() below).
         * Still runs on a background thread: PNG-encoding plus disk I/O
         * is real work, and the JNI call blocks whatever native thread
         * invoked it until this method returns.
         * Signature used in android_jni.c:
         * (Landroid/app/Activity;[BIILjava/lang/String;)V
         */
        @JvmStatic
        fun saveScreenshot(
            activity: Activity,
            rgba: ByteArray,
            width: Int,
            height: Int,
            filename: String
        ) {
            Thread {
                try {
                    val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
                    bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(rgba))

                    val picturesBase = activity.getExternalFilesDir(Environment.DIRECTORY_PICTURES)
                    if (picturesBase != null) {
                        val killsDir = File(picturesBase, "kills")
                        killsDir.mkdirs()
                        FileOutputStream(File(killsDir, filename)).use { out ->
                            bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)
                        }
                    } else {
                        Log.e(TAG, "saveScreenshot: no external files dir available")
                    }

                    bitmap.recycle()
                } catch (e: Exception) {
                    Log.e(TAG, "saveScreenshot error: ${e.message}")
                }
            }.start()
        }

        /**
         * Called from C via JNI when the player taps "Save" on a kill
         * screenshot inside the in-app Kill Shots gallery -- copies the
         * already-saved app-private PNG (see saveScreenshot() above,
         * under Pictures/kills) into the system MediaStore Images
         * collection so it also shows up in the phone's own Gallery app.
         * No-ops (logs and returns) if that file doesn't exist.
         * Signature used in android_jni.c:
         * (Landroid/app/Activity;Ljava/lang/String;)V
         */
        @JvmStatic
        fun saveImageToGallery(activity: Activity, filename: String) {
            Thread {
                try {
                    val picturesBase = activity.getExternalFilesDir(Environment.DIRECTORY_PICTURES)
                    val sourceFile = picturesBase?.let { File(File(it, "kills"), filename) }
                    if (sourceFile == null || !sourceFile.exists()) {
                        Log.e(TAG, "saveImageToGallery: source file not found: $filename")
                        return@Thread
                    }

                    val resolver = activity.contentResolver
                    val values = ContentValues().apply {
                        put(MediaStore.Images.Media.DISPLAY_NAME, filename)
                        put(MediaStore.Images.Media.MIME_TYPE, "image/png")
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                            put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/Vlither")
                            put(MediaStore.Images.Media.IS_PENDING, 1)
                        }
                    }
                    val collection = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
                    } else {
                        MediaStore.Images.Media.EXTERNAL_CONTENT_URI
                    }
                    val uri = resolver.insert(collection, values)
                    if (uri != null) {
                        resolver.openOutputStream(uri)?.use { out ->
                            sourceFile.inputStream().use { input -> input.copyTo(out) }
                        }
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                            values.clear()
                            values.put(MediaStore.Images.Media.IS_PENDING, 0)
                            resolver.update(uri, values, null, null)
                        }
                    } else {
                        Log.e(TAG, "saveImageToGallery: MediaStore insert failed")
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "saveImageToGallery error: ${e.message}")
                }
            }.start()
        }

        // ---------------------------------------------------------------
        // Screen recording (MediaProjection). Clips are saved app-private
        // only (Movies/clips under getExternalFilesDir) -- pushing a copy
        // to the phone's own Gallery only happens when the player taps
        // Save on a clip in the in-app Clips gallery, same pattern as kill
        // screenshots (see saveImageToGallery() above and
        // saveClipToGallery() below).
        //
        // The permission flow is asynchronous (system consent dialog via
        // startActivityForResult/onActivityResult), unlike every other
        // JNI-callable function here which is a synchronous call-and-
        // return. Native code finds out what happened via
        // pollRecorderEvent(), mirroring the existing IME event queue
        // pattern (see pollImeEvent() above) but with plain integer event
        // codes since there's no payload to carry. Android also requires
        // fresh user consent for every single recording session -- a
        // previously granted MediaProjection can't be reused for a new
        // recording after a previous one was stopped, so
        // requestStartRecording() always shows the system dialog again.
        // ---------------------------------------------------------------

        const val REC_EVENT_STARTED = 1
        const val REC_EVENT_DENIED = 2
        const val REC_EVENT_STOPPED = 3
        const val REC_EVENT_ERROR = 4
        private const val REQUEST_CODE_SCREEN_RECORD = 4202

        private val recorderEvents = ConcurrentLinkedQueue<Int>()
        private var mediaProjection: MediaProjection? = null
        private var mediaRecorder: MediaRecorder? = null
        private var virtualDisplay: VirtualDisplay? = null
        @Volatile private var recordingActive = false

        /** Kicks off the (asynchronous) system permission dialog. Result
         * arrives later via GameActivity.onActivityResult(), which either
         * starts the actual recording or pushes REC_EVENT_DENIED. */
        @JvmStatic
        fun requestStartRecording(activity: Activity) {
            if (recordingActive) return
            try {
                val manager = activity.getSystemService(Context.MEDIA_PROJECTION_SERVICE)
                    as MediaProjectionManager
                val intent = manager.createScreenCaptureIntent()
                activity.startActivityForResult(intent, REQUEST_CODE_SCREEN_RECORD)
            } catch (e: Exception) {
                Log.e(TAG, "requestStartRecording error: ${e.message}")
                recorderEvents.add(REC_EVENT_ERROR)
            }
        }

        /** Called from GameActivity.onActivityResult() once the player
         * responds to the system dialog and grants permission. Starts the
         * foreground service (required by the platform for
         * MediaProjection), obtains the projection, and wires up a
         * VirtualDisplay feeding a MediaRecorder writing straight to the
         * app-private clips folder. */
        private fun startRecordingSessionInternal(activity: Activity, resultCode: Int, data: Intent) {
            try {
                val serviceIntent = Intent(activity, ScreenRecordService::class.java)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    activity.startForegroundService(serviceIntent)
                } else {
                    activity.startService(serviceIntent)
                }

                val manager = activity.getSystemService(Context.MEDIA_PROJECTION_SERVICE)
                    as MediaProjectionManager
                val projection = manager.getMediaProjection(resultCode, data)
                if (projection == null) {
                    recorderEvents.add(REC_EVENT_ERROR)
                    activity.stopService(serviceIntent)
                    return
                }
                projection.registerCallback(object : MediaProjection.Callback() {
                    override fun onStop() {
                        // The OS itself can revoke/end a projection (e.g.
                        // the player denies a re-prompt some devices show,
                        // or the system reclaims it) -- treat that exactly
                        // like the player tapping Stop.
                        stopRecordingInternal(activity, notifyEvent = true)
                    }
                }, null)

                val metrics = activity.resources.displayMetrics
                val width = metrics.widthPixels
                val height = metrics.heightPixels
                val density = metrics.densityDpi

                val moviesBase = activity.getExternalFilesDir(Environment.DIRECTORY_MOVIES)
                if (moviesBase == null) {
                    Log.e(TAG, "startRecordingSession: no external files dir available")
                    recorderEvents.add(REC_EVENT_ERROR)
                    projection.stop()
                    activity.stopService(serviceIntent)
                    return
                }
                val clipsDir = File(moviesBase, "clips")
                clipsDir.mkdirs()
                val filename = "clip_${System.currentTimeMillis() / 1000L}.mp4"
                val outFile = File(clipsDir, filename)

                val recorder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    MediaRecorder(activity)
                } else {
                    @Suppress("DEPRECATION")
                    MediaRecorder()
                }
                recorder.setVideoSource(MediaRecorder.VideoSource.SURFACE)
                recorder.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4)
                recorder.setVideoEncoder(MediaRecorder.VideoEncoder.H264)
                recorder.setVideoSize(width, height)
                recorder.setVideoFrameRate(30)
                recorder.setVideoEncodingBitRate(8_000_000)
                recorder.setOutputFile(outFile.absolutePath)
                recorder.prepare()

                val display = projection.createVirtualDisplay(
                    "VlitherRecording",
                    width, height, density,
                    DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                    recorder.surface,
                    null, null
                )
                if (display == null) {
                    recorder.release()
                    projection.stop()
                    activity.stopService(serviceIntent)
                    recorderEvents.add(REC_EVENT_ERROR)
                    return
                }

                recorder.start()

                mediaProjection = projection
                mediaRecorder = recorder
                virtualDisplay = display
                recordingActive = true
                recorderEvents.add(REC_EVENT_STARTED)
            } catch (e: Exception) {
                Log.e(TAG, "startRecordingSession error: ${e.message}")
                recorderEvents.add(REC_EVENT_ERROR)
                stopRecordingInternal(activity, notifyEvent = false)
            }
        }

        @JvmStatic
        fun requestStopRecording(activity: Activity) {
            stopRecordingInternal(activity, notifyEvent = true)
        }

        private fun stopRecordingInternal(activity: Activity, notifyEvent: Boolean) {
            if (!recordingActive && mediaRecorder == null) return
            try {
                mediaRecorder?.let {
                    try {
                        it.stop()
                    } catch (e: Exception) {
                        // stop() throws if called too soon after start() or if
                        // nothing was ever written -- the file may be
                        // unusable, but this must never crash the app.
                        Log.e(TAG, "mediaRecorder.stop error: ${e.message}")
                    }
                    it.release()
                }
            } catch (e: Exception) {
                Log.e(TAG, "stopRecordingInternal error: ${e.message}")
            }
            mediaRecorder = null
            try {
                virtualDisplay?.release()
            } catch (e: Exception) {
                Log.e(TAG, "virtualDisplay.release error: ${e.message}")
            }
            virtualDisplay = null
            try {
                mediaProjection?.stop()
            } catch (e: Exception) {
                Log.e(TAG, "mediaProjection.stop error: ${e.message}")
            }
            mediaProjection = null
            val wasActive = recordingActive
            recordingActive = false
            try {
                activity.stopService(Intent(activity, ScreenRecordService::class.java))
            } catch (e: Exception) {
                Log.e(TAG, "stopService error: ${e.message}")
            }
            if (notifyEvent && wasActive) recorderEvents.add(REC_EVENT_STOPPED)
        }

        @JvmStatic
        fun isRecording(activity: Activity): Boolean = recordingActive

        /** Polls one queued recorder event (REC_EVENT_* above), or 0 if
         * none is pending. Mirrors pollImeEvent()'s "poll once per frame"
         * pattern but with a plain int instead of a byte-array payload. */
        @JvmStatic
        fun pollRecorderEvent(activity: Activity): Int {
            return recorderEvents.poll() ?: 0
        }

        /* ---- Lobby background picker (called from C via JNI) ----
           requestPickLobbyBackground() opens the system photo picker. The
           chosen picture is shrunk (long side <= 1920 px), rotated upright and
           saved as filesDir/lobby_bg.jpg, then LOBBY_BG_EVENT_* is queued for
           native code to pick up with pollLobbyBgEvent() (see lobby_bg.c).
           Cancelling the picker queues nothing. */
        private const val REQUEST_CODE_PICK_LOBBY_BG = 4203
        private const val LOBBY_BG_EVENT_SAVED = 1
        private const val LOBBY_BG_EVENT_ERROR = 2
        private const val LOBBY_BG_MAX_SIDE = 1920
        private val lobbyBgEvents = ConcurrentLinkedQueue<Int>()

        @JvmStatic
        fun requestPickLobbyBackground(activity: Activity) {
            try {
                val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
                    type = "image/*"
                    addCategory(Intent.CATEGORY_OPENABLE)
                }
                activity.startActivityForResult(intent, REQUEST_CODE_PICK_LOBBY_BG)
            } catch (e: Exception) {
                Log.e(TAG, "requestPickLobbyBackground error: ${e.message}")
                lobbyBgEvents.add(LOBBY_BG_EVENT_ERROR)
            }
        }

        @JvmStatic
        fun pollLobbyBgEvent(activity: Activity): Int {
            return lobbyBgEvents.poll() ?: 0
        }

        internal fun saveLobbyBackground(activity: Activity, uri: Uri) {
            Thread {
                try {
                    val resolver = activity.contentResolver

                    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
                    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
                        lobbyBgEvents.add(LOBBY_BG_EVENT_ERROR)
                        return@Thread
                    }

                    var sample = 1
                    val longSide = maxOf(bounds.outWidth, bounds.outHeight)
                    while (longSide / (sample * 2) >= LOBBY_BG_MAX_SIDE) sample *= 2
                    val opts = BitmapFactory.Options().apply { inSampleSize = sample }
                    var bmp = resolver.openInputStream(uri)?.use {
                        BitmapFactory.decodeStream(it, null, opts)
                    }
                    if (bmp == null) {
                        lobbyBgEvents.add(LOBBY_BG_EVENT_ERROR)
                        return@Thread
                    }

                    /* Phone photos are often stored sideways with an EXIF flag. */
                    var degrees = 0f
                    try {
                        resolver.openInputStream(uri)?.use { stream ->
                            val exif = android.media.ExifInterface(stream)
                            degrees = when (exif.getAttributeInt(
                                android.media.ExifInterface.TAG_ORIENTATION,
                                android.media.ExifInterface.ORIENTATION_NORMAL
                            )) {
                                android.media.ExifInterface.ORIENTATION_ROTATE_90 -> 90f
                                android.media.ExifInterface.ORIENTATION_ROTATE_180 -> 180f
                                android.media.ExifInterface.ORIENTATION_ROTATE_270 -> 270f
                                else -> 0f
                            }
                        }
                    } catch (e: Exception) {
                        Log.w(TAG, "lobby bg exif: ${e.message}")
                    }
                    if (degrees != 0f) {
                        val m = Matrix().apply { postRotate(degrees) }
                        bmp = Bitmap.createBitmap(bmp, 0, 0, bmp.width, bmp.height, m, true)
                    }

                    val side = maxOf(bmp!!.width, bmp.height)
                    if (side > LOBBY_BG_MAX_SIDE) {
                        val s = LOBBY_BG_MAX_SIDE.toFloat() / side
                        bmp = Bitmap.createScaledBitmap(
                            bmp, (bmp.width * s).toInt().coerceAtLeast(1),
                            (bmp.height * s).toInt().coerceAtLeast(1), true
                        )
                    }

                    val tmp = File(activity.filesDir, "lobby_bg.tmp")
                    val dest = File(activity.filesDir, "lobby_bg.jpg")
                    FileOutputStream(tmp).use { out ->
                        bmp.compress(Bitmap.CompressFormat.JPEG, 90, out)
                    }
                    if (dest.exists()) dest.delete()
                    if (!tmp.renameTo(dest)) {
                        lobbyBgEvents.add(LOBBY_BG_EVENT_ERROR)
                        return@Thread
                    }
                    lobbyBgEvents.add(LOBBY_BG_EVENT_SAVED)
                } catch (e: Throwable) {
                    Log.e(TAG, "saveLobbyBackground error: ${e.message}")
                    lobbyBgEvents.add(LOBBY_BG_EVENT_ERROR)
                }
            }.start()
        }

        /* ---- Settings backup (called from C via JNI, see game/backup.c) ----
           Export: system "save as" picker -> zip containing user.dat and
           quick_msgs.txt from filesDir. Import: system file picker -> reads
           such a zip and restores ONLY those two files. BACKUP_EVENT_* is
           queued for native code to pick up with pollBackupEvent(). */
        private const val REQUEST_CODE_BACKUP_EXPORT = 4204
        private const val REQUEST_CODE_BACKUP_IMPORT = 4205
        private const val BACKUP_EVENT_EXPORTED = 1
        private const val BACKUP_EVENT_IMPORTED = 2
        private const val BACKUP_EVENT_CANCELLED = 3
        private const val BACKUP_EVENT_ERROR = 4
        private val BACKUP_FILES = arrayOf("user.dat", "quick_msgs.txt")
        private val backupEvents = ConcurrentLinkedQueue<Int>()

        @JvmStatic
        fun requestBackupExport(activity: Activity) {
            try {
                val stamp = SimpleDateFormat("yyyyMMdd-HHmm", Locale.US).format(Date())
                val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = "application/zip"
                    putExtra(Intent.EXTRA_TITLE, "zoro-backup-$stamp.zip")
                }
                activity.startActivityForResult(intent, REQUEST_CODE_BACKUP_EXPORT)
            } catch (e: Exception) {
                Log.e(TAG, "requestBackupExport error: ${e.message}")
                backupEvents.add(BACKUP_EVENT_ERROR)
            }
        }

        @JvmStatic
        fun requestBackupImport(activity: Activity) {
            try {
                val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = "*/*"
                }
                activity.startActivityForResult(intent, REQUEST_CODE_BACKUP_IMPORT)
            } catch (e: Exception) {
                Log.e(TAG, "requestBackupImport error: ${e.message}")
                backupEvents.add(BACKUP_EVENT_ERROR)
            }
        }

        @JvmStatic
        fun pollBackupEvent(activity: Activity): Int {
            return backupEvents.poll() ?: 0
        }

        internal fun exportBackup(activity: Activity, uri: Uri) {
            Thread {
                try {
                    var wrote = 0
                    val out = activity.contentResolver.openOutputStream(uri)
                    if (out == null) {
                        backupEvents.add(BACKUP_EVENT_ERROR)
                        return@Thread
                    }
                    ZipOutputStream(out.buffered()).use { zip ->
                        for (name in BACKUP_FILES) {
                            val f = File(activity.filesDir, name)
                            if (!f.isFile) continue
                            zip.putNextEntry(ZipEntry(name))
                            f.inputStream().use { it.copyTo(zip) }
                            zip.closeEntry()
                            wrote++
                        }
                    }
                    backupEvents.add(if (wrote > 0) BACKUP_EVENT_EXPORTED else BACKUP_EVENT_ERROR)
                } catch (e: Throwable) {
                    Log.e(TAG, "exportBackup error: ${e.message}")
                    backupEvents.add(BACKUP_EVENT_ERROR)
                }
            }.start()
        }

        internal fun importBackup(activity: Activity, uri: Uri) {
            Thread {
                try {
                    /* Read into memory first and only touch the real files if
                       the zip is valid, so a wrong file can't wreck settings. */
                    val found = HashMap<String, ByteArray>()
                    val input = activity.contentResolver.openInputStream(uri)
                    if (input == null) {
                        backupEvents.add(BACKUP_EVENT_ERROR)
                        return@Thread
                    }
                    ZipInputStream(input.buffered()).use { zip ->
                        var entry = zip.nextEntry
                        while (entry != null) {
                            val name = entry.name.substringAfterLast('/')
                            if (!entry.isDirectory && name in BACKUP_FILES) {
                                val bytes = zip.readBytes()
                                if (bytes.size > 8 * 1024 * 1024) {
                                    backupEvents.add(BACKUP_EVENT_ERROR)
                                    return@Thread
                                }
                                found[name] = bytes
                            }
                            zip.closeEntry()
                            entry = zip.nextEntry
                        }
                    }
                    if (!found.containsKey("user.dat")) {
                        backupEvents.add(BACKUP_EVENT_ERROR)
                        return@Thread
                    }
                    for ((name, bytes) in found) {
                        val tmp = File(activity.filesDir, "$name.tmp")
                        val dest = File(activity.filesDir, name)
                        FileOutputStream(tmp).use { it.write(bytes) }
                        if (dest.exists()) dest.delete()
                        if (!tmp.renameTo(dest)) {
                            backupEvents.add(BACKUP_EVENT_ERROR)
                            return@Thread
                        }
                    }
                    backupEvents.add(BACKUP_EVENT_IMPORTED)
                } catch (e: Throwable) {
                    Log.e(TAG, "importBackup error: ${e.message}")
                    backupEvents.add(BACKUP_EVENT_ERROR)
                }
            }.start()
        }

        /* ---- Skin file import/export (called from C via JNI, game/skin_io.c) ----
           Import: picker -> file copied to filesDir/skin_import.tmp.
           Export: C wrote filesDir/skin_export.tmp -> copied to the chosen
           "save as" location. Events: 1 import ready, 2 exported,
           3 cancelled, 4 error. */
        private const val REQUEST_CODE_SKIN_EXPORT = 4206
        private const val REQUEST_CODE_SKIN_IMPORT = 4207
        private val skinIoEvents = ConcurrentLinkedQueue<Int>()

        @JvmStatic
        fun requestSkinImport(activity: Activity) {
            try {
                val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = "*/*"
                }
                activity.startActivityForResult(intent, REQUEST_CODE_SKIN_IMPORT)
            } catch (e: Exception) {
                Log.e(TAG, "requestSkinImport error: ${e.message}")
                skinIoEvents.add(4)
            }
        }

        @JvmStatic
        fun requestSkinExport(activity: Activity) {
            try {
                val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
                val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = "application/octet-stream"
                    putExtra(Intent.EXTRA_TITLE, "slither-skins-$stamp.ntlmod")
                }
                activity.startActivityForResult(intent, REQUEST_CODE_SKIN_EXPORT)
            } catch (e: Exception) {
                Log.e(TAG, "requestSkinExport error: ${e.message}")
                skinIoEvents.add(4)
            }
        }

        @JvmStatic
        fun pollSkinIoEvent(activity: Activity): Int {
            return skinIoEvents.poll() ?: 0
        }

        internal fun copySkinFile(activity: Activity, uri: Uri, export: Boolean) {
            Thread {
                try {
                    if (export) {
                        val src = File(activity.filesDir, "skin_export.tmp")
                        val out = activity.contentResolver.openOutputStream(uri, "wt")
                        if (out == null || !src.isFile) {
                            skinIoEvents.add(4)
                            return@Thread
                        }
                        out.use { o -> src.inputStream().use { it.copyTo(o) } }
                        skinIoEvents.add(2)
                    } else {
                        val input = activity.contentResolver.openInputStream(uri)
                        if (input == null) {
                            skinIoEvents.add(4)
                            return@Thread
                        }
                        val dest = File(activity.filesDir, "skin_import.tmp")
                        input.use { i -> FileOutputStream(dest).use { i.copyTo(it) } }
                        skinIoEvents.add(1)
                    }
                } catch (e: Throwable) {
                    Log.e(TAG, "copySkinFile error: ${e.message}")
                    skinIoEvents.add(4)
                }
            }.start()
        }

        /** Called from C via JNI when the player taps "Save" on a clip in
         * the in-app Clips gallery -- copies the already-saved app-private
         * clip (filename, bare name, must already exist under
         * Movies/clips) into the system MediaStore Videos collection so
         * it also shows up in the phone's own Gallery/Photos app. */
        @JvmStatic
        fun saveClipToGallery(activity: Activity, filename: String) {
            Thread {
                try {
                    val moviesBase = activity.getExternalFilesDir(Environment.DIRECTORY_MOVIES)
                    val sourceFile = moviesBase?.let { File(File(it, "clips"), filename) }
                    if (sourceFile == null || !sourceFile.exists()) {
                        Log.e(TAG, "saveClipToGallery: source file not found: $filename")
                        return@Thread
                    }

                    val resolver = activity.contentResolver
                    val values = ContentValues().apply {
                        put(MediaStore.Video.Media.DISPLAY_NAME, filename)
                        put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                            put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/Vlither")
                            put(MediaStore.Video.Media.IS_PENDING, 1)
                        }
                    }
                    val collection = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
                    } else {
                        MediaStore.Video.Media.EXTERNAL_CONTENT_URI
                    }
                    val uri = resolver.insert(collection, values)
                    if (uri != null) {
                        resolver.openOutputStream(uri)?.use { out ->
                            sourceFile.inputStream().use { input -> input.copyTo(out) }
                        }
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                            values.clear()
                            values.put(MediaStore.Video.Media.IS_PENDING, 0)
                            resolver.update(uri, values, null, null)
                        }
                    } else {
                        Log.e(TAG, "saveClipToGallery: MediaStore insert failed")
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "saveClipToGallery error: ${e.message}")
                }
            }.start()
        }

        /** Called from C via JNI when the player taps "Play" on a clip in
         * the in-app Clips gallery -- hands off to the phone's own video
         * player via a standard VIEW intent rather than an in-app player.
         * filename must already exist under the app-private Movies/clips
         * directory. */
        @JvmStatic
        fun playClip(activity: Activity, filename: String) {
            try {
                val moviesBase = activity.getExternalFilesDir(Environment.DIRECTORY_MOVIES)
                val sourceFile = moviesBase?.let { File(File(it, "clips"), filename) }
                if (sourceFile == null || !sourceFile.exists()) {
                    Log.e(TAG, "playClip: source file not found: $filename")
                    return
                }
                val uri = androidx.core.content.FileProvider.getUriForFile(
                    activity, "${activity.packageName}.fileprovider", sourceFile
                )
                val intent = Intent(Intent.ACTION_VIEW).apply {
                    setDataAndType(uri, "video/mp4")
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
                activity.startActivity(intent)
            } catch (e: Exception) {
                Log.e(TAG, "playClip error: ${e.message}")
            }
        }

        /** Enable or disable the real Android IME bridge used by ImGui. */
        @JvmStatic
        fun setTextInputActive(activity: Activity, active: Boolean) {
            (activity as? GameActivity)?.setTextInputActiveOnUi(active)
        }

        /**
         * What the focused ImGui text field holds (UTF-8), its
         * ImGuiInputTextFlags, the longest text it can take (0 = unknown) and
         * whether the player turned on Settings > Native text box. Native code
         * calls this right before setTextInputActive(true).
         * Signature used in android_jni.c: (Landroid/app/Activity;[BIIZ)V
         */
        @JvmStatic
        fun setTextInputText(
            activity: Activity, utf8: ByteArray?, flags: Int, maxLen: Int, useBox: Boolean
        ) {
            val a = activity as? GameActivity ?: return
            a.pendingText = if (utf8 == null) "" else String(utf8, Charsets.UTF_8)
            a.pendingFlags = flags
            a.pendingMaxLen = maxLen
            a.useNativeBox = useBox
        }

        @JvmStatic
        fun requestAdFromC(activity: Activity) {
            try {
                val intent = android.content.Intent(activity, MainActivity::class.java)
                intent.flags = android.content.Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                activity.startActivity(intent)
            } catch (e: Exception) {
                Log.e(TAG, "requestAdFromC error: ${e.message}")
            }
        }

        /**
         * Called from C via JNI (android_jni.c) when the first Vulkan frame
         * has been rendered. Fades out and removes the loading overlay.
         * Signature used in android_jni.c: (Landroid/app/Activity;)V
         */
        @JvmStatic
        fun notifyGameReady(activity: Activity) {
            activity.runOnUiThread {
                val overlay = overlayRef ?: return@runOnUiThread
                scanAnimator?.cancel()
                overlay.animate()
                    .alpha(0f)
                    .setDuration(600)
                    .setStartDelay(120)
                    .setInterpolator(AccelerateDecelerateInterpolator())
                    .setListener(object : AnimatorListenerAdapter() {
                        override fun onAnimationEnd(animation: Animator) {
                            (overlay.parent as? ViewGroup)?.removeView(overlay)
                            overlayRef  = null
                            scanAnimator = null
                        }
                    })
                    .start()
            }
        }
    }


    private var imeBridgeView: ImeBridgeView? = null
    private var textInputActive = false

    /**
     * A one-pixel Android text editor. It is visually hidden, but because it
     * exposes a genuine InputConnection, Gboard and other keyboards can send
     * commitText(), composing text, clipboard-history taps, emoji, deletion,
     * enter and hardware-key events. ImGui remains the visible text field.
     */
    private inner class ImeBridgeView(context: Context) : View(context) {
        private var composingCodePoints = 0

        /* Text the bridge has already inserted into the ImGui field that
         * could still be part of the word being typed (the tail of the
         * current word, never containing whitespace), plus the text that is
         * currently being composed. Gboard treats "20k" as ONE word: after
         * "20" has gone in it later sends setComposingText("20k") and expects
         * the editor to replace the "20" it already has. This bridge keeps no
         * real text buffer, so without this memory the "20" was never
         * replaced and "20k" was simply appended after it, giving "2020k".
         * See setComposingText() / setComposingRegion() below. */
        private val recentRun = StringBuilder()
        private var composingText = ""

        private fun noteInserted(text: String) {
            if (text.isEmpty()) return
            var cut = -1
            for (i in text.indices) if (Character.isWhitespace(text[i])) cut = i
            if (cut >= 0) {
                recentRun.setLength(0)
                recentRun.append(text, cut + 1, text.length)
            } else {
                recentRun.append(text)
            }
            if (recentRun.length > 64) recentRun.delete(0, recentRun.length - 64)
        }

        private fun breakRun() {
            recentRun.setLength(0)
        }

        /* Sends only what CHANGED between the text that is currently shown as
         * composing ([old]) and the text the keyboard wants there now
         * ([new]). Keyboards re-send the whole word on every keystroke
         * ("h", "he", "hel"...). Deleting the entire old word and typing it
         * again made the letters vanish and reappear on every key press,
         * because ImGui handles each Backspace over one or two frames. With
         * the common start kept, a normal keystroke is just "append one
         * character" and nothing is deleted. */
        private fun sendReplacement(old: String, new: String) {
            var p = 0
            val m = minOf(old.length, new.length)
            while (p < m && old[p] == new[p]) p++
            /* Never split a surrogate pair (emoji etc.). */
            if (p > 0 && Character.isHighSurrogate(old[p - 1])) p--
            val replace = Character.codePointCount(old, p, old.length)
            val tail = new.substring(p)
            if (replace == 0 && tail.isEmpty()) return
            enqueueImeComposition(replace, tail)
        }

        private fun noteKey(keyCode: Int) {
            if (KeyEvent.isModifierKey(keyCode)) return
            if (keyCode == KeyEvent.KEYCODE_DEL) {
                if (recentRun.isNotEmpty()) recentRun.setLength(recentRun.length - 1)
            } else {
                breakRun()
            }
        }

        /* Some keyboards (notably many numeric/digit keys) deliver the
         * exact same KeyEvent through TWO independent Android paths at
         * once: the normal system dispatch (View.onKeyDown/onKeyUp) AND
         * this view's InputConnection.sendKeyEvent() (explicit IME
         * injection). Both funnel into forwardKeyEvent() below, so
         * without a guard every digit typed via the on-screen keyboard
         * is inserted twice (typing "4" becomes "44"). Android stamps
         * every KeyEvent with an eventTime that is identical when the
         * same underlying event is (re)delivered through both paths,
         * but distinct for every new physical event -- including
         * auto-repeat while a key is held, which reuses downTime but
         * still advances eventTime each repeat. So eventTime is the
         * right discriminator: it catches true duplicate delivery
         * without swallowing legitimate held-key repeats.
         */
        private var lastForwardedKeyCode = -1
        private var lastForwardedAction = -1
        private var lastForwardedEventTime = -1L

        init {
            isFocusable = true
            isFocusableInTouchMode = true
            isClickable = false
            alpha = 0.01f
            importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS
        }

        override fun onCheckIsTextEditor(): Boolean = true

        private fun codePointCount(text: CharSequence): Int =
            Character.codePointCount(text, 0, text.length)

        private fun sendText(text: String) {
            enqueueImeText(text)
            noteInserted(text)
        }

        private fun sendKey(keyCode: Int, metaState: Int = 0) {
            noteKey(keyCode)
            enqueueImeKey(keyCode, KeyEvent.ACTION_DOWN, metaState)
            enqueueImeKey(keyCode, KeyEvent.ACTION_UP, metaState)
        }

        private fun sendShortcut(keyCode: Int) {
            breakRun()
            enqueueImeKey(KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.ACTION_DOWN, 0)
            enqueueImeKey(keyCode, KeyEvent.ACTION_DOWN, KeyEvent.META_CTRL_ON)
            enqueueImeKey(keyCode, KeyEvent.ACTION_UP, KeyEvent.META_CTRL_ON)
            enqueueImeKey(KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.ACTION_UP, 0)
        }

        private fun forwardKeyEvent(event: KeyEvent): Boolean {
            if (event.action == KeyEvent.ACTION_MULTIPLE) {
                val chars = event.characters.orEmpty()
                if (chars.isNotEmpty()) sendText(chars)
                return true
            }

            if (event.keyCode == lastForwardedKeyCode &&
                event.action == lastForwardedAction &&
                event.eventTime == lastForwardedEventTime) {
                return true
            }
            lastForwardedKeyCode = event.keyCode
            lastForwardedAction = event.action
            lastForwardedEventTime = event.eventTime

            if (event.action == KeyEvent.ACTION_DOWN &&
                !event.isCtrlPressed && !event.isAltPressed && event.isPrintingKey) {
                val codePoint = event.unicodeChar
                if (codePoint > 0) {
                    sendText(String(Character.toChars(codePoint)))
                    return true
                }
            }

            if (event.action == KeyEvent.ACTION_DOWN) noteKey(event.keyCode)
            enqueueImeKey(event.keyCode, event.action, event.metaState)
            return true
        }

        fun resetComposition() {
            composingCodePoints = 0
            composingText = ""
            breakRun()
        }

        override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
            outAttrs.inputType = InputType.TYPE_CLASS_TEXT or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or
                EditorInfo.IME_FLAG_NO_FULLSCREEN
            outAttrs.initialSelStart = 0
            outAttrs.initialSelEnd = 0

            return object : BaseInputConnection(this@ImeBridgeView, false) {
                override fun commitText(text: CharSequence?, newCursorPosition: Int): Boolean {
                    val committed = text?.toString().orEmpty()
                    if (composingCodePoints > 0 &&
                        codePointCount(composingText) == composingCodePoints) {
                        /* Usually the same word the keyboard was composing,
                         * maybe plus a space: only the difference is sent. */
                        sendReplacement(composingText, committed)
                    } else {
                        enqueueImeComposition(composingCodePoints, committed)
                    }
                    composingCodePoints = 0
                    composingText = ""
                    noteInserted(committed)
                    return true
                }

                override fun setComposingText(text: CharSequence?, newCursorPosition: Int): Boolean {
                    val composing = text?.toString().orEmpty()
                    if (composingCodePoints == 0 && recentRun.isNotEmpty() &&
                        composing.length > recentRun.length &&
                        composing.startsWith(recentRun.toString())) {
                        /* The keyboard is re-opening the word it already
                         * typed ("20" -> "20k") but the bridge was not told
                         * those characters were composing. They are already
                         * in the field, so only the new part is added. */
                        val already = recentRun.toString()
                        breakRun()
                        sendReplacement(already, composing)
                    } else if (codePointCount(composingText) == composingCodePoints) {
                        /* Normal case: change the composing word from what
                         * is shown now to what the keyboard wants. */
                        sendReplacement(composingText, composing)
                    } else {
                        /* Out of sync (should not happen): fall back to
                         * replacing everything that is believed composing. */
                        enqueueImeComposition(composingCodePoints, composing)
                    }
                    composingCodePoints = codePointCount(composing)
                    composingText = composing
                    return true
                }

                override fun setComposingRegion(start: Int, end: Int): Boolean {
                    /* Keyboards use this to re-open text they already
                     * committed as composing again. Only text this bridge
                     * inserted itself is known, so never claim more than that
                     * (the field may also hold text typed earlier). */
                    val n = minOf(maxOf(end - start, 0), codePointCount(recentRun))
                    if (n > 0 && composingCodePoints == 0) {
                        val keep = recentRun.offsetByCodePoints(recentRun.length, -n)
                        composingText = recentRun.substring(keep)
                        recentRun.setLength(keep)
                        composingCodePoints = n
                    }
                    return true
                }

                override fun finishComposingText(): Boolean {
                    noteInserted(composingText)
                    composingText = ""
                    composingCodePoints = 0
                    return true
                }

                override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                    composingCodePoints = 0
                    composingText = ""
                    breakRun()
                    repeat(beforeLength.coerceIn(0, 4096)) { sendKey(KeyEvent.KEYCODE_DEL) }
                    repeat(afterLength.coerceIn(0, 4096)) { sendKey(KeyEvent.KEYCODE_FORWARD_DEL) }
                    return true
                }

                override fun deleteSurroundingTextInCodePoints(
                    beforeLength: Int,
                    afterLength: Int
                ): Boolean = deleteSurroundingText(beforeLength, afterLength)

                override fun sendKeyEvent(event: KeyEvent): Boolean =
                    forwardKeyEvent(event)

                override fun performEditorAction(actionCode: Int): Boolean {
                    sendKey(KeyEvent.KEYCODE_ENTER)
                    return true
                }

                override fun performContextMenuAction(id: Int): Boolean {
                    return when (id) {
                        android.R.id.paste, android.R.id.pasteAsPlainText -> {
                            val text = getClipboardText(this@GameActivity)
                            sendText(text)
                            true
                        }
                        android.R.id.selectAll -> {
                            sendShortcut(KeyEvent.KEYCODE_A)
                            true
                        }
                        android.R.id.copy -> {
                            sendShortcut(KeyEvent.KEYCODE_C)
                            true
                        }
                        android.R.id.cut -> {
                            sendShortcut(KeyEvent.KEYCODE_X)
                            true
                        }
                        else -> super.performContextMenuAction(id)
                    }
                }
            }
        }

        override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean =
            forwardKeyEvent(event)

        override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean =
            forwardKeyEvent(event)

        override fun onKeyMultiple(
            keyCode: Int,
            repeatCount: Int,
            event: KeyEvent
        ): Boolean = forwardKeyEvent(event)
    }

    private fun installImeBridge() {
        val decor = window.decorView as? ViewGroup ?: return
        val bridge = ImeBridgeView(this)
        val params = FrameLayout.LayoutParams(1, 1, Gravity.TOP or Gravity.START)
        decor.addView(bridge, params)
        imeBridgeView = bridge
    }

    /* Ask for the keyboard. showSoftInput() with flag 0 is an explicit request
     * (SHOW_IMPLICIT is dropped, or hidden again, by many devices when the
     * request does not come straight from a touch on this view -- here the tap
     * landed on the ImGui surface, not on this 1px bridge view). Android 11+
     * also has WindowInsetsController, which is the reliable path on newer
     * versions where showSoftInput() alone is sometimes ignored for a view
     * that is not the one being touched, so ask through both. */
    private fun showImeNow(bridge: ImeBridgeView, imm: InputMethodManager) {
        bridge.requestFocus()
        imm.showSoftInput(bridge, 0)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.insetsController?.show(android.view.WindowInsets.Type.ime())
        }
    }

    private fun setTextInputActiveOnUi(active: Boolean) {
        runOnUiThread {
            if (useNativeBox) {
                if (active) showTextBox() else hideTextBox()
                return@runOnUiThread
            }
            /* The option was switched off while the box was open. */
            if (textDialog?.isShowing == true) hideTextBox()

            val bridge = imeBridgeView ?: return@runOnUiThread
            val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
                ?: return@runOnUiThread

            if (textInputActive == active) {
                /* Already in this state. When a text field is still focused
                 * in ImGui and the player taps it again, native calls us with
                 * active=true once more: that is the case where the keyboard
                 * was dismissed (Back gesture / hide arrow) while the field
                 * kept focus, so ImGui's "wants text" flag never changed and
                 * nothing would ever bring the keyboard back. Showing it
                 * again is harmless if it is already visible. */
                if (active) showImeNow(bridge, imm)
                return@runOnUiThread
            }
            textInputActive = active

            if (active) {
                clearImeEvents(this@GameActivity)
                bridge.resetComposition()
                bridge.requestFocus()
                imm.restartInput(bridge)
                bridge.post { showImeNow(bridge, imm) }
            } else {
                clearImeEvents(this@GameActivity)
                bridge.resetComposition()
                imm.hideSoftInputFromWindow(bridge.windowToken, 0)
                bridge.clearFocus()
                window.decorView.requestFocus()
            }
        }
    }

    /* ── Native text box (Settings > Native text box) ───────────────────
     *
     * The game's text fields are drawn by ImGui, so Android never sees them as
     * text fields. With this option on, tapping one opens a REAL Android
     * EditText in its own small window docked above the keyboard. Autocorrect,
     * paste, voice typing, the clipboard bar and selection handles then work
     * like in any other app, and the field can't end up hidden behind the
     * keyboard. Whatever is typed is handed to the ImGui field through the same
     * event queue the invisible IME bridge uses: only the part that changed is
     * sent, as "delete N characters at the end, then add this text".
     *
     * It is its own window (a Dialog), because the game window gives its input
     * queue to native code and so never delivers touches or keys to ordinary
     * views. The game keeps running behind it.
     */
    private var textDialog: Dialog? = null
    private var textDialogEdit: EditText? = null

    /** The text the ImGui field holds, as far as this box knows. */
    private var textDialogSent = ""
    private var textDialogSuppress = false

    @Volatile private var pendingText = ""
    @Volatile private var pendingFlags = 0
    @Volatile private var pendingMaxLen = 0
    @Volatile private var useNativeBox = false

    /** Sends only what changed between [old] and [new] to the ImGui field. */
    private fun sendTextDiff(old: String, new: String) {
        var p = 0
        val m = minOf(old.length, new.length)
        while (p < m && old[p] == new[p]) p++
        /* Never split a surrogate pair (emoji etc.). */
        if (p > 0 && Character.isHighSurrogate(old[p - 1])) p--
        val replace = Character.codePointCount(old, p, old.length)
        val tail = new.substring(p)
        if (replace == 0 && tail.isEmpty()) return
        enqueueImeComposition(replace, tail)
    }

    private fun textBoxSubmit() {
        enqueueImeKey(KeyEvent.KEYCODE_ENTER, KeyEvent.ACTION_DOWN, 0)
        enqueueImeKey(KeyEvent.KEYCODE_ENTER, KeyEvent.ACTION_UP, 0)
        hideTextBox()
    }

    private fun createTextBox(): Dialog {
        val edit = EditText(this).apply {
            setTextColor(Color.WHITE)
            setHintTextColor(0x99FFFFFF.toInt())
            setSingleLine(true)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 17f)
            setBackgroundColor(Color.TRANSPARENT)
            setPadding(dp(10f), dp(10f), dp(10f), dp(10f))
            /* NO_EXTRACT_UI / NO_FULLSCREEN: in landscape many keyboards
             * otherwise cover the whole screen with their own full-screen
             * editor and ignore this box. */
            imeOptions = EditorInfo.IME_ACTION_SEND or
                EditorInfo.IME_FLAG_NO_EXTRACT_UI or
                EditorInfo.IME_FLAG_NO_FULLSCREEN
        }
        val send = Button(this).apply {
            text = "OK"
            isAllCaps = false
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            minWidth = dp(64f)
            minimumWidth = dp(64f)
            background = GradientDrawable().apply {
                setColor(0xFF2F80ED.toInt())
                cornerRadius = dp(12f).toFloat()
            }
        }
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(10f), dp(8f), dp(10f), dp(8f))
            background = GradientDrawable().apply {
                setColor(0xF2151A22.toInt())
                cornerRadius = dp(14f).toFloat()
                setStroke(dp(1f), 0x33FFFFFF)
            }
            addView(edit, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(send, LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(44f)
            ).also { it.leftMargin = dp(8f) })
        }

        val dialog = Dialog(this, android.R.style.Theme_Translucent_NoTitleBar)
        dialog.setContentView(row)
        dialog.setCanceledOnTouchOutside(false)
        dialog.window?.let { w ->
            w.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
            w.setGravity(Gravity.BOTTOM)
            w.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            w.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
            /* Touches outside the box still reach the game behind it. */
            w.addFlags(WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL)
            /* Only this small window is resized by the keyboard; the game's
             * own window keeps its ADJUST_PAN mode and is never resized. */
            @Suppress("DEPRECATION")
            w.setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE or
                    WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE
            )
        }

        edit.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                if (textDialogSuppress) return
                val now = s?.toString().orEmpty()
                sendTextDiff(textDialogSent, now)
                textDialogSent = now
            }
        })
        edit.setOnEditorActionListener { _, _, _ ->
            textBoxSubmit()
            true
        }
        send.setOnClickListener { textBoxSubmit() }
        dialog.setOnDismissListener {
            /* Closed (OK, Enter, Back or the field lost focus): forget the
             * text, the next time the box opens it is filled in again from
             * the ImGui field. */
            textDialogSuppress = true
            edit.setText("")
            textDialogSuppress = false
            textDialogSent = ""
        }

        textDialogEdit = edit
        return dialog
    }

    private fun applyTextBoxType(edit: EditText, flags: Int, maxLen: Int) {
        val password = (flags and (1 shl 10)) != 0   // ImGuiInputTextFlags_Password
        val decimal = (flags and 1) != 0             // ImGuiInputTextFlags_CharsDecimal
        val type = when {
            password -> InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
            decimal -> InputType.TYPE_CLASS_NUMBER or
                InputType.TYPE_NUMBER_FLAG_DECIMAL or InputType.TYPE_NUMBER_FLAG_SIGNED
            else -> InputType.TYPE_CLASS_TEXT
        }
        if (edit.inputType != type) edit.inputType = type
        edit.filters = if (maxLen > 0) {
            arrayOf<InputFilter>(InputFilter.LengthFilter(maxLen))
        } else {
            arrayOf<InputFilter>()
        }
    }

    private fun hideDialogSystemBars(d: Dialog) {
        val w = d.window ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            w.insetsController?.let { c ->
                c.hide(android.view.WindowInsets.Type.systemBars())
                c.systemBarsBehavior =
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            w.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            )
        }
    }

    private fun showTextBox() {
        val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
            ?: return

        /* The invisible bridge must not hold the keyboard at the same time. */
        if (textInputActive) {
            textInputActive = false
            imeBridgeView?.let {
                it.resetComposition()
                imm.hideSoftInputFromWindow(it.windowToken, 0)
            }
        }

        val dialog = textDialog ?: createTextBox().also { textDialog = it }
        val edit = textDialogEdit ?: return
        val text = pendingText
        applyTextBoxType(edit, pendingFlags, pendingMaxLen)

        val inStep = dialog.isShowing && edit.text.toString() == text
        if (!inStep) {
            /* Fill the box with what the ImGui field holds, and make sure
             * that field's cursor is at the end, because changes are sent as
             * edits at the end of the text. */
            clearImeEvents(this)
            textDialogSuppress = true
            edit.setText(text)
            edit.setSelection(edit.text.length)
            textDialogSuppress = false
            textDialogSent = text
            enqueueImeKey(KeyEvent.KEYCODE_MOVE_END, KeyEvent.ACTION_DOWN, 0)
            enqueueImeKey(KeyEvent.KEYCODE_MOVE_END, KeyEvent.ACTION_UP, 0)
        }

        if (!dialog.isShowing) {
            dialog.show()
            hideDialogSystemBars(dialog)
        }
        edit.requestFocus()
        edit.post { imm.showSoftInput(edit, 0) }
    }

    private fun hideTextBox() {
        val dialog = textDialog ?: return
        if (!dialog.isShowing) return
        val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
        textDialogEdit?.let { imm?.hideSoftInputFromWindow(it.windowToken, 0) }
        try {
            dialog.dismiss()
        } catch (e: Exception) {
            Log.w(TAG, "text box dismiss failed: ${e.message}")
        }
    }

    /* ── Loading overlay ───────────────────────────────────────────── */

    private fun dp(value: Float): Int =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics
        ).toInt()

    private fun sp(value: Float): Float =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_SP, value, resources.displayMetrics
        )

    private fun buildLoadingOverlay(): FrameLayout {

        /* ── Root: full-screen dark background ── */
        val root = FrameLayout(this).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
            setBackgroundColor(Color.parseColor("#0D0E14"))
            alpha = 0f   // start invisible; we fade it in below
        }

        /* ── Centre column ── */
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity     = Gravity.CENTER
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER
            )
        }

        /* Line 1 – "Official Vlither by Ignite" */
        val line1 = TextView(this).apply {
            text    = "Official Vlither by Ignite"
            setTextColor(Color.parseColor("#5DCFCF"))   // muted cyan
            setTextSize(TypedValue.COMPLEX_UNIT_PX, sp(15f))
            typeface = Typeface.create("monospace", Typeface.NORMAL)
            gravity  = Gravity.CENTER
            letterSpacing = 0.12f
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).also { it.bottomMargin = dp(6f) }
        }

        /* Line 2 – "Mobile Vlither by zoro" */
        val line2 = TextView(this).apply {
            text    = "Mobile Vlither by zoro"
            setTextColor(Color.parseColor("#2BFF88"))   // bright neon green
            setTextSize(TypedValue.COMPLEX_UNIT_PX, sp(22f))
            typeface = Typeface.create("monospace", Typeface.BOLD)
            gravity  = Gravity.CENTER
            letterSpacing = 0.10f
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).also { it.bottomMargin = dp(36f) }
        }

        /* Loading bar track */
        val trackWidth = dp(260f)
        val track = FrameLayout(this).apply {
            layoutParams = LinearLayout.LayoutParams(trackWidth, dp(4f))
            setBackgroundColor(Color.parseColor("#1C2030"))  // dark track
            clipChildren = true
            clipToPadding = true
        }

        /* Scanning bar inside track */
        val scanBar = View(this).apply {
            layoutParams = FrameLayout.LayoutParams(dp(90f), dp(4f))
            setBackgroundColor(Color.parseColor("#00E5FF"))  // neon cyan
        }
        track.addView(scanBar)

        /* Animate scan bar: slides left → right, loops forever */
        val scanAnim = ObjectAnimator.ofFloat(
            scanBar, "translationX",
            -dp(90f).toFloat(),
            trackWidth.toFloat()
        ).apply {
            duration       = 1100L
            repeatCount    = ValueAnimator.INFINITE
            repeatMode     = ValueAnimator.RESTART
            interpolator   = LinearInterpolator()
        }
        scanAnimator = scanAnim

        col.addView(line1)
        col.addView(line2)
        col.addView(track)
        root.addView(col)

        /* Fade the whole overlay in */
        root.animate()
            .alpha(1f)
            .setDuration(700)
            .setInterpolator(AccelerateDecelerateInterpolator())
            .withEndAction { scanAnim.start() }
            .start()

        return root
    }

    /* ── System UI helpers ─────────────────────────────────────────── */

    private fun hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let { c ->
                c.hide(android.view.WindowInsets.Type.systemBars())
                c.systemBarsBehavior =
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            )
        }
    }

    /* ── Lifecycle ─────────────────────────────────────────────────── */

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        @Suppress("DEPRECATION")
        window.setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN or
                WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED or
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
            WindowManager.LayoutParams.FLAG_FULLSCREEN or
                WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED or
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
        )
        /* Never resize the Vulkan surface when the keyboard opens: pan the
         * window instead. A surface resize while the keyboard animates in
         * forces a swapchain rebuild mid-frame (the "freeze" when tapping a
         * text field), and the keyboard stays hidden until the next tap. */
        @Suppress("DEPRECATION")
        window.setSoftInputMode(
            WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN or
                WindowManager.LayoutParams.SOFT_INPUT_STATE_HIDDEN
        )
        enableHighPerformanceDisplay()
        hideSystemBars()
        installImeBridge()

        /* Add the overlay on top of NativeActivity's surface view */
        val overlay = buildLoadingOverlay()
        overlayRef  = overlay
        window.decorView.let {
            if (it is ViewGroup) it.addView(overlay)
        }

        Log.d(TAG, "GameActivity created – loading overlay shown")
    }

    private fun enableHighPerformanceDisplay() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                window.setSustainedPerformanceMode(true)
            }

            val display = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                display
            } else {
                @Suppress("DEPRECATION")
                windowManager.defaultDisplay
            }

            val bestMode = display?.supportedModes?.maxByOrNull { it.refreshRate }
            if (bestMode != null) {
                val attrs = window.attributes
                attrs.preferredDisplayModeId = bestMode.modeId
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    attrs.preferredRefreshRate = bestMode.refreshRate
                }
                window.attributes = attrs

                Log.i(TAG, "Requested display mode ${bestMode.physicalWidth}x${bestMode.physicalHeight} @ ${bestMode.refreshRate}Hz")
            }
        } catch (e: Exception) {
            Log.w(TAG, "High-performance display request failed: ${e.message}")
        }
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
    }

    // Background play: back in front -> the service is no longer needed.
    override fun onStart() {
        super.onStart()
        BackgroundPlayService.stop(this)
    }

    // Leaving the app mid-run: keep the process awake so the run continues.
    override fun onStop() {
        if (runActive) BackgroundPlayService.start(this)
        super.onStop()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    override fun onDestroy() {
        try {
            textDialog?.dismiss()
        } catch (_: Exception) {
        }
        textDialog = null
        textDialogEdit = null
        setTextInputActiveOnUi(false)
        clearImeEvents(this)
        imeBridgeView = null
        scanAnimator?.cancel()
        overlayRef  = null
        scanAnimator = null
        // Safety net: don't leak the MediaProjection/foreground service if
        // the activity is destroyed mid-recording (backgrounded and
        // reclaimed, crash elsewhere, etc).
        stopRecordingInternal(this, notifyEvent = false)
        super.onDestroy()
        Log.d(TAG, "GameActivity destroyed")
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_CODE_SCREEN_RECORD) {
            if (resultCode == Activity.RESULT_OK && data != null) {
                startRecordingSessionInternal(this, resultCode, data)
            } else {
                recorderEvents.add(REC_EVENT_DENIED)
            }
        } else if (requestCode == REQUEST_CODE_PICK_LOBBY_BG) {
            val uri = data?.data
            if (resultCode == Activity.RESULT_OK && uri != null) {
                saveLobbyBackground(this, uri)
            }
        } else if (requestCode == REQUEST_CODE_BACKUP_EXPORT ||
                   requestCode == REQUEST_CODE_BACKUP_IMPORT) {
            val uri = data?.data
            if (resultCode != Activity.RESULT_OK || uri == null) {
                backupEvents.add(BACKUP_EVENT_CANCELLED)
            } else if (requestCode == REQUEST_CODE_BACKUP_EXPORT) {
                exportBackup(this, uri)
            } else {
                importBackup(this, uri)
            }
        } else if (requestCode == REQUEST_CODE_SKIN_EXPORT ||
                   requestCode == REQUEST_CODE_SKIN_IMPORT) {
            val uri = data?.data
            if (resultCode != Activity.RESULT_OK || uri == null) {
                skinIoEvents.add(3)
            } else {
                copySkinFile(this, uri, requestCode == REQUEST_CODE_SKIN_EXPORT)
            }
        }
    }
}
