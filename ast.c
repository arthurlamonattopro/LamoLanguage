#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ast.h"

ASTNode* ast_new_node(ASTNodeType type, size_t size, int line, int column) {
    ASTNode* node = (ASTNode*)malloc(size);
    if (!node) {
        perror("Failed to allocate ASTNode");
        exit(EXIT_FAILURE);
    }
    memset(node, 0, size);
    node->type = type;
    node->line = line;
    node->column = column;
    node->next = NULL;
    return node;
}

ASTProgram* ast_new_program() {
    ASTProgram* node = (ASTProgram*)ast_new_node(AST_PROGRAM, sizeof(ASTProgram), 0, 0);
    node->declarations = NULL;
    return node;
}

ASTVarDecl* ast_new_var_decl(char* name, ASTNode* initializer, int line, int column) {
    ASTVarDecl* node = (ASTVarDecl*)ast_new_node(AST_VAR_DECL, sizeof(ASTVarDecl), line, column);
    node->name = strdup(name);
    node->initializer = initializer;
    return node;
}

ASTFnDecl* ast_new_fn_decl(char* name, char** params, int param_count, ASTNode* body, int line, int column) {
    ASTFnDecl* node = (ASTFnDecl*)ast_new_node(AST_FN_DECL, sizeof(ASTFnDecl), line, column);
    node->name = strdup(name);
    node->params = params;
    node->param_count = param_count;
    node->body = body;
    return node;
}

ASTBlock* ast_new_block(ASTNode* statements, int line, int column) {
    ASTBlock* node = (ASTBlock*)ast_new_node(AST_BLOCK, sizeof(ASTBlock), line, column);
    node->statements = statements;
    return node;
}

ASTIfStmt* ast_new_if_stmt(ASTNode* condition, ASTNode* then_branch, ASTNode* else_branch, int line, int column) {
    ASTIfStmt* node = (ASTIfStmt*)ast_new_node(AST_IF_STMT, sizeof(ASTIfStmt), line, column);
    node->condition = condition;
    node->then_branch = then_branch;
    node->else_branch = else_branch;
    return node;
}

ASTWhileStmt* ast_new_while_stmt(ASTNode* condition, ASTNode* body, int line, int column) {
    ASTWhileStmt* node = (ASTWhileStmt*)ast_new_node(AST_WHILE_STMT, sizeof(ASTWhileStmt), line, column);
    node->condition = condition;
    node->body = body;
    return node;
}

ASTForStmt* ast_new_for_stmt(ASTNode* initializer, ASTNode* condition, ASTNode* increment, ASTNode* body, int line, int column) {
    ASTForStmt* node = (ASTForStmt*)ast_new_node(AST_FOR_STMT, sizeof(ASTForStmt), line, column);
    node->initializer = initializer;
    node->condition = condition;
    node->increment = increment;
    node->body = body;
    return node;
}

ASTReturnStmt* ast_new_return_stmt(ASTNode* expression, int line, int column) {
    ASTReturnStmt* node = (ASTReturnStmt*)ast_new_node(AST_RETURN_STMT, sizeof(ASTReturnStmt), line, column);
    node->expression = expression;
    return node;
}

ASTPrintStmt* ast_new_print_stmt(ASTNode* expression, int line, int column) {
    ASTPrintStmt* node = (ASTPrintStmt*)ast_new_node(AST_PRINT_STMT, sizeof(ASTPrintStmt), line, column);
    node->expression = expression;
    return node;
}

ASTAssignStmt* ast_new_assign_stmt(char* name, ASTNode* value, TokenType op_type, int line, int column) {
    ASTAssignStmt* node = (ASTAssignStmt*)ast_new_node(AST_ASSIGN_STMT, sizeof(ASTAssignStmt), line, column);
    node->name = strdup(name);
    node->value = value;
    node->op_type = op_type;
    return node;
}

ASTCallStmt* ast_new_call_stmt(char* name, ASTNode** args, int arg_count, int line, int column) {
    ASTCallStmt* node = (ASTCallStmt*)ast_new_node(AST_CALL_STMT, sizeof(ASTCallStmt), line, column);
    node->name = strdup(name);
    node->args = args;
    node->arg_count = arg_count;
    return node;
}

ASTBinaryExpr* ast_new_binary_expr(ASTNode* left, TokenType operator, ASTNode* right, int line, int column) {
    ASTBinaryExpr* node = (ASTBinaryExpr*)ast_new_node(AST_BINARY_EXPR, sizeof(ASTBinaryExpr), line, column);
    node->left = left;
    node->operator = operator;
    node->right = right;
    return node;
}

ASTUnaryExpr* ast_new_unary_expr(TokenType operator, ASTNode* right, int line, int column) {
    ASTUnaryExpr* node = (ASTUnaryExpr*)ast_new_node(AST_UNARY_EXPR, sizeof(ASTUnaryExpr), line, column);
    node->operator = operator;
    node->right = right;
    return node;
}

ASTIntLiteral* ast_new_int_literal(int value, int line, int column) {
    ASTIntLiteral* node = (ASTIntLiteral*)ast_new_node(AST_INT_LITERAL, sizeof(ASTIntLiteral), line, column);
    node->value = value;
    return node;
}

ASTStringLiteral* ast_new_string_literal(char* value, int line, int column) {
    ASTStringLiteral* node = (ASTStringLiteral*)ast_new_node(AST_STRING_LITERAL, sizeof(ASTStringLiteral), line, column);
    node->value = strdup(value);
    return node;
}

ASTBoolLiteral* ast_new_bool_literal(int value, int line, int column) {
    ASTBoolLiteral* node = (ASTBoolLiteral*)ast_new_node(AST_BOOL_LITERAL, sizeof(ASTBoolLiteral), line, column);
    node->value = value;
    return node;
}

ASTIdentifier* ast_new_identifier(char* name, int line, int column) {
    ASTIdentifier* node = (ASTIdentifier*)ast_new_node(AST_IDENTIFIER, sizeof(ASTIdentifier), line, column);
    node->name = strdup(name);
    return node;
}

ASTCallExpr* ast_new_call_expr(char* name, ASTNode** args, int arg_count, int line, int column) {
    ASTCallExpr* node = (ASTCallExpr*)ast_new_node(AST_CALL_EXPR, sizeof(ASTCallExpr), line, column);
    node->name = strdup(name);
    node->args = args;
    node->arg_count = arg_count;
    return node;
}

ASTGroupingExpr* ast_new_grouping_expr(ASTNode* expression, int line, int column) {
    ASTGroupingExpr* node = (ASTGroupingExpr*)ast_new_node(AST_GROUPING_EXPR, sizeof(ASTGroupingExpr), line, column);
    node->expression = expression;
    return node;
}

void ast_free(ASTNode* node) {
    if (!node) return;
    ASTNode* next = node->next;

    switch (node->type) {
        case AST_PROGRAM:
            ast_free(((ASTProgram*)node)->declarations);
            break;
        case AST_VAR_DECL:
            free(((ASTVarDecl*)node)->name);
            ast_free(((ASTVarDecl*)node)->initializer);
            break;
        case AST_FN_DECL:
            free(((ASTFnDecl*)node)->name);
            for (int i = 0; i < ((ASTFnDecl*)node)->param_count; i++) {
                free(((ASTFnDecl*)node)->params[i]);
            }
            free(((ASTFnDecl*)node)->params);
            ast_free(((ASTFnDecl*)node)->body);
            break;
        case AST_BLOCK:
            ast_free(((ASTBlock*)node)->statements);
            break;
        case AST_IF_STMT:
            ast_free(((ASTIfStmt*)node)->condition);
            ast_free(((ASTIfStmt*)node)->then_branch);
            ast_free(((ASTIfStmt*)node)->else_branch);
            break;
        case AST_WHILE_STMT:
            ast_free(((ASTWhileStmt*)node)->condition);
            ast_free(((ASTWhileStmt*)node)->body);
            break;
        case AST_FOR_STMT:
            ast_free(((ASTForStmt*)node)->initializer);
            ast_free(((ASTForStmt*)node)->condition);
            ast_free(((ASTForStmt*)node)->increment);
            ast_free(((ASTForStmt*)node)->body);
            break;
        case AST_RETURN_STMT:
        case AST_PRINT_STMT:
            ast_free(((ASTReturnStmt*)node)->expression);
            break;
        case AST_ASSIGN_STMT:
            free(((ASTAssignStmt*)node)->name);
            ast_free(((ASTAssignStmt*)node)->value);
            break;
        case AST_CALL_STMT:
        case AST_CALL_EXPR:
            free(((ASTCallStmt*)node)->name);
            for (int i = 0; i < ((ASTCallStmt*)node)->arg_count; i++) {
                ast_free(((ASTCallStmt*)node)->args[i]);
            }
            free(((ASTCallStmt*)node)->args);
            break;
        case AST_BINARY_EXPR:
            ast_free(((ASTBinaryExpr*)node)->left);
            ast_free(((ASTBinaryExpr*)node)->right);
            break;
        case AST_UNARY_EXPR:
            ast_free(((ASTUnaryExpr*)node)->right);
            break;
        case AST_INT_LITERAL:
            break;
        case AST_STRING_LITERAL:
            free(((ASTStringLiteral*)node)->value);
            break;
        case AST_BOOL_LITERAL:
            break;
        case AST_IDENTIFIER:
            free(((ASTIdentifier*)node)->name);
            break;
        case AST_GROUPING_EXPR:
            ast_free(((ASTGroupingExpr*)node)->expression);
            break;
    }

    free(node);
    if (next) ast_free(next);
}
