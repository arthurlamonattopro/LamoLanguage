#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct Parser Parser;

Parser* parser_init(Lexer* lexer);
void parser_free(Parser* p);
ASTNode* parse_expression(Parser* p);
ASTNode* parse_statement(Parser* p);
ASTProgram* parse_program_v2(Parser* p);
void parser_error(Parser* p, const char* message);

#endif
