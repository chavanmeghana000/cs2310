#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Replace the trailing ".asm" with ".hack"; returns a malloc’d string or NULL */
static char *
replace_ext_hack(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot || strcmp(dot, ".asm") != 0) {
        return NULL;
    }
    size_t base = dot - path;
    /* 5 chars for ".hack" + 1 for '\0' */
    char *out = malloc(base + 6);
    if (!out) {
        return NULL;
    }
    memcpy(out,      path, base);
    memcpy(out + base, ".hack", 6);
    return out;
}

Assembler *
assembler_create(const char *asm_path)
{
    char *hack_path = replace_ext_hack(asm_path);
    if (!hack_path) {
        fprintf(stderr, "Error: '%s' is not a .asm file\n", asm_path);
        return NULL;
    }

    Assembler *a = malloc(sizeof *a);
    if (!a) {
        free(hack_path);
        return NULL;
    }

    a->asm_path  = strdup(asm_path);
    a->hack_path = hack_path;
    a->writer    = writer_create(a->asm_path, a->hack_path);
    return a;
}

void
assembler_compile(Assembler *a)
{
    if (!a) return;
    writer_write(a->writer);
}

void
assembler_destroy(Assembler *a)
{
    if (!a) return;
    writer_destroy(a->writer);
    free(a->asm_path);
    free(a->hack_path);
    free(a);
}

int
main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.asm>\n", argv[0]);
        return EXIT_FAILURE;
    }

    Assembler *asmblr = assembler_create(argv[1]);
    if (!asmblr) return EXIT_FAILURE;

    assembler_compile(asmblr);
    assembler_destroy(asmblr);
    return EXIT_SUCCESS;
}
