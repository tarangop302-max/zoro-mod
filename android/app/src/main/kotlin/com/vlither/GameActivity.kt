package com.vlither

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.app.Activity
import android.app.NativeActivity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
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
import android.text.InputType
import android.view.Display
import android.view.animation.AccelerateDecelerateInterpolator
import android.view.animation.LinearInterpolator
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicInteger

open class GameActivity : NativeActivity() {

    companion object {
        private const val TAG = "VlitherGame"

        /* Weak ref to the overlay so the static JNI callback can reach it */
        private var overlayRef: FrameLayout? = null
        private var scanAnimator: ObjectAnimator? = null

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
        }

        private fun sendKey(keyCode: Int, metaState: Int = 0) {
            enqueueImeKey(keyCode, KeyEvent.ACTION_DOWN, metaState)
            enqueueImeKey(keyCode, KeyEvent.ACTION_UP, metaState)
        }

        private fun sendShortcut(keyCode: Int) {
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

            enqueueImeKey(event.keyCode, event.action, event.metaState)
            return true
        }

        fun resetComposition() {
            composingCodePoints = 0
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
                    enqueueImeComposition(composingCodePoints, committed)
                    composingCodePoints = 0
                    return true
                }

                override fun setComposingText(text: CharSequence?, newCursorPosition: Int): Boolean {
                    val composing = text?.toString().orEmpty()
                    enqueueImeComposition(composingCodePoints, composing)
                    composingCodePoints = codePointCount(composing)
                    return true
                }

                override fun finishComposingText(): Boolean {
                    composingCodePoints = 0
                    return true
                }

                override fun deleteSurroundingText(beforeLength: Int, afterLength: Int): Boolean {
                    composingCodePoints = 0
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

    private fun setTextInputActiveOnUi(active: Boolean) {
        runOnUiThread {
            val bridge = imeBridgeView ?: return@runOnUiThread
            if (textInputActive == active) return@runOnUiThread
            textInputActive = active

            val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
                ?: return@runOnUiThread

            if (active) {
                clearImeEvents(this@GameActivity)
                bridge.resetComposition()
                bridge.requestFocus()
                imm.restartInput(bridge)
                bridge.post {
                    bridge.requestFocus()
                    imm.showSoftInput(bridge, InputMethodManager.SHOW_IMPLICIT)
                }
            } else {
                clearImeEvents(this@GameActivity)
                bridge.resetComposition()
                imm.hideSoftInputFromWindow(bridge.windowToken, 0)
                bridge.clearFocus()
                window.decorView.requestFocus()
            }
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

        /* Line 2 – "Mobile Vlither by Lucky" */
        val line2 = TextView(this).apply {
            text    = "Mobile Vlither by Lucky"
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

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    override fun onDestroy() {
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
        }
    }
}
