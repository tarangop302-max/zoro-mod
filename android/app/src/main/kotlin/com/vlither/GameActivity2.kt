package com.vlither

/**
 * Window 2 of the game.
 *
 * This is declared in the manifest with android:process=":game2", so when
 * Android launches it, it starts as a brand new process — a fresh copy of
 * the app's memory space. That means the native engine (thermite/, loaded
 * from libvlither.so) gets its own independent copy of every global it
 * uses: its own socket to the slither server, its own snake/world state,
 * its own Vulkan context. Window 1 (GameActivity, default process) and
 * Window 2 (this class, :game2 process) can then run two live games at
 * once without touching each other's memory — no changes needed inside
 * thermite/ itself.
 *
 * It extends GameActivity rather than duplicating it so all existing
 * behavior (JNI callbacks, screenshot/chat/HUD code, etc.) carries over
 * automatically. JNI here resolves methods by reflection on whatever
 * activity instance is passed in (see twindow_android.c's use of
 * GetObjectClass), not by a hardcoded class name, so a subclass works
 * exactly like the original.
 *
 * A separate class (rather than just launching GameActivity twice) is
 * needed because Android ties a manifest <activity> entry's process to
 * its class name — you can't launch the same class into two different
 * processes.
 */
class GameActivity2 : GameActivity()
