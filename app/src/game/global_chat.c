#include "global_chat.h"

#include "../user.h"
#include "profile_emoji.h"

#include "thermite/tchat.h"
#include "thermite/jsr_network.h"
#include "ntl_team.h"

#ifdef ANDROID
#include "../android_glfw_shim.h"
#include "../android_jni.h"
#include "../android_path.h"
#endif

#include <stdio.h>

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "../network/server.h"
#include "screenshot.h"
#include <math.h>
#include <time.h>

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)))
#endif

#define GLOBAL_CHAT_MAX_MESSAGES 50
#define GLOBAL_CHAT_NAME_LEN 32
#define GLOBAL_CHAT_TEXT_LEN 160

/*
 * Fixed 8-char alphanumeric "room key" shared by every
 * player. This lets us reuse the existing team-based JSR
 * relay protocol for a single public room that nobody has
 * to type a key to join.
 */
#define GLOBAL_CHAT_ROOM_KEY "GLOBAL01"

typedef struct {
    char name[GLOBAL_CHAT_NAME_LEN];
    char owner[GLOBAL_CHAT_NAME_LEN];
    char text[GLOBAL_CHAT_TEXT_LEN];
    char time_str[9]; /* "HH:MM:SS\0" */
    unsigned long seq; /* unread sequence number; 0 = our own message */
} global_chat_message;

static bool global_chat_initialized = false;
static bool global_chat_open = false;

/* TEAM CHAT panel state (side-rail tab, close request, panel opacity,
 * unread counter, and the teammate highlighted by LOCATE). */
typedef enum {
    GC_TAB_CHAT = 0,
    GC_TAB_PLAYERS,
    GC_TAB_SOS,
    GC_TAB_SETTINGS
} gc_tab_id;

static int gc_tab = GC_TAB_CHAT;
static bool gc_close_requested = false;
static float gc_panel_alpha = 0.82f;
static unsigned long gc_total_messages = 0;
static unsigned long gc_seen_messages = 0;
static char gc_locate_name[32] = "";

static const char* const gc_tab_ids[4] = {
    "##gc_tab0", "##gc_tab1", "##gc_tab2", "##gc_tab3"
};

/* Epoch milliseconds our own SOS signal is active until; 0 (or any value
 * <= now) means inactive. Session-only -- deliberately never saved to
 * user_settings, so a restart never leaves a stale SOS broadcasting. */
static long long global_chat_sos_until_ms = 0;

static long long global_chat_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void global_chat_set_sos(long long until_ms) {
    global_chat_sos_until_ms = until_ms;
}

bool global_chat_is_sos_active(void) {
    return global_chat_sos_until_ms > global_chat_now_ms();
}

long long global_chat_sos_remaining_ms(void) {
    long long remaining = global_chat_sos_until_ms - global_chat_now_ms();
    return remaining > 0 ? remaining : 0;
}

/* Drag/resize adjust mode -- see global_chat_set_adjust_mode(). */
static global_chat_adjust_mode global_chat_adjust = GLOBAL_CHAT_ADJUST_NONE;

/* Live-tracked rect (relative to viewport work area) while a mode
 * is active, so whichever value was current the instant the player
 * confirms is what gets saved. */
static float global_chat_adjust_x = 0.0f;
static float global_chat_adjust_y = 0.0f;
static float global_chat_adjust_w = 0.0f;
static float global_chat_adjust_h = 0.0f;

/* Networking state for the public/global room. */
static tchat_system* global_chat_tchat = NULL;
static jsr_network* global_chat_net = NULL;

static global_chat_message
    global_chat_messages[GLOBAL_CHAT_MAX_MESSAGES];

static int global_chat_message_count = 0;

/* ---------------------------------------------------------------------
 * Notices (system messages, joined / left, errors). These never go into
 * the chat feed: the newest one pops up as a short toast under the panel
 * header, and the last GC_MAX_EVENTS are kept in the Players tab under
 * "Activity".
 * ------------------------------------------------------------------- */
#define GC_MAX_EVENTS 30
#define GC_TOAST_MS 4000LL
#define GC_TOAST_FADE_MS 600LL

typedef enum {
    GC_EV_SYSTEM = 0,
    GC_EV_JOIN,
    GC_EV_LEAVE,
    GC_EV_RECONNECT
} gc_event_kind;

typedef struct {
    char name[GLOBAL_CHAT_NAME_LEN];
    char text[GLOBAL_CHAT_TEXT_LEN > 96 ? 96 : GLOBAL_CHAT_TEXT_LEN];
    char time_str[9];
    int kind;
} gc_event;

static gc_event gc_events[GC_MAX_EVENTS];
static int gc_event_count = 0;
static long long gc_toast_until_ms = 0;

/* Show "joined / left" and system notices (toast). */
static bool gc_show_events = true;

/* Set while our own just-typed message is added, so it is not counted as
 * unread. */
static bool gc_adding_own = false;

/* Discord-style "NEW MESSAGES" divider: set when the Chat tab is opened
 * with unread messages, placed before the first message whose seq is
 * greater than gc_divider_after, and cleared as soon as the player leaves
 * the Chat tab or collapses the panel. */
static bool gc_chat_viewing = false;
static bool gc_divider_active = false;
static bool gc_divider_scroll = false;
static unsigned long gc_divider_after = 0;

static char global_chat_input[GLOBAL_CHAT_TEXT_LEN] = "";

static void global_chat_panel_contents(
    tenv* env,
    ImVec2 wp,
    ImVec2 ws
);

/* =====================================================================
 * Quick messages: one-tap buttons in the side rail. The label (shown on
 * the button) and the text (sent to chat) of each of the 4 slots can be
 * edited in the Settings tab. Stored in their own small text file so the
 * main settings file layout is untouched.
 * ===================================================================== */
#define GC_QM_COUNT 4
#define GC_QM_LABEL_LEN 7
#define GC_QM_TEXT_LEN 80

/* Wire tag so every teammate's client knows a message is a quick message
 * (and which slot's sound to play) no matter what text each of them has
 * set. It is a 6-byte prefix of invisible Unicode characters: U+2063
 * followed by one of 4 zero-width characters (one per slot). The text
 * after it is the normal chat text; our own client strips the tag before
 * showing the message. */
#define GC_QM_TAG_LEN 6
static const char gc_qm_tag[GC_QM_COUNT][GC_QM_TAG_LEN + 1] = {
    "\xE2\x81\xA3\xE2\x80\x8B",
    "\xE2\x81\xA3\xE2\x80\x8C",
    "\xE2\x81\xA3\xE2\x80\x8D",
    "\xE2\x81\xA3\xE2\x81\xA0"
};

static char gc_qm_label[GC_QM_COUNT][GC_QM_LABEL_LEN];
static char gc_qm_text[GC_QM_COUNT][GC_QM_TEXT_LEN];
static bool gc_qm_sound = true;
/* SOS beep on/off (saved in the same quick_msgs.txt as "sos_sound="). */
static bool gc_sos_sound = true;
static bool gc_qm_loaded = false;

static void gc_qm_defaults(void) {
    static const char* const def_label[GC_QM_COUNT] = {
        "HELP", "GG", "JOIN", "NICE"
    };
    static const char* const def_text[GC_QM_COUNT] = {
        "Help me!", "GG", "Join me!", "Nice!"
    };

    for (int i = 0; i < GC_QM_COUNT; i++) {
        snprintf(gc_qm_label[i], GC_QM_LABEL_LEN, "%s", def_label[i]);
        snprintf(gc_qm_text[i], GC_QM_TEXT_LEN, "%s", def_text[i]);
    }

    gc_qm_sound = true;
}

static void gc_qm_path(char* out, int out_size) {
#ifdef ANDROID
    android_build_path(out, out_size, "quick_msgs.txt");
#else
    snprintf(out, (size_t)out_size, "%s", "quick_msgs.txt");
#endif
}

static void gc_qm_save(void) {
    char path[600];
    gc_qm_path(path, (int)sizeof(path));

    FILE* f = fopen(path, "w");

    if (f == NULL) {
        return;
    }

    fprintf(f, "sound=%d\n", gc_qm_sound ? 1 : 0);
    fprintf(f, "sos_sound=%d\n", gc_sos_sound ? 1 : 0);

    for (int i = 0; i < GC_QM_COUNT; i++) {
        char l[GC_QM_LABEL_LEN];
        char t[GC_QM_TEXT_LEN];

        snprintf(l, sizeof(l), "%s", gc_qm_label[i]);
        snprintf(t, sizeof(t), "%s", gc_qm_text[i]);

        /* The file is line/'|' based, so keep those out of the data. */
        for (char* c = l; *c; c++) {
            if (*c == '|' || *c == '\n' || *c == '\r') *c = ' ';
        }
        for (char* c = t; *c; c++) {
            if (*c == '|' || *c == '\n' || *c == '\r') *c = ' ';
        }

        fprintf(f, "%s|%s\n", l, t);
    }

    fclose(f);
}

static void gc_qm_ensure_loaded(void) {
    if (gc_qm_loaded) {
        return;
    }

    gc_qm_loaded = true;
    gc_qm_defaults();

    char path[600];
    gc_qm_path(path, (int)sizeof(path));

    FILE* f = fopen(path, "r");

    if (f == NULL) {
        return;
    }

    char line[256];
    int slot = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        size_t n = strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }

        if (strncmp(line, "sound=", 6) == 0) {
            gc_qm_sound = line[6] != '0';
            continue;
        }

        if (strncmp(line, "sos_sound=", 10) == 0) {
            gc_sos_sound = line[10] != '0';
            continue;
        }

        if (slot >= GC_QM_COUNT) {
            break;
        }

        char* bar = strchr(line, '|');

        if (bar == NULL) {
            continue;
        }

        *bar = '\0';

        snprintf(gc_qm_label[slot], GC_QM_LABEL_LEN, "%s", line);
        snprintf(gc_qm_text[slot], GC_QM_TEXT_LEN, "%s", bar + 1);

        slot++;
    }

    fclose(f);
}

static void gc_play_quick_sound(int id) {
#ifdef ANDROID
    android_jni_play_quick_sound(id);
#else
    (void)id;
#endif
}

/* SOS beep. kind 0 = urgent alert (a teammate just sent SOS),
 * kind 1 = short confirmation (we sent / are testing our own). */
static void gc_play_sos_beep(int kind) {
#ifdef ANDROID
    android_jni_play_sos_beep(kind);
#else
    (void)kind;
#endif
}

static void gc_sos_alert_tick(const char* own_name);

/* =====================================================================
 * TEAM CHAT "Tabbed Hub" look -- small drawing helpers.
 * Everything below only changes how the chat is drawn; the relay,
 * messages, SOS, emoji and position/size logic are unchanged.
 * ===================================================================== */
static ImVec2 gc_v2(float x, float y) {
    ImVec2 v;
    v.x = x;
    v.y = y;
    return v;
}

static ImVec2 gc_add(ImVec2 a, float x, float y) {
    return gc_v2(a.x + x, a.y + y);
}

static float gc_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float gc_text_w(const char* s) {
    ImVec2 sz;
    igCalcTextSize(&sz, s, NULL, false, -1.0f);
    return sz.x;
}

static void gc_text_center(
    ImDrawList* dl,
    ImVec2 mn,
    ImVec2 sz,
    ImU32 col,
    const char* s
) {
    float w = gc_text_w(s);
    float h = igGetTextLineHeight();

    ImDrawList_AddText_Vec2(
        dl,
        gc_v2(
            mn.x + (sz.x - w) * 0.5f,
            mn.y + (sz.y - h) * 0.5f
        ),
        col,
        s,
        NULL
    );
}

/* Rounded flat button: invisible hit area + custom fill. The caller
 * draws the label/icon on top afterwards. */
static bool gc_button(
    const char* id,
    ImVec2 mn,
    ImVec2 sz,
    ImU32 col,
    ImU32 col_hot,
    float rounding,
    ImU32 border
) {
    igSetCursorScreenPos(mn);

    bool clicked =
        igInvisibleButton(id, sz, ImGuiButtonFlags_None);

    bool hot = igIsItemHovered(0) || igIsItemActive();

    ImDrawList* dl = igGetWindowDrawList();

    ImDrawList_AddRectFilled(
        dl,
        mn,
        gc_add(mn, sz.x, sz.y),
        hot ? col_hot : col,
        rounding,
        0
    );

    if (border != 0) {
        ImDrawList_AddRect(
            dl,
            mn,
            gc_add(mn, sz.x, sz.y),
            border,
            rounding,
            0,
            1.0f
        );
    }

    return clicked;
}

/* ---- vector icons (the game font has no emoji glyphs) ---- */
static void gc_icon_chat(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    ImDrawList_AddRectFilled(
        dl,
        gc_v2(c.x - s * 0.5f, c.y - s * 0.42f),
        gc_v2(c.x + s * 0.5f, c.y + s * 0.2f),
        col,
        s * 0.18f,
        0
    );
    ImDrawList_AddTriangleFilled(
        dl,
        gc_v2(c.x - s * 0.22f, c.y + s * 0.15f),
        gc_v2(c.x - s * 0.30f, c.y + s * 0.5f),
        gc_v2(c.x + s * 0.08f, c.y + s * 0.15f),
        col
    );
}

static void gc_icon_players(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    ImDrawList_AddCircleFilled(
        dl, gc_v2(c.x, c.y - s * 0.18f), s * 0.2f, col, 16
    );
    ImDrawList_AddRectFilled(
        dl,
        gc_v2(c.x - s * 0.38f, c.y + s * 0.08f),
        gc_v2(c.x + s * 0.38f, c.y + s * 0.46f),
        col,
        s * 0.3f,
        ImDrawFlags_RoundCornersTop
    );
}

static void gc_icon_sos(ImDrawList* dl, ImVec2 c, float s, ImU32 col, ImU32 cut) {
    ImDrawList_AddTriangleFilled(
        dl,
        gc_v2(c.x, c.y - s * 0.46f),
        gc_v2(c.x - s * 0.52f, c.y + s * 0.4f),
        gc_v2(c.x + s * 0.52f, c.y + s * 0.4f),
        col
    );
    ImDrawList_AddLine(
        dl,
        gc_v2(c.x, c.y - s * 0.12f),
        gc_v2(c.x, c.y + s * 0.1f),
        cut,
        s * 0.1f
    );
    ImDrawList_AddCircleFilled(
        dl, gc_v2(c.x, c.y + s * 0.26f), s * 0.055f, cut, 8
    );
}

static void gc_icon_gear(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    ImDrawList_AddCircle(dl, c, s * 0.26f, col, 20, s * 0.2f);

    for (int i = 0; i < 8; i++) {
        float a = (float)i * 0.785398f;
        float ca = cosf(a);
        float sa = sinf(a);

        ImDrawList_AddLine(
            dl,
            gc_v2(c.x + ca * s * 0.32f, c.y + sa * s * 0.32f),
            gc_v2(c.x + ca * s * 0.47f, c.y + sa * s * 0.47f),
            col,
            s * 0.13f
        );
    }
}

static void gc_icon_x(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    ImDrawList_AddLine(
        dl,
        gc_v2(c.x - s * 0.5f, c.y - s * 0.5f),
        gc_v2(c.x + s * 0.5f, c.y + s * 0.5f),
        col,
        2.0f
    );
    ImDrawList_AddLine(
        dl,
        gc_v2(c.x + s * 0.5f, c.y - s * 0.5f),
        gc_v2(c.x - s * 0.5f, c.y + s * 0.5f),
        col,
        2.0f
    );
}

static void gc_icon_send(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    ImDrawList_AddTriangleFilled(
        dl,
        gc_v2(c.x - s * 0.45f, c.y - s * 0.5f),
        gc_v2(c.x + s * 0.55f, c.y),
        gc_v2(c.x - s * 0.45f, c.y + s * 0.5f),
        col
    );
}

static void global_chat_try_connect(
    tenv* env
);

static void gc_push_event(const char* name, const char* text, int kind) {
    long long now_ms = global_chat_now_ms();
    gc_event* e;

    /* "left" right before "joined" by the same player = a reconnect blip:
     * fold the two into one "reconnected" entry. */
    if (
        kind == GC_EV_JOIN &&
        gc_event_count > 0 &&
        gc_events[gc_event_count - 1].kind == GC_EV_LEAVE &&
        strcmp(gc_events[gc_event_count - 1].name, name) == 0
    ) {
        e = &gc_events[gc_event_count - 1];
        e->kind = GC_EV_RECONNECT;
    } else {
        if (gc_event_count >= GC_MAX_EVENTS) {
            memmove(
                &gc_events[0], &gc_events[1],
                sizeof(gc_event) * (GC_MAX_EVENTS - 1)
            );
            gc_event_count = GC_MAX_EVENTS - 1;
        }

        e = &gc_events[gc_event_count++];
        e->kind = kind;
    }

    snprintf(e->name, sizeof(e->name), "%s", name);

    if (e->kind == GC_EV_JOIN) {
        snprintf(e->text, sizeof(e->text), "%s joined", name);
    } else if (e->kind == GC_EV_LEAVE) {
        snprintf(e->text, sizeof(e->text), "%s left", name);
    } else if (e->kind == GC_EV_RECONNECT) {
        snprintf(e->text, sizeof(e->text), "%s reconnected", name);
    } else {
        snprintf(e->text, sizeof(e->text), "%s", text);
    }

    time_t now = time(NULL);
    struct tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    strftime(e->time_str, sizeof(e->time_str), "%H:%M", &local_tm);

    gc_toast_until_ms = now_ms + GC_TOAST_MS;
}

static void global_chat_add_message(
    const char* name,
    const char* owner,
    const char* text
) {
    if (
        name == NULL ||
        text == NULL ||
        text[0] == '\0'
    ) {
        return;
    }

    {
        bool ev_system =
            strcmp(name, "[SYSTEM]") == 0 ||
            strcmp(name, "ZORO") == 0;
        bool ev_join = strcmp(text, "joined public chat") == 0;
        bool ev_left = strcmp(text, "left public chat") == 0;

        if (ev_system || ev_join || ev_left) {
            gc_push_event(
                name, text,
                ev_join ? GC_EV_JOIN :
                ev_left ? GC_EV_LEAVE : GC_EV_SYSTEM
            );
            return;
        }
    }

    if (
        global_chat_message_count >=
        GLOBAL_CHAT_MAX_MESSAGES
    ) {
        memmove(
            &global_chat_messages[0],
            &global_chat_messages[1],
            sizeof(global_chat_message) *
            (GLOBAL_CHAT_MAX_MESSAGES - 1)
        );

        global_chat_message_count =
            GLOBAL_CHAT_MAX_MESSAGES - 1;
    }

    if (!gc_adding_own) {
        gc_total_messages++;
    }

    global_chat_message* message =
        &global_chat_messages[
            global_chat_message_count
        ];

    message->seq = gc_adding_own ? 0UL : gc_total_messages;

    strncpy(
        message->name,
        name,
        GLOBAL_CHAT_NAME_LEN - 1
    );

    message->name[
        GLOBAL_CHAT_NAME_LEN - 1
    ] = '\0';

    if (owner != NULL) {
        strncpy(
            message->owner,
            owner,
            GLOBAL_CHAT_NAME_LEN - 1
        );

        message->owner[
            GLOBAL_CHAT_NAME_LEN - 1
        ] = '\0';
    } else {
        message->owner[0] = '\0';
    }

    strncpy(
        message->text,
        text,
        GLOBAL_CHAT_TEXT_LEN - 1
    );

    message->text[
        GLOBAL_CHAT_TEXT_LEN - 1
    ] = '\0';

    time_t now = time(NULL);
    struct tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    strftime(
        message->time_str,
        sizeof(message->time_str),
        "%H:%M",
        &local_tm
    );

    global_chat_message_count++;
}

/*
 * Looks up the clan owner's label for whichever access key
 * the given username is currently connected with. Empty
 * string if not found (e.g. they've since disconnected).
 */
static void global_chat_find_owner(
    const char* username,
    char* out,
    size_t out_size
) {
    out[0] = '\0';

    if (
        global_chat_net == NULL ||
        username == NULL
    ) {
        return;
    }

    int count =
        jsr_network_roster_count(
            global_chat_net
        );

    for (
        int i = 0;
        i < count;
        i++
    ) {
        char name[32];

        if (
            !jsr_network_roster_name(
                global_chat_net,
                i,
                name,
                sizeof(name)
            )
        ) {
            continue;
        }

        if (
            strcmp(name, username) == 0
        ) {
            jsr_network_roster_owner(
                global_chat_net,
                i,
                out,
                out_size
            );

            return;
        }
    }
}

/*
 * Called by the JSR network layer whenever another
 * player's message arrives from the relay. Our own
 * messages are filtered out before this fires (see
 * jsr_add_chat_message's username check), so we don't
 * need to de-duplicate here.
 */
static void global_chat_on_network_message(
    tchat_message* msg
) {
    if (msg == NULL) {
        return;
    }

    char owner[32];

    global_chat_find_owner(
        msg->username,
        owner,
        sizeof(owner)
    );

    /* Quick message from a teammate: strip the invisible tag, show the
     * clean text, and play that slot's sound (their text can differ
     * from ours -- the tag is what tells us). */
    int qm_slot = -1;
    const char* shown = msg->message;

    for (int i = 0; i < GC_QM_COUNT; i++) {
        if (strncmp(msg->message, gc_qm_tag[i], GC_QM_TAG_LEN) == 0) {
            qm_slot = i;
            shown = msg->message + GC_QM_TAG_LEN;
            break;
        }
    }

    global_chat_add_message(
        msg->username,
        owner,
        shown
    );

    if (qm_slot >= 0) {
        gc_qm_ensure_loaded();

        if (gc_qm_sound) {
            gc_play_quick_sound(qm_slot);
        }
    }
}

void global_chat_init(tenv* env) {
    global_chat_initialized = true;
    global_chat_open = false;

    global_chat_message_count = 0;
    gc_event_count = 0;
    gc_toast_until_ms = 0;

    memset(
        global_chat_input,
        0,
        sizeof(global_chat_input)
    );

    /* Figure out the nickname to chat under. */
    const char* nickname = "Player";

    if (
        env != NULL &&
        env->usr != NULL &&
        env->usr->usrs.nickname[0] != '\0'
    ) {
        nickname = env->usr->usrs.nickname;
    }

    /* Local chat-state object (message/member bookkeeping). */
    global_chat_tchat = tchat_create(nickname);

    if (global_chat_tchat == NULL) {
        global_chat_add_message(
            "ZORO",
            NULL,
            "Could not start chat system."
        );

        return;
    }

    tchat_set_on_message_callback(
        global_chat_tchat,
        global_chat_on_network_message
    );

    /* Join the shared public room locally. */
    tchat_join_team(
        global_chat_tchat,
        GLOBAL_CHAT_ROOM_KEY
    );

    /* Network relay client (host/port args are unused; the
     * relay always points at the Railway deployment). */
    global_chat_net = jsr_network_create("", 0);

    if (global_chat_net == NULL) {
        global_chat_add_message(
            "ZORO",
            NULL,
            "Could not start network connection."
        );

        return;
    }

    global_chat_net->chat = global_chat_tchat;

    global_chat_try_connect(env);
}

/*
 * (Re)connects using whatever key is currently saved in
 * usrs->public_chat_key. Safe to call repeatedly -- does
 * nothing if already connected/connecting or if no key is
 * set yet.
 */
static void global_chat_try_connect(
    tenv* env
) {
    if (
        global_chat_net == NULL ||
        env == NULL ||
        env->usr == NULL
    ) {
        return;
    }

    const char* key =
        env->usr->usrs.public_chat_key;

    if (key[0] == '\0') {
        return;
    }

    const char* nickname = "Player";

    if (
        env->usr->usrs.nickname[0] != '\0'
    ) {
        nickname = env->usr->usrs.nickname;
    }

    jsr_network_connect(
        global_chat_net,
        GLOBAL_CHAT_ROOM_KEY,
        nickname,
        key
    );
}

void global_chat_update(tenv* env) {
    if (!global_chat_initialized) {
        return;
    }

    if (global_chat_net != NULL) {
        /* Process WebSocket events; must run every frame. */
        jsr_network_update(
            global_chat_net,
            1.0f / 60.0f
        );
    }

    /*
     * Auto-reconnect. The connection reliably dies whenever
     * the app is backgrounded for a bit (the render loop --
     * and with it jsr_network_update() -- simply stops
     * running while minimized, so the socket goes stale),
     * and there's otherwise no retry logic anywhere in the
     * network layer. Try again every few seconds whenever
     * we're disconnected, have a saved key, and that key
     * hasn't been explicitly rejected by the server.
     */
    static float reconnect_timer = 0.0f;
    reconnect_timer += 1.0f / 60.0f;

    if (
        reconnect_timer >= 3.0f &&
        global_chat_net != NULL &&
        !jsr_network_is_connected(global_chat_net) &&
        global_chat_net->ws_connection == NULL &&
        !jsr_network_is_auth_rejected(global_chat_net) &&
        env != NULL &&
        env->usr != NULL &&
        env->usr->usrs.public_chat_key[0] != '\0'
    ) {
        reconnect_timer = 0.0f;

        global_chat_try_connect(env);
    }

    /*
     * If the player's chosen nickname changes while
     * connected (or between connections), reconnect under
     * the new name -- otherwise the server, other players,
     * and our own roster entry all keep showing whatever
     * name we originally joined with.
     */
    static char last_seen_nickname[64] = "";

    if (
        global_chat_net != NULL &&
        env != NULL &&
        env->usr != NULL
    ) {
        const char* current_nickname = "Player";

        if (env->usr->usrs.nickname[0] != '\0') {
            current_nickname = env->usr->usrs.nickname;
        }

        if (
            last_seen_nickname[0] != '\0' &&
            strcmp(
                last_seen_nickname,
                current_nickname
            ) != 0
        ) {
            jsr_network_disconnect(
                global_chat_net
            );

            global_chat_try_connect(env);
        }

        strncpy(
            last_seen_nickname,
            current_nickname,
            sizeof(last_seen_nickname) - 1
        );

        last_seen_nickname[
            sizeof(last_seen_nickname) - 1
        ] = '\0';
    }

    /* Beep when a teammate newly turns SOS on. */
    {
        static float sos_alert_timer = 0.0f;
        sos_alert_timer += 1.0f / 60.0f;

        if (sos_alert_timer >= 0.25f) {
            sos_alert_timer = 0.0f;

            const char* own = NULL;

            if (
                env != NULL && env->usr != NULL &&
                env->usr->usrs.nickname[0] != '\0'
            ) {
                own = env->usr->usrs.nickname;
            }

            gc_sos_alert_tick(own);
        }
    }

    /*
     * Broadcast our own position roughly twice a second
     * while actually playing, so other Public Chat players
     * on the same game server can see us on their minimap.
     */
    static float location_timer = 0.0f;
    location_timer += 1.0f / 60.0f;

    if (
        location_timer >= 0.5f &&
        global_chat_net != NULL &&
        jsr_network_is_connected(global_chat_net) &&
        env != NULL &&
        env->usr != NULL
    ) {
        location_timer = 0.0f;

        game_data* gdata = &env->usr->gdata;

        if (
            gdata->curr_screen == PLAYING &&
            gdata->conn == CONNECTED
        ) {
            int snakes_len =
                tdarray_length(gdata->data.snakes);

            for (
                int i = 0;
                i < snakes_len;
                i++
            ) {
                snake* s =
                    gdata->data.snakes + i;

                if (s->id == gdata->data.snake_id) {
                    user_settings* own_usrs = &env->usr->usrs;

                    jsr_network_send_location(
                        global_chat_net,
                        s->xx + s->fx,
                        s->yy + s->fy,
                        own_usrs->ipv4,
                        own_usrs->own_marker_shape,
                        own_usrs->own_marker_color[0],
                        own_usrs->own_marker_color[1],
                        own_usrs->own_marker_color[2],
                        gdata->data.score,
                        gdata->data.ping,
                        global_chat_is_sos_active(),
                        own_usrs->profile_emoji_id
                    );

                    break;
                }
            }
        }
    }
}

global_chat_adjust_mode global_chat_get_adjust_mode(void) {
    return global_chat_adjust;
}

void global_chat_set_adjust_mode(
    tenv* env,
    global_chat_adjust_mode mode
) {
    if (mode == global_chat_adjust) {
        return;
    }

    if (
        global_chat_adjust != GLOBAL_CHAT_ADJUST_NONE &&
        mode == GLOBAL_CHAT_ADJUST_NONE
    ) {
        /* Confirming -- persist whatever rect was last captured
         * this session while dragging/resizing. */
        if (env != NULL && env->usr != NULL) {
            user_settings* usrs = &env->usr->usrs;

            usrs->public_chat_pos_custom = true;
            usrs->public_chat_rel_x = global_chat_adjust_x;
            usrs->public_chat_rel_y = global_chat_adjust_y;
            usrs->public_chat_rel_w = global_chat_adjust_w;
            usrs->public_chat_rel_h = global_chat_adjust_h;

            save_user_settings(usrs);
        }
    } else if (
        global_chat_adjust == GLOBAL_CHAT_ADJUST_NONE &&
        mode != GLOBAL_CHAT_ADJUST_NONE
    ) {
        /* Entering a mode -- make sure there's something visible
         * to drag/resize, and seed the tracked rect from wherever
         * the window currently sits so the first frame doesn't
         * jump it somewhere unexpected. */
        global_chat_open = true;

        if (env != NULL && env->usr != NULL) {
            user_settings* usrs = &env->usr->usrs;
            ImGuiViewport* vp = igGetMainViewport();

            if (
                !usrs->public_chat_pos_custom &&
                vp->WorkSize.x > 0.0f &&
                vp->WorkSize.y > 0.0f
            ) {
                float default_w = fminf(480.0f, vp->WorkSize.x - 36.0f);
                float default_h = fminf(420.0f, vp->WorkSize.y - 36.0f);

                usrs->public_chat_rel_x = 18.0f / vp->WorkSize.x;
                usrs->public_chat_rel_y = 18.0f / vp->WorkSize.y;
                usrs->public_chat_rel_w = default_w / vp->WorkSize.x;
                usrs->public_chat_rel_h = default_h / vp->WorkSize.y;
            }

            global_chat_adjust_x = usrs->public_chat_rel_x;
            global_chat_adjust_y = usrs->public_chat_rel_y;
            global_chat_adjust_w = usrs->public_chat_rel_w;
            global_chat_adjust_h = usrs->public_chat_rel_h;
        }
    }

    global_chat_adjust = mode;
}

void global_chat_draw(tenv* env) {
    if (!global_chat_initialized) {
        return;
    }

    ImGuiViewport* viewport =
        igGetMainViewport();

    /*
     * One window, one persistent ID, used for both the small
     * collapsed card and the full expanded chat box, so the
     * position stays put across the transition.
     */
    const char* title = "##zoro_chat_window";

    ImVec2 collapsed_size = {
        170.0f,
        108.0f
    };

    user_settings* usrs =
        (env != NULL && env->usr != NULL) ?
            &env->usr->usrs :
            NULL;

    /*
     * Menus and settings screens: the little collapsed card would sit
     * on top of their buttons and swallow the taps (it is drawn last,
     * and on Android it also registers a touch-capture rect). Skip it
     * entirely there. It stays on the title screen and in matches, and
     * an expanded chat or an active position/size adjustment is never
     * hidden. Skipping the draw also drops the capture rect, which is
     * rebuilt every frame.
     */
    if (
        env != NULL && env->usr != NULL &&
        !global_chat_open &&
        global_chat_adjust == GLOBAL_CHAT_ADJUST_NONE
    ) {
        screen gc_scr = env->usr->gdata.curr_screen;

        if (gc_scr != TITLE_SCREEN && gc_scr != PLAYING) {
            return;
        }
    }

    ImVec2 expanded_size;
    ImVec2 fixed_pos;

    if (usrs != NULL && usrs->public_chat_pos_custom) {
        expanded_size.x = usrs->public_chat_rel_w * viewport->WorkSize.x;
        expanded_size.y = usrs->public_chat_rel_h * viewport->WorkSize.y;
        fixed_pos.x = viewport->WorkPos.x + usrs->public_chat_rel_x * viewport->WorkSize.x;
        fixed_pos.y = viewport->WorkPos.y + usrs->public_chat_rel_y * viewport->WorkSize.y;
    } else {
        expanded_size.x = 480.0f;
        expanded_size.y = 420.0f;
        fixed_pos.x = viewport->WorkPos.x + 18.0f;
        fixed_pos.y = viewport->WorkPos.y + 18.0f;
    }

    float chat_min_w = fminf(260.0f, viewport->WorkSize.x - 36.0f);
    float chat_min_h = fminf(220.0f, viewport->WorkSize.y - 36.0f);
    float chat_max_w = fmaxf(chat_min_w, viewport->WorkSize.x - 36.0f);
    float chat_max_h = fmaxf(chat_min_h, viewport->WorkSize.y - 36.0f);

    if (expanded_size.x > chat_max_w) expanded_size.x = chat_max_w;
    if (expanded_size.y > chat_max_h) expanded_size.y = chat_max_h;
    if (expanded_size.x < chat_min_w) expanded_size.x = chat_min_w;
    if (expanded_size.y < chat_min_h) expanded_size.y = chat_min_h;

    /* Keep it fully on-screen for whatever size ended up in effect. The
     * small collapsed card is clamped by its own size so it can be
     * dragged right up to any edge. */
    ImVec2 clamp_sz = global_chat_open ? expanded_size : collapsed_size;

    if (fixed_pos.x < viewport->WorkPos.x) {
        fixed_pos.x = viewport->WorkPos.x;
    }
    if (fixed_pos.x > viewport->WorkPos.x + viewport->WorkSize.x - clamp_sz.x) {
        fixed_pos.x = viewport->WorkPos.x + viewport->WorkSize.x - clamp_sz.x;
    }
    if (fixed_pos.y < viewport->WorkPos.y) {
        fixed_pos.y = viewport->WorkPos.y;
    }
    if (fixed_pos.y > viewport->WorkPos.y + viewport->WorkSize.y - clamp_sz.y) {
        fixed_pos.y = viewport->WorkPos.y + viewport->WorkSize.y - clamp_sz.y;
    }

    /*
     * Position and size are forced every frame (no drag, no resize)
     * unless the player is adjusting them (RESIZE button, Settings
     * tab, or the TEAM CHAT settings panel) -- then ImGui's own
     * drag/resize takes over until they confirm.
     */
    bool adjusting_pos =
        global_chat_open &&
        global_chat_adjust == GLOBAL_CHAT_ADJUST_POSITION;

    bool adjusting_size =
        global_chat_open &&
        global_chat_adjust == GLOBAL_CHAT_ADJUST_SIZE;

    igSetNextWindowPos(
        fixed_pos,
        adjusting_pos ? ImGuiCond_Appearing : ImGuiCond_Always,
        (ImVec2){0.0f, 0.0f}
    );

    igSetNextWindowSize(
        global_chat_open ?
            expanded_size :
            collapsed_size,
        adjusting_size ? ImGuiCond_Appearing : ImGuiCond_Always
    );

    if (adjusting_size) {
        igSetNextWindowSizeConstraints(
            (ImVec2){chat_min_w, chat_min_h},
            (ImVec2){chat_max_w, chat_max_h},
            NULL,
            NULL
        );
    }

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground;

    if (!adjusting_pos) {
        flags |= ImGuiWindowFlags_NoMove;
    }

    if (!adjusting_size) {
        flags |= ImGuiWindowFlags_NoResize;
    }

    /* The whole look is drawn by hand, so the window itself is just
     * a transparent, border-less, padding-less canvas. */
    igPushStyleVar_Vec2(
        ImGuiStyleVar_WindowPadding,
        (ImVec2){0.0f, 0.0f}
    );
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);

    if (igBegin(title, NULL, flags)) {
        ImVec2 live_pos;
        ImVec2 live_size;

        igGetWindowPos(&live_pos);
        igGetWindowSize(&live_size);

#ifdef ANDROID
        android_ui_capture_rect(
            live_pos.x,
            live_pos.y,
            live_pos.x + live_size.x,
            live_pos.y + live_size.y
        );
#endif

        if (
            (adjusting_pos || adjusting_size) &&
            viewport->WorkSize.x > 0.0f &&
            viewport->WorkSize.y > 0.0f
        ) {
            /* Track the live rect so whatever it is the instant the
             * player confirms is what gets saved. */
            global_chat_adjust_x =
                (live_pos.x - viewport->WorkPos.x) / viewport->WorkSize.x;
            global_chat_adjust_y =
                (live_pos.y - viewport->WorkPos.y) / viewport->WorkSize.y;
            global_chat_adjust_w =
                live_size.x / viewport->WorkSize.x;
            global_chat_adjust_h =
                live_size.y / viewport->WorkSize.y;
        }

        if (!global_chat_open) {
            /* ---- collapsed card: "TEAM CHAT" + Open ---- */
            gc_chat_viewing = false;
            gc_divider_active = false;
            ImDrawList* dl = igGetWindowDrawList();
            ImVec2 p = live_pos;
            ImVec2 sz = live_size;

            ImDrawList_AddRectFilled(
                dl, p, gc_add(p, sz.x, sz.y),
                IM_COL32(36, 22, 64, 230), 20.0f, 0
            );
            ImDrawList_AddRect(
                dl, p, gc_add(p, sz.x, sz.y),
                IM_COL32(190, 160, 255, 115), 20.0f, 0, 1.5f
            );

            float head_h = 44.0f;

            gc_text_center(
                dl, p, gc_v2(sz.x, head_h),
                IM_COL32(241, 236, 255, 255), "TEAM CHAT"
            );

            ImDrawList_AddLine(
                dl,
                gc_v2(p.x + 1.0f, p.y + head_h),
                gc_v2(p.x + sz.x - 1.0f, p.y + head_h),
                IM_COL32(190, 160, 255, 64),
                1.0f
            );

            ImVec2 bmn = gc_v2(p.x + 12.0f, p.y + head_h + 11.0f);
            ImVec2 bsz = gc_v2(sz.x - 24.0f, sz.y - head_h - 23.0f);

            if (
                gc_button(
                    "##gc_open",
                    bmn,
                    bsz,
                    IM_COL32(120, 70, 220, 255),
                    IM_COL32(145, 92, 245, 255),
                    12.0f,
                    0
                )
            ) {
                global_chat_open = true;
            }

            /* Soft highlight on the top half = the gradient look. */
            ImDrawList_AddRectFilled(
                dl,
                bmn,
                gc_v2(bmn.x + bsz.x, bmn.y + bsz.y * 0.5f),
                IM_COL32(255, 255, 255, 34),
                12.0f,
                ImDrawFlags_RoundCornersTop
            );

            gc_text_center(
                dl, bmn, bsz,
                IM_COL32(255, 255, 255, 255), "Open"
            );

            /* Unread badge (WhatsApp style) on the top-right corner of the
             * card. Drawn on the foreground list so the window clip does
             * not cut it off. */
            {
                unsigned long unread_n = gc_total_messages - gc_seen_messages;

                if (unread_n > 0) {
                    char ub[8];

                    if (unread_n > 99) {
                        snprintf(ub, sizeof(ub), "99+");
                    } else {
                        snprintf(ub, sizeof(ub), "%lu", unread_n);
                    }

                    ImDrawList* fdl =
                        igGetForegroundDrawList_ViewportPtr(viewport);
                    float bh = 22.0f;
                    float bw = fmaxf(bh, gc_text_w(ub) + 12.0f);
                    ImVec2 bmx = gc_v2(p.x + sz.x - 3.0f, p.y + 3.0f + bh);
                    ImVec2 bmn2 = gc_v2(bmx.x - bw, p.y + 3.0f);

                    ImDrawList_AddRectFilled(
                        fdl, bmn2, bmx,
                        IM_COL32(255, 59, 78, 255), bh * 0.5f, 0
                    );
                    ImDrawList_AddRect(
                        fdl, bmn2, bmx,
                        IM_COL32(36, 22, 64, 255), bh * 0.5f, 0, 2.0f
                    );
                    gc_text_center(
                        fdl, bmn2, gc_v2(bw, bh),
                        IM_COL32(255, 255, 255, 255), ub
                    );
                }
            }

            /* Drag handle: the whole card. It MUST be submitted after the
             * Open button: when items overlap, ImGui gives the tap to the
             * first one submitted, so the Open button claims its own area
             * and this handle gets the rest (header and margins). Position
             * is applied next frame (it is forced from the saved values
             * above) and saved when the finger / mouse is released. */
            igSetCursorScreenPos(p);
            igInvisibleButton("##gc_drag", sz, ImGuiButtonFlags_None);

            static bool gc_dragging = false;

            if (
                usrs != NULL &&
                igIsItemActive() &&
                igIsMouseDragging(ImGuiMouseButton_Left, 4.0f) &&
                viewport->WorkSize.x > 0.0f &&
                viewport->WorkSize.y > 0.0f
            ) {
                ImGuiIO* gc_io = igGetIO_Nil();
                float nx = p.x + gc_io->MouseDelta.x;
                float ny = p.y + gc_io->MouseDelta.y;
                float max_x = viewport->WorkPos.x + viewport->WorkSize.x - sz.x;
                float max_y = viewport->WorkPos.y + viewport->WorkSize.y - sz.y;

                if (nx < viewport->WorkPos.x) nx = viewport->WorkPos.x;
                if (ny < viewport->WorkPos.y) ny = viewport->WorkPos.y;
                if (nx > max_x) nx = max_x;
                if (ny > max_y) ny = max_y;

                if (!usrs->public_chat_pos_custom) {
                    /* First move: seed the expanded size the same way
                     * the Adjust Position screen does, so opening the
                     * chat afterwards keeps its default size. */
                    float dw = fminf(480.0f, viewport->WorkSize.x - 36.0f);
                    float dh = fminf(420.0f, viewport->WorkSize.y - 36.0f);

                    usrs->public_chat_rel_w = dw / viewport->WorkSize.x;
                    usrs->public_chat_rel_h = dh / viewport->WorkSize.y;
                    usrs->public_chat_pos_custom = true;
                }

                usrs->public_chat_rel_x =
                    (nx - viewport->WorkPos.x) / viewport->WorkSize.x;
                usrs->public_chat_rel_y =
                    (ny - viewport->WorkPos.y) / viewport->WorkSize.y;
                gc_dragging = true;
            }

            if (gc_dragging && !igIsItemActive()) {
                gc_dragging = false;

                if (usrs != NULL) {
                    save_user_settings(usrs);
                }
            }
        } else {
            global_chat_panel_contents(
                env,
                live_pos,
                live_size
            );
        }
    }

    igEnd();

    igPopStyleVar(3);

    if (
        global_chat_open &&
        gc_close_requested
    ) {
        global_chat_open = false;

        /* Closing the window mid-adjustment would otherwise leave
         * adjust mode dangling with nothing to drag/resize. */
        if (global_chat_adjust != GLOBAL_CHAT_ADJUST_NONE) {
            global_chat_set_adjust_mode(env, GLOBAL_CHAT_ADJUST_NONE);
        }
    }

    gc_close_requested = false;
}

/* ---------------------------------------------------------------------
 * Panel contents helpers
 * ------------------------------------------------------------------- */

/* `text` is what we show locally; `wire` (may equal text) is what is
 * relayed to everyone else. */
static void gc_send_text_ex(tenv* env, const char* text, const char* wire) {
    if (text == NULL || text[0] == '\0') {
        return;
    }

    const char* nickname = env->usr->usrs.nickname;

    if (nickname == NULL || nickname[0] == '\0') {
        nickname = "Player";
    }

    /* Show it immediately for the sender (never counts as unread). */
    gc_adding_own = true;
    global_chat_add_message(
        nickname,
        NULL,
        text
    );
    gc_adding_own = false;

    /* Relay it to everyone else. */
    if (global_chat_net != NULL) {
        jsr_network_send_message(
            global_chat_net,
            wire
        );
    }
}

static void gc_send_text(tenv* env, const char* text) {
    gc_send_text_ex(env, text, text);
}

static void gc_submit_message(tenv* env) {
    if (global_chat_input[0] == '\0') {
        return;
    }

    gc_send_text(env, global_chat_input);

    memset(global_chat_input, 0, sizeof(global_chat_input));
}

/* One-tap quick message from the side rail. */
static void gc_send_quick(tenv* env, int slot) {
    if (slot < 0 || slot >= GC_QM_COUNT || gc_qm_text[slot][0] == '\0') {
        return;
    }

    /* Relay with the invisible quick-message tag so teammates hear this
     * slot's sound whatever their own quick texts are. */
    char wire[GC_QM_TAG_LEN + GC_QM_TEXT_LEN];
    snprintf(wire, sizeof(wire), "%s%s", gc_qm_tag[slot], gc_qm_text[slot]);

    gc_send_text_ex(env, gc_qm_text[slot], wire);

    if (gc_qm_sound) {
        gc_play_quick_sound(slot);
    }
}

static void gc_toggle_sos(void) {
    if (global_chat_is_sos_active()) {
        global_chat_set_sos(0);
    } else {
        /* Active for 5 minutes, or until manually cancelled --
         * whichever comes first. Re-broadcast happens automatically
         * on the next twice-a-second location update. */
        global_chat_set_sos(
            global_chat_now_ms() + 5LL * 60LL * 1000LL
        );

        if (gc_sos_sound) {
            gc_play_sos_beep(1);
        }
    }
}

static void gc_toggle_adjust(tenv* env, global_chat_adjust_mode mode) {
    if (global_chat_adjust == mode) {
        /* Confirms (saves) and locks back down. */
        global_chat_set_adjust_mode(env, GLOBAL_CHAT_ADJUST_NONE);
    } else if (global_chat_adjust != GLOBAL_CHAT_ADJUST_NONE) {
        /* Switching straight from one mode to the other. */
        global_chat_set_adjust_mode(env, mode);
    } else {
        global_chat_set_adjust_mode(env, mode);
    }
}

/* =====================================================================
 * Redesign helpers: emoji font, text clipping, player data, badges,
 * section titles and pill buttons.
 * ===================================================================== */

/* Liberation Sans -- the only panel font that has the profile-emoji
 * glyphs (the default panel font, JetBrains Mono, does not). Set every
 * frame from global_chat_panel_contents(). */
static ImFont* gc_emoji_font = NULL;


/* True once a finger/mouse drag scrolled a list since the last press, so
 * the release of that drag is not mistaken for a tap on a row. */
static bool gc_scroll_dragged = false;

static void gc_emoji_push(void) {
    if (gc_emoji_font != NULL) {
        igPushFont(gc_emoji_font, 0.0f); /* 0 = keep current size */
    }
}

static void gc_emoji_pop(void) {
    if (gc_emoji_font != NULL) {
        igPopFont();
    }
}

static float gc_emoji_w(const char* s) {
    gc_emoji_push();
    float w = gc_text_w(s);
    gc_emoji_pop();
    return w;
}

static void gc_emoji_text(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* s) {
    gc_emoji_push();
    ImDrawList_AddText_Vec2(dl, pos, col, s, NULL);
    gc_emoji_pop();
}

static void gc_emoji_center(
    ImDrawList* dl, ImVec2 mn, ImVec2 sz, ImU32 col, const char* s
) {
    gc_emoji_push();
    gc_text_center(dl, mn, sz, col, s);
    gc_emoji_pop();
}

/* True if the text has any non-ASCII byte (emoji etc.): those glyphs only
 * exist in the emoji font, not in the panel's mono font. */
static bool gc_has_non_ascii(const char* s) {
    for (; *s != '\0'; s++) {
        if ((unsigned char)*s >= 0x80) {
            return true;
        }
    }

    return false;
}

/* ---- colour emoji (the sprites the minimap dots use) ---------------- */

/* Decode one UTF-8 codepoint and advance *pp (bad bytes come back as-is). */
static unsigned int gc_utf8_decode(const char** pp) {
    const unsigned char* p = (const unsigned char*)*pp;
    unsigned int c = p[0];
    int n = 0;
    unsigned int cp = c;

    if (c == 0) {
        return 0;
    }

    if (c >= 0xF0 && c < 0xF8) { cp = c & 0x07; n = 3; }
    else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
    else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }

    int used = 1;

    for (int i = 0; i < n; i++) {
        if ((p[1 + i] & 0xC0) != 0x80) {
            *pp += 1;
            return c;
        }

        cp = (cp << 6) | (p[1 + i] & 0x3F);
        used++;
    }

    *pp += used;
    return cp;
}

static int gc_utf8_encode(unsigned int cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        out[1] = '\0';
        return 1;
    }

    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = '\0';
        return 2;
    }

    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = '\0';
        return 3;
    }

    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    out[4] = '\0';
    return 4;
}

/* Does the text contain at least one emoji that has a colour picture? */
static bool gc_text_has_sprite(const char* s) {
    if (!ntl_emoji_ready()) {
        return false;
    }

    const char* p = s;

    while (*p != '\0') {
        unsigned int cp = gc_utf8_decode(&p);

        if (cp >= 0x2600 && ntl_emoji_index_for_cp(cp) >= 0) {
            return true;
        }
    }

    return false;
}

/* Width an emoji takes next to text, and drawing it (sprite or legacy
 * glyph) for a profile-emoji id as sent in the location packet. */
static float gc_pemoji_w(int id, float lh) {
    if (profile_emoji_is_sprite(id)) {
        return ntl_emoji_ready() ? lh * 1.25f : 0.0f;
    }

    const char* g = profile_emoji_at(id);

    return g[0] != '\0' ? gc_emoji_w(g) : 0.0f;
}

static void gc_pemoji_draw(ImDrawList* dl, ImVec2 p, int id, float lh) {
    if (profile_emoji_is_sprite(id)) {
        if (ntl_emoji_ready()) {
            float es = lh * 1.25f;
            float y = p.y - (es - lh) * 0.5f;

            ntl_emoji_draw(
                dl, gc_v2(p.x, y), gc_v2(p.x + es, y + es),
                id - PROFILE_EMOJI_SPRITE_BASE,
                IM_COL32(255, 255, 255, 255)
            );
        }

        return;
    }

    const char* g = profile_emoji_at(id);

    if (g[0] != '\0') {
        gc_emoji_text(dl, p, IM_COL32(255, 255, 255, 255), g);
    }
}

/* Word-wrapped text with colour emoji drawn inline as pictures. Used for
 * chat messages that contain emoji. draw=false only measures. Returns
 * the height. */
typedef struct {
    ImDrawList* dl;
    ImVec2 origin;
    ImU32 col;
    bool draw;
    float wrap_w, lh, es, line_h, x, y;
} gc_rich;

static void gc_rich_fit(gc_rich* r, float w) {
    if (r->x > 0.0f && r->x + w > r->wrap_w) {
        r->x = 0.0f;
        r->y += r->line_h;
    }
}

static void gc_rich_text_atom(gc_rich* r, const char* str) {
    bool ext = gc_has_non_ascii(str);
    float w;

    if (ext) gc_emoji_push();
    w = gc_text_w(str);
    if (ext) gc_emoji_pop();

    gc_rich_fit(r, w);

    if (r->draw) {
        ImVec2 pos = gc_v2(
            r->origin.x + r->x,
            r->origin.y + r->y + (r->line_h - r->lh) * 0.5f
        );

        if (ext) gc_emoji_push();
        ImDrawList_AddText_Vec2(r->dl, pos, r->col, str, NULL);
        if (ext) gc_emoji_pop();
    }

    r->x += w;
}

static void gc_rich_word(gc_rich* r, const char* word) {
    if (word[0] == '\0') {
        return;
    }

    bool ext = gc_has_non_ascii(word);
    float w;

    if (ext) gc_emoji_push();
    w = gc_text_w(word);
    if (ext) gc_emoji_pop();

    if (w <= r->wrap_w) {
        gc_rich_text_atom(r, word);
        return;
    }

    /* A single word wider than the line: break it by character. */
    const char* q = word;

    while (*q != '\0') {
        const char* st = q;
        gc_utf8_decode(&q);

        char one[8];
        size_t n = (size_t)(q - st);

        if (n >= sizeof(one)) n = sizeof(one) - 1;

        memcpy(one, st, n);
        one[n] = '\0';

        gc_rich_text_atom(r, one);
    }
}

static float gc_rich_text(
    const char* text, float wrap_w, float lh,
    bool draw, ImDrawList* dl, ImVec2 origin, ImU32 col
) {
    gc_rich r;
    r.dl = dl;
    r.origin = origin;
    r.col = col;
    r.draw = draw;
    r.wrap_w = wrap_w;
    r.lh = lh;
    r.es = lh * 1.25f;
    r.line_h = lh * 1.4f;
    r.x = 0.0f;
    r.y = 0.0f;

    float space_w = gc_text_w(" ");
    char word[128];
    size_t wl = 0;
    const char* p = text;

    word[0] = '\0';

    while (*p != '\0') {
        const char* st = p;
        unsigned int cp = gc_utf8_decode(&p);
        int idx = cp >= 0x2600 ? ntl_emoji_index_for_cp(cp) : -1;

        if (cp == ' ' || cp == '\n' || cp == 0xFE0F || idx >= 0) {
            word[wl] = '\0';
            gc_rich_word(&r, word);
            wl = 0;

            if (idx >= 0) {
                gc_rich_fit(&r, r.es);

                if (draw) {
                    ImVec2 a = gc_v2(
                        origin.x + r.x,
                        origin.y + r.y + (r.line_h - r.es) * 0.5f
                    );

                    ntl_emoji_draw(
                        dl, a, gc_v2(a.x + r.es, a.y + r.es), idx,
                        IM_COL32(255, 255, 255, 255)
                    );
                }

                r.x += r.es + lh * 0.12f;
            } else if (cp == ' ' || cp == '\n') {
                if (r.x > 0.0f) {
                    r.x += space_w;
                }
            }
        } else {
            size_t n = (size_t)(p - st);

            if (wl + n >= sizeof(word)) {
                word[wl] = '\0';
                gc_rich_word(&r, word);
                wl = 0;
            }

            memcpy(word + wl, st, n);
            wl += n;
        }
    }

    word[wl] = '\0';
    gc_rich_word(&r, word);

    return r.y + r.line_h;
}

/* Copy `s` into out, ending with ".." if it has to be cut to fit max_w. */
static void gc_clip_text(char* out, size_t cap, const char* s, float max_w) {
    if (cap == 0) {
        return;
    }

    snprintf(out, cap, "%s", s != NULL ? s : "");

    if (max_w <= 0.0f) {
        out[0] = '\0';
        return;
    }

    if (gc_text_w(out) <= max_w) {
        return;
    }

    size_t len = strlen(out);

    while (len > 0) {
        len--;

        while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80) {
            len--;
        }

        if (len + 3 > cap) {
            continue;
        }

        out[len] = '.';
        out[len + 1] = '.';
        out[len + 2] = '\0';

        if (gc_text_w(out) <= max_w) {
            return;
        }

        out[len] = '\0';
    }

    out[0] = '\0';
}

/* 188582 -> "188,582" */
static void gc_fmt_num(char* out, size_t cap, long long v) {
    char raw[32];
    snprintf(raw, sizeof(raw), "%lld", v < 0 ? 0LL : v);

    size_t n = strlen(raw);
    size_t o = 0;

    for (size_t i = 0; i < n && o + 2 < cap; i++) {
        out[o++] = raw[i];

        size_t left = n - i - 1;

        if (left > 0 && left % 3 == 0 && o + 2 < cap) {
            out[o++] = ',';
        }
    }

    out[o] = '\0';
}

/* Avatar letter: first letter after the clan tag, so "{ J S R } G A N E S H"
 * gives "G" instead of "{". */
static void gc_initial(const char* name, char out[8]) {
    const char* p = strrchr(name, '}');
    p = p != NULL ? p + 1 : name;

    while (
        *p != '\0' &&
        (unsigned char)*p < 0x80 &&
        !isalnum((unsigned char)*p)
    ) {
        p++;
    }

    if (*p == '\0') {
        p = name;

        while (
            *p != '\0' &&
            (unsigned char)*p < 0x80 &&
            !isalnum((unsigned char)*p)
        ) {
            p++;
        }
    }

    if (*p == '\0') {
        out[0] = '?';
        out[1] = '\0';
        return;
    }

    unsigned char c = (unsigned char)*p;
    int n = 1;

    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;

    int i = 0;

    for (; i < n && p[i] != '\0' && i < 7; i++) {
        out[i] = p[i];
    }

    out[i] = '\0';

    if (n == 1) {
        out[0] = (char)toupper(c);
    }
}

static ImU32 gc_ping_color(int ping) {
    if (ping < 0) return IM_COL32(140, 133, 170, 200);
    if (ping < 90) return IM_COL32(125, 255, 176, 255);
    if (ping < 180) return IM_COL32(255, 204, 77, 255);
    return IM_COL32(242, 77, 77, 255);
}

/* One entry of the shared location feed (score / ping / SOS / emoji). */
#define GC_MAX_LOCS 64

typedef struct {
    char name[32];
    char ip[64];
    int score;
    int ping;
    bool sos;
    int emoji;
} gc_loc;

static int gc_load_locs(gc_loc* out) {
    if (global_chat_net == NULL) {
        return 0;
    }

    int n = jsr_network_location_count(global_chat_net);
    int k = 0;

    for (int i = 0; i < n && k < GC_MAX_LOCS; i++) {
        gc_loc* l = &out[k];

        l->name[0] = '\0';
        l->ip[0] = '\0';
        l->score = 0;
        l->ping = -1;
        l->sos = false;
        l->emoji = 0;

        if (
            !jsr_network_get_location(
                global_chat_net, i,
                l->name, sizeof(l->name),
                l->ip, sizeof(l->ip),
                NULL, NULL, NULL, NULL, NULL, NULL,
                &l->score, &l->ping,
                &l->sos, &l->emoji
            )
        ) {
            continue;
        }

        if (l->name[0] == '\0') {
            continue;
        }

        k++;
    }

    return k;
}

static const gc_loc* gc_find_loc(const gc_loc* locs, int n, const char* name) {
    for (int i = 0; i < n; i++) {
        if (strcmp(locs[i].name, name) == 0) {
            return &locs[i];
        }
    }

    return NULL;
}

/* How many OTHER teammates currently have SOS on. */
static int gc_others_sos_count(const char* own_name) {
    gc_loc locs[GC_MAX_LOCS];
    int n = gc_load_locs(locs);
    int count = 0;

    for (int i = 0; i < n; i++) {
        if (!locs[i].sos) {
            continue;
        }

        if (own_name != NULL && strcmp(own_name, locs[i].name) == 0) {
            continue;
        }

        count++;
    }

    return count;
}

/* Called ~4x/second. Plays the SOS alert beep once for each teammate
 * whose SOS has just turned on (not repeatedly while it stays on). */
static void gc_sos_alert_tick(const char* own_name) {
    static char prev[GC_MAX_LOCS][32];
    static int prev_n = 0;

    gc_qm_ensure_loaded();

    gc_loc locs[GC_MAX_LOCS];
    int n = gc_load_locs(locs);

    char now[GC_MAX_LOCS][32];
    int now_n = 0;
    bool fresh = false;

    for (int i = 0; i < n; i++) {
        if (!locs[i].sos) {
            continue;
        }

        if (own_name != NULL && strcmp(own_name, locs[i].name) == 0) {
            continue;
        }

        snprintf(now[now_n], sizeof(now[now_n]), "%s", locs[i].name);
        now_n++;

        bool was_on = false;

        for (int j = 0; j < prev_n; j++) {
            if (strcmp(prev[j], locs[i].name) == 0) {
                was_on = true;
                break;
            }
        }

        if (!was_on) {
            fresh = true;
        }
    }

    memcpy(prev, now, sizeof(prev));
    prev_n = now_n;

    if (fresh && gc_sos_sound) {
        gc_play_sos_beep(0);
    }
}

/* Small round-cornered count badge on the top-right of a rail button. */
static void gc_badge(
    ImDrawList* dl, ImVec2 bmn, float btn, float u,
    const char* txt, ImU32 col
) {
    float bw = fmaxf(gc_text_w(txt) + u * 0.5f, u * 1.1f);
    ImVec2 bc = gc_v2(bmn.x + btn - bw * 0.35f, bmn.y + bw * 0.1f);

    ImDrawList_AddRectFilled(
        dl,
        gc_v2(bc.x - bw * 0.5f, bc.y - u * 0.55f),
        gc_v2(bc.x + bw * 0.5f, bc.y + u * 0.55f),
        col, u * 0.55f, 0
    );

    gc_text_center(
        dl,
        gc_v2(bc.x - bw * 0.5f, bc.y - u * 0.55f),
        gc_v2(bw, u * 1.1f),
        IM_COL32(255, 255, 255, 255),
        txt
    );
}

/* Right-aligned pill tag ("YOU", "SOS"); moves *right_x left past it. */
static void gc_tag(
    ImDrawList* dl, float* right_x, float y, float h,
    const char* label, ImU32 col, float u
) {
    float w = gc_text_w(label) + u * 0.9f;
    float x = *right_x - w;

    ImDrawList_AddRectFilled(
        dl, gc_v2(x, y), gc_v2(x + w, y + h), col, h * 0.5f, 0
    );

    gc_text_center(
        dl, gc_v2(x, y), gc_v2(w, h),
        IM_COL32(255, 255, 255, 255), label
    );

    *right_x = x - u * 0.4f;
}

/* Section title with a faint rule running to the right edge. */
static void gc_section(const char* title, float width, float u) {
    ImVec2 p;
    igGetCursorScreenPos(&p);

    float lh = igGetTextLineHeight();
    ImDrawList* dl = igGetWindowDrawList();

    ImDrawList_AddText_Vec2(
        dl, p, IM_COL32(181, 156, 245, 255), title, NULL
    );

    float tw = gc_text_w(title);

    ImDrawList_AddLine(
        dl,
        gc_v2(p.x + tw + u * 0.6f, p.y + lh * 0.55f),
        gc_v2(p.x + width, p.y + lh * 0.55f),
        IM_COL32(190, 160, 255, 40),
        1.0f
    );

    igDummy(gc_v2(width, lh + u * 0.35f));
}

/* Vector smiley for the emoji button (the mono font has no emoji). */
static void gc_icon_smiley(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    float r = s * 0.5f;

    ImDrawList_AddCircle(dl, c, r, col, 24, 2.0f);

    ImDrawList_AddCircleFilled(
        dl, gc_v2(c.x - r * 0.35f, c.y - r * 0.25f), r * 0.11f, col, 8
    );
    ImDrawList_AddCircleFilled(
        dl, gc_v2(c.x + r * 0.35f, c.y - r * 0.25f), r * 0.11f, col, 8
    );

    ImVec2 pts[9];

    for (int i = 0; i < 9; i++) {
        float a = 0.14f * 3.14159265f + (0.72f * 3.14159265f) * (float)i / 8.0f;

        pts[i] = gc_v2(
            c.x + cosf(a) * r * 0.55f,
            c.y + sinf(a) * r * 0.45f + r * 0.02f
        );
    }

    ImDrawList_AddPolyline(dl, pts, 9, col, ImDrawFlags_None, 2.0f);
}

/* Emoji picker open under the message list (Chat tab). */
static bool gc_emoji_picker_open = false;

/* SWITCH needs a second tap ("SURE?") within a few seconds. */
static char gc_switch_pending[32] = "";
static double gc_switch_until = 0.0;

/* Start playing on a teammate's server: from the title screen this is
 * exactly the Play button; mid-game it uses the death-screen "restart"
 * path (close the socket, then reconnect to the new address). */
static void gc_play_on_server(tenv* env, const char* ip) {
    if (env == NULL || env->usr == NULL || ip == NULL || ip[0] == '\0') {
        return;
    }

    tuser_data* usr = env->usr;
    user_settings* usrs = &usr->usrs;
    game_data* gdata = &usr->gdata;

    if (gdata->curr_screen == TITLE_SCREEN) {
        strncpy(usrs->ipv4, ip, MAX_IPV4_LEN);
        usrs->ipv4[MAX_IPV4_LEN] = '\0';

        user_settings_add_saved_name(usrs, usrs->nickname);
        usr->gdata.conn = CONNECTING;
        usr->gdata.curr_screen = PLAYING;
        glfwSetTime(0);
        screenshot_run_reset();
        server_connect(env);
    } else if (
        gdata->curr_screen == PLAYING &&
        gdata->connection != NULL
    ) {
        strncpy(usrs->ipv4, ip, MAX_IPV4_LEN);
        usrs->ipv4[MAX_IPV4_LEN] = '\0';

        gdata->connection->is_closing = true;
        gdata->restart_req = true;
    }
}

/* Flat toggle/action button with a centred label. `on` = green. */
static bool gc_pill_button(
    const char* id, ImVec2 p, ImVec2 sz, const char* label, bool on, float u
) {
    bool clicked = gc_button(
        id, p, sz,
        on ? IM_COL32(40, 160, 90, 255) : IM_COL32(255, 255, 255, 20),
        on ? IM_COL32(55, 190, 110, 255) : IM_COL32(255, 255, 255, 42),
        u * 0.6f,
        on ? 0 : IM_COL32(190, 160, 255, 50)
    );

    gc_text_center(
        igGetWindowDrawList(), p, sz,
        IM_COL32(241, 236, 255, 255), label
    );

    return clicked;
}

/* Red "needs help" cards for every teammate whose SOS is active, each
 * with a LOCATE button (jumps to the Players tab, highlighting them).
 * Returns how many cards were drawn. */
static int gc_draw_sos_cards(float u, float width, const char* own_name) {
    if (global_chat_net == NULL) {
        return 0;
    }

    gc_loc locs[GC_MAX_LOCS];
    int count = gc_load_locs(locs);
    int shown = 0;
    float lh = igGetTextLineHeight();
    float card_h = lh * 2.5f + u * 0.2f;
    float pulse = 0.5f + 0.5f * sinf((float)igGetTime() * 5.0f);

    for (int i = 0; i < count; i++) {
        const gc_loc* l = &locs[i];

        if (!l->sos) {
            continue;
        }

        if (own_name != NULL && strcmp(own_name, l->name) == 0) {
            continue;
        }

        ImVec2 p;
        igGetCursorScreenPos(&p);

        ImDrawList* dl = igGetWindowDrawList();

        ImDrawList_AddRectFilled(
            dl, p, gc_add(p, width, card_h),
            IM_COL32(224, 56, 44, (int)(36.0f + pulse * 40.0f)),
            u * 0.7f, 0
        );
        ImDrawList_AddRect(
            dl, p, gc_add(p, width, card_h),
            IM_COL32(255, 90, 74, (int)(110.0f + pulse * 90.0f)),
            u * 0.7f, 0, 1.2f
        );

        ImVec2 icon_c = gc_v2(p.x + u * 1.2f, p.y + card_h * 0.5f);
        gc_icon_sos(
            dl, icon_c, lh * 1.05f,
            IM_COL32(255, 90, 74, 255),
            IM_COL32(60, 20, 20, 255)
        );

        float bw = gc_text_w("LOCATE") + u * 1.2f;
        float bh = card_h * 0.56f;
        ImVec2 bmn = gc_v2(
            p.x + width - bw - u * 0.6f,
            p.y + (card_h - bh) * 0.5f
        );

        float tx = p.x + u * 2.3f;
        float max_w = bmn.x - u * 0.5f - tx;

        char nm[48];
        gc_clip_text(nm, sizeof(nm), l->name, max_w);

        ImDrawList_AddText_Vec2(
            dl, gc_v2(tx, p.y + card_h * 0.5f - lh * 1.05f),
            IM_COL32(255, 236, 232, 255), nm, NULL
        );

        char sub[48];

        if (l->ping >= 0) {
            snprintf(sub, sizeof(sub), "needs help  -  %d ms", l->ping);
        } else {
            snprintf(sub, sizeof(sub), "needs help");
        }

        char sub_c[48];
        gc_clip_text(sub_c, sizeof(sub_c), sub, max_w);

        ImDrawList_AddText_Vec2(
            dl, gc_v2(tx, p.y + card_h * 0.5f + lh * 0.1f),
            IM_COL32(255, 168, 158, 230), sub_c, NULL
        );

        char btn_id[24];
        snprintf(btn_id, sizeof(btn_id), "##gc_loc%d", i);

        if (
            gc_button(
                btn_id, bmn, gc_v2(bw, bh),
                IM_COL32(224, 56, 44, 255),
                IM_COL32(245, 85, 70, 255),
                u * 0.45f, 0
            )
        ) {
            strncpy(gc_locate_name, l->name, sizeof(gc_locate_name) - 1);
            gc_locate_name[sizeof(gc_locate_name) - 1] = '\0';
            gc_tab = GC_TAB_PLAYERS;
        }

        gc_text_center(
            dl, bmn, gc_v2(bw, bh),
            IM_COL32(255, 255, 255, 255), "LOCATE"
        );

        igSetCursorScreenPos(p);
        igDummy(gc_v2(width, card_h + u * 0.4f));

        shown++;
    }

    return shown;
}

typedef struct {
    char name[32];
    char owner[32];
    char ip[64];
    int score;
    int ping;
    int emoji;
    bool sos;
    bool you;
    bool has_loc;
} gc_player_row;

/* You first, then anyone with SOS on, then highest score, then name. */
static int gc_player_cmp(const void* a, const void* b) {
    const gc_player_row* x = (const gc_player_row*)a;
    const gc_player_row* y = (const gc_player_row*)b;

    if (x->you != y->you) return x->you ? -1 : 1;
    if (x->sos != y->sos) return x->sos ? -1 : 1;
    if (x->score != y->score) return (y->score > x->score) - (y->score < x->score);

    return strcmp(x->name, y->name);
}

/* Roster list (Players tab): avatar, owner + name + emoji, score / server,
 * ping, YOU / SOS tags. Tap a row to highlight it. */
/* Players tab footer: the last notices (system, joined, left), newest
 * first. */
static void gc_draw_activity(float u, float width) {
    igDummy(gc_v2(1.0f, u * 0.5f));
    gc_section("Activity", width, u);

    if (gc_event_count <= 0) {
        igTextDisabled("Nothing yet.");
        return;
    }

    ImDrawList* dl = igGetWindowDrawList();
    float lh = igGetTextLineHeight();
    ImU32 dim = IM_COL32(150, 142, 185, 210);
    ImU32 dimmer = IM_COL32(120, 112, 155, 200);

    for (int i = gc_event_count - 1; i >= 0; i--) {
        const gc_event* ev = &gc_events[i];
        ImVec2 p;
        igGetCursorScreenPos(&p);

        ImU32 dot =
            ev->kind == GC_EV_JOIN ? IM_COL32(125, 255, 176, 255) :
            ev->kind == GC_EV_LEAVE ? IM_COL32(242, 77, 77, 255) :
            ev->kind == GC_EV_RECONNECT ? IM_COL32(255, 204, 77, 255) :
                                          IM_COL32(120, 210, 255, 255);

        float tw = gc_text_w(ev->time_str);
        float max_w = width - u * 1.4f - tw - u * 0.8f;
        char tt[96];
        gc_clip_text(tt, sizeof(tt), ev->text, max_w);

        ImDrawList_AddCircleFilled(
            dl, gc_v2(p.x + u * 0.5f, p.y + lh * 0.5f), u * 0.22f, dot, 10
        );
        ImDrawList_AddText_Vec2(dl, gc_v2(p.x + u * 1.2f, p.y), dim, tt, NULL);
        ImDrawList_AddText_Vec2(
            dl, gc_v2(p.x + width - tw, p.y), dimmer, ev->time_str, NULL
        );

        igDummy(gc_v2(width, lh + u * 0.3f));
    }
}

static void gc_draw_players_list(
    tenv* env, float u, float width, const char* own_name
) {
    int roster_count =
        global_chat_net != NULL ?
            jsr_network_roster_count(global_chat_net) :
            0;

    if (roster_count <= 0) {
        igTextDisabled(
            (global_chat_net != NULL && jsr_network_is_connected(global_chat_net)) ?
                "No players online." :
                "Not connected to team chat."
        );
        return;
    }

    if (roster_count > 64) {
        roster_count = 64;
    }

    static const ImU32 avatar_cols[6] = {
        IM_COL32(224, 112, 154, 255),
        IM_COL32(56, 182, 165, 255),
        IM_COL32(124, 74, 232, 255),
        IM_COL32(230, 160, 60, 255),
        IM_COL32(77, 140, 255, 255),
        IM_COL32(150, 200, 90, 255)
    };

    gc_loc locs[GC_MAX_LOCS];
    int loc_n = gc_load_locs(locs);

    gc_player_row rows[64];
    int n = 0;

    for (int i = 0; i < roster_count; i++) {
        gc_player_row* r = &rows[n];

        memset(r, 0, sizeof(*r));
        r->ping = -1;

        if (
            !jsr_network_roster_name(
                global_chat_net, i, r->name, sizeof(r->name)
            )
        ) {
            continue;
        }

        jsr_network_roster_owner(
            global_chat_net, i, r->owner, sizeof(r->owner)
        );

        const gc_loc* l = gc_find_loc(locs, loc_n, r->name);

        if (l != NULL) {
            r->has_loc = true;
            r->score = l->score;
            r->ping = l->ping;
            r->emoji = l->emoji;
            r->sos = l->sos;
            snprintf(r->ip, sizeof(r->ip), "%s", l->ip);
        }

        r->you =
            own_name != NULL &&
            own_name[0] != '\0' &&
            strcmp(own_name, r->name) == 0;

        n++;
    }

    qsort(rows, (size_t)n, sizeof(rows[0]), gc_player_cmp);

    float lh = igGetTextLineHeight();
    float row_h = lh * 2.9f;
    float gap = u * 0.4f;

    screen curr_scr = env->usr->gdata.curr_screen;
    const char* own_ip = env->usr->usrs.ipv4;
    float pulse = 0.5f + 0.5f * sinf((float)igGetTime() * 5.0f);

    for (int ri = 0; ri < n; ri++) {
        const gc_player_row* pr = &rows[ri];

        ImVec2 p;
        igGetCursorScreenPos(&p);

        ImVec2 pmax = gc_add(p, width, row_h);
        ImDrawList* dl = igGetWindowDrawList();

        bool located =
            gc_locate_name[0] != '\0' &&
            strcmp(gc_locate_name, pr->name) == 0;

        igPushID_Str(pr->name);

        /* PLAY (title screen) / SWITCH (in a match on another server) on
         * this teammate's server. Submitted BEFORE the row button: when
         * items overlap, ImGui gives the tap to the first one submitted. */
        int play_kind = 0;

        if (
            !pr->you && pr->has_loc && pr->ip[0] != '\0' &&
            width > u * 12.0f
        ) {
            if (curr_scr == TITLE_SCREEN) {
                play_kind = 1;
            } else if (curr_scr == PLAYING && strcmp(pr->ip, own_ip) != 0) {
                play_kind = 2;
            }
        }

        bool confirming =
            play_kind == 2 &&
            strcmp(gc_switch_pending, pr->name) == 0 &&
            igGetTime() < gc_switch_until;

        float play_w =
            play_kind != 0 ?
                gc_text_w(play_kind == 2 ? "SWITCH" : "PLAY") + u * 1.4f :
                0.0f;
        float play_h = lh * 1.8f;
        ImVec2 play_mn = gc_v2(
            pmax.x - u * 0.7f - play_w,
            p.y + (row_h - play_h) * 0.5f
        );
        bool play_clicked = false;
        bool play_hot = false;

        if (play_kind != 0) {
            igSetCursorScreenPos(play_mn);

            play_clicked = igInvisibleButton(
                "##gc_play", gc_v2(play_w, play_h), ImGuiButtonFlags_None
            );

            play_hot = igIsItemHovered(0) || igIsItemActive();

            igSetCursorScreenPos(p);
        }

        bool row_clicked = igInvisibleButton(
            "##gc_prow", gc_v2(width, row_h), ImGuiButtonFlags_None
        );

        bool hovered = igIsItemHovered(0) || igIsItemActive();

        ImU32 bg =
            located ? IM_COL32(124, 74, 232, 95) :
            pr->sos ? IM_COL32(224, 56, 44, 50) :
            hovered ? IM_COL32(255, 255, 255, 24) :
                      IM_COL32(255, 255, 255, 12);

        ImDrawList_AddRectFilled(dl, p, pmax, bg, u * 0.7f, 0);

        if (located) {
            ImDrawList_AddRect(
                dl, p, pmax, IM_COL32(190, 160, 255, 170),
                u * 0.7f, 0, 1.2f
            );
        } else if (pr->sos) {
            ImDrawList_AddRect(
                dl, p, pmax,
                IM_COL32(255, 90, 74, (int)(110.0f + pulse * 90.0f)),
                u * 0.7f, 0, 1.2f
            );
        } else if (pr->you) {
            ImDrawList_AddRect(
                dl, p, pmax, IM_COL32(125, 255, 176, 80),
                u * 0.7f, 0, 1.0f
            );
        }

        /* Avatar with an online dot. */
        unsigned hash = 0;

        for (const char* c = pr->name; *c; c++) {
            hash = hash * 31u + (unsigned char)*c;
        }

        float cy = p.y + row_h * 0.5f;
        float r_av = lh * 0.95f;
        ImVec2 ac = gc_v2(p.x + u * 0.9f + r_av, cy);

        ImDrawList_AddCircleFilled(
            dl, ac, r_av, avatar_cols[hash % 6u], 24
        );

        char initial[8];
        gc_initial(pr->name, initial);

        gc_text_center(
            dl,
            gc_v2(ac.x - r_av, ac.y - r_av),
            gc_v2(r_av * 2.0f, r_av * 2.0f),
            IM_COL32(255, 255, 255, 255),
            initial
        );

        ImVec2 dot = gc_v2(ac.x + r_av * 0.72f, ac.y + r_av * 0.72f);
        ImDrawList_AddCircleFilled(
            dl, dot, u * 0.38f, IM_COL32(28, 16, 52, 255), 12
        );
        ImDrawList_AddCircleFilled(
            dl, dot, u * 0.26f, IM_COL32(125, 255, 176, 255), 12
        );

        float tx = ac.x + r_av + u * 0.8f;
        float right =
            pmax.x - u * 0.7f -
            (play_kind != 0 ? play_w + u * 0.6f : 0.0f);
        float ty1 = cy - lh * 1.1f;
        float ty2 = cy + lh * 0.1f;

        /* Tags on the right of line 1. */
        float tag_h = lh * 1.2f;
        float tag_y = ty1 - (tag_h - lh) * 0.5f;
        float tag_x = right;

        if (pr->you) {
            gc_tag(dl, &tag_x, tag_y, tag_h, "YOU", IM_COL32(124, 74, 232, 230), u);
        }

        if (pr->sos) {
            gc_tag(dl, &tag_x, tag_y, tag_h, "SOS", IM_COL32(224, 56, 44, 255), u);
        }

        /* Line 1: owner (red) + name + emoji. */
        float avail1 = tag_x - tx;
        float x = tx;

        if (pr->owner[0] != '\0') {
            char ow[40];
            gc_clip_text(ow, sizeof(ow), pr->owner, avail1 * 0.45f);

            ImDrawList_AddText_Vec2(
                dl, gc_v2(x, ty1), IM_COL32(242, 77, 77, 255), ow, NULL
            );

            x += gc_text_w(ow) + u * 0.4f;
        }

        float ew0 = gc_pemoji_w(pr->emoji, lh);
        float ew = ew0 > 0.0f ? ew0 + u * 0.4f : 0.0f;

        char nm[48];
        gc_clip_text(nm, sizeof(nm), pr->name, avail1 - (x - tx) - ew);

        ImDrawList_AddText_Vec2(
            dl, gc_v2(x, ty1),
            pr->you ?
                IM_COL32(125, 255, 176, 255) :
                IM_COL32(180, 156, 245, 255),
            nm, NULL
        );

        x += gc_text_w(nm) + u * 0.4f;

        if (ew0 > 0.0f) {
            gc_pemoji_draw(dl, gc_v2(x, ty1), pr->emoji, lh);
        }

        /* Line 2: score / server on the left, ping on the right. */
        ImU32 dim = IM_COL32(140, 133, 170, 230);
        float ping_w = 0.0f;
        char ping_s[16] = "";

        if (pr->has_loc && pr->ping >= 0) {
            snprintf(ping_s, sizeof(ping_s), "%d ms", pr->ping);
            ping_w = gc_text_w(ping_s) + u * 0.8f;

            ImDrawList_AddCircleFilled(
                dl, gc_v2(right - ping_w + u * 0.25f, ty2 + lh * 0.5f),
                u * 0.22f, gc_ping_color(pr->ping), 10
            );

            ImDrawList_AddText_Vec2(
                dl, gc_v2(right - ping_w + u * 0.8f, ty2),
                gc_ping_color(pr->ping), ping_s, NULL
            );
        }

        char info[128];

        if (pr->has_loc) {
            char sc[24];
            gc_fmt_num(sc, sizeof(sc), pr->score);

            if (pr->ip[0] != '\0') {
                snprintf(info, sizeof(info), "%s  -  %s", sc, pr->ip);
            } else {
                snprintf(info, sizeof(info), "%s", sc);
            }
        } else if (pr->you) {
            if (curr_scr == PLAYING) {
                snprintf(info, sizeof(info), "%s", own_ip);
            } else {
                snprintf(info, sizeof(info), "In the menu");
            }
        } else {
            snprintf(info, sizeof(info), "No game info");
        }

        char info_c[128];
        gc_clip_text(
            info_c, sizeof(info_c), info,
            right - ping_w - u * 0.5f - tx
        );

        ImDrawList_AddText_Vec2(dl, gc_v2(tx, ty2), dim, info_c, NULL);

        /* PLAY / SWITCH button visual + action. */
        if (play_kind != 0) {
            ImU32 pc =
                play_kind == 1 ?
                    (play_hot ? IM_COL32(150, 100, 250, 255) : IM_COL32(124, 74, 232, 255)) :
                confirming ?
                    IM_COL32(230, 150, 40, 255) :
                    (play_hot ? IM_COL32(95, 80, 160, 255) : IM_COL32(70, 56, 120, 255));

            ImDrawList_AddRectFilled(
                dl, play_mn, gc_add(play_mn, play_w, play_h),
                pc, play_h * 0.5f, 0
            );

            gc_text_center(
                dl, play_mn, gc_v2(play_w, play_h),
                IM_COL32(255, 255, 255, 255),
                play_kind == 1 ? "PLAY" : (confirming ? "SURE?" : "SWITCH")
            );

            if (play_clicked && !gc_scroll_dragged) {
                if (play_kind == 1) {
                    gc_play_on_server(env, pr->ip);
                } else if (confirming) {
                    gc_switch_pending[0] = '\0';
                    gc_play_on_server(env, pr->ip);
                } else {
                    snprintf(
                        gc_switch_pending, sizeof(gc_switch_pending),
                        "%s", pr->name
                    );
                    gc_switch_until = igGetTime() + 3.0;
                }
            }
        }

        /* Tap (not drag) toggles the highlight. */
        if (row_clicked && !gc_scroll_dragged) {
            if (located) {
                gc_locate_name[0] = '\0';
            } else {
                snprintf(gc_locate_name, sizeof(gc_locate_name), "%s", pr->name);
            }
        }

        igPopID();

        igDummy(gc_v2(width, gap));
    }
}

/* Slim draggable scrollbar for the message list, drawn flush with the
 * right edge of the main area. Same absolute-drag logic as before: a
 * fat invisible hit area (fingers) and no per-frame delta
 * accumulation (touch-move events get coalesced). */
static void gc_message_scrollbar(
    ImVec2 track_pos,
    float track_h,
    float hit_w,
    float view_h,
    float scroll_max,
    float scroll_y,
    ImGuiWindow* msg_window,
    float u
) {
    static float drag_anchor_mouse_y = 0.0f;
    static float drag_anchor_scroll = 0.0f;

    float bar_w = fmaxf(5.0f, u * 0.3f);
    float bar_x = track_pos.x + (hit_w - bar_w) * 0.5f;

    float thumb_h = track_h;

    if (scroll_max > 0.0f) {
        thumb_h = track_h * (view_h / (view_h + scroll_max));
        thumb_h = gc_clampf(thumb_h, u * 2.0f, track_h);
    }

    float ratio = scroll_max > 0.0f ? (scroll_y / scroll_max) : 0.0f;
    float thumb_y = track_pos.y + ratio * (track_h - thumb_h);

    ImDrawList* dl = igGetWindowDrawList();

    ImDrawList_AddRectFilled(
        dl,
        gc_v2(bar_x, track_pos.y),
        gc_v2(bar_x + bar_w, track_pos.y + track_h),
        IM_COL32(176, 148, 245, 28),
        bar_w * 0.5f, 0
    );

    ImDrawList_AddRectFilled(
        dl,
        gc_v2(bar_x, thumb_y),
        gc_v2(bar_x + bar_w, thumb_y + thumb_h),
        IM_COL32(191, 165, 245, 170),
        bar_w * 0.5f, 0
    );

    igSetCursorScreenPos(track_pos);
    igInvisibleButton(
        "##gc_scrollbar",
        gc_v2(hit_w, track_h),
        ImGuiButtonFlags_None
    );

    if (scroll_max <= 0.0f) {
        return;
    }

    float usable = track_h - thumb_h;

    if (usable <= 0.0f) {
        return;
    }

    if (igIsItemActivated()) {
        ImVec2 mouse;
        igGetMousePos(&mouse);

        float click_ratio =
            gc_clampf(
                (mouse.y - track_pos.y - thumb_h * 0.5f) / usable,
                0.0f, 1.0f
            );

        float jump = click_ratio * scroll_max;

        igSetScrollY_WindowPtr(msg_window, jump);

        drag_anchor_mouse_y = mouse.y;
        drag_anchor_scroll = jump;
    }

    if (igIsItemActive()) {
        ImVec2 mouse;
        igGetMousePos(&mouse);

        float new_scroll =
            drag_anchor_scroll +
            ((mouse.y - drag_anchor_mouse_y) / usable) * scroll_max;

        igSetScrollY_WindowPtr(
            msg_window,
            gc_clampf(new_scroll, 0.0f, scroll_max)
        );
    }
}

/* Ends a scrolling child that was begun with ImGuiWindowFlags_NoScrollbar:
 * finger/mouse drag scrolls it, and the slim custom scrollbar is drawn
 * next to it (only when there is something to scroll). */
static void gc_end_scroll_child(
    ImVec2 area_pos,
    float area_h,
    float hit_w,
    float child_w,
    float u,
    bool allow_drag_scroll,
    bool over_items
) {
    if (
        allow_drag_scroll &&
        igIsWindowHovered(0) &&
        (over_items || !igIsAnyItemActive()) &&
        igIsMouseDragging(0, 6.0f)
    ) {
        igSetScrollY_Float(
            igGetScrollY() - igGetIO_Nil()->MouseDelta.y
        );
        gc_scroll_dragged = true;
    }

    float scroll_max = igGetScrollMaxY();
    float scroll_y = igGetScrollY();
    ImGuiWindow* win = igGetCurrentWindow();

    igEndChild();

    if (scroll_max > 1.0f) {
        gc_message_scrollbar(
            gc_v2(area_pos.x + child_w, area_pos.y),
            area_h,
            hit_w,
            area_h,
            scroll_max,
            scroll_y,
            win,
            u
        );
    }
}

/* Chat messages: one rounded bubble per message (name line + wrapped
 * text), consecutive messages from the same sender in the same minute
 * are grouped, join/leave notices are compact one-liners. */
static void gc_draw_messages(
    tenv* env,
    ImVec2 area_pos,
    ImVec2 area_size,
    float u,
    bool allow_drag_scroll
) {
    const char* own_name = env->usr->usrs.nickname;

    float hit_w = u * 1.6f;
    float child_w = area_size.x - hit_w;

    if (child_w < 60.0f) child_w = 60.0f;

    igSetCursorScreenPos(area_pos);

    igBeginChild_Str(
        "##global_chat_messages",
        gc_v2(child_w, area_size.y),
        0,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground
    );

    static int last_seen_message_count = 0;

    ImVec2 avail;
    igGetContentRegionAvail(&avail);

    bool was_at_bottom =
        igGetScrollY() >= igGetScrollMaxY() - 1.0f;

    ImDrawList* dl = igGetWindowDrawList();

    float w = avail.x;
    float lh = igGetTextLineHeight();
    float pad_x = u * 0.75f;
    float pad_y = u * 0.42f;
    float wrap_w = w - pad_x * 2.0f;

    if (wrap_w < 20.0f) wrap_w = 20.0f;

    gc_loc locs[GC_MAX_LOCS];
    int loc_n = gc_load_locs(locs);

    bool is_connected =
        global_chat_net != NULL &&
        jsr_network_is_connected(global_chat_net);

    ImU32 dim = IM_COL32(150, 142, 185, 210);
    ImU32 dimmer = IM_COL32(120, 112, 155, 200);

    if (global_chat_message_count == 0) {
        ImVec2 cp;
        igGetCursorScreenPos(&cp);

        float box_h = area_size.y * 0.5f;

        gc_text_center(
            dl, cp, gc_v2(w, box_h), dim,
            is_connected ? "No messages yet" : "Connecting to team chat..."
        );
        gc_text_center(
            dl, gc_add(cp, 0.0f, lh * 1.5f), gc_v2(w, box_h), dimmer,
            "Say hi to your team"
        );

        igDummy(gc_v2(w, box_h));
    }

    char prev_hm[8] = "";
    const char* prev_name = NULL;
    bool prev_normal = false;
    bool divider_drawn = false;
    bool divider_scrolled = false;

    for (int i = 0; i < global_chat_message_count; i++) {
        global_chat_message* m = &global_chat_messages[i];

        bool is_system = strcmp(m->name, "[SYSTEM]") == 0;
        bool is_own =
            own_name != NULL &&
            own_name[0] != '\0' &&
            strcmp(m->name, own_name) == 0;

        if (
            gc_divider_active &&
            !divider_drawn &&
            m->seq > gc_divider_after
        ) {
            divider_drawn = true;

            ImVec2 dp;
            igGetCursorScreenPos(&dp);

            const char* dtxt = "NEW MESSAGES";
            float dtw = gc_text_w(dtxt);
            float dh = lh + u * 0.7f;
            float dmid = dp.y + dh * 0.5f;
            float gap = u * 0.7f;
            float cx = dp.x + w * 0.5f;
            ImU32 dcol = IM_COL32(242, 120, 120, 255);
            ImU32 lcol = IM_COL32(242, 120, 120, 110);

            ImDrawList_AddLine(
                dl, gc_v2(dp.x + pad_x * 0.5f, dmid),
                gc_v2(cx - dtw * 0.5f - gap, dmid), lcol, 1.0f
            );
            ImDrawList_AddLine(
                dl, gc_v2(cx + dtw * 0.5f + gap, dmid),
                gc_v2(dp.x + w - pad_x * 0.5f, dmid), lcol, 1.0f
            );
            ImDrawList_AddText_Vec2(
                dl, gc_v2(cx - dtw * 0.5f, dp.y + (dh - lh) * 0.5f),
                dcol, dtxt, NULL
            );

            igDummy(gc_v2(w, dh));

            if (gc_divider_scroll) {
                /* Jump so the divider sits near the top of the view. */
                igSetScrollHereY(0.08f);
                gc_divider_scroll = false;
                divider_scrolled = true;
            }

            prev_normal = false;
        }

        char hm[8];
        snprintf(hm, sizeof(hm), "%.5s", m->time_str);

        igPushID_Int(i);

        bool joined = strcmp(m->text, "joined public chat") == 0;
        bool left = strcmp(m->text, "left public chat") == 0;

        if (!is_system && (joined || left)) {
            const char* verb = joined ? "joined" : "left";
            ImU32 dotc =
                joined ?
                    IM_COL32(125, 255, 176, 255) :
                    IM_COL32(242, 77, 77, 255);

            /* "left" immediately followed by "joined" = a reconnect blip. */
            if (left && i + 1 < global_chat_message_count) {
                global_chat_message* nx = &global_chat_messages[i + 1];

                if (
                    strcmp(nx->text, "joined public chat") == 0 &&
                    strcmp(nx->name, m->name) == 0
                ) {
                    verb = "reconnected";
                    dotc = IM_COL32(255, 204, 77, 255);
                    snprintf(hm, sizeof(hm), "%.5s", nx->time_str);
                    i++;
                }
            }

            prev_normal = false;

            if (gc_show_events) {
                ImVec2 p;
                igGetCursorScreenPos(&p);

                float tw = gc_text_w(hm);
                float tx = p.x + pad_x * 1.4f;

                ImDrawList_AddCircleFilled(
                    dl, gc_v2(p.x + pad_x * 0.7f, p.y + lh * 0.5f),
                    u * 0.22f, dotc, 10
                );

                float max_w =
                    w - pad_x * 1.4f - tw - pad_x -
                    gc_text_w(verb) - u * 0.5f;

                char nm[48];
                gc_clip_text(nm, sizeof(nm), m->name, max_w);

                ImDrawList_AddText_Vec2(
                    dl, gc_v2(tx, p.y), dim, nm, NULL
                );

                tx += gc_text_w(nm) + u * 0.4f;

                ImDrawList_AddText_Vec2(
                    dl, gc_v2(tx, p.y), dimmer, verb, NULL
                );

                ImDrawList_AddText_Vec2(
                    dl, gc_v2(p.x + w - tw - pad_x * 0.5f, p.y),
                    dimmer, hm, NULL
                );

                igDummy(gc_v2(w, lh + u * 0.35f));
            }

            igPopID();
            continue;
        }

        bool grouped =
            !is_system &&
            prev_normal &&
            prev_name != NULL &&
            strcmp(prev_name, m->name) == 0 &&
            strcmp(prev_hm, hm) == 0;

        bool ext = gc_has_non_ascii(m->text);
        bool rich = gc_text_has_sprite(m->text);

        ImVec2 tsz;

        if (rich) {
            tsz.x = wrap_w;
            tsz.y = gc_rich_text(
                m->text, wrap_w, lh, false, dl, gc_v2(0.0f, 0.0f), 0
            );
        } else {
            if (ext) gc_emoji_push();
            igCalcTextSize(&tsz, m->text, NULL, false, wrap_w);
            if (ext) gc_emoji_pop();
        }

        float head_h = grouped ? 0.0f : lh + u * 0.15f;
        float bh = pad_y * 2.0f + head_h + tsz.y;

        ImVec2 p;
        igGetCursorScreenPos(&p);

        ImVec2 pmax = gc_add(p, w, bh);

        ImU32 bg =
            is_system ? IM_COL32(62, 186, 255, 26) :
            is_own ? IM_COL32(124, 74, 232, 78) :
                     IM_COL32(255, 255, 255, 13);

        ImDrawList_AddRectFilled(dl, p, pmax, bg, u * 0.65f, 0);

        if (is_system) {
            ImDrawList_AddRect(
                dl, p, pmax, IM_COL32(120, 210, 255, 70),
                u * 0.65f, 0, 1.0f
            );
        } else if (is_own) {
            ImDrawList_AddRect(
                dl, p, pmax, IM_COL32(190, 160, 255, 60),
                u * 0.65f, 0, 1.0f
            );
        }

        if (!grouped) {
            float hx = p.x + pad_x;
            float hy = p.y + pad_y;
            float tw = gc_text_w(hm);

            ImDrawList_AddText_Vec2(
                dl, gc_v2(pmax.x - pad_x - tw, hy), dimmer, hm, NULL
            );

            float room = (pmax.x - pad_x - tw - u * 0.6f) - hx;

            if (is_system) {
                ImDrawList_AddText_Vec2(
                    dl, gc_v2(hx, hy),
                    IM_COL32(158, 232, 255, 255), "System", NULL
                );
            } else {
                const gc_loc* l = gc_find_loc(locs, loc_n, m->name);
                int eid = l != NULL ? l->emoji : 0;
                float ew0 = gc_pemoji_w(eid, lh);
                float ew = ew0 > 0.0f ? ew0 + u * 0.4f : 0.0f;
                float x = hx;

                if (m->owner[0] != '\0') {
                    char ow[40];
                    gc_clip_text(ow, sizeof(ow), m->owner, room * 0.45f);

                    ImDrawList_AddText_Vec2(
                        dl, gc_v2(x, hy),
                        IM_COL32(242, 77, 77, 255), ow, NULL
                    );

                    x += gc_text_w(ow) + u * 0.4f;
                }

                char nm[48];
                gc_clip_text(nm, sizeof(nm), m->name, room - (x - hx) - ew);

                ImDrawList_AddText_Vec2(
                    dl, gc_v2(x, hy),
                    is_own ?
                        IM_COL32(125, 255, 176, 255) :
                        IM_COL32(181, 156, 245, 255),
                    nm, NULL
                );

                x += gc_text_w(nm) + u * 0.4f;

                if (ew0 > 0.0f) {
                    gc_pemoji_draw(dl, gc_v2(x, hy), eid, lh);
                }
            }
        }

        if (rich) {
            gc_rich_text(
                m->text, wrap_w, lh, true, dl,
                gc_v2(p.x + pad_x, p.y + pad_y + head_h),
                is_system ?
                    IM_COL32(158, 232, 255, 255) :
                    IM_COL32(237, 232, 255, 255)
            );
        } else {
            igSetCursorScreenPos(gc_v2(p.x + pad_x, p.y + pad_y + head_h));
            igPushTextWrapPos(igGetCursorPosX() + wrap_w);
            igPushStyleColor_Vec4(
                ImGuiCol_Text,
                is_system ?
                    (ImVec4){0.62f, 0.91f, 1.0f, 1.0f} :
                    (ImVec4){0.93f, 0.91f, 1.0f, 1.0f}
            );
            if (ext) gc_emoji_push();
            igTextUnformatted(m->text, NULL);
            if (ext) gc_emoji_pop();
            igPopStyleColor(1);
            igPopTextWrapPos();
        }

        igSetCursorScreenPos(p);
        igDummy(gc_v2(w, bh + (grouped ? u * 0.12f : u * 0.3f)));

        prev_normal = !is_system;
        prev_name = m->name;
        snprintf(prev_hm, sizeof(prev_hm), "%s", hm);

        igPopID();
    }

    /* Teammates asking for help show up as cards at the end. */
    gc_draw_sos_cards(u, avail.x, own_name);

    if (
        !divider_scrolled &&
        global_chat_message_count != last_seen_message_count &&
        (was_at_bottom || last_seen_message_count == 0)
    ) {
        /* Only auto-scroll if the player was already at (or near)
         * the bottom. */
        igSetScrollHereY(1.0f);
    }

    last_seen_message_count = global_chat_message_count;

    /* "Jump to latest" button while scrolled up. */
    if (igGetScrollMaxY() - igGetScrollY() > lh * 3.0f) {
        ImVec2 wpos;
        ImVec2 wsz;
        igGetWindowPos(&wpos);
        igGetWindowSize(&wsz);

        float r = u * 1.1f;
        ImVec2 c = gc_v2(
            wpos.x + wsz.x - r - u * 0.4f,
            wpos.y + wsz.y - r - u * 0.4f
        );

        igSetCursorScreenPos(gc_v2(c.x - r, c.y - r));

        bool jump = igInvisibleButton(
            "##gc_jump", gc_v2(r * 2.0f, r * 2.0f), ImGuiButtonFlags_None
        );

        bool hot = igIsItemHovered(0) || igIsItemActive();

        ImDrawList_AddCircleFilled(
            dl, c, r,
            hot ? IM_COL32(150, 100, 250, 245) : IM_COL32(124, 74, 232, 230),
            24
        );

        ImDrawList_AddTriangleFilled(
            dl,
            gc_v2(c.x - r * 0.42f, c.y - r * 0.18f),
            gc_v2(c.x + r * 0.42f, c.y - r * 0.18f),
            gc_v2(c.x, c.y + r * 0.38f),
            IM_COL32(255, 255, 255, 255)
        );

        if (jump) {
            igSetScrollY_Float(igGetScrollMaxY());
        }
    }

    gc_end_scroll_child(
        area_pos, area_size.y, hit_w, child_w, u, allow_drag_scroll, false
    );
}

/* ---- colour emoji picker -------------------------------------------- */

#define GC_TRAY_MAX 12

/* Emoji picked in the chat picker; they are sent after the typed text. */
static int gc_tray[GC_TRAY_MAX];
static int gc_tray_n = 0;

/* Put the picked emoji (as real Unicode) at the end of the message. */
static void gc_tray_flush_into_input(void) {
    for (int i = 0; i < gc_tray_n; i++) {
        unsigned int cp = ntl_emoji_cp_for_index(gc_tray[i]);

        if (cp == 0) {
            continue;
        }

        char enc[8];
        int el = gc_utf8_encode(cp, enc);
        size_t len = strlen(global_chat_input);

        if (len + (size_t)el < sizeof(global_chat_input)) {
            memcpy(global_chat_input + len, enc, (size_t)el + 1);
        }
    }

    gc_tray_n = 0;
}

static float gc_tray_height(float u) {
    return u * 2.0f + u * 0.8f;
}

/* Strip above the input showing the emoji picked so far. Tap one to
 * remove it. */
static void gc_draw_tray(ImVec2 pos, float width, float u) {
    ImDrawList* dl = igGetWindowDrawList();

    float h = gc_tray_height(u);
    float chip_h = u * 2.0f;
    float chip_w = chip_h * 1.15f;
    float pad = u * 0.4f;
    float gx = u * 0.4f;

    ImDrawList_AddRectFilled(
        dl, pos, gc_add(pos, width, h),
        IM_COL32(255, 255, 255, 12), u * 0.8f, 0
    );
    ImDrawList_AddRect(
        dl, pos, gc_add(pos, width, h),
        IM_COL32(190, 160, 255, 55), u * 0.8f, 0, 1.0f
    );

    int remove = -1;

    for (int i = 0; i < gc_tray_n; i++) {
        ImVec2 p = gc_v2(
            pos.x + pad + (float)i * (chip_w + gx), pos.y + pad
        );

        if (p.x + chip_w > pos.x + width - pad) {
            break;
        }

        igSetCursorScreenPos(p);

        char id[16];
        snprintf(id, sizeof(id), "##gctray%d", i);

        bool clicked = igInvisibleButton(
            id, gc_v2(chip_w, chip_h), ImGuiButtonFlags_None
        );

        bool hot = igIsItemHovered(0) || igIsItemActive();

        ImDrawList_AddRectFilled(
            dl, p, gc_add(p, chip_w, chip_h),
            hot ? IM_COL32(255, 255, 255, 46) : IM_COL32(255, 255, 255, 22),
            chip_h * 0.5f, 0
        );

        float es = chip_h * 0.78f;
        ImVec2 a = gc_v2(
            p.x + (chip_w - es) * 0.5f, p.y + (chip_h - es) * 0.5f
        );

        ntl_emoji_draw(
            dl, a, gc_v2(a.x + es, a.y + es), gc_tray[i],
            IM_COL32(255, 255, 255, 255)
        );

        if (clicked) {
            remove = i;
        }
    }

    if (remove >= 0) {
        for (int i = remove; i + 1 < gc_tray_n; i++) {
            gc_tray[i] = gc_tray[i + 1];
        }

        gc_tray_n--;
    }
}

/* Size of an emoji grid for `count` chips in `width`: the same pill chips
 * as before, wrapped into rows. If it is taller than max_h it scrolls. */
static void gc_emoji_grid_layout(
    float width, float max_h, float u, int count,
    float* out_chip_h, int* out_cols, float* out_h
) {
    float pad = u * 0.55f;
    float gx = u * 0.45f;
    float chip_h = u * 2.1f;
    float chip_w = chip_h * 1.15f;

    int cols = (int)((width - pad * 2.0f + gx) / (chip_w + gx));

    if (cols < 1) cols = 1;

    int rows = (count + cols - 1) / cols;
    float full = pad * 2.0f + (float)rows * chip_h + (float)(rows - 1) * gx;

    *out_chip_h = chip_h;
    *out_cols = cols;
    *out_h = full > max_h ? max_h : full;
}

/* The colour emoji of the minimap dots, as a scrolling grid. With
 * usrs == NULL (chat picker) a tap adds the emoji to the message; with
 * usrs set (Settings) a tap chooses the profile emoji, plus a "none" chip. */
static void gc_draw_emoji_grid(
    const char* child_id, ImVec2 pos, ImVec2 size, float u,
    float chip_h, int cols, user_settings* usrs
) {
    ImDrawList* pdl = igGetWindowDrawList();

    ImDrawList_AddRectFilled(
        pdl, pos, gc_add(pos, size.x, size.y),
        IM_COL32(255, 255, 255, 12), u * 0.8f, 0
    );
    ImDrawList_AddRect(
        pdl, pos, gc_add(pos, size.x, size.y),
        IM_COL32(190, 160, 255, 55), u * 0.8f, 0, 1.0f
    );

    igSetCursorScreenPos(pos);

    igBeginChild_Str(
        child_id,
        size,
        0,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground
    );

    if (!ntl_emoji_ready()) {
        igTextDisabled("Emoji pictures are not available.");
        igEndChild();
        return;
    }

    if (
        igIsWindowHovered(0) &&
        !igIsAnyItemActive() &&
        igIsMouseDragging(0, 6.0f)
    ) {
        igSetScrollY_Float(igGetScrollY() - igGetIO_Nil()->MouseDelta.y);
        gc_scroll_dragged = true;
    }

    int total = NTL_EMOJI_COUNT + (usrs != NULL ? 1 : 0);
    float pad = u * 0.55f;
    float gx = u * 0.45f;
    float chip_w = chip_h * 1.15f;
    int rows = (total + cols - 1) / cols;

    ImVec2 base;
    igGetCursorScreenPos(&base);

    ImDrawList* dl = igGetWindowDrawList();

    for (int k = 0; k < total; k++) {
        int idx = usrs != NULL ? k - 1 : k; /* -1 = "none" chip */
        int r = k / cols;
        int c = k % cols;

        ImVec2 p = gc_v2(
            base.x + pad + (float)c * (chip_w + gx),
            base.y + pad + (float)r * (chip_h + gx)
        );

        igSetCursorScreenPos(p);

        char id[16];
        snprintf(id, sizeof(id), "##gcem%d", k);