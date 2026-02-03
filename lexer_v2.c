#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer_v2.h"

static char* my_strndup(const char* s, size_t n) {
    char* res = malloc(n + 1);
    if (res) {
        memcpy(res, s, n);
        res[n] = '\0';
    }
    return res;
}

Lexer* lexer_init(char* source) {
    Lexer* l = malloc(sizeof(Lexer));
    l->source = source;
    l->pos = 0;
    l->line = 1;
    l->column = 1;
    return l;
}

void lexer_free(Lexer* lexer) {
    free(lexer);
}

static char peek(Lexer* l) {
    return l->source[l->pos];
}

static char advance(Lexer* l) {
    char c = l->source[l->pos++];
    if (c == '\n') {
        l->line++;
        l->column = 1;
    } else {
        l->column++;
    }
    return c;
}

static void skip_whitespace(Lexer* l) {
    while (1) {
        char c = peek(l);
        if (isspace(c)) {
            advance(l);
        } else if (c == '/' && l->source[l->pos + 1] == '/') {
            while (peek(l) != '\n' && peek(l) != '\0') advance(l);
        } else if (c == '/' && l->source[l->pos + 1] == '*') {
            advance(l); advance(l);
            while (!(peek(l) == '*' && l->source[l->pos + 1] == '/') && peek(l) != '\0') advance(l);
            if (peek(l) != '\0') { advance(l); advance(l); }
        } else {
            break;
        }
    }
}

Token lexer_next_token(Lexer* l) {
    skip_whitespace(l);
    
    Token t;
    t.line = l->line;
    t.column = l->column;
    t.value = NULL;

    char c = peek(l);
    if (c == '\0') {
        t.type = TOKEN_EOF;
        t.value = strdup("EOF");
        return t;
    }

    if (isdigit(c)) {
        int start = l->pos;
        while (isdigit(peek(l))) advance(l);
        t.type = TOKEN_INT;
        t.value = my_strndup(&l->source[start], l->pos - start);
        return t;
    }

    if (isalpha(c) || c == '_') {
        int start = l->pos;
        while (isalnum(peek(l)) || peek(l) == '_') advance(l);
        t.value = my_strndup(&l->source[start], l->pos - start);
        
        if (strcmp(t.value, "let") == 0) t.type = TOKEN_LET;
        else if (strcmp(t.value, "fn") == 0) t.type = TOKEN_FN;
        else if (strcmp(t.value, "return") == 0) t.type = TOKEN_RETURN;
        else if (strcmp(t.value, "if") == 0) t.type = TOKEN_IF;
        else if (strcmp(t.value, "else") == 0) t.type = TOKEN_ELSE;
        else if (strcmp(t.value, "while") == 0) t.type = TOKEN_WHILE;
        else if (strcmp(t.value, "for") == 0) t.type = TOKEN_FOR;
        else if (strcmp(t.value, "print") == 0) t.type = TOKEN_PRINT;
        else if (strcmp(t.value, "input") == 0) t.type = TOKEN_INPUT;
        else if (strcmp(t.value, "isnumber") == 0) t.type = TOKEN_ISNUMBER;
        else if (strcmp(t.value, "isstring") == 0) t.type = TOKEN_ISSTRING;
        else if (strcmp(t.value, "exit") == 0) t.type = TOKEN_EXIT;
        else if (strcmp(t.value, "abs") == 0) t.type = TOKEN_ABS;
        else if (strcmp(t.value, "true") == 0) t.type = TOKEN_TRUE;
        else if (strcmp(t.value, "false") == 0) t.type = TOKEN_FALSE;
        else t.type = TOKEN_IDENTIFIER;
        
        return t;
    }

    if (c == '"') {
printf("DEBUG: Found quote at %d\n", l->pos);
        advance(l);
        int start = l->pos;
        while (peek(l) != '\0') {
            if (peek(l) == '\\' && l->source[l->pos + 1] == '"') {
                advance(l); advance(l);
            } else if (peek(l) == '"') {
                break;
            } else {
                advance(l);
            }
        }
        t.type = TOKEN_STRING;
        t.value = my_strndup(&l->source[start], l->pos - start);
        if (peek(l) == '"') advance(l);
        return t;
    }

    advance(l);
    switch (c) {
        case '(': t.type = TOKEN_LPAREN; t.value = strdup("("); break;
        case ')': t.type = TOKEN_RPAREN; t.value = strdup(")"); break;
        case '{': t.type = TOKEN_LBRACE; t.value = strdup("{"); break;
        case '}': t.type = TOKEN_RBRACE; t.value = strdup("}"); break;
        case '[': t.type = TOKEN_LBRACKET; t.value = strdup("["); break;
        case ']': t.type = TOKEN_RBRACKET; t.value = strdup("]"); break;
        case ',': t.type = TOKEN_COMMA; t.value = strdup(","); break;
        case ';': t.type = TOKEN_SEMICOLON; t.value = strdup(";"); break;
        case ':': t.type = TOKEN_COLON; t.value = strdup(":"); break;
        case '+':
            if (peek(l) == '=') { advance(l); t.type = TOKEN_PLUS_EQ; t.value = strdup("+="); }
            else if (peek(l) == '+') { advance(l); t.type = TOKEN_PLUS_PLUS; t.value = strdup("++"); }
            else { t.type = TOKEN_PLUS; t.value = strdup("+"); }
            break;
        case '-':
            if (peek(l) == '=') { advance(l); t.type = TOKEN_MINUS_EQ; t.value = strdup("-="); }
            else if (peek(l) == '-') { advance(l); t.type = TOKEN_MINUS_MINUS; t.value = strdup("--"); }
            else { t.type = TOKEN_MINUS; t.value = strdup("-"); }
            break;
        case '*': t.type = TOKEN_STAR; t.value = strdup("*"); break;
        case '/': t.type = TOKEN_SLASH; t.value = strdup("/"); break;
        case '%': t.type = TOKEN_PERCENT; t.value = strdup("%"); break;
        case '=':
            if (peek(l) == '=') { advance(l); t.type = TOKEN_EQ_EQ; t.value = strdup("=="); }
            else { t.type = TOKEN_EQUALS; t.value = strdup("="); }
            break;
        case '!':
            if (peek(l) == '=') { advance(l); t.type = TOKEN_BANG_EQ; t.value = strdup("!="); }
            else { t.type = TOKEN_BANG; t.value = strdup("!"); }
            break;
        case '<':
            if (peek(l) == '=') { advance(l); t.type = TOKEN_LT_EQ; t.value = strdup("<="); }
            else { t.type = TOKEN_LT; t.value = strdup("<"); }
            break;
        case '>':
            if (peek(l) == '=') { advance(l); t.type = TOKEN_GT_EQ; t.value = strdup(">="); }
            else { t.type = TOKEN_GT; t.value = strdup(">"); }
            break;
        case '&':
            if (peek(l) == '&') { advance(l); t.type = TOKEN_AND_AND; t.value = strdup("&&"); }
            else { t.type = TOKEN_UNKNOWN; t.value = strdup("&"); }
            break;
        case '|':
            if (peek(l) == '|') { advance(l); t.type = TOKEN_OR_OR; t.value = strdup("||"); }
            else { t.type = TOKEN_UNKNOWN; t.value = strdup("|"); }
            break;
        default:
            t.type = TOKEN_UNKNOWN;
            t.value = malloc(2);
            t.value[0] = c;
            t.value[1] = '\0';
            break;
    }
    return t;
}

Token lexer_peek_token(Lexer* l) {
    int pos = l->pos;
    int line = l->line;
    int col = l->column;
    Token t = lexer_next_token(l);
    l->pos = pos;
    l->line = line;
    l->column = col;
    return t;
}

void token_free(Token t) {
    free(t.value);
}

const char* token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_LET: return "let";
        case TOKEN_FN: return "fn";
        case TOKEN_RETURN: return "return";
        case TOKEN_IF: return "if";
        case TOKEN_ELSE: return "else";
        case TOKEN_WHILE: return "while";
        case TOKEN_FOR: return "for";
        case TOKEN_PRINT: return "print";
        case TOKEN_INPUT: return "input";
        case TOKEN_ISNUMBER: return "isnumber";
        case TOKEN_ISSTRING: return "isstring";
        case TOKEN_EXIT: return "exit";
        case TOKEN_ABS: return "abs";
        case TOKEN_TRUE: return "true";
        case TOKEN_FALSE: return "false";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_INT: return "INT";
        case TOKEN_STRING: return "STRING";
        case TOKEN_EQUALS: return "=";
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_PERCENT: return "%";
        case TOKEN_BANG: return "!";
        case TOKEN_LT: return "<";
        case TOKEN_GT: return ">";
        case TOKEN_LPAREN: return "(";
        case TOKEN_RPAREN: return ")";
        case TOKEN_LBRACE: return "{";
        case TOKEN_RBRACE: return "}";
        case TOKEN_LBRACKET: return "[";
        case TOKEN_RBRACKET: return "]";
        case TOKEN_COMMA: return ",";
        case TOKEN_SEMICOLON: return ";";
        case TOKEN_COLON: return ":";
        case TOKEN_EQ_EQ: return "==";
        case TOKEN_BANG_EQ: return "!=";
        case TOKEN_LT_EQ: return "<=";
        case TOKEN_GT_EQ: return ">=";
        case TOKEN_AND_AND: return "&&";
        case TOKEN_OR_OR: return "||";
        case TOKEN_PLUS_EQ: return "+=";
        case TOKEN_MINUS_EQ: return "-=";
        case TOKEN_PLUS_PLUS: return "++";
        case TOKEN_MINUS_MINUS: return "--";
        case TOKEN_EOF: return "EOF";
        default: return "UNKNOWN";
    }
}
