#include "global_chat.h"

#include "../user.h"
#include "profile_emoji.h"

#include "thermite/tchat.h"
#include "thermite/jsr_network.h"
#include "ntl_team.h"

#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#include <string.h>
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

static char global_chat_input[GLOBAL_CHAT_TEXT_LEN] = "";

static void global_chat_panel_contents(
    tenv* env,
    ImVec2 wp,
    ImVec2 ws
);

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

static void gc_icon_minus(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    ImDrawList_AddLine(
        dl,
        gc_v2(c.x - s * 0.5f, c.y),
        gc_v2(c.x + s * 0.5f, c.y),
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

    gc_total_messages++;

    global_chat_message* message =
        &global_chat_messages[
            global_chat_message_count
        ];

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

    global_chat_add_message(
        msg->username,
        owner,
        msg->message
    );
}

void global_chat_init(tenv* env) {
    global_chat_initialized = true;
    global_chat_open = false;

    global_chat_message_count = 0;

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

    /* Keep it fully on-screen for whatever size ended up in effect. */
    if (fixed_pos.x < viewport->WorkPos.x) {
        fixed_pos.x = viewport->WorkPos.x;
    }
    if (fixed_pos.x > viewport->WorkPos.x + viewport->WorkSize.x - expanded_size.x) {
        fixed_pos.x = viewport->WorkPos.x + viewport->WorkSize.x - expanded_size.x;
    }
    if (fixed_pos.y < viewport->WorkPos.y) {
        fixed_pos.y = viewport->WorkPos.y;
    }
    if (fixed_pos.y > viewport->WorkPos.y + viewport->WorkSize.y - expanded_size.y) {
        fixed_pos.y = viewport->WorkPos.y + viewport->WorkSize.y - expanded_size.y;
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

static void gc_submit_message(tenv* env) {
    if (global_chat_input[0] == '\0') {
        return;
    }

    const char* nickname = env->usr->usrs.nickname;

    if (nickname == NULL || nickname[0] == '\0') {
        nickname = "Player";
    }

    /* Show it immediately for the sender. */
    global_chat_add_message(
        nickname,
        NULL,
        global_chat_input
    );

    /* Relay it to everyone else. */
    if (global_chat_net != NULL) {
        jsr_network_send_message(
            global_chat_net,
            global_chat_input
        );
    }

    memset(global_chat_input, 0, sizeof(global_chat_input));
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

/* Red "needs help" cards for every teammate whose SOS is active, each
 * with a LOCATE button (jumps to the Players tab, highlighting them).
 * Returns how many cards were drawn. */
static int gc_draw_sos_cards(float u, float width, const char* own_name) {
    if (global_chat_net == NULL) {
        return 0;
    }

    int count = jsr_network_location_count(global_chat_net);
    int shown = 0;
    float lh = igGetTextLineHeight();
    float card_h = lh * 1.7f + u * 0.4f;

    for (int i = 0; i < count; i++) {
        char name[32];
        char ip[64];
        bool sos = false;
        int emoji_id = 0;

        name[0] = '\0';
        ip[0] = '\0';

        if (
            !jsr_network_get_location(
                global_chat_net,
                i,
                name,
                sizeof(name),
                ip,
                sizeof(ip),
                NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                &sos,
                &emoji_id
            )
        ) {
            continue;
        }

        if (!sos || name[0] == '\0') {
            continue;
        }

        if (own_name != NULL && strcmp(own_name, name) == 0) {
            continue;
        }

        ImVec2 p;
        igGetCursorScreenPos(&p);

        ImDrawList* dl = igGetWindowDrawList();

        ImDrawList_AddRectFilled(
            dl, p, gc_add(p, width, card_h),
            IM_COL32(224, 56, 44, 56), u * 0.6f, 0
        );
        ImDrawList_AddRect(
            dl, p, gc_add(p, width, card_h),
            IM_COL32(255, 90, 74, 128), u * 0.6f, 0, 1.0f
        );

        ImVec2 icon_c = gc_v2(p.x + u * 1.0f, p.y + card_h * 0.5f);
        gc_icon_sos(
            dl, icon_c, lh * 0.95f,
            IM_COL32(255, 90, 74, 255),
            IM_COL32(60, 20, 20, 255)
        );

        char line[80];
        snprintf(line, sizeof(line), "%s needs help", name);

        ImDrawList_AddText_Vec2(
            dl,
            gc_v2(p.x + u * 1.9f, p.y + (card_h - lh) * 0.5f),
            IM_COL32(255, 214, 208, 255),
            line,
            NULL
        );

        float bw = gc_text_w("LOCATE") + u * 1.2f;
        float bh = card_h * 0.64f;
        ImVec2 bmn = gc_v2(
            p.x + width - bw - u * 0.5f,
            p.y + (card_h - bh) * 0.5f
        );

        char btn_id[24];
        snprintf(btn_id, sizeof(btn_id), "##gc_loc%d", i);

        if (
            gc_button(
                btn_id, bmn, gc_v2(bw, bh),
                IM_COL32(224, 56, 44, 255),
                IM_COL32(245, 85, 70, 255),
                u * 0.4f, 0
            )
        ) {
            strncpy(gc_locate_name, name, sizeof(gc_locate_name) - 1);
            gc_locate_name[sizeof(gc_locate_name) - 1] = '\0';
            gc_tab = GC_TAB_PLAYERS;
        }

        gc_text_center(
            dl, bmn, gc_v2(bw, bh),
            IM_COL32(255, 255, 255, 255), "LOCATE"
        );

        igSetCursorScreenPos(p);
        igDummy(gc_v2(width, card_h + u * 0.35f));

        shown++;
    }

    return shown;
}

/* Roster list (Players tab). Same data the old "Online Players"
 * window used: roster + the location feed for IP / SOS / emoji. */
static void gc_draw_players_list(float u, float width, const char* own_name) {
    (void)own_name;

    int roster_count =
        global_chat_net != NULL ?
            jsr_network_roster_count(global_chat_net) :
            0;

    if (roster_count == 0) {
        igTextDisabled("No players online.");
        return;
    }

    static const ImU32 avatar_cols[6] = {
        IM_COL32(224, 112, 154, 255),
        IM_COL32(56, 182, 165, 255),
        IM_COL32(124, 74, 232, 255),
        IM_COL32(230, 160, 60, 255),
        IM_COL32(77, 140, 255, 255),
        IM_COL32(150, 200, 90, 255)
    };

    float lh = igGetTextLineHeight();
    float row_h = lh * 2.3f;

    for (int i = 0; i < roster_count; i++) {
        char name[32];
        char owner[32];

        if (
            !jsr_network_roster_name(
                global_chat_net, i, name, sizeof(name)
            )
        ) {
            continue;
        }

        owner[0] = '\0';
        jsr_network_roster_owner(
            global_chat_net, i, owner, sizeof(owner)
        );

        char loc_ip[64] = "";
        bool loc_sos = false;
        int loc_emoji_id = 0;

        int loc_count =
            jsr_network_location_count(global_chat_net);

        for (int j = 0; j < loc_count; j++) {
            char loc_name[32];

            if (
                !jsr_network_get_location(
                    global_chat_net,
                    j,
                    loc_name,
                    sizeof(loc_name),
                    loc_ip,
                    sizeof(loc_ip),
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    &loc_sos,
                    &loc_emoji_id
                )
            ) {
                loc_ip[0] = '\0';
                continue;
            }

            if (strcmp(loc_name, name) == 0) {
                break;
            }

            loc_ip[0] = '\0';
            loc_sos = false;
            loc_emoji_id = 0;
        }

        ImVec2 p;
        igGetCursorScreenPos(&p);

        ImDrawList* dl = igGetWindowDrawList();

        bool located =
            gc_locate_name[0] != '\0' &&
            strcmp(gc_locate_name, name) == 0;

        ImDrawList_AddRectFilled(
            dl, p, gc_add(p, width, row_h - 4.0f),
            located ?
                IM_COL32(124, 74, 232, 90) :
                IM_COL32(255, 255, 255, 12),
            u * 0.5f, 0
        );

        if (located) {
            ImDrawList_AddRect(
                dl, p, gc_add(p, width, row_h - 4.0f),
                IM_COL32(190, 160, 255, 160), u * 0.5f, 0, 1.0f
            );
        }

        /* Avatar */
        unsigned hash = 0;
        for (const char* c = name; *c; c++) {
            hash = hash * 31u + (unsigned char)*c;
        }

        ImVec2 ac = gc_v2(p.x + u * 1.1f, p.y + (row_h - 4.0f) * 0.5f);
        ImDrawList_AddCircleFilled(
            dl, ac, lh * 0.8f, avatar_cols[hash % 6u], 20
        );

        char initial[2] = { name[0] != '\0' ? name[0] : '?', '\0' };
        gc_text_center(
            dl,
            gc_v2(ac.x - lh, ac.y - lh),
            gc_v2(lh * 2.0f, lh * 2.0f),
            IM_COL32(255, 255, 255, 255),
            initial
        );

        float tx = p.x + u * 2.4f;
        float ty = p.y + (row_h - 4.0f - lh) * 0.5f;

        if (owner[0] != '\0') {
            ImDrawList_AddText_Vec2(
                dl, gc_v2(tx, ty),
                IM_COL32(242, 77, 77, 255), owner, NULL
            );
            tx += gc_text_w(owner) + u * 0.4f;
        }

        ImDrawList_AddText_Vec2(
            dl, gc_v2(tx, ty),
            IM_COL32(180, 156, 245, 255), name, NULL
        );
        tx += gc_text_w(name) + u * 0.5f;

        const char* loc_emoji = profile_emoji_at(loc_emoji_id);

        if (loc_emoji[0] != '\0') {
            ImDrawList_AddText_Vec2(
                dl, gc_v2(tx, ty),
                IM_COL32(255, 255, 255, 255), loc_emoji, NULL
            );
            tx += gc_text_w(loc_emoji) + u * 0.4f;
        }

        if (loc_sos) {
            ImDrawList_AddText_Vec2(
                dl, gc_v2(tx, ty),
                IM_COL32(255, 70, 55, 255), "SOS", NULL
            );
        }

        if (loc_ip[0] != '\0') {
            float iw = gc_text_w(loc_ip);

            ImDrawList_AddText_Vec2(
                dl,
                gc_v2(p.x + width - iw - u * 0.7f, ty),
                IM_COL32(140, 133, 170, 230),
                loc_ip,
                NULL
            );
        }

        igDummy(gc_v2(width, row_h));
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

    float ts_w = gc_text_w("00:00") + u * 0.8f;

    for (int i = 0; i < global_chat_message_count; i++) {
        global_chat_message* m = &global_chat_messages[i];

        bool is_system = strcmp(m->name, "[SYSTEM]") == 0;
        bool is_own =
            own_name != NULL &&
            own_name[0] != '\0' &&
            strcmp(m->name, own_name) == 0;

        char hm[8];
        snprintf(hm, sizeof(hm), "%.5s", m->time_str);

        igPushID_Int(i);

        igTextColored(
            (ImVec4){0.55f, 0.50f, 0.72f, 1.0f},
            "%s",
            hm
        );

        igSameLine(ts_w, -1.0f);

        if (is_system) {
            igPushStyleColor_Vec4(
                ImGuiCol_Text,
                (ImVec4){0.62f, 0.91f, 1.0f, 1.0f}
            );
            igTextWrapped("%s", m->text);
            igPopStyleColor(1);
        } else {
            if (m->owner[0] != '\0') {
                igTextColored(
                    (ImVec4){0.95f, 0.3f, 0.3f, 1.0f},
                    "%s",
                    m->owner
                );
                igSameLine(0.0f, 6.0f);
            }

            if (is_own) {
                igTextColored(
                    (ImVec4){0.49f, 1.0f, 0.69f, 1.0f},
                    "%s",
                    m->name
                );
            } else {
                igTextColored(
                    (ImVec4){0.71f, 0.61f, 0.96f, 1.0f},
                    "%s",
                    m->name
                );
            }

            igSameLine(0.0f, u * 0.5f);
            igTextWrapped("%s", m->text);
        }

        igPopID();

        igDummy(gc_v2(1.0f, u * 0.2f));
    }

    /* Teammates asking for help show up as cards at the end. */
    gc_draw_sos_cards(u, avail.x, own_name);

    if (
        global_chat_message_count != last_seen_message_count &&
        (was_at_bottom || last_seen_message_count == 0)
    ) {
        /* Only auto-scroll if the player was already at (or near)
         * the bottom. */
        igSetScrollHereY(1.0f);
    }

    last_seen_message_count = global_chat_message_count;

    /* Touch/mouse drag on the list itself scrolls it. */
    if (
        allow_drag_scroll &&
        igIsWindowHovered(0) &&
        !igIsAnyItemActive() &&
        igIsMouseDragging(0, 6.0f)
    ) {
        igSetScrollY_Float(
            igGetScrollY() - igGetIO_Nil()->MouseDelta.y
        );
    }

    float scroll_max = igGetScrollMaxY();
    float scroll_y = igGetScrollY();
    ImGuiWindow* msg_window = igGetCurrentWindow();

    igEndChild();

    gc_message_scrollbar(
        gc_v2(area_pos.x + child_w, area_pos.y),
        area_size.y,
        hit_w,
        area_size.y,
        scroll_max,
        scroll_y,
        msg_window,
        u
    );
}

/* Emoji chips row (the profile emoji picker). Drag sideways to scroll. */
static void gc_draw_emoji_row(
    user_settings* usrs,
    ImVec2 pos,
    ImVec2 size,
    float u,
    bool allow_drag_scroll
) {
    static bool chips_dragged = false;

    igSetCursorScreenPos(pos);

    igBeginChild_Str(
        "##gc_chips",
        size,
        0,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground
    );

    if (igIsMouseClicked_Bool(0, false)) {
        chips_dragged = false;
    }

    if (
        allow_drag_scroll &&
        igIsWindowHovered(0) &&
        igIsMouseDragging(0, 6.0f)
    ) {
        igSetScrollX_Float(
            igGetScrollX() - igGetIO_Nil()->MouseDelta.x
        );
        chips_dragged = true;
    }

    float chip_h = size.y - 4.0f;
    float chip_w = chip_h * 1.15f;
    ImDrawList* dl = igGetWindowDrawList();

    for (int e = 0; e < PROFILE_EMOJI_COUNT; e++) {
        if (e > 0) {
            igSameLine(0.0f, u * 0.45f);
        }

        ImVec2 p;
        igGetCursorScreenPos(&p);

        char id[16];
        snprintf(id, sizeof(id), "##pe%d", e);

        bool clicked =
            igInvisibleButton(
                id,
                gc_v2(chip_w, chip_h),
                ImGuiButtonFlags_None
            );

        bool hot = igIsItemHovered(0);
        bool selected = usrs->profile_emoji_id == e;

        ImU32 fill =
            selected ?
                IM_COL32(77, 140, 255, 255) :
                (hot ?
                    IM_COL32(255, 255, 255, 40) :
                    IM_COL32(255, 255, 255, 20));

        ImDrawList_AddRectFilled(
            dl, p, gc_add(p, chip_w, chip_h),
            fill, chip_h * 0.5f, 0
        );

        const char* label =
            profile_emoji_at(e)[0] != '\0' ?
                profile_emoji_at(e) :
                "-";

        gc_text_center(
            dl, p, gc_v2(chip_w, chip_h),
            IM_COL32(255, 255, 255, 255), label
        );

        if (clicked && !chips_dragged) {
            usrs->profile_emoji_id = e;
            save_user_settings(usrs);
        }
    }

    igEndChild();
}

/* "This server requires an access key" screen. Same logic as before,
 * drawn inside the main area. */
static void gc_draw_key_screen(
    tenv* env,
    user_settings* usrs,
    bool rejected,
    ImVec2 pos,
    ImVec2 size,
    float u
) {
    igSetCursorScreenPos(gc_add(pos, u * 0.9f, u * 0.6f));

    igBeginChild_Str(
        "##gc_keyscreen",
        gc_v2(size.x - u * 1.8f, size.y - u * 0.6f),
        0,
        ImGuiWindowFlags_NoBackground
    );

    igText("This server requires an access key.");

    igTextWrapped(
        "Ask your clan owner for your key, "
        "then enter it below."
    );

    igSpacing();

    static char key_input[96] = "";
    static bool key_input_seeded = false;

    if (!key_input_seeded) {
        strncpy(key_input, usrs->public_chat_key, sizeof(key_input) - 1);
        key_input[sizeof(key_input) - 1] = '\0';
        key_input_seeded = true;
    }

    igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, u * 0.5f);
    igPushStyleColor_Vec4(
        ImGuiCol_FrameBg, (ImVec4){1.0f, 1.0f, 1.0f, 0.10f}
    );

    igPushItemWidth(-1.0f);

    igInputTextWithHint(
        "##public_chat_key_input",
        "Access key",
        key_input,
        sizeof(key_input),
        ImGuiInputTextFlags_None,
        NULL,
        NULL
    );

    igPopItemWidth();
    igPopStyleColor(1);
    igPopStyleVar(1);

    if (rejected) {
        igTextColored(
            (ImVec4){0.9f, 0.3f, 0.3f, 1.0f},
            "%s",
            jsr_network_get_last_error(global_chat_net)
        );
    }

    igSpacing();

    ImVec2 p;
    igGetCursorScreenPos(&p);

    float bh = u * 2.4f;
    float bw;
    {
        ImVec2 av;
        igGetContentRegionAvail(&av);
        bw = av.x;
    }

    if (
        gc_button(
            "##gc_connect",
            p,
            gc_v2(bw, bh),
            IM_COL32(124, 74, 232, 255),
            IM_COL32(150, 100, 250, 255),
            u * 0.6f,
            0
        )
    ) {
        if (key_input[0] != '\0') {
            strncpy(
                usrs->public_chat_key,
                key_input,
                sizeof(usrs->public_chat_key) - 1
            );

            usrs->public_chat_key[
                sizeof(usrs->public_chat_key) - 1
            ] = '\0';

            save_user_settings(usrs);

            global_chat_try_connect(env);
        }
    }

    gc_text_center(
        igGetWindowDrawList(), p, gc_v2(bw, bh),
        IM_COL32(255, 255, 255, 255), "Connect"
    );

    igEndChild();
}

static void global_chat_panel_contents(
    tenv* env,
    ImVec2 wp,
    ImVec2 ws
) {
    if (env == NULL || env->usr == NULL) {
        return;
    }

    user_settings* usrs = &env->usr->usrs;

    /* Same panel text scale as before. (This ImGui version has no
     * SetWindowFontScale -- font size is set via PushFont/PopFont, so
     * every return path below must PopFont() to match.) */
    igPushFont(NULL, igGetStyle()->FontSizeBase * 0.85f);

    ImDrawList* dl = igGetWindowDrawList();

    float u = igGetTextLineHeight();

    bool rejected =
        global_chat_net != NULL &&
        jsr_network_is_auth_rejected(global_chat_net);

    bool needs_key =
        usrs->public_chat_key[0] == '\0' || rejected;

    bool is_connected =
        global_chat_net != NULL &&
        jsr_network_is_connected(global_chat_net);

    int online_count =
        global_chat_net != NULL ?
            jsr_network_roster_count(global_chat_net) :
            0;

    bool adjusting_pos =
        global_chat_adjust == GLOBAL_CHAT_ADJUST_POSITION;
    bool adjusting_size =
        global_chat_adjust == GLOBAL_CHAT_ADJUST_SIZE;

    /* ---------- panel + rail backgrounds ---------- */
    float radius = u * 1.1f;
    float rail_w = gc_clampf(ws.x * 0.126f, u * 2.7f, u * 4.2f);
    float alpha = gc_clampf(gc_panel_alpha, 0.05f, 1.0f);

    ImVec2 pmax = gc_add(wp, ws.x, ws.y);

    ImDrawList_AddRectFilled(
        dl, wp, pmax,
        IM_COL32(28, 16, 52, (int)(255.0f * alpha)),
        radius, 0
    );

    ImDrawList_AddRectFilled(
        dl, wp, gc_v2(wp.x + rail_w, pmax.y),
        IM_COL32(10, 5, 25, (int)(130.0f * alpha)),
        radius, ImDrawFlags_RoundCornersLeft
    );

    ImDrawList_AddRect(
        dl, wp, pmax,
        IM_COL32(190, 160, 255, 105),
        radius, 0, 1.5f
    );

    /* While moving/resizing, make that obvious. */
    if (adjusting_pos || adjusting_size) {
        ImDrawList_AddRect(
            dl, wp, pmax,
            IM_COL32(93, 255, 154, 220),
            radius, 0, 2.5f
        );
    }

    /* ---------- side rail: Chat / Players / SOS / Settings ---------- */
    unsigned long unread =
        gc_total_messages - gc_seen_messages;

    float btn = rail_w * 0.68f;
    float rail_x = wp.x + (rail_w - btn) * 0.5f;
    float rail_y = wp.y + u * 0.9f;
    float rail_gap = u * 0.7f;

    for (int t = 0; t < 4; t++) {
        ImVec2 bmn = gc_v2(
            rail_x,
            rail_y + (float)t * (btn + rail_gap)
        );

        bool on = gc_tab == t;

        if (on) {
            ImDrawList_AddRectFilled(
                dl,
                gc_add(bmn, -3.0f, -3.0f),
                gc_add(bmn, btn + 3.0f, btn + 3.0f),
                IM_COL32(124, 74, 232, 70),
                btn * 0.32f, 0
            );
        }

        if (
            gc_button(
                gc_tab_ids[t],
                bmn,
                gc_v2(btn, btn),
                on ?
                    IM_COL32(124, 74, 232, 255) :
                    IM_COL32(255, 255, 255, 16),
                on ?
                    IM_COL32(140, 90, 245, 255) :
                    IM_COL32(255, 255, 255, 38),
                btn * 0.28f,
                0
            )
        ) {
            gc_tab = t;
        }

        ImVec2 c = gc_add(bmn, btn * 0.5f, btn * 0.5f);
        float is = btn * 0.52f;
        ImU32 ic = IM_COL32(255, 255, 255, 255);

        if (t == GC_TAB_CHAT) {
            gc_icon_chat(dl, c, is, ic);
        } else if (t == GC_TAB_PLAYERS) {
            gc_icon_players(dl, c, is, ic);
        } else if (t == GC_TAB_SOS) {
            gc_icon_sos(
                dl, c, is,
                global_chat_is_sos_active() ?
                    IM_COL32(255, 90, 74, 255) : ic,
                on ?
                    IM_COL32(124, 74, 232, 255) :
                    IM_COL32(44, 30, 78, 255)
            );
        } else {
            gc_icon_gear(dl, c, is, ic);
        }

        if (t == GC_TAB_CHAT && !on && unread > 0) {
            char nb[8];
            snprintf(
                nb, sizeof(nb), "%lu",
                unread > 99 ? 99UL : unread
            );

            float bw = fmaxf(gc_text_w(nb) + u * 0.5f, u * 1.1f);
            ImVec2 bc = gc_v2(bmn.x + btn - bw * 0.35f, bmn.y + bw * 0.1f);

            ImDrawList_AddRectFilled(
                dl,
                gc_v2(bc.x - bw * 0.5f, bc.y - u * 0.55f),
                gc_v2(bc.x + bw * 0.5f, bc.y + u * 0.55f),
                IM_COL32(255, 74, 94, 255),
                u * 0.55f, 0
            );

            gc_text_center(
                dl,
                gc_v2(bc.x - bw * 0.5f, bc.y - u * 0.55f),
                gc_v2(bw, u * 1.1f),
                IM_COL32(255, 255, 255, 255),
                nb
            );
        }
    }

    if (gc_tab == GC_TAB_CHAT) {
        gc_seen_messages = gc_total_messages;
    }

    /* ---------- main area geometry ---------- */
    float mx = wp.x + rail_w;
    float mw = ws.x - rail_w;
    float pad = u * 0.95f;

    float head_h = gc_clampf(ws.y * 0.095f, u * 2.7f, u * 3.8f);
    float foot_h = gc_clampf(ws.y * 0.058f, u * 1.9f, u * 2.7f);
    float foot_y = wp.y + ws.y - foot_h - u * 0.85f;

    /* ---------- header ---------- */
    igPushFont(NULL, igGetStyle()->FontSizeBase * 0.85f * 1.12f);

    ImDrawList_AddText_Vec2(
        dl,
        gc_v2(mx + pad, wp.y + head_h * 0.1f),
        IM_COL32(241, 236, 255, 255),
        "{ J S R } TEAM CHAT",
        NULL
    );

    igPopFont();

    {
        char status[64];

        if (is_connected) {
            snprintf(
                status, sizeof(status),
                "Connected  |  %d online", online_count
            );
        } else {
            snprintf(status, sizeof(status), "Not connected");
        }

        ImU32 scol =
            is_connected ?
                IM_COL32(125, 255, 176, 255) :
                IM_COL32(242, 77, 77, 255);

        float sy = wp.y + head_h * 0.1f + u * 1.35f;

        ImDrawList_AddCircleFilled(
            dl,
            gc_v2(mx + pad + u * 0.3f, sy + u * 0.5f),
            u * 0.26f,
            scol,
            12
        );

        ImDrawList_AddText_Vec2(
            dl,
            gc_v2(mx + pad + u * 0.95f, sy),
            scol,
            status,
            NULL
        );
    }

    /* minimize + close (both collapse back to the Open card) */
    {
        float hb = head_h * 0.62f;
        float by = wp.y + (head_h - hb) * 0.5f;
        float x_close = pmax.x - pad * 0.5f - hb;
        float x_min = x_close - hb - u * 0.1f;

        if (
            gc_button(
                "##gc_min",
                gc_v2(x_min, by),
                gc_v2(hb, hb),
                IM_COL32(255, 255, 255, 0),
                IM_COL32(255, 255, 255, 30),
                hb * 0.3f, 0
            )
        ) {
            gc_close_requested = true;
        }

        gc_icon_minus(
            dl, gc_v2(x_min + hb * 0.5f, by + hb * 0.5f),
            hb * 0.45f, IM_COL32(203, 182, 255, 255)
        );

        if (
            gc_button(
                "##gc_close",
                gc_v2(x_close, by),
                gc_v2(hb, hb),
                IM_COL32(255, 255, 255, 0),
                IM_COL32(255, 255, 255, 30),
                hb * 0.3f, 0
            )
        ) {
            gc_close_requested = true;
        }

        gc_icon_x(
            dl, gc_v2(x_close + hb * 0.5f, by + hb * 0.5f),
            hb * 0.42f, IM_COL32(203, 182, 255, 255)
        );
    }

    ImDrawList_AddLine(
        dl,
        gc_v2(mx, wp.y + head_h),
        gc_v2(pmax.x - 1.0f, wp.y + head_h),
        IM_COL32(190, 160, 255, 52),
        1.0f
    );

    /* ---------- footer: RESIZE / OPACITY / SOS ---------- */
    {
        float gap = u * 0.55f;
        float bw = (mw - pad * 2.0f - gap * 2.0f) / 3.0f;
        float bx = mx + pad;

        /* RESIZE (becomes DONE while resizing, which saves) */
        if (
            gc_button(
                "##gc_f_resize",
                gc_v2(bx, foot_y),
                gc_v2(bw, foot_h),
                adjusting_size ?
                    IM_COL32(40, 160, 90, 255) :
                    IM_COL32(255, 255, 255, 18),
                adjusting_size ?
                    IM_COL32(55, 190, 110, 255) :
                    IM_COL32(255, 255, 255, 40),
                u * 0.6f, 0
            )
        ) {
            gc_toggle_adjust(env, GLOBAL_CHAT_ADJUST_SIZE);
        }

        gc_text_center(
            dl, gc_v2(bx, foot_y), gc_v2(bw, foot_h),
            IM_COL32(241, 236, 255, 255),
            adjusting_size ? "DONE" : "RESIZE"
        );

        /* OPACITY (cycles the panel's see-through level) */
        float ox = bx + bw + gap;

        if (
            gc_button(
                "##gc_f_opacity",
                gc_v2(ox, foot_y),
                gc_v2(bw, foot_h),
                IM_COL32(255, 255, 255, 18),
                IM_COL32(255, 255, 255, 40),
                u * 0.6f, 0
            )
        ) {
            static const float steps[4] = {0.15f, 0.40f, 0.65f, 0.82f};
            int next = 0;

            for (int k = 0; k < 4; k++) {
                if (gc_panel_alpha < steps[k] - 0.01f) {
                    next = k;
                    break;
                }

                next = (k + 1) % 4;
            }

            gc_panel_alpha = steps[next];
        }

        gc_text_center(
            dl, gc_v2(ox, foot_y), gc_v2(bw, foot_h),
            IM_COL32(241, 236, 255, 255), "OPACITY"
        );

        /* SOS toggle */
        float sx = ox + bw + gap;
        bool sos_on = global_chat_is_sos_active();

        if (
            gc_button(
                "##gc_f_sos",
                gc_v2(sx, foot_y),
                gc_v2(bw, foot_h),
                sos_on ?
                    IM_COL32(255, 70, 55, 255) :
                    IM_COL32(224, 56, 44, 255),
                IM_COL32(245, 90, 76, 255),
                u * 0.6f, 0
            )
        ) {
            gc_toggle_sos();
        }

        char sos_label[24];

        if (sos_on) {
            snprintf(
                sos_label, sizeof(sos_label), "SOS %llds",
                (long long)(global_chat_sos_remaining_ms() / 1000)
            );
        } else {
            snprintf(sos_label, sizeof(sos_label), "SOS");
        }

        gc_text_center(
            dl, gc_v2(sx, foot_y), gc_v2(bw, foot_h),
            IM_COL32(255, 255, 255, 255), sos_label
        );
    }

    /* ---------- tab content ---------- */
    float content_top = wp.y + head_h + u * 0.35f;
    float content_bot = foot_y - u * 0.55f;
    float area_x = mx + pad * 0.6f;
    float area_w = mw - pad * 1.2f;

    if (needs_key) {
        gc_draw_key_screen(
            env, usrs, rejected,
            gc_v2(mx, content_top),
            gc_v2(mw, content_bot - content_top),
            u
        );
    } else if (gc_tab == GC_TAB_CHAT) {
        float in_h = gc_clampf(ws.y * 0.07f, u * 2.3f, u * 3.1f);
        float in_y = content_bot - in_h;
        float chip_h = u * 1.95f;
        float chip_y = in_y - u * 0.45f - chip_h;
        float msg_bot = chip_y - u * 0.3f;

        gc_draw_messages(
            env,
            gc_v2(area_x, content_top),
            gc_v2(area_w, msg_bot - content_top),
            u,
            !adjusting_pos
        );

        gc_draw_emoji_row(
            usrs,
            gc_v2(mx + pad, chip_y),
            gc_v2(mw - pad * 2.0f, chip_h),
            u,
            !adjusting_pos
        );

        /* Message input + send */
        float gap = u * 0.55f;
        float in_w = mw - pad * 2.0f - in_h - gap;

        igSetCursorScreenPos(gc_v2(mx + pad, in_y));

        igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, u * 0.7f);
        igPushStyleVar_Vec2(
            ImGuiStyleVar_FramePadding,
            gc_v2(u * 0.9f, (in_h - u) * 0.5f)
        );
        igPushStyleColor_Vec4(
            ImGuiCol_FrameBg, (ImVec4){1.0f, 1.0f, 1.0f, 0.10f}
        );
        igPushStyleColor_Vec4(
            ImGuiCol_FrameBgHovered, (ImVec4){1.0f, 1.0f, 1.0f, 0.15f}
        );
        igPushStyleColor_Vec4(
            ImGuiCol_FrameBgActive, (ImVec4){1.0f, 1.0f, 1.0f, 0.18f}
        );

        igPushItemWidth(in_w);

        bool submitted =
            igInputTextWithHint(
                "##global_chat_input",
                "Type your message...",
                global_chat_input,
                sizeof(global_chat_input),
                ImGuiInputTextFlags_EnterReturnsTrue,
                NULL,
                NULL
            );

        igPopItemWidth();
        igPopStyleColor(3);
        igPopStyleVar(2);

        ImVec2 smn = gc_v2(mx + mw - pad - in_h, in_y);

        bool send_clicked =
            gc_button(
                "##gc_send",
                smn,
                gc_v2(in_h, in_h),
                IM_COL32(124, 74, 232, 255),
                IM_COL32(150, 100, 250, 255),
                u * 0.7f, 0
            );

        gc_icon_send(
            dl,
            gc_v2(smn.x + in_h * 0.5f, smn.y + in_h * 0.5f),
            in_h * 0.38f,
            IM_COL32(255, 255, 255, 255)
        );

        if (submitted || send_clicked) {
            gc_submit_message(env);
        }
    } else if (gc_tab == GC_TAB_PLAYERS) {
        igSetCursorScreenPos(gc_v2(area_x, content_top));

        igBeginChild_Str(
            "##gc_players",
            gc_v2(area_w, content_bot - content_top),
            0,
            ImGuiWindowFlags_NoBackground
        );

        char hdr[48];
        snprintf(hdr, sizeof(hdr), "Online players (%d)", online_count);
        igTextColored((ImVec4){0.71f, 0.61f, 0.96f, 1.0f}, "%s", hdr);
        igDummy(gc_v2(1.0f, u * 0.2f));

        ImVec2 av;
        igGetContentRegionAvail(&av);

        gc_draw_players_list(u, av.x, usrs->nickname);

        igEndChild();
    } else if (gc_tab == GC_TAB_SOS) {
        igSetCursorScreenPos(gc_v2(area_x, content_top));

        igBeginChild_Str(
            "##gc_sos",
            gc_v2(area_w, content_bot - content_top),
            0,
            ImGuiWindowFlags_NoBackground
        );

        bool sos_on = global_chat_is_sos_active();

        ImVec2 av;
        igGetContentRegionAvail(&av);

        if (sos_on) {
            igTextColored(
                (ImVec4){1.0f, 0.45f, 0.38f, 1.0f},
                "Your SOS is active - %llds left",
                (long long)(global_chat_sos_remaining_ms() / 1000)
            );
        } else {
            igTextDisabled("Your SOS is off.");
        }

        igTextWrapped(
            "SOS lets every teammate see you need help, on any server."
        );

        igDummy(gc_v2(1.0f, u * 0.3f));

        ImVec2 p;
        igGetCursorScreenPos(&p);

        float bh = u * 3.0f;

        if (
            gc_button(
                "##gc_sos_big",
                p,
                gc_v2(av.x, bh),
                sos_on ?
                    IM_COL32(120, 40, 36, 255) :
                    IM_COL32(224, 56, 44, 255),
                sos_on ?
                    IM_COL32(150, 55, 48, 255) :
                    IM_COL32(245, 90, 76, 255),
                u * 0.8f, 0
            )
        ) {
            gc_toggle_sos();
        }

        gc_text_center(
            igGetWindowDrawList(), p, gc_v2(av.x, bh),
            IM_COL32(255, 255, 255, 255),
            sos_on ? "CANCEL SOS" : "SEND SOS (5 min)"
        );

        igSetCursorScreenPos(p);
        igDummy(gc_v2(av.x, bh + u * 0.6f));

        igTextColored(
            (ImVec4){0.71f, 0.61f, 0.96f, 1.0f},
            "Teammates asking for help"
        );
        igDummy(gc_v2(1.0f, u * 0.2f));

        if (gc_draw_sos_cards(u, av.x, usrs->nickname) == 0) {
            igTextDisabled("Nobody needs help right now.");
        }

        igEndChild();
    } else {
        /* Settings */
        igSetCursorScreenPos(gc_v2(area_x, content_top));

        igBeginChild_Str(
            "##gc_settings",
            gc_v2(area_w, content_bot - content_top),
            0,
            ImGuiWindowFlags_NoBackground
        );

        ImVec2 av;
        igGetContentRegionAvail(&av);

        float bh = u * 2.5f;
        float gap = u * 0.5f;
        float bw = (av.x - gap) * 0.5f;

        igTextColored(
            (ImVec4){0.71f, 0.61f, 0.96f, 1.0f},
            "Window"
        );
        igDummy(gc_v2(1.0f, u * 0.15f));

        ImVec2 p;
        igGetCursorScreenPos(&p);

        if (
            gc_button(
                "##gc_s_move",
                p,
                gc_v2(bw, bh),
                adjusting_pos ?
                    IM_COL32(40, 160, 90, 255) :
                    IM_COL32(255, 255, 255, 20),
                adjusting_pos ?
                    IM_COL32(55, 190, 110, 255) :
                    IM_COL32(255, 255, 255, 42),
                u * 0.6f, 0
            )
        ) {
            gc_toggle_adjust(env, GLOBAL_CHAT_ADJUST_POSITION);
        }

        gc_text_center(
            igGetWindowDrawList(), p, gc_v2(bw, bh),
            IM_COL32(241, 236, 255, 255),
            adjusting_pos ? "DONE MOVING" : "MOVE"
        );

        ImVec2 p2 = gc_v2(p.x + bw + gap, p.y);

        if (
            gc_button(
                "##gc_s_size",
                p2,
                gc_v2(bw, bh),
                adjusting_size ?
                    IM_COL32(40, 160, 90, 255) :
                    IM_COL32(255, 255, 255, 20),
                adjusting_size ?
                    IM_COL32(55, 190, 110, 255) :
                    IM_COL32(255, 255, 255, 42),
                u * 0.6f, 0
            )
        ) {
            gc_toggle_adjust(env, GLOBAL_CHAT_ADJUST_SIZE);
        }

        gc_text_center(
            igGetWindowDrawList(), p2, gc_v2(bw, bh),
            IM_COL32(241, 236, 255, 255),
            adjusting_size ? "DONE SIZING" : "RESIZE"
        );

        igSetCursorScreenPos(p);
        igDummy(gc_v2(av.x, bh + u * 0.5f));

        if (adjusting_pos) {
            igTextWrapped(
                "Drag the chat anywhere, then press DONE MOVING."
            );
        } else if (adjusting_size) {
            igTextWrapped(
                "Drag the bottom-right corner, then press DONE SIZING."
            );
        }

        igDummy(gc_v2(1.0f, u * 0.3f));

        igTextColored(
            (ImVec4){0.71f, 0.61f, 0.96f, 1.0f},
            "Panel opacity"
        );

        igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, u * 0.5f);
        igPushStyleColor_Vec4(
            ImGuiCol_FrameBg, (ImVec4){1.0f, 1.0f, 1.0f, 0.10f}
        );
        igPushItemWidth(-1.0f);

        igSliderFloat(
            "##gc_alpha",
            &gc_panel_alpha,
            0.10f,
            1.0f,
            "%.2f",
            0
        );

        igPopItemWidth();
        igPopStyleColor(1);
        igPopStyleVar(1);

        igEndChild();
    }

    igPopFont();
}

/*
 * Draws a dot for every other Public Chat player who is on
 * the SAME game server as us (positions from different
 * servers aren't in the same coordinate space, so they
 * can't be meaningfully compared). Meant to be called
 * right after ntl_team_draw_minimap() with the exact same
 * x/y/size, so the dots line up on the same circle.
 */
void global_chat_draw_minimap_markers(
    tenv* env,
    float x,
    float y,
    float size
) {
    if (
        env == NULL ||
        env->usr == NULL ||
        size <= 0.0f ||
        global_chat_net == NULL
    ) {
        return;
    }

    tuser_data* u = env->usr;
    game_data* g = &u->gdata;

    if (!u->usrs.ntl_show_teammates) {
        return;
    }

    if (
        g->conn != CONNECTED ||
        g->data.grd <= 0.0f
    ) {
        return;
    }

    ImDrawList* dl = igGetWindowDrawList();

    if (dl == NULL) {
        return;
    }

    float radius = size * 0.5f;
    float map_radius = radius * 0.90f;
    ImVec2 center = {
        x + radius,
        y + radius
    };

    int count =
        jsr_network_location_count(
            global_chat_net
        );

    for (
        int i = 0;
        i < count;
        i++
    ) {
        char srv[64];
        char pname[32];
        float lx;
        float ly;
        int shape;
        float cr, cg, cb;
        bool sos;

        if (
            !jsr_network_get_location(
                global_chat_net,
                i,
                pname,
                sizeof(pname),
                srv,
                sizeof(srv),
                &lx,
                &ly,
                &shape,
                &cr,
                &cg,
                &cb,
                NULL,
                NULL,
                &sos,
                NULL
            )
        ) {
            continue;
        }

        /* Each teammate's own chosen marker color/shape --
         * see jsr_network_send_location -- rather than a
         * single local override, so e.g. a blue diamond they
         * picked shows up as a blue diamond here. */
        ImU32 col =
            igColorConvertFloat4ToU32(
                (ImVec4){cr, cg, cb, 1.0f}
            );

        if (
            strcmp(srv, u->usrs.ipv4) != 0
        ) {
            /* Different game server -- not comparable. */
            continue;
        }

        float rx =
            (lx - g->data.grd) / g->data.flux_grd;

        float ry =
            (ly - g->data.grd) / g->data.flux_grd;

        float dist =
            sqrtf(rx * rx + ry * ry);

        if (dist > 1.0f) {
            rx /= dist;
            ry /= dist;
        }

        ImVec2 p = {
            center.x + rx * map_radius,
            center.y + ry * map_radius
        };

        ntl_draw_marker(
            dl,
            p,
            u->usrs.ntl_marker_size,
            shape,
            col
        );

        if (sos) {
            /* Pulsing red ring around an SOS teammate's dot -- a plain
             * sine wave on wall-clock time so it's visible without
             * needing per-frame animation state. */
            float pulse =
                0.5f + 0.5f * sinf((float)global_chat_now_ms() * 0.006f);

            ImDrawList_AddCircle(
                dl,
                p,
                u->usrs.ntl_marker_size + 3.0f + pulse * 2.0f,
                IM_COL32(255, 40, 30, 200),
                0,
                2.0f
            );
        }

        if (u->usrs.ntl_marker_labels) {
            igPushFont(
                u->imgui_data.mono_font[FONT_SIZE_SMALL],
                u->imgui_data.mono_font[
                    FONT_SIZE_SMALL
                ]->LegacySize
            );

            ImVec2 text_size;

            igCalcTextSize(
                &text_size,
                pname,
                NULL,
                false,
                -1.0f
            );

            ImVec2 text_pos = {
                p.x - text_size.x * 0.5f,
                p.y + u->usrs.ntl_marker_size + 2.0f
            };

            ImDrawList_AddText_Vec2(
                dl,
                text_pos,
                IM_COL32(255, 255, 255, 225),
                pname,
                NULL
            );

            igPopFont();
        }
    }
}

jsr_network* global_chat_get_network(void) {
    return global_chat_net;
}

bool global_chat_is_teammate(
    const char* nickname
) {
    if (
        global_chat_net == NULL ||
        nickname == NULL ||
        nickname[0] == '\0'
    ) {
        return false;
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
            strcmp(name, nickname) == 0
        ) {
            return true;
        }
    }

    return false;
}

int global_chat_get_teammates(
    tenv* env,
    global_chat_teammate* out_teammates,
    int max_count
) {
    if (
        global_chat_net == NULL ||
        env == NULL ||
        env->usr == NULL ||
        out_teammates == NULL ||
        max_count <= 0
    ) {
        return 0;
    }

    const char* own_server_ip = env->usr->usrs.ipv4;

    int count =
        jsr_network_location_count(
            global_chat_net
        );

    int written = 0;

    for (
        int i = 0;
        i < count && written < max_count;
        i++
    ) {
        char username[32];
        char server_ip[64];
        int shape;
        float r, g, b;
        int score;
        int ping;
        bool sos;
        int emoji_id;

        if (
            !jsr_network_get_location(
                global_chat_net,
                i,
                username,
                sizeof(username),
                server_ip,
                sizeof(server_ip),
                NULL,
                NULL,
                &shape,
                &r,
                &g,
                &b,
                &score,
                &ping,
                &sos,
                &emoji_id
            )
        ) {
            continue;
        }

        /* Only players we're actually teamed up with (authenticated
         * on the shared relay key), on the same game server as us --
         * positions/scores from a different match aren't meaningful
         * to compare against, same filter used for the minimap
         * markers. Distance is deliberately NOT filtered here. */
        if (
            !global_chat_is_teammate(username) ||
            strcmp(server_ip, own_server_ip) != 0
        ) {
            continue;
        }

        strncpy(
            out_teammates[written].nickname,
            username,
            sizeof(out_teammates[written].nickname) - 1
        );
        out_teammates[written].nickname[
            sizeof(out_teammates[written].nickname) - 1
        ] = '\0';
        out_teammates[written].shape = shape;
        out_teammates[written].color[0] = r;
        out_teammates[written].color[1] = g;
        out_teammates[written].color[2] = b;
        out_teammates[written].score = score;
        out_teammates[written].ping = ping;
        out_teammates[written].sos = sos;
        out_teammates[written].emoji_id = emoji_id;
        written++;
    }

    return written;
}

void global_chat_destroy(tenv* env) {
    (void)env;

    global_chat_initialized =
        false;

    global_chat_open =
        false;

    global_chat_message_count =
        0;

    memset(
        global_chat_input,
        0,
        sizeof(global_chat_input)
    );

    if (global_chat_net != NULL) {
        jsr_network_destroy(global_chat_net);
        global_chat_net = NULL;
    }

    if (global_chat_tchat != NULL) {
        tchat_destroy(global_chat_tchat);
        global_chat_tchat = NULL;
    }
}
