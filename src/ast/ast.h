#ifndef AST_H
#define AST_H

#include "lexer.h"
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
    AST_BREAK_STMT,         // break;  (somente válido dentro de while/for)
    AST_CONTINUE_STMT,      // continue; (somente válido dentro de while/for)
    AST_ASSIGN_STMT,
    AST_CALL_STMT,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_INT_LITERAL,
    AST_FLOAT_LITERAL,
    AST_STRING_LITERAL,
    AST_BOOL_LITERAL,
    AST_IDENTIFIER,
    AST_CALL_EXPR,
    AST_GROUPING_EXPR,
    AST_IMPORT,             // import "path";  (carregado por lamo_v2.c)
    /* Sprint 3: arrays. AST_ARRAY_LITERAL is `[1, 2, 3]`; AST_INDEX_EXPR
     * is `arr[i]`; AST_PROP_EXPR is `arr.len` (the only supported
     * property today, but the node is generic so we can add .push()
     * etc. later without changing the AST shape). */
    AST_ARRAY_LITERAL,
    AST_INDEX_EXPR,
    AST_PROP_EXPR,
    /* Sprint 4: module member call — `module.member(args)`. `object` is
     * the module alias (always AST_IDENTIFIER today), `member_name` is
     * the function name being called inside the module, `args` /
     * `arg_count` are the call arguments. Resolved by the semantic pass
     * against the module registry kept in CompilationState; codegen emits
     * a call to lamo_mod_<alias>__<member_name>. Lives in both statement
     * and expression positions. */
    AST_MEMBER_CALL
} ASTNodeType;

// Estrutura base para todos os nós da AST
typedef struct ASTNode {
    ASTNodeType type;
    int line;
    int column;
    struct ASTNode* next;
    // Bug #5 fix: path do arquivo de origem deste nó. Setado pelo parser no
    // momento de criação (parser_init_with_file). O semântico usa isso para
    // reportar erros com o arquivo correto, mesmo em compilações multi-arquivo
    // (programa principal + imports). Pode ser NULL quando o nó é sintético
    // (ex.: nó criado pelo codegen sem passar pelo parser).
    const char* file_path;
} ASTNode;

typedef struct {
    ASTNode base;
    char* name;
    struct ASTNode* initializer;
    /* Sprint 3: optional type annotation (e.g. `let x: int = 5;`).
     * NULL when no annotation was given. The string is owned by the
     * AST and freed in ast_free(). Possible values: "int", "float",
     * "string", "bool". The semantic pass validates that the
     * initializer's inferred type matches; the codegen ignores it
     * (all Lamo values are LamoValue at runtime). */
    char* type_annotation;
} ASTVarDecl;

typedef struct {
    ASTNode base;
    char* name;
    char** params;
    int param_count;
    /* Sprint 3: optional per-parameter type annotations. NULL when
     * no annotations were given. params_types[i] is the annotation
     * for params[i], or NULL if that parameter had no annotation.
     * The array is owned by the AST and freed in ast_free(). */
    char** param_types;
    /* Sprint 3: optional return-type annotation (e.g. `fn f() -> int`).
     * NULL when no annotation was given. */
    char* return_type_annotation;
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
    char* name;
    struct ASTNode* value;
    LamoTokenType op_type;
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
    LamoTokenType operator;
    struct ASTNode* right;
} ASTBinaryExpr;

typedef struct {
    ASTNode base;
    LamoTokenType operator;
    struct ASTNode* right;
} ASTUnaryExpr;

typedef struct {
    ASTNode base;
    long long value;
} ASTIntLiteral;

typedef struct {
    ASTNode base;
    double value;
} ASTFloatLiteral;

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
    char* path;            // caminho bruto exatamente como apareceu entre aspas
    /* Sprint 4: optional module alias. When non-NULL, the imported file's
     * top-level declarations are renamed to `lamo_mod_<alias>__<name>` and
     * accessed through `<alias>.<name>` syntax (AST_MEMBER_CALL). When NULL,
     * the legacy behavior applies — everything is merged into the global
     * namespace. */
    char* alias;
} ASTImport;

/* Sprint 3: array literal — `[expr, expr, ...]`. elements is owned by
 * the AST (heap-allocated array of ASTNode*). */
typedef struct {
    ASTNode base;
    struct ASTNode** elements;
    int element_count;
} ASTArrayLiteral;

/* Sprint 3: index expression — `array[index]`. Both `array` and `index`
 * are owned by the AST. */
typedef struct {
    ASTNode base;
    struct ASTNode* array;
    struct ASTNode* index;
} ASTIndexExpr;

/* Sprint 3: property access — `obj.prop_name`. Currently prop_name is
 * restricted to "len" by the semantic pass, but the AST is generic. */
typedef struct {
    ASTNode base;
    struct ASTNode* object;
    char* prop_name;
} ASTPropExpr;

/* Sprint 4: module member call — `module.member(args)`. `object` is
 * conventionally an AST_IDENTIFIER naming a module alias declared by an
 * earlier `import "..." as alias;`. `member_name` is the function inside
 * the module. `args` / `arg_count` mirror ASTCallStmt.
 *
 * The same node type is used in both statement and expression positions;
 * codegen and the interpreter dispatch on context. This avoids splitting
 * the node into AST_MEMBER_CALL_STMT / AST_MEMBER_CALL_EXPR the way the
 * legacy AST_CALL_STMT / AST_CALL_EXPR pair was split — a lesson learned
 * the hard way from the call-stmt/call-expr duplication. */
typedef struct {
    ASTNode base;
    struct ASTNode* object;       /* typically AST_IDENTIFIER(name = alias) */
    char* member_name;            /* e.g. "sqrt" in `math.sqrt(x)` */
    struct ASTNode** args;
    int arg_count;
} ASTMemberCall;

typedef struct {
    ASTNode base;
    struct ASTNode* declarations;
} ASTProgram;

ASTNode* ast_new_node(ASTNodeType type, size_t size, int line, int column);
// Bug #5 fix: setters/getters para o "file path default" aplicado a cada nó
// criado pelo parser. O parser chama ast_set_default_file_path() antes de
// começar a parsear um arquivo; ast_new_node() copia o valor para cada nó.
// O ponteiro NÃO é owned pela AST — o caller precisa manter a string viva
// enquanto a AST existir.
void ast_set_default_file_path(const char* path);
const char* ast_get_default_file_path(void);
ASTProgram* ast_new_program();
ASTVarDecl* ast_new_var_decl(char* name, ASTNode* initializer, int line, int column);
ASTVarDecl* ast_new_var_decl_typed(char* name, ASTNode* initializer, char* type_annotation, int line, int column);
ASTFnDecl* ast_new_fn_decl(char* name, char** params, int param_count, ASTNode* body, int line, int column);
ASTFnDecl* ast_new_fn_decl_typed(char* name, char** params, char** param_types, int param_count, char* return_type_annotation, ASTNode* body, int line, int column);
ASTBlock* ast_new_block(ASTNode* statements, int line, int column);
ASTIfStmt* ast_new_if_stmt(ASTNode* condition, ASTNode* then_branch, ASTNode* else_branch, int line, int column);
ASTWhileStmt* ast_new_while_stmt(ASTNode* condition, ASTNode* body, int line, int column);
ASTForStmt* ast_new_for_stmt(ASTNode* initializer, ASTNode* condition, ASTNode* increment, ASTNode* body, int line, int column);
ASTReturnStmt* ast_new_return_stmt(ASTNode* expression, int line, int column);
// break; e continue; — sem campos além da base ASTNode. Os construtores
// retornam ASTNode* direto porque não há struct derivada.
ASTNode* ast_new_break_stmt(int line, int column);
ASTNode* ast_new_continue_stmt(int line, int column);
ASTAssignStmt* ast_new_assign_stmt(char* name, ASTNode* value, LamoTokenType op_type, int line, int column);
ASTCallStmt* ast_new_call_stmt(char* name, ASTNode** args, int arg_count, int line, int column);
ASTBinaryExpr* ast_new_binary_expr(ASTNode* left, LamoTokenType operator, ASTNode* right, int line, int column);
ASTUnaryExpr* ast_new_unary_expr(LamoTokenType operator, ASTNode* right, int line, int column);
ASTIntLiteral* ast_new_int_literal(long long value, int line, int column);
ASTFloatLiteral* ast_new_float_literal(double value, int line, int column);
ASTStringLiteral* ast_new_string_literal(char* value, int line, int column);
ASTBoolLiteral* ast_new_bool_literal(int value, int line, int column);
ASTIdentifier* ast_new_identifier(char* name, int line, int column);
ASTCallExpr* ast_new_call_expr(char* name, ASTNode** args, int arg_count, int line, int column);
ASTGroupingExpr* ast_new_grouping_expr(ASTNode* expression, int line, int column);
ASTImport* ast_new_import_decl(char* path, int line, int column);
/* Sprint 4: variant of ast_new_import_decl that also takes a module alias.
 * `alias` may be NULL — equivalent to the legacy constructor. The alias
 * string is owned by the AST after this call (strdup'd). */
ASTImport* ast_new_import_decl_aliased(char* path, char* alias, int line, int column);
ASTArrayLiteral* ast_new_array_literal(ASTNode** elements, int element_count, int line, int column);
ASTIndexExpr* ast_new_index_expr(ASTNode* array, ASTNode* index, int line, int column);
ASTPropExpr* ast_new_prop_expr(ASTNode* object, char* prop_name, int line, int column);
/* Sprint 4: module member call constructor. Takes ownership of `args`
 * (the array, not the elements — elements are still owned by their
 * constructors as usual). `member_name` is strdup'd. `object` is owned
 * by the AST and freed in ast_free(). */
ASTMemberCall* ast_new_member_call(ASTNode* object, char* member_name, ASTNode** args, int arg_count, int line, int column);
void ast_program_append(ASTProgram* destination, ASTProgram* source);

void ast_free(ASTNode* node);

#endif
