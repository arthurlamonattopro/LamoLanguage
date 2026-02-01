#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer_v2.h"

// Implementação de strndup para compatibilidade
static char* lamo_strndup(const char* s, size_t n) {
    size_t len = 0;
    while (len < n && s[len] != '\0') len++;
    char* res = malloc(len + 1);
    if (res) {
        memcpy(res, s, len);
        res[len] = '\0';
    }
    return res;
}

const char* token_type_name(TokenType type) {
    static const char* names[] = {
        "LET", "FN", "RETURN", "IF", "ELSE", "WHILE", "FOR", "PRINT",
        "TRUE", "FALSE", "IDENTIFIER", "INT", "STRING",
        "=", "+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=",
        "&&", "||", "!", "+=", "-=", "++", "--",
        "(", ")", "{", "}", "[", "]", ",", ";", ":",
        "EOF", "UNKNOWN"
    };
    return names[type];
}

Lexer* lexer_init(char* source) {
    Lexer* lexer = malloc(sizeof(Lexer));
    if (!lexer) return NULL;
    lexer->source = source;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
    return lexer;
}

void lexer_free(Lexer* lexer) {
    if (lexer) free(lexer);
}

static Token create_token(TokenType type, char* value, int line, int col) {
    Token t;
    t.type = type;
    t.value = value ? strdup(value) : strdup("");
    t.line = line;
    t.column = col;
    return t;
}

void token_free(Token t) {
    if (t.value) free(t.value);
}

static char peek(Lexer* lexer) {
    return lexer->source[lexer->pos];
}

static char peek_next(Lexer* lexer) {
    if (lexer->source[lexer->pos] == '\0') return '\0';
    return lexer->source[lexer->pos + 1];
}

static char advance(Lexer* lexer) {
    char c = lexer->source[lexer->pos++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return c;
}

static void skip_whitespace(Lexer* lexer) {
    while (isspace(peek(lexer))) advance(lexer);
}

static void skip_line_comment(Lexer* lexer) {
    while (peek(lexer) != '\n' && peek(lexer) != '\0') advance(lexer);
}

static void skip_block_comment(Lexer* lexer) {
    advance(lexer); // *
    while (!(peek(lexer) == '*' && peek_next(lexer) == '/') && peek(lexer) != '\0') {
        advance(lexer);
    }
    if (peek(lexer) != '\0') {
        advance(lexer); // *
        advance(lexer); // /
    }
}

static void skip_whitespace_and_comments(Lexer* lexer) {
    while (1) {
        skip_whitespace(lexer);
        if (peek(lexer) == '/' && peek_next(lexer) == '/') {
            advance(lexer); advance(lexer);
            skip_line_comment(lexer);
        } else if (peek(lexer) == '/' && peek_next(lexer) == '*') {
            advance(lexer);
            skip_block_comment(lexer);
        } else {
            break;
        }
    }
}

static Token scan_identifier(Lexer* lexer) {
    int start = lexer->pos;
    int line = lexer->line, col = lexer->column;
    
    while (isalnum(peek(lexer)) || peek(lexer) == '_') advance(lexer);
    
    char* buf = lamo_strndup(lexer->source + start, lexer->pos - start);
    
    // Keywords
    struct { const char* kw; TokenType type; } keywords[] = {
        {"let", TOKEN_LET}, {"fn", TOKEN_FN}, {"return", TOKEN_RETURN},
        {"if", TOKEN_IF}, {"else", TOKEN_ELSE}, {"while", TOKEN_WHILE},
        {"for", TOKEN_FOR}, {"print", TOKEN_PRINT},
        {"true", TOKEN_TRUE}, {"false", TOKEN_FALSE},
        {NULL, TOKEN_UNKNOWN}
    };
    
    for (int i = 0; keywords[i].kw; i++) {
        if (strcmp(buf, keywords[i].kw) == 0) {
            Token t = create_token(keywords[i].type, buf, line, col);
            free(buf);
            return t;
        }
    }
    
    Token t = create_token(TOKEN_IDENTIFIER, buf, line, col);
    free(buf);
    return t;
}

static Token scan_number(Lexer* lexer) {
    int start = lexer->pos;
    int line = lexer->line, col = lexer->column;
    
    while (isdigit(peek(lexer))) advance(lexer);
    
    char* buf = lamo_strndup(lexer->source + start, lexer->pos - start);
    Token t = create_token(TOKEN_INT, buf, line, col);
    free(buf);
    return t;
}

static Token scan_string(Lexer* lexer) {
    int line = lexer->line, col = lexer->column;
    int start = lexer->pos;
    
    while (peek(lexer) != '"' && peek(lexer) != '\0') {
        if (peek(lexer) == '\\' && peek_next(lexer) != '\0') {
            advance(lexer); // escape char
        }
        advance(lexer);
    }
    
    char* buf = lamo_strndup(lexer->source + start, lexer->pos - start);
    if (peek(lexer) == '"') advance(lexer);
    
    Token t = create_token(TOKEN_STRING, buf, line, col);
    free(buf);
    return t;
}

Token lexer_next_token(Lexer* lexer) {
    skip_whitespace_and_comments(lexer);
    
    if (peek(lexer) == '\0') 
        return create_token(TOKEN_EOF, "EOF", lexer->line, lexer->column);
    
    int line = lexer->line, col = lexer->column;
    char c = advance(lexer);
    
    // Identificadores e palavras-chave
    if (isalpha(c) || c == '_') {
        lexer->pos--; lexer->column--;
        return scan_identifier(lexer);
    }
    
    // Números
    if (isdigit(c)) {
        lexer->pos--; lexer->column--;
        return scan_number(lexer);
    }
    
    // Strings
    if (c == '"') return scan_string(lexer);
    
    // Operadores e delimitadores
    switch (c) {
        case '=':
            if (peek(lexer) == '=') { advance(lexer); return create_token(TOKEN_EQ_EQ, "==", line, col); }
            return create_token(TOKEN_EQUALS, "=", line, col);
        case '!':
            if (peek(lexer) == '=') { advance(lexer); return create_token(TOKEN_BANG_EQ, "!=", line, col); }
            return create_token(TOKEN_BANG, "!", line, col);
        case '<':
            if (peek(lexer) == '=') { advance(lexer); return create_token(TOKEN_LT_EQ, "<=", line, col); }
            return create_token(TOKEN_LT, "<", line, col);
        case '>':
            if (peek(lexer) == '=') { advance(lexer); return create_token(TOKEN_GT_EQ, ">=", line, col); }
            return create_token(TOKEN_GT, ">", line, col);
        case '&':
            if (peek(lexer) == '&') { advance(lexer); return create_token(TOKEN_AND_AND, "&&", line, col); }
            break;
        case '|':
            if (peek(lexer) == '|') { advance(lexer); return create_token(TOKEN_OR_OR, "||", line, col); }
            break;
        case '+':
            if (peek(lexer) == '+') { advance(lexer); return create_token(TOKEN_PLUS_PLUS, "++", line, col); }
            if (peek(lexer) == '=') { advance(lexer); return create_token(TOKEN_PLUS_EQ, "+=", line, col); }
            return create_token(TOKEN_PLUS, "+", line, col);
        case '-':
            if (peek(lexer) == '-') { advance(lexer); return create_token(TOKEN_MINUS_MINUS, "--", line, col); }
            if (peek(lexer) == '=') { advance(lexer); return create_token(TOKEN_MINUS_EQ, "-=", line, col); }
            return create_token(TOKEN_MINUS, "-", line, col);
        case '*': return create_token(TOKEN_STAR, "*", line, col);
        case '/': return create_token(TOKEN_SLASH, "/", line, col);
        case '%': return create_token(TOKEN_PERCENT, "%", line, col);
        case '(': return create_token(TOKEN_LPAREN, "(", line, col);
        case ')': return create_token(TOKEN_RPAREN, ")", line, col);
        case '{': return create_token(TOKEN_LBRACE, "{", line, col);
        case '}': return create_token(TOKEN_RBRACE, "}", line, col);
        case '[': return create_token(TOKEN_LBRACKET, "[", line, col);
        case ']': return create_token(TOKEN_RBRACKET, "]", line, col);
        case ',': return create_token(TOKEN_COMMA, ",", line, col);
        case ';': return create_token(TOKEN_SEMICOLON, ";", line, col);
        case ':': return create_token(TOKEN_COLON, ":", line, col);
    }
    
    char err[2] = {c, '\0'};
    return create_token(TOKEN_UNKNOWN, err, line, col);
}
