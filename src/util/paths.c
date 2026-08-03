#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/paths.h"

bool Paths_UserData(char *out_path, size_t out_size, const char *subdir) {
    char base[400];

    const char *shared = getenv("SHARED_USERDATA_PATH");
    const char *home = getenv("HOME");

    if (shared && shared[0]) {
        snprintf(base, sizeof(base), "%s/ra-explorer", shared);
    } else if (home && home[0]) {
        snprintf(base, sizeof(base), "%s/.userdata/ra-explorer", home);
    } else {
        return false;
    }

    int written = subdir && subdir[0]
        ? snprintf(out_path, out_size, "%s/%s", base, subdir)
        : snprintf(out_path, out_size, "%s", base);

    return written > 0 && (size_t)written < out_size;
}

bool Paths_MakeDirs(const char *path) {
    char partial[512];
    if (snprintf(partial, sizeof(partial), "%s", path) >= (int)sizeof(partial)) {
        return false;
    }

    for (char *p = partial + 1; *p; p++) {
        if (*p != '/') continue;

        *p = '\0';
        if (mkdir(partial, 0755) != 0 && errno != EEXIST) return false;
        *p = '/';
    }

    return mkdir(partial, 0755) == 0 || errno == EEXIST;
}

/* Walks dir once, accumulating sizes or deleting as it goes. */
static bool Paths_WalkDir(const char *dir, unsigned long long *out_bytes, int *out_files,
                          bool remove_files) {
    if (out_bytes) *out_bytes = 0;
    if (out_files) *out_files = 0;

    DIR *handle = opendir(dir);
    if (!handle) {
        return errno == ENOENT; /* nothing cached yet is a valid empty cache */
    }

    bool ok = true;
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[512];
        if (snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name) >= (int)sizeof(path)) {
            ok = false;
            continue;
        }

        struct stat info;
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;

        if (remove_files) {
            if (unlink(path) != 0) ok = false;
        } else {
            if (out_bytes) *out_bytes += (unsigned long long)info.st_size;
            if (out_files) (*out_files)++;
        }
    }

    closedir(handle);
    return ok;
}

bool Paths_DirUsage(const char *dir, unsigned long long *out_bytes, int *out_files) {
    return Paths_WalkDir(dir, out_bytes, out_files, false);
}

bool Paths_ClearDir(const char *dir) {
    return Paths_WalkDir(dir, NULL, NULL, true);
}
