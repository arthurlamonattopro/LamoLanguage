#include "codegen.h"
#include "lamo_runtime_data.h"
#include "../builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>   /* fmod() — used by constant folding for float % */

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

// Bug #3 fix: user_name() no longer returns a pointer into a static buffer.
// Instead, callers pass their own buffer (256 bytes is enough for any
// identifier; the language has no arbitrary-length identifier limits today).
// This makes it safe to use multiple user_name() calls in the same fprintf,
// e.g. fprintf(out, "%s, %s", user_name(a, bufa), user_name(b, bufb)).
#define LAMO_USER_NAME_MAX 256

#if defined(__GNUC__) || defined(__clang__)
#define LAMO_CGEN_UNUSED __attribute__((unused))
#else
#define LAMO_CGEN_UNUSED
#endif

// Explicit-buffer form. Kept for future use cases where a caller wants full
// control over buffer lifetime (e.g. embedding multiple user_name() results
// in the same fprintf). Currently unused — all callers use user_name1().
static LAMO_CGEN_UNUSED const char* user_name(const char* name, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%s%s", LAMO_USER_PREFIX, name);
    return buffer;
}

// Convenience wrapper that uses a fresh stack buffer for each call. Use this
// when you only need ONE user_name() per expression. If you need two (e.g.
// for "lamo_add(a, b)"), use user_name() with two explicit buffers.
static const char* user_name1(const char* name) {
    // Each call returns a pointer into its own static buffer. We keep a small
    // ring of 4 buffers so that up to 4 user_name1() calls in the same
    // expression don't collide. This is a pragmatic compromise: the previous
    // code had a single static buffer (Bug #3), which broke for cases like
    // `user_name(a) + ", " + user_name(a)`. The ring is not thread-safe but
    // the compiler is single-threaded.
    static char ring[4][LAMO_USER_NAME_MAX];
    static int idx = 0;
    char* buffer = ring[idx];
    idx = (idx + 1) % 4;
    snprintf(buffer, LAMO_USER_NAME_MAX, "%s%s", LAMO_USER_PREFIX, name);
    return buffer;
}

static void generate_statement_code(ASTNode* node, FILE* out);
static void generate_expression_code(ASTNode* node, FILE* out);
static void generate_call_arguments(ASTNode** args, int arg_count, FILE* out);
/* Sprint 2 refactor: is_gui_builtin / is_http_builtin / is_lang_builtin are
 * now inline functions in builtins.h, so we just use lamo_builtin_is_*()
 * directly. The forward declarations below are kept for the code paths that
 * still call them by the old names. */
#define is_gui_builtin(name)    lamo_builtin_is_gui(name)
#define is_http_builtin(name)   lamo_builtin_is_http(name)
#define is_lang_builtin(name)   lamo_builtin_is_lang(name)
static void generate_lang_builtin_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out);
static void generate_gui_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out);
static void generate_http_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out);
static void emit_runtime(FILE* out, int needs_gui, int needs_http, int feat_flags);
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

/* Sprint 2 refactor: is_gui_builtin / is_http_builtin / is_lang_builtin
 * were inlined into lamo_builtin_is_*() in builtins.h. The macros above
 * rewire the old call sites to the new shared table. */



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
    /* Sprint 3: array builtins. */
    if (strcmp(name, "len") == 0) {
        fprintf(out, "lamo_array_len(");
        generate_expression_code(args[0], out);
        fprintf(out, ")");
        return;
    }
    if (strcmp(name, "push") == 0) {
        fprintf(out, "lamo_array_push(");
        generate_expression_code(args[0], out);
        fprintf(out, ", ");
        generate_expression_code(args[1], out);
        fprintf(out, ")");
        return;
    }
    if (strcmp(name, "pop") == 0) {
        fprintf(out, "lamo_array_pop(");
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


// Emits the entire Lamo runtime (value + GUI + HTTP) by writing the pre-built
// string literal lamo_runtime_source (see lamo_runtime_data.c). The value
// runtime is always emitted; GUI and HTTP runtimes are gated behind #define
// LAMO_NEEDS_GUI_RUNTIME / LAMO_NEEDS_HTTP_RUNTIME, which we set here based
// on whether the program uses those builtins. This replaces ~600 lines of
// fprintf() calls in the old emit_value_runtime / emit_gui_runtime /
// emit_http_runtime functions (nit #9 fix).
#define FEAT_STRINGS (1 << 0)
#define FEAT_ARRAYS  (1 << 1)
#define FEAT_FLOATS  (1 << 2)

static void emit_runtime(FILE* out, int needs_gui, int needs_http, int feat_flags) {
    fputs("#define LAMO_NEEDS_VALUE_RUNTIME 1\n", out);
    /* Fine-grained feature flags: let GCC dead-strip unused subsections
     * even at -O0. A program that never touches strings, arrays, or floats
     * sees a measurably smaller generated .c and faster GCC invocation. */
    if (feat_flags & FEAT_STRINGS) fputs("#define LAMO_NEEDS_STRING_OPS 1\n", out);
    if (feat_flags & FEAT_ARRAYS)  fputs("#define LAMO_NEEDS_ARRAY_OPS 1\n",  out);
    if (feat_flags & FEAT_FLOATS)  fputs("#define LAMO_NEEDS_FLOAT_OPS 1\n",  out);
    if (needs_gui) {
        fputs("#define LAMO_NEEDS_GUI_RUNTIME 1\n", out);
    }
    if (needs_http) {
        fputs("#define LAMO_NEEDS_HTTP_RUNTIME 1\n", out);
    }
    fputs(lamo_runtime_source, out);
    fputs("\n#undef LAMO_NEEDS_VALUE_RUNTIME\n", out);
    if (feat_flags & FEAT_STRINGS) fputs("#undef LAMO_NEEDS_STRING_OPS\n", out);
    if (feat_flags & FEAT_ARRAYS)  fputs("#undef LAMO_NEEDS_ARRAY_OPS\n",  out);
    if (feat_flags & FEAT_FLOATS)  fputs("#undef LAMO_NEEDS_FLOAT_OPS\n",  out);
    if (needs_gui) {
        fputs("#undef LAMO_NEEDS_GUI_RUNTIME\n", out);
    }
    if (needs_http) {
        fputs("#undef LAMO_NEEDS_HTTP_RUNTIME\n", out);
    }
    fputs("\n", out);
}

/* Sprint 2 refactor: ast_uses_gui() and ast_uses_http() were ~95% copy-paste
 * of the same AST walk. Now we have a single recursive walker that takes a
 * predicate ("does this call name match the builtin family we're looking
 * for?") and returns 1 if any call in the AST matches. The two old entry
 * points become one-line wrappers around ast_uses_builtin().
 *
 * The predicate is a function pointer rather than a category enum so that
 * future call sites (e.g. "does this AST call any shadowable builtin?")
 * can use the same walker without extending the table. */
static int ast_uses_builtin(ASTNode* node, int (*predicate)(const char*)) {
    int i;

    if (!node) {
        return 0;
    }

    switch (node->type) {
        case AST_PROGRAM: {
            ASTNode* current = ((ASTProgram*)node)->declarations;
            while (current) {
                if (ast_uses_builtin(current, predicate)) {
                    return 1;
                }
                current = current->next;
            }
            return 0;
        }
        case AST_VAR_DECL:
            return ast_uses_builtin(((ASTVarDecl*)node)->initializer, predicate);
        case AST_FN_DECL:
            return ast_uses_builtin(((ASTFnDecl*)node)->body, predicate);
        case AST_BLOCK: {
            ASTNode* current = ((ASTBlock*)node)->statements;
            while (current) {
                if (ast_uses_builtin(current, predicate)) {
                    return 1;
                }
                current = current->next;
            }
            return 0;
        }
        case AST_IF_STMT: {
            ASTIfStmt* if_stmt = (ASTIfStmt*)node;
            return ast_uses_builtin(if_stmt->condition, predicate) ||
                   ast_uses_builtin(if_stmt->then_branch, predicate) ||
                   ast_uses_builtin(if_stmt->else_branch, predicate);
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* while_stmt = (ASTWhileStmt*)node;
            return ast_uses_builtin(while_stmt->condition, predicate) ||
                   ast_uses_builtin(while_stmt->body, predicate);
        }
        case AST_FOR_STMT: {
            ASTForStmt* for_stmt = (ASTForStmt*)node;
            return ast_uses_builtin(for_stmt->initializer, predicate) ||
                   ast_uses_builtin(for_stmt->condition, predicate) ||
                   ast_uses_builtin(for_stmt->increment, predicate) ||
                   ast_uses_builtin(for_stmt->body, predicate);
        }
        case AST_RETURN_STMT:
            return ast_uses_builtin(((ASTReturnStmt*)node)->expression, predicate);
        case AST_ASSIGN_STMT:
            return ast_uses_builtin(((ASTAssignStmt*)node)->value, predicate);
        case AST_CALL_STMT: {
            ASTCallStmt* call_stmt = (ASTCallStmt*)node;
            if (predicate(call_stmt->name)) {
                return 1;
            }
            for (i = 0; i < call_stmt->arg_count; i++) {
                if (ast_uses_builtin(call_stmt->args[i], predicate)) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* call_expr = (ASTCallExpr*)node;
            if (predicate(call_expr->name)) {
                return 1;
            }
            for (i = 0; i < call_expr->arg_count; i++) {
                if (ast_uses_builtin(call_expr->args[i], predicate)) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_BINARY_EXPR: {
            ASTBinaryExpr* expr = (ASTBinaryExpr*)node;
            return ast_uses_builtin(expr->left, predicate) ||
                   ast_uses_builtin(expr->right, predicate);
        }
        case AST_UNARY_EXPR:
            return ast_uses_builtin(((ASTUnaryExpr*)node)->right, predicate);
        case AST_GROUPING_EXPR:
            return ast_uses_builtin(((ASTGroupingExpr*)node)->expression, predicate);
        case AST_ARRAY_LITERAL: {
            /* Sprint 3: array literal — walk each element expression. */
            ASTArrayLiteral* arr = (ASTArrayLiteral*)node;
            int i;
            for (i = 0; i < arr->element_count; i++) {
                if (ast_uses_builtin(arr->elements[i], predicate)) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_INDEX_EXPR: {
            ASTIndexExpr* idx = (ASTIndexExpr*)node;
            return ast_uses_builtin(idx->array, predicate) ||
                   ast_uses_builtin(idx->index, predicate);
        }
        case AST_PROP_EXPR:
            return ast_uses_builtin(((ASTPropExpr*)node)->object, predicate);
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

static int ast_uses_gui(ASTNode* node) {
    return ast_uses_builtin(node, lamo_builtin_is_gui);
}

static int ast_uses_http(ASTNode* node) {
    return ast_uses_builtin(node, lamo_builtin_is_http);
}

/* ── Feature detection: string, array, float ops ─────────────────────────
 * These walk the AST and return 1 if the program uses the corresponding
 * feature. Used to emit fine-grained #define flags before the runtime so
 * GCC can dead-strip the unused sections even at -O0.
 *
 * "String ops" means: any string literal, any explicit string concatenation
 * (+ on strings), or a call to str()/input()/len().
 * "Array ops" means: any array literal, push/pop/append/array() calls.
 * "Float ops" means: any float literal, or float() cast. */

static int is_string_builtin(const char* name) {
    return strcmp(name, "str") == 0 ||
           strcmp(name, "input") == 0 ||
           strcmp(name, "len") == 0;
}
static int is_array_builtin(const char* name) {
    return strcmp(name, "push") == 0 ||
           strcmp(name, "pop") == 0 ||
           strcmp(name, "append") == 0 ||
           strcmp(name, "array") == 0;
}
static int is_float_builtin(const char* name) {
    return strcmp(name, "float") == 0 ||
           strcmp(name, "sqrt") == 0 ||
           strcmp(name, "pow") == 0 ||
           strcmp(name, "floor") == 0 ||
           strcmp(name, "ceil") == 0 ||
           strcmp(name, "abs") == 0;
}

/* Single combined walk that detects all three features in one pass.
 * Sets bits in *flags: bit 0 = strings, bit 1 = arrays, bit 2 = floats. */

static void ast_detect_features(ASTNode* node, int* flags) {
    int i;
    if (!node || *flags == (FEAT_STRINGS | FEAT_ARRAYS | FEAT_FLOATS)) return;

    switch (node->type) {
        case AST_STRING_LITERAL:
            *flags |= FEAT_STRINGS;
            return;
        case AST_FLOAT_LITERAL:
            *flags |= FEAT_FLOATS;
            return;
        case AST_ARRAY_LITERAL: {
            ASTArrayLiteral* arr = (ASTArrayLiteral*)node;
            *flags |= FEAT_ARRAYS;
            for (i = 0; i < arr->element_count; i++)
                ast_detect_features(arr->elements[i], flags);
            return;
        }
        case AST_PROP_EXPR:
            /* .len property implies string or array usage. */
            *flags |= FEAT_STRINGS;
            ast_detect_features(((ASTPropExpr*)node)->object, flags);
            return;
        case AST_INDEX_EXPR: {
            ASTIndexExpr* ie = (ASTIndexExpr*)node;
            /* Index on a string is a string op; on array is array op.
             * We can't know which without type info, so set both. */
            *flags |= FEAT_STRINGS | FEAT_ARRAYS;
            ast_detect_features(ie->array, flags);
            ast_detect_features(ie->index, flags);
            return;
        }
        case AST_CALL_STMT: {
            ASTCallStmt* cs = (ASTCallStmt*)node;
            if (is_string_builtin(cs->name)) *flags |= FEAT_STRINGS;
            if (is_array_builtin(cs->name))  *flags |= FEAT_ARRAYS;
            if (is_float_builtin(cs->name))  *flags |= FEAT_FLOATS;
            for (i = 0; i < cs->arg_count; i++)
                ast_detect_features(cs->args[i], flags);
            return;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* ce = (ASTCallExpr*)node;
            if (is_string_builtin(ce->name)) *flags |= FEAT_STRINGS;
            if (is_array_builtin(ce->name))  *flags |= FEAT_ARRAYS;
            if (is_float_builtin(ce->name))  *flags |= FEAT_FLOATS;
            for (i = 0; i < ce->arg_count; i++)
                ast_detect_features(ce->args[i], flags);
            return;
        }
        case AST_BINARY_EXPR: {
            ASTBinaryExpr* be = (ASTBinaryExpr*)node;
            /* % with floats at runtime calls lamo_mod which uses fmod — float op. */
            if (be->operator == TOKEN_PERCENT) *flags |= FEAT_FLOATS;
            ast_detect_features(be->left, flags);
            ast_detect_features(be->right, flags);
            return;
        }
        /* Recurse through all structural nodes. */
        case AST_PROGRAM: {
            for (ASTNode* c = ((ASTProgram*)node)->declarations; c; c = c->next)
                ast_detect_features(c, flags);
            return;
        }
        case AST_VAR_DECL:
            ast_detect_features(((ASTVarDecl*)node)->initializer, flags); return;
        case AST_FN_DECL:
            ast_detect_features(((ASTFnDecl*)node)->body, flags); return;
        case AST_BLOCK: {
            for (ASTNode* s = ((ASTBlock*)node)->statements; s; s = s->next)
                ast_detect_features(s, flags);
            return;
        }
        case AST_IF_STMT: {
            ASTIfStmt* is = (ASTIfStmt*)node;
            ast_detect_features(is->condition, flags);
            ast_detect_features(is->then_branch, flags);
            ast_detect_features(is->else_branch, flags);
            return;
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* ws = (ASTWhileStmt*)node;
            ast_detect_features(ws->condition, flags);
            ast_detect_features(ws->body, flags);
            return;
        }
        case AST_FOR_STMT: {
            ASTForStmt* fs = (ASTForStmt*)node;
            ast_detect_features(fs->initializer, flags);
            ast_detect_features(fs->condition, flags);
            ast_detect_features(fs->increment, flags);
            ast_detect_features(fs->body, flags);
            return;
        }
        case AST_RETURN_STMT:
            ast_detect_features(((ASTReturnStmt*)node)->expression, flags); return;
        case AST_ASSIGN_STMT:
            ast_detect_features(((ASTAssignStmt*)node)->value, flags); return;
        case AST_UNARY_EXPR:
            ast_detect_features(((ASTUnaryExpr*)node)->right, flags); return;
        case AST_GROUPING_EXPR:
            ast_detect_features(((ASTGroupingExpr*)node)->expression, flags); return;
        default:
            return;
    }
}

void generate_c_code(ASTNode* node, FILE* out) {
    ASTNode* current;
    int needs_gui_runtime;
    int needs_http_runtime;
    int feat_flags = 0;

    if (!node) {
        return;
    }

    fprintf(out, "// Generated by Lamo v2 (via AST)\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include <string.h>\n\n");
    needs_gui_runtime = ast_uses_gui(node);
    needs_http_runtime = ast_uses_http(node);
    ast_detect_features(node, &feat_flags);
    emit_runtime(out, needs_gui_runtime, needs_http_runtime, feat_flags);

    // 1. Forward declarations de funções definidas pelo usuário.
    current = ((ASTProgram*)node)->declarations;
    while (current) {
        if (current->type == AST_FN_DECL) {
            ASTFnDecl* fn_decl = (ASTFnDecl*)current;
            int i;
            fprintf(out, "LamoValue %s(", user_name1(fn_decl->name));
            for (i = 0; i < fn_decl->param_count; i++) {
                if (i > 0) {
                    fprintf(out, ", ");
                }
                fprintf(out, "LamoValue %s", user_name1(fn_decl->params[i]));
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
            fprintf(out, "static LamoValue %s;\n", user_name1(var_decl->name));
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
            fprintf(out, "%s = ", user_name1(var_decl->name));
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

static void generate_assignment_code(const char* name, ASTNode* value, LamoTokenType op_type, FILE* out) {
    // Bug #3 fix: each user_name1() call uses a different slot in a 4-entry
    // ring buffer, so multiple calls in the same statement (e.g. the += case
    // below, which references `name` twice) cannot clobber each other.
    fprintf(out, "%s = ", user_name1(name));
    if (op_type == TOKEN_PLUS_EQ) {
        fprintf(out, "lamo_add(%s, ", user_name1(name));
        generate_expression_code(value, out);
        fprintf(out, ")");
    } else if (op_type == TOKEN_MINUS_EQ) {
        fprintf(out, "lamo_sub(%s, ", user_name1(name));
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
            fprintf(out, "LamoValue %s = ", user_name1(var_decl->name));
            generate_expression_code(var_decl->initializer, out);
            fprintf(out, ";\n");
            break;
        }
        case AST_FN_DECL: {
            ASTFnDecl* fn_decl = (ASTFnDecl*)node;
            int i;
            fprintf(out, "LamoValue %s(", user_name1(fn_decl->name));
            for (i = 0; i < fn_decl->param_count; i++) {
                if (i > 0) {
                    fprintf(out, ", ");
                }
                fprintf(out, "LamoValue %s", user_name1(fn_decl->params[i]));
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
                    fprintf(out, "LamoValue %s = ", user_name1(var_decl->name));
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
        case AST_BREAK_STMT:
            // break/continue em Lamo mapeiam 1:1 para break/continue em C, mas
            // só são válidos dentro de while/for (checado pelo semântico).
            fprintf(out, "break;\n");
            break;
        case AST_CONTINUE_STMT:
            fprintf(out, "continue;\n");
            break;
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
                fprintf(out, "%s(", user_name1(call_stmt->name));
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
    /* Sprint 3: constant folding for arithmetic on literal operands.
     *
     * If both sides are numeric literals, we evaluate the expression at
     * compile time and emit a single literal instead of a runtime call.
     * This is a small but real performance win for code that does
     * arithmetic on constants (e.g. `let buf_size = 1024 * 4;`), and it
     * also shrinks the generated C.
     *
     * We deliberately fold only when BOTH operands are literals — this
     * avoids needing a full constant-propagation pass. Foldable cases:
     *   int + int   -> int
     *   int + float -> float
     *   float + int -> float
     *   float + float -> float
     * Same for -, *, /, %.
     *
     * Division/modulo by zero is a runtime error in Lamo; we don't fold
     * those (let the runtime emit the proper error message). */
    if (expr->left->type == AST_INT_LITERAL || expr->left->type == AST_FLOAT_LITERAL) {
        if (expr->right->type == AST_INT_LITERAL || expr->right->type == AST_FLOAT_LITERAL) {
            int left_is_float  = expr->left->type  == AST_FLOAT_LITERAL;
            int right_is_float = expr->right->type == AST_FLOAT_LITERAL;
            long long li = left_is_float  ? 0 : ((ASTIntLiteral*)expr->left)->value;
            double     lf = left_is_float  ? ((ASTFloatLiteral*)expr->left)->value  : 0.0;
            long long ri = right_is_float ? 0 : ((ASTIntLiteral*)expr->right)->value;
            double     rf = right_is_float ? ((ASTFloatLiteral*)expr->right)->value : 0.0;

            switch (expr->operator) {
                case TOKEN_PLUS:
                    if (left_is_float || right_is_float) {
                        fprintf(out, "lamo_make_float(%#.17g)", (double)(left_is_float ? lf : (double)li) + (right_is_float ? rf : (double)ri));
                    } else {
                        fprintf(out, "lamo_make_int(%lldLL)", li + ri);
                    }
                    return;
                case TOKEN_MINUS:
                    if (left_is_float || right_is_float) {
                        fprintf(out, "lamo_make_float(%#.17g)", (double)(left_is_float ? lf : (double)li) - (right_is_float ? rf : (double)ri));
                    } else {
                        fprintf(out, "lamo_make_int(%lldLL)", li - ri);
                    }
                    return;
                case TOKEN_STAR:
                    if (left_is_float || right_is_float) {
                        fprintf(out, "lamo_make_float(%#.17g)", (double)(left_is_float ? lf : (double)li) * (right_is_float ? rf : (double)ri));
                    } else {
                        fprintf(out, "lamo_make_int(%lldLL)", li * ri);
                    }
                    return;
                case TOKEN_SLASH:
                    /* Don't fold if the divisor is zero — let the runtime
                     * emit its "division by zero" error. */
                    if ((right_is_float && rf != 0.0) || (!right_is_float && ri != 0)) {
                        if (left_is_float || right_is_float) {
                            fprintf(out, "lamo_make_float(%#.17g)", (left_is_float ? lf : (double)li) / (right_is_float ? rf : (double)ri));
                        } else {
                            fprintf(out, "lamo_make_int(%lldLL)", li / ri);
                        }
                        return;
                    }
                    break;  /* fall through to runtime call */
                case TOKEN_PERCENT:
                    if ((right_is_float && rf != 0.0) || (!right_is_float && ri != 0)) {
                        if (left_is_float || right_is_float) {
                            fprintf(out, "lamo_make_float(%#.17g)", fmod(left_is_float ? lf : (double)li, right_is_float ? rf : (double)ri));
                        } else {
                            fprintf(out, "lamo_make_int(%lldLL)", li % ri);
                        }
                        return;
                    }
                    break;  /* fall through to runtime call */
                default:
                    /* Comparison/logical operators: not folded here.
                     * They could be, but the win is smaller and the
                     * boolean semantics need careful handling. */
                    break;
            }
        }
    }

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
            fprintf(out, "%s", user_name1(((ASTIdentifier*)node)->name));
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
                fprintf(out, "%s(", user_name1(call_expr->name));
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
        case AST_ARRAY_LITERAL: {
            /* Sprint 3: array literal. We emit a sequence of LamoValue
             * initializers inside a compound literal, then call
             * lamo_array_from_values. The compound literal has its
             * address taken, so it lives on the stack for the duration
             * of the call. */
            ASTArrayLiteral* arr = (ASTArrayLiteral*)node;
            int i;
            if (arr->element_count == 0) {
                fprintf(out, "lamo_array_from_values((LamoValue*)0, 0)");
            } else {
                fprintf(out, "lamo_array_from_values((LamoValue[]){");
                for (i = 0; i < arr->element_count; i++) {
                    if (i > 0) fprintf(out, ", ");
                    generate_expression_code(arr->elements[i], out);
                }
                fprintf(out, "}, %d)", arr->element_count);
            }
            break;
        }
        case AST_INDEX_EXPR: {
            /* Sprint 3: array index access. We coerce the index to int
             * (Lamo indexing is int-only for now) and call lamo_array_get. */
            ASTIndexExpr* idx = (ASTIndexExpr*)node;
            fprintf(out, "lamo_array_get(");
            generate_expression_code(idx->array, out);
            fprintf(out, ", lamo_as_int(");
            generate_expression_code(idx->index, out);
            fprintf(out, "))");
            break;
        }
        case AST_PROP_EXPR: {
            /* Sprint 3: property access. Currently only `.len` is
             * supported; the semantic pass rejects other property
             * names. We emit a direct call to lamo_array_len. */
            ASTPropExpr* prop = (ASTPropExpr*)node;
            if (strcmp(prop->prop_name, "len") == 0) {
                fprintf(out, "lamo_array_len(");
                generate_expression_code(prop->object, out);
                fprintf(out, ")");
            } else {
                /* Defensive: should be unreachable if the semantic pass
                 * is correct, but generate lamo_make_int(0) to avoid
                 * emitting broken C. */
                fprintf(out, "lamo_make_int(0)");
            }
            break;
        }
        default:
            fprintf(out, "lamo_make_int(0)");
            break;
    }
}

