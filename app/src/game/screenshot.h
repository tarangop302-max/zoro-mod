#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <thermite.h>

/* Upper bound on how many kills can be captured in a single run -- see
   the cap-and-evict-oldest logic in screenshot.c. Shared here so
   kill_review.c can size its own per-item arrays (selection state,
   preview textures) without duplicating the number. */
#define SCREENSHOT_MAX_RUN_CAPTURES 8

/* Requests a kill be captured on the next call to
   screenshot_process_pending() -- call this the moment a kill is detected
   (kill_feed.c does). The actual Vulkan readback deliberately doesn't
   happen here: at kill-detection time the current frame is still being
   recorded (mid-ImGui), so its swapchain image doesn't hold the finished,
   presented picture yet. Capturing has to wait until the frame that was
   being recorded when the kill was noticed has actually been submitted
   and presented. */
void screenshot_request(int kill_number);

/* Call once per frame, right after tcontext_end() -- i.e. after the frame
   has been fully submitted and presented -- so if a kill is pending, this
   grabs the swapchain image that was just shown on screen and holds it in
   memory for this run (nothing is saved to disk here -- see
   screenshot_run_save()). No-ops if nothing is pending, and no-ops
   entirely on non-Android builds. */
void screenshot_process_pending(tenv *env);

/* How many kills captured so far this run. */
int screenshot_run_count(void);

/* Read-only access to a captured kill for the review screen (index must
   be in [0, screenshot_run_count())). Returns false if index is out of
   range. Ownership of *out_rgba stays with screenshot.c -- don't free it,
   and don't hold the pointer past a screenshot_run_reset() call. */
bool screenshot_run_get(int index, const unsigned char **out_rgba,
                        int *out_w, int *out_h, int *out_kill_number);

/* Actually saves the given captured kills (indices into the same array
   screenshot_run_get() reads) -- PNG-encodes and writes each one via JNI,
   once to the app-private kills folder and once to the phone's own
   Gallery (see android_jni_save_screenshot() / GameActivity.kt). Meant to
   be called from the post-match review screen, after the player picks
   which ones to keep. No-ops on non-Android builds. Does NOT clear the
   run's captures -- call screenshot_run_reset() once the player is done
   reviewing (whether they saved anything or not). */
void screenshot_run_save(tenv *env, const int *indices, int count);

/* Frees every capture held for the current run. Call this whenever a
   fresh run is about to start (see title_screen.c's Play button and
   loop.c's restart path) so a new run always begins with a clean slate --
   nothing carries over between matches. Also safe to call after the
   player finishes reviewing (whether or not they saved anything), since
   by then the run is over anyway. */
void screenshot_run_reset(void);

#endif
