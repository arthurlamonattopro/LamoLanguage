#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"
#include <stdio.h>

// Função principal para gerar código C a partir da AST
void generate_c_code(ASTNode* node, FILE* out);

#endif // CODEGEN_H
