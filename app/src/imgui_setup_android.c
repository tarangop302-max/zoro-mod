#ifdef ANDROID

#include <time.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui/cimgui.h"
#include "imgui_setup.h"
#include "android_jni.h"
#include "user.h"
#include "game/profile_emoji.h"
#include "android_glfw_shim.h"

#include <android/asset_manager.h>
#include <android/keycodes.h>
#include <android/input.h>
#include <android_native_app_glue.h>
extern struct android_app* g_android_app;

static float g_android_imgui_scale = 1.0f;

static const char* android_imgui_get_clipboard(ImGuiContext* ctx) {
    (void)ctx;
    return android_jni_get_clipboard_text();
}

static void android_imgui_set_clipboard(ImGuiContext* ctx, const char* text) {
    (void)ctx;
    android_jni_set_clipboard_text(text ? text : "");
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}


static ImGuiKey android_ime_key_to_imgui(int keycode) {
    switch (keycode) {
        case AKEYCODE_TAB:         return ImGuiKey_Tab;
        case AKEYCODE_DPAD_LEFT:   return ImGuiKey_LeftArrow;
        case AKEYCODE_DPAD_RIGHT:  return ImGuiKey_RightArrow;
        case AKEYCODE_DPAD_UP:     return ImGuiKey_UpArrow;
        case AKEYCODE_DPAD_DOWN:   return ImGuiKey_DownArrow;
        case AKEYCODE_MOVE_HOME:   return ImGuiKey_Home;
        case AKEYCODE_MOVE_END:    return ImGuiKey_End;
        case AKEYCODE_FORWARD_DEL: return ImGuiKey_Delete;
        case AKEYCODE_DEL:         return ImGuiKey_Backspace;
        case AKEYCODE_ENTER:       return ImGuiKey_Enter;
        case AKEYCODE_ESCAPE:      return ImGuiKey_Escape;
        case AKEYCODE_CTRL_LEFT:   return ImGuiKey_LeftCtrl;
        case AKEYCODE_CTRL_RIGHT:  return ImGuiKey_RightCtrl;
        case AKEYCODE_SHIFT_LEFT:  return ImGuiKey_LeftShift;
        case AKEYCODE_SHIFT_RIGHT: return ImGuiKey_RightShift;
        case AKEYCODE_ALT_LEFT:    return ImGuiKey_LeftAlt;
        case AKEYCODE_ALT_RIGHT:   return ImGuiKey_RightAlt;
        case AKEYCODE_A:           return ImGuiKey_A;
        case AKEYCODE_C:           return ImGuiKey_C;
        case AKEYCODE_V:           return ImGuiKey_V;
        case AKEYCODE_X:           return ImGuiKey_X;
        case AKEYCODE_Y:           return ImGuiKey_Y;
        case AKEYCODE_Z:           return ImGuiKey_Z;
        default:                   return ImGuiKey_None;
    }
}

/* ---------------------------------------------------------------------------
 * Duplicate-character guard.
 *
 * A typed character can reach ImGui through two independent routes: the IME
 * bridge (commitText / sendKeyEvent in GameActivity.kt, drained below) AND the
 * native key-event handler (handle_input in twindow_android.c, which turns a
 * key press into a character itself). Many keyboards deliver digits and '.'
 * as real key events, so with both routes live every digit was inserted twice
 * ("148" became "114488").
 *
 * Both routes now funnel single characters through char_is_dup(): the same
 * character arriving from a DIFFERENT route within a few milliseconds is the
 * same keypress seen twice, so it is dropped. Two quick presses of the same
 * key come from the SAME route and are never dropped.
 * ------------------------------------------------------------------------- */
enum { CHAR_SRC_NONE = 0, CHAR_SRC_NATIVE = 1, CHAR_SRC_IME = 2 };

static bool char_is_dup(int src, unsigned int cp) {
    static int          s_last_src = CHAR_SRC_NONE;
    static unsigned int s_last_cp  = 0;
    static double       s_last_t   = 0.0;
    double now = monotonic_seconds();
    bool dup = s_last_src != CHAR_SRC_NONE && s_last_src != src &&
               s_last_cp == cp && (now - s_last_t) < 0.06;
    if (!dup) {
        s_last_src = src;
        s_last_cp  = cp;
        s_last_t   = now;
    }
    return dup;
}

/* Called by twindow_android.c for characters derived from native key events. */
void android_imgui_add_char(unsigned int cp) {
    if (cp == 0) return;
    if (char_is_dup(CHAR_SRC_NATIVE, cp)) return;
    ImGuiIO_AddInputCharacter(igGetIO_Nil(), cp);
}

/* If `s` is exactly ONE UTF-8 codepoint, store it in *cp and return true. */
static bool utf8_single_cp(const char* s, unsigned int* cp) {
    if (!s || !s[0]) return false;
    const unsigned char* u = (const unsigned char*)s;
    int len;
    unsigned int v;
    if (u[0] < 0x80)              { len = 1; v = u[0]; }
    else if ((u[0] >> 5) == 0x6)  { len = 2; v = u[0] & 0x1f; }
    else if ((u[0] >> 4) == 0xe)  { len = 3; v = u[0] & 0x0f; }
    else if ((u[0] >> 3) == 0x1e) { len = 4; v = u[0] & 0x07; }
    else return false;
    for (int i = 1; i < len; ++i) {
        if ((u[i] & 0xc0) != 0x80) return false;
        v = (v << 6) | (u[i] & 0x3f);
    }
    if (u[len] != '\0') return false; /* more than one codepoint */
    *cp = v;
    return true;
}

/* Called by twindow_android.c right before the Android keyboard is requested.
 * Hands the Java side what the focused ImGui text field holds, so the optional
 * Native text box (Settings) can open pre-filled with it. The text is only
 * READ here; edits come back as normal IME events. The ImGui state is checked
 * before use and ignored if it looks wrong, in which case the box simply
 * starts empty. */
void android_imgui_prepare_text_entry(void) {
    const char* text = "";
    int len = 0;
    int flags = 0;
    int max_len = 0;
    bool use_box = false;

    if (g_android_app && g_android_app->userData) {
        twindow* wnd = (twindow*)g_android_app->userData;
        if (wnd->env && wnd->env->usr)
            use_box = wnd->env->usr->usrs.native_text_box;
    }

    if (use_box) {
        ImGuiContext* g = igGetCurrentContext();
        if (g && g->ActiveId != 0 && g->InputTextState.ID == g->ActiveId) {
            const ImGuiInputTextState* st = &g->InputTextState;
            if (st->TextA.Data && st->TextLen > 0 && st->TextLen < 65536 &&
                st->TextLen <= st->TextA.Size) {
                text = st->TextA.Data;
                len = st->TextLen;
            }
            flags = (int)st->Flags;
            if (st->BufCapacity > 1 && st->BufCapacity < 1000000)
                max_len = st->BufCapacity - 1;
        }
    }
    android_jni_set_text_input_text(text, len, flags, max_len, use_box);
}

static void android_drain_ime_events(ImGuiIO* io) {
    android_ime_event event;
    while (android_jni_poll_ime_event(&event)) {
        unsigned int one_cp = 0;
        if (event.type == ANDROID_IME_EVENT_TEXT) {
            if (event.text && event.text[0] != '\0') {
                if (!(utf8_single_cp(event.text, &one_cp) &&
                      char_is_dup(CHAR_SRC_IME, one_cp)))
                    ImGuiIO_AddInputCharactersUTF8(io, event.text);
            }
        } else if (event.type == ANDROID_IME_EVENT_COMPOSITION) {
            for (int i = 0; i < event.replace_codepoints; ++i) {
                ImGuiIO_AddKeyEvent(io, ImGuiKey_Backspace, true);
                ImGuiIO_AddKeyEvent(io, ImGuiKey_Backspace, false);
            }
            if (event.text && event.text[0] != '\0') {
                /* Only a plain single character (no replacement of earlier
                   composing text) can be a duplicate of a key press. */
                if (!(event.replace_codepoints == 0 &&
                      utf8_single_cp(event.text, &one_cp) &&
                      char_is_dup(CHAR_SRC_IME, one_cp)))
                    ImGuiIO_AddInputCharactersUTF8(io, event.text);
            }
        } else if (event.type == ANDROID_IME_EVENT_KEY) {
            const bool down = event.action == AKEY_EVENT_ACTION_DOWN ||
                              event.action == AKEY_EVENT_ACTION_MULTIPLE;
            const bool ctrl = (event.meta_state & AMETA_CTRL_ON) != 0 ||
                ((event.keycode == AKEYCODE_CTRL_LEFT ||
                  event.keycode == AKEYCODE_CTRL_RIGHT) && down);
            const bool shift = (event.meta_state & AMETA_SHIFT_ON) != 0 ||
                ((event.keycode == AKEYCODE_SHIFT_LEFT ||
                  event.keycode == AKEYCODE_SHIFT_RIGHT) && down);
            const bool alt = (event.meta_state & AMETA_ALT_ON) != 0 ||
                ((event.keycode == AKEYCODE_ALT_LEFT ||
                  event.keycode == AKEYCODE_ALT_RIGHT) && down);

            ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, ctrl);
            ImGuiIO_AddKeyEvent(io, ImGuiMod_Shift, shift);
            ImGuiIO_AddKeyEvent(io, ImGuiMod_Alt, alt);

            ImGuiKey key = android_ime_key_to_imgui(event.keycode);
            if (key != ImGuiKey_None)
                ImGuiIO_AddKeyEvent(io, key, down);
        }
        android_jni_release_ime_event(&event);
    }
}


/* ---------------------------------------------------------------------------
 * Text-edit popup (Cut / Copy / Paste / Select all).
 *
 * Long-pressing inside an active text box used to fire an automatic paste
 * after 0.55 s, so text could never be selected (the clipboard was pasted again
 * every time you held your finger down). It now behaves like a normal Android
 * text field: a long press selects the word under the finger and shows a small
 * popup with Cut / Copy / Paste / Select all.
 *
 * The popup is drawn on the foreground draw list and hit-tested here instead of
 * being an ImGui window: tapping a real ImGui button would take focus away from
 * the text box and lose the selection before the action could run. Touches that
 * land on the popup are swallowed by android_text_menu_filter_touch(), which
 * twindow_android.c calls before forwarding any touch to ImGui.
 * ------------------------------------------------------------------------- */
enum { TM_CUT = 0, TM_COPY, TM_PASTE, TM_SELALL, TM_COUNT };
static const char* const k_tm_labels[TM_COUNT] = {"Cut", "Copy", "Paste", "Select all"};

static struct {
    bool   visible;
    bool   swallowing;     /* a touch that started on the popup is in progress */
    int    pressed;        /* item under that touch, -1 = none/background      */
    int    pending;        /* action to run next frame, -1 = none              */
    ImVec2 anchor;         /* where the long press happened                    */
    ImVec2 bg_min, bg_max; /* last drawn popup rectangle                       */
    ImVec2 item_min[TM_COUNT], item_max[TM_COUNT];
    bool   laid_out;
} s_tm = {.pressed = -1, .pending = -1};

static int tm_hit_item(float x, float y) {
    if (!s_tm.visible || !s_tm.laid_out) return -2; /* -2: popup not there */
    if (x < s_tm.bg_min.x || x > s_tm.bg_max.x ||
        y < s_tm.bg_min.y || y > s_tm.bg_max.y) return -2;
    for (int i = 0; i < TM_COUNT; ++i)
        if (x >= s_tm.item_min[i].x && x <= s_tm.item_max[i].x &&
            y >= s_tm.item_min[i].y && y <= s_tm.item_max[i].y) return i;
    return -1; /* on the popup, but between buttons */
}

bool android_text_menu_swallowing(void) { return s_tm.swallowing; }

/* Called for every UI touch event before it reaches ImGui.
   button: -1 move, 0 up, 1 down. Returns true when the popup consumed it. */
bool android_text_menu_filter_touch(float x, float y, int button) {
    if (!s_tm.visible) { s_tm.swallowing = false; return false; }
    if (button == 1) {
        int hit = tm_hit_item(x, y);
        if (hit == -2) { s_tm.visible = false; return false; } /* tap elsewhere */
        s_tm.swallowing = true;
        s_tm.pressed    = hit;
        return true;
    }
    if (!s_tm.swallowing) return false;
    if (button == -1) return true;
    /* up */
    int hit = tm_hit_item(x, y);
    if (hit >= 0 && hit == s_tm.pressed) {
        s_tm.pending = hit;
        s_tm.visible = false;
    }
    s_tm.swallowing = false;
    s_tm.pressed    = -1;
    return true;
}

static void tm_key_chord(ImGuiIO* io, ImGuiKey key) {
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, true);
    ImGuiIO_AddKeyEvent(io, key, true);
    ImGuiIO_AddKeyEvent(io, key, false);
    ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, false);
}

/* Runs the chosen action. The text box is still the active widget because the
   popup touch never reached ImGui, so ordinary shortcut keys do the work. */
static void tm_run_pending(ImGuiIO* io) {
    int a = s_tm.pending;
    if (a < 0) return;
    s_tm.pending = -1;
    switch (a) {
        case TM_CUT:    tm_key_chord(io, ImGuiKey_X); break;
        case TM_COPY:   tm_key_chord(io, ImGuiKey_C); break;
        case TM_PASTE:  android_jni_enqueue_clipboard_paste(); break;
        case TM_SELALL: tm_key_chord(io, ImGuiKey_A); break;
    }
}

/* Long press: select the word under the finger (a synthetic double click, which
   ImGui's InputText turns into "select word") and open the popup. */
static void tm_long_press(ImGuiIO* io, float x, float y) {
    ImGuiIO_AddMouseSourceEvent(io, ImGuiMouseSource_TouchScreen);
    ImGuiIO_AddMousePosEvent(io, x, y);
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    ImGuiIO_AddMouseButtonEvent(io, 0, true);
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    ImGuiIO_AddMouseButtonEvent(io, 0, true); /* finger is still down */
    s_tm.visible    = true;
    s_tm.laid_out   = false;
    s_tm.swallowing = false;
    s_tm.pressed    = -1;
    s_tm.anchor     = (ImVec2){x, y};
}

#define TM_COL(r, g, b, a) \
    ((ImU32)(((ImU32)(a) << 24) | ((ImU32)(b) << 16) | ((ImU32)(g) << 8) | (ImU32)(r)))

/* Draw + lay out the popup. Call right after igNewFrame(). */
static void tm_draw(ImGuiIO* io) {
    if (!s_tm.visible) { s_tm.laid_out = false; return; }
    if (!io->WantTextInput) { /* text box lost focus */
        s_tm.visible = s_tm.swallowing = false;
        s_tm.laid_out = false;
        return;
    }

    const float sc   = g_android_imgui_scale;
    const float padx = 18.0f * sc, pady = 14.0f * sc, gap = 2.0f * sc;
    float widths[TM_COUNT], total = 0.0f, text_h = 0.0f;
    for (int i = 0; i < TM_COUNT; ++i) {
        ImVec2 ts;
        igCalcTextSize(&ts, k_tm_labels[i], NULL, false, -1.0f);
        widths[i] = ts.x + padx * 2.0f;
        text_h    = ts.y;
        total    += widths[i] + (i ? gap : 0.0f);
    }
    const float h = text_h + pady * 2.0f;

    float x0 = s_tm.anchor.x - total * 0.5f;
    float y0 = s_tm.anchor.y - h - 56.0f * sc;       /* above the finger */
    if (y0 < 6.0f * sc) y0 = s_tm.anchor.y + 56.0f * sc; /* no room: below it */
    if (x0 + total > io->DisplaySize.x - 6.0f * sc) x0 = io->DisplaySize.x - total - 6.0f * sc;
    if (x0 < 6.0f * sc) x0 = 6.0f * sc;

    s_tm.bg_min = (ImVec2){x0, y0};
    s_tm.bg_max = (ImVec2){x0 + total, y0 + h};

    ImDrawList* dl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
    ImDrawList_AddRectFilled(dl,
        (ImVec2){x0 - 2.0f, y0 - 2.0f}, (ImVec2){x0 + total + 2.0f, y0 + h + 2.0f},
        TM_COL(0, 0, 0, 255), 8.0f * sc, 0);

    float x = x0;
    for (int i = 0; i < TM_COUNT; ++i) {
        s_tm.item_min[i] = (ImVec2){x, y0};
        s_tm.item_max[i] = (ImVec2){x + widths[i], y0 + h};
        const bool down = s_tm.swallowing && s_tm.pressed == i;
        ImDrawList_AddRectFilled(dl, s_tm.item_min[i], s_tm.item_max[i],
            down ? TM_COL(90, 90, 90, 255) : TM_COL(48, 48, 48, 255),
            i == 0 ? 8.0f * sc : (i == TM_COUNT - 1 ? 8.0f * sc : 0.0f),
            i == 0 ? ImDrawFlags_RoundCornersLeft
                   : (i == TM_COUNT - 1 ? ImDrawFlags_RoundCornersRight
                                        : ImDrawFlags_RoundCornersNone));
        ImDrawList_AddText_Vec2(dl, (ImVec2){x + padx, y0 + pady},
                                TM_COL(235, 235, 235, 255), k_tm_labels[i], NULL);
        x += widths[i] + gap;
    }
    s_tm.laid_out = true;
    /* keep gameplay controls from also reacting to touches on the popup */
    android_ui_capture_rect(x0 - 2.0f, y0 - 2.0f, x0 + total + 2.0f, y0 + h + 2.0f);
}

void imgui_init(tenv* env) {
    tuser_data*   usr  = env->usr;
    user_settings* usrs = &usr->usrs;

    igCreateContext(NULL);
    igImplVulkan_Init(&(ImGui_ImplVulkan_InitInfo){
        .ApiVersion       = VK_API_VERSION_1_0,
        .Instance         = env->ctx->instance,
        .PhysicalDevice   = env->ctx->ph_device,
        .Device           = env->ctx->device,
        .QueueFamily      = env->ctx->queue_family,
        .Queue            = env->ctx->queue,
        .DescriptorPool   = env->ctx->descriptor_pool,
        .MinImageCount    = env->ctx->min_image_count,
        .ImageCount       = env->ctx->fif,
        .PipelineInfoMain = {
            .RenderPass  = env->ctx->renderpass,
            .Subpass     = 0,
            .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        },
        .UseDynamicRendering = false,
    });

    ImGuiIO* io = igGetIO_Nil();
    io->DisplaySize = (ImVec2){(float)env->ctx->size[0],
                               (float)env->ctx->size[1]};

    AAssetManager* am = g_android_app->activity->assetManager;

    /* Resolution-aware UI density. Use the shorter edge so ultrawide phones
       do not receive oversized controls, while low-resolution devices still
       get a compact layout that fits without clipping. */
    float short_edge = (float)(env->ctx->size[0] < env->ctx->size[1]
                                   ? env->ctx->size[0] : env->ctx->size[1]);
    float ui_scale = short_edge / 720.0f;
    if (ui_scale < 0.75f) ui_scale = 0.75f;
    if (ui_scale > 1.30f) ui_scale = 1.30f;
    g_android_imgui_scale = ui_scale;

    static const ImWchar icon_ranges[] = {0xe900, 0xeaea, 0};

    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        float size = (20.0f + i * 4.0f) * ui_scale;

        ImFontConfig icons_cfg = {
            .FontDataOwnedByAtlas = true,
            .OversampleH          = 0,
            .OversampleV          = 0,
            .GlyphMaxAdvanceX     = FLT_MAX,
            .RasterizerDensity    = 1,
            .RasterizerMultiply   = 1,
            .EllipsisChar         = 0,
            .MergeMode            = true,
            .GlyphOffset          = (ImVec2){0, (2.0f + i) * ui_scale},
            .GlyphMinAdvanceX     = (26.0f + i * 6.0f) * ui_scale,
        };

#define LOAD_FONT(path, sz, cfg, ranges) do { \
    AAsset* _a = AAssetManager_open(am, path, AASSET_MODE_BUFFER); \
    if (_a) { \
        off_t _len = AAsset_getLength(_a); \
        void* _buf = malloc(_len); \
        AAsset_read(_a, _buf, _len); \
        AAsset_close(_a); \
        ImFontAtlas_AddFontFromMemoryTTF(io->Fonts, _buf, (int)_len, sz, cfg, ranges); \
    } \
} while(0)

#define LOAD_FONT_RET(out, path, sz, cfg, ranges) do { \
    AAsset* _a = AAssetManager_open(am, path, AASSET_MODE_BUFFER); \
    (out) = NULL; \
    if (_a) { \
        off_t _len = AAsset_getLength(_a); \
        void* _buf = malloc(_len); \
        AAsset_read(_a, _buf, _len); \
        AAsset_close(_a); \
        (out) = ImFontAtlas_AddFontFromMemoryTTF(io->Fonts, _buf, (int)_len, sz, cfg, ranges); \
    } \
} while(0)

        LOAD_FONT_RET(usr->imgui_data.mono_font[i],
            "fonts/mono_regular.ttf", size, NULL, PROFILE_EMOJI_GLYPH_RANGES);
        LOAD_FONT("fonts/iconfont.ttf", size, &icons_cfg, icon_ranges);

        LOAD_FONT_RET(usr->imgui_data.regular_font[i],
            "fonts/regular_regular.ttf", size, NULL, PROFILE_EMOJI_GLYPH_RANGES);
        LOAD_FONT("fonts/iconfont.ttf", size, &icons_cfg, icon_ranges);

        LOAD_FONT_RET(usr->imgui_data.mono_font_bold[i],
            "fonts/mono_bold.ttf", size, NULL, PROFILE_EMOJI_GLYPH_RANGES);
        LOAD_FONT("fonts/iconfont.ttf", size, &icons_cfg, icon_ranges);

        LOAD_FONT_RET(usr->imgui_data.regular_font_bold[i],
            "fonts/regular_bold.ttf", size, NULL, PROFILE_EMOJI_GLYPH_RANGES);
        LOAD_FONT("fonts/iconfont.ttf", size, &icons_cfg, icon_ranges);

#undef LOAD_FONT
#undef LOAD_FONT_RET
    }

    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io->IniFilename  = NULL;

    /* Use Android's real system clipboard instead of Dear ImGui's private
       fallback clipboard. This enables copy/paste in every InputText widget. */
    ImGuiPlatformIO* platform_io = igGetPlatformIO_Nil();
    platform_io->Platform_GetClipboardTextFn = android_imgui_get_clipboard;
    platform_io->Platform_SetClipboardTextFn = android_imgui_set_clipboard;

    ImGuiStyle* style = igGetStyle();
    style->DockingNodeHasCloseButton        = false;
    style->WindowMenuButtonPosition         = ImGuiDir_None;
    style->TabCloseButtonMinWidthUnselected = -1;
    style->WindowBorderSize = style->FrameBorderSize =
    style->ChildBorderSize  = style->PopupBorderSize =
    style->TabBorderSize    = 1;
    style->FramePadding     = (ImVec2){8.0f * ui_scale, 8.0f * ui_scale};
    style->ItemSpacing      = (ImVec2){4.0f * ui_scale, 4.0f * ui_scale};
    style->ItemInnerSpacing = (ImVec2){4.0f * ui_scale, 4.0f * ui_scale};
    style->WindowPadding    = (ImVec2){4.0f * ui_scale, 4.0f * ui_scale};
    style->GrabMinSize      = 18.0f * ui_scale;
    style->FrameRounding = style->TabRounding = style->ChildRounding =
    style->GrabRounding  = style->PopupRounding =
    style->ScrollbarRounding = style->WindowRounding =
    style->TreeLinesRounding = 3.0f * ui_scale;
    style->ScrollbarSize    = 18.0f * ui_scale;
    style->DockingSeparatorSize = 1;
    style->ScrollbarPadding = 1.0f * ui_scale;
    style->CellPadding.x    = 2.0f * ui_scale;
    style->TouchExtraPadding = (ImVec2){4.0f * ui_scale, 4.0f * ui_scale};

    igStyleColorsDark(style);
    style->Colors[ImGuiCol_Text]       = (ImVec4){0.89f, 0.89f, 0.89f, 1.00f};
    style->Colors[ImGuiCol_WindowBg]   =
    style->Colors[ImGuiCol_PopupBg]    = (ImVec4){0.20f, 0.20f, 0.20f, 1.00f};
    style->Colors[ImGuiCol_Border]     = (ImVec4){0.00f, 0.00f, 0.00f, 1.00f};
    style->Colors[ImGuiCol_FrameBg]    = (ImVec4){0.16f, 0.16f, 0.16f, 1.00f};
    style->Colors[ImGuiCol_TitleBgActive] = (ImVec4){0.12f, 0.12f, 0.12f, 1.00f};
    style->Colors[ImGuiCol_Button]     = (ImVec4){0.25f, 0.25f, 0.25f, 1.00f};
    style->Colors[ImGuiCol_ButtonHovered] = (ImVec4){0.31f, 0.31f, 0.31f, 1.00f};
    style->Colors[ImGuiCol_Header]     = (ImVec4){0.25f, 0.25f, 0.25f, 1.00f};
    style->Colors[ImGuiCol_Tab]        = (ImVec4){0.25f, 0.25f, 0.25f, 1.00f};
    style->Colors[ImGuiCol_TabSelected] = (ImVec4){0.31f, 0.31f, 0.31f, 1.00f};
    style->Colors[ImGuiCol_ModalWindowDimBg] = (ImVec4){0, 0, 0, 0.7f};
    style->Colors[ImGuiCol_FrameBgHovered]   = (ImVec4){0.23f, 0.23f, 0.23f, 1.00f};
    style->Colors[ImGuiCol_FrameBgActive]    = (ImVec4){0.12f, 0.12f, 0.12f, 1.00f};
    style->Colors[ImGuiCol_SliderGrab]       = (ImVec4){0.31f, 0.31f, 0.31f, 1.00f};
    style->Colors[ImGuiCol_ButtonActive]     = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
}

bool g_imgui_wants_keyboard = false;
/* True for one frame when the player tapped and a text box is (still) focused
   afterwards -- i.e. they tapped a text field. twindow_android.c uses it to
   ask Android for the keyboard again, because the keyboard may have been
   dismissed (Back / hide arrow) while the field kept focus, in which case
   g_imgui_wants_keyboard never changes and nothing else would reopen it. */
bool g_imgui_keyboard_retap = false;

void imgui_prerender(void) {
    igImplVulkan_NewFrame();

    ImGuiIO* io = igGetIO_Nil();
    android_drain_ime_events(io);

    static struct timespec _last_time = {0, 0};
    struct timespec _now;
    clock_gettime(CLOCK_MONOTONIC, &_now);
    if (_last_time.tv_sec == 0 && _last_time.tv_nsec == 0) {
        io->DeltaTime = 1.0f / 60.0f;
    } else {
        double dt = (_now.tv_sec  - _last_time.tv_sec)
                  + (_now.tv_nsec - _last_time.tv_nsec) * 1e-9;
        io->DeltaTime = (dt > 0.0) ? (float)dt : 1.0f / 60.0f;
    }
    _last_time = _now;

    if (g_android_app->userData) {
        twindow* wnd = (twindow*)g_android_app->userData;
        /* Mouse position/buttons now arrive as queued ImGui events from
           twindow_android.c (see imgui_touch_event), so quick taps register. */
        if (wnd->size[0] > 0 && wnd->size[1] > 0)
            io->DisplaySize = (ImVec2){(float)wnd->size[0], (float)wnd->size[1]};

        extern bool g_panel_open;
        static float s_scroll_last_y   = 0.0f;
        static bool  s_scroll_was_down = false;
        if (g_panel_open) {
            bool down_now = wnd->ui_touch.down;
            if (down_now && s_scroll_was_down) {
                float dy = wnd->ui_touch.y - s_scroll_last_y;

                if (dy < -2.0f || dy > 2.0f)
                    ImGuiIO_AddMouseWheelEvent(io, 0.0f, dy / 30.0f);
            }
            s_scroll_was_down = down_now;
            s_scroll_last_y   = down_now ? wnd->ui_touch.y : 0.0f;
        } else {
            s_scroll_was_down = false;
        }

        /* Text, keyboard clipboard suggestions, composition and edit keys
           arrive through the native Android InputConnection. A stationary
           long press inside a text box selects the word and opens the
           Cut / Copy / Paste / Select all popup (see tm_long_press). */
        static bool   s_lp_tracking = false;
        static bool   s_lp_fired    = false;
        static ImVec2 s_lp_start    = {0.0f, 0.0f};
        static double s_lp_start_t  = 0.0;

        tm_run_pending(io);

        if (!wnd->ui_touch.down || !io->WantTextInput || s_tm.swallowing) {
            s_lp_tracking = false;
            s_lp_fired    = false;
        } else if (!s_lp_tracking) {
            s_lp_tracking = true;
            s_lp_fired    = false;
            s_lp_start    = (ImVec2){wnd->ui_touch.x, wnd->ui_touch.y};
            s_lp_start_t  = monotonic_seconds();
        } else {
            float dx = wnd->ui_touch.x - s_lp_start.x;
            float dy = wnd->ui_touch.y - s_lp_start.y;
            float cancel_distance = 18.0f * g_android_imgui_scale;
            if (dx * dx + dy * dy > cancel_distance * cancel_distance) {
                /* finger moved: it is a drag-select, not a long press */
                s_lp_tracking = false;
                s_lp_fired    = true;
            } else if (!s_lp_fired &&
                       monotonic_seconds() - s_lp_start_t >= 0.45) {
                tm_long_press(io, wnd->ui_touch.x, wnd->ui_touch.y);
                s_lp_fired = true;
            }
        }

        g_imgui_wants_keyboard = io->WantTextInput;

        /* ImGui updates WantTextInput one frame after the click that changed
           it, so remember that a tap ended last frame and look at the flag
           now: still true means the tap landed on / kept a text field. */
        static bool s_tap_last_frame = false;
        g_imgui_keyboard_retap = s_tap_last_frame && io->WantTextInput;
        s_tap_last_frame = wnd->ui_touch.just_up;
    }

    igNewFrame();
    tm_draw(igGetIO_Nil());
}

void imgui_render(VkCommandBuffer cmd) {
    igImplVulkan_RenderDrawData(igGetDrawData(), cmd, NULL);
}

void imgui_destroy(void) {
    igImplVulkan_Shutdown();
    igDestroyContext(NULL);
}

#endif
