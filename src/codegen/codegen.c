#include "codegen.h"
#include "lamo_runtime_data.h"
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
static int is_gui_builtin(const char* name);
static int is_http_builtin(const char* name);
static int is_lang_builtin(const char* name);
static void generate_lang_builtin_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out);
static void generate_gui_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out);
static void generate_http_call_expr(const char* name, ASTNode** args, int arg_count, FILE* out);
static void emit_runtime(FILE* out, int needs_gui, int needs_http);
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


// Emits the entire Lamo runtime (value + GUI + HTTP) by writing the pre-built
// string literal lamo_runtime_source (see lamo_runtime_data.c). The value
// runtime is always emitted; GUI and HTTP runtimes are gated behind #define
// LAMO_NEEDS_GUI_RUNTIME / LAMO_NEEDS_HTTP_RUNTIME, which we set here based
// on whether the program uses those builtins. This replaces ~600 lines of
// fprintf() calls in the old emit_value_runtime / emit_gui_runtime /
// emit_http_runtime functions (nit #9 fix).
static void emit_runtime(FILE* out, int needs_gui, int needs_http) {
    fputs("#define LAMO_NEEDS_VALUE_RUNTIME 1\n", out);
    if (needs_gui) {
        fputs("#define LAMO_NEEDS_GUI_RUNTIME 1\n", out);
    }
    if (needs_http) {
        fputs("#define LAMO_NEEDS_HTTP_RUNTIME 1\n", out);
    }
    fputs(lamo_runtime_source, out);
    fputs("\n#undef LAMO_NEEDS_VALUE_RUNTIME\n", out);
    if (needs_gui) {
        fputs("#undef LAMO_NEEDS_GUI_RUNTIME\n", out);
    }
    if (needs_http) {
        fputs("#undef LAMO_NEEDS_HTTP_RUNTIME\n", out);
    }
    fputs("\n", out);
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
    needs_gui_runtime = ast_uses_gui(node);
    needs_http_runtime = ast_uses_http(node);
    emit_runtime(out, needs_gui_runtime, needs_http_runtime);

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

static void generate_assignment_code(const char* name, ASTNode* value, TokenType op_type, FILE* out) {
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
        default:
            fprintf(out, "lamo_make_int(0)");
            break;
    }
}

