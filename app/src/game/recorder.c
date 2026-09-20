#include "recorder.h"

#include "../user.h"

#ifdef ANDROID
#include "../android_jni.h"
#endif

#ifndef IM_COL32
#define IM_COL32(R, G, B, A) \
  (((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | ((ImU32)(R)))
#endif

void recorder_toggle(void) {
#ifdef ANDROID
  if (android_jni_is_recording()) {
    android_jni_request_stop_recording();
  } else {
    android_jni_request_start_recording();
  }
#endif
}

bool recorder_is_recording(void) {
#ifdef ANDROID
  return android_jni_is_recording();
#else
  return false;
#endif
}

void recorder_update(void) {
#ifdef ANDROID
  /* Drain pending events so the queue doesn't grow unboundedly. Nothing
     else to do with them right now -- see the header comment. */
  int guard = 0;
  while (android_jni_poll_recorder_event() != ANDROID_RECORDER_EVENT_NONE &&
        guard++ < 8) {
  }
#endif
}

void recorder_button_draw(tenv *env) {
#ifdef ANDROID
  game_data *gdata = &env->usr->gdata;
  if (gdata->curr_screen != PLAYING || !gdata->data.follow_view ||
      gdata->preview_active) {
    return;
  }

  tcontext *ctx = env->ctx;
  float sh = ctx->size[1];

  float r = sh * 0.028f;
  if (r < 20.0f) r = 20.0f;
  if (r > 34.0f) r = 34.0f;
  float cx = r + sh * 0.03f;
  /* Sits below the team/public chat panel's usual top-left spot so the
     two don't overlap by default; the chat panel can still be moved on
     top of this if the player repositions it, same as any two HUD
     elements can collide today. */
  float cy = sh * 0.20f;

  bool recording = recorder_is_recording();

  ImDrawList *dl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
  ImU32 ring_col = recording ? IM_COL32(255, 70, 70, 255)
                             : IM_COL32(230, 230, 235, 200);
  ImU32 fill_col = recording ? IM_COL32(120, 20, 20, 160)
                             : IM_COL32(40, 40, 48, 140);

  ImDrawList_AddCircleFilled(dl, (ImVec2){cx, cy}, r, fill_col, 32);
  ImDrawList_AddCircle(dl, (ImVec2){cx, cy}, r, ring_col, 32, 3.0f);

  if (recording) {
    float s = r * 0.5f;
    ImDrawList_AddRectFilled(dl, (ImVec2){cx - s * 0.5f, cy - s * 0.5f},
                             (ImVec2){cx + s * 0.5f, cy + s * 0.5f},
                             IM_COL32(255, 255, 255, 240), 3.0f,
                             ImDrawFlags_None);
  } else {
    ImDrawList_AddCircleFilled(dl, (ImVec2){cx, cy}, r * 0.5f,
                               IM_COL32(230, 60, 60, 240), 24);
  }

  ImGuiIO *io = igGetIO_Nil();
  if (!io->WantCaptureMouse &&
      igIsMouseClicked_Bool(ImGuiMouseButton_Left, false)) {
    float dx = io->MousePos.x - cx;
    float dy = io->MousePos.y - cy;
    if (dx * dx + dy * dy <= r * r) {
      recorder_toggle();
    }
  }
#else
  (void)env;
#endif
}
