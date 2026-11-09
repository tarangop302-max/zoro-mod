#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#ifdef ANDROID

#include <stdbool.h>
#include <stddef.h>

long android_jni_get_unlock_remaining_ms(void);

void android_jni_request_ad(void);

void android_jni_notify_game_ready(void);

/* Plays a short built-in tone for a Team Chat quick message (slot id
   0-3, each slot has its own tone). Fire-and-forget. */
void android_jni_play_quick_sound(int id);

/* SOS beep: kind 0 = urgent alert (teammate sent SOS), 1 = short confirm. */
void android_jni_play_sos_beep(int kind);

/* Background play: tells GameActivity whether a live run is in progress
   (alive, connected). Only reported on change. */
void android_jni_set_run_active(bool active);

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

/* Realtime leaderboards: opens the NTL "RealTime Status" page (live top
   players of every server) in a WebView over the game, with Reload and
   Close buttons. in_game = 1 makes it slightly see-through (opened during
   play); 0 is the full-screen version used on the title screen. */
void android_jni_show_realtime_lb(int in_game);

/* Lobby background picker. android_jni_request_pick_lobby_bg() opens the
   system photo picker; when the player chooses a picture Kotlin shrinks it and
   saves it as "lobby_bg.jpg" in the app files folder, then queues an event.
   android_jni_poll_lobby_bg_event(): 0 = nothing, 1 = new picture saved,
   2 = failed (cancelling the picker produces no event). */
void android_jni_request_pick_lobby_bg(void);
int android_jni_poll_lobby_bg_event(void);

/* Settings backup (see game/backup.c). Export opens the system "save as"
   picker and writes user.dat + quick_msgs.txt into a zip there; import opens
   the system file picker and restores them from such a zip. Both are
   asynchronous: poll android_jni_poll_backup_event() once per frame. */
typedef enum android_backup_event {
    ANDROID_BACKUP_EVENT_NONE = 0,
    ANDROID_BACKUP_EVENT_EXPORTED = 1,
    ANDROID_BACKUP_EVENT_IMPORTED = 2,
    ANDROID_BACKUP_EVENT_CANCELLED = 3,
    ANDROID_BACKUP_EVENT_ERROR = 4,
} android_backup_event;

void android_jni_backup_export(void);
void android_jni_backup_import(void);
android_backup_event android_jni_poll_backup_event(void);

/* Skin file import/export (see game/skin_io.c). Import: system picker, Kotlin
   copies the chosen file to skin_import.tmp in the files dir. Export: C writes
   skin_export.tmp first, then Kotlin copies it to a "save as" location.
   Poll events: 0 none, 1 import file ready, 2 exported, 3 cancelled, 4 error. */
void android_jni_skinio_import(void);
void android_jni_skinio_export(void);
int android_jni_poll_skinio_event(void);

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

/* Blocking HTTPS GET through Android's own network stack (system TLS).
   Call from a worker thread, never the render thread. Returns the HTTP status
   (>0) and a malloc'ed, NUL-terminated body in *out_body (free() it), or -1
   with a short reason in err. */
int android_jni_http_get(const char* url, char** out_body, size_t* out_len,
                         char* err, size_t err_cap);

bool android_jni_poll_ime_event(android_ime_event* out_event);
void android_jni_release_ime_event(android_ime_event* event);

#endif
#endif
