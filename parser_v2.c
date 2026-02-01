#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer_v2.h"

// === ESTRUTURAS ===

typedef struct {
    char* name;
    int is_string;
} Variable;

typedef struct {
    char* name;
    char** params;
    int param_count;
} Function;

typedef struct {
    Lexer* lexer;
    Token current;
    Variable* vars;
    int var_count;
    Function* funcs;
    int func_count;
    int indent;
} Parser;

// === PARSER INIT/FREE ===

Parser* parser_init(Lexer* lexer) {
    Parser* p = malloc(sizeof(Parser));
    p->lexer = lexer;
    p->current = lexer_next_token(lexer);
    p->vars = NULL;
    p->var_count = 0;
    p->funcs = NULL;
    p->func_count = 0;
    p->indent = 1;
    return p;
}

void parser_free(Parser* p) {
    if (!p) return;
    for (int i = 0; i < p->var_count; i++) free(p->vars[i].name);
    free(p->vars);
    for (int i = 0; i < p->func_count; i++) {
        free(p->funcs[i].name);
        for (int j = 0; j < p->funcs[i].param_count; j++) free(p->funcs[i].params[j]);
        free(p->funcs[i].params);
    }
    free(p->funcs);
    token_free(p->current);
    free(p);
}

// === HELPERS ===

static void print_indent(Parser* p, FILE* out) {
    for (int i = 0; i < p->indent; i++) fprintf(out, "    ");
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

static void register_var(Parser* p, const char* name, int is_string) {
    p->vars = realloc(p->vars, sizeof(Variable) * (p->var_count + 1));
    p->vars[p->var_count].name = strdup(name);
    p->vars[p->var_count].is_string = is_string;
    p->var_count++;
}

static int is_string_var(Parser* p, const char* name) {
    for (int i = 0; i < p->var_count; i++) {
        if (strcmp(p->vars[i].name, name) == 0) return p->vars[i].is_string;
    }
    return 0;
}

static void register_func(Parser* p, const char* name, char** params, int count) {
    p->funcs = realloc(p->funcs, sizeof(Function) * (p->func_count + 1));
    p->funcs[p->func_count].name = strdup(name);
    p->funcs[p->func_count].params = params;
    p->funcs[p->func_count].param_count = count;
    p->func_count++;
}

// === EXPRESSÕES (Precedência correta) ===
// or -> and -> equality -> comparison -> term -> factor -> unary -> primary

void parse_expression(Parser* p, FILE* out);

static void parse_primary(Parser* p, FILE* out) {
    if (p->current.type == TOKEN_INT) {
        fprintf(out, "%s", p->current.value);
        advance_p(p);
    } 
    else if (p->current.type == TOKEN_STRING) {
        fprintf(out, "\"%s\"", p->current.value);
        advance_p(p);
    }
    else if (p->current.type == TOKEN_TRUE) {
        fprintf(out, "1");
        advance_p(p);
    }
    else if (p->current.type == TOKEN_FALSE) {
        fprintf(out, "0");
        advance_p(p);
    }
    else if (p->current.type == TOKEN_IDENTIFIER) {
        char* name = strdup(p->current.value);
        advance_p(p);
        
        // Chamada de função
        if (p->current.type == TOKEN_LPAREN) {
            eat_p(p, TOKEN_LPAREN);
            fprintf(out, "%s(", name);
            int first = 1;
            while (p->current.type != TOKEN_RPAREN && p->current.type != TOKEN_EOF) {
                if (!first) fprintf(out, ", ");
                first = 0;
                parse_expression(p, out);
                if (p->current.type == TOKEN_COMMA) advance_p(p);
            }
            eat_p(p, TOKEN_RPAREN);
            fprintf(out, ")");
        } else {
            fprintf(out, "%s", name);
        }
        free(name);
    }
    else if (p->current.type == TOKEN_LPAREN) {
        eat_p(p, TOKEN_LPAREN);
        fprintf(out, "(");
        parse_expression(p, out);
        fprintf(out, ")");
        eat_p(p, TOKEN_RPAREN);
    }
    else {
        error(p, "Expressão inválida");
    }
}

static void parse_unary(Parser* p, FILE* out) {
    if (p->current.type == TOKEN_BANG || p->current.type == TOKEN_MINUS) {
        char* op = strdup(p->current.value);
        advance_p(p);
        fprintf(out, "%s", op);
        parse_unary(p, out);
        free(op);
    } else {
        parse_primary(p, out);
    }
}

static void parse_factor(Parser* p, FILE* out) {
    parse_unary(p, out);
    while (p->current.type == TOKEN_STAR || p->current.type == TOKEN_SLASH || 
           p->current.type == TOKEN_PERCENT) {
        char* op = strdup(p->current.value);
        advance_p(p);
        fprintf(out, " %s ", op);
        parse_unary(p, out);
        free(op);
    }
}

static void parse_term(Parser* p, FILE* out) {
    parse_factor(p, out);
    while (p->current.type == TOKEN_PLUS || p->current.type == TOKEN_MINUS) {
        char* op = strdup(p->current.value);
        advance_p(p);
        fprintf(out, " %s ", op);
        parse_factor(p, out);
        free(op);
    }
}

static void parse_comparison(Parser* p, FILE* out) {
    parse_term(p, out);
    while (p->current.type == TOKEN_LT || p->current.type == TOKEN_GT ||
           p->current.type == TOKEN_LT_EQ || p->current.type == TOKEN_GT_EQ) {
        char* op = strdup(p->current.value);
        advance_p(p);
        fprintf(out, " %s ", op);
        parse_term(p, out);
        free(op);
    }
}

static void parse_equality(Parser* p, FILE* out) {
    parse_comparison(p, out);
    while (p->current.type == TOKEN_EQ_EQ || p->current.type == TOKEN_BANG_EQ) {
        char* op = strdup(p->current.value);
        advance_p(p);
        fprintf(out, " %s ", op);
        parse_comparison(p, out);
        free(op);
    }
}

static void parse_and(Parser* p, FILE* out) {
    parse_equality(p, out);
    while (p->current.type == TOKEN_AND_AND) {
        advance_p(p);
        fprintf(out, " && ");
        parse_equality(p, out);
    }
}

void parse_expression(Parser* p, FILE* out) {
    parse_and(p, out);
    while (p->current.type == TOKEN_OR_OR) {
        advance_p(p);
        fprintf(out, " || ");
        parse_and(p, out);
    }
}

// === STATEMENTS ===

void parse_statement(Parser* p, FILE* out);

static void parse_block(Parser* p, FILE* out) {
    eat_p(p, TOKEN_LBRACE);
    fprintf(out, "{\n");
    p->indent++;
    while (p->current.type != TOKEN_RBRACE && p->current.type != TOKEN_EOF) {
        parse_statement(p, out);
    }
    p->indent--;
    print_indent(p, out);
    fprintf(out, "}\n");
    eat_p(p, TOKEN_RBRACE);
}

void parse_statement(Parser* p, FILE* out) {
    // let x = valor;
    if (p->current.type == TOKEN_LET) {
        eat_p(p, TOKEN_LET);
        char* name = strdup(p->current.value);
        eat_p(p, TOKEN_IDENTIFIER);
        eat_p(p, TOKEN_EQUALS);
        
        int is_str = (p->current.type == TOKEN_STRING);
        register_var(p, name, is_str);
        
        print_indent(p, out);
        if (is_str) {
            fprintf(out, "const char* %s = ", name);
        } else {
            fprintf(out, "int %s = ", name);
        }
        parse_expression(p, out);
        fprintf(out, ";\n");
        eat_p(p, TOKEN_SEMICOLON);
        free(name);
    }
    // fn nome(params) { ... }
    else if (p->current.type == TOKEN_FN) {
        eat_p(p, TOKEN_FN);
        char* name = strdup(p->current.value);
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
        
        register_func(p, name, params, param_count);
        
        print_indent(p, out);
        fprintf(out, "int %s(", name);
        for (int i = 0; i < param_count; i++) {
            if (i > 0) fprintf(out, ", ");
            fprintf(out, "int %s", params[i]);
        }
        fprintf(out, ") ");
        parse_block(p, out);
        free(name);
    }
    // Atribuição: x = valor; ou x += valor;
    else if (p->current.type == TOKEN_IDENTIFIER) {
        char* name = strdup(p->current.value);
        advance_p(p);
        
        if (p->current.type == TOKEN_LPAREN) {
            // Chamada de função como statement
            eat_p(p, TOKEN_LPAREN);
            print_indent(p, out);
            fprintf(out, "%s(", name);
            int first = 1;
            while (p->current.type != TOKEN_RPAREN && p->current.type != TOKEN_EOF) {
                if (!first) fprintf(out, ", ");
                first = 0;
                parse_expression(p, out);
                if (p->current.type == TOKEN_COMMA) advance_p(p);
            }
            eat_p(p, TOKEN_RPAREN);
            fprintf(out, ");\n");
            eat_p(p, TOKEN_SEMICOLON);
        }
        else if (p->current.type == TOKEN_EQUALS || p->current.type == TOKEN_PLUS_EQ ||
                 p->current.type == TOKEN_MINUS_EQ) {
            char* op = strdup(p->current.value);
            advance_p(p);
            print_indent(p, out);
            fprintf(out, "%s %s ", name, op);
            parse_expression(p, out);
            fprintf(out, ";\n");
            eat_p(p, TOKEN_SEMICOLON);
            free(op);
        }
        else if (p->current.type == TOKEN_PLUS_PLUS) {
            advance_p(p);
            print_indent(p, out);
            fprintf(out, "%s++;\n", name);
            eat_p(p, TOKEN_SEMICOLON);
        }
        else if (p->current.type == TOKEN_MINUS_MINUS) {
            advance_p(p);
            print_indent(p, out);
            fprintf(out, "%s--;\n", name);
            eat_p(p, TOKEN_SEMICOLON);
        }
        else {
            error(p, "Esperado operador de atribuição");
        }
        free(name);
    }
    // print(expr);
    else if (p->current.type == TOKEN_PRINT) {
        eat_p(p, TOKEN_PRINT);
        eat_p(p, TOKEN_LPAREN);
        
        print_indent(p, out);
        
        if (p->current.type == TOKEN_STRING) {
            fprintf(out, "printf(\"%%s\\n\", \"%s\");\n", p->current.value);
            advance_p(p);
        } else if (p->current.type == TOKEN_IDENTIFIER && is_string_var(p, p->current.value)) {
            fprintf(out, "printf(\"%%s\\n\", %s);\n", p->current.value);
            advance_p(p);
        } else {
            fprintf(out, "printf(\"%%d\\n\", ");
            parse_expression(p, out);
            fprintf(out, ");\n");
        }
        
        eat_p(p, TOKEN_RPAREN);
        eat_p(p, TOKEN_SEMICOLON);
    }
    // if (cond) { ... } else { ... }
    else if (p->current.type == TOKEN_IF) {
        eat_p(p, TOKEN_IF);
        eat_p(p, TOKEN_LPAREN);
        print_indent(p, out);
        fprintf(out, "if (");
        parse_expression(p, out);
        fprintf(out, ") ");
        eat_p(p, TOKEN_RPAREN);
        parse_block(p, out);
        
        if (p->current.type == TOKEN_ELSE) {
            eat_p(p, TOKEN_ELSE);
            print_indent(p, out);
            fprintf(out, "else ");
            if (p->current.type == TOKEN_IF) {
                parse_statement(p, out);
            } else {
                parse_block(p, out);
            }
        }
    }
    // while (cond) { ... }
    else if (p->current.type == TOKEN_WHILE) {
        eat_p(p, TOKEN_WHILE);
        eat_p(p, TOKEN_LPAREN);
        print_indent(p, out);
        fprintf(out, "while (");
        parse_expression(p, out);
        fprintf(out, ") ");
        eat_p(p, TOKEN_RPAREN);
        parse_block(p, out);
    }
    // for (init; cond; step) { ... }
    else if (p->current.type == TOKEN_FOR) {
        eat_p(p, TOKEN_FOR);
        eat_p(p, TOKEN_LPAREN);
        print_indent(p, out);
        fprintf(out, "for (");
        
        // init
        if (p->current.type == TOKEN_LET) {
            eat_p(p, TOKEN_LET);
            char* name = strdup(p->current.value);
            eat_p(p, TOKEN_IDENTIFIER);
            eat_p(p, TOKEN_EQUALS);
            register_var(p, name, 0);
            fprintf(out, "int %s = ", name);
            parse_expression(p, out);
            free(name);
        }
        eat_p(p, TOKEN_SEMICOLON);
        fprintf(out, "; ");
        
        // cond
        parse_expression(p, out);
        eat_p(p, TOKEN_SEMICOLON);
        fprintf(out, "; ");
        
        // step
        if (p->current.type == TOKEN_IDENTIFIER) {
            char* name = strdup(p->current.value);
            advance_p(p);
            if (p->current.type == TOKEN_PLUS_PLUS) {
                advance_p(p);
                fprintf(out, "%s++", name);
            } else if (p->current.type == TOKEN_PLUS_EQ) {
                advance_p(p);
                fprintf(out, "%s += ", name);
                parse_expression(p, out);
            } else {
                fprintf(out, "%s", name);
            }
            free(name);
        }
        
        fprintf(out, ") ");
        eat_p(p, TOKEN_RPAREN);
        parse_block(p, out);
    }
    // return expr;
    else if (p->current.type == TOKEN_RETURN) {
        eat_p(p, TOKEN_RETURN);
        print_indent(p, out);
        fprintf(out, "return ");
        parse_expression(p, out);
        fprintf(out, ";\n");
        eat_p(p, TOKEN_SEMICOLON);
    }
    else if (p->current.type != TOKEN_EOF) {
        advance_p(p);
    }
}

// === PROGRAMA PRINCIPAL ===

void parse_program_v2(Parser* p, FILE* out) {
    fprintf(out, "// Código gerado por Lamo v2\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include <string.h>\n\n");
    
    // Primeira passada: coletar funções
    while (p->current.type == TOKEN_FN) {
        parse_statement(p, out);
        fprintf(out, "\n");
    }
    
    fprintf(out, "int main() {\n");
    
    while (p->current.type != TOKEN_EOF) {
        parse_statement(p, out);
    }
    
    print_indent(p, out);
    fprintf(out, "return 0;\n}\n");
}
