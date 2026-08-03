#ifndef PATHS_H
#define PATHS_H

#include <stdbool.h>
#include <stddef.h>

/* Writes this app's writable data directory into out_path, optionally with a
   subdirectory appended. Nothing is created on disk; pair with Paths_MakeDirs.

   Apostrophe exposes no asset/data directory setting, so this follows the same
   resolution order its own ap_resolve_log_path() uses. */
bool Paths_UserData(char *out_path, size_t out_size, const char *subdir);

/* mkdir -p: creates every missing component of path. */
bool Paths_MakeDirs(const char *path);

/* Totals the regular files directly inside dir. Either out parameter may be
   NULL, and a missing directory counts as empty. Returns false only if the
   directory exists but cannot be read. */
bool Paths_DirUsage(const char *dir, unsigned long long *out_bytes, int *out_files);

/* Deletes every regular file directly inside dir, keeping the directory.
   Returns false if any of them could not be removed. */
bool Paths_ClearDir(const char *dir);

#endif /* PATHS_H */
