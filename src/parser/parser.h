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

// Registra um erro sintático sem chamar exit(). O parser tenta se recuperar
// sincronizando no próximo ; ou } e continua o parse. Use parser_had_error()
// depois de parse_program_v2 para saber se algum erro ocorreu.
void parser_error(Parser* p, const char* message);

// Retorna 1 se o parser registrou pelo menos um erro desde a inicialização.
int parser_had_error(const Parser* p);

// Número absoluto de erros registrados.
int parser_error_count(const Parser* p);

#endif
