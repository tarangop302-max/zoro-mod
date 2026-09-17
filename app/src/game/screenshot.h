#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <thermite.h>

/* Requests a screenshot be captured on the next call to
   screenshot_process_pending() -- call this the moment a kill is detected
   (kill_feed.c does). The actual Vulkan readback deliberately doesn't
   happen here: at kill-detection time the current frame is still being
   recorded (mid-ImGui), so its swapchain image doesn't hold the finished,
   presented picture yet. Capturing has to wait until the frame that was
   being recorded when the kill was noticed has actually been submitted
   and presented. */
void screenshot_request(int kill_number);

/* Call once per frame, right after tcontext_end() -- i.e. after the frame
   has been fully submitted and presented -- so if a screenshot is
   pending, this grabs the swapchain image that was just shown on
   screen. No-ops if nothing is pending, and no-ops entirely on
   non-Android builds (there's no phone gallery to save into there). */
void screenshot_process_pending(tenv *env);

#endif
