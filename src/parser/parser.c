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
    /* Phase 2: when non-zero, parse_primary does NOT treat `IDENTIFIER {`
     * as a struct literal. Used by the match parser so `match c { ... }`
     * doesn't get misparsed as `match (c { ... })`. The flag is set
     * around the scrutinee expression and cleared afterwards. */
    int no_struct_literal;
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
    p->no_struct_literal = 0;  /* Phase 2: struct literal parsing enabled by default */
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
            case TOKEN_AS:  /* Sprint 4: treat `as` as a sync point too */
            /* Phase 2: struct / impl / enum / match also start new top-level
             * or block-level statements, so the synchronizer should stop at
             * them after an error. */
            case TOKEN_STRUCT:
            case TOKEN_IMPL:
            case TOKEN_ENUM:
            case TOKEN_MATCH:
                return;
            default:
                advance_p(p);
                break;
        }
    }
}

void parser_error(Parser* p, const char* msg) {
    /* Delegate to the hinted variant with no hint. */
    parser_error_with_hint(p, msg, NULL);
}

/* Sprint 4: parser_error with an optional hint. The hint is printed
 * below the source snippet, formatted as "hint: <text>". Pass NULL
 * when no hint is appropriate. */
void parser_error_with_hint(Parser* p, const char* msg, const char* hint) {
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
    /* Sprint 4: color the "error" label red+bold when stderr is a TTY. */
    if (lamo_error_use_color()) {
        fprintf(stderr, "\n%s:%d:%d: %s[Syntax Error]%s %s\n",
                label, p->current.line, p->current.column,
                LAMO_COLOR_BOLD LAMO_COLOR_RED, LAMO_COLOR_RESET, msg);
    } else {
        fprintf(stderr, "\n%s:%d:%d: [Syntax Error] %s\n",
                label, p->current.line, p->current.column, msg);
    }
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
    /* Sprint 4: print the hint below the snippet, if any. */
    error_print_hint(stderr, hint);
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

/* Phase 2: consume an optional semicolon. The spec shows method bodies
 * and top-level programs without semicolons; we accept both styles.
 * If the next token is `;`, consume it. Otherwise, leave it alone (the
 * next statement-parsing iteration will handle it). This is safe because
 * Lamo's grammar is unambiguous without `;` in practice — every
 * statement starts with a keyword (let, fn, if, while, for, return,
 * break, continue, import, struct, impl, enum, match) or an identifier
 * followed by `=`, `(`, `.`, `[`, `++`, `--`, none of which can extend
 * the previous expression. */
static void optional_semicolon(Parser* p) {
    if (p->current.type == TOKEN_SEMICOLON) {
        advance_p(p);
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
static ASTNode* parser_recover(Parser* p);

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
            int prop_line = p->current.line;
            int prop_column = p->current.column;
            advance_p(p);
            /* Phase 2: if the next token is '(', this is a method call
             * (expr.method(args)) - parse the argument list and build an
             * AST_MEMBER_CALL. Otherwise it's a property access
             * (expr.prop) - build an AST_PROP_EXPR. The semantic pass
             * dispatches AST_MEMBER_CALL based on the object's inferred
             * type: module alias / array / struct. */
            if (p->current.type == TOKEN_LPAREN) {
                advance_p(p);  /* consume '(' */
                ASTNode** args = NULL;
                int arg_count = 0;
                while (p->current.type != TOKEN_RPAREN && p->current.type != TOKEN_EOF) {
                    ASTNode* arg = parse_expression(p);
                    if (arg) {
                        ASTNode** resized = realloc(args, sizeof(ASTNode*) * (size_t)(arg_count + 1));
                        if (!resized) {
                            parser_error(p, "out of memory while growing argument list");
                            ast_free(arg);
                            for (int i = 0; i < arg_count; i++) ast_free(args[i]);
                            free(args);
                            free(prop_name);
                            ast_free(node);
                            return parser_recover(p);
                        }
                        args = resized;
                        args[arg_count++] = arg;
                    }
                    if (p->current.type == TOKEN_COMMA) advance_p(p);
                }
                expect_p(p, TOKEN_RPAREN, "missing ')' after method call arguments");
                node = (ASTNode*)ast_new_member_call(node, prop_name, args, arg_count, prop_line, prop_column);
                free(prop_name);
            } else {
                node = (ASTNode*)ast_new_prop_expr(node, prop_name, line, column);
                free(prop_name);
            }
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

        /* Sprint 4: `module.member(args)` in expression position. We
         * recognize this BEFORE the regular call-expression branch so the
         * `.` doesn't fall through to parse_postfix (which would wrap it
         * as a property expression and lose the call). The resulting
         * AST_MEMBER_CALL is resolved by the semantic pass against the
         * module registry. */
        if (p->current.type == TOKEN_DOT) {
            int obj_line = p->current.line;
            int obj_column = p->current.column;
            advance_p(p);  /* consume '.' */
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected member name after '.' in module call expression");
                free(name);
                return parser_recover(p);
            }
            char* member_name = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);
            /* Only build a member CALL when followed by '('. Otherwise
             * fall back to a property expression (AST_PROP_EXPR) so the
             * existing `.len` and future `.prop` forms keep working. The
             * conversion is done by building the prop_expr here and letting
             * parse_postfix continue wrapping if there are more dots. */
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
                            free(member_name);
                            return parser_recover(p);
                        }
                        args = resized;
                        args[arg_count++] = arg;
                    }
                    if (p->current.type == TOKEN_COMMA) advance_p(p);
                }
                eat_p(p, TOKEN_RPAREN);
                ASTNode* obj = (ASTNode*)ast_new_identifier(name, obj_line, obj_column);
                ASTNode* node = (ASTNode*)ast_new_member_call(obj, member_name, args, arg_count, line, column);
                free(name);
                free(member_name);
                return node;
            } else {
                /* `module.member` without a call — build a prop_expr so
                 * parse_postfix can keep wrapping further `.x.y.z` chains.
                 * The semantic pass resolves whether `object` is a module
                 * alias or a regular value (e.g. array `.len`). */
                ASTNode* obj = (ASTNode*)ast_new_identifier(name, obj_line, obj_column);
                ASTNode* node = (ASTNode*)ast_new_prop_expr(obj, member_name, line, column);
                free(name);
                free(member_name);
                return node;
            }
        }

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
        } else if (p->current.type == TOKEN_LBRACE && !p->no_struct_literal) {
            /* Phase 2: struct literal — `Name { field: value, ... }`.
             * We treat `IDENTIFIER {` in expression position as a struct
             * literal. The semantic pass rejects it if `Name` is not a
             * declared struct type. Field order does NOT need to match
             * the struct declaration order — we look up each field name
             * at compile time.
             *
             * Why this is safe: Lamo has no block expressions, so a `{`
             * after an identifier in expression position is unambiguously
             * a struct literal. Statements like `if (cond) { ... }` work
             * because the `(cond)` is parenthesized — after parse_primary
             * returns the cond, the next token is `)` (not `{`), so this
             * branch doesn't fire. */
            advance_p(p);  /* consume '{' */
            char** field_names = NULL;
            ASTNode** field_values = NULL;
            int field_count = 0;
            while (p->current.type != TOKEN_RBRACE && p->current.type != TOKEN_EOF) {
                if (p->current.type != TOKEN_IDENTIFIER) {
                    parser_error(p, "expected field name in struct literal");
                    for (int i = 0; i < field_count; i++) { free(field_names[i]); ast_free(field_values[i]); }
                    free(field_names);
                    free(field_values);
                    free(name);
                    return parser_recover(p);
                }
                char* fname = strdup(p->current.value);
                advance_p(p);
                expect_p(p, TOKEN_COLON, "expected ':' after field name in struct literal");
                ASTNode* fval = parse_expression(p);
                /* Grow arrays. Realloc one at a time and assign back
                 * immediately, so each variable always holds a valid
                 * pointer. This avoids -Wuse-after-free: after a
                 * successful realloc, the old pointer may be invalid. */
                {
                    char** fn_resized = realloc(field_names, sizeof(char*) * (size_t)(field_count + 1));
                    if (!fn_resized) {
                        parser_error(p, "out of memory while growing struct literal field list");
                        free(fname);
                        ast_free(fval);
                        for (int i = 0; i < field_count; i++) { free(field_names[i]); ast_free(field_values[i]); }
                        free(field_names);
                        free(field_values);
                        free(name);
                        return parser_recover(p);
                    }
                    field_names = fn_resized;
                }
                {
                    ASTNode** fv_resized = realloc(field_values, sizeof(ASTNode*) * (size_t)(field_count + 1));
                    if (!fv_resized) {
                        parser_error(p, "out of memory while growing struct literal field list");
                        free(fname);
                        ast_free(fval);
                        for (int i = 0; i < field_count; i++) { free(field_names[i]); ast_free(field_values[i]); }
                        free(field_names);
                        free(field_values);
                        free(name);
                        return parser_recover(p);
                    }
                    field_values = fv_resized;
                }
                field_names[field_count] = fname;
                field_values[field_count] = fval;
                field_count++;
                if (p->current.type == TOKEN_COMMA) advance_p(p);
                else if (p->current.type == TOKEN_SEMICOLON) advance_p(p);
            }
            expect_p(p, TOKEN_RBRACE, "missing '}' at end of struct literal");
            ASTNode* node = (ASTNode*)ast_new_struct_literal(name, field_names, field_values, field_count, line, column);
            /* Free field_names array contents (the AST strdup'd them) and
             * the field_values array (the AST took ownership of the elements).
             * Free the arrays themselves (the AST made its own copies). */
            for (int i = 0; i < field_count; i++) free(field_names[i]);
            free(field_names);
            free(field_values);
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
         * The explicit message tells them exactly what is wrong.
         * Sprint 4: also emit a hint suggesting the most likely fix. */
        if (p->current.type == TOKEN_SEMICOLON) {
            parser_error_with_hint(p,
                "missing initializer after '=' in let declaration",
                "did you forget a value after '='? e.g. `let x = 0;`");
            free(name);
            free(type_annotation);
            return parser_recover(p);
        }
        ASTNode* initializer = parse_expression(p);
        /* Phase 2: semicolon is optional (spec shows code without `;`). */
        optional_semicolon(p);
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
        /* Phase 2: accept either a STRING path (legacy) or an IDENTIFIER
         * (bare module name like `import math`). For the bare form, we
         * synthesize the path `<name>.lamo` and (if no `as` clause is
         * given) use the name itself as the alias. This matches the
         * spec syntax:
         *   import math        // -> import "math.lamo" as math
         *   import math as m   // -> import "math.lamo" as m
         * The loader (lamo_v2.c::resolve_import_path) resolves the path
         * relative to the importing file. */
        char* path = NULL;
        char* alias = NULL;
        if (p->current.type == TOKEN_STRING) {
            path = strdup(p->current.value);
            eat_p(p, TOKEN_STRING);
        } else if (p->current.type == TOKEN_IDENTIFIER) {
            /* Phase 3 (stdlib): accept dotted module names like `import std.io`.
             * We accumulate the dotted path into `<a>/<b>/<c>.lamo` and use
             * the LAST component as the default alias (so `import std.io`
             * becomes `import "std/io.lamo" as io`).
             *
             * This is the syntax the standard library uses to expose itself
             * without colliding with user files: a top-level `std/` directory
             * holds the standard modules, and the loader knows to look there
             * when the path starts with `std/`. */
            char* first = strdup(p->current.value);
            char* last_segment = strdup(p->current.value);
            size_t total_len = strlen(first);
            eat_p(p, TOKEN_IDENTIFIER);
            while (p->current.type == TOKEN_DOT) {
                eat_p(p, TOKEN_DOT);
                if (p->current.type != TOKEN_IDENTIFIER) {
                    parser_error(p, "expected identifier after '.' in import path");
                    free(first); free(last_segment);
                    return parser_recover(p);
                }
                /* Append "/<next>" to first (which accumulates the path). */
                {
                    size_t seg_len = strlen(p->current.value);
                    char* grown = (char*)realloc(first, total_len + 1 + seg_len + 1);
                    if (!grown) { free(first); free(last_segment); parser_error(p, "out of memory in import path"); return parser_recover(p); }
                    first = grown;
                    first[total_len] = '/';
                    memcpy(first + total_len + 1, p->current.value, seg_len + 1);
                    total_len += 1 + seg_len;
                }
                free(last_segment);
                last_segment = strdup(p->current.value);
                eat_p(p, TOKEN_IDENTIFIER);
            }
            /* Synthesize "<path>.lamo". */
            size_t plen = total_len + 6;
            path = malloc(plen);
            snprintf(path, plen, "%s.lamo", first);
            /* Default alias is the last segment. */
            alias = last_segment;
            free(first);
        } else {
            parser_error(p, "expected string path or module name after import");
            return parser_recover(p);
        }
        /* Optional `as IDENTIFIER` clause. Overrides the default alias. */
        if (p->current.type == TOKEN_AS) {
            eat_p(p, TOKEN_AS);
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected identifier after 'as' in import");
                free(path);
                free(alias);
                return parser_recover(p);
            }
            free(alias);  /* free the default alias (if any) */
            alias = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);
        }
        optional_semicolon(p);  /* Phase 2: `;` optional */
        ASTNode* node = (ASTNode*)ast_new_import_decl_aliased(path, alias, line, column);
        free(path);
        free(alias);
        return node;
    }
    /* ─── Phase 2: struct / impl / enum / match declarations ─────────── */
    else if (p->current.type == TOKEN_STRUCT) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_STRUCT);
        if (p->current.type != TOKEN_IDENTIFIER) {
            parser_error(p, "expected struct name after 'struct'");
            return parser_recover(p);
        }
        char* name = strdup(p->current.value);
        eat_p(p, TOKEN_IDENTIFIER);
        expect_p(p, TOKEN_LBRACE, "expected '{' to open struct body");
        char** field_names = NULL;
        char** field_types = NULL;
        int field_count = 0;
        while (p->current.type != TOKEN_RBRACE && p->current.type != TOKEN_EOF) {
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected field name in struct body");
                for (int i = 0; i < field_count; i++) { free(field_names[i]); free(field_types[i]); }
                free(field_names); free(field_types); free(name);
                return parser_recover(p);
            }
            char* fname = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);
            expect_p(p, TOKEN_COLON, "expected ':' after field name in struct");
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected type name after ':' in struct field");
                free(fname); free(name);
                for (int i = 0; i < field_count; i++) { free(field_names[i]); free(field_types[i]); }
                free(field_names); free(field_types);
                return parser_recover(p);
            }
            char* ftype = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);
            /* Grow both arrays. Realloc one at a time and assign back
             * immediately to avoid -Wuse-after-free. */
            {
                char** fn_r = realloc(field_names, sizeof(char*) * (size_t)(field_count + 1));
                if (!fn_r) {
                    parser_error(p, "out of memory while growing struct field list");
                    free(fname); free(ftype); free(name);
                    for (int i = 0; i < field_count; i++) { free(field_names[i]); free(field_types[i]); }
                    free(field_names); free(field_types);
                    return parser_recover(p);
                }
                field_names = fn_r;
            }
            {
                char** ft_r = realloc(field_types, sizeof(char*) * (size_t)(field_count + 1));
                if (!ft_r) {
                    parser_error(p, "out of memory while growing struct field list");
                    free(fname); free(ftype); free(name);
                    for (int i = 0; i < field_count; i++) { free(field_names[i]); free(field_types[i]); }
                    free(field_names); free(field_types);
                    return parser_recover(p);
                }
                field_types = ft_r;
            }
            field_names[field_count] = fname;
            field_types[field_count] = ftype;
            field_count++;
            /* Fields can be separated by `,` or `;` or just newlines
             * (the spec example shows no separators, just one field per
             * line). We accept all three for flexibility. */
            if (p->current.type == TOKEN_COMMA) advance_p(p);
            else if (p->current.type == TOKEN_SEMICOLON) advance_p(p);
        }
        expect_p(p, TOKEN_RBRACE, "missing '}' at end of struct body");
        /* Optional trailing semicolon (struct decls usually don't have one,
         * but we allow it for symmetry with other declarations). */
        if (p->current.type == TOKEN_SEMICOLON) advance_p(p);
        ASTNode* node = (ASTNode*)ast_new_struct_decl(name, field_names, field_types, field_count, line, column);
        for (int i = 0; i < field_count; i++) { free(field_names[i]); free(field_types[i]); }
        free(field_names); free(field_types); free(name);
        return node;
    }
    else if (p->current.type == TOKEN_IMPL) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_IMPL);
        if (p->current.type != TOKEN_IDENTIFIER) {
            parser_error(p, "expected struct name after 'impl'");
            return parser_recover(p);
        }
        char* struct_name = strdup(p->current.value);
        eat_p(p, TOKEN_IDENTIFIER);
        expect_p(p, TOKEN_LBRACE, "expected '{' to open impl body");
        /* Parse a sequence of `fn ...` declarations as the methods. */
        ASTNode* head = NULL;
        ASTNode* tail = NULL;
        while (p->current.type != TOKEN_RBRACE && p->current.type != TOKEN_EOF) {
            /* Reuse parse_statement's TOKEN_FN handling by calling it
             * directly. parse_statement returns AST_FN_DECL nodes for
             * `fn name(params) { body }`. We chain them via ->next. */
            if (p->current.type != TOKEN_FN) {
                parser_error(p, "expected 'fn' inside impl block");
                parser_synchronize(p);
                /* Try to recover by skipping to next 'fn' or '}'. */
                while (p->current.type != TOKEN_FN && p->current.type != TOKEN_RBRACE && p->current.type != TOKEN_EOF) {
                    advance_p(p);
                }
                continue;
            }
            ASTNode* method = parse_statement(p);
            if (method) {
                if (!head) { head = method; tail = method; }
                else { tail->next = method; tail = method; }
            }
            if (p->panic_mode) parser_synchronize(p);
        }
        expect_p(p, TOKEN_RBRACE, "missing '}' at end of impl body");
        if (p->current.type == TOKEN_SEMICOLON) advance_p(p);
        ASTNode* node = (ASTNode*)ast_new_impl_decl(struct_name, head, line, column);
        free(struct_name);
        return node;
    }
    else if (p->current.type == TOKEN_ENUM) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_ENUM);
        if (p->current.type != TOKEN_IDENTIFIER) {
            parser_error(p, "expected enum name after 'enum'");
            return parser_recover(p);
        }
        char* name = strdup(p->current.value);
        eat_p(p, TOKEN_IDENTIFIER);
        expect_p(p, TOKEN_LBRACE, "expected '{' to open enum body");
        char** variants = NULL;
        int variant_count = 0;
        while (p->current.type != TOKEN_RBRACE && p->current.type != TOKEN_EOF) {
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected variant name in enum body");
                for (int i = 0; i < variant_count; i++) free(variants[i]);
                free(variants); free(name);
                return parser_recover(p);
            }
            char* vname = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);
            {
                char** resized = realloc(variants, sizeof(char*) * (size_t)(variant_count + 1));
                if (!resized) {
                    parser_error(p, "out of memory while growing enum variant list");
                    free(vname);
                    for (int i = 0; i < variant_count; i++) free(variants[i]);
                    free(variants); free(name);
                    return parser_recover(p);
                }
                variants = resized;
                variants[variant_count++] = vname;
            }
            if (p->current.type == TOKEN_COMMA) advance_p(p);
            else if (p->current.type == TOKEN_SEMICOLON) advance_p(p);
        }
        expect_p(p, TOKEN_RBRACE, "missing '}' at end of enum body");
        if (p->current.type == TOKEN_SEMICOLON) advance_p(p);
        ASTNode* node = (ASTNode*)ast_new_enum_decl(name, variants, variant_count, line, column);
        for (int i = 0; i < variant_count; i++) free(variants[i]);
        free(variants); free(name);
        return node;
    }
    else if (p->current.type == TOKEN_MATCH) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_MATCH);
        /* Phase 2: disable struct literal parsing while parsing the
         * scrutinee so `match c { ... }` doesn't get misparsed as
         * `match (c { ... })` (a struct literal swallowing the match
         * body). The flag is cleared after the scrutinee is parsed. */
        p->no_struct_literal = 1;
        ASTNode* scrutinee = parse_expression(p);
        p->no_struct_literal = 0;
        expect_p(p, TOKEN_LBRACE, "expected '{' to open match body");
        char** patterns = NULL;
        int* pattern_is_wildcard = NULL;
        ASTNode** bodies = NULL;
        int arm_count = 0;
        while (p->current.type != TOKEN_RBRACE && p->current.type != TOKEN_EOF) {
            char* pat = NULL;
            int is_wild = 0;
            if (p->current.type == TOKEN_IDENTIFIER) {
                /* "_" is the wildcard pattern (we read it as an identifier
                 * since the lexer doesn't have a special token for it). */
                if (strcmp(p->current.value, "_") == 0) {
                    is_wild = 1;
                    pat = strdup("_");
                } else {
                    pat = strdup(p->current.value);
                }
                eat_p(p, TOKEN_IDENTIFIER);
            } else {
                parser_error(p, "expected pattern (variant name or '_') in match arm");
                free(pat);
                for (int i = 0; i < arm_count; i++) { free(patterns[i]); ast_free(bodies[i]); }
                free(patterns); free(pattern_is_wildcard); free(bodies);
                ast_free(scrutinee);
                return parser_recover(p);
            }
            expect_p(p, TOKEN_FAT_ARROW, "expected '=>' in match arm");
            /* Arm body: parse a single statement. We use parse_statement
             * so the user can write `Red => print("red");` or
             * `Red => { print("red"); print("!"); }`. */
            ASTNode* body = parse_statement(p);
            /* Grow arrays. Realloc one at a time and assign back
             * immediately to avoid -Wuse-after-free. */
            {
                char** p_r = realloc(patterns, sizeof(char*) * (size_t)(arm_count + 1));
                if (!p_r) {
                    parser_error(p, "out of memory while growing match arm list");
                    free(pat); ast_free(body);
                    for (int i = 0; i < arm_count; i++) { free(patterns[i]); ast_free(bodies[i]); }
                    free(patterns); free(pattern_is_wildcard); free(bodies);
                    ast_free(scrutinee);
                    return parser_recover(p);
                }
                patterns = p_r;
            }
            {
                int* w_r = realloc(pattern_is_wildcard, sizeof(int) * (size_t)(arm_count + 1));
                if (!w_r) {
                    parser_error(p, "out of memory while growing match arm list");
                    free(pat); ast_free(body);
                    for (int i = 0; i < arm_count; i++) { free(patterns[i]); ast_free(bodies[i]); }
                    free(patterns); free(pattern_is_wildcard); free(bodies);
                    ast_free(scrutinee);
                    return parser_recover(p);
                }
                pattern_is_wildcard = w_r;
            }
            {
                ASTNode** b_r = realloc(bodies, sizeof(ASTNode*) * (size_t)(arm_count + 1));
                if (!b_r) {
                    parser_error(p, "out of memory while growing match arm list");
                    free(pat); ast_free(body);
                    for (int i = 0; i < arm_count; i++) { free(patterns[i]); ast_free(bodies[i]); }
                    free(patterns); free(pattern_is_wildcard); free(bodies);
                    ast_free(scrutinee);
                    return parser_recover(p);
                }
                bodies = b_r;
            }
            patterns[arm_count] = pat;
            pattern_is_wildcard[arm_count] = is_wild;
            bodies[arm_count] = body;
            arm_count++;
            if (p->current.type == TOKEN_COMMA) advance_p(p);
        }
        expect_p(p, TOKEN_RBRACE, "missing '}' at end of match body");
        if (p->current.type == TOKEN_SEMICOLON) advance_p(p);
        ASTNode* node = (ASTNode*)ast_new_match_stmt(scrutinee, patterns, pattern_is_wildcard, bodies, arm_count, line, column);
        for (int i = 0; i < arm_count; i++) free(patterns[i]);
        free(patterns); free(pattern_is_wildcard); free(bodies);
        return node;
    }
    else if (p->current.type == TOKEN_IDENTIFIER) {
        char* name = strdup(p->current.value);
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);

        /* Sprint 4: `module.member(args);` — module member call statement.
         * We handle this BEFORE the regular call-statement branch because
         * the `.` would otherwise fall through to the "expected '=', '+='..."
         * error. The object is always an AST_IDENTIFIER naming the module
         * alias; the semantic pass validates that the alias was declared
         * by an earlier `import "..." as alias;`.
         *
         * Phase 2: we also accept `obj.field = value;` here (field
         * assignment). The disambiguation is: after consuming `.IDENTIFIER`,
         * if the next token is `(`, parse as method call; if it's `=`,
         * `+=`, or `-=`, parse as field assignment. */
        if (p->current.type == TOKEN_DOT) {
            int obj_line = p->current.line;
            int obj_column = p->current.column;
            advance_p(p);  /* consume '.' */
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected member name after '.' in module call");
                free(name);
                return parser_recover(p);
            }
            char* member_name = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);
            if (p->current.type == TOKEN_LPAREN) {
                /* Method call: `obj.method(args);` */
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
                            free(member_name);
                            return parser_recover(p);
                        }
                        args = resized;
                        args[arg_count++] = arg;
                    }
                    if (p->current.type == TOKEN_COMMA) advance_p(p);
                }
                expect_p(p, TOKEN_RPAREN, "missing ')' — did you forget to close the argument list?");
                optional_semicolon(p);  /* Phase 2: `;` optional */
                ASTNode* obj = (ASTNode*)ast_new_identifier(name, obj_line, obj_column);
                ASTNode* node = (ASTNode*)ast_new_member_call(obj, member_name, args, arg_count, line, column);
                free(name);
                free(member_name);
                return node;
            } else if (p->current.type == TOKEN_EQUALS || p->current.type == TOKEN_PLUS_EQ ||
                       p->current.type == TOKEN_MINUS_EQ) {
                /* Phase 2: field assignment `obj.field = value;` */
                LamoTokenType op_type = p->current.type;
                advance_p(p);
                ASTNode* value = parse_expression(p);
                /* Semicolon is optional — the spec shows method bodies
                 * without semicolons. We accept both `;` and no-`;` (e.g.
                 * when the next token is `}`). */
                if (p->current.type == TOKEN_SEMICOLON) advance_p(p);
                ASTNode* obj = (ASTNode*)ast_new_identifier(name, obj_line, obj_column);
                ASTNode* target = (ASTNode*)ast_new_prop_expr(obj, member_name, line, column);
                ASTNode* node = (ASTNode*)ast_new_place_assign_stmt(target, value, op_type, line, column);
                free(name);
                free(member_name);
                return node;
            } else {
                parser_error(p, "expected '(' or '=' after '.member' in statement");
                free(name);
                free(member_name);
                return parser_recover(p);
            }
        }

        /* Phase 2: `arr[index] = value;` — index assignment. We detect
         * this when the identifier is followed by `[`. The expression
         * form `arr[index]` (read) is handled by parse_postfix; here we
         * specifically handle the assignment form. After consuming the
         * `[index]`, we look for `=`, `+=`, or `-=`. */
        if (p->current.type == TOKEN_LBRACKET) {
            int idx_line = p->current.line;
            int idx_column = p->current.column;
            advance_p(p);  /* consume '[' */
            ASTNode* index = parse_expression(p);
            expect_p(p, TOKEN_RBRACKET, "missing ']' after index in assignment");
            if (p->current.type == TOKEN_EQUALS || p->current.type == TOKEN_PLUS_EQ ||
                p->current.type == TOKEN_MINUS_EQ) {
                LamoTokenType op_type = p->current.type;
                advance_p(p);
                ASTNode* value = parse_expression(p);
                optional_semicolon(p);  /* Phase 2: `;` optional */
                ASTNode* obj = (ASTNode*)ast_new_identifier(name, idx_line, idx_column);
                ASTNode* target = (ASTNode*)ast_new_index_expr(obj, index, line, column);
                ASTNode* node = (ASTNode*)ast_new_place_assign_stmt(target, value, op_type, line, column);
                free(name);
                return node;
            } else {
                parser_error(p, "expected '=', '+=', or '-=' after 'arr[index]' in statement");
                ast_free(index);
                free(name);
                return parser_recover(p);
            }
        }

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
            optional_semicolon(p);  /* Phase 2: `;` optional */
            ASTNode* node = (ASTNode*)ast_new_call_stmt(name, args, arg_count, line, column);
            free(name);
            return node;
        }
        else if (p->current.type == TOKEN_EQUALS || p->current.type == TOKEN_PLUS_EQ ||
                 p->current.type == TOKEN_MINUS_EQ) {
            LamoTokenType op_type = p->current.type;
            advance_p(p);
            ASTNode* value = parse_expression(p);
            optional_semicolon(p);  /* Phase 2: `;` optional */
            ASTNode* node = (ASTNode*)ast_new_assign_stmt(name, value, op_type, line, column);
            free(name);
            return node;
        } else if (p->current.type == TOKEN_PLUS_PLUS) {
            advance_p(p);
            optional_semicolon(p);  /* Phase 2: `;` optional */
            ASTNode* one = (ASTNode*)ast_new_int_literal(1, line, column);
            ASTNode* ident = (ASTNode*)ast_new_identifier(name, line, column);
            ASTNode* expr = (ASTNode*)ast_new_binary_expr(ident, TOKEN_PLUS, one, line, column);
            ASTNode* node = (ASTNode*)ast_new_assign_stmt(name, expr, TOKEN_EQUALS, line, column);
            free(name);
            return node;
        }
        else if (p->current.type == TOKEN_MINUS_MINUS) {
            advance_p(p);
            optional_semicolon(p);  /* Phase 2: `;` optional */
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
             * `let` case — applies to `for (let i = ; ...)` as well.
             * Sprint 4: same hint as the let case. */
            if (p->current.type == TOKEN_SEMICOLON) {
                parser_error_with_hint(p,
                    "missing initializer after '=' in for-let declaration",
                    "did you forget a value after '='? e.g. `for (let i = 0; i < n; i++) { ... }`");
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
        optional_semicolon(p);  /* Phase 2: `;` optional */
        return (ASTNode*)ast_new_return_stmt(expression, line, column);
    }
    else if (p->current.type == TOKEN_LBRACE) {
        /* Phase 2: a bare block `{ ... }` as a statement. This is used
         * by match arm bodies like `Red => { print("red"); print("!"); }`.
         * We delegate to parse_block which handles the braces and the
         * inner statement list. */
        return parse_block(p);
    }
    else if (p->current.type == TOKEN_BREAK) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_BREAK);
        optional_semicolon(p);  /* Phase 2: `;` optional */
        return ast_new_break_stmt(line, column);
    }
    else if (p->current.type == TOKEN_CONTINUE) {
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_CONTINUE);
        optional_semicolon(p);  /* Phase 2: `;` optional */
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
