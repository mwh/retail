// Copyright (C) 2011 Michael Homer
// Distributed under the GNU GPL 3.
#ifndef __GLIBC__
// GNU getline makes the code simpler and safer than the other
// line-reading functions, but is an extension. This implements
// the behaviour for other platforms.
int getline(char **buf, int *size, FILE *fp) {
    if (*buf == NULL || *size <= 0) {
        *buf = malloc(128);
        *size = 128;
    }
    int pos = 0;
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

