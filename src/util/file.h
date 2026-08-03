#ifndef FILE_H
#define FILE_H

/* Reads the whole file at `path` into a NUL-terminated buffer owned by the
   caller (free with free()). NULL if it cannot be read, or is empty: callers
   parse the result as JSON, for which that is no better than missing. */
char *File_ReadAll(const char *path);

#endif /* FILE_H */
