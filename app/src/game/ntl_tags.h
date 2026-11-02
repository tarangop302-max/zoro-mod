#ifndef NTL_TAGS_H
#define NTL_TAGS_H

/* NTL tags: the small picture that dangles from a snake's antenna in the
 * NTL mod for slither.io (https://ntl-slither.com). This module is a port of
 * the NTL mod's tag system:
 *
 *   - Tags uploaded on NTL's site (ids 368.. / 756..) are downloaded from
 *     tags/cstags + newtag.php, decoded by webp_decode.c and packed into a
 *     second, runtime atlas, so every NTL player's tag can be shown.
 *   - 164 tags (60 built-in t_N + 104 public "fstags") live in one atlas,
 *     app/res/textures/ntl_tags.png, described by ntl_tags_data.h (both made
 *     by tools/gen_ntl_tags.py). Tag ids are NTL's own ids, so every NTL
 *     client agrees on which tag is which.
 *   - How a snake gets its tag (same as the NTL mod):
 *       1. the spawn packet's skin header bytes (own choice, see
 *          ntl_tags_encode_header / ntl_tags_decode_header), and
 *       2. the NTL tag feed (ws://ws.ntl-slither.com:9000) which sends
 *          "ntlid -> tag id" pairs for the players on our server.
 *   - Drawing: a springy 10-point antenna + the tag image at its tip, drawn
 *     with the ImGui window draw list right after each snake's head.
 */

#include <stdbool.h>
#include <stdint.h>
#include <thermite.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "../cimgui/cimgui.h"

#include "snake.h"

#define NTL_TAG_NONE (-1)

/* Atlas texture (ImGui descriptor set), set by the renderer; 0 = not loaded. */
void ntl_tags_set_texture(unsigned long long texture_id);

void ntl_tags_init(tenv *env);
void ntl_tags_update(tenv *env);
void ntl_tags_destroy(tenv *env);

/* Is `tag_id` an NTL tag we have a picture for? */
bool ntl_tags_valid(int tag_id);

/* Settings (persisted in ntl_tags.cfg, not in the binary user_settings). */
bool ntl_tags_show_enabled(void);
int ntl_tags_selected(void); /* NTL tag id or NTL_TAG_NONE */

/* Spawn-packet skin header (8 bytes, the NTL "e" array). Writes the bytes
 * that carry our own tag. Returns false (and leaves hdr alone) when no tag is
 * selected. */
bool ntl_tags_encode_header(uint8_t hdr[8]);

/* Spawn packet of another player: skin header bytes -> tag id (or
 * NTL_TAG_NONE). *plain_skin is the default-skin id when the header says
 * "default skin, not a custom one", otherwise -1. */
int ntl_tags_decode_header(const uint8_t *hdr, int len, int *plain_skin);

/* Draw antenna + tag for one snake. (hx,hy) is the head in world units,
 * `alive` is alive_amt * (1 - dead_amt). Call inside the game window. */
void ntl_tags_draw_snake(tenv *env, snake *o, float hx, float hy,
                         float screen_cx, float screen_cy, float alive);

/* "Show NTL tags" option, one per gameplay mode (0 = Normal, 1 = Assist).
 * On by default; shown in the Normal / Assist pages of the Settings window. */
bool ntl_tags_show_get(int mode);
void ntl_tags_show_set(int mode, bool on);

/* Is `tag_id` drawable right now: bundled, or an uploaded tag whose picture
 * has been downloaded and decoded? (ntl_tags_valid() is bundled-only.) */
bool ntl_tags_known(int tag_id);

/* Opens NTL's tag upload page (ntl-slither.com/uploadtag.php) in the browser. */
void ntl_tags_open_upload_page(void);

/* The tag block of the Skin Editor window. Height it needs (call inside the
 * same font as the editor), and the block itself, drawn at window position
 * (x, y) with width w. */
float ntl_tags_block_height(void);
void ntl_tags_skin_editor_ui(tenv *env, float x, float y, float w);

#endif
