#ifndef PARSER_V2_H
#define PARSER_V2_H

#include "lexer_v2.h"
#include "ast.h"

typedef struct Parser Parser;

Parser* parser_init(Lexer* lexer);
void parser_free(Parser* p);
ASTNode* parse_expression(Parser* p);
ASTNode* parse_statement(Parser* p);
ASTProgram* parse_program_v2(Parser* p);

#endif
