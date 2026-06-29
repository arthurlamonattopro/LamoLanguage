#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int indent_level = 0;

static void print_indent(FILE* out) {
    int i;
    for (i = 0; i < indent_level; i++) {
        fprintf(out, "    ");
    }
}

// Prefixo adicionado a todos os identificadores declarados pelo usuário para
// evitar colisões com nomes da libc (abs, exit, index, ...).
// Builtins da linguagem (print, input, ...) e builtins GUI/HTTP continuam
// sendo detectados pelo nome original.
#define LAMO_USER_PREFIX "lamo_u_"

static const char* user_name(const char* name) {
    // Retorna um ponteiro estático temporário; bom o suficiente para uso
    // imediato em fprintf. Buffer grande o suficiente para qualquer nome.
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s%s", LAMO_USER_PREFIX, name);
    return buffer;
}

static void generate_statement_code(ASTNode* node, FILE* out);
static void generate_expression_code(ASTNode* node, FILE* out);
static void generate_call_arguments(ASTNode** args, int arg_count, FILE* out);
static int is_gui_builtin(const char* name);
static int is_http_builtin(const char* name);
static int is_lang_builtin(const char* name);
static void generate_lang_builtin_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out);
static void generate_gui_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out);
static void generate_http_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out);
static void emit_value_runtime(FILE* out);
static void emit_gui_runtime(FILE* out);
static void emit_http_runtime(FILE* out);
static int ast_uses_gui(ASTNode* node);
static int ast_uses_http(ASTNode* node);



static void generate_call_arguments(ASTNode** args, int arg_count, FILE* out) {
    int i;
    for (i = 0; i < arg_count; i++) {
        if (i > 0) {
            fprintf(out, ", ");
        }
        generate_expression_code(args[i], out);
    }
}

static int is_gui_builtin(const char* name) {
    return strcmp(name, "gui_open") == 0 ||
           strcmp(name, "gui_should_close") == 0 ||
           strcmp(name, "gui_begin_frame") == 0 ||
           strcmp(name, "gui_draw_rect") == 0 ||
           strcmp(name, "gui_draw_text") == 0 ||
           strcmp(name, "gui_end_frame") == 0 ||
           strcmp(name, "gui_close") == 0;
}

static int is_http_builtin(const char* name) {
    return strcmp(name, "http_route") == 0 ||
           strcmp(name, "http_serve") == 0 ||
           strcmp(name, "http_serve_once") == 0;
}

static int is_lang_builtin(const char* name) {
    return strcmp(name, "print") == 0 ||
           strcmp(name, "input") == 0 ||
           strcmp(name, "input_int") == 0 ||
           strcmp(name, "input_str") == 0 ||
           strcmp(name, "isnumber") == 0 ||
           strcmp(name, "isstring") == 0 ||
           strcmp(name, "exit") == 0 ||
           strcmp(name, "abs") == 0;
}



static void generate_lang_builtin_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out) {
    (void)arg_count;

    if (strcmp(name, "print") == 0) {
        fprintf(out, "(lamo_print_value(");
        generate_expression_code(args[0], out);
        fprintf(out, "), lamo_make_int(0))");
        return;
    }
    if (strcmp(name, "input") == 0) {
        fprintf(out, "lamo_input_value(");
        generate_expression_code(args[0], out);
        fprintf(out, ")");
        return;
    }
    if (strcmp(name, "input_int") == 0) {
        fprintf(out, "lamo_input_int_value(");
        generate_expression_code(args[0], out);
        fprintf(out, ")");
        return;
    }
    if (strcmp(name, "input_str") == 0) {
        fprintf(out, "lamo_input_str_value(");
        generate_expression_code(args[0], out);
        fprintf(out, ")");
        return;
    }
    if (strcmp(name, "isnumber") == 0) {
        fprintf(out, "lamo_isnumber_value(");
        generate_expression_code(args[0], out);
        fprintf(out, ")");
        return;
    }
    if (strcmp(name, "isstring") == 0) {
        fprintf(out, "lamo_isstring_value(");
        generate_expression_code(args[0], out);
        fprintf(out, ")");
        return;
    }
    if (strcmp(name, "exit") == 0) {
        fprintf(out, "(exit(lamo_as_int(");
        generate_expression_code(args[0], out);
        fprintf(out, ")), lamo_make_int(0))");
        return;
    }
    if (strcmp(name, "abs") == 0) {
        fprintf(out, "lamo_abs_value(");
        generate_expression_code(args[0], out);
        fprintf(out, ")");
        return;
    }
    fprintf(out, "lamo_make_int(0)");
}



static void generate_gui_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out) {
    if (strcmp(name, "gui_open") == 0 && arg_count == 3) {
        fprintf(out, "lamo_make_int(lamo_gui_open(lamo_as_int(");
        generate_expression_code(args[0], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[1], out);
        fprintf(out, "), lamo_as_cstring(");
        generate_expression_code(args[2], out);
        fprintf(out, ")))");
        return;
    }
    if (strcmp(name, "gui_should_close") == 0 && arg_count == 0) {
        fprintf(out, "lamo_make_int(lamo_gui_should_close())");
        return;
    }
    if (strcmp(name, "gui_begin_frame") == 0 && arg_count == 3) {
        fprintf(out, "(lamo_gui_begin_frame(lamo_as_int(");
        generate_expression_code(args[0], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[1], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[2], out);
        fprintf(out, ")), lamo_make_int(0))");
        return;
    }
    if (strcmp(name, "gui_draw_rect") == 0 && arg_count == 7) {
        fprintf(out, "(lamo_gui_draw_rect(lamo_as_int(");
        generate_expression_code(args[0], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[1], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[2], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[3], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[4], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[5], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[6], out);
        fprintf(out, ")), lamo_make_int(0))");
        return;
    }
    if (strcmp(name, "gui_draw_text") == 0 && arg_count == 6) {
        fprintf(out, "(lamo_gui_draw_text(lamo_as_cstring(");
        generate_expression_code(args[0], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[1], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[2], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[3], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[4], out);
        fprintf(out, "), lamo_as_int(");
        generate_expression_code(args[5], out);
        fprintf(out, ")), lamo_make_int(0))");
        return;
    }
    if (strcmp(name, "gui_end_frame") == 0 && arg_count == 0) {
        fprintf(out, "(lamo_gui_end_frame(), lamo_make_int(0))");
        return;
    }
    if (strcmp(name, "gui_close") == 0 && arg_count == 0) {
        fprintf(out, "(lamo_gui_close(), lamo_make_int(0))");
        return;
    }
    fprintf(out, "lamo_make_int(0)");
}



static void generate_http_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out) {
    if (strcmp(name, "http_route") == 0 && arg_count == 2) {
        fprintf(out, "(lamo_http_add_route(lamo_as_cstring(");
        generate_expression_code(args[0], out);
        fprintf(out, "), lamo_as_cstring(");
        generate_expression_code(args[1], out);
        fprintf(out, ")), lamo_make_int(0))");
        return;
    }
    if (strcmp(name, "http_serve") == 0 && arg_count == 1) {
        fprintf(out, "lamo_make_int(lamo_http_run_server(lamo_as_int(");
        generate_expression_code(args[0], out);
        fprintf(out, "), 0))");
        return;
    }
    if (strcmp(name, "http_serve_once") == 0 && arg_count == 1) {
        fprintf(out, "lamo_make_int(lamo_http_run_server(lamo_as_int(");
        generate_expression_code(args[0], out);
        fprintf(out, "), 1))");
        return;
    }
    fprintf(out, "lamo_make_int(0)");
}


static void emit_value_runtime(FILE* out) {
    fprintf(out, "typedef enum {\n");
    fprintf(out, "    LAMO_VALUE_INT,\n");
    fprintf(out, "    LAMO_VALUE_FLOAT,\n");
    fprintf(out, "    LAMO_VALUE_STRING,\n");
    fprintf(out, "    LAMO_VALUE_BOOL\n");
    fprintf(out, "} LamoValueType;\n");
    fprintf(out, "\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    LamoValueType type;\n");
    fprintf(out, "    long long int_value;\n");
    fprintf(out, "    double float_value;\n");
    fprintf(out, "    const char* string_value;\n");
    fprintf(out, "} LamoValue;\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_make_int(long long value) {\n");
    fprintf(out, "    LamoValue result;\n");
    fprintf(out, "    result.type = LAMO_VALUE_INT;\n");
    fprintf(out, "    result.int_value = value;\n");
    fprintf(out, "    result.float_value = 0.0;\n");
    fprintf(out, "    result.string_value = NULL;\n");
    fprintf(out, "    return result;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_make_float(double value) {\n");
    fprintf(out, "    LamoValue result;\n");
    fprintf(out, "    result.type = LAMO_VALUE_FLOAT;\n");
    fprintf(out, "    result.int_value = 0;\n");
    fprintf(out, "    result.float_value = value;\n");
    fprintf(out, "    result.string_value = NULL;\n");
    fprintf(out, "    return result;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_make_bool(int value) {\n");
    fprintf(out, "    LamoValue result;\n");
    fprintf(out, "    result.type = LAMO_VALUE_BOOL;\n");
    fprintf(out, "    result.int_value = value ? 1 : 0;\n");
    fprintf(out, "    result.float_value = 0.0;\n");
    fprintf(out, "    result.string_value = NULL;\n");
    fprintf(out, "    return result;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_make_string(const char* value) {\n");
    fprintf(out, "    LamoValue result;\n");
    fprintf(out, "    result.type = LAMO_VALUE_STRING;\n");
    fprintf(out, "    result.int_value = 0;\n");
    fprintf(out, "    result.float_value = 0.0;\n");
    fprintf(out, "    result.string_value = value ? value : \"\";\n");
    fprintf(out, "    return result;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    // ---- Arena de strings -------------------------------------------------
    // O runtime Lamo nao tem GC nem ownership tracking por LamoValue. Strings
    // alocados dinamicamente (concatenacao, input de string, etc.) sao
    // registrados nesta arena e libertados de uma vez no fim do programa via
    // atexit(). Isso resolve o leak classico de loops que concatenam strings:
    // antes, cada `s = s + "x"` vazava o buffer anterior; agora todos os
    // buffers sao trackeados e libertados no encerramento.
    //
    // Limitacao conhecida: programas longos (ex.: servidor HTTP) ainda veem a
    // arena crescer enquanto rodam. Isso e aceitavel para um prototipo
    // educacional e esta documentado no README.
    fprintf(out, "static char** lamo_string_arena = NULL;\n");
    fprintf(out, "static size_t lamo_string_arena_count = 0;\n");
    fprintf(out, "static size_t lamo_string_arena_capacity = 0;\n");
    fprintf(out, "\n");
    fprintf(out, "static char* lamo_arena_alloc(size_t length) {\n");
    fprintf(out, "    char* block;\n");
    fprintf(out, "    if (lamo_string_arena_count == lamo_string_arena_capacity) {\n");
    fprintf(out, "        size_t new_cap = lamo_string_arena_capacity ? lamo_string_arena_capacity * 2 : 16;\n");
    fprintf(out, "        char** new_arena = realloc(lamo_string_arena, sizeof(char*) * new_cap);\n");
    fprintf(out, "        if (!new_arena) { fprintf(stderr, \"runtime error: string arena exhausted\\n\"); exit(1); }\n");
    fprintf(out, "        lamo_string_arena = new_arena;\n");
    fprintf(out, "        lamo_string_arena_capacity = new_cap;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    block = malloc(length);\n");
    fprintf(out, "    if (!block) { fprintf(stderr, \"runtime error: failed to allocate string\\n\"); exit(1); }\n");
    fprintf(out, "    lamo_string_arena[lamo_string_arena_count++] = block;\n");
    fprintf(out, "    return block;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static char* lamo_arena_strdup(const char* value) {\n");
    fprintf(out, "    size_t length;\n");
    fprintf(out, "    char* copy;\n");
    fprintf(out, "    if (!value) { value = \"\"; }\n");
    fprintf(out, "    length = strlen(value);\n");
    fprintf(out, "    copy = lamo_arena_alloc(length + 1);\n");
    fprintf(out, "    memcpy(copy, value, length + 1);\n");
    fprintf(out, "    return copy;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_arena_free_all(void) {\n");
    fprintf(out, "    size_t i;\n");
    fprintf(out, "    for (i = 0; i < lamo_string_arena_count; i++) {\n");
    fprintf(out, "        free(lamo_string_arena[i]);\n");
    fprintf(out, "    }\n");
    fprintf(out, "    free(lamo_string_arena);\n");
    fprintf(out, "    lamo_string_arena = NULL;\n");
    fprintf(out, "    lamo_string_arena_count = 0;\n");
    fprintf(out, "    lamo_string_arena_capacity = 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static char* lamo_heap_strdup(const char* value) {\n");
    fprintf(out, "    size_t length;\n");
    fprintf(out, "    char* copy;\n");
    fprintf(out, "    if (!value) { value = \"\"; }\n");
    fprintf(out, "    length = strlen(value);\n");
    fprintf(out, "    copy = malloc(length + 1);\n");
    fprintf(out, "    if (!copy) {\n");
    fprintf(out, "        fprintf(stderr, \"runtime error: failed to allocate string\\n\");\n");
    fprintf(out, "        exit(1);\n");
    fprintf(out, "    }\n");
    fprintf(out, "    memcpy(copy, value, length + 1);\n");
    fprintf(out, "    return copy;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_runtime_type_error(const char* message) {\n");
    fprintf(out, "    fprintf(stderr, \"runtime error: %%s\\n\", message);\n");
    fprintf(out, "    exit(1);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "// Coerção: int/float interoperam; string não.\n");
    fprintf(out, "static long long lamo_as_int(LamoValue value) {\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_STRING) {\n");
    fprintf(out, "        lamo_runtime_type_error(\"expected int-compatible value, got string\");\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return (long long)value.float_value;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return value.int_value;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static double lamo_as_float(LamoValue value) {\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_STRING) {\n");
    fprintf(out, "        lamo_runtime_type_error(\"expected number, got string\");\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return value.float_value;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return (double)value.int_value;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static const char* lamo_as_cstring(LamoValue value) {\n");
    fprintf(out, "    if (value.type != LAMO_VALUE_STRING) {\n");
    fprintf(out, "        lamo_runtime_type_error(\"expected string value\");\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return value.string_value ? value.string_value : \"\";\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static char* lamo_value_to_owned_string(LamoValue value) {\n");
    fprintf(out, "    char buffer[64];\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_STRING) {\n");
    fprintf(out, "        return lamo_heap_strdup(lamo_as_cstring(value));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        snprintf(buffer, sizeof(buffer), \"%%g\", value.float_value);\n");
    fprintf(out, "        return lamo_heap_strdup(buffer);\n");
    fprintf(out, "    }\n");
    fprintf(out, "    snprintf(buffer, sizeof(buffer), \"%%lld\", value.int_value);\n");
    fprintf(out, "    return lamo_heap_strdup(buffer);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_concat_values(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    char* left_text = lamo_value_to_owned_string(left);\n");
    fprintf(out, "    char* right_text = lamo_value_to_owned_string(right);\n");
    fprintf(out, "    size_t left_length = strlen(left_text);\n");
    fprintf(out, "    size_t right_length = strlen(right_text);\n");
    fprintf(out, "    char* combined = lamo_arena_alloc(left_length + right_length + 1);\n");
    fprintf(out, "    memcpy(combined, left_text, left_length);\n");
    fprintf(out, "    memcpy(combined + left_length, right_text, right_length + 1);\n");
    fprintf(out, "    free(left_text);\n");
    fprintf(out, "    free(right_text);\n");
    fprintf(out, "    return lamo_make_string(combined);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static int lamo_is_truthy(LamoValue value) {\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_STRING) {\n");
    fprintf(out, "        return value.string_value && value.string_value[0] != '\\0';\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return value.float_value != 0.0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return value.int_value != 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_print_value(LamoValue value) {\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_STRING) {\n");
    fprintf(out, "        printf(\"%%s\\n\", value.string_value ? value.string_value : \"\");\n");
    fprintf(out, "    } else if (value.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        printf(\"%%g\\n\", value.float_value);\n");
    fprintf(out, "    } else {\n");
    fprintf(out, "        printf(\"%%lld\\n\", value.int_value);\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "// Operações aritméticas: se qualquer operando for float, resultado é float.\n");
    fprintf(out, "static LamoValue lamo_add(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_STRING || right.type == LAMO_VALUE_STRING) {\n");
    fprintf(out, "        return lamo_concat_values(left, right);\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_float(lamo_as_float(left) + lamo_as_float(right));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_int(lamo_as_int(left) + lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_sub(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_float(lamo_as_float(left) - lamo_as_float(right));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_int(lamo_as_int(left) - lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_mul(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_float(lamo_as_float(left) * lamo_as_float(right));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_int(lamo_as_int(left) * lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_div(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_float(lamo_as_float(left) / lamo_as_float(right));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_int(lamo_as_int(left) / lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_mod(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_float((double)((long long)lamo_as_float(left) %% (long long)lamo_as_float(right)));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_int(lamo_as_int(left) %% lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_negate(LamoValue value) {\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_FLOAT) return lamo_make_float(-value.float_value);\n");
    fprintf(out, "    return lamo_make_int(-lamo_as_int(value));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_not(LamoValue value) {\n");
    fprintf(out, "    return lamo_make_bool(!lamo_is_truthy(value));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_less(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_bool(lamo_as_float(left) < lamo_as_float(right));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_bool(lamo_as_int(left) < lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_greater(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_bool(lamo_as_float(left) > lamo_as_float(right));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_bool(lamo_as_int(left) > lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_less_equal(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_bool(lamo_as_float(left) <= lamo_as_float(right));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_bool(lamo_as_int(left) <= lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_greater_equal(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_bool(lamo_as_float(left) >= lamo_as_float(right));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_bool(lamo_as_int(left) >= lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_equal(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_STRING || right.type == LAMO_VALUE_STRING) {\n");
    fprintf(out, "        if (left.type != LAMO_VALUE_STRING || right.type != LAMO_VALUE_STRING) {\n");
    fprintf(out, "            return lamo_make_bool(0);\n");
    fprintf(out, "        }\n");
    fprintf(out, "        return lamo_make_bool(strcmp(lamo_as_cstring(left), lamo_as_cstring(right)) == 0);\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (left.type == LAMO_VALUE_FLOAT || right.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        return lamo_make_bool(lamo_as_float(left) == lamo_as_float(right));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_bool(lamo_as_int(left) == lamo_as_int(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_not_equal(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    LamoValue result = lamo_equal(left, right);\n");
    fprintf(out, "    return lamo_make_bool(!lamo_is_truthy(result));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_and(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    return lamo_make_bool(lamo_is_truthy(left) && lamo_is_truthy(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_or(LamoValue left, LamoValue right) {\n");
    fprintf(out, "    return lamo_make_bool(lamo_is_truthy(left) || lamo_is_truthy(right));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_print_prompt(LamoValue prompt) {\n");
    fprintf(out, "    if (prompt.type == LAMO_VALUE_STRING) {\n");
    fprintf(out, "        printf(\"%%s\", lamo_as_cstring(prompt));\n");
    fprintf(out, "    } else if (prompt.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        printf(\"%%g\", prompt.float_value);\n");
    fprintf(out, "    } else {\n");
    fprintf(out, "        printf(\"%%lld\", prompt.int_value);\n");
    fprintf(out, "    }\n");
    fprintf(out, "    fflush(stdout);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    // input(prompt) - le um inteiro do stdin. Mantido para compatibilidade
    // com programas existentes. Para ler strings, use input_str(prompt).
    fprintf(out, "static LamoValue lamo_input_value(LamoValue prompt) {\n");
    fprintf(out, "    long long value;\n");
    fprintf(out, "    lamo_print_prompt(prompt);\n");
    fprintf(out, "    if (scanf(\"%%lld\", &value) != 1) {\n");
    fprintf(out, "        value = 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_int(value);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    // input_int(prompt) - alias explicito para input(), le long long.
    fprintf(out, "static LamoValue lamo_input_int_value(LamoValue prompt) {\n");
    fprintf(out, "    long long value;\n");
    fprintf(out, "    lamo_print_prompt(prompt);\n");
    fprintf(out, "    if (scanf(\"%%lld\", &value) != 1) {\n");
    fprintf(out, "        value = 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_make_int(value);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    // input_str(prompt) - le uma linha de texto do stdin, sem o newline final.
    // A string retornada e alocada na arena de strings (libertada no atexit).
    fprintf(out, "static LamoValue lamo_input_str_value(LamoValue prompt) {\n");
    fprintf(out, "    char buffer[1024];\n");
    fprintf(out, "    size_t length;\n");
    fprintf(out, "    char* owned;\n");
    fprintf(out, "    lamo_print_prompt(prompt);\n");
    fprintf(out, "    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {\n");
    fprintf(out, "        buffer[0] = '\\0';\n");
    fprintf(out, "    }\n");
    fprintf(out, "    length = strlen(buffer);\n");
    fprintf(out, "    while (length > 0 && (buffer[length-1] == '\\n' || buffer[length-1] == '\\r')) {\n");
    fprintf(out, "        buffer[--length] = '\\0';\n");
    fprintf(out, "    }\n");
    fprintf(out, "    owned = lamo_arena_strdup(buffer);\n");
    fprintf(out, "    return lamo_make_string(owned);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_isnumber_value(LamoValue value) {\n");
    fprintf(out, "    return lamo_make_bool(value.type == LAMO_VALUE_INT || value.type == LAMO_VALUE_FLOAT);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_isstring_value(LamoValue value) {\n");
    fprintf(out, "    return lamo_make_bool(value.type == LAMO_VALUE_STRING);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoValue lamo_abs_value(LamoValue value) {\n");
    fprintf(out, "    if (value.type == LAMO_VALUE_FLOAT) {\n");
    fprintf(out, "        double v = value.float_value;\n");
    fprintf(out, "        return lamo_make_float(v < 0 ? -v : v);\n");
    fprintf(out, "    }\n");
    fprintf(out, "    {\n");
    fprintf(out, "        long long raw = lamo_as_int(value);\n");
    fprintf(out, "        return lamo_make_int(raw < 0 ? -raw : raw);\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
}

static void emit_gui_runtime(FILE* out) {
    fprintf(out, "#ifdef _WIN32\n");
    fprintf(out, "#include <windows.h>\n");
    fprintf(out, "\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    HWND hwnd;\n");
    fprintf(out, "    HDC back_dc;\n");
    fprintf(out, "    HBITMAP back_bitmap;\n");
    fprintf(out, "    HFONT font;\n");
    fprintf(out, "    int width;\n");
    fprintf(out, "    int height;\n");
    fprintf(out, "    int is_open;\n");
    fprintf(out, "} LamoGuiState;\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoGuiState lamo_gui = {0};\n");
    fprintf(out, "static int lamo_gui_registered = 0;\n");
    fprintf(out, "\n");
    fprintf(out, "static LRESULT CALLBACK lamo_gui_window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {\n");
    fprintf(out, "    (void)l_param;\n");
    fprintf(out, "    switch (message) {\n");
    fprintf(out, "        case WM_CLOSE:\n");
    fprintf(out, "            lamo_gui.is_open = 0;\n");
    fprintf(out, "            DestroyWindow(hwnd);\n");
    fprintf(out, "            return 0;\n");
    fprintf(out, "        case WM_DESTROY:\n");
    fprintf(out, "            lamo_gui.is_open = 0;\n");
    fprintf(out, "            PostQuitMessage(0);\n");
    fprintf(out, "            return 0;\n");
    fprintf(out, "        default:\n");
    fprintf(out, "            return DefWindowProcA(hwnd, message, w_param, l_param);\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_process_messages(void) {\n");
    fprintf(out, "    MSG message;\n");
    fprintf(out, "    while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {\n");
    fprintf(out, "        if (message.message == WM_QUIT) {\n");
    fprintf(out, "            lamo_gui.is_open = 0;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        TranslateMessage(&message);\n");
    fprintf(out, "        DispatchMessageA(&message);\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static int lamo_gui_open(int width, int height, const char* title) {\n");
    fprintf(out, "    WNDCLASSA window_class;\n");
    fprintf(out, "    HDC window_dc;\n");
    fprintf(out, "    RECT rect;\n");
    fprintf(out, "    if (lamo_gui.is_open) { return 1; }\n");
    fprintf(out, "    if (!lamo_gui_registered) {\n");
    fprintf(out, "        ZeroMemory(&window_class, sizeof(window_class));\n");
    fprintf(out, "        window_class.lpfnWndProc = lamo_gui_window_proc;\n");
    fprintf(out, "        window_class.hInstance = GetModuleHandleA(NULL);\n");
    fprintf(out, "        window_class.lpszClassName = \"LamoGuiWindow\";\n");
    fprintf(out, "        window_class.hCursor = LoadCursor(NULL, IDC_ARROW);\n");
    fprintf(out, "        window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);\n");
    fprintf(out, "        if (!RegisterClassA(&window_class)) {\n");
    fprintf(out, "            DWORD error_code = GetLastError();\n");
    fprintf(out, "            if (error_code != ERROR_CLASS_ALREADY_EXISTS) {\n");
    fprintf(out, "                fprintf(stderr, \"failed to register GUI window class (error %%lu)\\n\", (unsigned long)error_code);\n");
    fprintf(out, "                return 0;\n");
    fprintf(out, "            }\n");
    fprintf(out, "        }\n");
    fprintf(out, "        lamo_gui_registered = 1;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    lamo_gui.width = width;\n");
    fprintf(out, "    lamo_gui.height = height;\n");
    fprintf(out, "    rect.left = 0; rect.top = 0; rect.right = width; rect.bottom = height;\n");
    fprintf(out, "    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, 0);\n");
    fprintf(out, "    lamo_gui.hwnd = CreateWindowExA(0, \"LamoGuiWindow\", title, WS_OVERLAPPEDWINDOW,\n");
    fprintf(out, "        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,\n");
    fprintf(out, "        NULL, NULL, GetModuleHandleA(NULL), NULL);\n");
    fprintf(out, "    if (!lamo_gui.hwnd) { fprintf(stderr, \"failed to create GUI window\\n\"); return 0; }\n");
    fprintf(out, "    ShowWindow(lamo_gui.hwnd, SW_SHOW);\n");
    fprintf(out, "    UpdateWindow(lamo_gui.hwnd);\n");
    fprintf(out, "    window_dc = GetDC(lamo_gui.hwnd);\n");
    fprintf(out, "    lamo_gui.back_dc = CreateCompatibleDC(window_dc);\n");
    fprintf(out, "    lamo_gui.back_bitmap = CreateCompatibleBitmap(window_dc, width, height);\n");
    fprintf(out, "    SelectObject(lamo_gui.back_dc, lamo_gui.back_bitmap);\n");
    fprintf(out, "    ReleaseDC(lamo_gui.hwnd, window_dc);\n");
    fprintf(out, "    SetBkMode(lamo_gui.back_dc, TRANSPARENT);\n");
    fprintf(out, "    lamo_gui.font = CreateFontA(24, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,\n");
    fprintf(out, "        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,\n");
    fprintf(out, "        DEFAULT_PITCH | FF_DONTCARE, \"Segoe UI\");\n");
    fprintf(out, "    if (lamo_gui.font) { SelectObject(lamo_gui.back_dc, lamo_gui.font); }\n");
    fprintf(out, "    lamo_gui.is_open = 1;\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static int lamo_gui_should_close(void) {\n");
    fprintf(out, "    lamo_gui_process_messages();\n");
    fprintf(out, "    return lamo_gui.is_open ? 0 : 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_begin_frame(int r, int g, int b) {\n");
    fprintf(out, "    RECT rect; HBRUSH brush;\n");
    fprintf(out, "    if (!lamo_gui.is_open) return;\n");
    fprintf(out, "    rect.left = 0; rect.top = 0; rect.right = lamo_gui.width; rect.bottom = lamo_gui.height;\n");
    fprintf(out, "    brush = CreateSolidBrush(RGB(r, g, b));\n");
    fprintf(out, "    FillRect(lamo_gui.back_dc, &rect, brush);\n");
    fprintf(out, "    DeleteObject(brush);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_draw_rect(int x, int y, int width, int height, int r, int g, int b) {\n");
    fprintf(out, "    RECT rect; HBRUSH brush;\n");
    fprintf(out, "    if (!lamo_gui.is_open) return;\n");
    fprintf(out, "    rect.left = x; rect.top = y; rect.right = x + width; rect.bottom = y + height;\n");
    fprintf(out, "    brush = CreateSolidBrush(RGB(r, g, b));\n");
    fprintf(out, "    FillRect(lamo_gui.back_dc, &rect, brush);\n");
    fprintf(out, "    DeleteObject(brush);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_draw_text(const char* text, int x, int y, int r, int g, int b) {\n");
    fprintf(out, "    if (!lamo_gui.is_open) return;\n");
    fprintf(out, "    SetTextColor(lamo_gui.back_dc, RGB(r, g, b));\n");
    fprintf(out, "    TextOutA(lamo_gui.back_dc, x, y, text, (int)strlen(text));\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_end_frame(void) {\n");
    fprintf(out, "    HDC window_dc;\n");
    fprintf(out, "    if (!lamo_gui.is_open) return;\n");
    fprintf(out, "    window_dc = GetDC(lamo_gui.hwnd);\n");
    fprintf(out, "    BitBlt(window_dc, 0, 0, lamo_gui.width, lamo_gui.height, lamo_gui.back_dc, 0, 0, SRCCOPY);\n");
    fprintf(out, "    ReleaseDC(lamo_gui.hwnd, window_dc);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_close(void) {\n");
    fprintf(out, "    if (lamo_gui.font) { DeleteObject(lamo_gui.font); lamo_gui.font = NULL; }\n");
    fprintf(out, "    if (lamo_gui.back_bitmap) { DeleteObject(lamo_gui.back_bitmap); lamo_gui.back_bitmap = NULL; }\n");
    fprintf(out, "    if (lamo_gui.back_dc) { DeleteDC(lamo_gui.back_dc); lamo_gui.back_dc = NULL; }\n");
    fprintf(out, "    if (lamo_gui.hwnd) { DestroyWindow(lamo_gui.hwnd); lamo_gui.hwnd = NULL; }\n");
    fprintf(out, "    lamo_gui.is_open = 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "#elif defined(__unix__) || defined(__unix) || defined(__linux__) || defined(__APPLE__)\n");
    fprintf(out, "// Backend X11 para Linux/Mac. C e Xlib são triviais de linkar (-lX11).\n");
    fprintf(out, "#include <X11/Xlib.h>\n");
    fprintf(out, "#include <X11/Xutil.h>\n");
    fprintf(out, "#include <X11/Xos.h>\n");
    fprintf(out, "\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    Display* display;\n");
    fprintf(out, "    Window window;\n");
    fprintf(out, "    GC gc;\n");
    fprintf(out, "    Pixmap back_pixmap;\n");
    fprintf(out, "    XFontStruct* font;\n");
    fprintf(out, "    int width;\n");
    fprintf(out, "    int height;\n");
    fprintf(out, "    int is_open;\n");
    fprintf(out, "    Atom wm_delete;\n");
    fprintf(out, "} LamoGuiState;\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoGuiState lamo_gui = {0};\n");
    fprintf(out, "\n");
    fprintf(out, "static int lamo_gui_open(int width, int height, const char* title) {\n");
    fprintf(out, "    int screen;\n");
    fprintf(out, "    if (lamo_gui.is_open) { return 1; }\n");
    fprintf(out, "    lamo_gui.display = XOpenDisplay(NULL);\n");
    fprintf(out, "    if (!lamo_gui.display) {\n");
    fprintf(out, "        fprintf(stderr, \"failed to open X11 display\\n\");\n");
    fprintf(out, "        return 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    screen = DefaultScreen(lamo_gui.display);\n");
    fprintf(out, "    lamo_gui.width = width;\n");
    fprintf(out, "    lamo_gui.height = height;\n");
    fprintf(out, "    lamo_gui.window = XCreateSimpleWindow(lamo_gui.display,\n");
    fprintf(out, "        RootWindow(lamo_gui.display, screen),\n");
    fprintf(out, "        0, 0, width, height, 1,\n");
    fprintf(out, "        BlackPixel(lamo_gui.display, screen),\n");
    fprintf(out, "        WhitePixel(lamo_gui.display, screen));\n");
    fprintf(out, "    XSelectInput(lamo_gui.display, lamo_gui.window,\n");
    fprintf(out, "        ExposureMask | StructureNotifyMask);\n");
    fprintf(out, "    XStoreName(lamo_gui.display, lamo_gui.window, title ? title : \"Lamo\");\n");
    fprintf(out, "    lamo_gui.wm_delete = XInternAtom(lamo_gui.display, \"WM_DELETE_WINDOW\", False);\n");
    fprintf(out, "    XSetWMProtocols(lamo_gui.display, lamo_gui.window, &lamo_gui.wm_delete, 1);\n");
    fprintf(out, "    lamo_gui.gc = XCreateGC(lamo_gui.display, lamo_gui.window, 0, NULL);\n");
    fprintf(out, "    lamo_gui.back_pixmap = XCreatePixmap(lamo_gui.display, lamo_gui.window,\n");
    fprintf(out, "        width, height, DefaultDepth(lamo_gui.display, screen));\n");
    fprintf(out, "    lamo_gui.font = XLoadQueryFont(lamo_gui.display, \"fixed\");\n");
    fprintf(out, "    if (lamo_gui.font) { XSetFont(lamo_gui.display, lamo_gui.gc, lamo_gui.font->fid); }\n");
    fprintf(out, "    XMapWindow(lamo_gui.display, lamo_gui.window);\n");
    fprintf(out, "    XFlush(lamo_gui.display);\n");
    fprintf(out, "    lamo_gui.is_open = 1;\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_process_messages(void) {\n");
    fprintf(out, "    XEvent event;\n");
    fprintf(out, "    while (XPending(lamo_gui.display) > 0) {\n");
    fprintf(out, "        XNextEvent(lamo_gui.display, &event);\n");
    fprintf(out, "        if (event.type == ClientMessage) {\n");
    fprintf(out, "            if ((Atom)event.xclient.data.l[0] == lamo_gui.wm_delete) {\n");
    fprintf(out, "                lamo_gui.is_open = 0;\n");
    fprintf(out, "            }\n");
    fprintf(out, "        }\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static int lamo_gui_should_close(void) {\n");
    fprintf(out, "    if (!lamo_gui.display) return 1;\n");
    fprintf(out, "    lamo_gui_process_messages();\n");
    fprintf(out, "    return lamo_gui.is_open ? 0 : 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_begin_frame(int r, int g, int b) {\n");
    fprintf(out, "    XColor color;\n");
    fprintf(out, "    if (!lamo_gui.is_open) return;\n");
    fprintf(out, "    color.red = (unsigned short)(r * 257);\n");
    fprintf(out, "    color.green = (unsigned short)(g * 257);\n");
    fprintf(out, "    color.blue = (unsigned short)(b * 257);\n");
    fprintf(out, "    color.flags = DoRed | DoGreen | DoBlue;\n");
    fprintf(out, "    XAllocColor(lamo_gui.display, DefaultColormap(lamo_gui.display, 0), &color);\n");
    fprintf(out, "    XSetForeground(lamo_gui.display, lamo_gui.gc, color.pixel);\n");
    fprintf(out, "    XFillRectangle(lamo_gui.display, lamo_gui.back_pixmap, lamo_gui.gc,\n");
    fprintf(out, "        0, 0, lamo_gui.width, lamo_gui.height);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_draw_rect(int x, int y, int width, int height, int r, int g, int b) {\n");
    fprintf(out, "    XColor color;\n");
    fprintf(out, "    if (!lamo_gui.is_open) return;\n");
    fprintf(out, "    color.red = (unsigned short)(r * 257);\n");
    fprintf(out, "    color.green = (unsigned short)(g * 257);\n");
    fprintf(out, "    color.blue = (unsigned short)(b * 257);\n");
    fprintf(out, "    color.flags = DoRed | DoGreen | DoBlue;\n");
    fprintf(out, "    XAllocColor(lamo_gui.display, DefaultColormap(lamo_gui.display, 0), &color);\n");
    fprintf(out, "    XSetForeground(lamo_gui.display, lamo_gui.gc, color.pixel);\n");
    fprintf(out, "    XFillRectangle(lamo_gui.display, lamo_gui.back_pixmap, lamo_gui.gc,\n");
    fprintf(out, "        x, y, width, height);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_draw_text(const char* text, int x, int y, int r, int g, int b) {\n");
    fprintf(out, "    XColor color;\n");
    fprintf(out, "    if (!lamo_gui.is_open) return;\n");
    fprintf(out, "    color.red = (unsigned short)(r * 257);\n");
    fprintf(out, "    color.green = (unsigned short)(g * 257);\n");
    fprintf(out, "    color.blue = (unsigned short)(b * 257);\n");
    fprintf(out, "    color.flags = DoRed | DoGreen | DoBlue;\n");
    fprintf(out, "    XAllocColor(lamo_gui.display, DefaultColormap(lamo_gui.display, 0), &color);\n");
    fprintf(out, "    XSetForeground(lamo_gui.display, lamo_gui.gc, color.pixel);\n");
    fprintf(out, "    XDrawString(lamo_gui.display, lamo_gui.back_pixmap, lamo_gui.gc,\n");
    fprintf(out, "        x, y + (lamo_gui.font ? lamo_gui.font->ascent : 12),\n");
    fprintf(out, "        text ? text : \"\", text ? (int)strlen(text) : 0);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_end_frame(void) {\n");
    fprintf(out, "    if (!lamo_gui.is_open) return;\n");
    fprintf(out, "    XCopyArea(lamo_gui.display, lamo_gui.back_pixmap, lamo_gui.window, lamo_gui.gc,\n");
    fprintf(out, "        0, 0, lamo_gui.width, lamo_gui.height, 0, 0);\n");
    fprintf(out, "    XFlush(lamo_gui.display);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_gui_close(void) {\n");
    fprintf(out, "    if (lamo_gui.font) { XFreeFont(lamo_gui.display, lamo_gui.font); lamo_gui.font = NULL; }\n");
    fprintf(out, "    if (lamo_gui.back_pixmap) { XFreePixmap(lamo_gui.display, lamo_gui.back_pixmap); lamo_gui.back_pixmap = 0; }\n");
    fprintf(out, "    if (lamo_gui.gc) { XFreeGC(lamo_gui.display, lamo_gui.gc); lamo_gui.gc = NULL; }\n");
    fprintf(out, "    if (lamo_gui.window) { XDestroyWindow(lamo_gui.display, lamo_gui.window); lamo_gui.window = 0; }\n");
    fprintf(out, "    if (lamo_gui.display) { XCloseDisplay(lamo_gui.display); lamo_gui.display = NULL; }\n");
    fprintf(out, "    lamo_gui.is_open = 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "#else\n");
    fprintf(out, "static int lamo_gui_open(int width, int height, const char* title) {\n");
    fprintf(out, "    (void)width; (void)height; (void)title;\n");
    fprintf(out, "    fprintf(stderr, \"GUI builtins are not supported on this platform.\\n\");\n");
    fprintf(out, "    return 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "static int lamo_gui_should_close(void) { return 1; }\n");
    fprintf(out, "static void lamo_gui_begin_frame(int r, int g, int b) { (void)r; (void)g; (void)b; }\n");
    fprintf(out, "static void lamo_gui_draw_rect(int x, int y, int width, int height, int r, int g, int b) {\n");
    fprintf(out, "    (void)x; (void)y; (void)width; (void)height; (void)r; (void)g; (void)b;\n");
    fprintf(out, "}\n");
    fprintf(out, "static void lamo_gui_draw_text(const char* text, int x, int y, int r, int g, int b) {\n");
    fprintf(out, "    (void)text; (void)x; (void)y; (void)r; (void)g; (void)b;\n");
    fprintf(out, "}\n");
    fprintf(out, "static void lamo_gui_end_frame(void) {}\n");
    fprintf(out, "static void lamo_gui_close(void) {}\n");
    fprintf(out, "#endif\n");
    fprintf(out, "\n");
}

static void emit_http_runtime(FILE* out) {
    fprintf(out, "#ifdef _WIN32\n");
    fprintf(out, "#include <winsock2.h>\n");
    fprintf(out, "#include <ws2tcpip.h>\n");
    fprintf(out, "typedef SOCKET lamo_socket_t;\n");
    fprintf(out, "#define LAMO_INVALID_SOCKET INVALID_SOCKET\n");
    fprintf(out, "#define lamo_close_socket closesocket\n");
    fprintf(out, "#else\n");
    fprintf(out, "#include <sys/types.h>\n");
    fprintf(out, "#include <sys/socket.h>\n");
    fprintf(out, "#include <netinet/in.h>\n");
    fprintf(out, "#include <arpa/inet.h>\n");
    fprintf(out, "#include <unistd.h>\n");
    fprintf(out, "typedef int lamo_socket_t;\n");
    fprintf(out, "#define LAMO_INVALID_SOCKET (-1)\n");
    fprintf(out, "#define lamo_close_socket close\n");
    fprintf(out, "#endif\n");
    fprintf(out, "\n");
    fprintf(out, "typedef struct LamoHttpRoute {\n");
    fprintf(out, "    char* path;\n");
    fprintf(out, "    char* body;\n");
    fprintf(out, "    struct LamoHttpRoute* next;\n");
    fprintf(out, "} LamoHttpRoute;\n");
    fprintf(out, "\n");
    fprintf(out, "static LamoHttpRoute* lamo_http_routes = NULL;\n");
    fprintf(out, "static int lamo_http_runtime_initialized = 0;\n");
    fprintf(out, "\n");
    fprintf(out, "static int lamo_http_init_runtime(void) {\n");
    fprintf(out, "#ifdef _WIN32\n");
    fprintf(out, "    WSADATA wsa_data;\n");
    fprintf(out, "    if (!lamo_http_runtime_initialized) {\n");
    fprintf(out, "        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {\n");
    fprintf(out, "            fprintf(stderr, \"failed to initialize Winsock\\n\");\n");
    fprintf(out, "            return 0;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        lamo_http_runtime_initialized = 1;\n");
    fprintf(out, "    }\n");
    fprintf(out, "#else\n");
    fprintf(out, "    lamo_http_runtime_initialized = 1;\n");
    fprintf(out, "#endif\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_http_add_route(const char* path, const char* body) {\n");
    fprintf(out, "    LamoHttpRoute* route;\n");
    fprintf(out, "    if (!lamo_http_init_runtime()) { return; }\n");
    fprintf(out, "    route = malloc(sizeof(LamoHttpRoute));\n");
    fprintf(out, "    if (!route) { fprintf(stderr, \"failed to allocate HTTP route\\n\"); exit(1); }\n");
    fprintf(out, "    route->path = lamo_heap_strdup(path);\n");
    fprintf(out, "    route->body = lamo_heap_strdup(body);\n");
    fprintf(out, "    route->next = lamo_http_routes;\n");
    fprintf(out, "    lamo_http_routes = route;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static const LamoHttpRoute* lamo_http_find_route(const char* path) {\n");
    fprintf(out, "    LamoHttpRoute* current = lamo_http_routes;\n");
    fprintf(out, "    while (current) {\n");
    fprintf(out, "        if (strcmp(current->path, path) == 0) { return current; }\n");
    fprintf(out, "        current = current->next;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return NULL;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static int lamo_http_send_all(lamo_socket_t socket_fd, const char* data, int total_length) {\n");
    fprintf(out, "    int sent = 0;\n");
    fprintf(out, "    while (sent < total_length) {\n");
    fprintf(out, "        int chunk = send(socket_fd, data + sent, total_length - sent, 0);\n");
    fprintf(out, "        if (chunk <= 0) { return 0; }\n");
    fprintf(out, "        sent += chunk;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static int lamo_http_send_response(lamo_socket_t client_socket, int status_code, const char* body) {\n");
    fprintf(out, "    char header[512];\n");
    fprintf(out, "    const char* reason = status_code == 200 ? \"OK\" : \"Not Found\";\n");
    fprintf(out, "    int body_length = (int)strlen(body);\n");
    fprintf(out, "    int header_length = snprintf(header, sizeof(header),\n");
    fprintf(out, "        \"HTTP/1.1 %%d %%s\\r\\n\"\n");
    fprintf(out, "        \"Content-Type: text/plain; charset=utf-8\\r\\n\"\n");
    fprintf(out, "        \"Content-Length: %%d\\r\\n\"\n");
    fprintf(out, "        \"Connection: close\\r\\n\\r\\n\",\n");
    fprintf(out, "        status_code, reason, body_length);\n");
    fprintf(out, "    if (header_length < 0 || !lamo_http_send_all(client_socket, header, header_length)) {\n");
    fprintf(out, "        return 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return lamo_http_send_all(client_socket, body, body_length);\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static void lamo_http_handle_client(lamo_socket_t client_socket) {\n");
    fprintf(out, "    char request[2048];\n");
    fprintf(out, "    char method[16];\n");
    fprintf(out, "    char path[1024];\n");
    fprintf(out, "    int received = recv(client_socket, request, (int)sizeof(request) - 1, 0);\n");
    fprintf(out, "    if (received <= 0) { return; }\n");
    fprintf(out, "    request[received] = '\\0';\n");
    fprintf(out, "    method[0] = '\\0';\n");
    fprintf(out, "    path[0] = '\\0';\n");
    fprintf(out, "    if (sscanf(request, \"%%15s %%1023s\", method, path) == 2) {\n");
    fprintf(out, "        const LamoHttpRoute* route = lamo_http_find_route(path);\n");
    fprintf(out, "        if (route) {\n");
    fprintf(out, "            lamo_http_send_response(client_socket, 200, route->body);\n");
    fprintf(out, "            return;\n");
    fprintf(out, "        }\n");
    fprintf(out, "    }\n");
    fprintf(out, "    lamo_http_send_response(client_socket, 404, \"not found\");\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
    fprintf(out, "static int lamo_http_run_server(int port, int serve_once) {\n");
    fprintf(out, "    lamo_socket_t server_socket;\n");
    fprintf(out, "    struct sockaddr_in address;\n");
    fprintf(out, "    int opt_value = 1;\n");
    fprintf(out, "    if (!lamo_http_init_runtime()) { return 0; }\n");
    fprintf(out, "    server_socket = socket(AF_INET, SOCK_STREAM, 0);\n");
    fprintf(out, "    if (server_socket == LAMO_INVALID_SOCKET) {\n");
    fprintf(out, "        fprintf(stderr, \"failed to create HTTP socket\\n\");\n");
    fprintf(out, "        return 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt_value, sizeof(opt_value));\n");
    fprintf(out, "    memset(&address, 0, sizeof(address));\n");
    fprintf(out, "    address.sin_family = AF_INET;\n");
    fprintf(out, "    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);\n");
    fprintf(out, "    address.sin_port = htons((unsigned short)port);\n");
    fprintf(out, "    if (bind(server_socket, (struct sockaddr*)&address, sizeof(address)) != 0) {\n");
    fprintf(out, "        fprintf(stderr, \"failed to bind HTTP server on port %%d\\n\", port);\n");
    fprintf(out, "        lamo_close_socket(server_socket);\n");
    fprintf(out, "        return 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (listen(server_socket, 8) != 0) {\n");
    fprintf(out, "        fprintf(stderr, \"failed to listen on HTTP server port %%d\\n\", port);\n");
    fprintf(out, "        lamo_close_socket(server_socket);\n");
    fprintf(out, "        return 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    while (1) {\n");
    fprintf(out, "        lamo_socket_t client_socket = accept(server_socket, NULL, NULL);\n");
    fprintf(out, "        if (client_socket == LAMO_INVALID_SOCKET) { break; }\n");
    fprintf(out, "        lamo_http_handle_client(client_socket);\n");
    fprintf(out, "        lamo_close_socket(client_socket);\n");
    fprintf(out, "        if (serve_once) { break; }\n");
    fprintf(out, "    }\n");
    fprintf(out, "    lamo_close_socket(server_socket);\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n");
    fprintf(out, "\n");
}


static int ast_uses_gui(ASTNode* node) {
    int i;

    if (!node) {
        return 0;
    }

    switch (node->type) {
        case AST_PROGRAM: {
            ASTNode* current = ((ASTProgram*)node)->declarations;
            while (current) {
                if (ast_uses_gui(current)) {
                    return 1;
                }
                current = current->next;
            }
            return 0;
        }
        case AST_VAR_DECL:
            return ast_uses_gui(((ASTVarDecl*)node)->initializer);
        case AST_FN_DECL:
            return ast_uses_gui(((ASTFnDecl*)node)->body);
        case AST_BLOCK: {
            ASTNode* current = ((ASTBlock*)node)->statements;
            while (current) {
                if (ast_uses_gui(current)) {
                    return 1;
                }
                current = current->next;
            }
            return 0;
        }
        case AST_IF_STMT: {
            ASTIfStmt* if_stmt = (ASTIfStmt*)node;
            return ast_uses_gui(if_stmt->condition) ||
                   ast_uses_gui(if_stmt->then_branch) ||
                   ast_uses_gui(if_stmt->else_branch);
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* while_stmt = (ASTWhileStmt*)node;
            return ast_uses_gui(while_stmt->condition) ||
                   ast_uses_gui(while_stmt->body);
        }
        case AST_FOR_STMT: {
            ASTForStmt* for_stmt = (ASTForStmt*)node;
            return ast_uses_gui(for_stmt->initializer) ||
                   ast_uses_gui(for_stmt->condition) ||
                   ast_uses_gui(for_stmt->increment) ||
                   ast_uses_gui(for_stmt->body);
        }
        case AST_RETURN_STMT:
            return ast_uses_gui(((ASTReturnStmt*)node)->expression);
        case AST_ASSIGN_STMT:
            return ast_uses_gui(((ASTAssignStmt*)node)->value);
        case AST_CALL_STMT: {
            ASTCallStmt* call_stmt = (ASTCallStmt*)node;
            if (is_gui_builtin(call_stmt->name)) {
                return 1;
            }
            for (i = 0; i < call_stmt->arg_count; i++) {
                if (ast_uses_gui(call_stmt->args[i])) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* call_expr = (ASTCallExpr*)node;
            if (is_gui_builtin(call_expr->name)) {
                return 1;
            }
            for (i = 0; i < call_expr->arg_count; i++) {
                if (ast_uses_gui(call_expr->args[i])) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_BINARY_EXPR: {
            ASTBinaryExpr* expr = (ASTBinaryExpr*)node;
            return ast_uses_gui(expr->left) || ast_uses_gui(expr->right);
        }
        case AST_UNARY_EXPR:
            return ast_uses_gui(((ASTUnaryExpr*)node)->right);
        case AST_GROUPING_EXPR:
            return ast_uses_gui(((ASTGroupingExpr*)node)->expression);
        case AST_INT_LITERAL:
        case AST_FLOAT_LITERAL:
        case AST_STRING_LITERAL:
        case AST_BOOL_LITERAL:
        case AST_IDENTIFIER:
        case AST_IMPORT:
            return 0;
        default:
            return 0;
    }
}

static int ast_uses_http(ASTNode* node) {
    int i;

    if (!node) {
        return 0;
    }

    switch (node->type) {
        case AST_PROGRAM: {
            ASTNode* current = ((ASTProgram*)node)->declarations;
            while (current) {
                if (ast_uses_http(current)) {
                    return 1;
                }
                current = current->next;
            }
            return 0;
        }
        case AST_VAR_DECL:
            return ast_uses_http(((ASTVarDecl*)node)->initializer);
        case AST_FN_DECL:
            return ast_uses_http(((ASTFnDecl*)node)->body);
        case AST_BLOCK: {
            ASTNode* current = ((ASTBlock*)node)->statements;
            while (current) {
                if (ast_uses_http(current)) {
                    return 1;
                }
                current = current->next;
            }
            return 0;
        }
        case AST_IF_STMT: {
            ASTIfStmt* if_stmt = (ASTIfStmt*)node;
            return ast_uses_http(if_stmt->condition) ||
                   ast_uses_http(if_stmt->then_branch) ||
                   ast_uses_http(if_stmt->else_branch);
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* while_stmt = (ASTWhileStmt*)node;
            return ast_uses_http(while_stmt->condition) ||
                   ast_uses_http(while_stmt->body);
        }
        case AST_FOR_STMT: {
            ASTForStmt* for_stmt = (ASTForStmt*)node;
            return ast_uses_http(for_stmt->initializer) ||
                   ast_uses_http(for_stmt->condition) ||
                   ast_uses_http(for_stmt->increment) ||
                   ast_uses_http(for_stmt->body);
        }
        case AST_RETURN_STMT:
            return ast_uses_http(((ASTReturnStmt*)node)->expression);
        case AST_ASSIGN_STMT:
            return ast_uses_http(((ASTAssignStmt*)node)->value);
        case AST_CALL_STMT: {
            ASTCallStmt* call_stmt = (ASTCallStmt*)node;
            if (is_http_builtin(call_stmt->name)) {
                return 1;
            }
            for (i = 0; i < call_stmt->arg_count; i++) {
                if (ast_uses_http(call_stmt->args[i])) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* call_expr = (ASTCallExpr*)node;
            if (is_http_builtin(call_expr->name)) {
                return 1;
            }
            for (i = 0; i < call_expr->arg_count; i++) {
                if (ast_uses_http(call_expr->args[i])) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_BINARY_EXPR: {
            ASTBinaryExpr* expr = (ASTBinaryExpr*)node;
            return ast_uses_http(expr->left) || ast_uses_http(expr->right);
        }
        case AST_UNARY_EXPR:
            return ast_uses_http(((ASTUnaryExpr*)node)->right);
        case AST_GROUPING_EXPR:
            return ast_uses_http(((ASTGroupingExpr*)node)->expression);
        case AST_INT_LITERAL:
        case AST_FLOAT_LITERAL:
        case AST_STRING_LITERAL:
        case AST_BOOL_LITERAL:
        case AST_IDENTIFIER:
        case AST_IMPORT:
            return 0;
        default:
            return 0;
    }
}

void generate_c_code(ASTNode* node, FILE* out) {
    ASTNode* current;
    int needs_gui_runtime;
    int needs_http_runtime;

    if (!node) {
        return;
    }

    fprintf(out, "// Generated by Lamo v2 (via AST)\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include <string.h>\n\n");
    emit_value_runtime(out);
    needs_gui_runtime = ast_uses_gui(node);
    needs_http_runtime = ast_uses_http(node);
    if (needs_gui_runtime) {
        emit_gui_runtime(out);
    }
    if (needs_http_runtime) {
        emit_http_runtime(out);
    }

    // 1. Forward declarations de funções definidas pelo usuário.
    current = ((ASTProgram*)node)->declarations;
    while (current) {
        if (current->type == AST_FN_DECL) {
            ASTFnDecl* fn_decl = (ASTFnDecl*)current;
            int i;
            fprintf(out, "LamoValue %s(", user_name(fn_decl->name));
            for (i = 0; i < fn_decl->param_count; i++) {
                if (i > 0) {
                    fprintf(out, ", ");
                }
                fprintf(out, "LamoValue %s", user_name(fn_decl->params[i]));
            }
            if (fn_decl->param_count == 0) {
                fprintf(out, "void");
            }
            fprintf(out, ");\n");
        }
        current = current->next;
    }
    fprintf(out, "\n");

    // 2. Declarações de variáveis globais no escopo de arquivo.
    //    Inicializadores não-constantes são emitidos dentro de main().
    current = ((ASTProgram*)node)->declarations;
    while (current) {
        if (current->type == AST_VAR_DECL) {
            ASTVarDecl* var_decl = (ASTVarDecl*)current;
            fprintf(out, "static LamoValue %s;\n", user_name(var_decl->name));
        }
        current = current->next;
    }
    fprintf(out, "\n");

    // 3. Corpos das funções definidas pelo usuário.
    current = ((ASTProgram*)node)->declarations;
    while (current) {
        if (current->type == AST_FN_DECL) {
            generate_statement_code(current, out);
            fprintf(out, "\n");
        }
        current = current->next;
    }

    // 4. main(): executa os statements top-level. Variáveis globais (let x = ...)
    //    viram assignments para as globais declaradas acima, e outras funções
    //    chamadas pelo nome podem referenciar essas globais porque estão em
    //    escopo de arquivo.
    fprintf(out, "int main(void) {\n");
    indent_level++;

    // Registra limpeza da arena de strings no encerramento do programa.
    print_indent(out);
    fprintf(out, "atexit(lamo_arena_free_all);\n");

    current = ((ASTProgram*)node)->declarations;
    while (current) {
        if (current->type == AST_VAR_DECL) {
            // Inicializa a global correspondente no início do main.
            ASTVarDecl* var_decl = (ASTVarDecl*)current;
            print_indent(out);
            fprintf(out, "%s = ", user_name(var_decl->name));
            generate_expression_code(var_decl->initializer, out);
            fprintf(out, ";\n");
        } else if (current->type != AST_FN_DECL && current->type != AST_IMPORT) {
            generate_statement_code(current, out);
        }
        current = current->next;
    }

    indent_level--;
    fprintf(out, "    return 0;\n}\n");
}

static void generate_assignment_code(const char* name, ASTNode* value, TokenType op_type, FILE* out) {
    fprintf(out, "%s = ", user_name(name));
    if (op_type == TOKEN_PLUS_EQ) {
        fprintf(out, "lamo_add(%s, ", user_name(name));
        generate_expression_code(value, out);
        fprintf(out, ")");
    } else if (op_type == TOKEN_MINUS_EQ) {
        fprintf(out, "lamo_sub(%s, ", user_name(name));
        generate_expression_code(value, out);
        fprintf(out, ")");
    } else {
        generate_expression_code(value, out);
    }
}

static void generate_statement_code(ASTNode* node, FILE* out) {
    if (!node) {
        return;
    }

    if (node->type != AST_BLOCK) {
        print_indent(out);
    }

    switch (node->type) {
        case AST_VAR_DECL: {
            // Variáveis locais dentro de blocos (não top-level). As top-level
            // são tratadas diretamente em generate_c_code como globais.
            ASTVarDecl* var_decl = (ASTVarDecl*)node;
            fprintf(out, "LamoValue %s = ", user_name(var_decl->name));
            generate_expression_code(var_decl->initializer, out);
            fprintf(out, ";\n");
            break;
        }
        case AST_FN_DECL: {
            ASTFnDecl* fn_decl = (ASTFnDecl*)node;
            int i;
            fprintf(out, "LamoValue %s(", user_name(fn_decl->name));
            for (i = 0; i < fn_decl->param_count; i++) {
                if (i > 0) {
                    fprintf(out, ", ");
                }
                fprintf(out, "LamoValue %s", user_name(fn_decl->params[i]));
            }
            if (fn_decl->param_count == 0) {
                fprintf(out, "void");
            }
            fprintf(out, ") ");
            generate_statement_code(fn_decl->body, out);
            break;
        }
        case AST_BLOCK: {
            ASTBlock* block = (ASTBlock*)node;
            ASTNode* current = block->statements;
            fprintf(out, "{\n");
            indent_level++;
            while (current) {
                generate_statement_code(current, out);
                current = current->next;
            }
            indent_level--;
            print_indent(out);
            fprintf(out, "}\n");
            break;
        }
        case AST_IF_STMT: {
            ASTIfStmt* if_stmt = (ASTIfStmt*)node;
            fprintf(out, "if (lamo_is_truthy(");
            generate_expression_code(if_stmt->condition, out);
            fprintf(out, ")) ");
            generate_statement_code(if_stmt->then_branch, out);
            if (if_stmt->else_branch) {
                print_indent(out);
                fprintf(out, "else ");
                generate_statement_code(if_stmt->else_branch, out);
            }
            break;
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* while_stmt = (ASTWhileStmt*)node;
            fprintf(out, "while (lamo_is_truthy(");
            generate_expression_code(while_stmt->condition, out);
            fprintf(out, ")) ");
            generate_statement_code(while_stmt->body, out);
            break;
        }
        case AST_FOR_STMT: {
            ASTForStmt* for_stmt = (ASTForStmt*)node;
            fprintf(out, "for (");
            if (for_stmt->initializer) {
                if (for_stmt->initializer->type == AST_VAR_DECL) {
                    ASTVarDecl* var_decl = (ASTVarDecl*)for_stmt->initializer;
                    fprintf(out, "LamoValue %s = ", user_name(var_decl->name));
                    generate_expression_code(var_decl->initializer, out);
                } else if (for_stmt->initializer->type == AST_ASSIGN_STMT) {
                    ASTAssignStmt* assign_stmt = (ASTAssignStmt*)for_stmt->initializer;
                    generate_assignment_code(assign_stmt->name, assign_stmt->value, assign_stmt->op_type, out);
                }
            }
            fprintf(out, "; ");
            if (for_stmt->condition) {
                fprintf(out, "lamo_is_truthy(");
                generate_expression_code(for_stmt->condition, out);
                fprintf(out, ")");
            }
            fprintf(out, "; ");
            if (for_stmt->increment) {
                ASTAssignStmt* assign_stmt = (ASTAssignStmt*)for_stmt->increment;
                generate_assignment_code(assign_stmt->name, assign_stmt->value, assign_stmt->op_type, out);
            }
            fprintf(out, ") ");
            generate_statement_code(for_stmt->body, out);
            break;
        }
        case AST_RETURN_STMT: {
            ASTReturnStmt* return_stmt = (ASTReturnStmt*)node;
            if (return_stmt->expression) {
                fprintf(out, "return ");
                generate_expression_code(return_stmt->expression, out);
                fprintf(out, ";\n");
            } else {
                fprintf(out, "return lamo_make_int(0);\n");
            }
            break;
        }
        case AST_ASSIGN_STMT: {
            ASTAssignStmt* assign_stmt = (ASTAssignStmt*)node;
            generate_assignment_code(assign_stmt->name, assign_stmt->value, assign_stmt->op_type, out);
            fprintf(out, ";\n");
            break;
        }
        case AST_CALL_STMT: {
            ASTCallStmt* call_stmt = (ASTCallStmt*)node;
            if (is_lang_builtin(call_stmt->name)) {
                generate_lang_builtin_call_expr(call_stmt->name, call_stmt->args, call_stmt->arg_count, out);
            } else if (is_gui_builtin(call_stmt->name)) {
                generate_gui_call_expr(call_stmt->name, call_stmt->args, call_stmt->arg_count, out);
            } else if (is_http_builtin(call_stmt->name)) {
                generate_http_call_expr(call_stmt->name, call_stmt->args, call_stmt->arg_count, out);
            } else {
                fprintf(out, "%s(", user_name(call_stmt->name));
                generate_call_arguments(call_stmt->args, call_stmt->arg_count, out);
                fprintf(out, ")");
            }
            fprintf(out, ";\n");
            break;
        }
        case AST_IMPORT:
            // import é resolvido antes do codegen; não emite nada aqui.
            break;
        default:
            break;
    }
}

static void generate_binary_expr(ASTBinaryExpr* expr, FILE* out) {
    switch (expr->operator) {
        case TOKEN_PLUS:      fprintf(out, "lamo_add("); break;
        case TOKEN_MINUS:     fprintf(out, "lamo_sub("); break;
        case TOKEN_STAR:      fprintf(out, "lamo_mul("); break;
        case TOKEN_SLASH:     fprintf(out, "lamo_div("); break;
        case TOKEN_PERCENT:   fprintf(out, "lamo_mod("); break;
        case TOKEN_LT:        fprintf(out, "lamo_less("); break;
        case TOKEN_GT:        fprintf(out, "lamo_greater("); break;
        case TOKEN_LT_EQ:     fprintf(out, "lamo_less_equal("); break;
        case TOKEN_GT_EQ:     fprintf(out, "lamo_greater_equal("); break;
        case TOKEN_EQ_EQ:     fprintf(out, "lamo_equal("); break;
        case TOKEN_BANG_EQ:   fprintf(out, "lamo_not_equal("); break;
        case TOKEN_AND_AND:   fprintf(out, "lamo_and("); break;
        case TOKEN_OR_OR:     fprintf(out, "lamo_or("); break;
        default:
            fprintf(out, "lamo_make_int(0)");
            return;
    }

    generate_expression_code(expr->left, out);
    fprintf(out, ", ");
    generate_expression_code(expr->right, out);
    fprintf(out, ")");
}

// Emite um literal de string como constante C, escapando corretamente.
static void emit_c_string_literal(const char* s, FILE* out) {
    fputc('"', out);
    while (*s) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (c >= 0x20 && c < 0x7f) {
                    fputc((char)c, out);
                } else {
                    fprintf(out, "\\x%02x", c);
                }
                break;
        }
        s++;
    }
    fputc('"', out);
}

static void generate_expression_code(ASTNode* node, FILE* out) {
    if (!node) {
        fprintf(out, "lamo_make_int(0)");
        return;
    }

    switch (node->type) {
        case AST_INT_LITERAL:
            fprintf(out, "lamo_make_int(%lldLL)", ((ASTIntLiteral*)node)->value);
            break;
        case AST_FLOAT_LITERAL:
            fprintf(out, "lamo_make_float(%#.17g)", ((ASTFloatLiteral*)node)->value);
            break;
        case AST_STRING_LITERAL:
            fputs("lamo_make_string(", out);
            emit_c_string_literal(((ASTStringLiteral*)node)->value, out);
            fputc(')', out);
            break;
        case AST_BOOL_LITERAL:
            fprintf(out, "lamo_make_bool(%d)", ((ASTBoolLiteral*)node)->value);
            break;
        case AST_IDENTIFIER:
            fprintf(out, "%s", user_name(((ASTIdentifier*)node)->name));
            break;
        case AST_BINARY_EXPR:
            generate_binary_expr((ASTBinaryExpr*)node, out);
            break;
        case AST_UNARY_EXPR: {
            ASTUnaryExpr* expr = (ASTUnaryExpr*)node;
            if (expr->operator == TOKEN_MINUS) {
                fprintf(out, "lamo_negate(");
                generate_expression_code(expr->right, out);
                fprintf(out, ")");
            } else if (expr->operator == TOKEN_BANG) {
                fprintf(out, "lamo_not(");
                generate_expression_code(expr->right, out);
                fprintf(out, ")");
            } else {
                fprintf(out, "lamo_make_int(0)");
            }
            break;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* call_expr = (ASTCallExpr*)node;
            if (is_lang_builtin(call_expr->name)) {
                generate_lang_builtin_call_expr(call_expr->name, call_expr->args, call_expr->arg_count, out);
            } else if (is_gui_builtin(call_expr->name)) {
                generate_gui_call_expr(call_expr->name, call_expr->args, call_expr->arg_count, out);
            } else if (is_http_builtin(call_expr->name)) {
                generate_http_call_expr(call_expr->name, call_expr->args, call_expr->arg_count, out);
            } else {
                fprintf(out, "%s(", user_name(call_expr->name));
                generate_call_arguments(call_expr->args, call_expr->arg_count, out);
                fprintf(out, ")");
            }
            break;
        }
        case AST_GROUPING_EXPR:
            fprintf(out, "(");
            generate_expression_code(((ASTGroupingExpr*)node)->expression, out);
            fprintf(out, ")");
            break;
        default:
            fprintf(out, "lamo_make_int(0)");
            break;
    }
}

