#ifndef SKIN_IO_H
#define SKIN_IO_H

#include <stdint.h>
#include <thermite.h>
#include "../constants.h"
#include "user_settings.h"

/* Slither skin library + .ntlmod import/export (Skin Editor).
   A .ntlmod skins file is base64( encodeURIComponent( JSON ) ) where JSON is
   [{"s":"255,255,255,0,0,0,0,0,<count>,<colour>,...","t":-1,"c":-1}, ...].
   Imported skins are appended to the Saved skins list (see below). */

#define SKIN_LIB_MAX 5000

typedef struct slither_skin {
  char code[MAX_SKIN_CODE_LEN + 1];
  uint8_t accessory;
} slither_skin;

void skin_io_request_import(tenv *env);
void skin_io_request_export(tenv *env);
void skin_io_update(tenv *env); /* call once per frame */
const char *skin_io_status(void);

/* "Saved skins" as one continuous list: the 30 slots stored in user_settings
   followed by the extra skins kept in slither_skins.dat (imported skins land
   here, so Saved skins can hold far more than 30). */
int skin_saved_total(const user_settings *us);
bool skin_saved_get(const user_settings *us, int i, char *code_out /* >=257 */,
                    uint8_t *accessory_out);
bool skin_saved_add(user_settings *us, const char *code, uint8_t accessory);

#endif
