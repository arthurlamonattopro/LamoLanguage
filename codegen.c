#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int indent_level = 0;

static void print_indent(FILE* out) {
    for (int i = 0; i < indent_level; i++) {
        fprintf(out, "    ");
    }
}

static void generate_statement_code(ASTNode* node, FILE* out);
static void generate_expression_code(ASTNode* node, FILE* out);

static const char* op_to_str(TokenType type) {
    switch (type) {
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_PERCENT: return "%%";
        case TOKEN_EQ_EQ: return "==";
        case TOKEN_BANG_EQ: return "!=";
        case TOKEN_LT: return "<";
        case TOKEN_GT: return ">";
        case TOKEN_LT_EQ: return "<=";
        case TOKEN_GT_EQ: return ">=";
        case TOKEN_AND_AND: return "&&";
        case TOKEN_OR_OR: return "||";
        case TOKEN_BANG: return "!";
        case TOKEN_EQUALS: return "=";
        case TOKEN_PLUS_EQ: return "+=";
        case TOKEN_MINUS_EQ: return "-=";
        default: return "??";
    }
}

void generate_c_code(ASTNode* node, FILE* out) {
    if (!node) return;

    fprintf(out, "// Código gerado por Lamo v2 (via AST)\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include <string.h>\n\n");

    // Protótipos de funções primeiro
    ASTNode* current = ((ASTProgram*)node)->declarations;
    while (current) {
        if (current->type == AST_FN_DECL) {
            ASTFnDecl* fn_decl = (ASTFnDecl*)current;
            fprintf(out, "int %s(", fn_decl->name);
            for (int i = 0; i < fn_decl->param_count; i++) {
                if (i > 0) fprintf(out, ", ");
                fprintf(out, "int %s", fn_decl->params[i]);
            }
            fprintf(out, ");\n");
        }
        current = current->next;
    }
    fprintf(out, "\n");

    // Definições de funções
    current = ((ASTProgram*)node)->declarations;
    while (current) {
        if (current->type == AST_FN_DECL) {
            generate_statement_code(current, out);
            fprintf(out, "\n");
        }
        current = current->next;
    }

    fprintf(out, "int main() {\n");
    indent_level++;

    current = ((ASTProgram*)node)->declarations;
    while (current) {
        if (current->type != AST_FN_DECL) {
            generate_statement_code(current, out);
        }
        current = current->next;
    }

    indent_level--;
    fprintf(out, "    return 0;\n}\n");
}

static void generate_statement_code(ASTNode* node, FILE* out) {
    if (!node) return;

    if (node->type != AST_BLOCK) print_indent(out);

    switch (node->type) {
        case AST_VAR_DECL: {
            ASTVarDecl* var_decl = (ASTVarDecl*)node;
            fprintf(out, "int %s = ", var_decl->name);
            generate_expression_code(var_decl->initializer, out);
            fprintf(out, ";\n");
            break;
        }
        case AST_FN_DECL: {
            ASTFnDecl* fn_decl = (ASTFnDecl*)node;
            fprintf(out, "int %s(", fn_decl->name);
            for (int i = 0; i < fn_decl->param_count; i++) {
                if (i > 0) fprintf(out, ", ");
                fprintf(out, "int %s", fn_decl->params[i]);
            }
            fprintf(out, ") ");
            generate_statement_code(fn_decl->body, out);
            break;
        }
        case AST_BLOCK: {
            ASTBlock* block = (ASTBlock*)node;
            fprintf(out, "{\n");
            indent_level++;
            ASTNode* current = block->statements;
            while (current) {
                generate_statement_code(current, out);
                current = current->next;
            }
            indent_level--;
            print_indent(out);
            fprintf(out, "}\n");
            break;
        }
        case AST_IF_STMT: {
            ASTIfStmt* if_stmt = (ASTIfStmt*)node;
            fprintf(out, "if (");
            generate_expression_code(if_stmt->condition, out);
            fprintf(out, ") ");
            generate_statement_code(if_stmt->then_branch, out);
            if (if_stmt->else_branch) {
                print_indent(out);
                fprintf(out, "else ");
                generate_statement_code(if_stmt->else_branch, out);
            }
            break;
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* while_stmt = (ASTWhileStmt*)node;
            fprintf(out, "while (");
            generate_expression_code(while_stmt->condition, out);
            fprintf(out, ") ");
            generate_statement_code(while_stmt->body, out);
            break;
        }
        case AST_FOR_STMT: {
            ASTForStmt* for_stmt = (ASTForStmt*)node;
            fprintf(out, "for (");
            if (for_stmt->initializer) {
                if (for_stmt->initializer->type == AST_VAR_DECL) {
                    ASTVarDecl* vd = (ASTVarDecl*)for_stmt->initializer;
                    fprintf(out, "int %s = ", vd->name);
                    generate_expression_code(vd->initializer, out);
                } else if (for_stmt->initializer->type == AST_ASSIGN_STMT) {
                    ASTAssignStmt* as = (ASTAssignStmt*)for_stmt->initializer;
                    fprintf(out, "%s %s ", as->name, op_to_str(as->op_type));
                    generate_expression_code(as->value, out);
                }
            }
            fprintf(out, "; ");
            if (for_stmt->condition) generate_expression_code(for_stmt->condition, out);
            fprintf(out, "; ");
            if (for_stmt->increment) {
                ASTAssignStmt* as = (ASTAssignStmt*)for_stmt->increment;
                fprintf(out, "%s %s ", as->name, op_to_str(as->op_type));
                generate_expression_code(as->value, out);
            }
            fprintf(out, ") ");
            generate_statement_code(for_stmt->body, out);
            break;
        }
        case AST_RETURN_STMT: {
            ASTReturnStmt* ret_stmt = (ASTReturnStmt*)node;
            fprintf(out, "return ");
            generate_expression_code(ret_stmt->expression, out);
            fprintf(out, ";\n");
            break;
        }
        case AST_PRINT_STMT: {
            ASTPrintStmt* print_stmt = (ASTPrintStmt*)node;
            if (print_stmt->expression->type == AST_STRING_LITERAL) {
                fprintf(out, "printf(\"%%s\\n\", ");
            } else {
                fprintf(out, "printf(\"%%d\\n\", ");
            }
            generate_expression_code(print_stmt->expression, out);
            fprintf(out, ");\n");
            break;
        }
        case AST_ASSIGN_STMT: {
            ASTAssignStmt* assign_stmt = (ASTAssignStmt*)node;
            fprintf(out, "%s %s ", assign_stmt->name, op_to_str(assign_stmt->op_type));
            generate_expression_code(assign_stmt->value, out);
            fprintf(out, ";\n");
            break;
        }
        case AST_CALL_STMT: {
            ASTCallStmt* call_stmt = (ASTCallStmt*)node;
            fprintf(out, "%s(", call_stmt->name);
            for (int i = 0; i < call_stmt->arg_count; i++) {
                if (i > 0) fprintf(out, ", ");
                generate_expression_code(call_stmt->args[i], out);
            }
            fprintf(out, ");\n");
            break;
        }
        default: break;
    }
}

static void generate_expression_code(ASTNode* node, FILE* out) {
    if (!node) return;

    switch (node->type) {
        case AST_INT_LITERAL:
            fprintf(out, "%d", ((ASTIntLiteral*)node)->value);
            break;
        case AST_STRING_LITERAL:
            fprintf(out, "\"%s\"", ((ASTStringLiteral*)node)->value);
            break;
        case AST_BOOL_LITERAL:
            fprintf(out, "%d", ((ASTBoolLiteral*)node)->value);
            break;
        case AST_IDENTIFIER:
            fprintf(out, "%s", ((ASTIdentifier*)node)->name);
            break;
        case AST_BINARY_EXPR: {
            ASTBinaryExpr* expr = (ASTBinaryExpr*)node;
            generate_expression_code(expr->left, out);
            fprintf(out, " %s ", op_to_str(expr->operator));
            generate_expression_code(expr->right, out);
            break;
        }
        case AST_UNARY_EXPR: {
            ASTUnaryExpr* expr = (ASTUnaryExpr*)node;
            fprintf(out, "%s", op_to_str(expr->operator));
            generate_expression_code(expr->right, out);
            break;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* call_expr = (ASTCallExpr*)node;
            fprintf(out, "%s(", call_expr->name);
            for (int i = 0; i < call_expr->arg_count; i++) {
                if (i > 0) fprintf(out, ", ");
                generate_expression_code(call_expr->args[i], out);
            }
            fprintf(out, ")");
            break;
        }
        case AST_GROUPING_EXPR:
            fprintf(out, "(");
            generate_expression_code(((ASTGroupingExpr*)node)->expression, out);
            fprintf(out, ")");
            break;
        default: break;
    }
}
