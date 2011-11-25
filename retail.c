// retail - tail with regular expressions
// Copyright (C) 2011 Michael Homer
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#include <stdio.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "plat.c"

#define MODE_NORMAL 0
#define MODE_REGEX 1
#define MODE_SKIPSTART 2
#define MODE_BYTES 3

#define QUIT_REGEX 1

int quit_mode = 0;
regex_t quitre;

void tail_quit(char *line) {
    if (quit_mode & QUIT_REGEX) {
        if (0 == regexec(&quitre, line, 0, NULL, 0)) {
            exit(0);
        }
    }
}

int tail_bytes(FILE *fp, long int num_bytes, int follow) {
    char buf[2048];
    int read;
    if (num_bytes > 0) {
        fseek(fp, 0, SEEK_END);
        fseek(fp, -num_bytes, SEEK_CUR);
    } else {
        fseek(fp, -num_bytes - 1, SEEK_SET);
    }
    read = fread(buf, 1, 2048, fp);
    while (read == 2048) {
        fwrite(buf, 1, read, stdout);
        read = fread(buf, 1, 2048, fp);
    }
    fwrite(buf, 1, read, stdout);
    fflush(stdout);
    if (follow) {
        read = fread(buf, 1, 2048, fp);
        while (1) {
            fwrite(buf, 1, read, stdout);
            fflush(stdout);
            if (read <= 0)
                sleep(0.5);
            read = fread(buf, 1, 2048, fp);
        }
    }
    return 0;
}

int tail_regex_unseekable(FILE *fp, char *pattern) {
    regex_t re;
    int i;
    int rcv = regcomp(&re, pattern, REG_NOSUB | REG_NEWLINE | REG_EXTENDED);
    if (rcv != 0) {
        int len = regerror(rcv, &re, NULL, 0);
        char *estr = malloc(len);
        regerror(rcv, &re, estr, len);
        fprintf(stderr, "Error compiling regex: %s\n", estr);
        exit(1);
    }
    int lines_size = 10;
    int lines_pos = -1;
    char **lines = malloc(sizeof(char*) * 10);
    size_t *lines_sizes = malloc(sizeof(size_t) * 10);
    for (i=0; i < lines_size; i++)
        lines_sizes[i] = 0;
    char *buf;
    size_t size = 0;
    while (-1 != getline(&buf, &size, fp)) {
        if (0 == regexec(&re, buf, 0, NULL, 0)) {
            lines_pos = 0;
        }
        if (lines_pos != -1) {
            lines[lines_pos] = buf;
            lines_sizes[lines_pos++] = size;
            if (lines_pos == lines_size) {
                lines_size *= 2;
                lines = realloc(lines, sizeof(char*) * lines_size);
                lines_sizes = realloc(lines_sizes, sizeof(size_t) * lines_size);
                for (i=lines_size / 2; i<lines_size; i++)
                    lines_sizes[i] = 0;
            }
            buf = lines[lines_pos];
            size = lines_sizes[lines_pos];
        }
    }
    for (i=0; i<lines_pos; i++)
        printf("%s", lines[i]);
    return 0;
}

int tail_regex(FILE *fp, char *pattern) {
    regex_t re;
    int rcv = regcomp(&re, pattern, REG_NOSUB | REG_NEWLINE | REG_EXTENDED);
    if (rcv != 0) {
        int len = regerror(rcv, &re, NULL, 0);
        char *estr = malloc(len);
        regerror(rcv, &re, estr, len);
        fprintf(stderr, "Error compiling regex: %s\n", estr);
        exit(1);
    }
    char *buf;
    size_t size = 0;
    long int tmppos = 0;
    long int matchpos = -1;
    tmppos = ftell(fp);
    while (-1 != getline(&buf, &size, fp)) {
        if (0 == regexec(&re, buf, 0, NULL, 0)) {
            matchpos = tmppos;
        }
        tmppos = ftell(fp);
    }
    if (matchpos != -1) {
        fseek(fp, matchpos, SEEK_SET);
        while (-1 != getline(&buf, &size, fp)) {
            printf("%s", buf);
            tail_quit(buf);
        }
    }
    return 0;
}

int tail_skipstart(FILE *fp, int numlines) {
    char *buf;
    size_t size = 0;
    while (numlines > 0 && -1 != getline(&buf, &size, fp)) {
        numlines--;
    }
    while (-1 != getline(&buf, &size, fp)) {
        printf("%s", buf);
        tail_quit(buf);
    }
    return 0;
}

int tail_follow(FILE *fp) {
    char *buf;
    size_t size = 0;
    int v;
    while (1) {
        v = getline(&buf, &size, fp);
        if (v != -1) {
            printf("%s", buf);
            tail_quit(buf);
        } else
            sleep(0.5);
    }
    return 0;
}

int main(int argc, char **argv) {
    int num_lines = -1;
    int num_bytes = 0;
    int first = 0;
    int last = 0;
    int i;
    int mode = MODE_NORMAL;
    int follow = 0;
    char *filename = NULL;
    char *regex;
    char *quitregex = NULL;
    for (i=1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'r') {
            mode = MODE_REGEX;
            regex = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1] == 'u') {
            quitregex = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1] == 'n') {
            if (argv[i+1][0] == '+') {
                num_lines = atoi(argv[++i] + 1);
                mode = MODE_SKIPSTART;
            } else
                num_lines = atoi(argv[++i]);
        } else if (argv[i][0] == '-' && argv[i][1] >= '0' &&
                argv[i][1] <= '9') {
            num_lines = atoi(argv[i] + 1);
        } else if (argv[i][0] == '+' && argv[i][1] >= '0' &&
                argv[i][1] <= '9') {
            num_lines = atoi(argv[i] + 1);
            mode = MODE_SKIPSTART;
        } else if (argv[i][0] == '-' && argv[i][1] == 'c') {
            if (argv[i+1][0] == '+')
                num_bytes = -atoi(argv[++i] + 1);
            else
                num_bytes = atoi(argv[++i]);
            mode = MODE_BYTES;
        } else if (argv[i][0] == '-' && argv[i][1] == 'f') {
            follow = 1;
        } else {
            filename = argv[i];
        }
    }
    if (quitregex != NULL) {
        int rcv = regcomp(&quitre, quitregex,
                REG_NOSUB | REG_NEWLINE | REG_EXTENDED);
        if (rcv != 0) {
            int len = regerror(rcv, &quitre, NULL, 0);
            char *estr = malloc(len);
            regerror(rcv, &quitre, estr, len);
            fprintf(stderr, "Error compiling regex: %s\n", estr);
            exit(1);
        }
        quit_mode |= QUIT_REGEX;
    }
    FILE *fp = stdin;
    if (filename != NULL && strcmp(filename, "-") != 0)
        fp = fopen(filename, "r");
    if (ispipe(fp))
        follow = 0;
    int rv;
    if (mode == MODE_REGEX && -1 == ftell(fp))
        rv = tail_regex_unseekable(fp, regex);
    else if (mode == MODE_REGEX)
        rv = tail_regex(fp, regex);
    if (mode == MODE_SKIPSTART)
        rv = tail_skipstart(fp, num_lines);
    if (mode == MODE_BYTES)
        return tail_bytes(fp, num_bytes, follow);
    if (mode != MODE_NORMAL) {
        if (follow)
            tail_follow(fp);
        return rv;
    }
    if (num_lines == -1)
        num_lines = 10;
    char *buf[num_lines + 1];
    size_t siz[num_lines + 1];
    for (i=0; i<num_lines + 1; i++) {
        buf[i] = malloc(128);
        siz[i] = 128;
    }
    while (-1 != getline(&buf[last], &siz[i], fp)) {
        last++;
        if (last == num_lines + 1)
            last = 0;
        if (last == first)
            first++;
        if (first == num_lines + 1)
            first = 0;
    }
    for (i=first; i != last; i++) {
        printf("%s", buf[i]);
        if (i == num_lines)
            i = -1;
    }
    if (follow)
        tail_follow(fp);
    return 0;
}
