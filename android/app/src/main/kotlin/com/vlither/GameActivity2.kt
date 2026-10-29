package com.vlither

/**
 * Window 2 of the game.
 *
 * Declared in the manifest with android:process=":game2", so Android
 * launches it as a brand new process — its own copy of every native
 * global the engine uses (socket, snake/world state, Vulkan context).
 * Window 1 (GameActivity, default process) and this one can then run two
 * live games at once without touching each other's memory, with no
 * changes needed inside thermite/ itself.
 *
 * Extends GameActivity (rather than duplicating it) so all existing
 * behavior carries over automatically. JNI resolves methods by reflection
 * on whatever activity instance is passed in (see twindow_android.c's use
 * of GetObjectClass), not by a hardcoded class name, so a subclass works
 * exactly like the original.
 */
class GameActivity2 : GameActivity()
