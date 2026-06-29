#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "parser.h"

struct Parser {
    Lexer* lexer;
    Token current;
    int error_count;
    int panic_mode;   // se 1, suprime próximos erros até sincronizar
};

Parser* parser_init(Lexer* lexer) {
    Parser* p = malloc(sizeof(Parser));
    if (!p) {
        perror("Failed to allocate Parser");
        exit(EXIT_FAILURE);
    }
    p->lexer = lexer;
    p->current = lexer_next_token(lexer);
    p->error_count = 0;
    p->panic_mode = 0;
    return p;
}

void parser_free(Parser* p) {
    if (!p) return;
    token_free(p->current);
    free(p);
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
                return;
            default:
                advance_p(p);
                break;
        }
    }
}

void parser_error(Parser* p, const char* msg) {
    if (p->panic_mode) {
        return; // já estamos em recuperação, ignora erros derivados
    }
    p->panic_mode = 1;
    p->error_count++;
    fprintf(stderr, "\n[Syntax Error] %d:%d: %s\n",
            p->current.line, p->current.column, msg);
    fprintf(stderr, "  found token: %s (%s)\n",
            token_type_name(p->current.type),
            p->current.value ? p->current.value : "<null>");
    // NÃO chama exit(1) — deixa o caller tentar sincronizar e continuar.
}

int parser_had_error(const Parser* p) {
    return p->error_count > 0;
}

int parser_error_count(const Parser* p) {
    return p->error_count;
}

static void eat_p(Parser* p, TokenType type) {
    if (p->current.type == type) {
        advance_p(p);
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "expected %s, got %s",
                 token_type_name(type), token_type_name(p->current.type));
        parser_error(p, buf);
    }
}

ASTNode* parse_expression(Parser* p);

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
                    args = realloc(args, sizeof(ASTNode*) * (arg_count + 1));
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
        TokenType op_type = p->current.type;
        int line = p->current.line;
        int column = p->current.column;
        advance_p(p);
        ASTNode* right = parse_unary(p);
        return (ASTNode*)ast_new_unary_expr(op_type, right, line, column);
    } else {
        return parse_primary(p);
    }
}

static ASTNode* parse_factor(Parser* p) {
    ASTNode* left = parse_unary(p);
    while (p->current.type == TOKEN_STAR || p->current.type == TOKEN_SLASH ||
           p->current.type == TOKEN_PERCENT) {
        TokenType op_type = p->current.type;
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
        TokenType op_type = p->current.type;
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
        TokenType op_type = p->current.type;
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
        TokenType op_type = p->current.type;
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
        TokenType op_type = p->current.type;
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
        TokenType op_type = p->current.type;
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
    eat_p(p, TOKEN_LBRACE);
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
    eat_p(p, TOKEN_RBRACE);
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
        eat_p(p, TOKEN_EQUALS);
        ASTNode* initializer = parse_expression(p);
        eat_p(p, TOKEN_SEMICOLON);
        ASTNode* node = (ASTNode*)ast_new_var_decl(name, initializer, line, column);
        free(name);
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
        eat_p(p, TOKEN_LPAREN);

        char** params = NULL;
        int param_count = 0;

        while (p->current.type != TOKEN_RPAREN && p->current.type != TOKEN_EOF) {
            if (p->current.type != TOKEN_IDENTIFIER) {
                parser_error(p, "expected parameter name");
                break;
            }
            params = realloc(params, sizeof(char*) * (param_count + 1));
            params[param_count] = strdup(p->current.value);
            param_count++;
            eat_p(p, TOKEN_IDENTIFIER);
            if (p->current.type == TOKEN_COMMA) advance_p(p);
        }
        eat_p(p, TOKEN_RPAREN);

        ASTNode* body = parse_block(p);
        ASTNode* node = (ASTNode*)ast_new_fn_decl(name, params, param_count, body, line, column);
        free(name);
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
        eat_p(p, TOKEN_SEMICOLON);
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
                    args = realloc(args, sizeof(ASTNode*) * (arg_count + 1));
                    args[arg_count++] = arg;
                }
                if (p->current.type == TOKEN_COMMA) advance_p(p);
            }
            eat_p(p, TOKEN_RPAREN);
            eat_p(p, TOKEN_SEMICOLON);
            ASTNode* node = (ASTNode*)ast_new_call_stmt(name, args, arg_count, line, column);
            free(name);
            return node;
        }
        else if (p->current.type == TOKEN_EQUALS || p->current.type == TOKEN_PLUS_EQ ||
                 p->current.type == TOKEN_MINUS_EQ) {
            TokenType op_type = p->current.type;
            advance_p(p);
            ASTNode* value = parse_expression(p);
            eat_p(p, TOKEN_SEMICOLON);
            ASTNode* node = (ASTNode*)ast_new_assign_stmt(name, value, op_type, line, column);
            free(name);
            return node;
        } else if (p->current.type == TOKEN_PLUS_PLUS) {
            advance_p(p);
            eat_p(p, TOKEN_SEMICOLON);
            ASTNode* one = (ASTNode*)ast_new_int_literal(1, line, column);
            ASTNode* ident = (ASTNode*)ast_new_identifier(name, line, column);
            ASTNode* expr = (ASTNode*)ast_new_binary_expr(ident, TOKEN_PLUS, one, line, column);
            ASTNode* node = (ASTNode*)ast_new_assign_stmt(name, expr, TOKEN_EQUALS, line, column);
            free(name);
            return node;
        }
        else if (p->current.type == TOKEN_MINUS_MINUS) {
            advance_p(p);
            eat_p(p, TOKEN_SEMICOLON);
            ASTNode* one = (ASTNode*)ast_new_int_literal(1, line, column);
            ASTNode* ident = (ASTNode*)ast_new_identifier(name, line, column);
            ASTNode* expr = (ASTNode*)ast_new_binary_expr(ident, TOKEN_MINUS, one, line, column);
            ASTNode* node = (ASTNode*)ast_new_assign_stmt(name, expr, TOKEN_EQUALS, line, column);
            free(name);
            return node;
        }
        else {
            parser_error(p, "expected assignment operator or function call");
            free(name);
            return parser_recover(p);
        }
    }
    else if (p->current.type == TOKEN_IF) {
        eat_p(p, TOKEN_IF);
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_LPAREN);
        ASTNode* condition = parse_expression(p);
        eat_p(p, TOKEN_RPAREN);
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
        eat_p(p, TOKEN_LPAREN);
        ASTNode* condition = parse_expression(p);
        eat_p(p, TOKEN_RPAREN);
        ASTNode* body = parse_block(p);
        return (ASTNode*)ast_new_while_stmt(condition, body, line, column);
    }
    else if (p->current.type == TOKEN_FOR) {
        eat_p(p, TOKEN_FOR);
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_LPAREN);

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
            eat_p(p, TOKEN_EQUALS);
            ASTNode* init_expr = parse_expression(p);
            initializer = (ASTNode*)ast_new_var_decl(v_name, init_expr, v_line, v_column);
            free(v_name);
        } else if (p->current.type == TOKEN_IDENTIFIER) {
            char* v_name = strdup(p->current.value);
            int assign_line = p->current.line;
            int assign_column = p->current.column;
            advance_p(p);
            if (p->current.type == TOKEN_EQUALS || p->current.type == TOKEN_PLUS_EQ || p->current.type == TOKEN_MINUS_EQ) {
                TokenType op_type = p->current.type;
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
        eat_p(p, TOKEN_SEMICOLON);

        ASTNode* condition = parse_expression(p);
        eat_p(p, TOKEN_SEMICOLON);

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
                TokenType op_type = p->current.type;
                advance_p(p);
                ASTNode* value = parse_expression(p);
                increment = (ASTNode*)ast_new_assign_stmt(v_name, value, op_type, inc_line, inc_column);
            }
            free(v_name);
        }

        eat_p(p, TOKEN_RPAREN);
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
        eat_p(p, TOKEN_SEMICOLON);
        return (ASTNode*)ast_new_return_stmt(expression, line, column);
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
