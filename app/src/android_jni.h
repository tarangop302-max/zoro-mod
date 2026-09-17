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

/* Saves an RGBA8 pixel buffer as a PNG: once to the app-private
   "Pictures/kills" directory (see android_path.h -- this is what the
   in-app kill-shots gallery reads back from) and once into the system
   MediaStore Pictures collection, so it also shows up in the phone's own
   Gallery app. filename should be a bare name (no directory), e.g.
   "kill_1734000000_3.png". Safe to call from any thread; does its own
   JNIEnv attach like the other android_jni_* functions here. */
void android_jni_save_screenshot(const unsigned char* rgba, int width,
                                 int height, const char* filename);

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
