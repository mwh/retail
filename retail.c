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

#define MODE_NORMAL 0
#define MODE_REGEX 1
#define MODE_SKIPSTART 2

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
    int size = 0;
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
        }
    }
    return 0;
}

int tail_skipstart(FILE *fp, int numlines) {
    char *buf;
    int size = 0;
    while (numlines > 0 && -1 != getline(&buf, &size, fp)) {
        numlines--;
    }
    while (-1 != getline(&buf, &size, fp)) {
        printf("%s", buf);
    }
    return 0;
}

int main(int argc, char **argv) {
    int num_lines = -1;
    int first = 0;
    int last = 0;
    int i;
    int mode = MODE_NORMAL;
    char *filename = NULL;
    char *regex;
    for (i=1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'r') {
            mode = MODE_REGEX;
            regex = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1] == 'n') {
            num_lines = atoi(argv[++i]);
        } else if (argv[i][0] == '-' && argv[i][1] >= '0' &&
                argv[i][1] <= '9') {
            num_lines = atoi(argv[i] + 1);
        } else if (argv[i][0] == '+' && argv[i][1] >= '0' &&
                argv[i][1] <= '9') {
            num_lines = atoi(argv[i] + 1);
            mode = MODE_SKIPSTART;
        } else {
            filename = argv[i];
        }
    }
    FILE *fp = stdin;
    if (filename != NULL)
        fp = fopen(filename, "r");
    int rv;
    if (mode == MODE_REGEX)
        rv = tail_regex(fp, regex);
    if (mode == MODE_SKIPSTART)
        rv = tail_skipstart(fp, num_lines);
    if (mode != MODE_NORMAL)
        return rv;
    if (num_lines == -1)
        num_lines = 10;
    char *buf[num_lines + 1];
    int siz[num_lines + 1];
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
    return 0;
}
