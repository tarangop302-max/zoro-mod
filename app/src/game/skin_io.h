#ifndef SKIN_IO_H
#define SKIN_IO_H

#include <stdint.h>
#include <thermite.h>
#include "../constants.h"

/* Slither skin library + .ntlmod import/export (Skin Editor).
   A .ntlmod skins file is base64( encodeURIComponent( JSON ) ) where JSON is
   [{"s":"255,255,255,0,0,0,0,0,<count>,<colour>,...","t":-1,"c":-1}, ...].
   Imported skins go into a separate library (slither_skins.dat) so the 30
   slot "Saved skins" list and user.dat layout stay untouched. */

#define SKIN_LIB_MAX 5000

typedef struct slither_skin {
  char code[MAX_SKIN_CODE_LEN + 1];
  uint8_t accessory;
} slither_skin;

void skin_io_request_import(tenv *env);
void skin_io_request_export(tenv *env);
void skin_io_update(tenv *env); /* call once per frame */
const char *skin_io_status(void);

int skin_lib_count(void);
const slither_skin *skin_lib_get(int i);
void skin_lib_clear(void);

#endif
