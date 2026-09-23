#ifndef ANDROID_PATH_H
#define ANDROID_PATH_H

#ifdef ANDROID

#include <string.h>
#include <stdio.h>

void android_set_files_dir(const char* path);

const char* android_get_files_dir(void);

/* App-private but external storage (NDK's ANativeActivity::externalDataPath,
   same directory Kotlin reaches via getExternalFilesDir(null)) -- used for
   kill screenshots. Both native and Kotlin independently append the same
   "/Pictures/kills" suffix to their own copy of this base path rather than
   passing the full path across the JNI boundary, so they always agree on
   where files live without needing to coordinate at runtime. */
void android_set_pictures_dir(const char* path);

const char* android_get_pictures_dir(void);

static inline void android_build_kills_dir(char* out, int out_size) {
    snprintf(out, (size_t)out_size, "%s/Pictures/kills", android_get_pictures_dir());
}

/* Same base path as android_build_kills_dir() above (it's the app's
   external data dir generally, not literally "Pictures"-specific) --
   Kotlin's getExternalFilesDir(Environment.DIRECTORY_MOVIES) resolves to
   the same base, just a different subfolder. Used for recorded clips. */
static inline void android_build_clips_dir(char* out, int out_size) {
    snprintf(out, (size_t)out_size, "%s/Movies/clips", android_get_pictures_dir());
}

static inline void android_build_path(char* out, int out_size, const char* filename) {
    snprintf(out, (size_t)out_size, "%s/%s", android_get_files_dir(), filename);
}

#endif

#endif
