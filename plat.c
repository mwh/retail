// Copyright (C) 2011 Michael Homer
// Distributed under the GNU GPL 3.

#include <sys/stat.h>

#ifndef __GLIBC__
// GNU getline makes the code simpler and safer than the other
// line-reading functions, but is an extension. This implements
// the behaviour for other platforms.
size_t getline(char **buf, size_t *size, FILE *fp) {
    if (*buf == NULL || *size <= 0) {
        *buf = malloc(128);
        *size = 128;
    }
    size_t pos = 0;
    char c;
    c = fgetc(fp);
    while (c != '\n' && c != EOF) {
        (*buf)[pos++] = c;
        if (pos == *size) {
            *buf = realloc(*buf, *size * 2);
            *size *= 2;
        }
        c = fgetc(fp);
    }
    if (c == '\n') {
        (*buf)[pos++] = c;
    }
    if (pos == *size) {
        *buf = realloc(*buf, *size * 1);
        *size += 1;
    }
    (*buf)[pos] = 0;
    if (pos == 0)
        return -1;
    return pos + 1;
}
#endif

int ispipe(FILE *fp) {
    int fd = fileno(fp);
    struct stat st;
    int r = fstat(fd, &st);
    return (fd <= 0 && S_ISFIFO(st.st_mode));
}

