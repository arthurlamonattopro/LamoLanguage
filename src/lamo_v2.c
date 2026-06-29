#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <limits.h>
#include <unistd.h>
#endif
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "semantic.h"

#define VERSION "2.0"

enum {
    EXIT_SUCCESS_CODE = 0,
    EXIT_COMPILE_ERROR = 1,
    EXIT_BACKEND_ERROR = 2
};

typedef enum {
    COMMAND_RUN,
    COMMAND_BUILD,
    COMMAND_CHECK
} LamoCommand;

typedef enum {
    FILE_NOT_LOADED,
    FILE_LOADING,
    FILE_LOADED
} FileLoadState;

typedef struct {
    char** file_paths;
    char** sources;
    Lexer** lexers;
    Parser** parsers;
    FileLoadState* load_states;
    int* load_stack;
    int count;
    int capacity;
    int load_depth;
    int load_stack_capacity;
} CompilationState;

static void print_usage(const char* prog);
static char* read_file(const char* path);
static int run_shell_command(const char* command);
static int compile_sources(const char** input_files, int input_file_count, LamoCommand command, const char* output_path);
static const char* executable_suffix(void);
static int load_program_recursive(CompilationState* state, ASTProgram* aggregate_program, const char* path);
static int ensure_load_stack_capacity(CompilationState* state, int required_depth);
static int push_loading_file(CompilationState* state, int file_index);
static void pop_loading_file(CompilationState* state);
static void report_import_cycle(const CompilationState* state, int repeated_index);
void generate_c_code(ASTNode* node, FILE* out);

static char* duplicate_string(const char* value) {
    size_t length = strlen(value);
    char* copy = malloc(length + 1);

    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

static int ensure_state_capacity(CompilationState* state, int required_count) {
    int new_capacity;
    char** resized_paths;
    char** resized_sources;
    Lexer** resized_lexers;
    Parser** resized_parsers;
    FileLoadState* resized_load_states;
    int i;

    if (required_count <= state->capacity) {
        return 1;
    }

    new_capacity = state->capacity > 0 ? state->capacity * 2 : 4;
    while (new_capacity < required_count) {
        new_capacity *= 2;
    }

    resized_paths = calloc((size_t)new_capacity, sizeof(char*));
    resized_sources = calloc((size_t)new_capacity, sizeof(char*));
    resized_lexers = calloc((size_t)new_capacity, sizeof(Lexer*));
    resized_parsers = calloc((size_t)new_capacity, sizeof(Parser*));
    resized_load_states = calloc((size_t)new_capacity, sizeof(FileLoadState));
    if (!resized_paths || !resized_sources || !resized_lexers || !resized_parsers || !resized_load_states) {
        free(resized_paths);
        free(resized_sources);
        free(resized_lexers);
        free(resized_parsers);
        free(resized_load_states);
        return 0;
    }

    for (i = 0; i < state->count; i++) {
        resized_paths[i] = state->file_paths[i];
        resized_sources[i] = state->sources[i];
        resized_lexers[i] = state->lexers[i];
        resized_parsers[i] = state->parsers[i];
        resized_load_states[i] = state->load_states[i];
    }

    free(state->file_paths);
    free(state->sources);
    free(state->lexers);
    free(state->parsers);
    free(state->load_states);

    state->file_paths = resized_paths;
    state->sources = resized_sources;
    state->lexers = resized_lexers;
    state->parsers = resized_parsers;
    state->load_states = resized_load_states;
    state->capacity = new_capacity;

    return 1;
}

static int ensure_load_stack_capacity(CompilationState* state, int required_depth) {
    int new_capacity;
    int* resized_stack;

    if (required_depth <= state->load_stack_capacity) {
        return 1;
    }

    new_capacity = state->load_stack_capacity > 0 ? state->load_stack_capacity * 2 : 8;
    while (new_capacity < required_depth) {
        new_capacity *= 2;
    }

    resized_stack = realloc(state->load_stack, sizeof(int) * (size_t)new_capacity);
    if (!resized_stack) {
        return 0;
    }

    state->load_stack = resized_stack;
    state->load_stack_capacity = new_capacity;
    return 1;
}

static int push_loading_file(CompilationState* state, int file_index) {
    if (!ensure_load_stack_capacity(state, state->load_depth + 1)) {
        return 0;
    }

    state->load_stack[state->load_depth++] = file_index;
    return 1;
}

static void pop_loading_file(CompilationState* state) {
    if (state->load_depth > 0) {
        state->load_depth--;
    }
}

static void report_import_cycle(const CompilationState* state, int repeated_index) {
    int cycle_start = -1;
    int i;

    for (i = 0; i < state->load_depth; i++) {
        if (state->load_stack[i] == repeated_index) {
            cycle_start = i;
            break;
        }
    }

    if (cycle_start < 0) {
        fprintf(stderr, "import cycle detected involving %s\n", state->file_paths[repeated_index]);
        return;
    }

    fprintf(stderr, "import cycle detected: ");
    for (i = cycle_start; i < state->load_depth; i++) {
        fprintf(stderr, "%s -> ", state->file_paths[state->load_stack[i]]);
    }
    fprintf(stderr, "%s\n", state->file_paths[repeated_index]);
}

static int find_loaded_file(const CompilationState* state, const char* normalized_path) {
    int i;

    for (i = 0; i < state->count; i++) {
        if (strcmp(state->file_paths[i], normalized_path) == 0) {
            return i;
        }
    }

    return -1;
}

static int reserve_file_slot(CompilationState* state, char* normalized_path) {
    int index;

    if (!ensure_state_capacity(state, state->count + 1)) {
        return -1;
    }

    index = state->count++;
    state->file_paths[index] = normalized_path;
    state->sources[index] = NULL;
    state->lexers[index] = NULL;
    state->parsers[index] = NULL;
    state->load_states[index] = FILE_NOT_LOADED;
    return index;
}

static void free_compilation_state(CompilationState* state) {
    int i;

    for (i = 0; i < state->count; i++) {
        parser_free(state->parsers[i]);
        lexer_free(state->lexers[i]);
        free(state->sources[i]);
        free(state->file_paths[i]);
    }

    free(state->parsers);
    free(state->lexers);
    free(state->sources);
    free(state->file_paths);
    free(state->load_states);
    free(state->load_stack);
}

static int path_is_absolute(const char* path) {
    if (!path || !path[0]) {
        return 0;
    }

#ifdef _WIN32
    if ((isalpha((unsigned char)path[0]) && path[1] == ':') || path[0] == '\\' || path[0] == '/') {
        return 1;
    }
#else
    if (path[0] == '/') {
        return 1;
    }
#endif

    return 0;
}

static char* normalize_path(const char* path) {
#ifdef _WIN32
    char buffer[4096];

    if (!_fullpath(buffer, path, sizeof(buffer))) {
        return NULL;
    }

    return duplicate_string(buffer);
#else
    return realpath(path, NULL);
#endif
}

static char* path_directory(const char* path) {
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* separator = slash;
    size_t length;
    char* directory;

    if (backslash && (!separator || backslash > separator)) {
        separator = backslash;
    }

    if (!separator) {
        return duplicate_string(".");
    }

    length = (size_t)(separator - path);
    if (length == 0) {
        length = 1;
    }

    directory = malloc(length + 1);
    if (!directory) {
        return NULL;
    }

    memcpy(directory, path, length);
    directory[length] = '\0';
    return directory;
}

static char* path_join(const char* directory, const char* file_name) {
    size_t directory_length;
    size_t file_length;
    int needs_separator;
    char* joined;

    if (path_is_absolute(file_name)) {
        return duplicate_string(file_name);
    }

    directory_length = strlen(directory);
    file_length = strlen(file_name);
    needs_separator = directory_length > 0 &&
        directory[directory_length - 1] != '/' &&
        directory[directory_length - 1] != '\\';

    joined = malloc(directory_length + (size_t)needs_separator + file_length + 1);
    if (!joined) {
        return NULL;
    }

    memcpy(joined, directory, directory_length);
    if (needs_separator) {
#ifdef _WIN32
        joined[directory_length++] = '\\';
#else
        joined[directory_length++] = '/';
#endif
    }
    memcpy(joined + directory_length, file_name, file_length);
    joined[directory_length + file_length] = '\0';
    return joined;
}

static char* resolve_import_path(const char* importing_file, const char* import_path) {
    char* directory = path_directory(importing_file);
    char* joined;
    char* normalized;

    if (!directory) {
        return NULL;
    }

    joined = path_join(directory, import_path);
    free(directory);
    if (!joined) {
        return NULL;
    }

    normalized = normalize_path(joined);
    free(joined);
    return normalized;
}

// Caminha a AST já parseada procurando nós AST_IMPORT e carrega cada
// dependência recursivamente. Substitui o antigo pré-processamento textual de
// imports — agora respeita comentários e strings porque o lexer cuidou disso.
static int load_imports_from_ast(CompilationState* state, ASTProgram* aggregate_program, ASTNode* node, const char* importing_file);

static int load_imports_from_ast(CompilationState* state, ASTProgram* aggregate_program, ASTNode* node, const char* importing_file) {
    if (!node) {
        return 1;
    }

    for (ASTNode* current = node; current; current = current->next) {
        if (current->type == AST_IMPORT) {
            ASTImport* imp = (ASTImport*)current;
            char* resolved = resolve_import_path(importing_file, imp->path);
            if (!resolved) {
                fprintf(stderr, "%s:%d:%d: failed to resolve import \"%s\"\n",
                        importing_file, current->line, current->column, imp->path);
                return 0;
            }
            if (!load_program_recursive(state, aggregate_program, resolved)) {
                free(resolved);
                return 0;
            }
            free(resolved);
        }
    }
    return 1;
}

static void print_usage(const char* prog) {
    printf("Lamo v%s\n\n", VERSION);
    printf("Usage:\n");
    printf("  %s run <file.lamo> [more-files.lamo ...] [-o output]\n", prog);
    printf("  %s build <file.lamo> [more-files.lamo ...] [-o output]\n", prog);
    printf("  %s check <file.lamo> [more-files.lamo ...]\n", prog);
    printf("  %s help\n", prog);
    printf("  %s version\n", prog);
}

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    long size;
    char* content;
    size_t bytes_read;

    if (!file) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);

    content = malloc((size_t)size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    bytes_read = fread(content, 1, (size_t)size, file);
    content[bytes_read] = '\0';
    fclose(file);
    return content;
}

int main(int argc, char** argv) {
    LamoCommand command = COMMAND_RUN;
    const char** input_files = NULL;
    int input_file_count = 0;
    const char* output_path = NULL;
    int arg_index = 1;

    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_COMPILE_ERROR;
    }

    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return EXIT_SUCCESS_CODE;
    }

    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("%s\n", VERSION);
        return EXIT_SUCCESS_CODE;
    }

    if (strcmp(argv[1], "run") == 0) {
        command = COMMAND_RUN;
        arg_index = 2;
    } else if (strcmp(argv[1], "build") == 0) {
        command = COMMAND_BUILD;
        arg_index = 2;
    } else if (strcmp(argv[1], "check") == 0) {
        command = COMMAND_CHECK;
        arg_index = 2;
    }

    if (arg_index >= argc) {
        print_usage(argv[0]);
        return EXIT_COMPILE_ERROR;
    }

    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "-o") == 0) {
            arg_index++;
            if (arg_index >= argc) {
                fprintf(stderr, "missing output path after -o\n");
                return EXIT_COMPILE_ERROR;
            }
            output_path = argv[arg_index++];
        } else {
            const char** resized_input_files = realloc(input_files, sizeof(char*) * (size_t)(input_file_count + 1));
            if (!resized_input_files) {
                fprintf(stderr, "failed to allocate input file list\n");
                free(input_files);
                return EXIT_COMPILE_ERROR;
            }
            input_files = resized_input_files;
            input_files[input_file_count++] = argv[arg_index++];
        }
    }

    if (input_file_count == 0) {
        fprintf(stderr, "no input files provided\n");
        free(input_files);
        return EXIT_COMPILE_ERROR;
    }

    if (command == COMMAND_CHECK && output_path) {
        fprintf(stderr, "`-o` is not supported with `check`\n");
        free(input_files);
        return EXIT_COMPILE_ERROR;
    }

    {
        int exit_code = compile_sources(input_files, input_file_count, command, output_path);
        free(input_files);
        return exit_code;
    }
}

static int load_program_recursive(CompilationState* state, ASTProgram* aggregate_program, const char* path) {
    char* normalized_path = normalize_path(path);
    char* raw_source;
    ASTProgram* parsed_program;
    int slot;
    int existing_index;
    int success = 0;

    if (!normalized_path) {
        fprintf(stderr, "failed to resolve %s: %s\n", path, strerror(errno));
        return 0;
    }

    existing_index = find_loaded_file(state, normalized_path);
    if (existing_index >= 0) {
        if (state->load_states[existing_index] == FILE_LOADING) {
            report_import_cycle(state, existing_index);
            free(normalized_path);
            return 0;
        }

        free(normalized_path);
        return 1;
    }

    slot = reserve_file_slot(state, normalized_path);
    if (slot < 0) {
        fprintf(stderr, "failed to allocate compiler state for imported files\n");
        free(normalized_path);
        return 0;
    }

    state->load_states[slot] = FILE_LOADING;
    if (!push_loading_file(state, slot)) {
        fprintf(stderr, "failed to track import stack for %s\n", normalized_path);
        state->load_states[slot] = FILE_NOT_LOADED;
        return 0;
    }

    raw_source = read_file(normalized_path);
    if (!raw_source) {
        fprintf(stderr, "failed to read %s: %s\n", normalized_path, strerror(errno));
        goto cleanup;
    }

    // Agora o source vai direto pro lexer — sem strip_imports_and_load_dependencies.
    // Imports viram tokens TOKEN_IMPORT e nós AST_IMPORT, carregados depois.
    state->sources[slot] = raw_source;
    state->lexers[slot] = lexer_init(raw_source);
    // Bug #4 fix: passar o path do arquivo pro parser para que erros sintáticos
    // incluam a origem. Bug #5 fix: o parser também seta o default file path
    // para que todos os nós da AST carreguem essa origem, permitindo ao
    // semântico reportar erros multi-arquivo corretamente.
    state->parsers[slot] = parser_init_with_file(state->lexers[slot], normalized_path);
    parsed_program = parse_program_v2(state->parsers[slot]);

    if (parser_had_error(state->parsers[slot])) {
        fprintf(stderr, "%s: %d syntax error(s)\n",
                normalized_path,
                parser_error_count(state->parsers[slot]));
        // Mesmo com erros, tenta carregar imports dos pedaços que parsearam
        // para dar mais diagnóstico ao usuário. Mas falha a compilação.
        load_imports_from_ast(state, aggregate_program, parsed_program->declarations, normalized_path);
        ast_free((ASTNode*)parsed_program);
        goto cleanup;
    }

    // Carrega dependências antes de mesclar — preserva ordem de imports.
    if (!load_imports_from_ast(state, aggregate_program, parsed_program->declarations, normalized_path)) {
        ast_free((ASTNode*)parsed_program);
        goto cleanup;
    }

    ast_program_append(aggregate_program, parsed_program);
    state->load_states[slot] = FILE_LOADED;
    success = 1;

cleanup:
    pop_loading_file(state);
    if (!success && slot >= 0) {
        state->load_states[slot] = FILE_NOT_LOADED;
    }
    return success;
}

// Verifica se a AST usa GUI builtins (para linkar -lX11 no Linux).
// Caminha a AST procurando call_stmt/call_expr com nomes de builtins GUI.
//
// Bug #8 fix: a versão antiga usava strncmp(n->name, "gui_", 4) == 0, o que
// disparava -lX11 para QUALQUER função que começa com "gui_", incluindo
// funções definidas pelo usuário (já que builtins são shadowable). Agora
// comparamos contra a lista exata de builtins GUI que o codegen conhece.
static int lamo_is_gui_builtin_name(const char* name) {
    return strcmp(name, "gui_open") == 0 ||
           strcmp(name, "gui_should_close") == 0 ||
           strcmp(name, "gui_begin_frame") == 0 ||
           strcmp(name, "gui_draw_rect") == 0 ||
           strcmp(name, "gui_draw_text") == 0 ||
           strcmp(name, "gui_end_frame") == 0 ||
           strcmp(name, "gui_close") == 0;
}

static int lamo_program_uses_gui_recursive(ASTNode* node);
static int lamo_program_uses_gui_recursive(ASTNode* node) {
    if (!node) return 0;
    switch (node->type) {
        case AST_PROGRAM: {
            for (ASTNode* c = ((ASTProgram*)node)->declarations; c; c = c->next) {
                if (lamo_program_uses_gui_recursive(c)) return 1;
            }
            return 0;
        }
        case AST_VAR_DECL:
            return lamo_program_uses_gui_recursive(((ASTVarDecl*)node)->initializer);
        case AST_FN_DECL:
            return lamo_program_uses_gui_recursive(((ASTFnDecl*)node)->body);
        case AST_BLOCK: {
            for (ASTNode* c = ((ASTBlock*)node)->statements; c; c = c->next) {
                if (lamo_program_uses_gui_recursive(c)) return 1;
            }
            return 0;
        }
        case AST_IF_STMT: {
            ASTIfStmt* n = (ASTIfStmt*)node;
            return lamo_program_uses_gui_recursive(n->condition) ||
                   lamo_program_uses_gui_recursive(n->then_branch) ||
                   lamo_program_uses_gui_recursive(n->else_branch);
        }
        case AST_WHILE_STMT: {
            ASTWhileStmt* n = (ASTWhileStmt*)node;
            return lamo_program_uses_gui_recursive(n->condition) ||
                   lamo_program_uses_gui_recursive(n->body);
        }
        case AST_FOR_STMT: {
            ASTForStmt* n = (ASTForStmt*)node;
            return lamo_program_uses_gui_recursive(n->initializer) ||
                   lamo_program_uses_gui_recursive(n->condition) ||
                   lamo_program_uses_gui_recursive(n->increment) ||
                   lamo_program_uses_gui_recursive(n->body);
        }
        case AST_RETURN_STMT:
            return lamo_program_uses_gui_recursive(((ASTReturnStmt*)node)->expression);
        case AST_ASSIGN_STMT:
            return lamo_program_uses_gui_recursive(((ASTAssignStmt*)node)->value);
        case AST_CALL_STMT: {
            ASTCallStmt* n = (ASTCallStmt*)node;
            if (lamo_is_gui_builtin_name(n->name)) return 1;
            for (int i = 0; i < n->arg_count; i++) {
                if (lamo_program_uses_gui_recursive(n->args[i])) return 1;
            }
            return 0;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* n = (ASTCallExpr*)node;
            if (lamo_is_gui_builtin_name(n->name)) return 1;
            for (int i = 0; i < n->arg_count; i++) {
                if (lamo_program_uses_gui_recursive(n->args[i])) return 1;
            }
            return 0;
        }
        case AST_BINARY_EXPR: {
            ASTBinaryExpr* n = (ASTBinaryExpr*)node;
            return lamo_program_uses_gui_recursive(n->left) ||
                   lamo_program_uses_gui_recursive(n->right);
        }
        case AST_UNARY_EXPR:
            return lamo_program_uses_gui_recursive(((ASTUnaryExpr*)node)->right);
        case AST_GROUPING_EXPR:
            return lamo_program_uses_gui_recursive(((ASTGroupingExpr*)node)->expression);
        default:
            return 0;
    }
}

static int program_uses_gui(ASTProgram* program) {
    return lamo_program_uses_gui_recursive((ASTNode*)program);
}

static int compile_sources(const char** input_files, int input_file_count, LamoCommand command, const char* output_path) {
    ASTProgram* program_ast = ast_new_program();
    CompilationState state;
    const char* semantic_label = NULL;
    int exit_code = EXIT_SUCCESS_CODE;
    int i;

    memset(&state, 0, sizeof(state));

    // Bug #5 fix: o label aqui é só fallback. O semântico agora usa
    // node->file_path de cada nó da AST (setado pelo parser) para reportar
    // erros no arquivo correto. Em compilações multi-arquivo (programa
    // principal + imports), isso significa que o erro aponta para o arquivo
    // onde o problema realmente está. O label abaixo só aparece em nós
    // sintéticos ou se algum caminho não foi setado.
    semantic_label = input_file_count == 1 ? input_files[0] : "<multiple inputs>";

    for (i = 0; i < input_file_count; i++) {
        if (!load_program_recursive(&state, program_ast, input_files[i])) {
            exit_code = EXIT_COMPILE_ERROR;
            goto cleanup;
        }
    }

    if (!semantic_analyze(program_ast, semantic_label)) {
        exit_code = EXIT_COMPILE_ERROR;
        goto cleanup;
    }

    if (command == COMMAND_CHECK) {
        if (input_file_count == 1) {
            printf("check passed: %s\n", input_files[0]);
        } else {
            printf("check passed: %d file(s)\n", input_file_count);
        }
        goto cleanup;
    }

    {
        const char* c_output_path = "lamo_exec.c";
        const char* binary_output_path = output_path ? output_path : "lamo_exec";
        FILE* out = fopen(c_output_path, "w");
        char gcc_command[2048];
        char exec_command[1024];
        int needs_gui;

        if (!out) {
            fprintf(stderr, "failed to open %s for writing: %s\n", c_output_path, strerror(errno));
            exit_code = EXIT_COMPILE_ERROR;
            goto cleanup;
        }

        generate_c_code((ASTNode*)program_ast, out);
        fclose(out);

        needs_gui = program_uses_gui(program_ast);

        snprintf(gcc_command, sizeof(gcc_command),
#ifdef _WIN32
                 "gcc -Wall -Wextra -std=c99 -o \"%s\" \"%s\" -lgdi32 -luser32 -lws2_32",
#else
                 needs_gui ?
                     "gcc -Wall -Wextra -std=c99 -o \"%s\" \"%s\" -lX11" :
                     "gcc -Wall -Wextra -std=c99 -o \"%s\" \"%s\"",
#endif
                 binary_output_path, c_output_path);
        if (run_shell_command(gcc_command) != 0) {
            fprintf(stderr, "backend compilation failed while building %s from %s\n",
                    binary_output_path, c_output_path);
            exit_code = EXIT_BACKEND_ERROR;
            goto cleanup;
        }

        if (command == COMMAND_BUILD) {
            printf("build succeeded: %s%s\n", binary_output_path, executable_suffix());
            goto cleanup;
        }

        // Em shells POSIX, `system("\"lamo_exec\"")` falha porque o diretório
        // corrente tipicamente não está no PATH. Prefixamos com `./` em
        // plataformas Unix e mantemos o path entre aspas no Windows (cmd.exe
        // procura no diretório corrente por padrão).
#ifdef _WIN32
        snprintf(exec_command, sizeof(exec_command), "\"%s%s\"", binary_output_path, executable_suffix());
#else
        snprintf(exec_command, sizeof(exec_command), "./\"%s%s\"", binary_output_path, executable_suffix());
#endif
        if (run_shell_command(exec_command) != 0) {
            fprintf(stderr, "program exited with a non-zero status: %s%s\n",
                    binary_output_path, executable_suffix());
            exit_code = EXIT_BACKEND_ERROR;
            goto cleanup;
        }
    }

cleanup:
    ast_free((ASTNode*)program_ast);
    free_compilation_state(&state);
    return exit_code;
}

static int run_shell_command(const char* command) {
    return system(command);
}

static const char* executable_suffix(void) {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}
