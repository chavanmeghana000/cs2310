#include "writer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Table entry for mnemonic→bits */
typedef struct { const char *mnemonic, *bits; } TableEntry;

/* DEST field lookup */
static TableEntry dest_table[] = {
    {"",   "000"}, {"M",   "001"}, {"D",   "010"}, {"MD",  "011"},
    {"A",  "100"}, {"AM",  "101"}, {"AD",  "110"}, {"AMD", "111"},
};
static const size_t DEST_COUNT = sizeof dest_table/sizeof *dest_table;

/* COMP field lookup */
static TableEntry comp_table[] = {
    {"0",   "0101010"}, {"1",    "0111111"}, {"-1",   "0111010"},
    {"D",   "0001100"}, {"A",    "0110000"}, {"!D",   "0001101"},
    {"!A",  "0110001"}, {"-D",   "0001111"}, {"-A",   "0110011"},
    {"D+1", "0011111"}, {"A+1",  "0110111"}, {"D-1",  "0001110"},
    {"A-1", "0110010"}, {"D+A",  "0000010"}, {"D-A",  "0010011"},
    {"A-D", "0000111"}, {"D&A",  "0000000"}, {"D|A",  "0010101"},
    {"M",   "1110000"}, {"!M",   "1110001"}, {"-M",   "1110011"},
    {"M+1", "1110111"}, {"M-1",  "1110010"}, {"D+M",  "1000010"},
    {"D-M", "1010011"}, {"M-D",  "1000111"}, {"D&M",  "1000000"},
    {"D|M", "1010101"},
};
static const size_t COMP_COUNT = sizeof comp_table/sizeof *comp_table;

/* JUMP field lookup */
static TableEntry jump_table[] = {
    {"",    "000"}, {"JGT", "001"}, {"JEQ", "010"}, {"JGE", "011"},
    {"JLT", "100"}, {"JNE", "101"}, {"JLE", "110"}, {"JMP", "111"},
};
static const size_t JUMP_COUNT = sizeof jump_table/sizeof *jump_table;

/* Look up bits by scanning the given table */
static const char *lookup_bits(const TableEntry *tbl, size_t n, const char *mn) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(tbl[i].mnemonic, mn) == 0) {
            return tbl[i].bits;
        }
    }
    return NULL;  // error
}

/* Build a 16-bit binary string for an A-instruction or C-instruction */
static char *translate_to_binary(const char *inst) {
    if (inst[0] == '@') {
        /* A-instruction: @value */
        int value = atoi(inst + 1);
        char *bits = malloc(17);
        if (!bits) exit(EXIT_FAILURE);
        bits[16] = '\0';
        for (int i = 15; i >= 0; i--) {
            bits[i] = (value & 1) ? '1' : '0';
            value >>= 1;
        }
        return bits;
    }

    /* C-instruction: dest=comp;jump (each part optional) */
    char dest[4] = "", comp[8] = "", jump[4] = "";
    const char *eq = strchr(inst, '=');
    const char *sc = strchr(inst, ';');

    if (eq) {
        size_t len = eq - inst;
        memcpy(dest, inst, len);
        dest[len] = '\0';
        strncpy(comp, eq + 1, sc ? (size_t)(sc - eq - 1) : strlen(eq + 1));
        comp[sc ? (size_t)(sc - eq - 1) : strlen(eq + 1)] = '\0';
    } else {
        /* no dest */
        size_t clen = sc ? (size_t)(sc - inst) : strlen(inst);
        strncpy(comp, inst, clen);
        comp[clen] = '\0';
    }

    if (sc) {
        strcpy(jump, sc + 1);
    }

    const char *cb = lookup_bits(comp_table, COMP_COUNT, comp);
    const char *db = lookup_bits(dest_table, DEST_COUNT, dest);
    const char *jb = lookup_bits(jump_table, JUMP_COUNT, jump);

    if (!cb || !db || !jb) {
        fprintf(stderr, "Error: invalid C-instruction '%s'\n", inst);
        exit(EXIT_FAILURE);
    }

    char *out = malloc(17);
    if (!out) exit(EXIT_FAILURE);
    /* C-instructions start with "111" */
    snprintf(out, 17, "111%s%s%s", cb, db, jb);
    return out;
}

/* —— Public API —— */

Writer *
writer_create(const char *asm_path, const char *hack_path)
{
    Writer *w = malloc(sizeof *w);
    if (!w) { perror("malloc"); exit(EXIT_FAILURE); }

    w->parser = parser_create(asm_path);
    fprintf(stderr, "DEBUG: writer_create opening '%s'\n", hack_path);

    w->out_fp = fopen(hack_path, "w");
    if (!w->out_fp) { perror("fopen"); exit(EXIT_FAILURE); }

    return w;
}

void
writer_write(Writer *w)
{
    while (parser_has_more(w->parser)) {
        char *inst = parser_advance(w->parser);
        char *bin  = translate_to_binary(inst);
        fprintf(stderr, "DEBUG: writing -> %s\n", bin);
        fprintf(w->out_fp, "%s\n", bin);
        free(bin);
    }
    fflush(w->out_fp);
}

void
writer_destroy(Writer *w)
{
    if (!w) return;
    fclose(w->out_fp);
    parser_destroy(w->parser);
    free(w);
}
