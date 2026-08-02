#include "global_chat.h"

#include "../user.h"

#include "thermite/tchat.h"
#include "thermite/jsr_network.h"

#include <string.h>


#define GLOBAL_CHAT_MAX_MESSAGES 50
#define GLOBAL_CHAT_NAME_LEN 32
#define GLOBAL_CHAT_TEXT_LEN 160

/*
 * All users of this APK automatically join
 * this same public chat room.
 */
#define GLOBAL_CHAT_ROOM_KEY "GLOBAL01"


typedef struct {
    char name[GLOBAL_CHAT_NAME_LEN];
    char text[GLOBAL_CHAT_TEXT_LEN];
} global_chat_message;


/* ---------------------------------------------------------
 * STATE
 * --------------------------------------------------------- */

static bool global_chat_initialized = false;
static bool global_chat_open = false;

static tchat_system* global_chat_tchat = NULL;
static jsr_network* global_chat_net = NULL;

static global_chat_message
    global_chat_messages[GLOBAL_CHAT_MAX_MESSAGES];

static int global_chat_message_count = 0;

static char
    global_chat_input[GLOBAL_CHAT_TEXT_LEN] = "";


/* ---------------------------------------------------------
 * ADD MESSAGE
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

    /*
     * Remove the oldest message when
     * the message list is full.
     */
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


/* ---------------------------------------------------------
 * RECEIVE NETWORK MESSAGE
 * --------------------------------------------------------- */

static void global_chat_on_network_message(
    tchat_message* msg
) {
    if (
        msg == NULL
    ) {
        return;
    }

    global_chat_add_message(
        msg->username,
        msg->message
    );
}


/* ---------------------------------------------------------
 * INITIALIZE
 * --------------------------------------------------------- */

void global_chat_init(
    tenv* env
) {
    global_chat_initialized = true;

    global_chat_open = false;

    global_chat_message_count = 0;

    memset(
        global_chat_input,
        0,
        sizeof(global_chat_input)
    );

    /*
     * Messages shown when the chat opens.
     */
    global_chat_add_message(
        "ZORO",
        "Welcome to Public Chat!"
    );

    global_chat_add_message(
        "ZORO",
        "Everyone using this mod can chat here."
    );


    /*
     * Get the current player's nickname.
     */
    const char* nickname =
        "Player";

    if (
        env != NULL &&
        env->usr != NULL &&
        env->usr->usrs.nickname[0] != '\0'
    ) {
        nickname =
            env->usr->usrs.nickname;
    }


    /*
     * Create local chat system.
     */
    global_chat_tchat =
        tchat_create(nickname);

    if (
        global_chat_tchat == NULL
    ) {
        global_chat_add_message(
            "ZORO",
            "Chat system could not start."
        );

        return;
    }


    tchat_set_on_message_callback(
        global_chat_tchat,
        global_chat_on_network_message
    );


    /*
     * Join the fixed public room.
     */
    tchat_join_team(
        global_chat_tchat,
        GLOBAL_CHAT_ROOM_KEY
    );


    /*
     * Create network connection.
     */
    global_chat_net =
        jsr_network_create(
            "",
            0
        );

    if (
        global_chat_net == NULL
    ) {
        global_chat_add_message(
            "ZORO",
            "Network connection could not start."
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

void global_chat_update(
    tenv* env
) {
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
 * DRAW CHAT BUTTON
 * --------------------------------------------------------- */

void global_chat_draw(
    tenv* env
) {
    if (
        !global_chat_initialized
    ) {
        return;
    }


    /*
     * IMPORTANT:
     *
     * The button is placed directly inside
     * the current ImGui window instead of
     * creating another transparent window.
     *
     * This makes Android touch input work
     * more reliably.
     */
    ImGuiViewport* viewport =
        igGetMainViewport();


    ImVec2 button_position = {
        viewport->WorkPos.x + 12.0f,
        viewport->WorkPos.y + 105.0f
    };


    /*
     * Save the current cursor position.
     */
    ImVec2 old_cursor;

    igGetCursorScreenPos(
        &old_cursor
    );


    /*
     * Move cursor to the chat button.
     */
    igSetCursorScreenPos(
        button_position
    );


    /*
     * Small button shown at the left side.
     */
    if (
        igButton(
            "[ CHAT ]",
            (ImVec2){
                120.0f,
                42.0f
            }
        )
    ) {
        global_chat_open =
            !global_chat_open;
    }


    /*
     * Restore cursor so other game UI
     * is not moved.
     */
    igSetCursorScreenPos(
        old_cursor
    );


    /*
     * Draw chat panel.
     */
    if (
        global_chat_open
    ) {
        global_chat_panel(
            env
        );
    }
}


/* ---------------------------------------------------------
 * DRAW CHAT PANEL
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
     * Panel size.
     */
    float panel_width =
        viewport->WorkSize.x *
        0.48f;

    float panel_height =
        viewport->WorkSize.y *
        0.68f;


    /*
     * Keep panel usable on different
     * screen sizes.
     */
    if (
        panel_width < 420.0f
    ) {
        panel_width =
            420.0f;
    }

    if (
        panel_width > 650.0f
    ) {
        panel_width =
            650.0f;
    }

    if (
        panel_height < 360.0f
    ) {
        panel_height =
            360.0f;
    }

    if (
        panel_height > 620.0f
    ) {
        panel_height =
            620.0f;
    }


    /*
     * Place panel near the center-left.
     */
    ImVec2 panel_position = {
        viewport->WorkPos.x +
        viewport->WorkSize.x *
        0.16f,

        viewport->WorkPos.y +
        viewport->WorkSize.y *
        0.15f
    };


    igSetNextWindowPos(
        panel_position,
        ImGuiCond_Always,
        (ImVec2){
            0.0f,
            0.0f
        }
    );


    igSetNextWindowSize(
        (ImVec2){
            panel_width,
            panel_height
        },
        ImGuiCond_Always
    );


    /*
     * Dark panel.
     */
    igSetNextWindowBgAlpha(
        0.94f
    );


    ImGuiWindowFlags panel_flags =

        ImGuiWindowFlags_NoTitleBar |

        ImGuiWindowFlags_NoResize |

        ImGuiWindowFlags_NoMove |

        ImGuiWindowFlags_NoSavedSettings;


    if (
        igBegin(
            "##zoro_public_chat",
            NULL,
            panel_flags
        )
    ) {


        /* -----------------------------------------
         * HEADER
         * ----------------------------------------- */

        igTextColored(
            (ImVec4){
                0.30f,
                0.65f,
                1.00f,
                1.00f
            },
            "ZORO PUBLIC CHAT"
        );


        /*
         * Minimize button.
         */
        igSameLine(
            panel_width -
            65.0f,
            0.0f
        );


        if (
            igButton(
                "-",
                (ImVec2){
                    42.0f,
                    30.0f
                }
            )
        ) {
            global_chat_open =
                false;
        }


        igSeparator();


        /* -----------------------------------------
         * MESSAGE AREA
         * ----------------------------------------- */

        ImVec2 available;

        igGetContentRegionAvail(
            &available
        );


        float message_area_height =

            available.y -

            60.0f;


        if (
            message_area_height <
            100.0f
        ) {
            message_area_height =
                100.0f;
        }


        igBeginChild_Str(
            "##zoro_chat_messages",
            (ImVec2){
                0.0f,
                message_area_height
            },
            true,
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


            ImVec4 name_color = {
                0.45f,
                0.75f,
                1.00f,
                1.00f
            };


            /*
             * ZORO messages are green.
             */
            if (
                strcmp(
                    message->name,
                    "ZORO"
                ) == 0
            ) {
                name_color =
                    (ImVec4){
                        0.35f,
                        0.90f,
                        0.45f,
                        1.00f
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
         * Keep newest messages visible.
         */
        if (
            global_chat_message_count >
            0
        ) {
            igSetScrollHereY(
                1.0f
            );
        }


        igEndChild();


        /* -----------------------------------------
         * INPUT
         * ----------------------------------------- */

        float send_width =
            90.0f;


        float input_width =

            panel_width -

            send_width -

            48.0f;


        if (
            input_width <
            150.0f
        ) {
            input_width =
                150.0f;
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


        /* -----------------------------------------
         * SEND MESSAGE
         * ----------------------------------------- */

        if (
            submitted ||
            send_clicked
        ) {
            if (
                global_chat_input[0]
                != '\0'
            ) {
                const char* nickname =
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
                 * Show message immediately.
                 */
                global_chat_add_message(
                    nickname,
                    global_chat_input
                );


                /*
                 * Send to the public room.
                 */
                if (
                    global_chat_net !=
                    NULL
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

        global_chat_net =
            NULL;
    }


    if (
        global_chat_tchat != NULL
    ) {
        tchat_destroy(
            global_chat_tchat
        );

        global_chat_tchat =
            NULL;
    }
}