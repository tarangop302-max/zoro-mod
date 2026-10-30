#ifndef BACKUP_H
#define BACKUP_H

#include <thermite.h>

/* Settings & controls backup (Android). Export saves user.dat (every
   setting, control layout, HUD layout, saved names, team keys...) plus the
   Team Chat quick messages into a zip at a place the player picks with the
   system file picker, so it survives Clear data / uninstall. Import reads
   such a zip back and reloads everything immediately. Both are asynchronous
   (file picker), so backup_update() must be called once per frame. */
void backup_request_export(tenv *env);
void backup_request_import(void);
void backup_update(tenv *env);

/* Short status line for the Settings UI ("" when nothing happened yet). */
const char *backup_status(void);

#endif
