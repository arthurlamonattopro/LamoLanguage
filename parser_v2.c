#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer_v2.h"
#include "ast.h"

typedef struct {
    Lexer* lexer;
    Token current;
} Parser;

Parser* parser_init(Lexer* lexer) {
    Parser* p = malloc(sizeof(Parser));
    if (!p) {
        perror("Failed to allocate Parser");
        exit(EXIT_FAILURE);
    }
    p->lexer = lexer;
    p->current = lexer_next_token(lexer);
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

static void error(Parser* p, const char* msg) {
    fprintf(stderr, "\n[Erro] Linha %d, Coluna %d: %s\n", 
            p->current.line, p->current.column, msg);
    fprintf(stderr, "       Token atual: '%s' (%s)\n", 
            p->current.value, token_type_name(p->current.type));
    exit(1);
}

static void eat_p(Parser* p, TokenType type) {
    if (p->current.type == type) {
        advance_p(p);
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "Esperado '%s', encontrado '%s'", 
                 token_type_name(type), p->current.value);
        error(p, buf);
    }
}

ASTNode* parse_expression(Parser* p);

static ASTNode* parse_primary(Parser* p) {
    if (p->current.type == TOKEN_INT) {
        int val = atoi(p->current.value);
        ASTNode* node = (ASTNode*)ast_new_int_literal(val, p->current.line, p->current.column);
        advance_p(p);
        return node;
    } 
    else if (p->current.type == TOKEN_STRING) {
        ASTNode* node = (ASTNode*)ast_new_string_literal(p->current.value, p->current.line, p->current.column);
        advance_p(p);
        return node;
    }
    else if (p->current.type == TOKEN_TRUE) {
        ASTNode* node = (ASTNode*)ast_new_bool_literal(1, p->current.line, p->current.column);
        advance_p(p);
        return node;
    }
    else if (p->current.type == TOKEN_FALSE) {
        ASTNode* node = (ASTNode*)ast_new_bool_literal(0, p->current.line, p->current.column);
        advance_p(p);
        return node;
    }
    else if (p->current.type == TOKEN_INPUT) {
        int line = p->current.line;
        int col = p->current.column;
        advance_p(p);
        eat_p(p, TOKEN_LPAREN);
        ASTNode* prompt = NULL;
        if (p->current.type != TOKEN_RPAREN) {
            prompt = parse_expression(p);
        }
        eat_p(p, TOKEN_RPAREN);
        return ast_new_input_expr(prompt, line, col);
    }
    else if (p->current.type == TOKEN_ISNUMBER) {
        int line = p->current.line;
        int col = p->current.column;
        advance_p(p);
        eat_p(p, TOKEN_LPAREN);
        ASTNode* expr = parse_expression(p);
        eat_p(p, TOKEN_RPAREN);
        return ast_new_isnumber_expr(expr, line, col);
    }
    else if (p->current.type == TOKEN_ISSTRING) {
        int line = p->current.line;
        int col = p->current.column;
        advance_p(p);
        eat_p(p, TOKEN_LPAREN);
        ASTNode* expr = parse_expression(p);
        eat_p(p, TOKEN_RPAREN);
        return ast_new_isstring_expr(expr, line, col);
    }
    else if (p->current.type == TOKEN_EXIT) {
        int line = p->current.line;
        int col = p->current.column;
        advance_p(p);
        eat_p(p, TOKEN_LPAREN);
        ASTNode* code = parse_expression(p);
        eat_p(p, TOKEN_RPAREN);
        return ast_new_exit_stmt(code, line, col);
    }
    else if (p->current.type == TOKEN_ABS) {
        int line = p->current.line;
        int col = p->current.column;
        advance_p(p);
        eat_p(p, TOKEN_LPAREN);
        ASTNode* expr = parse_expression(p);
        eat_p(p, TOKEN_RPAREN);
        return ast_new_abs_expr(expr, line, col);
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
                args = realloc(args, sizeof(ASTNode*) * (arg_count + 1));
                args[arg_count++] = parse_expression(p);
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
        eat_p(p, TOKEN_LPAREN);
        ASTNode* expr = parse_expression(p);
        eat_p(p, TOKEN_RPAREN);
        return (ASTNode*)ast_new_grouping_expr(expr, p->current.line, p->current.column);
    }
    else {
        error(p, "Expressão inválida");
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
    }
    eat_p(p, TOKEN_RBRACE);
    return (ASTNode*)ast_new_block(head, p->current.line, p->current.column);
}

ASTNode* parse_statement(Parser* p) {
    if (p->current.type == TOKEN_LET) {
        eat_p(p, TOKEN_LET);
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
        char* name = strdup(p->current.value);
        int line = p->current.line;
        int column = p->current.column;
        eat_p(p, TOKEN_IDENTIFIER);
        eat_p(p, TOKEN_LPAREN);
        
        char** params = NULL;
        int param_count = 0;
        
        while (p->current.type != TOKEN_RPAREN && p->current.type != TOKEN_EOF) {
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
                args = realloc(args, sizeof(ASTNode*) * (arg_count + 1));
                args[arg_count++] = parse_expression(p);
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
            error(p, "Esperado operador de atribuição ou chamada de função");
            free(name);
            return NULL;
        }
    }
    else if (p->current.type == TOKEN_PRINT) {
        eat_p(p, TOKEN_PRINT);
        eat_p(p, TOKEN_LPAREN);
        ASTNode* expr = parse_expression(p);
        eat_p(p, TOKEN_RPAREN);
        eat_p(p, TOKEN_SEMICOLON);
        return (ASTNode*)ast_new_print_stmt(expr, p->current.line, p->current.column);
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
        ASTNode* expression = parse_expression(p);
        eat_p(p, TOKEN_SEMICOLON);
        return (ASTNode*)ast_new_return_stmt(expression, p->current.line, p->current.column);
    }
    else if (p->current.type != TOKEN_EOF) {
        advance_p(p);
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
    }
    program->declarations = head;
    return program;
}
