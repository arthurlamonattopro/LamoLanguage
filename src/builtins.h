#ifndef LAMO_BUILTINS_H
#define LAMO_BUILTINS_H

/*
 * builtins.h - Single source of truth for Lamo builtins.
 *
 * Sprint 2 refactor: previously, the same builtin name lists were
 * duplicated across lexer.c (lexer_is_builtin_name), semantic.c
 * (builtin_function_arity, builtin_function_return_type),
 * codegen.c (is_gui_builtin, is_http_builtin, is_lang_builtin), and
 * lamo_v2.c (lamo_is_gui_builtin_name). Adding a new builtin required
 * editing 5 places; forgetting one caused silent bugs. This single
 * table replaces all of them.
 *
 * To add a new builtin:
 *   1. Add an entry to lamo_builtins[] below.
 *   2. Implement the actual function in lamo_runtime.h.
 *   3. Add the codegen case in generate_lang_builtin_call_expr /
 *      generate_gui_call_expr / generate_http_call_expr in codegen.c.
 *
 * This header is included by lexer.c, semantic.c, codegen.c, and
 * lamo_v2.c. It uses only `inline` static functions and a `static
 * const` array, so it does not introduce any new translation units or
 * link-time symbols.
 */

#include <string.h>
#include <stddef.h>

typedef enum {
    BUILTIN_LANG,
    BUILTIN_GUI,
    BUILTIN_HTTP
} BuiltinCategory;

typedef enum {
    BUILTIN_RET_INT,            /* always returns int (or int-shaped) */
    BUILTIN_RET_STRING,         /* always returns string */
    BUILTIN_RET_BOOL,           /* always returns bool */
    BUILTIN_RET_MIRROR_ARG0     /* return type mirrors args[0]'s type (abs) */
} BuiltinRetPolicy;

typedef struct {
    const char* name;
    int arity;
    BuiltinCategory category;
    BuiltinRetPolicy ret_policy;
} BuiltinInfo;

static const BuiltinInfo lamo_builtins[] = {
    /* Language builtins — shadowable by user functions, available everywhere. */
    {"print",      1, BUILTIN_LANG, BUILTIN_RET_INT},
    {"input",      1, BUILTIN_LANG, BUILTIN_RET_INT},
    {"input_int",  1, BUILTIN_LANG, BUILTIN_RET_INT},
    {"input_str",  1, BUILTIN_LANG, BUILTIN_RET_STRING},
    {"isnumber",   1, BUILTIN_LANG, BUILTIN_RET_BOOL},
    {"isstring",   1, BUILTIN_LANG, BUILTIN_RET_BOOL},
    {"exit",       1, BUILTIN_LANG, BUILTIN_RET_INT},
    {"abs",        1, BUILTIN_LANG, BUILTIN_RET_MIRROR_ARG0},
    /* Sprint 3: array builtins. len(arr) returns the element count as int.
     * push(arr, x) appends x and returns 0. pop(arr) removes and returns
     * the last element. */
    {"len",        1, BUILTIN_LANG, BUILTIN_RET_INT},
    {"push",       2, BUILTIN_LANG, BUILTIN_RET_INT},
    {"pop",        1, BUILTIN_LANG, BUILTIN_RET_INT},  /* actually returns the popped value's type, but we report INT conservatively */
    /* GUI builtins — Windows GDI / X11, gated behind LAMO_NEEDS_GUI_RUNTIME. */
    {"gui_open",          3, BUILTIN_GUI, BUILTIN_RET_INT},
    {"gui_should_close",  0, BUILTIN_GUI, BUILTIN_RET_INT},
    {"gui_begin_frame",   3, BUILTIN_GUI, BUILTIN_RET_INT},
    {"gui_draw_rect",     7, BUILTIN_GUI, BUILTIN_RET_INT},
    {"gui_draw_text",     6, BUILTIN_GUI, BUILTIN_RET_INT},
    {"gui_end_frame",     0, BUILTIN_GUI, BUILTIN_RET_INT},
    {"gui_close",         0, BUILTIN_GUI, BUILTIN_RET_INT},
    /* HTTP builtins — link with -lws2_32 on Windows, plain sockets on POSIX. */
    {"http_route",      2, BUILTIN_HTTP, BUILTIN_RET_INT},
    {"http_serve",      1, BUILTIN_HTTP, BUILTIN_RET_INT},
    {"http_serve_once", 1, BUILTIN_HTTP, BUILTIN_RET_INT},
};
#define LAMO_BUILTINS_COUNT (sizeof(lamo_builtins) / sizeof(lamo_builtins[0]))

/* Lookup by name. Returns NULL if not found. */
static inline const BuiltinInfo* lamo_builtin_lookup(const char* name) {
    size_t i;
    if (!name) return NULL;
    for (i = 0; i < LAMO_BUILTINS_COUNT; i++) {
        if (strcmp(lamo_builtins[i].name, name) == 0) {
            return &lamo_builtins[i];
        }
    }
    return NULL;
}

/* Convenience predicates — replace the old per-file is_*_builtin() helpers. */
static inline int lamo_builtin_is_lang(const char* name) {
    const BuiltinInfo* b = lamo_builtin_lookup(name);
    return b != NULL && b->category == BUILTIN_LANG;
}
static inline int lamo_builtin_is_gui(const char* name) {
    const BuiltinInfo* b = lamo_builtin_lookup(name);
    return b != NULL && b->category == BUILTIN_GUI;
}
static inline int lamo_builtin_is_http(const char* name) {
    const BuiltinInfo* b = lamo_builtin_lookup(name);
    return b != NULL && b->category == BUILTIN_HTTP;
}

/* Returns arity (-1 if not a builtin). Replaces semantic.c's
 * builtin_function_arity(). */
static inline int lamo_builtin_arity(const char* name) {
    const BuiltinInfo* b = lamo_builtin_lookup(name);
    return b ? b->arity : -1;
}

#endif /* LAMO_BUILTINS_H */
