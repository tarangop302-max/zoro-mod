#include "global_chat.h"

#include "../user.h"

#include "thermite/tchat.h"
#include "thermite/jsr_network.h"

#include <string.h>

#define GLOBAL_CHAT_MAX_MESSAGES 50
#define GLOBAL_CHAT_NAME_LEN 32
#define GLOBAL_CHAT_TEXT_LEN 160

/*
 * Shared public room.
 * Every player using this mod joins this room automatically.
 */
#define GLOBAL_CHAT_ROOM_KEY "GLOBAL01"

typedef struct {
    char name[GLOBAL_CHAT_NAME_LEN];
    char text[GLOBAL_CHAT_TEXT_LEN];
} global_chat_message;

static bool global_chat_initialized = false;
static bool global_chat_open = false;

/* Public-chat networking. */
static tchat_system* global_chat_tchat = NULL;
static jsr_network* global_chat_net = NULL;

static global_chat_message
    global_chat_messages[GLOBAL_CHAT_MAX_MESSAGES];

static int global_chat_message_count = 0;

static char global_chat_input[GLOBAL_CHAT_TEXT_LEN] = "";


/* ---------------------------------------------------------
 * MESSAGE HANDLING
 * --------------------------------------------------------- */

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


/* ---------------------------------------------------------
 * INITIALIZATION
 * --------------------------------------------------------- */

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

    const char* nickname = "Player";

    if (
        env != NULL &&
        env->usr != NULL &&
        env->usr->usrs.nickname[0] != '\0'
    ) {
        nickname =
            env->usr->usrs.nickname;
    }

    global_chat_tchat =
        tchat_create(nickname);

    if (
        global_chat_tchat == NULL
    ) {
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

    tchat_join_team(
        global_chat_tchat,
        GLOBAL_CHAT_ROOM_KEY
    );

    global_chat_net =
        jsr_network_create("", 0);

    if (
        global_chat_net == NULL
    ) {
        global_chat_add_message(
            "ZORO",
            "Could not start network connection."
        );

        return;
    }

    global_chat_net->chat =
        global_chat_tchat;

    jsr_network_connect(
        global_chat_net,
        GLOBAL_CHAT_ROOM_KEY,
        nickname
    );
}


/* ---------------------------------------------------------
 * UPDATE
 * --------------------------------------------------------- */

void global_chat_update(tenv* env) {
    (void)env;

    if (
        !global_chat_initialized
    ) {
        return;
    }

    if (
        global_chat_net != NULL
    ) {
        jsr_network_update(
            global_chat_net,
            1.0f / 60.0f
        );
    }
}


/* ---------------------------------------------------------
 * SMALL CHAT BUTTON
 * --------------------------------------------------------- */

void global_chat_draw(tenv* env) {
    if (
        !global_chat_initialized
    ) {
        return;
    }

    ImGuiViewport* viewport =
        igGetMainViewport();

    /*
     * Button position:
     * left side, below the ping/FPS display.
     */
    ImVec2 button_pos = {
        viewport->WorkPos.x + 12.0f,
        viewport->WorkPos.y + 135.0f
    };

    igSetNextWindowPos(
        button_pos,
        ImGuiCond_Always,
        (ImVec2){0.0f, 0.0f}
    );

    igSetNextWindowSize(
        (ImVec2){
            135.0f,
            54.0f
        },
        ImGuiCond_Always
    );

    igSetNextWindowBgAlpha(
        0.0f
    );

    ImGuiWindowFlags button_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground;

    if (
        igBegin(
            "##zoro_chat_button_window",
            NULL,
            button_flags
        )
    ) {
        /*
         * The speech symbol is written as text
         * so no image asset is required.
         */
        if (
            igButton(
                "[CHAT]",
                (ImVec2){
                    125.0f,
                    44.0f
                }
            )
        ) {
            global_chat_open = true;
        }
    }

    igEnd();

    if (
        global_chat_open
    ) {
        global_chat_panel(env);
    }
}


/* ---------------------------------------------------------
 * ZORO PUBLIC CHAT PANEL
 * --------------------------------------------------------- */

void global_chat_panel(
    tenv* env
) {
    if (
        !global_chat_initialized
    ) {
        return;
    }

    ImGuiViewport* viewport =
        igGetMainViewport();

    /*
     * Similar placement to the blue box:
     * center-left instead of full-screen center.
     */
    float panel_width =
        viewport->WorkSize.x * 0.50f;

    float panel_height =
        viewport->WorkSize.y * 0.66f;

    if (
        panel_width < 470.0f
    ) {
        panel_width = 470.0f;
    }

    if (
        panel_width > 650.0f
    ) {
        panel_width = 650.0f;
    }

    if (
        panel_height < 390.0f
    ) {
        panel_height = 390.0f;
    }

    if (
        panel_height > 620.0f
    ) {
        panel_height = 620.0f;
    }

    ImVec2 panel_pos = {
        viewport->WorkPos.x +
            viewport->WorkSize.x *
            0.18f,

        viewport->WorkPos.y +
            viewport->WorkSize.y *
            0.16f
    };

    igSetNextWindowPos(
        panel_pos,
        ImGuiCond_Always,
        (ImVec2){0.0f, 0.0f}
    );

    igSetNextWindowSize(
        (ImVec2){
            panel_width,
            panel_height
        },
        ImGuiCond_Always
    );

    ImGuiWindowFlags panel_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;

    if (
        igBegin(
            "##zoro_public_chat_panel",
            NULL,
            panel_flags
        )
    ) {
        /* ---------------- HEADER ---------------- */

        igTextColored(
            (ImVec4){
                0.35f,
                0.65f,
                1.0f,
                1.0f
            },
            "ZORO PUBLIC CHAT"
        );

        /*
         * Put the minimize button
         * on the right side.
         */
        float minimize_x =
            panel_width - 58.0f;

        if (
            minimize_x > 0.0f
        ) {
            igSameLine(
                minimize_x,
                0.0f
            );
        }

        if (
            igButton(
                "-",
                (ImVec2){
                    40.0f,
                    30.0f
                }
            )
        ) {
            global_chat_open = false;
        }

        igSeparator();


        /* ---------------- MESSAGES ---------------- */

        ImVec2 available;

        igGetContentRegionAvail(
            &available
        );

        float input_area_height =
            62.0f;

        float message_height =
            available.y -
            input_area_height;

        if (
            message_height < 100.0f
        ) {
            message_height =
                100.0f;
        }

        igBeginChild_Str(
            "##zoro_chat_messages",
            (ImVec2){
                0.0f,
                message_height
            },
            false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar
        );

        for (
            int i = 0;
            i <
            global_chat_message_count;
            i++
        ) {
            global_chat_message*
                message =
                &global_chat_messages[i];

            /*
             * Green for ZORO system messages,
             * light blue for other players.
             */
            ImVec4 name_color = {
                0.35f,
                0.75f,
                1.0f,
                1.0f
            };

            if (
                strcmp(
                    message->name,
                    "ZORO"
                ) == 0
            ) {
                name_color =
                    (ImVec4){
                        0.35f,
                        0.85f,
                        0.40f,
                        1.0f
                    };
            }

            igTextColored(
                name_color,
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

            igSpacing();
        }

        /*
         * Automatically show the newest
         * messages at the bottom.
         */
        igSetScrollHereY(
            1.0f
        );

        igEndChild();


        /* ---------------- INPUT ---------------- */

        float send_width =
            95.0f;

        float input_width =
            panel_width -
            send_width -
            45.0f;

        if (
            input_width < 150.0f
        ) {
            input_width = 150.0f;
        }

        igPushItemWidth(
            input_width
        );

        bool submitted =
            igInputTextWithHint(
                "##zoro_chat_input",
                "Type a message...",
                global_chat_input,
                sizeof(
                    global_chat_input
                ),
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
                    send_width,
                    38.0f
                }
            );


        /* ---------------- SEND ---------------- */

        if (
            submitted ||
            send_clicked
        ) {
            if (
                global_chat_input[0]
                != '\0'
            ) {
                const char*
                    nickname =
                    "Player";

                if (
                    env != NULL &&
                    env->usr != NULL &&
                    env->usr
                        ->usrs
                        .nickname[0]
                    != '\0'
                ) {
                    nickname =
                        env->usr
                            ->usrs
                            .nickname;
                }

                /*
                 * Show immediately
                 * on the sender's screen.
                 */
                global_chat_add_message(
                    nickname,
                    global_chat_input
                );

                /*
                 * Send to all mod users
                 * connected to GLOBAL01.
                 */
                if (
                    global_chat_net
                    != NULL
                ) {
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

    igEnd();
}


/* ---------------------------------------------------------
 * DESTROY
 * --------------------------------------------------------- */

void global_chat_destroy(
    tenv* env
) {
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
        sizeof(
            global_chat_input
        )
    );

    if (
        global_chat_net != NULL
    ) {
        jsr_network_destroy(
            global_chat_net
        );

        global_chat_net = NULL;
    }

    if (
        global_chat_tchat != NULL
    ) {
        tchat_destroy(
            global_chat_tchat
        );

        global_chat_tchat = NULL;
    }
}