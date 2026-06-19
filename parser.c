#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define LINE_BUF       256
#define CMD_INIT_CAP   128
#define LAB_INIT_CAP    64

/* Trim leading/trailing whitespace and CR/LF */
static char *trim(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && (unsigned char)*end <= ' ') end--;
    end[1] = '\0';
    return s;
}

/* Strip '//' comments and any stray '\r' */
static void strip_comment(char *s) {
    char *c = strstr(s, "//");
    if (c) *c = '\0';
    if ((c = strchr(s, '\r'))) *c = '\0';
}

/* Append a command string to p->commands[], growing the array as needed */
static void add_command(Parser *p, const char *cmd) {
    if (p->count == p->capacity) {
        p->capacity *= 2;
        p->commands = realloc(p->commands,
                              p->capacity * sizeof *p->commands);
    }
    p->commands[p->count++] = strdup(cmd);
}

/* Find a label by key, or return -1 */
static int find_label(Parser *p, const char *key) {
    for (int i = 0; i < p->label_count; i++) {
        if (strcmp(p->labels[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

/* Insert or update label key→value (value is string "@<addr>") */
static void put_label(Parser *p, const char *key, const char *value) {
    int idx = find_label(p, key);
    if (idx >= 0) {
        free(p->labels[idx].value);
        p->labels[idx].value = strdup(value);
    } else {
        if (p->label_count == p->label_cap) {
            p->label_cap *= 2;
            p->labels = realloc(p->labels,
                                p->label_cap * sizeof *p->labels);
        }
        p->labels[p->label_count].key   = strdup(key);
        p->labels[p->label_count].value = strdup(value);
        p->label_count++;
    }
}

/* Resolve a symbol to its "@<addr>" string, allocating new var if needed */
static const char *resolve_symbol(Parser *p, const char *sym) {
    int idx = find_label(p, sym);
    if (idx >= 0) {
        return p->labels[idx].value;
    }
    char buf[16];
    snprintf(buf, sizeof buf, "@%d", p->next_var++);
    put_label(p, sym, buf);
    /* Return the stored strdup’ed value */
    return p->labels[p->label_count - 1].value;
}

/* Preload R0–R15 and SP, LCL, ARG, THIS, THAT */
static void init_predefined(Parser *p) {
    // Special pointers
    put_label(p, "SP",     "@0");
    put_label(p, "LCL",    "@1");
    put_label(p, "ARG",    "@2");
    put_label(p, "THIS",   "@3");
    put_label(p, "THAT",   "@4");

    // General-purpose registers
    put_label(p, "R0",  "@0");
    put_label(p, "R1",  "@1");
    put_label(p, "R2",  "@2");
    put_label(p, "R3",  "@3");
    put_label(p, "R4",  "@4");
    put_label(p, "R5",  "@5");
    put_label(p, "R6",  "@6");
    put_label(p, "R7",  "@7");
    put_label(p, "R8",  "@8");
    put_label(p, "R9",  "@9");
    put_label(p, "R10", "@10");
    put_label(p, "R11", "@11");
    put_label(p, "R12", "@12");
    put_label(p, "R13", "@13");
    put_label(p, "R14", "@14");
    put_label(p, "R15", "@15");

    // Memory-mapped I/O
    put_label(p, "SCREEN", "@16384");
    put_label(p, "KBD",    "@24576");
}

Parser *parser_create(const char *asm_path) {
    Parser *p = calloc(1, sizeof *p);
    p->capacity    = CMD_INIT_CAP;
    p->commands    = malloc(p->capacity * sizeof *p->commands);
    p->count       = 0;
    p->index       = 0;
    p->label_cap   = LAB_INIT_CAP;
    p->labels      = malloc(p->label_cap * sizeof *p->labels);
    p->label_count = 0;
    p->next_var    = 16;

    init_predefined(p);

    p->file = fopen(asm_path, "r");
    if (!p->file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    char line[LINE_BUF];
    int instruction_count = 0;

    /* PASS 1: record label declarations and count real instructions */
    while (fgets(line, sizeof line, p->file)) {
        strip_comment(line);
        char *s = trim(line);
        if (*s == '\0') continue;
        if (s[0] == '(') {
            /* e.g. (OUTPUT_D) → key="OUTPUT_D", value="@<instruction_count>" */
            int len = strlen(s) - 2;
            char key[LINE_BUF];
            memcpy(key, s + 1, len);
            key[len] = '\0';
            char addr[16];
            snprintf(addr, sizeof addr, "@%d", instruction_count);
            put_label(p, key, addr);
        } else {
            instruction_count++;
        }
    }

    /* PASS 2: rewind, then store each A/C instruction, resolving symbols */
    rewind(p->file);
    while (fgets(line, sizeof line, p->file)) {
        strip_comment(line);
        char *s = trim(line);
        if (*s == '\0' || s[0] == '(') continue;

        if (s[0] == '@') {
            /* A-instruction: either numeric or symbol */
            if (isdigit((unsigned char)s[1])) {
                add_command(p, s);
            } else {
                const char *res = resolve_symbol(p, s + 1);
                add_command(p, res);
            }
        } else {
            /* C-instruction */
            add_command(p, s);
        }
    }

    rewind(p->file);
    return p;
}

bool parser_has_more(Parser *p) {
    return p->index < p->count;
}

char *parser_advance(Parser *p) {
    if (p->index < p->count) {
        return p->commands[p->index++];
    }
    return NULL;
}

char *parser_current(Parser *p) {
    if (p->index > 0) {
        return p->commands[p->index - 1];
    }
    return NULL;
}

char *parser_label_lookup(Parser *p, const char *instr) {
    int idx = find_label(p, instr + (*instr == '@'));
    return (idx >= 0) ? p->labels[idx].value : NULL;
}

void parser_destroy(Parser *p) {
    if (!p) return;
    fclose(p->file);
    for (int i = 0; i < p->count; i++) {
        free(p->commands[i]);
    }
    free(p->commands);
    for (int i = 0; i < p->label_count; i++) {
        free(p->labels[i].key);
        free(p->labels[i].value);
    }
    free(p->labels);
    free(p);
}
