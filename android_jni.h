#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#ifdef ANDROID

#include <stdbool.h>

long android_jni_get_unlock_remaining_ms(void);

void android_jni_request_ad(void);

void android_jni_notify_game_ready(void);

void android_jni_open_url(const char* url);

const char* android_jni_get_clipboard_text(void);

void android_jni_set_clipboard_text(const char* text);

void android_jni_set_text_input_active(bool active);
bool android_jni_enqueue_clipboard_paste(void);

/* Saves an RGBA8 pixel buffer as a PNG into the app-private
   "Pictures/kills" directory only (see android_path.h) -- this is what
   the in-app kill-shots gallery reads back from. Does NOT touch the
   phone's own Gallery app; see android_jni_save_image_to_gallery() for
   that, which the gallery calls separately when the player explicitly
   asks to keep a specific one. filename should be a bare name (no
   directory), e.g. "kill_1734000000_3.png". Safe to call from any
   thread; does its own JNIEnv attach like the other android_jni_*
   functions here. */
void android_jni_save_screenshot(const unsigned char* rgba, int width,
                                 int height, const char* filename);

/* Copies an already-saved kill screenshot (filename, bare name, must
   already exist under the app-private "Pictures/kills" directory -- see
   android_jni_save_screenshot() above) into the system MediaStore Images
   collection, so it shows up in the phone's own Gallery app too. Called
   when the player taps "Save" on an item in the in-app Kill Shots
   gallery. Fire-and-forget; failures are logged on the Kotlin side, not
   surfaced back to native code. */
void android_jni_save_image_to_gallery(const char* filename);

/* --- Screen recording (MediaProjection, see GameActivity.kt) ---
   The permission flow is asynchronous (system consent dialog), so
   starting/stopping just kick things off; use
   android_jni_poll_recorder_event() once per frame to find out what
   actually happened, mirroring android_jni_poll_ime_event()'s pattern but
   with a plain event code instead of a byte-array payload (there's no
   data to carry -- Kotlin already knows the clip's filename). */
typedef enum android_recorder_event {
    ANDROID_RECORDER_EVENT_NONE = 0,
    ANDROID_RECORDER_EVENT_STARTED = 1,
    ANDROID_RECORDER_EVENT_DENIED = 2,
    ANDROID_RECORDER_EVENT_STOPPED = 3,
    ANDROID_RECORDER_EVENT_ERROR = 4,
} android_recorder_event;

/* Shows the system "allow screen recording" dialog. Android requires
   fresh consent for every recording session -- there's no way to skip
   this on a second recording within the same app run. */
void android_jni_request_start_recording(void);
void android_jni_request_stop_recording(void);
/* Cheap, synchronous state check (unlike the event queue, safe to call
   every frame without draining anything). */
bool android_jni_is_recording(void);
/* Returns ANDROID_RECORDER_EVENT_NONE if nothing is pending. */
android_recorder_event android_jni_poll_recorder_event(void);

/* Copies an already-saved clip (filename, bare name, must already exist
   under the app-private "Movies/clips" directory) into the system
   MediaStore Videos collection, so it shows up in the phone's own
   Gallery/Photos app too. Called when the player taps "Save" on a clip
   in the in-app Clips gallery. */
void android_jni_save_clip_to_gallery(const char* filename);

/* Hands an already-saved clip off to the phone's own video player via a
   standard VIEW intent. Called when the player taps "Play" on a clip in
   the in-app Clips gallery. */
void android_jni_play_clip(const char* filename);

typedef enum android_ime_event_type {
    ANDROID_IME_EVENT_NONE = 0,
    ANDROID_IME_EVENT_TEXT        = 1,
    ANDROID_IME_EVENT_KEY         = 2,
    ANDROID_IME_EVENT_COMPOSITION = 3,
} android_ime_event_type;

typedef struct android_ime_event {
    android_ime_event_type type;
    char* text;
    int keycode;
    int action;
    int meta_state;
    int replace_codepoints;
} android_ime_event;

bool android_jni_poll_ime_event(android_ime_event* out_event);
void android_jni_release_ime_event(android_ime_event* event);

#endif
#endif
