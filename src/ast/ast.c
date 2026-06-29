#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ast.h"

// Bug #5 fix: o parser seta esse "current file path" antes de criar nós da
// AST, e ast_new_node() copia o ponteiro para cada nó. Isso permite que o
// semântico reporte erros com o arquivo de origem correto, mesmo em
// compilações multi-arquivo (programa principal + imports). Single-threaded:
// safe.
//
// O ponteiro NÃO é owned pela AST — ele aponta para o `normalized_path` que
// já vive na CompilationState durante todo o tempo de vida da AST. Não há
// free aqui.
static const char* g_default_file_path = NULL;

void ast_set_default_file_path(const char* path) {
    g_default_file_path = path;
}

const char* ast_get_default_file_path(void) {
    return g_default_file_path;
}

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
    node->file_path = g_default_file_path;
    return node;
}

ASTProgram* ast_new_program() {
    ASTProgram* node = (ASTProgram*)ast_new_node(AST_PROGRAM, sizeof(ASTProgram), 0, 0);
    node->declarations = NULL;
    return node;
}

ASTVarDecl* ast_new_var_decl(char* name, ASTNode* initializer, int line, int column) {
    return ast_new_var_decl_typed(name, initializer, NULL, line, column);
}

/* Sprint 3: typed var decl. type_annotation is owned by the AST (it's
 * strdup'd here). NULL means no annotation. */
ASTVarDecl* ast_new_var_decl_typed(char* name, ASTNode* initializer, char* type_annotation, int line, int column) {
    ASTVarDecl* node = (ASTVarDecl*)ast_new_node(AST_VAR_DECL, sizeof(ASTVarDecl), line, column);
    node->name = strdup(name);
    node->initializer = initializer;
    node->type_annotation = type_annotation ? strdup(type_annotation) : NULL;
    return node;
}

ASTFnDecl* ast_new_fn_decl(char* name, char** params, int param_count, ASTNode* body, int line, int column) {
    return ast_new_fn_decl_typed(name, params, NULL, param_count, NULL, body, line, column);
}

/* Sprint 3: typed fn decl. param_types may be NULL (no annotations) or
 * an array of param_count entries, each either NULL or a heap-allocated
 * string. The array is taken ownership of (caller should NOT free it).
 * return_type_annotation is owned by the AST (strdup'd here). */
ASTFnDecl* ast_new_fn_decl_typed(char* name, char** params, char** param_types, int param_count, char* return_type_annotation, ASTNode* body, int line, int column) {
    ASTFnDecl* node = (ASTFnDecl*)ast_new_node(AST_FN_DECL, sizeof(ASTFnDecl), line, column);
    node->name = strdup(name);
    node->params = params;
    node->param_types = param_types;
    node->param_count = param_count;
    node->return_type_annotation = return_type_annotation ? strdup(return_type_annotation) : NULL;
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

ASTNode* ast_new_break_stmt(int line, int column) {
    return ast_new_node(AST_BREAK_STMT, sizeof(ASTNode), line, column);
}

ASTNode* ast_new_continue_stmt(int line, int column) {
    return ast_new_node(AST_CONTINUE_STMT, sizeof(ASTNode), line, column);
}

ASTAssignStmt* ast_new_assign_stmt(char* name, ASTNode* value, LamoTokenType op_type, int line, int column) {
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

ASTBinaryExpr* ast_new_binary_expr(ASTNode* left, LamoTokenType operator, ASTNode* right, int line, int column) {
    ASTBinaryExpr* node = (ASTBinaryExpr*)ast_new_node(AST_BINARY_EXPR, sizeof(ASTBinaryExpr), line, column);
    node->left = left;
    node->operator = operator;
    node->right = right;
    return node;
}

ASTUnaryExpr* ast_new_unary_expr(LamoTokenType operator, ASTNode* right, int line, int column) {
    ASTUnaryExpr* node = (ASTUnaryExpr*)ast_new_node(AST_UNARY_EXPR, sizeof(ASTUnaryExpr), line, column);
    node->operator = operator;
    node->right = right;
    return node;
}

ASTIntLiteral* ast_new_int_literal(long long value, int line, int column) {
    ASTIntLiteral* node = (ASTIntLiteral*)ast_new_node(AST_INT_LITERAL, sizeof(ASTIntLiteral), line, column);
    node->value = value;
    return node;
}

ASTFloatLiteral* ast_new_float_literal(double value, int line, int column) {
    ASTFloatLiteral* node = (ASTFloatLiteral*)ast_new_node(AST_FLOAT_LITERAL, sizeof(ASTFloatLiteral), line, column);
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

ASTImport* ast_new_import_decl(char* path, int line, int column) {
    ASTImport* node = (ASTImport*)ast_new_node(AST_IMPORT, sizeof(ASTImport), line, column);
    node->path = strdup(path);
    return node;
}

/* Sprint 3: array literal. Takes ownership of the elements array (caller
 * must NOT free it). element_count is the number of valid entries in
 * elements. */
ASTArrayLiteral* ast_new_array_literal(ASTNode** elements, int element_count, int line, int column) {
    ASTArrayLiteral* node = (ASTArrayLiteral*)ast_new_node(AST_ARRAY_LITERAL, sizeof(ASTArrayLiteral), line, column);
    node->elements = elements;
    node->element_count = element_count;
    return node;
}

ASTIndexExpr* ast_new_index_expr(ASTNode* array, ASTNode* index, int line, int column) {
    ASTIndexExpr* node = (ASTIndexExpr*)ast_new_node(AST_INDEX_EXPR, sizeof(ASTIndexExpr), line, column);
    node->array = array;
    node->index = index;
    return node;
}

ASTPropExpr* ast_new_prop_expr(ASTNode* object, char* prop_name, int line, int column) {
    ASTPropExpr* node = (ASTPropExpr*)ast_new_node(AST_PROP_EXPR, sizeof(ASTPropExpr), line, column);
    node->object = object;
    node->prop_name = strdup(prop_name);
    return node;
}

void ast_program_append(ASTProgram* destination, ASTProgram* source) {
    ASTNode* tail;

    if (!destination || !source || !source->declarations) {
        if (source) {
            source->declarations = NULL;
            free(source);
        }
        return;
    }

    if (!destination->declarations) {
        destination->declarations = source->declarations;
        source->declarations = NULL;
        free(source);
        return;
    }

    tail = destination->declarations;
    while (tail->next) {
        tail = tail->next;
    }

    tail->next = source->declarations;
    source->declarations = NULL;
    free(source);
}

/* Sprint 2 refactor: the `next` list traversal is now iterative. The
 * previous version recursed on `next` at the tail of the function:
 *
 *     free(node);
 *     if (next) ast_free(next);
 *
 * That consumed O(N) stack frames for a list of N sibling nodes — fine
 * for small programs, but a Lamo file with tens of thousands of
 * top-level statements would blow the C stack. The recursive descent
 * into CHILDREN (initializer, body, args, left/right, ...) is left
 * alone because that depth is bounded by expression nesting, not by
 * the number of statements in the program.
 *
 * The new shape is a loop that walks `next` siblings while recursing
 * into children exactly as before. */
void ast_free(ASTNode* node) {
    while (node) {
        ASTNode* next = node->next;

        switch (node->type) {
            case AST_PROGRAM:
                ast_free(((ASTProgram*)node)->declarations);
                break;
            case AST_VAR_DECL:
                free(((ASTVarDecl*)node)->name);
                free(((ASTVarDecl*)node)->type_annotation);
                ast_free(((ASTVarDecl*)node)->initializer);
                break;
            case AST_FN_DECL:
                free(((ASTFnDecl*)node)->name);
                for (int i = 0; i < ((ASTFnDecl*)node)->param_count; i++) {
                    free(((ASTFnDecl*)node)->params[i]);
                }
                free(((ASTFnDecl*)node)->params);
                /* Sprint 3: free param type annotations. param_types may
                 * be NULL (no annotations on any parameter). */
                if (((ASTFnDecl*)node)->param_types) {
                    for (int i = 0; i < ((ASTFnDecl*)node)->param_count; i++) {
                        free(((ASTFnDecl*)node)->param_types[i]);
                    }
                    free(((ASTFnDecl*)node)->param_types);
                }
                free(((ASTFnDecl*)node)->return_type_annotation);
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
                ast_free(((ASTReturnStmt*)node)->expression);
                break;
            case AST_BREAK_STMT:
            case AST_CONTINUE_STMT:
                /* leaf nodes — no extra fields to free. */
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
            case AST_FLOAT_LITERAL:
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
            case AST_IMPORT:
                free(((ASTImport*)node)->path);
                break;
            case AST_ARRAY_LITERAL: {
                /* Sprint 3: array literal. Free each element expression,
                 * then the elements array itself. */
                ASTArrayLiteral* arr = (ASTArrayLiteral*)node;
                int i;
                for (i = 0; i < arr->element_count; i++) {
                    ast_free(arr->elements[i]);
                }
                free(arr->elements);
                break;
            }
            case AST_INDEX_EXPR: {
                ASTIndexExpr* idx = (ASTIndexExpr*)node;
                ast_free(idx->array);
                ast_free(idx->index);
                break;
            }
            case AST_PROP_EXPR: {
                ASTPropExpr* prop = (ASTPropExpr*)node;
                ast_free(prop->object);
                free(prop->prop_name);
                break;
            }
        }

        free(node);
        node = next;
    }
}
