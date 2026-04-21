#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

int semantic_analyze(ASTProgram* program, const char* file_path);

#endif
