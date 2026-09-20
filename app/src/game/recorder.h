#ifndef RECORDER_H
#define RECORDER_H

#include <thermite.h>

/* Starts recording if not already active, stops it if it is. The actual
   start is asynchronous (system permission dialog) -- see recorder.c and
   android_jni.h's ANDROID_RECORDER_EVENT_* for how that resolves. No-op
   on non-Android builds. */
void recorder_toggle(void);

/* Cheap, synchronous state check -- safe to call every frame (e.g. to
   decide what color to draw the record button). Always false on
   non-Android builds. */
bool recorder_is_recording(void);

/* Call once per frame to drain the recorder's event queue. Currently
   just keeps the queue from growing unboundedly; the record button's
   color already reflects state via recorder_is_recording() polled
   directly each frame, so a denied/errored permission request simply
   leaves recording off with no extra UI needed right now. */
void recorder_update(void);

/* Draws the small floating Record button during live gameplay (only
   when actually following your own snake -- not during the death popup,
   not in the background bot-preview behind Settings/Controls). Tapping
   it calls recorder_toggle(). No-op on non-Android builds. */
void recorder_button_draw(tenv *env);

#endif
