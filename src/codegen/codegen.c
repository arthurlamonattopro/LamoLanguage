#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int indent_level = 0;

static void print_indent(FILE* out) {
    int i;
    for (i = 0; i < indent_level; i++) {
        fprintf(out, "    ");
    }
}

static void generate_statement_code(ASTNode* node, FILE* out);
static void generate_expression_code(ASTNode* node, FILE* out);
static void generate_call_arguments(ASTNode** args, int arg_count, FILE* out);
static int is_gui_builtin(const char* name);
static void generate_gui_call(const char* name, ASTNode** args, int arg_count, FILE* out);
static void emit_gui_runtime(FILE* out);
static int ast_uses_gui(ASTNode* node);

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

static void generate_call_arguments(ASTNode** args, int arg_count, FILE* out) {
    int i;
    for (i = 0; i < arg_count; i++) {
        if (i > 0) {
            fprintf(out, ", ");
        }
        generate_expression_code(args[i], out);
    }
}

static int is_gui_builtin(const char* name) {
    return strcmp(name, "gui_open") == 0 ||
           strcmp(name, "gui_should_close") == 0 ||
           strcmp(name, "gui_begin_frame") == 0 ||
           strcmp(name, "gui_draw_rect") == 0 ||
           strcmp(name, "gui_draw_text") == 0 ||
           strcmp(name, "gui_end_frame") == 0 ||
           strcmp(name, "gui_close") == 0;
}

static void generate_gui_call(const char* name, ASTNode** args, int arg_count, FILE* out) {
    fprintf(out, "lamo_%s(", name);
    generate_call_arguments(args, arg_count, out);
    fprintf(out, ")");
}

static void emit_gui_runtime(FILE* out) {
    fprintf(out, "#ifdef _WIN32\n");
    fprintf(out, "#include <windows.h>\n\n");
    fprintf(out, "typedef struct {\n");
    fprintf(out, "    HWND hwnd;\n");
    fprintf(out, "    HDC back_dc;\n");
    fprintf(out, "    HBITMAP back_bitmap;\n");
    fprintf(out, "    HFONT font;\n");
    fprintf(out, "    int width;\n");
    fprintf(out, "    int height;\n");
    fprintf(out, "    int is_open;\n");
    fprintf(out, "} LamoGuiState;\n\n");
    fprintf(out, "static LamoGuiState lamo_gui = {0};\n");
    fprintf(out, "static int lamo_gui_registered = 0;\n\n");
    fprintf(out, "static LRESULT CALLBACK lamo_gui_window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {\n");
    fprintf(out, "    (void)l_param;\n");
    fprintf(out, "    switch (message) {\n");
    fprintf(out, "        case WM_CLOSE:\n");
    fprintf(out, "            lamo_gui.is_open = 0;\n");
    fprintf(out, "            DestroyWindow(hwnd);\n");
    fprintf(out, "            return 0;\n");
    fprintf(out, "        case WM_DESTROY:\n");
    fprintf(out, "            lamo_gui.is_open = 0;\n");
    fprintf(out, "            PostQuitMessage(0);\n");
    fprintf(out, "            return 0;\n");
    fprintf(out, "        default:\n");
    fprintf(out, "            return DefWindowProcA(hwnd, message, w_param, l_param);\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");
    fprintf(out, "static void lamo_gui_process_messages(void) {\n");
    fprintf(out, "    MSG message;\n");
    fprintf(out, "    while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {\n");
    fprintf(out, "        if (message.message == WM_QUIT) {\n");
    fprintf(out, "            lamo_gui.is_open = 0;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        TranslateMessage(&message);\n");
    fprintf(out, "        DispatchMessageA(&message);\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");
    fprintf(out, "static int lamo_gui_open(int width, int height, const char* title) {\n");
    fprintf(out, "    WNDCLASSA window_class;\n");
    fprintf(out, "    HDC window_dc;\n");
    fprintf(out, "    RECT rect;\n");
    fprintf(out, "    if (lamo_gui.is_open) {\n");
    fprintf(out, "        return 1;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (!lamo_gui_registered) {\n");
    fprintf(out, "        ZeroMemory(&window_class, sizeof(window_class));\n");
    fprintf(out, "        window_class.lpfnWndProc = lamo_gui_window_proc;\n");
    fprintf(out, "        window_class.hInstance = GetModuleHandleA(NULL);\n");
    fprintf(out, "        window_class.lpszClassName = \"LamoGuiWindow\";\n");
    fprintf(out, "        window_class.hCursor = LoadCursor(NULL, IDC_ARROW);\n");
    fprintf(out, "        window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);\n");
    fprintf(out, "        if (!RegisterClassA(&window_class)) {\n");
    fprintf(out, "            DWORD error_code = GetLastError();\n");
    fprintf(out, "            if (error_code != ERROR_CLASS_ALREADY_EXISTS) {\n");
    fprintf(out, "                fprintf(stderr, \"failed to register GUI window class (error %%lu)\\n\", (unsigned long)error_code);\n");
    fprintf(out, "                return 0;\n");
    fprintf(out, "            }\n");
    fprintf(out, "        }\n");
    fprintf(out, "        lamo_gui_registered = 1;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    lamo_gui.width = width;\n");
    fprintf(out, "    lamo_gui.height = height;\n");
    fprintf(out, "    rect.left = 0;\n");
    fprintf(out, "    rect.top = 0;\n");
    fprintf(out, "    rect.right = width;\n");
    fprintf(out, "    rect.bottom = height;\n");
    fprintf(out, "    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, 0);\n");
    fprintf(out, "    lamo_gui.hwnd = CreateWindowExA(0, \"LamoGuiWindow\", title, WS_OVERLAPPEDWINDOW,\n");
    fprintf(out, "        CW_USEDEFAULT, CW_USEDEFAULT,\n");
    fprintf(out, "        rect.right - rect.left, rect.bottom - rect.top,\n");
    fprintf(out, "        NULL, NULL, GetModuleHandleA(NULL), NULL);\n");
    fprintf(out, "    if (!lamo_gui.hwnd) {\n");
    fprintf(out, "        fprintf(stderr, \"failed to create GUI window\\n\");\n");
    fprintf(out, "        return 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    ShowWindow(lamo_gui.hwnd, SW_SHOW);\n");
    fprintf(out, "    UpdateWindow(lamo_gui.hwnd);\n");
    fprintf(out, "    window_dc = GetDC(lamo_gui.hwnd);\n");
    fprintf(out, "    lamo_gui.back_dc = CreateCompatibleDC(window_dc);\n");
    fprintf(out, "    lamo_gui.back_bitmap = CreateCompatibleBitmap(window_dc, width, height);\n");
    fprintf(out, "    SelectObject(lamo_gui.back_dc, lamo_gui.back_bitmap);\n");
    fprintf(out, "    ReleaseDC(lamo_gui.hwnd, window_dc);\n");
    fprintf(out, "    SetBkMode(lamo_gui.back_dc, TRANSPARENT);\n");
    fprintf(out, "    lamo_gui.font = CreateFontA(24, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,\n");
    fprintf(out, "        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,\n");
    fprintf(out, "        DEFAULT_PITCH | FF_DONTCARE, \"Segoe UI\");\n");
    fprintf(out, "    if (lamo_gui.font) {\n");
    fprintf(out, "        SelectObject(lamo_gui.back_dc, lamo_gui.font);\n");
    fprintf(out, "    }\n");
    fprintf(out, "    lamo_gui.is_open = 1;\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n\n");
    fprintf(out, "static int lamo_gui_should_close(void) {\n");
    fprintf(out, "    lamo_gui_process_messages();\n");
    fprintf(out, "    return lamo_gui.is_open ? 0 : 1;\n");
    fprintf(out, "}\n\n");
    fprintf(out, "static void lamo_gui_begin_frame(int r, int g, int b) {\n");
    fprintf(out, "    RECT rect;\n");
    fprintf(out, "    HBRUSH brush;\n");
    fprintf(out, "    if (!lamo_gui.is_open) {\n");
    fprintf(out, "        return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    rect.left = 0;\n");
    fprintf(out, "    rect.top = 0;\n");
    fprintf(out, "    rect.right = lamo_gui.width;\n");
    fprintf(out, "    rect.bottom = lamo_gui.height;\n");
    fprintf(out, "    brush = CreateSolidBrush(RGB(r, g, b));\n");
    fprintf(out, "    FillRect(lamo_gui.back_dc, &rect, brush);\n");
    fprintf(out, "    DeleteObject(brush);\n");
    fprintf(out, "}\n\n");
    fprintf(out, "static void lamo_gui_draw_rect(int x, int y, int width, int height, int r, int g, int b) {\n");
    fprintf(out, "    RECT rect;\n");
    fprintf(out, "    HBRUSH brush;\n");
    fprintf(out, "    if (!lamo_gui.is_open) {\n");
    fprintf(out, "        return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    rect.left = x;\n");
    fprintf(out, "    rect.top = y;\n");
    fprintf(out, "    rect.right = x + width;\n");
    fprintf(out, "    rect.bottom = y + height;\n");
    fprintf(out, "    brush = CreateSolidBrush(RGB(r, g, b));\n");
    fprintf(out, "    FillRect(lamo_gui.back_dc, &rect, brush);\n");
    fprintf(out, "    DeleteObject(brush);\n");
    fprintf(out, "}\n\n");
    fprintf(out, "static void lamo_gui_draw_text(const char* text, int x, int y, int r, int g, int b) {\n");
    fprintf(out, "    if (!lamo_gui.is_open) {\n");
    fprintf(out, "        return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    SetTextColor(lamo_gui.back_dc, RGB(r, g, b));\n");
    fprintf(out, "    TextOutA(lamo_gui.back_dc, x, y, text, (int)strlen(text));\n");
    fprintf(out, "}\n\n");
    fprintf(out, "static void lamo_gui_end_frame(void) {\n");
    fprintf(out, "    HDC window_dc;\n");
    fprintf(out, "    if (!lamo_gui.is_open) {\n");
    fprintf(out, "        return;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    window_dc = GetDC(lamo_gui.hwnd);\n");
    fprintf(out, "    BitBlt(window_dc, 0, 0, lamo_gui.width, lamo_gui.height, lamo_gui.back_dc, 0, 0, SRCCOPY);\n");
    fprintf(out, "    ReleaseDC(lamo_gui.hwnd, window_dc);\n");
    fprintf(out, "}\n\n");
    fprintf(out, "static void lamo_gui_close(void) {\n");
    fprintf(out, "    if (lamo_gui.font) {\n");
    fprintf(out, "        DeleteObject(lamo_gui.font);\n");
    fprintf(out, "        lamo_gui.font = NULL;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (lamo_gui.back_bitmap) {\n");
    fprintf(out, "        DeleteObject(lamo_gui.back_bitmap);\n");
    fprintf(out, "        lamo_gui.back_bitmap = NULL;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (lamo_gui.back_dc) {\n");
    fprintf(out, "        DeleteDC(lamo_gui.back_dc);\n");
    fprintf(out, "        lamo_gui.back_dc = NULL;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    if (lamo_gui.hwnd) {\n");
    fprintf(out, "        DestroyWindow(lamo_gui.hwnd);\n");
    fprintf(out, "        lamo_gui.hwnd = NULL;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    lamo_gui.is_open = 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "#else\n");
    fprintf(out, "static int lamo_gui_open(int width, int height, const char* title) {\n");
    fprintf(out, "    (void)width;\n");
    fprintf(out, "    (void)height;\n");
    fprintf(out, "    (void)title;\n");
    fprintf(out, "    fprintf(stderr, \"GUI builtins are only supported on Windows right now.\\n\");\n");
    fprintf(out, "    return 0;\n");
    fprintf(out, "}\n");
    fprintf(out, "static int lamo_gui_should_close(void) { return 1; }\n");
    fprintf(out, "static void lamo_gui_begin_frame(int r, int g, int b) { (void)r; (void)g; (void)b; }\n");
    fprintf(out, "static void lamo_gui_draw_rect(int x, int y, int width, int height, int r, int g, int b) {\n");
    fprintf(out, "    (void)x; (void)y; (void)width; (void)height; (void)r; (void)g; (void)b;\n");
    fprintf(out, "}\n");
    fprintf(out, "static void lamo_gui_draw_text(const char* text, int x, int y, int r, int g, int b) {\n");
    fprintf(out, "    (void)text; (void)x; (void)y; (void)r; (void)g; (void)b;\n");
    fprintf(out, "}\n");
    fprintf(out, "static void lamo_gui_end_frame(void) {}\n");
    fprintf(out, "static void lamo_gui_close(void) {}\n");
    fprintf(out, "#endif\n\n");
}

static int ast_uses_gui(ASTNode* node) {
    int i;

    if (!node) {
        return 0;
    }

    switch (node->type) {
        case AST_PROGRAM: {
            ASTNode* current = ((ASTProgram*)node)->declarations;
            while (current) {
                if (ast_uses_gui(current)) {
                    return 1;
                }
                current = current->next;
            }
            return 0;
        }
        case AST_VAR_DECL:
            return ast_uses_gui(((ASTVarDecl*)node)->initializer);
        case AST_FN_DECL:
            return ast_uses_gui(((ASTFnDecl*)node)->body);
        case AST_BLOCK: {
            ASTNode* current = ((ASTBlock*)node)->statements;
            while (current) {
                if (ast_uses_gui(current)) {
                    return 1;
                }
                current = current->next;
            }
            return 0;
        }
        case AST_IF_STMT: {
            ASTIfStmt* if_stmt = (ASTIfStmt*)node;
            return ast_uses_gui(if_stmt->condition) ||
                   ast_uses_gui(if_stmt->then_branch) ||
                   ast_uses_gui(if_stmt->else_branch);
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* while_stmt = (ASTWhileStmt*)node;
            return ast_uses_gui(while_stmt->condition) ||
                   ast_uses_gui(while_stmt->body);
        }
        case AST_FOR_STMT: {
            ASTForStmt* for_stmt = (ASTForStmt*)node;
            return ast_uses_gui(for_stmt->initializer) ||
                   ast_uses_gui(for_stmt->condition) ||
                   ast_uses_gui(for_stmt->increment) ||
                   ast_uses_gui(for_stmt->body);
        }
        case AST_RETURN_STMT:
            return ast_uses_gui(((ASTReturnStmt*)node)->expression);
        case AST_PRINT_STMT:
        case AST_INPUT_EXPR:
        case AST_ISNUMBER_EXPR:
        case AST_ISSTRING_EXPR:
        case AST_EXIT_STMT:
        case AST_ABS_EXPR:
            return ast_uses_gui(((ASTPrintStmt*)node)->expression);
        case AST_ASSIGN_STMT:
            return ast_uses_gui(((ASTAssignStmt*)node)->value);
        case AST_CALL_STMT: {
            ASTCallStmt* call_stmt = (ASTCallStmt*)node;
            if (is_gui_builtin(call_stmt->name)) {
                return 1;
            }
            for (i = 0; i < call_stmt->arg_count; i++) {
                if (ast_uses_gui(call_stmt->args[i])) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* call_expr = (ASTCallExpr*)node;
            if (is_gui_builtin(call_expr->name)) {
                return 1;
            }
            for (i = 0; i < call_expr->arg_count; i++) {
                if (ast_uses_gui(call_expr->args[i])) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_BINARY_EXPR: {
            ASTBinaryExpr* expr = (ASTBinaryExpr*)node;
            return ast_uses_gui(expr->left) || ast_uses_gui(expr->right);
        }
        case AST_UNARY_EXPR:
            return ast_uses_gui(((ASTUnaryExpr*)node)->right);
        case AST_GROUPING_EXPR:
            return ast_uses_gui(((ASTGroupingExpr*)node)->expression);
        case AST_INT_LITERAL:
        case AST_STRING_LITERAL:
        case AST_BOOL_LITERAL:
        case AST_IDENTIFIER:
            return 0;
        default:
            return 0;
    }
}

void generate_c_code(ASTNode* node, FILE* out) {
    ASTNode* current;
    int needs_gui_runtime;

    if (!node) {
        return;
    }

    fprintf(out, "// Generated by Lamo v2 (via AST)\n");
    fprintf(out, "#include <stdio.h>\n");
    fprintf(out, "#include <stdlib.h>\n");
    fprintf(out, "#include <string.h>\n\n");
    needs_gui_runtime = ast_uses_gui(node);
    if (needs_gui_runtime) {
        emit_gui_runtime(out);
    }

    current = ((ASTProgram*)node)->declarations;
    while (current) {
        if (current->type == AST_FN_DECL) {
            ASTFnDecl* fn_decl = (ASTFnDecl*)current;
            int i;
            fprintf(out, "int %s(", fn_decl->name);
            for (i = 0; i < fn_decl->param_count; i++) {
                if (i > 0) {
                    fprintf(out, ", ");
                }
                fprintf(out, "int %s", fn_decl->params[i]);
            }
            fprintf(out, ");\n");
        }
        current = current->next;
    }
    fprintf(out, "\n");

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
    if (!node) {
        return;
    }

    if (node->type != AST_BLOCK) {
        print_indent(out);
    }

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
            int i;
            fprintf(out, "int %s(", fn_decl->name);
            for (i = 0; i < fn_decl->param_count; i++) {
                if (i > 0) {
                    fprintf(out, ", ");
                }
                fprintf(out, "int %s", fn_decl->params[i]);
            }
            fprintf(out, ") ");
            generate_statement_code(fn_decl->body, out);
            break;
        }
        case AST_BLOCK: {
            ASTBlock* block = (ASTBlock*)node;
            ASTNode* current = block->statements;
            fprintf(out, "{\n");
            indent_level++;
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
                    ASTVarDecl* var_decl = (ASTVarDecl*)for_stmt->initializer;
                    fprintf(out, "int %s = ", var_decl->name);
                    generate_expression_code(var_decl->initializer, out);
                } else if (for_stmt->initializer->type == AST_ASSIGN_STMT) {
                    ASTAssignStmt* assign_stmt = (ASTAssignStmt*)for_stmt->initializer;
                    fprintf(out, "%s %s ", assign_stmt->name, op_to_str(assign_stmt->op_type));
                    generate_expression_code(assign_stmt->value, out);
                }
            }
            fprintf(out, "; ");
            if (for_stmt->condition) {
                generate_expression_code(for_stmt->condition, out);
            }
            fprintf(out, "; ");
            if (for_stmt->increment) {
                ASTAssignStmt* assign_stmt = (ASTAssignStmt*)for_stmt->increment;
                fprintf(out, "%s %s ", assign_stmt->name, op_to_str(assign_stmt->op_type));
                generate_expression_code(assign_stmt->value, out);
            }
            fprintf(out, ") ");
            generate_statement_code(for_stmt->body, out);
            break;
        }
        case AST_RETURN_STMT: {
            ASTReturnStmt* return_stmt = (ASTReturnStmt*)node;
            fprintf(out, "return ");
            generate_expression_code(return_stmt->expression, out);
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
            if (is_gui_builtin(call_stmt->name)) {
                generate_gui_call(call_stmt->name, call_stmt->args, call_stmt->arg_count, out);
            } else {
                fprintf(out, "%s(", call_stmt->name);
                generate_call_arguments(call_stmt->args, call_stmt->arg_count, out);
                fprintf(out, ")");
            }
            fprintf(out, ";\n");
            break;
        }
        default:
            break;
    }
}

static void generate_expression_code(ASTNode* node, FILE* out) {
    if (!node) {
        return;
    }

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
            if (is_gui_builtin(call_expr->name)) {
                generate_gui_call(call_expr->name, call_expr->args, call_expr->arg_count, out);
            } else {
                fprintf(out, "%s(", call_expr->name);
                generate_call_arguments(call_expr->args, call_expr->arg_count, out);
                fprintf(out, ")");
            }
            break;
        }
        case AST_GROUPING_EXPR:
            fprintf(out, "(");
            generate_expression_code(((ASTGroupingExpr*)node)->expression, out);
            fprintf(out, ")");
            break;
        case AST_INPUT_EXPR: {
            ASTPrintStmt* input_expr = (ASTPrintStmt*)node;
            fprintf(out, "({ ");
            if (input_expr->expression) {
                if (input_expr->expression->type == AST_STRING_LITERAL) {
                    fprintf(out, "printf(\"%%s\", ");
                } else {
                    fprintf(out, "printf(\"%%d\", ");
                }
                generate_expression_code(input_expr->expression, out);
                fprintf(out, "); ");
            }
            fprintf(out, "int _val; scanf(\"%%d\", &_val); _val; })");
            break;
        }
        case AST_ISNUMBER_EXPR:
            fprintf(out, "1");
            break;
        case AST_ISSTRING_EXPR: {
            ASTPrintStmt* is_string_expr = (ASTPrintStmt*)node;
            if (is_string_expr->expression->type == AST_STRING_LITERAL) {
                fprintf(out, "1");
            } else {
                fprintf(out, "0");
            }
            break;
        }
        case AST_EXIT_STMT: {
            ASTPrintStmt* exit_stmt = (ASTPrintStmt*)node;
            fprintf(out, "exit(");
            generate_expression_code(exit_stmt->expression, out);
            fprintf(out, ")");
            break;
        }
        case AST_ABS_EXPR: {
            ASTPrintStmt* abs_expr = (ASTPrintStmt*)node;
            fprintf(out, "abs(");
            generate_expression_code(abs_expr->expression, out);
            fprintf(out, ")");
            break;
        }
        default:
            break;
    }
}
