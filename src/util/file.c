#include <stdio.h>
#include <stdlib.h>

#include "util/file.h"

char *File_ReadAll(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(file);
        return NULL;
    }

    /* Terminate at what was read, not at size: the file may have been
       truncated in between. */
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[read] = '\0';

    /* Nothing read despite a positive size: a directory opens and seeks
       happily, and only fails here. */
    if (read == 0) {
        free(text);
        return NULL;
    }

    return text;
}
