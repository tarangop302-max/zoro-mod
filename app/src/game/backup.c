#include "backup.h"

#include "../user.h"
#include "global_chat.h"
#include "user_settings.h"

#include <stdio.h>
#include <string.h>

#ifdef ANDROID
#include "../android_jni.h"
#endif

static char s_status[160] = "";

static void set_status(const char *msg) {
  snprintf(s_status, sizeof s_status, "%s", msg);
}

const char *backup_status(void) { return s_status; }

void backup_request_export(tenv *env) {
#ifdef ANDROID
  /* Make sure user.dat on disk matches what is currently in memory. */
  save_user_settings(&env->usr->usrs);
  set_status("Choose where to save the backup...");
  android_jni_backup_export();
#else
  (void)env;
  set_status("Backup is only available on Android.");
#endif
}

void backup_request_import(void) {
#ifdef ANDROID
  set_status("Choose a backup file to restore...");
  android_jni_backup_import();
#else
  set_status("Backup is only available on Android.");
#endif
}

void backup_update(tenv *env) {
#ifdef ANDROID
  int guard = 0;
  android_backup_event ev;
  while ((ev = android_jni_poll_backup_event()) != ANDROID_BACKUP_EVENT_NONE &&
         guard++ < 8) {
    switch (ev) {
      case ANDROID_BACKUP_EVENT_EXPORTED:
        set_status("Backup saved. Keep that file somewhere safe.");
        break;
      case ANDROID_BACKUP_EVENT_IMPORTED: {
        user_settings *usrs = &env->usr->usrs;
        read_user_settings(usrs);   /* reload the restored user.dat */
        save_user_settings(usrs);   /* re-write it in the current layout */
        global_chat_reload_quick_msgs();
        set_status("Backup restored. If anything looks stale, reopen the screen.");
        break;
      }
      case ANDROID_BACKUP_EVENT_CANCELLED:
        set_status("Cancelled.");
        break;
      case ANDROID_BACKUP_EVENT_ERROR:
      default:
        set_status("Backup failed. Make sure you picked a Zoro backup zip.");
        break;
    }
  }
#else
  (void)env;
#endif
}
