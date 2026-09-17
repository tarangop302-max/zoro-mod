#ifdef ANDROID

#include "android_path.h"
#include <string.h>

static char g_files_dir[512] = "";
static char g_pictures_dir[512] = "";

void android_set_files_dir(const char* path) {
    if (path) {
        strncpy(g_files_dir, path, sizeof(g_files_dir) - 1);
        g_files_dir[sizeof(g_files_dir) - 1] = '\0';
    }
}

const char* android_get_files_dir(void) {
    return g_files_dir;
}

void android_set_pictures_dir(const char* path) {
    if (path) {
        strncpy(g_pictures_dir, path, sizeof(g_pictures_dir) - 1);
        g_pictures_dir[sizeof(g_pictures_dir) - 1] = '\0';
    }
}

const char* android_get_pictures_dir(void) {
    return g_pictures_dir;
}

#endif
