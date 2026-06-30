#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "parser.h"
#include "../error_util.h"

struct Parser {
    Lexer* lexer;
    Token current;
    int error_count;
    int panic_mode;   // se 1, suprime próximos erros até sincronizar
    const char* file_path;  // Bug #4 fix: path exibido nas mensagens de erro
};

Parser* parser_init(Lexer* lexer) {
    return parser_init_with_file(lexer, NULL);
}

Parser* parser_init_with_file(Lexer* lexer, const char* file_path) {
    Parser* p = malloc(sizeof(Parser));
    if (!p) {
        perror("Failed to allocate Parser");
        exit(EXIT_FAILURE);
    }
    p->lexer = lexer;
    p->current = lexer_next_token(lexer);
    p->error_count = 0;
    p->panic_mode = 0;
    p->file_path = file_path;
    // Bug #5 fix: todos os nós da AST criados a partir deste momento até o
    // fim deste parse vão receber file_path como origem. O caller é dono do
    // string e deve mantê-lo vivo enquanto a AST existir (lamo_v2.c usa
    // normalized_path que vive na CompilationState até o fim).
    ast_set_default_file_path(file_path);
    return p;
}

void parser_free(Parser* p) {
    if (!p) return;
    token_free(p->current);
    free(p);
}

const char* parser_file_path(const Parser* p) {
    return p ? p->file_path : NULL;
}

static void advance_p(Parser* p) {
    token_free(p->current);
    p->current = lexer_next_token(p->lexer);
}

// Sincroniza o parser após um erro: descarta tokens até encontrar um ponto
// estável (statement boundary como ; ou }, ou início de statement como let, fn,
// if, while, for, return, import). Técnica padrão de "synchronize-and-continue"
// (Crafting Interpreters, cap. 8).
static void parser_synchronize(Parser* p) {
    p->panic_mode = 0;

    while (p->current.type != TOKEN_EOF) {
        if (p->current.type == TOKEN_SEMICOLON) {
            advance_p(p);
            return;
        }
        switch (p->current.type) {
            case TOKEN_LET:
            case TOKEN_FN:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_RETURN:
            case TOKEN_IMPORT:
            case TOKEN_BREAK:
            case TOKEN_CONTINUE:
                return;
            default:
                advance_p(p);
                break;
        }
    }
}

void parser_error(Parser* p, const char* msg) {
    const char* label;
    if (p->panic_mode) {
        return; // já estamos em recuperação, ignora erros derivados
    }
    p->panic_mode = 1;
    p->error_count++;
    // Bug #4 fix: inclui o path do arquivo na mensagem de erro, igual ao
    // semantic.c. Caso o parser tenha sido criado sem file_path (caso legado
    // de parser_init()), usa "<input>".
    label = p->file_path ? p->file_path : "<input>";
    fprintf(stderr, "\n%s:%d:%d: [Syntax Error] %s\n",
            label, p->current.line, p->current.column, msg);
    fprintf(stderr, "  found token: %s (%s)\n",
            token_type_name(p->current.type),
            p->current.value ? p->current.value : "<null>");
    /* Sprint 3 fix: print the source line + a caret pointing at the
     * column. The lexer owns the source string, so we ask it for the
     * raw pointer. If the parser was constructed without a lexer
     * (defensive), no snippet is printed. */
    if (p->lexer) {
        const char* source = NULL;
        /* lexer_source() is not in the public API but the Lexer struct
         * is defined in lexer.h with a `source` field, so we can read
         * it directly. We go through the struct because there's no
         * getter. */
        source = ((Lexer*)p->lexer)->source;
        error_print_snippet(stderr, source, p->current.line, p->current.column);
    }
    // NÃO chama exit(1) — deixa o caller tentar sincronizar e continuar.
}

int parser_had_error(const Parser* p) {
    return p->error_count > 0;
}

int parser_error_count(const Parser* p) {
    return p->error_count;
}

static void eat_p(Parser* p, LamoTokenType type) {
    if (p->current.type == type) {
        advance_p(p);
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "expected %s, got %s",
                 token_type_name(type), token_type_name(p->current.type));
        parser_error(p, buf);
    }
}

/* expect_p: like eat_p but emits a human-readable message instead of the
 * raw token type name. Use this at call sites where the context makes the
 * error unambiguous (e.g. "missing ';' after statement" is clearer than
 * "expected TOKEN_SEMICOLON, got TOKEN_IDENTIFIER"). */
static void expect_p(Parser* p, LamoTokenType type, const char* human_msg) {
    if (p->current.type == type) {
        advance_p(p);
    } else {
        parser_error(p, human_msg);
    }
}

ASTNode* parse_expression(Parser* p);
static ASTNode* parse_primary(Parser* p);

/* Sprint 3: parse_postfix — handles `expr[index]` and `expr.prop` after
 * a primary expression. We treat both as left-associative postfix
 * operators, so `a[0][1].len` parses as `((a[0])[1]).len`.
 *
 * The implementation is a loop that keeps wrapping the current node
 * in an index or prop expr as long as we see `[` or `.`. */
static ASTNode* parse_postfix(Parser* p);

static ASTNode* parse_postfix(Parser* p) {
    ASTNode* node = parse_primary(p);
    while (1) {
        if (p->current.type == TOKEN_LBRACKET) {
            int line = p->current.line;
            int column = p->current.column;
            advance_p(p);  /* consume '[' */
            ASTNode* index = parse_expression(p);
            eat_p(p, TOKEN_RBRACKET);
            node = (ASTNode*)ast_new_index_expr(node, index, line, column);
        } else if (p->current.type == TOKEN_DOT) {
            int line = p->current.line;
            int column = p->current.column;
            advance_p(p);  /* consume '.' */
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected property name after '.'");
                return node;  /* return what we have so far; caller may recover */
            }
            char* prop_name = strdup(p->current.value);
            advance_p(p);
            node = (ASTNode*)ast_new_prop_expr(node, prop_name, line, column);
            free(prop_name);
        } else {
            break;
        }
    }
    return node;
}

// Tenta sincronizar e retornar NULL. Usado quando um erro impede de construir
// um nó válido: o caller recebe NULL e o parser continua a partir do próximo
// statement estável.
static ASTNode* parser_recover(Parser* p) {
    parser_synchronize(p);
    return NULL;
}

// Converte um literal inteiro (decimal, hex, binário) para long long.
// Trata "0x..." com strtoll base 16, "0b..." manualmente, e decimal com strtoll.
static long long parse_int_literal(const char* text) {
    if (!text) return 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        return strtoll(text, NULL, 16);
    }
    if (text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
        // pula "0b"
        const char* digits = text + 2;
        long long v = 0;
        while (*digits) {
            if (*digits == '0' || *digits == '1') {
                v = (v << 1) | (*digits - '0');
            }
            digits++;
        }
        return v;
    }
    return strtoll(text, NULL, 10);
}

static ASTNode* parse_primary(Parser* p) {
    if (p->current.type == TOKEN_INT) {
        long long val = parse_int_literal(p->current.value);
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        return (ASTNode*)ast_new_int_literal(val, line, column);
    }
    else if (p->current.type == TOKEN_FLOAT) {
        double val = strtod(p->current.value, NULL);
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        return (ASTNode*)ast_new_float_literal(val, line, column);
    }
    else if (p->current.type == TOKEN_STRING) {
        int line = p->current.line;
        int column = p->current.column;
        ASTNode* node = (ASTNode*)ast_new_string_literal(p->current.value, line, column);
        advance_p(p);
        return node;
    }
    else if (p->current.type == TOKEN_TRUE) {
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        return (ASTNode*)ast_new_bool_literal(1, line, column);
    }
    else if (p->current.type == TOKEN_FALSE) {
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        return (ASTNode*)ast_new_bool_literal(0, line, column);
    }
    else if (p->current.type == TOKEN_IDENTIFIER) {
        char* name = strdup(p->current.value);
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);

        if (p->current.type == TOKEN_LPAREN) {
            eat_p(p, TOKEN_LPAREN);
            ASTNode** args = NULL;
            int arg_count = 0;
            while (p->current.type != TOKEN_RPAREN && p->current.type != TOKEN_EOF) {
                ASTNode* arg = parse_expression(p);
                if (arg) {
                    ASTNode** resized = realloc(args, sizeof(ASTNode*) * (size_t)(arg_count + 1));
                    if (!resized) {
                        parser_error(p, "out of memory while growing argument list");
                        ast_free(arg);
                        free(args);
                        free(name);
                        return parser_recover(p);
                    }
                    args = resized;
                    args[arg_count++] = arg;
                }
                if (p->current.type == TOKEN_COMMA) advance_p(p);
            }
            eat_p(p, TOKEN_RPAREN);
            ASTNode* node = (ASTNode*)ast_new_call_expr(name, args, arg_count, line, column);
            free(name);
            return node;
        } else {
            ASTNode* node = (ASTNode*)ast_new_identifier(name, line, column);
            free(name);
            return node;
        }
    }
    else if (p->current.type == TOKEN_LPAREN) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_LPAREN);
        ASTNode* expr = parse_expression(p);
        eat_p(p, TOKEN_RPAREN);
        return (ASTNode*)ast_new_grouping_expr(expr, line, column);
    }
    else if (p->current.type == TOKEN_LBRACKET) {
        /* Sprint 3: array literal — `[expr, expr, ...]`. The empty
         * array `[]` is allowed and produces a zero-length array. */
        int line = p->current.line;
        int column = p->current.column;
        ASTNode** elements = NULL;
        int element_count = 0;
        eat_p(p, TOKEN_LBRACKET);
        while (p->current.type != TOKEN_RBRACKET && p->current.type != TOKEN_EOF) {
            ASTNode* elem = parse_expression(p);
            if (elem) {
                ASTNode** resized = realloc(elements, sizeof(ASTNode*) * (size_t)(element_count + 1));
                if (!resized) {
                    parser_error(p, "out of memory while growing array literal");
                    ast_free(elem);
                    for (int i = 0; i < element_count; i++) ast_free(elements[i]);
                    free(elements);
                    return parser_recover(p);
                }
                elements = resized;
                elements[element_count++] = elem;
            }
            if (p->current.type == TOKEN_COMMA) advance_p(p);
        }
        eat_p(p, TOKEN_RBRACKET);
        return (ASTNode*)ast_new_array_literal(elements, element_count, line, column);
    }
    else {
        char buf[256];
        snprintf(buf, sizeof(buf), "invalid expression, found %s",
                 token_type_name(p->current.type));
        parser_error(p, buf);
        return NULL;
    }
}

static ASTNode* parse_unary(Parser* p) {
    if (p->current.type == TOKEN_BANG || p->current.type == TOKEN_MINUS) {
        LamoTokenType op_type = p->current.type;
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        ASTNode* right = parse_unary(p);
        return (ASTNode*)ast_new_unary_expr(op_type, right, line, column);
    } else {
        /* Sprint 3: parse_postfix handles `expr[idx]` and `expr.prop`
         * on top of parse_primary. */
        return parse_postfix(p);
    }
}

static ASTNode* parse_factor(Parser* p) {
    ASTNode* left = parse_unary(p);
    while (p->current.type == TOKEN_STAR || p->current.type == TOKEN_SLASH ||
           p->current.type == TOKEN_PERCENT) {
        LamoTokenType op_type = p->current.type;
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        ASTNode* right = parse_unary(p);
        left = (ASTNode*)ast_new_binary_expr(left, op_type, right, line, column);
    }
    return left;
}

static ASTNode* parse_term(Parser* p) {
    ASTNode* left = parse_factor(p);
    while (p->current.type == TOKEN_PLUS || p->current.type == TOKEN_MINUS) {
        LamoTokenType op_type = p->current.type;
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        ASTNode* right = parse_factor(p);
        left = (ASTNode*)ast_new_binary_expr(left, op_type, right, line, column);
    }
    return left;
}

static ASTNode* parse_comparison(Parser* p) {
    ASTNode* left = parse_term(p);
    while (p->current.type == TOKEN_LT || p->current.type == TOKEN_GT ||
           p->current.type == TOKEN_LT_EQ || p->current.type == TOKEN_GT_EQ) {
        LamoTokenType op_type = p->current.type;
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        ASTNode* right = parse_term(p);
        left = (ASTNode*)ast_new_binary_expr(left, op_type, right, line, column);
    }
    return left;
}

static ASTNode* parse_equality(Parser* p) {
    ASTNode* left = parse_comparison(p);
    while (p->current.type == TOKEN_EQ_EQ || p->current.type == TOKEN_BANG_EQ) {
        LamoTokenType op_type = p->current.type;
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        ASTNode* right = parse_comparison(p);
        left = (ASTNode*)ast_new_binary_expr(left, op_type, right, line, column);
    }
    return left;
}

static ASTNode* parse_and(Parser* p) {
    ASTNode* left = parse_equality(p);
    while (p->current.type == TOKEN_AND_AND) {
        LamoTokenType op_type = p->current.type;
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        ASTNode* right = parse_equality(p);
        left = (ASTNode*)ast_new_binary_expr(left, op_type, right, line, column);
    }
    return left;
}

ASTNode* parse_expression(Parser* p) {
    ASTNode* left = parse_and(p);
    while (p->current.type == TOKEN_OR_OR) {
        LamoTokenType op_type = p->current.type;
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        ASTNode* right = parse_and(p);
        left = (ASTNode*)ast_new_binary_expr(left, op_type, right, line, column);
    }
    return left;
}

ASTNode* parse_statement(Parser* p);

static ASTNode* parse_block(Parser* p) {
    int line = p->current.line;
    int column = p->current.column;
    expect_p(p, TOKEN_LBRACE, "expected '{' to open block");
    ASTNode* head = NULL;
    ASTNode* current = NULL;
    while (p->current.type != TOKEN_RBRACE && p->current.type != TOKEN_EOF) {
        ASTNode* stmt = parse_statement(p);
        if (stmt) {
            if (!head) {
                head = stmt;
                current = stmt;
            } else {
                current->next = stmt;
                current = stmt;
            }
        }
        // Se um erro ocorreu dentro do statement, sincroniza dentro do bloco.
        if (p->panic_mode) {
            parser_synchronize(p);
        }
    }
    expect_p(p, TOKEN_RBRACE, "missing '}' — block not closed");
    return (ASTNode*)ast_new_block(head, line, column);
}

ASTNode* parse_statement(Parser* p) {
    if (p->current.type == TOKEN_LET) {
        eat_p(p, TOKEN_LET);
        if (p->current.type != TOKEN_IDENTIFIER) {
            parser_error(p, "expected identifier after let");
            return parser_recover(p);
        }
        char* name = strdup(p->current.value);
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_IDENTIFIER);
        /* Sprint 3: optional type annotation `: int | float | string | bool`.
         * We accept any identifier here — the semantic pass will reject
         * unknown type names with a clearer message than the parser could. */
        char* type_annotation = NULL;
        if (p->current.type == TOKEN_COLON) {
            eat_p(p, TOKEN_COLON);
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected type name after ':' in let declaration");
                free(name);
                return parser_recover(p);
            }
            type_annotation = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);
        }
        eat_p(p, TOKEN_EQUALS);
        /* Sprint 1 fix: explicit "missing initializer" error for `let x = ;`.
         * Without this, parse_expression() would fall through to parse_primary()
         * and emit a generic "invalid expression, found ;" — which technically
         * reports the error but is unhelpful to a beginner writing Lamo.
         * The explicit message tells them exactly what is wrong. */
        if (p->current.type == TOKEN_SEMICOLON) {
            parser_error(p, "missing initializer after '=' in let declaration");
            free(name);
            free(type_annotation);
            return parser_recover(p);
        }
        ASTNode* initializer = parse_expression(p);
        expect_p(p, TOKEN_SEMICOLON, "missing ';' after let declaration");
        ASTNode* node = (ASTNode*)ast_new_var_decl_typed(name, initializer, type_annotation, line, column);
        free(name);
        free(type_annotation);
        return node;
    }
    else if (p->current.type == TOKEN_FN) {
        eat_p(p, TOKEN_FN);
        if (p->current.type != TOKEN_IDENTIFIER) {
            parser_error(p, "expected function name after fn");
            return parser_recover(p);
        }
        char* name = strdup(p->current.value);
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_IDENTIFIER);
        expect_p(p, TOKEN_LPAREN, "expected '(' after function name");

        char** params = NULL;
        char** param_types = NULL;  /* Sprint 3: per-param type annotations */
        int param_count = 0;
        int has_any_type_annotation = 0;

        while (p->current.type != TOKEN_RPAREN && p->current.type != TOKEN_EOF) {
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected parameter name");
                break;
            }
            char** resized = realloc(params, sizeof(char*) * (size_t)(param_count + 1));
            if (!resized) {
                parser_error(p, "out of memory while growing parameter list");
                for (int i = 0; i < param_count; i++) free(params[i]);
                free(params);
                if (param_types) {
                    for (int i = 0; i < param_count; i++) free(param_types[i]);
                    free(param_types);
                }
                free(name);
                return parser_recover(p);
            }
            params = resized;
            params[param_count] = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);

            /* Sprint 3: optional `: type` per parameter. We allocate the
             * param_types array lazily — only if at least one parameter
             * has an annotation. This keeps the common case (no
             * annotations) allocation-free. */
            if (p->current.type == TOKEN_COLON) {
                if (!param_types) {
                    param_types = calloc((size_t)param_count, sizeof(char*));
                    if (!param_types) {
                        parser_error(p, "out of memory while allocating param_types array");
                        for (int i = 0; i < param_count; i++) free(params[i]);
                        free(params);
                        free(name);
                        return parser_recover(p);
                    }
                }
                /* Grow param_types to match params. */
                {
                    char** pt_resized = realloc(param_types, sizeof(char*) * (size_t)(param_count + 1));
                    if (!pt_resized) {
                        parser_error(p, "out of memory while growing param_types array");
                        for (int i = 0; i < param_count; i++) free(params[i]);
                        free(params);
                        for (int i = 0; i < param_count; i++) free(param_types[i]);
                        free(param_types);
                        free(name);
                        return parser_recover(p);
                    }
                    param_types = pt_resized;
                    param_types[param_count] = NULL;
                }
                eat_p(p, TOKEN_COLON);
                if (p->current.type != TOKEN_IDENTIFIER) {
                    parser_error(p, "expected type name after ':' in parameter list");
                    for (int i = 0; i <= param_count; i++) free(params[i]);
                    free(params);
                    for (int i = 0; i < param_count; i++) free(param_types[i]);
                    free(param_types);
                    free(name);
                    return parser_recover(p);
                }
                param_types[param_count] = strdup(p->current.value);
                eat_p(p, TOKEN_IDENTIFIER);
                has_any_type_annotation = 1;
            } else if (param_types) {
                /* Need to grow param_types even when this param has no
                 * annotation, to keep the arrays in sync. */
                char** pt_resized = realloc(param_types, sizeof(char*) * (size_t)(param_count + 1));
                if (!pt_resized) {
                    parser_error(p, "out of memory while growing param_types array");
                    free(name);
                    return parser_recover(p);
                }
                param_types = pt_resized;
                param_types[param_count] = NULL;
            }
            param_count++;
            if (p->current.type == TOKEN_COMMA) advance_p(p);
        }
        expect_p(p, TOKEN_RPAREN, "missing ')' — did you forget to close the parameter list?");
        char* return_type_annotation = NULL;
        if (p->current.type == TOKEN_ARROW) {
            eat_p(p, TOKEN_ARROW);
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected type name after '->' in function signature");
                free(name);
                return parser_recover(p);
            }
            return_type_annotation = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);
            (void)has_any_type_annotation;
        }

        ASTNode* body = parse_block(p);
        ASTNode* node = (ASTNode*)ast_new_fn_decl_typed(name, params, param_types, param_count, return_type_annotation, body, line, column);
        free(name);
        free(return_type_annotation);
        return node;
    }
    else if (p->current.type == TOKEN_IMPORT) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_IMPORT);
        if (p->current.type != TOKEN_STRING) {
            parser_error(p, "expected string path after import");
            return parser_recover(p);
        }
        char* path = strdup(p->current.value);
        eat_p(p, TOKEN_STRING);
        expect_p(p, TOKEN_SEMICOLON, "missing ';' after import statement");
        ASTNode* node = (ASTNode*)ast_new_import_decl(path, line, column);
        free(path);
        return node;
    }
    else if (p->current.type == TOKEN_IDENTIFIER) {
        char* name = strdup(p->current.value);
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);

        if (p->current.type == TOKEN_LPAREN) {
            eat_p(p, TOKEN_LPAREN);
            ASTNode** args = NULL;
            int arg_count = 0;
            while (p->current.type != TOKEN_RPAREN && p->current.type != TOKEN_EOF) {
                ASTNode* arg = parse_expression(p);
                if (arg) {
                    ASTNode** resized = realloc(args, sizeof(ASTNode*) * (size_t)(arg_count + 1));
                    if (!resized) {
                        parser_error(p, "out of memory while growing argument list");
                        ast_free(arg);
                        free(args);
                        free(name);
                        return parser_recover(p);
                    }
                    args = resized;
                    args[arg_count++] = arg;
                }
                if (p->current.type == TOKEN_COMMA) advance_p(p);
            }
            expect_p(p, TOKEN_RPAREN, "missing ')' — did you forget to close the argument list?");
            expect_p(p, TOKEN_SEMICOLON, "missing ';' after function call");
            ASTNode* node = (ASTNode*)ast_new_call_stmt(name, args, arg_count, line, column);
            free(name);
            return node;
        }
        else if (p->current.type == TOKEN_EQUALS || p->current.type == TOKEN_PLUS_EQ ||
                 p->current.type == TOKEN_MINUS_EQ) {
            LamoTokenType op_type = p->current.type;
            advance_p(p);
            ASTNode* value = parse_expression(p);
            expect_p(p, TOKEN_SEMICOLON, "missing ';' after assignment");
            ASTNode* node = (ASTNode*)ast_new_assign_stmt(name, value, op_type, line, column);
            free(name);
            return node;
        } else if (p->current.type == TOKEN_PLUS_PLUS) {
            advance_p(p);
            expect_p(p, TOKEN_SEMICOLON, "missing ';' after '++'");
            ASTNode* one = (ASTNode*)ast_new_int_literal(1, line, column);
            ASTNode* ident = (ASTNode*)ast_new_identifier(name, line, column);
            ASTNode* expr = (ASTNode*)ast_new_binary_expr(ident, TOKEN_PLUS, one, line, column);
            ASTNode* node = (ASTNode*)ast_new_assign_stmt(name, expr, TOKEN_EQUALS, line, column);
            free(name);
            return node;
        }
        else if (p->current.type == TOKEN_MINUS_MINUS) {
            advance_p(p);
            expect_p(p, TOKEN_SEMICOLON, "missing ';' after '--'");
            ASTNode* one = (ASTNode*)ast_new_int_literal(1, line, column);
            ASTNode* ident = (ASTNode*)ast_new_identifier(name, line, column);
            ASTNode* expr = (ASTNode*)ast_new_binary_expr(ident, TOKEN_MINUS, one, line, column);
            ASTNode* node = (ASTNode*)ast_new_assign_stmt(name, expr, TOKEN_EQUALS, line, column);
            free(name);
            return node;
        }
        else {
            parser_error(p, "expected '=', '+=', '-=', '++', '--', or '(' after identifier");
            free(name);
            return parser_recover(p);
        }
    }
    else if (p->current.type == TOKEN_IF) {
        eat_p(p, TOKEN_IF);
        int line = p->current.line;
        int column = p->current.column;
        expect_p(p, TOKEN_LPAREN, "expected '(' after 'if'");
        ASTNode* condition = parse_expression(p);
        expect_p(p, TOKEN_RPAREN, "missing ')' after if condition");
        ASTNode* then_branch = parse_block(p);

        ASTNode* else_branch = NULL;
        if (p->current.type == TOKEN_ELSE) {
            eat_p(p, TOKEN_ELSE);
            if (p->current.type == TOKEN_IF) {
                else_branch = parse_statement(p);
            } else {
                else_branch = parse_block(p);
            }
        }
        return (ASTNode*)ast_new_if_stmt(condition, then_branch, else_branch, line, column);
    }
    else if (p->current.type == TOKEN_WHILE) {
        eat_p(p, TOKEN_WHILE);
        int line = p->current.line;
        int column = p->current.column;
        expect_p(p, TOKEN_LPAREN, "expected '(' after 'while'");
        ASTNode* condition = parse_expression(p);
        expect_p(p, TOKEN_RPAREN, "missing ')' after while condition");
        ASTNode* body = parse_block(p);
        return (ASTNode*)ast_new_while_stmt(condition, body, line, column);
    }
    else if (p->current.type == TOKEN_FOR) {
        eat_p(p, TOKEN_FOR);
        int line = p->current.line;
        int column = p->current.column;
        expect_p(p, TOKEN_LPAREN, "expected '(' after 'for'");

        ASTNode* initializer = NULL;
        if (p->current.type == TOKEN_LET) {
            eat_p(p, TOKEN_LET);
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected identifier after let in for");
                return parser_recover(p);
            }
            char* v_name = strdup(p->current.value);
            int v_line = p->current.line;
            int v_column = p->current.column;
            eat_p(p, TOKEN_IDENTIFIER);
            /* Sprint 3: optional `: type` annotation in for-let. */
            char* v_type_annotation = NULL;
            if (p->current.type == TOKEN_COLON) {
                eat_p(p, TOKEN_COLON);
                if (p->current.type != TOKEN_IDENTIFIER) {
                    parser_error(p, "expected type name after ':' in for-let declaration");
                    free(v_name);
                    return parser_recover(p);
                }
                v_type_annotation = strdup(p->current.value);
                eat_p(p, TOKEN_IDENTIFIER);
            }
            eat_p(p, TOKEN_EQUALS);
            /* Sprint 1 fix: same "missing initializer" check as the standalone
             * `let` case — applies to `for (let i = ; ...)` as well. */
            if (p->current.type == TOKEN_SEMICOLON) {
                parser_error(p, "missing initializer after '=' in for-let declaration");
                free(v_name);
                free(v_type_annotation);
                return parser_recover(p);
            }
            ASTNode* init_expr = parse_expression(p);
            initializer = (ASTNode*)ast_new_var_decl_typed(v_name, init_expr, v_type_annotation, v_line, v_column);
            free(v_name);
            free(v_type_annotation);
        } else if (p->current.type == TOKEN_IDENTIFIER) {
            char* v_name = strdup(p->current.value);
            int assign_line = p->current.line;
            int assign_column = p->current.column;
            advance_p(p);
            if (p->current.type == TOKEN_EQUALS || p->current.type == TOKEN_PLUS_EQ || p->current.type == TOKEN_MINUS_EQ) {
                LamoTokenType op_type = p->current.type;
                advance_p(p);
                ASTNode* value = parse_expression(p);
                initializer = (ASTNode*)ast_new_assign_stmt(v_name, value, op_type, assign_line, assign_column);
            } else if (p->current.type == TOKEN_PLUS_PLUS) {
                advance_p(p);
                ASTNode* one = (ASTNode*)ast_new_int_literal(1, assign_line, assign_column);
                ASTNode* ident = (ASTNode*)ast_new_identifier(v_name, assign_line, assign_column);
                ASTNode* expr = (ASTNode*)ast_new_binary_expr(ident, TOKEN_PLUS, one, assign_line, assign_column);
                initializer = (ASTNode*)ast_new_assign_stmt(v_name, expr, TOKEN_EQUALS, assign_line, assign_column);
            } else if (p->current.type == TOKEN_MINUS_MINUS) {
                advance_p(p);
                ASTNode* one = (ASTNode*)ast_new_int_literal(1, assign_line, assign_column);
                ASTNode* ident = (ASTNode*)ast_new_identifier(v_name, assign_line, assign_column);
                ASTNode* expr = (ASTNode*)ast_new_binary_expr(ident, TOKEN_MINUS, one, assign_line, assign_column);
                initializer = (ASTNode*)ast_new_assign_stmt(v_name, expr, TOKEN_EQUALS, assign_line, assign_column);
            }
            free(v_name);
        }
        expect_p(p, TOKEN_SEMICOLON, "missing ';' in for loop — expected 'for (init; condition; increment)'");

        ASTNode* condition = parse_expression(p);
        expect_p(p, TOKEN_SEMICOLON, "missing ';' in for loop — expected 'for (init; condition; increment)'");

        ASTNode* increment = NULL;
        if (p->current.type == TOKEN_IDENTIFIER) {
            char* v_name = strdup(p->current.value);
            int inc_line = p->current.line;
            int inc_column = p->current.column;
            advance_p(p);
            if (p->current.type == TOKEN_PLUS_PLUS) {
                advance_p(p);
                ASTNode* one = (ASTNode*)ast_new_int_literal(1, inc_line, inc_column);
                ASTNode* ident = (ASTNode*)ast_new_identifier(v_name, inc_line, inc_column);
                ASTNode* expr = (ASTNode*)ast_new_binary_expr(ident, TOKEN_PLUS, one, inc_line, inc_column);
                increment = (ASTNode*)ast_new_assign_stmt(v_name, expr, TOKEN_EQUALS, inc_line, inc_column);
            } else if (p->current.type == TOKEN_MINUS_MINUS) {
                advance_p(p);
                ASTNode* one = (ASTNode*)ast_new_int_literal(1, inc_line, inc_column);
                ASTNode* ident = (ASTNode*)ast_new_identifier(v_name, inc_line, inc_column);
                ASTNode* expr = (ASTNode*)ast_new_binary_expr(ident, TOKEN_MINUS, one, inc_line, inc_column);
                increment = (ASTNode*)ast_new_assign_stmt(v_name, expr, TOKEN_EQUALS, inc_line, inc_column);
            } else if (p->current.type == TOKEN_PLUS_EQ || p->current.type == TOKEN_MINUS_EQ || p->current.type == TOKEN_EQUALS) {
                LamoTokenType op_type = p->current.type;
                advance_p(p);
                ASTNode* value = parse_expression(p);
                increment = (ASTNode*)ast_new_assign_stmt(v_name, value, op_type, inc_line, inc_column);
            }
            free(v_name);
        }

        expect_p(p, TOKEN_RPAREN, "missing ')' to close for loop header");
        ASTNode* body = parse_block(p);
        return (ASTNode*)ast_new_for_stmt(initializer, condition, increment, body, line, column);
    }
    else if (p->current.type == TOKEN_RETURN) {
        eat_p(p, TOKEN_RETURN);
        int line = p->current.line;
        int column = p->current.column;
        ASTNode* expression = NULL;
        if (p->current.type != TOKEN_SEMICOLON) {
            expression = parse_expression(p);
        }
        expect_p(p, TOKEN_SEMICOLON, "missing ';' after return statement");
        return (ASTNode*)ast_new_return_stmt(expression, line, column);
    }
    else if (p->current.type == TOKEN_BREAK) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_BREAK);
        expect_p(p, TOKEN_SEMICOLON, "missing ';' after break");
        return ast_new_break_stmt(line, column);
    }
    else if (p->current.type == TOKEN_CONTINUE) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_CONTINUE);
        expect_p(p, TOKEN_SEMICOLON, "missing ';' after continue");
        return ast_new_continue_stmt(line, column);
    }
    else if (p->current.type == TOKEN_UNKNOWN) {
        char buf[256];
        snprintf(buf, sizeof(buf), "unexpected token '%s'",
                 p->current.value ? p->current.value : "?");
        parser_error(p, buf);
        return parser_recover(p);
    }
    else if (p->current.type != TOKEN_EOF) {
        char buf[256];
        snprintf(buf, sizeof(buf), "unexpected token %s in statement",
                 token_type_name(p->current.type));
        parser_error(p, buf);
        return parser_recover(p);
    }
    return NULL;
}

ASTProgram* parse_program_v2(Parser* p) {
    ASTProgram* program = ast_new_program();
    ASTNode* head = NULL;
    ASTNode* current = NULL;

    while (p->current.type != TOKEN_EOF) {
        ASTNode* stmt = parse_statement(p);
        if (stmt) {
            if (!head) {
                head = stmt;
                current = stmt;
            } else {
                current->next = stmt;
                current = stmt;
            }
        }
        if (p->panic_mode) {
            parser_synchronize(p);
        }
    }
    program->declarations = head;
    return program;
}
