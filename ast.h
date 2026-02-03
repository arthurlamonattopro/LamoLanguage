#ifndef AST_H
#define AST_H

#include "lexer_v2.h"
#include <stddef.h>

// Enumeração dos tipos de nós da AST
typedef enum {
    AST_PROGRAM,
    AST_VAR_DECL,
    AST_FN_DECL,
    AST_BLOCK,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_RETURN_STMT,
    AST_PRINT_STMT,
    AST_INPUT_EXPR,
    AST_ISNUMBER_EXPR,
    AST_ISSTRING_EXPR,
    AST_EXIT_STMT,
    AST_ABS_EXPR,
    AST_ASSIGN_STMT,
    AST_CALL_STMT,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_INT_LITERAL,
    AST_STRING_LITERAL,
    AST_BOOL_LITERAL,
    AST_IDENTIFIER,
    AST_CALL_EXPR,
    AST_GROUPING_EXPR
} ASTNodeType;

// Estrutura base para todos os nós da AST
typedef struct ASTNode {
    ASTNodeType type;
    int line;
    int column;
    struct ASTNode* next;
} ASTNode;

typedef struct {
    ASTNode base;
    char* name;
    struct ASTNode* initializer;
} ASTVarDecl;

typedef struct {
    ASTNode base;
    char* name;
    char** params;
    int param_count;
    struct ASTNode* body;
} ASTFnDecl;

typedef struct {
    ASTNode base;
    struct ASTNode* statements;
} ASTBlock;

typedef struct {
    ASTNode base;
    struct ASTNode* condition;
    struct ASTNode* then_branch;
    struct ASTNode* else_branch;
} ASTIfStmt;

typedef struct {
    ASTNode base;
    struct ASTNode* condition;
    struct ASTNode* body;
} ASTWhileStmt;

typedef struct {
    ASTNode base;
    struct ASTNode* initializer;
    struct ASTNode* condition;
    struct ASTNode* increment;
    struct ASTNode* body;
} ASTForStmt;

typedef struct {
    ASTNode base;
    struct ASTNode* expression;
} ASTReturnStmt;

typedef struct {
    ASTNode base;
    struct ASTNode* expression;
} ASTPrintStmt;

typedef struct {
    ASTNode base;
    char* name;
    struct ASTNode* value;
    TokenType op_type;
} ASTAssignStmt;

typedef struct {
    ASTNode base;
    char* name;
    struct ASTNode** args;
    int arg_count;
} ASTCallStmt;

typedef struct {
    ASTNode base;
    struct ASTNode* left;
    TokenType operator;
    struct ASTNode* right;
} ASTBinaryExpr;

typedef struct {
    ASTNode base;
    TokenType operator;
    struct ASTNode* right;
} ASTUnaryExpr;

typedef struct {
    ASTNode base;
    int value;
} ASTIntLiteral;

typedef struct {
    ASTNode base;
    char* value;
} ASTStringLiteral;

typedef struct {
    ASTNode base;
    int value;
} ASTBoolLiteral;

typedef struct {
    ASTNode base;
    char* name;
} ASTIdentifier;

typedef struct {
    ASTNode base;
    char* name;
    struct ASTNode** args;
    int arg_count;
} ASTCallExpr;

typedef struct {
    ASTNode base;
    struct ASTNode* expression;
} ASTGroupingExpr;

typedef struct {
    ASTNode base;
    struct ASTNode* declarations;
} ASTProgram;

ASTNode* ast_new_node(ASTNodeType type, size_t size, int line, int column);
ASTProgram* ast_new_program();
ASTVarDecl* ast_new_var_decl(char* name, ASTNode* initializer, int line, int column);
ASTFnDecl* ast_new_fn_decl(char* name, char** params, int param_count, ASTNode* body, int line, int column);
ASTBlock* ast_new_block(ASTNode* statements, int line, int column);
ASTIfStmt* ast_new_if_stmt(ASTNode* condition, ASTNode* then_branch, ASTNode* else_branch, int line, int column);
ASTWhileStmt* ast_new_while_stmt(ASTNode* condition, ASTNode* body, int line, int column);
ASTForStmt* ast_new_for_stmt(ASTNode* initializer, ASTNode* condition, ASTNode* increment, ASTNode* body, int line, int column);
ASTReturnStmt* ast_new_return_stmt(ASTNode* expression, int line, int column);
ASTPrintStmt* ast_new_print_stmt(ASTNode* expression, int line, int column);
ASTNode* ast_new_input_expr(ASTNode* prompt, int line, int column);
ASTNode* ast_new_isnumber_expr(ASTNode* expression, int line, int column);
ASTNode* ast_new_isstring_expr(ASTNode* expression, int line, int column);
ASTNode* ast_new_exit_stmt(ASTNode* code, int line, int column);
ASTNode* ast_new_abs_expr(ASTNode* expression, int line, int column);
ASTAssignStmt* ast_new_assign_stmt(char* name, ASTNode* value, TokenType op_type, int line, int column);
ASTCallStmt* ast_new_call_stmt(char* name, ASTNode** args, int arg_count, int line, int column);
ASTBinaryExpr* ast_new_binary_expr(ASTNode* left, TokenType operator, ASTNode* right, int line, int column);
ASTUnaryExpr* ast_new_unary_expr(TokenType operator, ASTNode* right, int line, int column);
ASTIntLiteral* ast_new_int_literal(int value, int line, int column);
ASTStringLiteral* ast_new_string_literal(char* value, int line, int column);
ASTBoolLiteral* ast_new_bool_literal(int value, int line, int column);
ASTIdentifier* ast_new_identifier(char* name, int line, int column);
ASTCallExpr* ast_new_call_expr(char* name, ASTNode** args, int arg_count, int line, int column);
ASTGroupingExpr* ast_new_grouping_expr(ASTNode* expression, int line, int column);

void ast_free(ASTNode* node);

#endif
