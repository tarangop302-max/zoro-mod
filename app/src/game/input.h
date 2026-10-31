#ifndef INPUT_H
#define INPUT_H

#include <thermite.h>

void input(tenv* env);

/* Handles the "Open settings" hotkey (toggle / press-and-hold). Called every
   frame from game_loop() -- even while the settings popup is open and
   input() itself is skipped -- so the same key can also close the popup. */
void input_settings_hotkey(tenv* env);

/* Handles the "Keyboard layout" hotkey: opens the Adjust keyboard buttons
   editor mid-match. Called every frame from game_loop(). */
void input_key_editor_hotkey(tenv* env);

#endif
