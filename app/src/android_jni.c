#ifdef ANDROID

#include "android_jni.h"
#include "android_path.h"
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AJNI_TAG "vlither_jni"
#define AJNI_LOG(...) \
    __android_log_print(ANDROID_LOG_DEBUG, AJNI_TAG, __VA_ARGS__)

#define UNLOCK_FILENAME "vlither_unlock_expiry.txt"


extern struct android_app* g_android_app;

static int android_jni_read_i32_le(const unsigned char* bytes) {
    return (int)((unsigned int)bytes[0] |
                 ((unsigned int)bytes[1] << 8) |
                 ((unsigned int)bytes[2] << 16) |
                 ((unsigned int)bytes[3] << 24));
}

bool android_jni_poll_ime_event(android_ime_event* out_event) {
    if (!out_event) return false;
    memset(out_event, 0, sizeof(*out_event));

    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm || !g_android_app->activity->clazz) {
        return false;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    bool have_event = false;
    jclass cls = NULL;
    jbyteArray packet = NULL;
    jbyte* packet_bytes = NULL;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return false;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return false;
    }

    cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto ime_poll_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "pollImeEvent", "(Landroid/app/Activity;)[B");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto ime_poll_cleanup;
    }

    packet = (jbyteArray)(*env)->CallStaticObjectMethod(
        env, cls, mid, g_android_app->activity->clazz);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        packet = NULL;
        goto ime_poll_cleanup;
    }
    if (!packet) goto ime_poll_cleanup;

    jsize len = (*env)->GetArrayLength(env, packet);
    if (len <= 0 || len > 65541) goto ime_poll_cleanup;

    packet_bytes = (*env)->GetByteArrayElements(env, packet, NULL);
    if (!packet_bytes || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        packet_bytes = NULL;
        goto ime_poll_cleanup;
    }

    const unsigned char* bytes = (const unsigned char*)packet_bytes;
    if (bytes[0] == ANDROID_IME_EVENT_TEXT && len > 1) {
        size_t text_len = (size_t)len - 1u;
        char* copy = (char*)malloc(text_len + 1u);
        if (copy) {
            memcpy(copy, bytes + 1, text_len);
            copy[text_len] = '\0';
            out_event->type = ANDROID_IME_EVENT_TEXT;
            out_event->text = copy;
            have_event = true;
        }
    } else if (bytes[0] == ANDROID_IME_EVENT_KEY && len == 13) {
        out_event->type = ANDROID_IME_EVENT_KEY;
        out_event->keycode = android_jni_read_i32_le(bytes + 1);
        out_event->action = android_jni_read_i32_le(bytes + 5);
        out_event->meta_state = android_jni_read_i32_le(bytes + 9);
        have_event = true;
    } else if (bytes[0] == ANDROID_IME_EVENT_COMPOSITION && len >= 5) {
        size_t text_len = (size_t)len - 5u;
        char* copy = (char*)malloc(text_len + 1u);
        if (copy) {
            if (text_len > 0) memcpy(copy, bytes + 5, text_len);
            copy[text_len] = '\0';
            out_event->type = ANDROID_IME_EVENT_COMPOSITION;
            out_event->text = copy;
            out_event->replace_codepoints =
                android_jni_read_i32_le(bytes + 1);
            if (out_event->replace_codepoints < 0)
                out_event->replace_codepoints = 0;
            if (out_event->replace_codepoints > 4096)
                out_event->replace_codepoints = 4096;
            have_event = true;
        }
    }

ime_poll_cleanup:
    if (packet_bytes) {
        (*env)->ReleaseByteArrayElements(env, packet, packet_bytes, JNI_ABORT);
    }
    if (packet) (*env)->DeleteLocalRef(env, packet);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return have_event;
}

void android_jni_release_ime_event(android_ime_event* event) {
    if (!event) return;
    free(event->text);
    memset(event, 0, sizeof(*event));
}

long android_jni_get_unlock_remaining_ms(void) {
    const char* files_dir = android_get_files_dir();
    if (!files_dir || files_dir[0] == '\0') {
        AJNI_LOG("get_unlock: files_dir not set yet");
        return -1L;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", files_dir, UNLOCK_FILENAME);

    FILE* f = fopen(path, "r");
    if (!f) return -1L;

    long long expiry_ms = 0LL;
    int n = fscanf(f, "%lld", &expiry_ms);
    fclose(f);

    if (n != 1 || expiry_ms <= 0LL) {
        AJNI_LOG("get_unlock: bad file content");
        return -1L;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long now_ms = (long long)ts.tv_sec * 1000LL +
                       (long long)(ts.tv_nsec / 1000000LL);

    long long remaining = expiry_ms - now_ms;
    return (remaining > 0LL) ? (long)remaining : -1L;
}

void android_jni_request_ad(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        AJNI_LOG("request_ad: g_android_app not ready");
        return;
    }

    JavaVM*  vm  = g_android_app->activity->vm;
    jobject  obj = g_android_app->activity->clazz;
    JNIEnv*  env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            AJNI_LOG("request_ad: AttachCurrentThread failed");
            return;
        }
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        AJNI_LOG("request_ad: GetEnv failed status=%d", status);
        return;
    }

    jclass cls = (*env)->FindClass(env, "com/vlither/GameActivity");
    if (!cls) {
        AJNI_LOG("request_ad: GameActivity class not found");
        (*env)->ExceptionClear(env);
        goto cleanup;
    }

    {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "requestAdFromC", "(Landroid/app/Activity;)V");
        if (!mid) {
            AJNI_LOG("request_ad: method not found");
            (*env)->ExceptionClear(env);
            goto cleanup;
        }
        (*env)->CallStaticVoidMethod(env, cls, mid, obj);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }

cleanup:
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

void android_jni_notify_game_ready(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        AJNI_LOG("notify_game_ready: g_android_app not ready");
        return;
    }

    JavaVM*  vm  = g_android_app->activity->vm;
    jobject  obj = g_android_app->activity->clazz;
    JNIEnv*  env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            AJNI_LOG("notify_game_ready: AttachCurrentThread failed");
            return;
        }
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        AJNI_LOG("notify_game_ready: GetEnv failed status=%d", status);
        return;
    }

    jclass cls = (*env)->FindClass(env, "com/vlither/GameActivity");
    if (!cls) {
        AJNI_LOG("notify_game_ready: GameActivity class not found");
        (*env)->ExceptionClear(env);
        goto cleanup2;
    }

    {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "notifyGameReady", "(Landroid/app/Activity;)V");
        if (!mid) {
            AJNI_LOG("notify_game_ready: method not found");
            (*env)->ExceptionClear(env);
            goto cleanup2;
        }
        (*env)->CallStaticVoidMethod(env, cls, mid, obj);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }

cleanup2:
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

/*
 * Calls a static (Landroid/app/Activity;I)V method on the running
 * activity's class. The class is taken from the activity INSTANCE
 * (GetObjectClass) rather than FindClass("com/vlither/GameActivity"):
 * on a native thread FindClass uses the system class loader, which
 * cannot see app classes, so the lookup silently fails and nothing is
 * ever played. GetStaticMethodID also searches superclasses, so this
 * works for GameActivity2..5 (which extend GameActivity) too.
 */
static void ajni_call_activity_int(const char* method, int arg) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM*  vm  = g_android_app->activity->vm;
    jobject  obj = g_android_app->activity->clazz;
    JNIEnv*  env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            AJNI_LOG("%s: AttachCurrentThread failed", method);
            return;
        }
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        AJNI_LOG("%s: GetEnv failed status=%d", method, status);
        return;
    }

    jclass cls = (*env)->GetObjectClass(env, obj);
    if (!cls) {
        AJNI_LOG("%s: GetObjectClass failed", method);
        (*env)->ExceptionClear(env);
        goto cleanup_ai;
    }

    {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, method, "(Landroid/app/Activity;I)V");
        if (!mid) {
            AJNI_LOG("%s: method not found", method);
            (*env)->ExceptionClear(env);
            goto cleanup_ai;
        }
        (*env)->CallStaticVoidMethod(env, cls, mid, obj, (jint)arg);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }

cleanup_ai:
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

void android_jni_play_quick_sound(int id) {
    ajni_call_activity_int("playQuickSound", id);
}

void android_jni_show_realtime_lb(int in_game) {
    ajni_call_activity_int("showRealtimeLeaderboard", in_game ? 1 : 0);
}

void android_jni_play_sos_beep(int kind) {
    ajni_call_activity_int("playSosBeep", kind);
}

void android_jni_set_run_active(bool active) {
    ajni_call_activity_int("setRunActive", active ? 1 : 0);
}

void android_jni_open_url(const char* url) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        AJNI_LOG("open_url: g_android_app not ready");
        return;
    }

    JavaVM*  vm  = g_android_app->activity->vm;
    jobject  act = g_android_app->activity->clazz;
    JNIEnv*  env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            AJNI_LOG("open_url: AttachCurrentThread failed");
            return;
        }
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        AJNI_LOG("open_url: GetEnv failed status=%d", status);
        return;
    }

    jclass  uri_cls    = (*env)->FindClass(env, "android/net/Uri");
    jclass  intent_cls = (*env)->FindClass(env, "android/content/Intent");
    if (!uri_cls || !intent_cls) {
        AJNI_LOG("open_url: class lookup failed");
        (*env)->ExceptionClear(env);
        goto ou_cleanup;
    }

    {

        jmethodID uri_parse = (*env)->GetStaticMethodID(
            env, uri_cls, "parse",
            "(Ljava/lang/String;)Landroid/net/Uri;");
        jstring url_jstr = (*env)->NewStringUTF(env, url);
        jobject uri = (*env)->CallStaticObjectMethod(
            env, uri_cls, uri_parse, url_jstr);
        (*env)->DeleteLocalRef(env, url_jstr);
        if (!uri || (*env)->ExceptionCheck(env)) {
            AJNI_LOG("open_url: Uri.parse failed");
            (*env)->ExceptionClear(env);
            goto ou_cleanup;
        }

        jfieldID av_fid = (*env)->GetStaticFieldID(
            env, intent_cls, "ACTION_VIEW", "Ljava/lang/String;");
        jstring action_view = (jstring)(*env)->GetStaticObjectField(
            env, intent_cls, av_fid);

        jmethodID ctor = (*env)->GetMethodID(
            env, intent_cls, "<init>",
            "(Ljava/lang/String;Landroid/net/Uri;)V");
        jobject intent = (*env)->NewObject(
            env, intent_cls, ctor, action_view, uri);
        if (!intent || (*env)->ExceptionCheck(env)) {
            AJNI_LOG("open_url: Intent ctor failed");
            (*env)->ExceptionClear(env);
            goto ou_cleanup;
        }

        jclass act_cls = (*env)->GetObjectClass(env, act);
        jmethodID sa   = (*env)->GetMethodID(
            env, act_cls, "startActivity",
            "(Landroid/content/Intent;)V");
        (*env)->CallVoidMethod(env, act, sa, intent);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

        (*env)->DeleteLocalRef(env, intent);
        (*env)->DeleteLocalRef(env, uri);
    }

ou_cleanup:
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}


void android_jni_set_text_input_active(bool active) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(
        env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto text_input_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "setTextInputActive", "(Landroid/app/Activity;Z)V");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto text_input_cleanup;
    }

    (*env)->CallStaticVoidMethod(env, cls, mid,
        g_android_app->activity->clazz, active ? JNI_TRUE : JNI_FALSE);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

text_input_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

bool android_jni_enqueue_clipboard_paste(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm || !g_android_app->activity->clazz) {
        return false;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    bool queued = false;
    jclass cls = NULL;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return false;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return false;
    }

    cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto enqueue_paste_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "enqueueClipboardPaste", "(Landroid/app/Activity;)Z");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto enqueue_paste_cleanup;
    }

    queued = (*env)->CallStaticBooleanMethod(
        env, cls, mid, g_android_app->activity->clazz) == JNI_TRUE;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        queued = false;
    }

enqueue_paste_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return queued;
}

const char* android_jni_get_clipboard_text(void) {
    static char* clipboard_cache = NULL;
    static size_t clipboard_capacity = 0;
    static const char empty[] = "";

    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return empty;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return empty;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return empty;
    }

    const char* result = empty;
    jclass cls = (*env)->GetObjectClass(
        env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_get_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "getClipboardUtf8", "(Landroid/app/Activity;)[B");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_get_cleanup;
    }

    jbyteArray bytes = (jbyteArray)(*env)->CallStaticObjectMethod(
        env, cls, mid, g_android_app->activity->clazz);
    if (!bytes || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_get_cleanup;
    }

    jsize len = (*env)->GetArrayLength(env, bytes);
    if (len < 0) len = 0;
    if (len > 1048576) len = 1048576;
    size_t needed = (size_t)len + 1u;
    if (needed > clipboard_capacity) {
        size_t new_capacity = needed < 256u ? 256u : needed;
        char* new_cache = (char*)realloc(clipboard_cache, new_capacity);
        if (new_cache) {
            clipboard_cache = new_cache;
            clipboard_capacity = new_capacity;
        }
    }

    if (clipboard_cache && needed <= clipboard_capacity) {
        if (len > 0) {
            (*env)->GetByteArrayRegion(
                env, bytes, 0, len, (jbyte*)clipboard_cache);
        }
        if (!(*env)->ExceptionCheck(env)) {
            clipboard_cache[len] = '\0';
            result = clipboard_cache;
        } else {
            (*env)->ExceptionClear(env);
        }
    }
    (*env)->DeleteLocalRef(env, bytes);

clipboard_get_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return result;
}

void android_jni_set_clipboard_text(const char* text) {
    if (!text || !g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(
        env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_set_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "setClipboardUtf8", "(Landroid/app/Activity;[B)V");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_set_cleanup;
    }

    size_t length = strlen(text);
    if (length > 1048576u) length = 1048576u;
    jbyteArray bytes = (*env)->NewByteArray(env, (jsize)length);
    if (!bytes || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_set_cleanup;
    }
    if (length > 0) {
        (*env)->SetByteArrayRegion(
            env, bytes, 0, (jsize)length, (const jbyte*)text);
    }
    if (!(*env)->ExceptionCheck(env)) {
        (*env)->CallStaticVoidMethod(
            env, cls, mid, g_android_app->activity->clazz, bytes);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, bytes);

clipboard_set_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

void android_jni_save_screenshot(const unsigned char* rgba, int width,
                                 int height, const char* filename) {
    if (!rgba || width <= 0 || height <= 0 || !filename ||
        !g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(
        env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto screenshot_save_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "saveScreenshot",
        "(Landroid/app/Activity;[BIILjava/lang/String;)V");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto screenshot_save_cleanup;
    }

    {
        size_t pixel_bytes = (size_t)width * (size_t)height * 4u;
        jbyteArray bytes = (*env)->NewByteArray(env, (jsize)pixel_bytes);
        if (!bytes || (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            goto screenshot_save_cleanup;
        }
        (*env)->SetByteArrayRegion(
            env, bytes, 0, (jsize)pixel_bytes, (const jbyte*)rgba);

        jstring jfilename = (*env)->NewStringUTF(env, filename);
        if (!jfilename || (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            (*env)->DeleteLocalRef(env, bytes);
            goto screenshot_save_cleanup;
        }

        if (!(*env)->ExceptionCheck(env)) {
            (*env)->CallStaticVoidMethod(
                env, cls, mid, g_android_app->activity->clazz, bytes,
                (jint)width, (jint)height, jfilename);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, jfilename);
        (*env)->DeleteLocalRef(env, bytes);
    }

screenshot_save_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

void android_jni_save_image_to_gallery(const char* filename) {
    if (!filename || !g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(
        env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto gallery_save_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "saveImageToGallery",
        "(Landroid/app/Activity;Ljava/lang/String;)V");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto gallery_save_cleanup;
    }

    {
        jstring jfilename = (*env)->NewStringUTF(env, filename);
        if (!jfilename || (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            goto gallery_save_cleanup;
        }

        if (!(*env)->ExceptionCheck(env)) {
            (*env)->CallStaticVoidMethod(
                env, cls, mid, g_android_app->activity->clazz, jfilename);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, jfilename);
    }

gallery_save_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

void android_jni_request_start_recording(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (cls && !(*env)->ExceptionCheck(env)) {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "requestStartRecording", "(Landroid/app/Activity;)V");
        if (mid && !(*env)->ExceptionCheck(env)) {
            (*env)->CallStaticVoidMethod(env, cls, mid,
                                         g_android_app->activity->clazz);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

void android_jni_request_stop_recording(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (cls && !(*env)->ExceptionCheck(env)) {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "requestStopRecording", "(Landroid/app/Activity;)V");
        if (mid && !(*env)->ExceptionCheck(env)) {
            (*env)->CallStaticVoidMethod(env, cls, mid,
                                         g_android_app->activity->clazz);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

bool android_jni_is_recording(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return false;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    bool result = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return false;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return false;
    }

    jclass cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (cls && !(*env)->ExceptionCheck(env)) {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "isRecording", "(Landroid/app/Activity;)Z");
        if (mid && !(*env)->ExceptionCheck(env)) {
            result = (*env)->CallStaticBooleanMethod(
                env, cls, mid, g_android_app->activity->clazz);
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            result = false;
        }
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return result;
}

android_recorder_event android_jni_poll_recorder_event(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return ANDROID_RECORDER_EVENT_NONE;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    int result = ANDROID_RECORDER_EVENT_NONE;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return ANDROID_RECORDER_EVENT_NONE;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return ANDROID_RECORDER_EVENT_NONE;
    }

    jclass cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (cls && !(*env)->ExceptionCheck(env)) {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "pollRecorderEvent", "(Landroid/app/Activity;)I");
        if (mid && !(*env)->ExceptionCheck(env)) {
            result = (*env)->CallStaticIntMethod(
                env, cls, mid, g_android_app->activity->clazz);
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            result = ANDROID_RECORDER_EVENT_NONE;
        }
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    if (result < ANDROID_RECORDER_EVENT_NONE ||
        result > ANDROID_RECORDER_EVENT_ERROR) {
        return ANDROID_RECORDER_EVENT_NONE;
    }
    return (android_recorder_event)result;
}

void android_jni_save_clip_to_gallery(const char* filename) {
    if (!filename || !g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = NULL;
    cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clip_gallery_cleanup;
    }

    {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "saveClipToGallery",
            "(Landroid/app/Activity;Ljava/lang/String;)V");
        if (!mid || (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            goto clip_gallery_cleanup;
        }

        jstring jfilename = (*env)->NewStringUTF(env, filename);
        if (!jfilename || (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            goto clip_gallery_cleanup;
        }

        if (!(*env)->ExceptionCheck(env)) {
            (*env)->CallStaticVoidMethod(
                env, cls, mid, g_android_app->activity->clazz, jfilename);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, jfilename);
    }

clip_gallery_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

void android_jni_play_clip(const char* filename) {
    if (!filename || !g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = NULL;
    cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto play_clip_cleanup;
    }

    {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "playClip",
            "(Landroid/app/Activity;Ljava/lang/String;)V");
        if (!mid || (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            goto play_clip_cleanup;
        }

        jstring jfilename = (*env)->NewStringUTF(env, filename);
        if (!jfilename || (*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            goto play_clip_cleanup;
        }

        if (!(*env)->ExceptionCheck(env)) {
            (*env)->CallStaticVoidMethod(
                env, cls, mid, g_android_app->activity->clazz, jfilename);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, jfilename);
    }

play_clip_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

/* ---- Lobby background picker (see GameActivity.kt) ---- */

void android_jni_request_pick_lobby_bg(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (cls && !(*env)->ExceptionCheck(env)) {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "requestPickLobbyBackground", "(Landroid/app/Activity;)V");
        if (mid && !(*env)->ExceptionCheck(env)) {
            (*env)->CallStaticVoidMethod(env, cls, mid,
                                         g_android_app->activity->clazz);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

int android_jni_poll_lobby_bg_event(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return 0;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    int result = 0;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return 0;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return 0;
    }

    jclass cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (cls && !(*env)->ExceptionCheck(env)) {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "pollLobbyBgEvent", "(Landroid/app/Activity;)I");
        if (mid && !(*env)->ExceptionCheck(env)) {
            result = (*env)->CallStaticIntMethod(
                env, cls, mid, g_android_app->activity->clazz);
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            result = 0;
        }
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return (result == 1 || result == 2) ? result : 0;
}

/* ---- Settings backup (Kotlin: GameActivity.requestBackupExport/Import,
   pollBackupEvent) ---- */
static void call_static_void_activity(const char* name) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (cls && !(*env)->ExceptionCheck(env)) {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, name, "(Landroid/app/Activity;)V");
        if (mid && !(*env)->ExceptionCheck(env)) {
            (*env)->CallStaticVoidMethod(env, cls, mid,
                                         g_android_app->activity->clazz);
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

void android_jni_backup_export(void) {
    call_static_void_activity("requestBackupExport");
}

void android_jni_backup_import(void) {
    call_static_void_activity("requestBackupImport");
}

android_backup_event android_jni_poll_backup_event(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return ANDROID_BACKUP_EVENT_NONE;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    int result = ANDROID_BACKUP_EVENT_NONE;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return ANDROID_BACKUP_EVENT_NONE;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return ANDROID_BACKUP_EVENT_NONE;
    }

    jclass cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (cls && !(*env)->ExceptionCheck(env)) {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "pollBackupEvent", "(Landroid/app/Activity;)I");
        if (mid && !(*env)->ExceptionCheck(env)) {
            result = (*env)->CallStaticIntMethod(
                env, cls, mid, g_android_app->activity->clazz);
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            result = ANDROID_BACKUP_EVENT_NONE;
        }
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return (result >= ANDROID_BACKUP_EVENT_EXPORTED &&
            result <= ANDROID_BACKUP_EVENT_ERROR)
               ? (android_backup_event)result
               : ANDROID_BACKUP_EVENT_NONE;
}

void android_jni_skinio_import(void) {
    call_static_void_activity("requestSkinImport");
}

void android_jni_skinio_export(void) {
    call_static_void_activity("requestSkinExport");
}

int android_jni_poll_skinio_event(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return 0;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    int result = 0;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return 0;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return 0;
    }

    jclass cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (cls && !(*env)->ExceptionCheck(env)) {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "pollSkinIoEvent", "(Landroid/app/Activity;)I");
        if (mid && !(*env)->ExceptionCheck(env)) {
            result = (*env)->CallStaticIntMethod(
                env, cls, mid, g_android_app->activity->clazz);
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            result = 0;
        }
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return (result >= 1 && result <= 4) ? result : 0;
}



/* ---- blocking HTTPS GET via java.net.HttpURLConnection ------------------ */
#define AJNI_HTTP_MAX (12 * 1024 * 1024)

static void ajni_http_err(char* err, size_t cap, const char* msg) {
    if (err && cap) snprintf(err, cap, "%s", msg);
}

int android_jni_http_get(const char* url, char** out_body, size_t* out_len,
                         char* err, size_t err_cap) {
    if (out_body) *out_body = NULL;
    if (out_len) *out_len = 0;
    ajni_http_err(err, err_cap, "");
    if (!url || !out_body || !out_len || !g_android_app ||
        !g_android_app->activity || !g_android_app->activity->vm) {
        ajni_http_err(err, err_cap, "no JVM");
        return -1;
    }
    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    int st = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (st == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            ajni_http_err(err, err_cap, "attach failed");
            return -1;
        }
        did_attach = true;
    } else if (st != JNI_OK || !env) {
        ajni_http_err(err, err_cap, "GetEnv failed");
        return -1;
    }

    int result = -1;
    char* buf = NULL;
    size_t len = 0, cap = 0;
    jclass url_c = NULL, hc = NULL, is_c = NULL;
    jobject u = NULL, conn = NULL, is = NULL;
    jbyteArray arr = NULL;

#define AJ_FAIL(msg) do { (*env)->ExceptionClear(env); \
    ajni_http_err(err, err_cap, msg); goto http_done; } while (0)
#define AJ_CHK(msg) do { if ((*env)->ExceptionCheck(env)) AJ_FAIL(msg); } while (0)

    url_c = (*env)->FindClass(env, "java/net/URL");
    hc = (*env)->FindClass(env, "java/net/HttpURLConnection");
    is_c = (*env)->FindClass(env, "java/io/InputStream");
    if (!url_c || !hc || !is_c) AJ_FAIL("java classes missing");

    {
        jstring js = (*env)->NewStringUTF(env, url);
        jmethodID ctor = (*env)->GetMethodID(env, url_c, "<init>", "(Ljava/lang/String;)V");
        u = (*env)->NewObject(env, url_c, ctor, js);
        (*env)->DeleteLocalRef(env, js);
        AJ_CHK("bad url");
        jmethodID open = (*env)->GetMethodID(env, url_c, "openConnection",
                                             "()Ljava/net/URLConnection;");
        conn = (*env)->CallObjectMethod(env, u, open);
        AJ_CHK("openConnection failed");
        if (!conn) AJ_FAIL("no connection");

        jmethodID set_ct = (*env)->GetMethodID(env, hc, "setConnectTimeout", "(I)V");
        jmethodID set_rt = (*env)->GetMethodID(env, hc, "setReadTimeout", "(I)V");
        jmethodID set_rp = (*env)->GetMethodID(env, hc, "setRequestProperty",
                                               "(Ljava/lang/String;Ljava/lang/String;)V");
        (*env)->CallVoidMethod(env, conn, set_ct, 10000);
        (*env)->CallVoidMethod(env, conn, set_rt, 10000);
        static const char* const props[][2] = {
            {"User-Agent", "Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 "
                           "(KHTML, like Gecko) Chrome/120.0 Mobile Safari/537.36"},
            {"Origin", "https://slither.io"},
            {"Referer", "https://slither.io/"},
            {"Accept", "*/*"},
        };
        for (int i = 0; i < 4; ++i) {
            jstring k = (*env)->NewStringUTF(env, props[i][0]);
            jstring v = (*env)->NewStringUTF(env, props[i][1]);
            (*env)->CallVoidMethod(env, conn, set_rp, k, v);
            (*env)->DeleteLocalRef(env, k);
            (*env)->DeleteLocalRef(env, v);
        }
        AJ_CHK("request setup failed");

        jmethodID get_code = (*env)->GetMethodID(env, hc, "getResponseCode", "()I");
        int code = (*env)->CallIntMethod(env, conn, get_code);
        AJ_CHK("no response (network/TLS error)");

        jmethodID get_is = (*env)->GetMethodID(
            env, hc, code >= 400 ? "getErrorStream" : "getInputStream",
            "()Ljava/io/InputStream;");
        is = (*env)->CallObjectMethod(env, conn, get_is);
        (*env)->ExceptionClear(env);
        result = code;
        if (is) {
            jmethodID rd = (*env)->GetMethodID(env, is_c, "read", "([B)I");
            arr = (*env)->NewByteArray(env, 16384);
            for (;;) {
                int n = (*env)->CallIntMethod(env, is, rd, arr);
                if ((*env)->ExceptionCheck(env)) {
                    (*env)->ExceptionClear(env);
                    if (len == 0) result = -1;
                    ajni_http_err(err, err_cap, "read failed");
                    break;
                }
                if (n <= 0) break;
                if (len + (size_t)n + 1 > cap) {
                    size_t nc = cap ? cap * 2 : 32768;
                    while (nc < len + (size_t)n + 1) nc *= 2;
                    if (nc > AJNI_HTTP_MAX + 1) { result = -1; ajni_http_err(err, err_cap, "reply too large"); break; }
                    char* nb = (char*)realloc(buf, nc);
                    if (!nb) { result = -1; break; }
                    buf = nb;
                    cap = nc;
                }
                (*env)->GetByteArrayRegion(env, arr, 0, n, (jbyte*)(buf + len));
                len += (size_t)n;
            }
            jmethodID cl = (*env)->GetMethodID(env, is_c, "close", "()V");
            (*env)->CallVoidMethod(env, is, cl);
            (*env)->ExceptionClear(env);
        }
        jmethodID disc = (*env)->GetMethodID(env, hc, "disconnect", "()V");
        (*env)->CallVoidMethod(env, conn, disc);
        (*env)->ExceptionClear(env);
    }

http_done:
    if (arr) (*env)->DeleteLocalRef(env, arr);
    if (is) (*env)->DeleteLocalRef(env, is);
    if (conn) (*env)->DeleteLocalRef(env, conn);
    if (u) (*env)->DeleteLocalRef(env, u);
    if (url_c) (*env)->DeleteLocalRef(env, url_c);
    if (hc) (*env)->DeleteLocalRef(env, hc);
    if (is_c) (*env)->DeleteLocalRef(env, is_c);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
#undef AJ_FAIL
#undef AJ_CHK
    if (result > 0) {
        if (!buf) buf = (char*)calloc(1, 1);
        if (buf) buf[len] = 0;
        *out_body = buf;
        *out_len = len;
    } else {
        free(buf);
    }
    return result;
}

#endif
