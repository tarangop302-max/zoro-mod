#include "global_chat.h"

#include "cimgui/cimgui.h"

#include <stdio.h>
#include <string.h>

#define GLOBAL_CHAT_MAX_MESSAGES 50
#define GLOBAL_CHAT_MESSAGE_LEN  160
#define GLOBAL_CHAT_NAME_LEN     32

typedef struct {
  char name[GLOBAL_CHAT_NAME_LEN];
  char text[GLOBAL_CHAT_MESSAGE_LEN];
} global_chat_message;

static bool global_chat_initialized = false;

static global_chat_message
    global_chat_messages[GLOBAL_CHAT_MAX_MESSAGES];

static int global_chat_message_count = 0;

static char global_chat_input[GLOBAL_CHAT_MESSAGE_LEN] = "";

static void global_chat_add_message(
    const char* name,
    const char* text
) {
  if (name == NULL || text == NULL) {
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

  snprintf(
      message->name,
      sizeof(message->name),
      "%s",
      name
  );

  snprintf(
      message->text,
      sizeof(message->text),
      "%s",
      text
  );

  global_chat_message_count++;
}

void global_chat_init(tenv* env) {
  (void)env;

  memset(
      global_chat_messages,
      0,
      sizeof(global_chat_messages)
  );

  memset(
      global_chat_input,
      0,
      sizeof(global_chat_input)
  );

  global_chat_message_count = 0;

  global_chat_initialized = true;

  /*
   * Temporary welcome message.
   * This confirms that the new chat panel is working.
   */
  global_chat_add_message(
      "ZORO",
      "Welcome to Global Chat!"
  );
}

void global_chat_update(tenv* env) {
  (void)env;

  if (!global_chat_initialized) {
    return;
  }

  /*
   * Online server communication will be added here.
   */
}

void global_chat_panel(tenv* env) {
  (void)env;

  if (!global_chat_initialized) {
    return;
  }

  ImGuiViewport* viewport =
      igGetMainViewport();

  ImVec2 window_size = {
      viewport->Size.x * 0.92f,
      viewport->Size.y * 0.82f
  };

  ImVec2 window_pos = {
      viewport->Pos.x +
          (viewport->Size.x -
           window_size.x) *
          0.5f,

      viewport->Pos.y +
          (viewport->Size.y -
           window_size.y) *
          0.5f
  };

  igSetNextWindowPos(
      window_pos,
      ImGuiCond_Always,
      (ImVec2){0.0f, 0.0f}
  );

  igSetNextWindowSize(
      window_size,
      ImGuiCond_Always
  );

  igBegin(
      "ZORO Global Chat",
      NULL,

      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize
  );

  igText(
      "PUBLIC CHAT"
  );

  igSeparator();

  igBeginChild_Str(
      "##global_chat_messages",
      (ImVec2){
          0.0f,
          -55.0f
      },

      true,

      ImGuiWindowFlags_AlwaysVerticalScrollbar
  );

  if (
      global_chat_message_count ==
      0
  ) {
    igTextDisabled(
        "No messages yet."
    );
  }

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
        6.0f
    );

    igTextWrapped(
        "%s",
        message->text
    );
  }

  igEndChild();

  igSetNextItemWidth(
      -75.0f
  );

  bool enter_pressed =
      igInputText(
          "##global_chat_input",
          global_chat_input,
          sizeof(global_chat_input),

          ImGuiInputTextFlags_EnterReturnsTrue,

          NULL,
          NULL
      );

  igSameLine(
      0.0f,
      8.0f
  );

  bool send_pressed =
      igButton(
          "SEND",
          (ImVec2){
              65.0f,
              0.0f
          }
      );

  if (
      (enter_pressed ||
       send_pressed) &&

      global_chat_input[0] !=
          '\0'
  ) {
    /*
     * Temporary local message.
     *
     * Later this will be sent to the
     * Railway chat server so every
     * ZORO mod user can see it.
     */
    global_chat_add_message(
        "You",
        global_chat_input
    );

    global_chat_input[0] =
        '\0';
  }

  igEnd();
}

void global_chat_destroy(tenv* env) {
  (void)env;

  memset(
      global_chat_messages,
      0,
      sizeof(global_chat_messages)
  );

  memset(
      global_chat_input,
      0,
      sizeof(global_chat_input)
  );

  global_chat_message_count = 0;

  global_chat_initialized = false;
}