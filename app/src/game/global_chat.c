#include "global_chat.h"

#include <string.h>

#define GLOBAL_CHAT_MAX_MESSAGES 50
#define GLOBAL_CHAT_NAME_LEN 32
#define GLOBAL_CHAT_TEXT_LEN 160

typedef struct {
    char name[GLOBAL_CHAT_NAME_LEN];
    char text[GLOBAL_CHAT_TEXT_LEN];
} global_chat_message;

static bool global_chat_initialized = false;
static bool global_chat_open = false;

static global_chat_message
    global_chat_messages[GLOBAL_CHAT_MAX_MESSAGES];

static int global_chat_message_count = 0;

static char global_chat_input[GLOBAL_CHAT_TEXT_LEN] = "";

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

void global_chat_init(tenv* env) {
    (void)env;

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
}

void global_chat_update(tenv* env) {
    (void)env;

    if (!global_chat_initialized) {
        return;
    }

    /*
     * The online server connection will be added here.
     * The current version only creates the complete UI.
     */
}

void global_chat_draw(tenv* env) {
    (void)env;

    if (!global_chat_initialized) {
        return;
    }

    ImGuiViewport* viewport =
        igGetMainViewport();

    ImVec2 button_size = {
        125.0f,
        48.0f
    };

    ImVec2 button_pos = {
        viewport->WorkPos.x +
            viewport->WorkSize.x -
            button_size.x -
            18.0f,

        viewport->WorkPos.y +
            18.0f
    };

    igSetNextWindowPos(
        button_pos,
        ImGuiCond_Always,
        (ImVec2){0.0f, 0.0f}
    );

    igSetNextWindowSize(
        button_size,
        ImGuiCond_Always
    );

    igSetNextWindowBgAlpha(
        0.85f
    );

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;

    if (
        igBegin(
            "##zoro_global_chat_button",
            NULL,
            flags
        )
    ) {
        if (
            igButton(
                "PUBLIC CHAT",
                (ImVec2){
                    108.0f,
                    32.0f
                }
            )
        ) {
            global_chat_open =
                !global_chat_open;
        }
    }

    igEnd();

    if (
        global_chat_open
    ) {
        global_chat_panel(
            env
        );
    }
}

void global_chat_panel(tenv* env) {
    if (!global_chat_initialized) {
        return;
    }

    ImGuiViewport* viewport =
        igGetMainViewport();

    float panel_width =
        viewport->WorkSize.x * 0.82f;

    float panel_height =
        viewport->WorkSize.y * 0.72f;

    if (
        panel_width > 700.0f
    ) {
        panel_width = 700.0f;
    }

    if (
        panel_height > 650.0f
    ) {
        panel_height = 650.0f;
    }

    ImVec2 panel_pos = {
        viewport->WorkPos.x +
            (
                viewport->WorkSize.x -
                panel_width
            ) * 0.5f,

        viewport->WorkPos.y +
            (
                viewport->WorkSize.y -
                panel_height
            ) * 0.5f
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

    bool open = true;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (
        igBegin(
            "ZORO PUBLIC CHAT",
            &open,
            flags
        )
    ) {
        igText(
            "Public chat - no team key required"
        );

        igSeparator();

        float input_height =
            50.0f;

        float message_area_height =
            igGetContentRegionAvail().y -
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
            panel_width - 115.0f
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

                global_chat_add_message(
                    nickname,
                    global_chat_input
                );

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

    if (!open) {
        global_chat_open =
            false;
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
}