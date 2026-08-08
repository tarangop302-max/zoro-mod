#include "global_chat.h"

#include "../user.h"

#include "thermite/tchat.h"
#include "thermite/jsr_network.h"

#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#include <string.h>
#include <math.h>

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
    char text[GLOBAL_CHAT_TEXT_LEN];
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

static void global_chat_add_message(
    const char* name,
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

    strncpy(
        message->text,
        text,
        GLOBAL_CHAT_TEXT_LEN - 1
    );

    message->text[
        GLOBAL_CHAT_TEXT_LEN - 1
    ] = '\0';

    global_chat_message_count++;
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

    global_chat_add_message(
        msg->username,
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
        "Welcome to Public Chat!"
    );

    global_chat_add_message(
        "ZORO",
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
            "Could not start network connection."
        );

        return;
    }

    global_chat_net->chat = global_chat_tchat;

    jsr_network_connect(
        global_chat_net,
        GLOBAL_CHAT_ROOM_KEY,
        nickname
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
            "ZORO PUBLIC CHAT##zoro_chat_window" :
            "PUBLIC CHAT##zoro_chat_window";

    ImVec2 collapsed_size = {
        150.0f,
        74.0f
    };

    /*
     * Matches the size the panel was manually placed at:
     * a compact box in the top-left corner.
     */
    ImVec2 expanded_size = {
        480.0f,
        420.0f
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

                    if (
                        jsr_network_roster_name(
                            global_chat_net,
                            i,
                            name,
                            sizeof(name)
                        )
                    ) {
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
        }

        igEnd();
    }
}

static void global_chat_panel_contents(
    tenv* env,
    ImVec2 live_size
) {
    igText(
        "Public chat - no team key required"
    );

    bool is_connected =
        global_chat_net != NULL &&
        jsr_network_is_connected(
            global_chat_net
        );

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

    igSameLine(
        0.0f,
        10.0f
    );

    if (
        igSmallButton(
            global_chat_players_open ?
                "Hide players" :
                "Show players"
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

    igBeginChild_Str(
        "##global_chat_messages",
        (ImVec2){
            0.0f,
            message_area_height
        },
        true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar
    );

    for (
        int i = 0;
        i < global_chat_message_count;
        i++
    ) {
        global_chat_message* message =
            &global_chat_messages[i];

        igTextColored(
            (ImVec4){
                0.25f,
                0.75f,
                1.0f,
                1.0f
            },
            "%s:",
            message->name
        );

        igSameLine(
            0.0f,
            7.0f
        );

        igTextWrapped(
            "%s",
            message->text
        );
    }

    igEndChild();

    igPushItemWidth(
        live_size.x - 115.0f
    );

    bool submitted =
        igInputText(
            "##global_chat_input",
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
                1.0f,
                0.85f,
                0.2f,
                1.0f
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
        float lx;
        float ly;

        if (
            !jsr_network_get_location(
                global_chat_net,
                i,
                NULL,
                0,
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

        ImDrawList_AddCircleFilled(
            dl,
            p,
            4.0f,
            col,
            12
        );
    }
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