#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semantic.h"
#include "lexer.h"

// ---------------------------------------------------------------------------
// Type model used by the semantic analyzer.
//
// Lamo is dynamically typed at runtime, but the semantic pass does a
// best-effort compile-time check on operators that would always crash at
// runtime (e.g. `"abc" * 3`). The inferred type is attached to each Symbol
// and propagated through expressions.
//
// LAMO_TYPE_UNKNOWN is used for "could not infer" (e.g. function return type
// before it is analyzed, or after a previous error). Operations involving
// UNKNOWN are not flagged so we don't cascade errors.
// ---------------------------------------------------------------------------
typedef enum {
    LAMO_TYPE_UNKNOWN,
    LAMO_TYPE_INT,
    LAMO_TYPE_FLOAT,
    LAMO_TYPE_STRING,
    LAMO_TYPE_BOOL
} LamoType;

typedef enum {
    SYMBOL_VAR,
    SYMBOL_FN
} SymbolKind;

typedef struct Symbol {
    char* name;
    SymbolKind kind;
    int arity;
    LamoType type;          // for SYMBOL_VAR: the variable's inferred type.
                            // for SYMBOL_FN: the inferred return type (UNKNOWN if not yet known).
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
static LamoType semantic_infer_expression(SemanticContext* ctx, ASTNode* node);
static int builtin_function_arity(const char* name);
static LamoType builtin_function_return_type(const char* name, ASTNode** args, int arg_count);
static int semantic_validate_builtin_call(SemanticContext* ctx, const char* name, ASTNode** args, int arg_count, int line, int column);

static const char* type_name(LamoType type) {
    switch (type) {
        case LAMO_TYPE_INT:    return "int";
        case LAMO_TYPE_FLOAT:  return "float";
        case LAMO_TYPE_STRING: return "string";
        case LAMO_TYPE_BOOL:   return "bool";
        case LAMO_TYPE_UNKNOWN: return "unknown";
    }
    return "unknown";
}

static int is_numeric_type(LamoType type) {
    return type == LAMO_TYPE_INT || type == LAMO_TYPE_FLOAT;
}

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

static void scope_define(SemanticContext* ctx, Scope* scope, const char* name, SymbolKind kind, int arity, LamoType type, int line, int column) {
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
    symbol->type = type;
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

// Visit a call site: validates arity, runs builtin-specific checks, and
// returns the inferred return type of the call. The args themselves are
// visited (and their types inferred) as part of the validation.
static LamoType semantic_visit_call(SemanticContext* ctx, const char* name, ASTNode** args, int arg_count, int line, int column) {
    Symbol* symbol = scope_find(ctx->current_scope, name);
    int builtin_arity = builtin_function_arity(name);
    LamoType return_type = LAMO_TYPE_UNKNOWN;

    if (symbol && symbol->kind == SYMBOL_FN) {
        if (symbol->arity != arg_count) {
            char message[256];
            snprintf(message, sizeof(message), "function '%s' expects %d argument(s), got %d",
                     name, symbol->arity, arg_count);
            semantic_error_at(ctx, line, column, message);
        }
        return_type = symbol->type;
    } else if (builtin_arity >= 0) {
        if (builtin_arity != arg_count) {
            char message[256];
            snprintf(message, sizeof(message), "builtin '%s' expects %d argument(s), got %d",
                     name, builtin_arity, arg_count);
            semantic_error_at(ctx, line, column, message);
        } else {
            semantic_validate_builtin_call(ctx, name, args, arg_count, line, column);
        }
        // Compute return type from builtin signature (with arg-dependent types where relevant).
        return_type = builtin_function_return_type(name, args, arg_count);
    } else {
        char message[256];
        snprintf(message, sizeof(message), "call to undeclared function '%s'", name);
        semantic_error_at(ctx, line, column, message);
    }

    for (int i = 0; i < arg_count; i++) {
        semantic_infer_expression(ctx, args[i]);
    }
    return return_type;
}

// Todos os builtins (linguagem + GUI + HTTP) num único lugar.
// Os builtins de linguagem (print, input, isnumber, isstring, exit, abs)
// agora são tratados como identificadores comuns no lexer e resolvidos aqui.
static int builtin_function_arity(const char* name) {
    // Builtins de linguagem
    if (strcmp(name, "print") == 0) return 1;
    if (strcmp(name, "input") == 0) return 1;       // input(prompt) -> int (legado)
    if (strcmp(name, "input_int") == 0) return 1;   // input_int(prompt) -> int
    if (strcmp(name, "input_str") == 0) return 1;   // input_str(prompt) -> string
    if (strcmp(name, "isnumber") == 0) return 1;
    if (strcmp(name, "isstring") == 0) return 1;
    if (strcmp(name, "exit") == 0) return 1;
    if (strcmp(name, "abs") == 0) return 1;
    // GUI
    if (strcmp(name, "gui_open") == 0) return 3;
    if (strcmp(name, "gui_should_close") == 0) return 0;
    if (strcmp(name, "gui_begin_frame") == 0) return 3;
    if (strcmp(name, "gui_draw_rect") == 0) return 7;
    if (strcmp(name, "gui_draw_text") == 0) return 6;
    if (strcmp(name, "gui_end_frame") == 0) return 0;
    if (strcmp(name, "gui_close") == 0) return 0;
    // HTTP
    if (strcmp(name, "http_route") == 0) return 2;
    if (strcmp(name, "http_serve") == 0) return 1;
    if (strcmp(name, "http_serve_once") == 0) return 1;
    return -1;
}

// Return type for each builtin. Most are fixed; `abs` mirrors the type of
// its argument (int -> int, float -> float).
static LamoType builtin_function_return_type(const char* name, ASTNode** args, int arg_count) {
    (void)arg_count;
    if (strcmp(name, "print") == 0) return LAMO_TYPE_INT;
    if (strcmp(name, "input") == 0) return LAMO_TYPE_INT;
    if (strcmp(name, "input_int") == 0) return LAMO_TYPE_INT;
    if (strcmp(name, "input_str") == 0) return LAMO_TYPE_STRING;
    if (strcmp(name, "isnumber") == 0) return LAMO_TYPE_BOOL;
    if (strcmp(name, "isstring") == 0) return LAMO_TYPE_BOOL;
    if (strcmp(name, "exit") == 0) return LAMO_TYPE_INT;
    if (strcmp(name, "abs") == 0) {
        if (arg_count >= 1 && args[0]) {
            // We don't recursively infer here (that happens in semantic_visit_call);
            // we just look at the literal-ish nodes to guess. Conservative: if it is
            // a float literal or a known float variable, return FLOAT; otherwise INT.
            // The actual recursive inference happens via semantic_infer_expression
            // when args are visited, so by the time we reach here, simple cases are
            // already represented in the AST node types we can read directly.
            ASTNode* arg = args[0];
            if (arg->type == AST_FLOAT_LITERAL) return LAMO_TYPE_FLOAT;
        }
        return LAMO_TYPE_INT;
    }
    // GUI/HTTP builtins all return int (status codes).
    return LAMO_TYPE_INT;
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

// Reports a type-mismatch error if `actual` is incompatible with `expected`,
// accounting for LAMO_TYPE_UNKNOWN (no error) and numeric compatibility
// (int and float are interchangeable in numeric contexts).
static void semantic_check_numeric_operand(SemanticContext* ctx, const char* op_name,
                                            LamoType left, LamoType right,
                                            int line, int column) {
    if (left == LAMO_TYPE_UNKNOWN || right == LAMO_TYPE_UNKNOWN) {
        return; // don't cascade errors when a previous expression failed to infer
    }
    if (left == LAMO_TYPE_STRING) {
        char message[256];
        snprintf(message, sizeof(message),
                 "operator '%s' does not support string operand (got %s, %s)",
                 op_name, type_name(left), type_name(right));
        semantic_error_at(ctx, line, column, message);
        return;
    }
    if (right == LAMO_TYPE_STRING) {
        char message[256];
        snprintf(message, sizeof(message),
                 "operator '%s' does not support string operand (got %s, %s)",
                 op_name, type_name(left), type_name(right));
        semantic_error_at(ctx, line, column, message);
        return;
    }
}

static void semantic_visit_statement(SemanticContext* ctx, ASTNode* node) {
    if (!node) {
        return;
    }

    switch (node->type) {
        case AST_VAR_DECL: {
            ASTVarDecl* var_decl = (ASTVarDecl*)node;
            LamoType init_type = semantic_infer_expression(ctx, var_decl->initializer);
            scope_define(ctx, ctx->current_scope, var_decl->name, SYMBOL_VAR, 0, init_type, node->line, node->column);
            break;
        }
        case AST_FN_DECL: {
            ASTFnDecl* fn_decl = (ASTFnDecl*)node;
            Scope* parent = ctx->current_scope;
            int previous_inside_function = ctx->inside_function;

            ctx->current_scope = scope_push(parent);
            ctx->inside_function = 1;

            for (int i = 0; i < fn_decl->param_count; i++) {
                // Parameters are untyped in Lamo syntax; treat as UNKNOWN so
                // the caller's arguments can flow without spurious errors.
                scope_define(ctx, ctx->current_scope, fn_decl->params[i], SYMBOL_VAR, 0, LAMO_TYPE_UNKNOWN, node->line, node->column);
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
            semantic_infer_expression(ctx, if_stmt->condition);
            semantic_visit_statement(ctx, if_stmt->then_branch);
            semantic_visit_statement(ctx, if_stmt->else_branch);
            break;
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* while_stmt = (ASTWhileStmt*)node;
            semantic_infer_expression(ctx, while_stmt->condition);
            semantic_visit_statement(ctx, while_stmt->body);
            break;
        }
        case AST_FOR_STMT: {
            ASTForStmt* for_stmt = (ASTForStmt*)node;
            Scope* parent = ctx->current_scope;
            ctx->current_scope = scope_push(parent);

            semantic_visit_statement(ctx, for_stmt->initializer);
            semantic_infer_expression(ctx, for_stmt->condition);
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
            if (return_stmt->expression) {
                semantic_infer_expression(ctx, return_stmt->expression);
            }
            break;
        }
        case AST_ASSIGN_STMT: {
            ASTAssignStmt* assign_stmt = (ASTAssignStmt*)node;
            Symbol* symbol = scope_find(ctx->current_scope, assign_stmt->name);
            if (!symbol || symbol->kind != SYMBOL_VAR) {
                char message[256];
                snprintf(message, sizeof(message), "assignment to undeclared variable '%s'", assign_stmt->name);
                semantic_error_at(ctx, node->line, node->column, message);
            }
            LamoType value_type = semantic_infer_expression(ctx, assign_stmt->value);
            // Update the variable's inferred type to the new value. Lamo is
            // dynamically typed, so reassignment with a different type is
            // allowed — but we still want to flag incompatibilities in
            // compound assignment (+=, -=).
            if (symbol) {
                if (assign_stmt->op_type == TOKEN_PLUS_EQ) {
                    // += : if symbol is string, value can be anything (concat).
                    // If symbol is numeric, value must be numeric.
                    if (is_numeric_type(symbol->type) && value_type == LAMO_TYPE_STRING) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "cannot use '+=' to add string to numeric variable '%s' (got %s += %s)",
                                 assign_stmt->name, type_name(symbol->type), type_name(value_type));
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                } else if (assign_stmt->op_type == TOKEN_MINUS_EQ) {
                    // -= : both must be numeric.
                    semantic_check_numeric_operand(ctx, "-=",
                                                   symbol->type, value_type,
                                                   node->line, node->column);
                }
                symbol->type = value_type;
            }
            break;
        }
        case AST_CALL_STMT: {
            ASTCallStmt* call_stmt = (ASTCallStmt*)node;
            semantic_visit_call(ctx, call_stmt->name, call_stmt->args, call_stmt->arg_count, node->line, node->column);
            break;
        }
        case AST_IMPORT:
            // import é resolvido pelo loader antes da análise semântica; nada a
            // validar aqui além da estrutura.
            break;
        default:
            break;
    }
}

// Infer the compile-time type of an expression and run any operator-level
// checks. Visits sub-expressions recursively. Returns LAMO_TYPE_UNKNOWN when
// the type cannot be inferred (e.g. due to a prior error).
static LamoType semantic_infer_expression(SemanticContext* ctx, ASTNode* node) {
    if (!node) {
        return LAMO_TYPE_UNKNOWN;
    }

    switch (node->type) {
        case AST_INT_LITERAL:
            return LAMO_TYPE_INT;
        case AST_FLOAT_LITERAL:
            return LAMO_TYPE_FLOAT;
        case AST_STRING_LITERAL:
            return LAMO_TYPE_STRING;
        case AST_BOOL_LITERAL:
            return LAMO_TYPE_BOOL;
        case AST_IDENTIFIER: {
            ASTIdentifier* identifier = (ASTIdentifier*)node;
            Symbol* symbol = scope_find(ctx->current_scope, identifier->name);
            if (!symbol || symbol->kind != SYMBOL_VAR) {
                // Permite usar builtins como valores? Por enquanto não — apenas
                // como calls. Variáveis precisam estar declaradas.
                char message[256];
                snprintf(message, sizeof(message), "use of undeclared variable '%s'", identifier->name);
                semantic_error_at(ctx, node->line, node->column, message);
                return LAMO_TYPE_UNKNOWN;
            }
            return symbol->type;
        }
        case AST_BINARY_EXPR: {
            ASTBinaryExpr* expr = (ASTBinaryExpr*)node;
            LamoType left = semantic_infer_expression(ctx, expr->left);
            LamoType right = semantic_infer_expression(ctx, expr->right);

            switch (expr->operator) {
                case TOKEN_PLUS:
                    // + aceita string (concat) com qualquer coisa, ou numérico.
                    if (left == LAMO_TYPE_STRING || right == LAMO_TYPE_STRING) {
                        return LAMO_TYPE_STRING;
                    }
                    if (left == LAMO_TYPE_UNKNOWN || right == LAMO_TYPE_UNKNOWN) {
                        return LAMO_TYPE_UNKNOWN;
                    }
                    if (left == LAMO_TYPE_FLOAT || right == LAMO_TYPE_FLOAT) {
                        return LAMO_TYPE_FLOAT;
                    }
                    return LAMO_TYPE_INT;
                case TOKEN_MINUS:
                case TOKEN_STAR:
                case TOKEN_SLASH:
                case TOKEN_PERCENT: {
                    const char* op_name =
                        expr->operator == TOKEN_MINUS ? "-" :
                        expr->operator == TOKEN_STAR  ? "*" :
                        expr->operator == TOKEN_SLASH ? "/" : "%";
                    semantic_check_numeric_operand(ctx, op_name, left, right, node->line, node->column);
                    if (left == LAMO_TYPE_UNKNOWN || right == LAMO_TYPE_UNKNOWN) {
                        return LAMO_TYPE_UNKNOWN;
                    }
                    if (left == LAMO_TYPE_FLOAT || right == LAMO_TYPE_FLOAT) {
                        return LAMO_TYPE_FLOAT;
                    }
                    return LAMO_TYPE_INT;
                }
                case TOKEN_LT:
                case TOKEN_GT:
                case TOKEN_LT_EQ:
                case TOKEN_GT_EQ: {
                    const char* op_name =
                        expr->operator == TOKEN_LT    ? "<" :
                        expr->operator == TOKEN_GT    ? ">" :
                        expr->operator == TOKEN_LT_EQ ? "<=" : ">=";
                    semantic_check_numeric_operand(ctx, op_name, left, right, node->line, node->column);
                    return LAMO_TYPE_BOOL;
                }
                case TOKEN_EQ_EQ:
                case TOKEN_BANG_EQ:
                    // Equality accepts any pair; runtime handles mixed types by returning false.
                    return LAMO_TYPE_BOOL;
                case TOKEN_AND_AND:
                case TOKEN_OR_OR:
                    return LAMO_TYPE_BOOL;
                default:
                    return LAMO_TYPE_UNKNOWN;
            }
        }
        case AST_UNARY_EXPR: {
            ASTUnaryExpr* expr = (ASTUnaryExpr*)node;
            LamoType right = semantic_infer_expression(ctx, expr->right);
            if (expr->operator == TOKEN_MINUS) {
                if (right == LAMO_TYPE_STRING) {
                    semantic_error_at(ctx, node->line, node->column,
                                      "unary '-' does not support string operand");
                }
                return right; // -int -> int, -float -> float
            }
            if (expr->operator == TOKEN_BANG) {
                return LAMO_TYPE_BOOL;
            }
            return LAMO_TYPE_UNKNOWN;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* call_expr = (ASTCallExpr*)node;
            return semantic_visit_call(ctx, call_expr->name, call_expr->args, call_expr->arg_count, node->line, node->column);
        }
        case AST_GROUPING_EXPR:
            return semantic_infer_expression(ctx, ((ASTGroupingExpr*)node)->expression);
        default:
            // Recurse into statement-shaped nodes that can appear inside
            // expressions via legacy AST types we still keep for compat.
            semantic_visit_statement(ctx, node);
            return LAMO_TYPE_UNKNOWN;
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
            scope_define(&ctx, ctx.current_scope, fn_decl->name, SYMBOL_FN, fn_decl->param_count, LAMO_TYPE_UNKNOWN, node->line, node->column);
        }
    }

    for (ASTNode* node = program->declarations; node; node = node->next) {
        semantic_visit_statement(&ctx, node);
    }

    scope_free(ctx.current_scope);
    return ctx.errors == 0;
}
