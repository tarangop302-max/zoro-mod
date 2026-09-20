#ifndef GLOBAL_CHAT_H
#define GLOBAL_CHAT_H

#include <thermite.h>
#include "thermite/jsr_network.h"

/* Initialize the ZORO Public Chat system. */
void global_chat_init(tenv* env);

/* Update the chat system every frame. */
void global_chat_update(tenv* env);

/* Draw the Public Chat window (button when collapsed,
 * full chat box when expanded). */
void global_chat_draw(tenv* env);

/* Lets the TEAM CHAT settings panel put the "{ J S R } TEAM CHAT"
 * window into (and out of) a temporary drag/resize mode, so the
 * player can reposition or resize it, then confirm to lock it back
 * down. Position and size are otherwise fixed. */
typedef enum {
    GLOBAL_CHAT_ADJUST_NONE = 0,
    GLOBAL_CHAT_ADJUST_POSITION,
    GLOBAL_CHAT_ADJUST_SIZE
} global_chat_adjust_mode;

/* Current adjust mode (GLOBAL_CHAT_ADJUST_NONE when locked). */
global_chat_adjust_mode global_chat_get_adjust_mode(void);

/* Switch adjust mode. Passing GLOBAL_CHAT_ADJUST_NONE while a mode
 * is active confirms the change: the current position/size is
 * saved to the user's settings and the window locks back down. */
void global_chat_set_adjust_mode(tenv* env, global_chat_adjust_mode mode);

/* True if `nickname` is currently connected to the shared JSR
 * public-chat relay -- i.e. authenticated with a valid access key,
 * the same check the "Online players" roster is built from. Used
 * to highlight teammates' in-game name labels. */
bool global_chat_is_teammate(const char* nickname);

/* One entry from global_chat_get_teammates(). */
typedef struct {
    char nickname[32];
    int shape;
    float color[3];
    int score;
    int ping;  /* -1 if not yet known (see jsr_network_get_location) */
    bool sos;  /* true while this teammate's SOS signal is active */
    int emoji_id;  /* index into PROFILE_EMOJIS (profile_emoji.h); 0 = none */
} global_chat_teammate;

/* Activate our own SOS signal until until_ms (epoch milliseconds), or
 * clear it immediately by passing a value <= the current time. While
 * active, it rides along on the same location broadcast that already
 * powers the minimap markers (see jsr_network_send_location), so it
 * reaches every Public Chat teammate regardless of distance -- and
 * clears itself automatically if the app is closed without explicitly
 * cancelling it, since a new session starts with no SOS active. */
void global_chat_set_sos(long long until_ms);

/* True while our own SOS signal is currently active. */
bool global_chat_is_sos_active(void);

/* Milliseconds remaining on our own SOS signal, or 0 if inactive. */
long long global_chat_sos_remaining_ms(void);

/* Fill out_teammates (capacity max_count) with every teammate
 * currently known to be on our own game server -- using the same
 * location broadcast that already powers the minimap markers, so
 * this works regardless of how far away they are, not only while
 * they're close enough for the game server to include them in our
 * local snake list. Returns the number of entries written. */
int global_chat_get_teammates(
    tenv* env,
    global_chat_teammate* out_teammates,
    int max_count
);

/* Draw a dot on the minimap for every Public Chat player
 * on the same game server as us. Call this right after
 * ntl_team_draw_minimap() with the same x/y/size. */
void global_chat_draw_minimap_markers(
    tenv* env,
    float x,
    float y,
    float size
);

/* Read-only access to the underlying JSR network connection
 * (NULL if not initialized yet), for other systems that want
 * to show the same roster/location data -- e.g. the "Players
 * & minimap" tab under Team Chat. */
jsr_network* global_chat_get_network(void);

/* Release Public Chat resources. */
void global_chat_destroy(tenv* env);

#endif