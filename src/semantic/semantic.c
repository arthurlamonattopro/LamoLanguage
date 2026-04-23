#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semantic.h"

typedef enum {
    SYMBOL_VAR,
    SYMBOL_FN
} SymbolKind;

typedef struct Symbol {
    char* name;
    SymbolKind kind;
    int arity;
    struct Symbol* next;
} Symbol;

typedef struct Scope {
    Symbol* symbols;
    struct Scope* parent;
} Scope;

typedef struct {
    const char* file_path;
    Scope* current_scope;
    int inside_function;
    int errors;
} SemanticContext;

static void semantic_visit_statement(SemanticContext* ctx, ASTNode* node);
static void semantic_visit_expression(SemanticContext* ctx, ASTNode* node);
static int builtin_function_arity(const char* name);
static int semantic_validate_builtin_call(SemanticContext* ctx, const char* name, ASTNode** args, int arg_count, int line, int column);

static Scope* scope_push(Scope* parent) {
    Scope* scope = malloc(sizeof(Scope));
    if (!scope) {
        perror("Failed to allocate semantic scope");
        exit(EXIT_FAILURE);
    }

    scope->symbols = NULL;
    scope->parent = parent;
    return scope;
}

static void scope_free(Scope* scope) {
    Symbol* symbol = scope->symbols;
    while (symbol) {
        Symbol* next = symbol->next;
        free(symbol->name);
        free(symbol);
        symbol = next;
    }
    free(scope);
}

static void semantic_error_at(SemanticContext* ctx, int line, int column, const char* message) {
    fprintf(stderr, "%s:%d:%d: semantic error: %s\n",
            ctx->file_path ? ctx->file_path : "<input>",
            line,
            column,
            message);
    ctx->errors++;
}

static Symbol* scope_find_in_current(Scope* scope, const char* name) {
    for (Symbol* symbol = scope->symbols; symbol; symbol = symbol->next) {
        if (strcmp(symbol->name, name) == 0) {
            return symbol;
        }
    }
    return NULL;
}

static Symbol* scope_find(Scope* scope, const char* name) {
    for (Scope* current = scope; current; current = current->parent) {
        Symbol* symbol = scope_find_in_current(current, name);
        if (symbol) {
            return symbol;
        }
    }
    return NULL;
}

static void scope_define(SemanticContext* ctx, Scope* scope, const char* name, SymbolKind kind, int arity, int line, int column) {
    if (scope_find_in_current(scope, name)) {
        char message[256];
        snprintf(message, sizeof(message), "duplicate declaration of '%s'", name);
        semantic_error_at(ctx, line, column, message);
        return;
    }

    Symbol* symbol = malloc(sizeof(Symbol));
    if (!symbol) {
        perror("Failed to allocate semantic symbol");
        exit(EXIT_FAILURE);
    }

    symbol->name = strdup(name);
    symbol->kind = kind;
    symbol->arity = arity;
    symbol->next = scope->symbols;
    scope->symbols = symbol;
}

static void semantic_visit_block(SemanticContext* ctx, ASTBlock* block) {
    Scope* parent = ctx->current_scope;
    ctx->current_scope = scope_push(parent);

    for (ASTNode* statement = block->statements; statement; statement = statement->next) {
        semantic_visit_statement(ctx, statement);
    }

    Scope* finished = ctx->current_scope;
    ctx->current_scope = parent;
    scope_free(finished);
}

static void semantic_visit_call(SemanticContext* ctx, const char* name, ASTNode** args, int arg_count, int line, int column) {
    Symbol* symbol = scope_find(ctx->current_scope, name);
    int builtin_arity = builtin_function_arity(name);

    if (symbol && symbol->kind == SYMBOL_FN) {
        if (symbol->arity != arg_count) {
            char message[256];
            snprintf(message, sizeof(message), "function '%s' expects %d argument(s), got %d",
                     name, symbol->arity, arg_count);
            semantic_error_at(ctx, line, column, message);
        }
    } else if (builtin_arity >= 0) {
        if (builtin_arity != arg_count) {
            char message[256];
            snprintf(message, sizeof(message), "builtin '%s' expects %d argument(s), got %d",
                     name, builtin_arity, arg_count);
            semantic_error_at(ctx, line, column, message);
        } else {
            semantic_validate_builtin_call(ctx, name, args, arg_count, line, column);
        }
    } else {
        char message[256];
        snprintf(message, sizeof(message), "call to undeclared function '%s'", name);
        semantic_error_at(ctx, line, column, message);
    }

    for (int i = 0; i < arg_count; i++) {
        semantic_visit_expression(ctx, args[i]);
    }
}

static int builtin_function_arity(const char* name) {
    if (strcmp(name, "gui_open") == 0) return 3;
    if (strcmp(name, "gui_should_close") == 0) return 0;
    if (strcmp(name, "gui_begin_frame") == 0) return 3;
    if (strcmp(name, "gui_draw_rect") == 0) return 7;
    if (strcmp(name, "gui_draw_text") == 0) return 6;
    if (strcmp(name, "gui_end_frame") == 0) return 0;
    if (strcmp(name, "gui_close") == 0) return 0;
    if (strcmp(name, "http_route") == 0) return 2;
    if (strcmp(name, "http_serve") == 0) return 1;
    if (strcmp(name, "http_serve_once") == 0) return 1;
    return -1;
}

static int semantic_validate_builtin_call(SemanticContext* ctx, const char* name, ASTNode** args, int arg_count, int line, int column) {
    (void)arg_count;

    if (strcmp(name, "gui_open") == 0 && args[2]->type != AST_STRING_LITERAL) {
        semantic_error_at(ctx, line, column, "gui_open(width, height, title) requires a string literal title");
        return 0;
    }

    if (strcmp(name, "gui_draw_text") == 0 && args[0]->type != AST_STRING_LITERAL) {
        semantic_error_at(ctx, line, column, "gui_draw_text(text, x, y, r, g, b) requires a string literal text");
        return 0;
    }

    return 1;
}

static void semantic_visit_statement(SemanticContext* ctx, ASTNode* node) {
    if (!node) {
        return;
    }

    switch (node->type) {
        case AST_VAR_DECL: {
            ASTVarDecl* var_decl = (ASTVarDecl*)node;
            semantic_visit_expression(ctx, var_decl->initializer);
            scope_define(ctx, ctx->current_scope, var_decl->name, SYMBOL_VAR, 0, node->line, node->column);
            break;
        }
        case AST_FN_DECL: {
            ASTFnDecl* fn_decl = (ASTFnDecl*)node;
            Scope* parent = ctx->current_scope;
            int previous_inside_function = ctx->inside_function;

            ctx->current_scope = scope_push(parent);
            ctx->inside_function = 1;

            for (int i = 0; i < fn_decl->param_count; i++) {
                scope_define(ctx, ctx->current_scope, fn_decl->params[i], SYMBOL_VAR, 0, node->line, node->column);
            }

            semantic_visit_statement(ctx, fn_decl->body);

            Scope* finished = ctx->current_scope;
            ctx->current_scope = parent;
            ctx->inside_function = previous_inside_function;
            scope_free(finished);
            break;
        }
        case AST_BLOCK:
            semantic_visit_block(ctx, (ASTBlock*)node);
            break;
        case AST_IF_STMT: {
            ASTIfStmt* if_stmt = (ASTIfStmt*)node;
            semantic_visit_expression(ctx, if_stmt->condition);
            semantic_visit_statement(ctx, if_stmt->then_branch);
            semantic_visit_statement(ctx, if_stmt->else_branch);
            break;
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* while_stmt = (ASTWhileStmt*)node;
            semantic_visit_expression(ctx, while_stmt->condition);
            semantic_visit_statement(ctx, while_stmt->body);
            break;
        }
        case AST_FOR_STMT: {
            ASTForStmt* for_stmt = (ASTForStmt*)node;
            Scope* parent = ctx->current_scope;
            ctx->current_scope = scope_push(parent);

            semantic_visit_statement(ctx, for_stmt->initializer);
            semantic_visit_expression(ctx, for_stmt->condition);
            semantic_visit_statement(ctx, for_stmt->increment);
            semantic_visit_statement(ctx, for_stmt->body);

            Scope* finished = ctx->current_scope;
            ctx->current_scope = parent;
            scope_free(finished);
            break;
        }
        case AST_RETURN_STMT: {
            ASTReturnStmt* return_stmt = (ASTReturnStmt*)node;
            if (!ctx->inside_function) {
                semantic_error_at(ctx, node->line, node->column, "return is only valid inside a function");
            }
            semantic_visit_expression(ctx, return_stmt->expression);
            break;
        }
        case AST_PRINT_STMT:
            semantic_visit_expression(ctx, ((ASTPrintStmt*)node)->expression);
            break;
        case AST_ASSIGN_STMT: {
            ASTAssignStmt* assign_stmt = (ASTAssignStmt*)node;
            Symbol* symbol = scope_find(ctx->current_scope, assign_stmt->name);
            if (!symbol || symbol->kind != SYMBOL_VAR) {
                char message[256];
                snprintf(message, sizeof(message), "assignment to undeclared variable '%s'", assign_stmt->name);
                semantic_error_at(ctx, node->line, node->column, message);
            }
            semantic_visit_expression(ctx, assign_stmt->value);
            break;
        }
        case AST_CALL_STMT: {
            ASTCallStmt* call_stmt = (ASTCallStmt*)node;
            semantic_visit_call(ctx, call_stmt->name, call_stmt->args, call_stmt->arg_count, node->line, node->column);
            break;
        }
        default:
            break;
    }
}

static void semantic_visit_expression(SemanticContext* ctx, ASTNode* node) {
    if (!node) {
        return;
    }

    switch (node->type) {
        case AST_IDENTIFIER: {
            ASTIdentifier* identifier = (ASTIdentifier*)node;
            Symbol* symbol = scope_find(ctx->current_scope, identifier->name);
            if (!symbol || symbol->kind != SYMBOL_VAR) {
                char message[256];
                snprintf(message, sizeof(message), "use of undeclared variable '%s'", identifier->name);
                semantic_error_at(ctx, node->line, node->column, message);
            }
            break;
        }
        case AST_BINARY_EXPR: {
            ASTBinaryExpr* expr = (ASTBinaryExpr*)node;
            semantic_visit_expression(ctx, expr->left);
            semantic_visit_expression(ctx, expr->right);
            break;
        }
        case AST_UNARY_EXPR:
            semantic_visit_expression(ctx, ((ASTUnaryExpr*)node)->right);
            break;
        case AST_CALL_EXPR: {
            ASTCallExpr* call_expr = (ASTCallExpr*)node;
            semantic_visit_call(ctx, call_expr->name, call_expr->args, call_expr->arg_count, node->line, node->column);
            break;
        }
        case AST_GROUPING_EXPR:
            semantic_visit_expression(ctx, ((ASTGroupingExpr*)node)->expression);
            break;
        case AST_INPUT_EXPR:
        case AST_ISNUMBER_EXPR:
        case AST_ISSTRING_EXPR:
        case AST_EXIT_STMT:
        case AST_ABS_EXPR:
            semantic_visit_expression(ctx, ((ASTPrintStmt*)node)->expression);
            break;
        case AST_INT_LITERAL:
        case AST_STRING_LITERAL:
        case AST_BOOL_LITERAL:
            break;
        default:
            semantic_visit_statement(ctx, node);
            break;
    }
}

int semantic_analyze(ASTProgram* program, const char* file_path) {
    SemanticContext ctx;
    ctx.file_path = file_path;
    ctx.current_scope = scope_push(NULL);
    ctx.inside_function = 0;
    ctx.errors = 0;

    for (ASTNode* node = program->declarations; node; node = node->next) {
        if (node->type == AST_FN_DECL) {
            ASTFnDecl* fn_decl = (ASTFnDecl*)node;
            scope_define(&ctx, ctx.current_scope, fn_decl->name, SYMBOL_FN, fn_decl->param_count, node->line, node->column);
        }
    }

    for (ASTNode* node = program->declarations; node; node = node->next) {
        semantic_visit_statement(&ctx, node);
    }

    scope_free(ctx.current_scope);
    return ctx.errors == 0;
}
