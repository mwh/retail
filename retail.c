// retail - tail with regular expressions
// Copyright (C) 2011, 2014 Michael Homer
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
#include <errno.h>

int ispipe(FILE *);
#ifdef NEED_GETLINE
// getline was only recently standardised and is implemented at the end
// of the file; see there for more details.
size_t getline(char **, size_t *, FILE *);
#endif

// There are four supported main modes of operation:
// MODE_NORMAL is the default behaviour, printing the last N lines.
#define MODE_NORMAL 0
// MODE_REGEX is the novel part of retail, printing the lines after
// a regular expression matches.
#define MODE_REGEX 1
// MODE_SKIPSTART implements the POSIX -n +N behaviour, printing
// all lines after the first N are skipped.
#define MODE_SKIPSTART 2
// MODE_BYTES implements the POSIX -c N behaviour, where N is
// interpreted as bytes.
#define MODE_BYTES 3

// There are currently only two supported termination modes:
// The default is to terminate at the end of the file.
// QUIT_REGEX, if set, causes the program to terminate on printing
// a line matching a regular expression.
#define QUIT_REGEX 1

#define VERSION_MAJOR 0
#define VERSION_MINOR 1

// If zero, terminate at the end of the file. If set to one of
// the QUIT_* constants, override that behaviour.
int quit_mode = 0;
// In the QUIT_REGEX termination mode this regular expression
// determines when to exit.
regex_t quitre;

// Output from the first occurrence of pattern, not the last.
int print_from_first = 0;

// The program name used to run is saved in progname.
char *progname;

// tail_quit is called with each output line to check whether
// to terminate. It is called after printing the line.
// If in QUIT_REGEX mode, checks the current line against
// quitre, and terminates if it matches.
void tail_quit(char *line) {
    if (quit_mode & QUIT_REGEX) {
        if (0 == regexec(&quitre, line, 0, NULL, 0)) {
            exit(0);
        }
    }
}

// tail_bytes implements -c N (bytes) mode.
// num_bytes is N; if it is negative it is relative to the
// start of the file and indicates skipping that many bytes.
// follow is 1 if the program should continue reading from
// the file as it changes until killed.
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
                usleep(500000);
            read = fread(buf, 1, 2048, fp);
        }
    }
    return 0;
}

// tail_regex_unseekable implements -r for streams that are
// unseekable, such as standard input and fifos. It caches
// lines read in an array until it reaches the end of the
// file and knows that these begin with the last matching
// line.
int tail_regex_unseekable(FILE *fp, char *pattern) {
    regex_t re;
    int i;
    int rcv = regcomp(&re, pattern, REG_NOSUB | REG_NEWLINE | REG_EXTENDED);
    if (rcv != 0) {
        int len = regerror(rcv, &re, NULL, 0);
        char *estr = malloc(len);
        regerror(rcv, &re, estr, len);
        fprintf(stderr, "%s: error compiling regex: %s\n", progname, estr);
        exit(1);
    }
    // The lines array will be resized dynamically if
    // required. It is always lines_size long and the next
    // line will go at lines_pos.
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
            if (!print_from_first || lines_pos == -1)
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

// tail_regex implements -r for streams that are seekable,
// such as ordinary files. It reads through the entire file
// remembering the seek position of any matching line, and
// returns to that location at the end to print from.
int tail_regex(FILE *fp, char *pattern) {
    regex_t re;
    int rcv = regcomp(&re, pattern, REG_NOSUB | REG_NEWLINE | REG_EXTENDED);
    if (rcv != 0) {
        int len = regerror(rcv, &re, NULL, 0);
        char *estr = malloc(len);
        regerror(rcv, &re, estr, len);
        fprintf(stderr, "%s: error compiling regex: %s\n", progname, estr);
        exit(1);
    }
    char *buf;
    size_t size = 0;
    // tmppos saves the position of the start of the most
    // recent line, in case it matches.
    long int tmppos = 0;
    // matchpos saves the position of the last matching line.
    // Its value is copied from tmppos when the line matched.
    long int matchpos = -1;
    tmppos = ftell(fp);
    while (-1 != getline(&buf, &size, fp)) {
        if (0 == regexec(&re, buf, 0, NULL, 0)) {
            matchpos = tmppos;
            if (print_from_first)
                break;
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

// tail_skipstart implements -n +N (skip from start) mode.
// numlines is the number of lines to skip; it must be
// non-negative.
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

// tail_follow implements -f (follow) behaviour for the
// default mode. It keeps the file open and checks for new
// lines every half a second.
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
            usleep(500000);
    }
    return 0;
}

// help outputs the --help text.
int help(char *progname) {
    printf("Usage: %s [OPTION]... [FILE]\n", progname);
    puts("Print the last 10 lines of FILE to standard output.");
    puts("If no FILE given or FILE is -, use standard input.");
    puts("");
    puts("Options:");
    puts("  -b         with -r, begin at first matching line, not last.");
    puts("  -c N       print the last N bytes; -c +N will begin with");
    puts("             the Nth byte");
    puts("  -f         continue reading from file as data is appended");
    puts("  -n N       output the last N lines; -n +N will begin with");
    puts("             the Nth line");
    puts("  -r REGEX   output lines beginning with last line matching");
    puts("             extended regular expression REGEX");
    puts("  -u REGEX   stop following file when a line matches extended");
    puts("             regular expression REGEX, and exit.");
    puts("  --help     display help and exit");
    puts("  --version  display version information and exit");
    puts("");
    puts("The -r and -u options take an extended POSIX regular expression as");
    puts("argument. These expressions are matched against each line without");
    puts("implicit anchoring. ^ and $ will match the start and end of a line");
    puts("respectively. -u is meaningful only when joined with -f, and will");
    puts("output the matching line before terminating.");
    return 0;
}

// version outputs the --version information.
int version() {
    printf("%s %i.%i\n", progname, VERSION_MAJOR, VERSION_MINOR);
    puts("Copyright (C) 2011, 2014 Michael Homer.");
    puts("Licenced under the GNU GPL version 3 or later.");
    puts("This program comes with ABSOLUTELY NO WARRANTY.");
    puts("This is free software, and you are welcome to redistribute it");
    puts("under certain conditions; see the LICENCE file in the source or");
    puts("<http://gnu.org/licenses/gpl.html> for details.");
    return 0;
}

int main(int argc, char **argv) {
    // num_lines holds the argument given to -n, if any.
    int num_lines = -1;
    // num_bytes holds the argument given to -c, if any.
    int num_bytes = 0;
    // first and last are used by the default mode (below).
    int first = 0;
    int last = 0;
    int i;
    int mode = MODE_NORMAL;
    // follow is set to 1 if -f was given.
    int follow = 0;
    char *filename = NULL;
    char *regex;
    char *quitregex = NULL;
    // This loop computes progname for error and help outputs
    // as the basename of argv[0].
    for (i=0; i<strlen(argv[0]); i++)
        if (argv[0][i] == '/')
            progname = argv[0] + i + 1;
    if (!progname)
        progname = argv[0];
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
        } else if (argv[i][0] == '-' && argv[i][1] == 'b') {
            print_from_first = 1;
        } else if (strcmp(argv[i], "--help") == 0
                || strcmp(argv[i], "-h") == 0) {
            return help(argv[0]);
        } else if (strcmp(argv[i], "--version") == 0
                || strcmp(argv[i], "-v") == 0) {
            return version();
        } else if (argv[i][0] == '-' && argv[i][1] != 0) {
            fprintf(stderr, "%s: unrecognised option %s.\n", progname,
                    argv[i]);
            fprintf(stderr, "Use `%s --help` for usage details.\n", argv[0]);
            exit(1);
        } else {
            filename = argv[i];
        }
    }
    // The quit regex is used repeatedly from multiple modes,
    // so precalculate it now and set quit_mode to QUIT_REGEX.
    if (quitregex != NULL) {
        int rcv = regcomp(&quitre, quitregex,
                REG_NOSUB | REG_NEWLINE | REG_EXTENDED);
        if (rcv != 0) {
            int len = regerror(rcv, &quitre, NULL, 0);
            char *estr = malloc(len);
            regerror(rcv, &quitre, estr, len);
            fprintf(stderr, "%s: error compiling regex: %s\n", progname, estr);
            exit(1);
        }
        quit_mode |= QUIT_REGEX;
    }
    FILE *fp = stdin;
    if (filename != NULL && strcmp(filename, "-") != 0)
        fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "%s: error opening '%s': %s\n",
                progname, filename, strerror(errno));
        exit(1);
    }
    // Following does not make sense with pipe inputs.
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
    // The default behaviour of POSIX tail is implemented
    // from here on.
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

#ifdef NEED_GETLINE
// GNU getline makes the code simpler and safer than the other
// line-reading functions, but is a GNU extension not
// standardised until POSIX-2008. This implements the behaviour
// for other platforms.
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

// ispipe returns true if fp is a pipe and false otherwise.
int ispipe(FILE *fp) {
    int fd = fileno(fp);
    struct stat st;
    int r = fstat(fd, &st);
    return (fd <= 0 && S_ISFIFO(st.st_mode));
}
