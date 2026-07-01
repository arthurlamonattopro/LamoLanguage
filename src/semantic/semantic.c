#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../builtins.h"
#include "../error_util.h"
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
    LAMO_TYPE_BOOL,
    /* Phase 2: composite types. ARRAY is the dynamic array type from
     * Sprint 3; STRUCT is a user-defined struct. The struct's name is
     * stored separately on the Symbol (struct_name field) since multiple
     * distinct struct types exist. */
    LAMO_TYPE_ARRAY,
    LAMO_TYPE_STRUCT
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
    /* Sprint 2 fix: store the source location of the original declaration so
     * the duplicate-declaration error message can point the user at the
     * previous site, not just the new one. */
    int line;
    int column;
    /* Sprint 4: also store the originating file path so cross-file duplicate
     * declarations include the file name, not just line:col. Pointer is not
     * owned — it points into the AST node's file_path string which lives as
     * long as the ASTProgram. */
    const char* file_path;
    /* Phase 2: when type == LAMO_TYPE_STRUCT, this is the struct's type
     * name (e.g. "Player"). Borrowed pointer into the matching
     * ASTStructDecl->name — NOT owned. NULL for non-struct symbols. */
    const char* struct_name;
    struct Symbol* next;
} Symbol;

typedef struct Scope {
    Symbol* symbols;
    struct Scope* parent;
} Scope;

/* Sprint 3: source lookup callback so semantic_error_at can print the
 * offending source line + caret. The compile_sources() caller registers
 * a function that maps file_path -> source text; in single-file builds
 * we just use the one source we have. */
typedef const char* (*SourceLookupFn)(const char* path, void* user_data);

typedef struct {
    const char* file_path;
    // Bug #5 fix: file_path do nó sendo visitado no momento. Setado em
    // semantic_visit_statement() com base no node->file_path da AST. Usado
    // por semantic_error_at() para reportar erros no arquivo correto.
    const char* last_node_path;
    Scope* current_scope;
    int inside_function;
    // Próximo passo 5: break/continue só são válidos dentro de while/for.
    // Incrementado ao entrar num loop, decrementado ao sair.
    int inside_loop;
    int errors;
    /* Sprint 3: source lookup for error snippets. May be NULL — in that
     * case semantic_error_at just omits the snippet. */
    SourceLookupFn source_lookup;
    void* source_lookup_user_data;
    /* Sprint 4: module-resolution callbacks. May be NULL — in that case
     * AST_MEMBER_CALL nodes always error with "module resolution not
     * available". When set (by lamo_v2.c via semantic_analyze_full),
     * they resolve `alias.member(args)` against the module registry. */
    LamoModuleResolveFn module_resolve;
    LamoModuleArityFn module_arity;
    void* module_user_data;
    /* Return-type tracking: set when entering a function body so that
     * AST_RETURN_STMT can validate the returned expression against the
     * declared (or inferred) return type. LAMO_TYPE_UNKNOWN means either
     * we are not inside a function or the return type could not be
     * determined (no annotation, no inferrable body). */
    LamoType current_fn_return_type;
    const char* current_fn_name;   /* for error messages; may be NULL */
    /* Phase 2: struct/enum/method registries.
     *
     * struct_defs: linked list of all AST_STRUCT_DECL nodes seen at top
     *   level. Used to look up field indices for AST_STRUCT_LITERAL,
     *   AST_PROP_EXPR (field access), AST_MEMBER_CALL (method call),
     *   and AST_PLACE_ASSIGN_STMT (field assignment).
     *
     * enum_defs: linked list of all AST_ENUM_DECL nodes. Used to resolve
     *   match-arm patterns and to register variant names as int constants.
     *
     * impl_defs: linked list of all AST_IMPL_DECL nodes. Used to look up
     *   methods by struct name + method name. */
    ASTNode* struct_defs;
    ASTNode* enum_defs;
    ASTNode* impl_defs;
    /* Phase 2: when visiting an impl block, this is set to the struct
     * name so that `self` references inside method bodies can be
     * resolved. NULL outside of impl method bodies. */
    const char* current_impl_struct;
} SemanticContext;

static void semantic_visit_statement(SemanticContext* ctx, ASTNode* node);
static LamoType semantic_infer_expression(SemanticContext* ctx, ASTNode* node);
/* Sprint 4: forward declaration so semantic_error_at can delegate to
 * the hinted variant below. */
static void semantic_error_at_hint(SemanticContext* ctx, int line, int column,
                                    const char* message, const char* hint);
/* Sprint 2 refactor: arity lookup is now a single call into builtins.h's
 * lamo_builtin_arity(). The forward declaration is kept so the call sites
 * below don't need to be renamed. */
static int builtin_function_arity(const char* name);
static LamoType builtin_function_return_type(const char* name, ASTNode** args, int arg_count);
static int semantic_validate_builtin_call(SemanticContext* ctx, const char* name, ASTNode** args, int arg_count, int line, int column);

static const char* type_name(LamoType type) {
    switch (type) {
        case LAMO_TYPE_INT:    return "int";
        case LAMO_TYPE_FLOAT:  return "float";
        case LAMO_TYPE_STRING: return "string";
        case LAMO_TYPE_BOOL:   return "bool";
        case LAMO_TYPE_ARRAY:  return "array";
        case LAMO_TYPE_STRUCT: return "struct";
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
    /* Delegate to the hinted variant with no hint. */
    semantic_error_at_hint(ctx, line, column, message, NULL);
}

/* Sprint 4: semantic_error_at with an optional hint. The hint is
 * printed below the source snippet as "hint: <text>". Pass NULL when
 * no hint is appropriate. The hint should give actionable advice —
 * e.g. "did you mean to declare 'x' with `let x = ...;`?". */
static void semantic_error_at_hint(SemanticContext* ctx, int line, int column,
                                    const char* message, const char* hint) {
    // Bug #5 fix: usa o file_path do nó específico que disparou o erro,
    // não o label global do contexto. Em compilações multi-arquivo (programa
    // principal + imports), isso significa que o erro aponta para o arquivo
    // onde o problema realmente está, não para "<multiple inputs>".
    //
    // O caller passa o line/column do nó; aqui nós não recebemos o ponteiro
    // do nó diretamente, mas o ctx->last_node_path é setado por
    // semantic_visit_statement antes de chamar esta função para nós
    // específicos. Se last_node_path for NULL (caso legado), cai no
    // file_path do contexto.
    const char* label = ctx->last_node_path ? ctx->last_node_path :
                        (ctx->file_path ? ctx->file_path : "<input>");
    /* Sprint 4: color the "semantic error" label red+bold on TTY. */
    if (lamo_error_use_color()) {
        fprintf(stderr, "%s:%d:%d: %ssemantic error:%s %s\n",
                label, line, column,
                LAMO_COLOR_BOLD LAMO_COLOR_RED, LAMO_COLOR_RESET, message);
    } else {
        fprintf(stderr, "%s:%d:%d: semantic error: %s\n",
                label, line, column, message);
    }
    /* Sprint 3: print the source line + caret. We ask the registered
     * source-lookup callback for the source text of the current file;
     * if no callback is registered (e.g. semantic_analyze was called
     * directly without going through compile_sources), we just skip
     * the snippet. */
    if (ctx->source_lookup) {
        const char* source = ctx->source_lookup(label, ctx->source_lookup_user_data);
        if (source) {
            error_print_snippet(stderr, source, line, column);
        }
    }
    /* Sprint 4: print the hint below the snippet, if any. */
    error_print_hint(stderr, hint);
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

static void scope_define(SemanticContext* ctx, Scope* scope, const char* name, SymbolKind kind, int arity, LamoType type, int line, int column, const char* file_path) {
    Symbol* existing = scope_find_in_current(scope, name);
    if (existing) {
        /* Sprint 2 fix: report the kind of the previously-declared symbol
         * and its source location, so the user can find the original
         * declaration without grepping.
         * Sprint 4: include the file name in cross-file conflicts. */
        char message[512];
        const char* prev_kind_str = existing->kind == SYMBOL_FN ? "function" : "variable";
        const char* new_kind_str = kind == SYMBOL_FN ? "function" : "variable";
        const char* prev_file = existing->file_path ? existing->file_path : "<unknown>";
        const char* cur_file  = file_path           ? file_path           : "<unknown>";
        int cross_file = (prev_file != cur_file) && (strcmp(prev_file, cur_file) != 0);
        if (cross_file) {
            snprintf(message, sizeof(message),
                     "duplicate declaration of '%s' as %s "
                     "(previously declared as %s in %s at %d:%d)",
                     name, new_kind_str, prev_kind_str, prev_file,
                     existing->line, existing->column);
        } else {
            snprintf(message, sizeof(message),
                     "duplicate declaration of '%s' as %s "
                     "(previously declared as %s at %d:%d)",
                     name, new_kind_str, prev_kind_str,
                     existing->line, existing->column);
        }
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
    symbol->line = line;
    symbol->column = column;
    symbol->file_path = file_path;
    symbol->struct_name = NULL;  /* Phase 2: set by callers via scope_define_struct */
    symbol->next = scope->symbols;
    scope->symbols = symbol;
}

/* Phase 2: define a variable with a known struct type. The struct_name
 * is borrowed from the ASTStructDecl->name (NOT owned). */
static void scope_define_struct_var(SemanticContext* ctx, Scope* scope, const char* name, const char* struct_name, int line, int column, const char* file_path) {
    scope_define(ctx, scope, name, SYMBOL_VAR, 0, LAMO_TYPE_STRUCT, line, column, file_path);
    Symbol* sym = scope_find_in_current(scope, name);
    if (sym) sym->struct_name = struct_name;
}

/* ─── Phase 2: struct / enum / method lookup helpers ───────────────── */

/* Find a struct definition by name. Returns NULL if not found. */
static ASTStructDecl* find_struct_def(SemanticContext* ctx, const char* name) {
    ASTNode* cur;
    if (!name) return NULL;
    for (cur = ctx->struct_defs; cur; cur = cur->next) {
        if (cur->type == AST_STRUCT_DECL) {
            ASTStructDecl* sd = (ASTStructDecl*)cur;
            if (sd->name && strcmp(sd->name, name) == 0) return sd;
        }
    }
    return NULL;
}

/* Find the index of a field in a struct. Returns -1 if not found. */
static int struct_field_index(ASTStructDecl* sd, const char* field_name) {
    int i;
    if (!sd || !field_name) return -1;
    for (i = 0; i < sd->field_count; i++) {
        if (sd->field_names[i] && strcmp(sd->field_names[i], field_name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Find an enum definition by name. Returns NULL if not found. */
static ASTEnumDecl* find_enum_def(SemanticContext* ctx, const char* name) {
    ASTNode* cur;
    if (!name) return NULL;
    for (cur = ctx->enum_defs; cur; cur = cur->next) {
        if (cur->type == AST_ENUM_DECL) {
            ASTEnumDecl* ed = (ASTEnumDecl*)cur;
            if (ed->name && strcmp(ed->name, name) == 0) return ed;
        }
    }
    return NULL;
}

/* Find an enum variant by name across all registered enums. Returns the
 * variant's index (>= 0) via *out_index, and the enum's name via the
 * return value (borrowed pointer). Returns NULL if not found. */
static const char* find_enum_variant_any(SemanticContext* ctx, const char* variant_name, int* out_index) {
    ASTNode* cur;
    if (!variant_name) return NULL;
    for (cur = ctx->enum_defs; cur; cur = cur->next) {
        if (cur->type == AST_ENUM_DECL) {
            ASTEnumDecl* ed = (ASTEnumDecl*)cur;
            int i;
            for (i = 0; i < ed->variant_count; i++) {
                if (ed->variants[i] && strcmp(ed->variants[i], variant_name) == 0) {
                    if (out_index) *out_index = i;
                    return ed->name;
                }
            }
        }
    }
    return NULL;
}

/* Find a method on a struct by name. Returns the AST_FN_DECL node, or
 * NULL if not found. Searches all impl blocks for the given struct. */
static ASTFnDecl* find_method(SemanticContext* ctx, const char* struct_name, const char* method_name) {
    ASTNode* cur;
    if (!struct_name || !method_name) return NULL;
    for (cur = ctx->impl_defs; cur; cur = cur->next) {
        if (cur->type == AST_IMPL_DECL) {
            ASTImplDecl* id = (ASTImplDecl*)cur;
            if (id->struct_name && strcmp(id->struct_name, struct_name) == 0) {
                for (ASTNode* m = id->methods; m; m = m->next) {
                    if (m->type == AST_FN_DECL) {
                        ASTFnDecl* fn = (ASTFnDecl*)m;
                        if (fn->name && strcmp(fn->name, method_name) == 0) {
                            return fn;
                        }
                    }
                }
            }
        }
    }
    return NULL;
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
        char hint[256];
        snprintf(message, sizeof(message), "call to undeclared function '%s'", name);
        snprintf(hint, sizeof(hint),
                 "did you forget to define '%s' (with `fn %s(...) { ... }`) or import it (with `import \"...\" as ...;`)?",
                 name, name);
        semantic_error_at_hint(ctx, line, column, message, hint);
    }

    for (int i = 0; i < arg_count; i++) {
        semantic_infer_expression(ctx, args[i]);
    }
    return return_type;
}

// Sprint 2 refactor: the per-builtin arity and return-type logic now lives
// in src/builtins.h as a single shared table. The two wrappers below are
// thin adapters that preserve the old call-site names while delegating to
// the table. Adding a new builtin only requires editing builtins.h (plus
// the codegen site that emits the call).
static int builtin_function_arity(const char* name) {
    return lamo_builtin_arity(name);
}

// Return type for each builtin. Reads the BuiltinRetPolicy from the table;
// for BUILTIN_RET_MIRROR_ARG0 (abs), inspects args[0] to guess int vs float.
static LamoType builtin_function_return_type(const char* name, ASTNode** args, int arg_count) {
    const BuiltinInfo* info = lamo_builtin_lookup(name);
    if (!info) {
        return LAMO_TYPE_UNKNOWN;
    }
    switch (info->ret_policy) {
        case BUILTIN_RET_INT:
            return LAMO_TYPE_INT;
        case BUILTIN_RET_STRING:
            return LAMO_TYPE_STRING;
        case BUILTIN_RET_BOOL:
            return LAMO_TYPE_BOOL;
        case BUILTIN_RET_MIRROR_ARG0:
            /* abs(): if the argument is a float literal, the result is float;
             * otherwise we conservatively report int. Real type inference of
             * the argument expression happens via semantic_infer_expression
             * when the args are visited, so by the time we get here, simple
             * cases are already represented in the AST node types. */
            if (arg_count >= 1 && args[0] && args[0]->type == AST_FLOAT_LITERAL) {
                return LAMO_TYPE_FLOAT;
            }
            return LAMO_TYPE_INT;
    }
    return LAMO_TYPE_UNKNOWN;
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

/* Sprint 3: map a type-annotation string ("int", "float", "string", "bool")
 * to the internal LamoType enum. Returns LAMO_TYPE_UNKNOWN for unknown
 * names so the caller can emit a single clear error.
 *
 * Phase 2: also recognizes user-defined struct names. Since structs are
 * registered during the first pre-pass, we can resolve struct-name
 * annotations here. We pass the SemanticContext so we can look up the
 * struct registry. */
static LamoType annotation_to_type_with_ctx(SemanticContext* ctx, const char* annotation) {
    if (!annotation) return LAMO_TYPE_UNKNOWN;
    if (strcmp(annotation, "int") == 0) return LAMO_TYPE_INT;
    if (strcmp(annotation, "float") == 0) return LAMO_TYPE_FLOAT;
    if (strcmp(annotation, "string") == 0) return LAMO_TYPE_STRING;
    if (strcmp(annotation, "bool") == 0) return LAMO_TYPE_BOOL;
    if (strcmp(annotation, "array") == 0) return LAMO_TYPE_ARRAY;
    /* Phase 2: struct-name annotation. */
    if (ctx && find_struct_def(ctx, annotation)) return LAMO_TYPE_STRUCT;
    return LAMO_TYPE_UNKNOWN;
}

/* Legacy wrapper that doesn't take a context — kept for compatibility with
 * any callers that don't have a SemanticContext. Loses struct-name resolution.
 * Currently unused (the with_ctx variant is the one actually called), but
 * kept as a public-ish API for future use. The __attribute__((unused)) silences
 * -Wunused-function without removing the function. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static LamoType annotation_to_type(const char* annotation) {
    return annotation_to_type_with_ctx(NULL, annotation);
}

/* Infer the return type of a function body by scanning all direct
 * AST_RETURN_STMT nodes (without descending into nested function
 * declarations). Used when there is no return-type annotation to give
 * the call site a more useful type than UNKNOWN.
 *
 * Algorithm: collect every return-expression type; if all agree on a
 * single type, that becomes the inferred type. If they conflict, or if
 * there are no return statements, returns LAMO_TYPE_UNKNOWN.
 *
 * Note: this is a shallow scan — it does NOT call the full
 * semantic_infer_expression (which would require a valid context with
 * the function's parameter scope). Instead it uses a lightweight
 * literal-only inference that handles the common cases (returning a
 * literal, a variable declared as a known type, or a builtin call).
 * For complex expressions the result is UNKNOWN, which is safe. */
static LamoType infer_fn_return_type_from_body(ASTNode* node, int depth) {
    if (!node || depth > 32) return LAMO_TYPE_UNKNOWN;

    if (node->type == AST_RETURN_STMT) {
        ASTReturnStmt* ret = (ASTReturnStmt*)node;
        if (!ret->expression) return LAMO_TYPE_UNKNOWN; /* bare return */
        switch (ret->expression->type) {
            case AST_INT_LITERAL:    return LAMO_TYPE_INT;
            case AST_FLOAT_LITERAL:  return LAMO_TYPE_FLOAT;
            case AST_STRING_LITERAL: return LAMO_TYPE_STRING;
            case AST_BOOL_LITERAL:   return LAMO_TYPE_BOOL;
            default:                 return LAMO_TYPE_UNKNOWN;
        }
    }

    /* Don't descend into nested fn declarations. */
    if (node->type == AST_FN_DECL) return LAMO_TYPE_UNKNOWN;

    LamoType found = LAMO_TYPE_UNKNOWN;

    /* Walk children based on node type. */
    ASTNode* children[8];
    int n = 0;
    switch (node->type) {
        case AST_BLOCK: {
            ASTNode* s = ((ASTBlock*)node)->statements;
            while (s && n < 8) { children[n++] = s; s = s->next; }
            break;
        }
        case AST_IF_STMT: {
            ASTIfStmt* is = (ASTIfStmt*)node;
            children[n++] = is->then_branch;
            if (is->else_branch) children[n++] = is->else_branch;
            break;
        }
        case AST_WHILE_STMT:
            children[n++] = ((ASTWhileStmt*)node)->body; break;
        case AST_FOR_STMT:
            children[n++] = ((ASTForStmt*)node)->body; break;
        default: break;
    }

    for (int i = 0; i < n; i++) {
        LamoType t = infer_fn_return_type_from_body(children[i], depth + 1);
        if (t == LAMO_TYPE_UNKNOWN) continue;
        if (found == LAMO_TYPE_UNKNOWN) { found = t; continue; }
        if (found != t) return LAMO_TYPE_UNKNOWN; /* conflict */
    }

    /* Also walk the ->next siblings at this level (statement lists). */
    if (node->next) {
        LamoType t = infer_fn_return_type_from_body(node->next, depth);
        if (t != LAMO_TYPE_UNKNOWN) {
            if (found == LAMO_TYPE_UNKNOWN) found = t;
            else if (found != t) return LAMO_TYPE_UNKNOWN;
        }
    }

    return found;
}

static void semantic_visit_statement(SemanticContext* ctx, ASTNode* node) {
    if (!node) {
        return;
    }

    // Bug #5 fix: lembra de qual arquivo veio este nó para que
    // semantic_error_at() possa reportar a origem correta.
    if (node->file_path) {
        ctx->last_node_path = node->file_path;
    }

    switch (node->type) {
        case AST_VAR_DECL: {
            ASTVarDecl* var_decl = (ASTVarDecl*)node;
            LamoType init_type = semantic_infer_expression(ctx, var_decl->initializer);
            const char* inferred_struct_name = NULL;
            /* If the initializer is a struct literal, infer the struct name
             * from the literal itself. This lets `let p = Player {...};`
             * work without requiring a `: Player` annotation. */
            if (var_decl->initializer && var_decl->initializer->type == AST_STRUCT_LITERAL) {
                ASTStructLiteral* sl = (ASTStructLiteral*)var_decl->initializer;
                if (find_struct_def(ctx, sl->struct_name)) {
                    inferred_struct_name = sl->struct_name;
                    init_type = LAMO_TYPE_STRUCT;
                }
            }
            /* If the initializer is an array literal, infer LAMO_TYPE_ARRAY. */
            if (var_decl->initializer && var_decl->initializer->type == AST_ARRAY_LITERAL) {
                init_type = LAMO_TYPE_ARRAY;
            }
            /* Sprint 3: validate type annotation if present. The check is
             * strict: int != float (annotated int with float initializer
             * is an error), and string/bool are entirely separate. The
             * one relaxation: UNKNOWN initializer type (e.g. from a
             * previous error) is accepted to avoid cascading errors. */
            const char* annotated_struct_name = NULL;
            if (var_decl->type_annotation) {
                LamoType annotated = annotation_to_type_with_ctx(ctx, var_decl->type_annotation);
                if (annotated == LAMO_TYPE_UNKNOWN) {
                    char message[256];
                    snprintf(message, sizeof(message),
                             "unknown type annotation '%s' (expected int, float, string, bool, array, or a struct name)",
                             var_decl->type_annotation);
                    semantic_error_at(ctx, node->line, node->column, message);
                } else if (annotated == LAMO_TYPE_STRUCT) {
                    /* The annotation is a struct name; remember it so we
                     * can define the variable with the struct type. */
                    annotated_struct_name = var_decl->type_annotation;
                    /* If the initializer is also a struct literal, validate
                     * the struct names match. */
                    if (inferred_struct_name && strcmp(inferred_struct_name, annotated_struct_name) != 0) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "type annotation '%s' does not match struct literal '%s'",
                                 annotated_struct_name, inferred_struct_name);
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                    init_type = LAMO_TYPE_STRUCT;
                } else if (init_type != LAMO_TYPE_UNKNOWN && init_type != annotated) {
                    /* Allow int initializer for float annotation (numeric
                     * widening) and float initializer for int annotation
                     * (will be truncated at runtime, but is a common
                     * pattern). The strict-check version would reject
                     * both; we err on the side of permissiveness here. */
                    int numeric_compat = (annotated == LAMO_TYPE_INT && init_type == LAMO_TYPE_FLOAT) ||
                                         (annotated == LAMO_TYPE_FLOAT && init_type == LAMO_TYPE_INT);
                    if (!numeric_compat) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "type annotation '%s' does not match inferred type '%s'",
                                 var_decl->type_annotation, type_name(init_type));
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                    init_type = annotated;
                } else {
                    init_type = annotated;
                }
            }
            /* Phase 2: if the variable has a struct type (either from
             * annotation or inferred from a struct literal), define it
             * with the struct name so field access can be validated. */
            if (init_type == LAMO_TYPE_STRUCT) {
                const char* sn = annotated_struct_name ? annotated_struct_name : inferred_struct_name;
                if (sn) {
                    scope_define_struct_var(ctx, ctx->current_scope, var_decl->name, sn, node->line, node->column, node->file_path);
                } else {
                    scope_define(ctx, ctx->current_scope, var_decl->name, SYMBOL_VAR, 0, init_type, node->line, node->column, node->file_path);
                }
            } else {
                scope_define(ctx, ctx->current_scope, var_decl->name, SYMBOL_VAR, 0, init_type, node->line, node->column, node->file_path);
            }
            break;
        }
        case AST_FN_DECL: {
            ASTFnDecl* fn_decl = (ASTFnDecl*)node;
            Scope* parent = ctx->current_scope;
            int previous_inside_function = ctx->inside_function;
            LamoType previous_fn_return_type = ctx->current_fn_return_type;
            const char* previous_fn_name = ctx->current_fn_name;

            ctx->current_scope = scope_push(parent);
            ctx->inside_function = 1;

            for (int i = 0; i < fn_decl->param_count; i++) {
                /* Sprint 3: if the parameter has a type annotation, use
                 * it as the inferred type; otherwise leave UNKNOWN so
                 * the caller's argument type flows in unchanged. We also
                 * validate that the annotation is a known type name.
                 *
                 * Phase 2: also recognize struct-name annotations, so a
                 * function can declare `fn heal(p: Player) { ... }` and
                 * access `p.hp` inside the body. */
                LamoType param_type = LAMO_TYPE_UNKNOWN;
                const char* param_struct_name = NULL;
                if (fn_decl->param_types && fn_decl->param_types[i]) {
                    param_type = annotation_to_type_with_ctx(ctx, fn_decl->param_types[i]);
                    if (param_type == LAMO_TYPE_UNKNOWN) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "unknown type annotation '%s' on parameter '%s' (expected int, float, string, bool, array, or a struct name)",
                                 fn_decl->param_types[i], fn_decl->params[i]);
                        semantic_error_at(ctx, node->line, node->column, message);
                    } else if (param_type == LAMO_TYPE_STRUCT) {
                        param_struct_name = fn_decl->param_types[i];
                    }
                }
                if (param_type == LAMO_TYPE_STRUCT && param_struct_name) {
                    scope_define_struct_var(ctx, ctx->current_scope, fn_decl->params[i], param_struct_name, node->line, node->column, node->file_path);
                } else {
                    scope_define(ctx, ctx->current_scope, fn_decl->params[i], SYMBOL_VAR, 0, param_type, node->line, node->column, node->file_path);
                }
            }

            /* Phase 2: if we're inside an impl block, define `self` as a
             * struct-typed variable so the method body can reference it.
             * Methods in Lamo don't declare `self` as a parameter (it's
             * implicit), so we add it to the local scope here. The codegen
             * emits `self` as the first parameter of the underlying C function. */
            if (ctx->current_impl_struct) {
                scope_define_struct_var(ctx, ctx->current_scope, "self", ctx->current_impl_struct, node->line, node->column, node->file_path);
            }

            /* Look up the symbol we registered for this function so we can
             * update its return type after body inference (if no annotation). */
            Symbol* fn_symbol = scope_find(parent, fn_decl->name);

            /* Set context so AST_RETURN_STMT can validate against us. */
            ctx->current_fn_return_type = fn_symbol ? fn_symbol->type : LAMO_TYPE_UNKNOWN;
            ctx->current_fn_name = fn_decl->name;

            /* If no annotation yet, do a shallow pre-scan of the body to
             * infer a return type before visiting (so call sites that appear
             * later in the same file can benefit from it). Also detect when
             * multiple return statements within the body yield incompatible
             * types — that is a semantic error even without an annotation. */
            if (ctx->current_fn_return_type == LAMO_TYPE_UNKNOWN && fn_decl->body) {
                LamoType inferred = infer_fn_return_type_from_body(fn_decl->body, 0);
                if (inferred != LAMO_TYPE_UNKNOWN) {
                    ctx->current_fn_return_type = inferred;
                    if (fn_symbol) fn_symbol->type = inferred;
                } else {
                    /* infer returned UNKNOWN — either no returns, or a conflict.
                     * Re-scan to detect the conflict so we can emit a good error.
                     * We look for any two concrete-typed return statements that
                     * disagree. Use a simple two-pass: collect first type, then
                     * find one that differs. */
                    LamoType first_ret_type = LAMO_TYPE_UNKNOWN;
                    /* Walk the body looking for literal-typed return stmts. */
                    ASTNode* scan = fn_decl->body;
                    /* For a block, look at its statements directly. */
                    if (scan && scan->type == AST_BLOCK) {
                        scan = ((ASTBlock*)scan)->statements;
                    }
                    /* Find the first typed return. */
                    for (ASTNode* s = scan; s; s = s->next) {
                        if (s->type == AST_RETURN_STMT) {
                            ASTReturnStmt* rs = (ASTReturnStmt*)s;
                            if (rs->expression) {
                                LamoType t = LAMO_TYPE_UNKNOWN;
                                switch (rs->expression->type) {
                                    case AST_INT_LITERAL:    t = LAMO_TYPE_INT;    break;
                                    case AST_FLOAT_LITERAL:  t = LAMO_TYPE_FLOAT;  break;
                                    case AST_STRING_LITERAL: t = LAMO_TYPE_STRING; break;
                                    case AST_BOOL_LITERAL:   t = LAMO_TYPE_BOOL;   break;
                                    default: break;
                                }
                                if (t != LAMO_TYPE_UNKNOWN) {
                                    if (first_ret_type == LAMO_TYPE_UNKNOWN) {
                                        first_ret_type = t;
                                    } else if (first_ret_type != t) {
                                        int nc = (first_ret_type == LAMO_TYPE_INT && t == LAMO_TYPE_FLOAT) ||
                                                 (first_ret_type == LAMO_TYPE_FLOAT && t == LAMO_TYPE_INT);
                                        if (!nc) {
                                            char message[256];
                                            snprintf(message, sizeof(message),
                                                "function '%s' has inconsistent return types: %s and %s",
                                                fn_decl->name, type_name(first_ret_type), type_name(t));
                                            semantic_error_at(ctx, s->line, s->column, message);
                                        }
                                    }
                                }
                            }
                        } else if (s->type == AST_IF_STMT) {
                            /* Descend one level into if/else to catch the common pattern. */
                            ASTIfStmt* is = (ASTIfStmt*)s;
                            ASTNode* branches[2] = { is->then_branch, is->else_branch };
                            for (int b = 0; b < 2; b++) {
                                ASTNode* br = branches[b];
                                if (!br) continue;
                                ASTNode* brs = br->type == AST_BLOCK
                                    ? ((ASTBlock*)br)->statements : br;
                                for (ASTNode* bs = brs; bs; bs = bs->next) {
                                    if (bs->type != AST_RETURN_STMT) continue;
                                    ASTReturnStmt* rs = (ASTReturnStmt*)bs;
                                    if (!rs->expression) continue;
                                    LamoType t = LAMO_TYPE_UNKNOWN;
                                    switch (rs->expression->type) {
                                        case AST_INT_LITERAL:    t = LAMO_TYPE_INT;    break;
                                        case AST_FLOAT_LITERAL:  t = LAMO_TYPE_FLOAT;  break;
                                        case AST_STRING_LITERAL: t = LAMO_TYPE_STRING; break;
                                        case AST_BOOL_LITERAL:   t = LAMO_TYPE_BOOL;   break;
                                        default: break;
                                    }
                                    if (t != LAMO_TYPE_UNKNOWN) {
                                        if (first_ret_type == LAMO_TYPE_UNKNOWN) {
                                            first_ret_type = t;
                                        } else if (first_ret_type != t) {
                                            int nc = (first_ret_type == LAMO_TYPE_INT && t == LAMO_TYPE_FLOAT) ||
                                                     (first_ret_type == LAMO_TYPE_FLOAT && t == LAMO_TYPE_INT);
                                            if (!nc) {
                                                char message[256];
                                                snprintf(message, sizeof(message),
                                                    "function '%s' has inconsistent return types: %s and %s",
                                                    fn_decl->name, type_name(first_ret_type), type_name(t));
                                                semantic_error_at(ctx, bs->line, bs->column, message);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            semantic_visit_statement(ctx, fn_decl->body);

            Scope* finished = ctx->current_scope;
            ctx->current_scope = parent;
            ctx->inside_function = previous_inside_function;
            ctx->current_fn_return_type = previous_fn_return_type;
            ctx->current_fn_name = previous_fn_name;
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
            int prev_in_loop = ctx->inside_loop;
            ctx->inside_loop = 1;
            semantic_visit_statement(ctx, while_stmt->body);
            ctx->inside_loop = prev_in_loop;
            break;
        }
        case AST_FOR_STMT: {
            ASTForStmt* for_stmt = (ASTForStmt*)node;
            Scope* parent = ctx->current_scope;
            ctx->current_scope = scope_push(parent);

            semantic_visit_statement(ctx, for_stmt->initializer);
            semantic_infer_expression(ctx, for_stmt->condition);
            semantic_visit_statement(ctx, for_stmt->increment);
            int prev_in_loop = ctx->inside_loop;
            ctx->inside_loop = 1;
            semantic_visit_statement(ctx, for_stmt->body);
            ctx->inside_loop = prev_in_loop;

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
                LamoType ret_expr_type = semantic_infer_expression(ctx, return_stmt->expression);
                /* Validate against the declared/inferred return type. Skip
                 * when either side is UNKNOWN to avoid cascading errors. */
                if (ctx->current_fn_return_type != LAMO_TYPE_UNKNOWN
                    && ret_expr_type != LAMO_TYPE_UNKNOWN
                    && ret_expr_type != ctx->current_fn_return_type) {
                    /* Allow numeric widening (int ↔ float). */
                    int numeric_compat =
                        (ctx->current_fn_return_type == LAMO_TYPE_FLOAT && ret_expr_type == LAMO_TYPE_INT) ||
                        (ctx->current_fn_return_type == LAMO_TYPE_INT   && ret_expr_type == LAMO_TYPE_FLOAT);
                    if (!numeric_compat) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "function '%s' declared to return %s but this return yields %s",
                                 ctx->current_fn_name ? ctx->current_fn_name : "<unknown>",
                                 type_name(ctx->current_fn_return_type),
                                 type_name(ret_expr_type));
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                }
            }
            break;
        }
        case AST_BREAK_STMT:
            // Próximo passo 5: break é inválido fora de loops. `if` não conta
            // como loop — só while e for incrementam ctx->inside_loop.
            if (!ctx->inside_loop) {
                semantic_error_at(ctx, node->line, node->column,
                                  "break is only valid inside a while or for loop");
            }
            break;
        case AST_CONTINUE_STMT:
            if (!ctx->inside_loop) {
                semantic_error_at(ctx, node->line, node->column,
                                  "continue is only valid inside a while or for loop");
            }
            break;
        case AST_ASSIGN_STMT: {
            ASTAssignStmt* assign_stmt = (ASTAssignStmt*)node;
            Symbol* symbol = scope_find(ctx->current_scope, assign_stmt->name);
            if (!symbol || symbol->kind != SYMBOL_VAR) {
                char message[256];
                char hint[256];
                snprintf(message, sizeof(message), "assignment to undeclared variable '%s'", assign_stmt->name);
                snprintf(hint, sizeof(hint),
                         "did you mean `let %s = ...;`? Lamo requires variables to be declared before assignment.",
                         assign_stmt->name);
                semantic_error_at_hint(ctx, node->line, node->column, message, hint);
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
        case AST_MEMBER_CALL: {
            /* Sprint 4: `module.member(args);` statement. Resolve the
             * alias against the module registry, validate the member
             * exists, and validate call arity. The args are visited for
             * type errors just like a regular call.
             *
             * Phase 2: AST_MEMBER_CALL is also used for value method
             * calls like `arr.push(x)` and `player.damage(10)`. The
             * semantic_infer_expression function dispatches based on the
             * object's inferred type. */
            ASTMemberCall* mc = (ASTMemberCall*)node;
            semantic_infer_expression(ctx, (ASTNode*)mc);  /* reuse the expression-path logic */
            break;
        }
        case AST_IMPORT:
            // import é resolvido pelo loader antes da análise semântica; nada a
            // validar aqui além da estrutura.
            break;
        /* ─── Phase 2: struct / impl / enum / match / place-assign ────── */
        case AST_STRUCT_DECL: {
            /* Already registered during the pre-pass; nothing to visit.
             * We could validate field types here, but the type names are
             * already validated lazily when variables are declared with
             * those types. */
            break;
        }
        case AST_IMPL_DECL: {
            ASTImplDecl* id = (ASTImplDecl*)node;
            /* Validate the struct exists. */
            if (!find_struct_def(ctx, id->struct_name)) {
                char message[256];
                snprintf(message, sizeof(message),
                         "impl for unknown struct '%s' (declare it with `struct %s { ... }` first)",
                         id->struct_name, id->struct_name);
                semantic_error_at(ctx, node->line, node->column, message);
                break;
            }
            /* Set the current impl struct so method bodies can use `self`.
             * Mark each method's AST node with sema_struct_name so codegen
             * knows to (a) emit it with the mangled name `lamo_method_<Type>__<name>`
             * and (b) prepend `self` as the first parameter. We do NOT
             * mangle fn->name in-place — that would break find_method,
             * which looks up methods by their original name. */
            const char* prev_impl = ctx->current_impl_struct;
            ctx->current_impl_struct = id->struct_name;
            for (ASTNode* m = id->methods; m; m = m->next) {
                if (m->type == AST_FN_DECL) {
                    m->sema_struct_name = id->struct_name;
                    semantic_visit_statement(ctx, m);
                }
            }
            ctx->current_impl_struct = prev_impl;
            break;
        }
        case AST_ENUM_DECL: {
            /* Already registered during the pre-pass. Validate variant
             * names are unique within the enum. */
            ASTEnumDecl* ed = (ASTEnumDecl*)node;
            for (int i = 0; i < ed->variant_count; i++) {
                for (int j = i + 1; j < ed->variant_count; j++) {
                    if (strcmp(ed->variants[i], ed->variants[j]) == 0) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "duplicate variant '%s' in enum '%s'",
                                 ed->variants[i], ed->name);
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                }
            }
            break;
        }
        case AST_MATCH_STMT: {
            ASTMatchStmt* ms = (ASTMatchStmt*)node;
            LamoType scrut_type = semantic_infer_expression(ctx, ms->scrutinee);
            /* Validate each arm's pattern. Patterns can be:
             *   - "_" (wildcard) - always matches
             *   - Identifier that names an enum variant
             *   - Integer literal (not yet supported - future work)
             * We check that named patterns correspond to a registered
             * enum variant. Exhaustiveness is checked below. */
            int has_wildcard = 0;
            int total_variants = -1;
            const char* scrut_enum_name = NULL;
            /* If the scrutinee's type is known to be an enum (we'd need
             * to track enum types on Symbols, which we don't currently
             * do for variables - only struct types are tracked). For now,
             * we accept any patterns and check exhaustiveness only when
             * all variants of some enum are listed (heuristic). */
            for (int i = 0; i < ms->arm_count; i++) {
                if (ms->pattern_is_wildcard[i]) {
                    has_wildcard = 1;
                } else {
                    int vidx = -1;
                    const char* ename = find_enum_variant_any(ctx, ms->patterns[i], &vidx);
                    if (!ename) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "match pattern '%s' is not a known enum variant (declare an `enum { ... }` first, or use '_' for wildcard)",
                                 ms->patterns[i]);
                        semantic_error_at(ctx, node->line, node->column, message);
                    } else {
                        /* Track the enum we're matching against. */
                        if (scrut_enum_name == NULL) {
                            scrut_enum_name = ename;
                            total_variants = ((ASTEnumDecl*)find_enum_def(ctx, ename))->variant_count;
                        } else if (strcmp(scrut_enum_name, ename) != 0) {
                            char message[256];
                            snprintf(message, sizeof(message),
                                     "match arm pattern '%s' belongs to enum '%s', but earlier arms matched enum '%s'",
                                     ms->patterns[i], ename, scrut_enum_name);
                            semantic_error_at(ctx, node->line, node->column, message);
                        }
                    }
                }
                /* Visit the arm body. */
                if (ms->bodies[i]) {
                    semantic_visit_statement(ctx, ms->bodies[i]);
                }
            }
            /* Exhaustiveness check: if we know the enum (total_variants > 0)
             * and there's no wildcard, count unique variants. If the count
             * is less than total_variants, warn (but don't error - the user
             * might intentionally not handle all cases). */
            if (!has_wildcard && total_variants > 0) {
                /* Count unique variant names among the patterns. */
                int unique = 0;
                for (int i = 0; i < ms->arm_count; i++) {
                    if (ms->pattern_is_wildcard[i]) continue;
                    int dup = 0;
                    for (int j = 0; j < i; j++) {
                        if (strcmp(ms->patterns[i], ms->patterns[j]) == 0) {
                            dup = 1; break;
                        }
                    }
                    if (!dup) unique++;
                }
                if (unique < total_variants) {
                    /* Emit a warning (not an error - Lamo doesn't have a
                     * separate warning channel, so we use stderr directly). */
                    char message[256];
                    snprintf(message, sizeof(message),
                             "warning: match on enum '%s' is not exhaustive (%d of %d variants covered; add a '_' arm or cover the rest)",
                             scrut_enum_name, unique, total_variants);
                    semantic_error_at(ctx, node->line, node->column, message);
                } else if (unique > total_variants) {
                    /* Duplicate variant - already checked above per-enum. */
                }
            }
            (void)scrut_type;
            break;
        }
        case AST_PLACE_ASSIGN_STMT: {
            ASTPlaceAssignStmt* pa = (ASTPlaceAssignStmt*)node;
            /* Validate the target is AST_INDEX_EXPR or AST_PROP_EXPR. */
            if (!pa->target) break;
            if (pa->target->type == AST_INDEX_EXPR) {
                ASTIndexExpr* ie = (ASTIndexExpr*)pa->target;
                /* The object should be array-typed. We infer its type to
                 * validate, but the codegen will emit lamo_array_set. */
                semantic_infer_expression(ctx, ie->array);
                semantic_infer_expression(ctx, ie->index);
                LamoType value_type = semantic_infer_expression(ctx, pa->value);
                (void)value_type;
            } else if (pa->target->type == AST_PROP_EXPR) {
                ASTPropExpr* pe = (ASTPropExpr*)pa->target;
                /* The object should be struct-typed; the field name must
                 * exist. semantic_infer_expression on a AST_PROP_EXPR
                 * already does this validation. */
                semantic_infer_expression(ctx, (ASTNode*)pe);
                LamoType value_type = semantic_infer_expression(ctx, pa->value);
                (void)value_type;
            } else {
                semantic_error_at(ctx, node->line, node->column,
                                  "invalid assignment target (expected arr[i] or obj.field)");
            }
            break;
        }
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

    // Bug #5 fix: atualiza o path do nó atual. Expression nodes também podem
    // disparar erros (ex.: "use of undeclared variable"), e precisamos do
    // arquivo certo.
    if (node->file_path) {
        ctx->last_node_path = node->file_path;
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
                char hint[256];
                snprintf(message, sizeof(message), "use of undeclared variable '%s'", identifier->name);
                snprintf(hint, sizeof(hint),
                         "did you forget to declare it with `let %s = ...;`?",
                         identifier->name);
                semantic_error_at_hint(ctx, node->line, node->column, message, hint);
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
        case AST_MEMBER_CALL: {
            /* Sprint 4: `module.member(args)` in expression position.
             * Resolve through the module registry; if found, treat like
             * a regular function call (validate arity, visit args). The
             * return type is LAMO_TYPE_UNKNOWN — the module member's
             * body is the prefixed function, and we don't have a cheap
             * way to look up its inferred return type from here. The
             * type-inference downstream will simply treat the result as
             * unknown, which is safe (no cascading errors).
             *
             * Phase 2: AST_MEMBER_CALL is now also used for value method
             * calls — `arr.push(x)`, `arr.len()`, `player.damage(10)`.
             * The dispatch:
             *   - If the object is an identifier that matches a registered
             *     module alias, it's a module call (existing behavior).
             *   - Else if the object is an identifier that resolves to an
             *     array-typed variable, it's an array method call.
             *   - Else if the object is an identifier that resolves to a
             *     struct-typed variable, it's a struct method call.
             *   - Else: error. */
            ASTMemberCall* mc = (ASTMemberCall*)node;
            const char* alias = NULL;
            if (!mc->object) {
                semantic_error_at(ctx, node->line, node->column,
                                  "member call missing object expression");
                return LAMO_TYPE_UNKNOWN;
            }
            /* If the object is an identifier, try module-alias resolution
             * first (Sprint 4 behavior). */
            if (mc->object->type == AST_IDENTIFIER) {
                alias = ((ASTIdentifier*)mc->object)->name;
                if (ctx->module_resolve && ctx->module_resolve(alias, mc->member_name, ctx->module_user_data)) {
                    /* It's a module call. Validate arity and visit args. */
                    if (!ctx->module_arity) {
                        for (int i = 0; i < mc->arg_count; i++) {
                            semantic_infer_expression(ctx, mc->args[i]);
                        }
                        return LAMO_TYPE_UNKNOWN;
                    }
                    int expected_arity = ctx->module_arity(alias, mc->member_name, ctx->module_user_data);
                    if (expected_arity >= 0 && expected_arity != mc->arg_count) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "module member `%s.%s` expects %d argument(s), got %d",
                                 alias, mc->member_name, expected_arity, mc->arg_count);
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                    for (int i = 0; i < mc->arg_count; i++) {
                        semantic_infer_expression(ctx, mc->args[i]);
                    }
                    return LAMO_TYPE_UNKNOWN;
                }
                /* Not a module alias; fall through to value-method-call. */
            }
            /* Phase 2: value method call. Infer the object's type. */
            LamoType obj_type = semantic_infer_expression(ctx, mc->object);
            const char* obj_struct_name = NULL;
            if (mc->object->type == AST_IDENTIFIER) {
                Symbol* sym = scope_find(ctx->current_scope, ((ASTIdentifier*)mc->object)->name);
                if (sym && sym->kind == SYMBOL_VAR) {
                    obj_struct_name = sym->struct_name;
                }
            }
            if (obj_type == LAMO_TYPE_ARRAY || (obj_type == LAMO_TYPE_UNKNOWN && !obj_struct_name)) {
                /* Array method call: .push, .pop, .len. */
                if (strcmp(mc->member_name, "push") == 0) {
                    if (mc->arg_count != 1) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "array method `push` expects 1 argument, got %d", mc->arg_count);
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                } else if (strcmp(mc->member_name, "pop") == 0) {
                    if (mc->arg_count != 0) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "array method `pop` expects 0 arguments, got %d", mc->arg_count);
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                } else if (strcmp(mc->member_name, "len") == 0) {
                    if (mc->arg_count != 0) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "array method `len` expects 0 arguments, got %d", mc->arg_count);
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                } else {
                    /* Phase 3 (stdlib): when the object's type is UNKNOWN
                     * (e.g. returned by a module call we can't statically
                     * resolve), don't error on unknown methods — the actual
                     * struct method dispatch happens at runtime via the
                     * codegen. Only error if we KNOW it's an array. */
                    if (obj_type == LAMO_TYPE_ARRAY) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "arrays have no method '%s' (valid: push, pop, len)",
                                 mc->member_name);
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                    /* For UNKNOWN objects, accept and let runtime/codegen
                     * handle it. Mark the node so codegen knows to attempt
                     * struct method dispatch. */
                }
                /* Visit args. */
                for (int i = 0; i < mc->arg_count; i++) {
                    semantic_infer_expression(ctx, mc->args[i]);
                }
                /* Return type: push/pop return int (or the popped value's
                 * type for pop, but we conservatively say UNKNOWN); len
                 * returns int. */
                if (strcmp(mc->member_name, "len") == 0) return LAMO_TYPE_INT;
                return LAMO_TYPE_UNKNOWN;
            }
            if (obj_type == LAMO_TYPE_STRUCT && obj_struct_name) {
                /* Struct method call. */
                ASTFnDecl* method = find_method(ctx, obj_struct_name, mc->member_name);
                if (!method) {
                    char message[256];
                    snprintf(message, sizeof(message),
                             "struct '%s' has no method '%s'",
                             obj_struct_name, mc->member_name);
                    semantic_error_at(ctx, node->line, node->column, message);
                } else {
                    /* Validate arity: method's param_count + 1 (for self)
                     * should equal arg_count + 1 = the actual number of
                     * values we'll pass (self + args). So args should
                     * equal method->param_count. */
                    if (method->param_count != mc->arg_count) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "method '%s.%s' expects %d argument(s), got %d",
                                 obj_struct_name, mc->member_name, method->param_count, mc->arg_count);
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                }
                /* Annotate the AST node so codegen knows the struct type. */
                node->sema_struct_name = obj_struct_name;
                /* Also annotate the object identifier for codegen. */
                mc->object->sema_struct_name = obj_struct_name;
                for (int i = 0; i < mc->arg_count; i++) {
                    semantic_infer_expression(ctx, mc->args[i]);
                }
                return LAMO_TYPE_UNKNOWN;  /* method return type unknown */
            }
            /* Object is not array, not struct, not module. */
            {
                char message[256];
                snprintf(message, sizeof(message),
                         "cannot call method '%s' on value of type '%s' (only arrays and structs have methods)",
                         mc->member_name, type_name(obj_type));
                semantic_error_at(ctx, node->line, node->column, message);
                for (int i = 0; i < mc->arg_count; i++) {
                    semantic_infer_expression(ctx, mc->args[i]);
                }
                return LAMO_TYPE_UNKNOWN;
            }
        }
        case AST_GROUPING_EXPR:
            return semantic_infer_expression(ctx, ((ASTGroupingExpr*)node)->expression);
        /* ─── Phase 2: composite expression types ───────────────────── */
        case AST_ARRAY_LITERAL: {
            ASTArrayLiteral* arr = (ASTArrayLiteral*)node;
            for (int i = 0; i < arr->element_count; i++) {
                semantic_infer_expression(ctx, arr->elements[i]);
            }
            return LAMO_TYPE_ARRAY;
        }
        case AST_INDEX_EXPR: {
            ASTIndexExpr* ie = (ASTIndexExpr*)node;
            LamoType arr_type = semantic_infer_expression(ctx, ie->array);
            semantic_infer_expression(ctx, ie->index);
            /* If the array is a struct, indexing doesn't make sense. */
            if (arr_type == LAMO_TYPE_STRUCT) {
                semantic_error_at(ctx, node->line, node->column,
                                  "cannot index into a struct value (use .field access instead)");
            }
            /* Indexing an array returns the element type, which we can't
             * know statically (arrays are heterogeneous). Return UNKNOWN. */
            return LAMO_TYPE_UNKNOWN;
        }
        case AST_PROP_EXPR: {
            ASTPropExpr* pe = (ASTPropExpr*)node;
            /* Phase 3 (stdlib): if the object is a registered module alias
             * and the property is a registered member (variable), accept
             * the access. This is what makes `math.PI` work after
             * `import std.math as math`. The codegen will emit a reference
             * to the prefixed global `lamo_mod_<alias>__<member>`. */
            if (pe->object && pe->object->type == AST_IDENTIFIER) {
                const char* alias = ((ASTIdentifier*)pe->object)->name;
                if (ctx->module_resolve && ctx->module_resolve(alias, pe->prop_name, ctx->module_user_data)) {
                    /* It's a module variable. Mark the node so codegen can
                     * find the alias without re-doing the lookup. We store
                     * the alias on sema_struct_name (a string ptr field
                     * already on every AST node) as a side channel — the
                     * codegen will recognize this convention for prop_expr
                     * nodes only. */
                    node->sema_struct_name = alias;
                    return LAMO_TYPE_UNKNOWN;
                }
            }
            LamoType obj_type = semantic_infer_expression(ctx, pe->object);
            const char* obj_struct_name = NULL;
            if (pe->object->type == AST_IDENTIFIER) {
                Symbol* sym = scope_find(ctx->current_scope, ((ASTIdentifier*)pe->object)->name);
                if (sym && sym->kind == SYMBOL_VAR) {
                    obj_struct_name = sym->struct_name;
                }
            }
            /* Case 1: array.len (existing behavior). */
            if (obj_type == LAMO_TYPE_ARRAY || (obj_type == LAMO_TYPE_UNKNOWN && !obj_struct_name)) {
                if (strcmp(pe->prop_name, "len") == 0) {
                    return LAMO_TYPE_INT;
                }
                /* Unknown property on an array/unknown-typed value. If the
                 * object is unknown, don't error (could be a module alias
                 * that's checked elsewhere). If it's an array, error. */
                if (obj_type == LAMO_TYPE_ARRAY) {
                    char message[256];
                    snprintf(message, sizeof(message),
                             "arrays have no property '%s' (did you mean .len?)",
                             pe->prop_name);
                    semantic_error_at(ctx, node->line, node->column, message);
                }
                return LAMO_TYPE_UNKNOWN;
            }
            /* Case 2: struct field access. */
            if (obj_type == LAMO_TYPE_STRUCT && obj_struct_name) {
                ASTStructDecl* sd = find_struct_def(ctx, obj_struct_name);
                int idx = struct_field_index(sd, pe->prop_name);
                if (idx < 0) {
                    char message[256];
                    snprintf(message, sizeof(message),
                             "struct '%s' has no field '%s'",
                             obj_struct_name, pe->prop_name);
                    semantic_error_at(ctx, node->line, node->column, message);
                }
                /* Annotate the AST node so codegen knows the struct type
                 * and can look up the field index. */
                node->sema_struct_name = obj_struct_name;
                pe->object->sema_struct_name = obj_struct_name;
                /* Field type is UNKNOWN (we don't track per-field types
                 * yet). Return UNKNOWN. */
                return LAMO_TYPE_UNKNOWN;
            }
            /* Object is a string, int, etc. - no properties. */
            {
                char message[256];
                snprintf(message, sizeof(message),
                         "value of type '%s' has no property '%s'",
                         type_name(obj_type), pe->prop_name);
                semantic_error_at(ctx, node->line, node->column, message);
                return LAMO_TYPE_UNKNOWN;
            }
        }
        case AST_STRUCT_LITERAL: {
            ASTStructLiteral* sl = (ASTStructLiteral*)node;
            ASTStructDecl* sd = find_struct_def(ctx, sl->struct_name);
            if (!sd) {
                char message[256];
                snprintf(message, sizeof(message),
                         "unknown struct type '%s' (declare it with `struct %s { ... }` first)",
                         sl->struct_name, sl->struct_name);
                semantic_error_at(ctx, node->line, node->column, message);
                /* Still visit field values for cascading errors. */
                for (int i = 0; i < sl->field_count; i++) {
                    semantic_infer_expression(ctx, sl->field_values[i]);
                }
                return LAMO_TYPE_UNKNOWN;
            }
            /* Validate each field name exists in the struct. */
            for (int i = 0; i < sl->field_count; i++) {
                int idx = struct_field_index(sd, sl->field_names[i]);
                if (idx < 0) {
                    char message[256];
                    snprintf(message, sizeof(message),
                             "struct '%s' has no field '%s'",
                             sl->struct_name, sl->field_names[i]);
                    semantic_error_at(ctx, node->line, node->column, message);
                }
                semantic_infer_expression(ctx, sl->field_values[i]);
            }
            /* Check that all struct fields are covered (warning, not error). */
            if (sl->field_count < sd->field_count) {
                /* Find a missing field and report it. */
                for (int i = 0; i < sd->field_count; i++) {
                    int found = 0;
                    for (int j = 0; j < sl->field_count; j++) {
                        if (strcmp(sd->field_names[i], sl->field_names[j]) == 0) {
                            found = 1; break;
                        }
                    }
                    if (!found) {
                        char message[256];
                        snprintf(message, sizeof(message),
                                 "warning: struct '%s' field '%s' not set in literal (defaults to 0)",
                                 sl->struct_name, sd->field_names[i]);
                        semantic_error_at(ctx, node->line, node->column, message);
                    }
                }
            }
            /* Annotate the AST node so codegen knows the struct type. */
            node->sema_struct_name = sl->struct_name;
            return LAMO_TYPE_STRUCT;
        }
        default:
            // Recurse into statement-shaped nodes that can appear inside
            // expressions via legacy AST types we still keep for compat.
            semantic_visit_statement(ctx, node);
            return LAMO_TYPE_UNKNOWN;
    }
}

int semantic_analyze_with_source_lookup(ASTProgram* program, const char* file_path,
                                        LamoSourceLookupFn lookup, void* user_data) {
    /* Delegate to the full entry point with NULL module callbacks —
     * keeps the Sprint 3 signature compatible. */
    return semantic_analyze_full(program, file_path, lookup, user_data,
                                  NULL, NULL, NULL);
}

/* Sprint 4: full entry point with module-resolution callbacks. The
 * source-lookup and module callbacks may all be NULL; the semantic pass
 * just skips the corresponding features. */
int semantic_analyze_full(ASTProgram* program, const char* file_path,
                          LamoSourceLookupFn src_lookup, void* src_user_data,
                          LamoModuleResolveFn mod_resolve,
                          LamoModuleArityFn mod_arity,
                          void* mod_user_data) {
    SemanticContext ctx;
    ctx.file_path = file_path;
    ctx.last_node_path = NULL;  // Bug #5 fix: preenchido por node->file_path
    ctx.current_scope = scope_push(NULL);
    ctx.inside_function = 0;
    ctx.inside_loop = 0;        // Próximo passo 5: break/continue tracking
    ctx.errors = 0;
    /* Sprint 3: source lookup for error snippets. May be NULL — in that
     * case semantic_error_at just omits the snippet. */
    ctx.source_lookup = src_lookup;
    ctx.source_lookup_user_data = src_user_data;
    /* Sprint 4: module-resolution callbacks. May be NULL — in that case
     * AST_MEMBER_CALL nodes always error. */
    ctx.module_resolve = mod_resolve;
    ctx.module_arity = mod_arity;
    ctx.module_user_data = mod_user_data;
    /* Return-type tracking: UNKNOWN at top level (not inside any function). */
    ctx.current_fn_return_type = LAMO_TYPE_UNKNOWN;
    ctx.current_fn_name = NULL;
    /* Phase 2: initialize struct/enum/impl registries. We point them at
     * program->declarations and walk that list, filtering by node type,
     * in the lookup helpers (find_struct_def, find_enum_def, find_method).
     * This avoids the need for separate linked lists (and the resulting
     * corruption of node->next). */
    ctx.struct_defs = program->declarations;
    ctx.enum_defs = program->declarations;
    ctx.impl_defs = program->declarations;
    ctx.current_impl_struct = NULL;

    /* Phase 2: register enum variants as global int constants. Each
     * variant becomes a SYMBOL_VAR with type INT and a known value (its
     * index). The codegen emits these as global LamoValue variables. */
    for (ASTNode* node = program->declarations; node; node = node->next) {
        if (node->type == AST_ENUM_DECL) {
            ASTEnumDecl* ed = (ASTEnumDecl*)node;
            for (int i = 0; i < ed->variant_count; i++) {
                /* Register the variant name as a global int constant. */
                scope_define(&ctx, ctx.current_scope, ed->variants[i], SYMBOL_VAR, 0, LAMO_TYPE_INT, node->line, node->column, node->file_path);
            }
        }
    }

    for (ASTNode* node = program->declarations; node; node = node->next) {
        if (node->type == AST_FN_DECL) {
            ASTFnDecl* fn_decl = (ASTFnDecl*)node;
            /* Sprint 3: if the function has a return-type annotation, use
             * it as the inferred return type. Otherwise leave UNKNOWN
             * (the call site will infer from the call context, or stay
             * UNKNOWN if there's no context). */
            LamoType ret_type = LAMO_TYPE_UNKNOWN;
            if (fn_decl->return_type_annotation) {
                ret_type = annotation_to_type_with_ctx(&ctx, fn_decl->return_type_annotation);
                if (ret_type == LAMO_TYPE_UNKNOWN) {
                    char message[256];
                    snprintf(message, sizeof(message),
                             "unknown return type annotation '%s' on function '%s' (expected int, float, string, bool, array, or a struct name)",
                             fn_decl->return_type_annotation, fn_decl->name);
                    semantic_error_at(&ctx, node->line, node->column, message);
                }
            }
            scope_define(&ctx, ctx.current_scope, fn_decl->name, SYMBOL_FN, fn_decl->param_count, ret_type, node->line, node->column, node->file_path);
        }
    }

    for (ASTNode* node = program->declarations; node; node = node->next) {
        semantic_visit_statement(&ctx, node);
    }

    scope_free(ctx.current_scope);
    return ctx.errors == 0;
}

int semantic_analyze(ASTProgram* program, const char* file_path) {
    /* Backwards-compatible entry point: no source lookup, so error
     * messages will not include a source snippet. */
    return semantic_analyze_with_source_lookup(program, file_path, NULL, NULL);
}
