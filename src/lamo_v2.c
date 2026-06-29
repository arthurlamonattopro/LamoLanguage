#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
/* Sprint 1 fix (Windows): the Lamo TokenType (in lexer.h) used to collide
 * with the Windows SDK TokenType (an enumerator in winnt.h's
 * _TOKEN_INFORMATION_CLASS enum, exposed via <windows.h>). We renamed
 * ours to LamoTokenType to avoid the collision, so the include order
 * no longer matters. We still define WIN32_LEAN_AND_MEAN and NOGDI to
 * keep the <windows.h> surface small (smaller object files, faster
 * compiles). */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <direct.h>
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>
#endif
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "semantic.h"
#include "builtins.h"

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
static int run_argv(char* const argv[]);
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
// Sprint 2 refactor: the name check now delegates to the shared table in
// builtins.h (lamo_builtin_is_gui), so adding a new GUI builtin only
// requires editing builtins.h — this code path picks it up automatically.
//
// Bug #8 fix (preserved): we compare against the exact builtin list rather
// than using strncmp(name, "gui_", 4) == 0, which would have incorrectly
// linked -lX11 for any user-defined function starting with "gui_".
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
            if (lamo_builtin_is_gui(n->name)) return 1;
            for (int i = 0; i < n->arg_count; i++) {
                if (lamo_program_uses_gui_recursive(n->args[i])) return 1;
            }
            return 0;
        }
        case AST_CALL_EXPR: {
            ASTCallExpr* n = (ASTCallExpr*)node;
            if (lamo_builtin_is_gui(n->name)) return 1;
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
        case AST_ARRAY_LITERAL: {
            /* Sprint 3: walk array literal elements for GUI builtin usage. */
            ASTArrayLiteral* arr = (ASTArrayLiteral*)node;
            int i;
            for (i = 0; i < arr->element_count; i++) {
                if (lamo_program_uses_gui_recursive(arr->elements[i])) return 1;
            }
            return 0;
        }
        case AST_INDEX_EXPR: {
            ASTIndexExpr* idx = (ASTIndexExpr*)node;
            return lamo_program_uses_gui_recursive(idx->array) ||
                   lamo_program_uses_gui_recursive(idx->index);
        }
        case AST_PROP_EXPR:
            return lamo_program_uses_gui_recursive(((ASTPropExpr*)node)->object);
        default:
            return 0;
    }
}

static int program_uses_gui(ASTProgram* program) {
    return lamo_program_uses_gui_recursive((ASTNode*)program);
}

/* Sprint 3: source-lookup callback for the semantic analyzer.
 *
 * Given a file path, returns the source text that was loaded for it
 * (stored in CompilationState.sources[]). Returns NULL if the path is
 * not known to the compilation state — in that case the semantic
 * analyzer just omits the source snippet for that error.
 *
 * This is what threads the source text from the loader (lamo_v2.c) into
 * the semantic analyzer (semantic.c) without leaking the CompilationState
 * type into semantic.h. */
static const char* lamo_source_lookup(const char* file_path, void* user_data) {
    CompilationState* state = (CompilationState*)user_data;
    int i;
    if (!state || !file_path) {
        return NULL;
    }
    for (i = 0; i < state->count; i++) {
        if (state->file_paths[i] && strcmp(state->file_paths[i], file_path) == 0) {
            return state->sources[i];
        }
    }
    return NULL;
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

    if (!semantic_analyze_with_source_lookup(program_ast, semantic_label, lamo_source_lookup, &state)) {
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
        int needs_gui;
        int exit_status;
        char binary_with_suffix[1024];

        if (!out) {
            fprintf(stderr, "failed to open %s for writing: %s\n", c_output_path, strerror(errno));
            exit_code = EXIT_COMPILE_ERROR;
            goto cleanup;
        }

        generate_c_code((ASTNode*)program_ast, out);
        fclose(out);

        needs_gui = program_uses_gui(program_ast);

        /* Build argv for the C compiler. Using execvp/CreateProcess instead of
         * system() means user-controlled paths (which only the compiler itself
         * supplies here, but the binary name is also derived from -o output_path
         * that the user can pass) are passed verbatim to the OS — no shell
         * metacharacters can ever be interpreted. */
        snprintf(binary_with_suffix, sizeof(binary_with_suffix),
                 "%s%s", binary_output_path, executable_suffix());

        {
            char c_output_local[256];
            snprintf(c_output_local, sizeof(c_output_local), "%s", c_output_path);
#ifdef _WIN32
            (void)needs_gui;
            /* On Windows the GUI runtime is always linked (GDI32 is always
             * present), so we don't gate on needs_gui. */
            char* argv[] = {
                "gcc", "-Wall", "-Wextra", "-std=c99",
                "-o", binary_with_suffix, c_output_local,
                "-lgdi32", "-luser32", "-lws2_32", "-lm",
                NULL
            };
            exit_status = run_argv(argv);
#else
            if (needs_gui) {
                char* argv[] = {
                    "gcc", "-Wall", "-Wextra", "-std=c99",
                    "-o", binary_with_suffix, c_output_local,
                    "-lX11", "-lm",
                    NULL
                };
                exit_status = run_argv(argv);
            } else {
                char* argv[] = {
                    "gcc", "-Wall", "-Wextra", "-std=c99",
                    "-o", binary_with_suffix, c_output_local,
                    "-lm",
                    NULL
                };
                exit_status = run_argv(argv);
            }
#endif
        }

        if (exit_status != 0) {
            fprintf(stderr, "backend compilation failed while building %s from %s\n",
                    binary_with_suffix, c_output_path);
            exit_code = EXIT_BACKEND_ERROR;
            goto cleanup;
        }

        if (command == COMMAND_BUILD) {
            printf("build succeeded: %s\n", binary_with_suffix);
            goto cleanup;
        }

        /* Execute the compiled binary. Again, run via argv rather than
         * system() so that user-supplied -o paths cannot inject shell
         * metacharacters. On POSIX, prefix with "./" because PATH lookup
         * would not find a file in the current directory. */
        {
#ifdef _WIN32
            char* argv[] = { binary_with_suffix, NULL };
#else
            char dot_slash[2048];
            snprintf(dot_slash, sizeof(dot_slash), "./%s", binary_with_suffix);
            char* argv[] = { dot_slash, NULL };
#endif
            exit_status = run_argv(argv);
        }
        if (exit_status != 0) {
            fprintf(stderr, "program exited with a non-zero status: %s\n",
                    binary_with_suffix);
            exit_code = EXIT_BACKEND_ERROR;
            goto cleanup;
        }
    }

cleanup:
    ast_free((ASTNode*)program_ast);
    free_compilation_state(&state);
    return exit_code;
}

/* run_argv: execute a program with the given argv without going through a
 * shell. This eliminates the entire class of shell-injection bugs that
 * system() would expose on user-controlled paths (e.g. the -o argument).
 *
 * POSIX: fork() + execvp(), wait for child, return its exit status
 *        (0-255). If the child was killed by a signal, return 128 + signum.
 * Windows: CreateProcessA() with the first argv element as the program
 *        path. Returns the child's exit code, or -1 on failure to spawn.
 *
 * argv MUST be NULL-terminated. argv[0] is the program name (sent to
 * execvp() as the file to search PATH for; on Windows it is the full
 * command line token). */
static int run_argv(char* const argv[]) {
    if (!argv || !argv[0]) {
        return -1;
    }

#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD exit_code = 0;
    char command_line[8192];
    size_t pos = 0;
    int i;

    /* Build a quoted command line. We don't shell-interpret it (we use
     * CreateProcess, not cmd.exe), but Windows still parses the command
     * line string itself, so we must quote each argument. */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    for (i = 0; argv[i] != NULL; i++) {
        const char* arg = argv[i];
        size_t arg_len = strlen(arg);
        /* Each arg gets surrounded by quotes; inner quotes are doubled.
         * This follows the standard CommandLineToArgvW rules. */
        if (pos + arg_len * 2 + 4 >= sizeof(command_line)) {
            fprintf(stderr, "internal error: command line too long\n");
            return -1;
        }
        if (i > 0) command_line[pos++] = ' ';
        command_line[pos++] = '"';
        for (size_t j = 0; j < arg_len; j++) {
            char c = arg[j];
            if (c == '"') {
                command_line[pos++] = '"';
                command_line[pos++] = '"';
            } else {
                command_line[pos++] = c;
            }
        }
        command_line[pos++] = '"';
    }
    command_line[pos] = '\0';

    if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, 0,
                        NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        fprintf(stderr, "failed to spawn '%s' (error %lu)\n", argv[0], (unsigned long)err);
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &exit_code)) {
        exit_code = (DWORD)-1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
#else
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        /* Child: execvp() searches PATH. */
        execvp(argv[0], argv);
        /* If we got here, execvp() failed. */
        fprintf(stderr, "exec '%s' failed: %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    /* Parent: wait for the child to finish. */
    for (;;) {
        int status = 0;
        pid_t waited = waitpid(pid, &status, 0);
        if (waited < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
            return -1;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }
        /* Should not happen, but be safe. */
        return -1;
    }
#endif
}

static const char* executable_suffix(void) {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}
