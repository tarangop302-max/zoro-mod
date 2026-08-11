#include "global_chat.h"

#include "../user.h"

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
static bool global_chat_players_open = false;

/* Networking state for the public/global room. */
static tchat_system* global_chat_tchat = NULL;
static jsr_network* global_chat_net = NULL;

static global_chat_message
    global_chat_messages[GLOBAL_CHAT_MAX_MESSAGES];

static int global_chat_message_count = 0;

static char global_chat_input[GLOBAL_CHAT_TEXT_LEN] = "";

static void global_chat_panel_contents(
    tenv* env,
    ImVec2 live_size
);

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
        "%H:%M:%S",
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

    global_chat_add_message(
        "ZORO",
        NULL,
        "Welcome to Public Chat!"
    );

    global_chat_add_message(
        "ZORO",
        NULL,
        "Everyone using this mod can chat here."
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
                    jsr_network_send_location(
                        global_chat_net,
                        s->xx + s->fx,
                        s->yy + s->fy,
                        env->usr->usrs.ipv4
                    );

                    break;
                }
            }
        }
    }
}

void global_chat_draw(tenv* env) {
    if (!global_chat_initialized) {
        return;
    }

    ImGuiViewport* viewport =
        igGetMainViewport();

    /*
     * One window, one persistent ID ("##zoro_chat_window"),
     * used for both the small collapsed button and the
     * full expanded chat box. Because the ID never changes,
     * ImGui keeps the same position across the transition --
     * so dragging it (by its title bar, which already works
     * reliably, unlike a hand-rolled per-widget drag) moves
     * the "button" and the chat box together as one thing.
     */
    const char* title =
        global_chat_open ?
            "{ J S R } TEAM CHAT##zoro_chat_window" :
            "PUBLIC CHAT##zoro_chat_window";

    ImVec2 collapsed_size = {
        150.0f,
        74.0f
    };

    /*
     * Same overall footprint as before, reshaped to a wider,
     * shorter box (matches the target layout) so the header
     * row ("Connected to relay ... Show players") has enough
     * width to lay out without the button clipping.
     */
    ImVec2 expanded_size = {
        540.0f,
        373.0f
    };

    if (expanded_size.x > viewport->WorkSize.x - 36.0f) {
        expanded_size.x = viewport->WorkSize.x - 36.0f;
    }

    if (expanded_size.y > viewport->WorkSize.y - 36.0f) {
        expanded_size.y = viewport->WorkSize.y - 36.0f;
    }

    ImVec2 fixed_pos = {
        viewport->WorkPos.x + 18.0f,
        viewport->WorkPos.y + 18.0f
    };

    /*
     * Position and size are forced every frame (no
     * FirstUseEver here, no drag) -- the window is pinned
     * to the top-left corner permanently.
     */
    igSetNextWindowPos(
        fixed_pos,
        ImGuiCond_Always,
        (ImVec2){0.0f, 0.0f}
    );

    igSetNextWindowSize(
        global_chat_open ?
            expanded_size :
            collapsed_size,
        ImGuiCond_Always
    );

    igSetNextWindowBgAlpha(
        global_chat_open ? 0.1f : 0.85f
    );

    bool open = true;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize;

    if (!global_chat_open) {
        flags |=
            ImGuiWindowFlags_NoScrollbar;
    }

    if (
        igBegin(
            title,
            global_chat_open ? &open : NULL,
            flags
        )
    ) {
        ImVec2 live_pos;
        ImVec2 live_size;

        igGetWindowPos(&live_pos);
        igGetWindowSize(&live_size);

#ifdef ANDROID
        /*
         * Live position/size so touch capture follows this
         * window wherever it gets dragged to, in either
         * state.
         */
        android_ui_capture_rect(
            live_pos.x,
            live_pos.y,
            live_pos.x + live_size.x,
            live_pos.y + live_size.y
        );
#endif

        if (!global_chat_open) {
            if (
                igButton(
                    "Open",
                    (ImVec2){
                        -1.0f,
                        0.0f
                    }
                )
            ) {
                global_chat_open = true;
            }
        } else {
            global_chat_panel_contents(
                env,
                live_size
            );
        }
    }

    igEnd();

    if (
        global_chat_open &&
        !open
    ) {
        global_chat_open = false;
    }

    if (
        global_chat_open &&
        global_chat_players_open
    ) {
        ImVec2 players_pos = {
            fixed_pos.x,
            fixed_pos.y +
                expanded_size.y +
                8.0f
        };

        ImVec2 players_size = {
            expanded_size.x,
            180.0f
        };

        igSetNextWindowPos(
            players_pos,
            ImGuiCond_Always,
            (ImVec2){0.0f, 0.0f}
        );

        igSetNextWindowSize(
            players_size,
            ImGuiCond_Always
        );

        igSetNextWindowBgAlpha(
            0.1f
        );

        ImGuiWindowFlags players_flags =
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize;

        if (
            igBegin(
                "Online Players##zoro_chat_players",
                NULL,
                players_flags
            )
        ) {
#ifdef ANDROID
            ImVec2 pl_pos;
            ImVec2 pl_size;

            igGetWindowPos(&pl_pos);
            igGetWindowSize(&pl_size);

            android_ui_capture_rect(
                pl_pos.x,
                pl_pos.y,
                pl_pos.x + pl_size.x,
                pl_pos.y + pl_size.y
            );
#endif

            int roster_count =
                global_chat_net != NULL ?
                    jsr_network_roster_count(
                        global_chat_net
                    ) :
                    0;

            if (roster_count == 0) {
                igTextDisabled(
                    "No players online."
                );
            } else {
                for (
                    int i = 0;
                    i < roster_count;
                    i++
                ) {
                    char name[32];
                    char owner[32];

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

                    owner[0] = '\0';

                    jsr_network_roster_owner(
                        global_chat_net,
                        i,
                        owner,
                        sizeof(owner)
                    );

                    if (owner[0] != '\0') {
                        igTextColored(
                            (ImVec4){
                                0.95f,
                                0.3f,
                                0.3f,
                                1.0f
                            },
                            "%s",
                            owner
                        );

                        igSameLine(
                            0.0f,
                            6.0f
                        );
                    }

                    igTextColored(
                        (ImVec4){
                            0.25f,
                            0.75f,
                            1.0f,
                            1.0f
                        },
                        "%s",
                        name
                    );
                }
            }
        }

        igEnd();
    }
}

static void global_chat_panel_contents(
    tenv* env,
    ImVec2 live_size
) {
    if (env == NULL || env->usr == NULL) {
        return;
    }

    user_settings* usrs = &env->usr->usrs;

    /* Target design uses noticeably larger, easier-to-read text than
     * ImGui's default size for this panel. SetWindowFontScale() is
     * obsolete in this ImGui version, so scale via PushFont() instead;
     * every return path below must PopFont() to match. */
    igPushFont(NULL, igGetStyle()->FontSizeBase * 1.15f);

    bool rejected =
        global_chat_net != NULL &&
        jsr_network_is_auth_rejected(
            global_chat_net
        );

    if (usrs->public_chat_key[0] == '\0' ||
        rejected) {
        igText(
            "This server requires an access key."
        );

        igTextWrapped(
            "Ask your clan owner for your key, "
            "then enter it below."
        );

        igSeparator();

        static char key_input[96] = "";
        static bool key_input_seeded = false;

        if (!key_input_seeded) {
            strncpy(
                key_input,
                usrs->public_chat_key,
                sizeof(key_input) - 1
            );

            key_input[
                sizeof(key_input) - 1
            ] = '\0';

            key_input_seeded = true;
        }

        igPushItemWidth(
            live_size.x - 30.0f
        );

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

        if (rejected) {
            igTextColored(
                (ImVec4){
                    0.9f,
                    0.3f,
                    0.3f,
                    1.0f
                },
                "%s",
                jsr_network_get_last_error(
                    global_chat_net
                )
            );
        }

        if (
            igButton(
                "Connect",
                (ImVec2){
                    -1.0f,
                    0.0f
                }
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

        igPopFont();
        return;
    }

    bool is_connected =
        global_chat_net != NULL &&
        jsr_network_is_connected(
            global_chat_net
        );

    ImVec2 row_avail;
    igGetContentRegionAvail(&row_avail);
    float row_width = row_avail.x;

    ImVec2 row_start_screen;
    igGetCursorScreenPos(&row_start_screen);

    if (is_connected) {
        igTextColored(
            (ImVec4){
                0.3f,
                0.9f,
                0.3f,
                1.0f
            },
            "Connected to relay"
        );
    } else {
        igTextColored(
            (ImVec4){
                0.9f,
                0.3f,
                0.3f,
                1.0f
            },
            "Not connected"
        );
    }

    igSameLine(
        0.0f,
        10.0f
    );

    int online_count =
        global_chat_net != NULL ?
            jsr_network_roster_count(
                global_chat_net
            ) :
            0;

    igTextDisabled(
        "%d online",
        online_count
    );

    const char* players_label =
        global_chat_players_open ?
            "Hide players" :
            "Show players";

    ImGuiStyle* style = igGetStyle();
    ImVec2 label_size;
    igCalcTextSize(
        &label_size,
        players_label,
        NULL,
        false,
        -1.0f
    );

    float button_w =
        label_size.x +
        style->FramePadding.x * 2.0f +
        16.0f;

    /*
     * Right-align the button, but never let it land on top of
     * the status text to its left: floor it at "wherever the
     * text on this row actually ended, plus a gap" -- measured
     * from the last item's screen rect, not GetCursorPosX()
     * (which resets to the line start, not the previous item's
     * end). A long "Connected to relay N online" line pushes
     * the button onto its own row instead of drawing under it.
     */
    ImVec2 text_end_screen;
    igGetItemRectMax(&text_end_screen);
    float text_end_x = text_end_screen.x - row_start_screen.x;

    float target_x = row_width - button_w;
    if (target_x < text_end_x + 10.0f) {
        target_x = text_end_x + 10.0f;
    }

    igSameLine(
        target_x,
        -1.0f
    );

    if (
        igButton(
            players_label,
            (ImVec2){
                button_w,
                0.0f
            }
        )
    ) {
        global_chat_players_open =
            !global_chat_players_open;
    }

    igSeparator();

    float input_height =
        50.0f;

    ImVec2 avail;
    igGetContentRegionAvail(&avail);

    float message_area_height =
        avail.y -
        input_height;

    if (
        message_area_height <
        80.0f
    ) {
        message_area_height =
            80.0f;
    }

    igPushStyleVar_Float(
        ImGuiStyleVar_ScrollbarSize,
        22.0f
    );

    igPushStyleVar_Float(
        ImGuiStyleVar_GrabMinSize,
        40.0f
    );

    igBeginChild_Str(
        "##global_chat_messages",
        (ImVec2){
            0.0f,
            message_area_height
        },
        true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar
    );

    static int last_seen_message_count = 0;

    ImVec2 msg_area_avail;
    igGetContentRegionAvail(&msg_area_avail);
    float msg_row_width = msg_area_avail.x;

    bool was_at_bottom =
        igGetScrollY() >=
        igGetScrollMaxY() - 1.0f;

    for (
        int i = 0;
        i < global_chat_message_count;
        i++
    ) {
        global_chat_message* message =
            &global_chat_messages[i];

        bool is_system =
            strcmp(
                message->name,
                "[SYSTEM]"
            ) == 0;

        /* Right-aligned timestamp, drawn first so the name/text below
         * can simply flow left without worrying about its width. */
        ImVec2 time_size;
        igCalcTextSize(
            &time_size,
            message->time_str,
            NULL,
            false,
            -1.0f
        );

        float time_x = msg_row_width - time_size.x;
        if (time_x < 0.0f) time_x = 0.0f;

        if (is_system) {
            /*
             * Label + timestamp on their own line (the label is
             * always short, so it can never collide with the
             * timestamp), then the actual system text wrapped
             * below -- same pattern as player messages use, so
             * a long message never overlaps the timestamp.
             */
            igTextColored(
                (ImVec4){
                    0.3f,
                    0.85f,
                    0.95f,
                    1.0f
                },
                "%s:",
                message->name
            );

            igSameLine(
                time_x,
                -1.0f
            );

            igTextDisabled(
                "%s",
                message->time_str
            );

            igTextWrapped(
                "%s",
                message->text
            );

            igDummy(
                (ImVec2){0.0f, 8.0f}
            );

            continue;
        }

        if (message->owner[0] != '\0') {
            igTextColored(
                (ImVec4){
                    0.95f,
                    0.3f,
                    0.3f,
                    1.0f
                },
                "%s",
                message->owner
            );

            igSameLine(
                0.0f,
                6.0f
            );
        }

        igTextColored(
            (ImVec4){
                0.95f,
                0.3f,
                0.3f,
                1.0f
            },
            "%s:",
            message->name
        );

        igSameLine(
            time_x,
            -1.0f
        );

        igTextDisabled(
            "%s",
            message->time_str
        );

        igTextWrapped(
            "%s",
            message->text
        );

        igDummy(
            (ImVec2){0.0f, 8.0f}
        );
    }

    if (
        global_chat_message_count !=
            last_seen_message_count &&
        (
            was_at_bottom ||
            last_seen_message_count == 0
        )
    ) {
        /*
         * Only auto-scroll if the player was already at (or
         * near) the bottom -- if they scrolled up to read
         * older messages, a new one arriving shouldn't yank
         * them back down.
         */
        igSetScrollHereY(1.0f);
    }

    last_seen_message_count =
        global_chat_message_count;

    igEndChild();

    igPopStyleVar(2);

    igPushItemWidth(
        live_size.x - 115.0f
    );

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

    igSameLine(
        0.0f,
        8.0f
    );

    bool send_clicked =
        igButton(
            "SEND",
            (ImVec2){
                80.0f,
                0.0f
            }
        );

    if (
        submitted ||
        send_clicked
    ) {
        if (
            global_chat_input[0] !=
            '\0'
        ) {
            const char* nickname =
                env->usr
                    ->usrs
                    .nickname;

            if (
                nickname == NULL ||
                nickname[0] ==
                '\0'
            ) {
                nickname =
                    "Player";
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

            memset(
                global_chat_input,
                0,
                sizeof(
                    global_chat_input
                )
            );
        }
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

    ImU32 col =
        igColorConvertFloat4ToU32(
            (ImVec4){
                u->usrs.ntl_marker_color[0],
                u->usrs.ntl_marker_color[1],
                u->usrs.ntl_marker_color[2],
                u->usrs.ntl_marker_color[3]
            }
        );

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

        if (
            !jsr_network_get_location(
                global_chat_net,
                i,
                pname,
                sizeof(pname),
                srv,
                sizeof(srv),
                &lx,
                &ly
            )
        ) {
            continue;
        }

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
            u->usrs.ntl_marker_shape,
            col
        );

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