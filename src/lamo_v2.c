#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
#include "eval/eval.h"
#include "lampm/lampm.h"

#define VERSION "2.2.0"

enum {
    EXIT_SUCCESS_CODE = 0,
    EXIT_COMPILE_ERROR = 1,
    EXIT_BACKEND_ERROR = 2
};

/* Global CLI options. Set in main() based on argv / env vars. */
static int g_verbose = 0;       /* LAMO_VERBOSE=1 or --verbose */
static int g_quiet = 0;         /* LAMO_QUIET=1 or --quiet */

typedef enum {
    COMMAND_RUN,
    COMMAND_BUILD,
    COMMAND_CHECK,
    COMMAND_EVAL,
    COMMAND_NEW,
    COMMAND_CLEAN,
    COMMAND_REPL
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
static void print_command_help(const char* prog, const char* command);
static char* read_file(const char* path);
static int run_argv(char* const argv[]);
static int compile_sources(const char** input_files, int input_file_count, LamoCommand command, const char* output_path);
static const char* executable_suffix(void);
static const char* lamo_cc(void);
static int load_program_recursive(CompilationState* state, ASTProgram* aggregate_program, const char* path);
static int load_program_recursive_from(CompilationState* state, ASTProgram* aggregate_program,
                                        const char* path, const char* imported_from,
                                        int import_line, int import_column);
static int ensure_load_stack_capacity(CompilationState* state, int required_depth);
static int push_loading_file(CompilationState* state, int file_index);
static void pop_loading_file(CompilationState* state);
static void report_import_cycle(const CompilationState* state, int repeated_index);
static void report_import_cycle_at(const CompilationState* state, int repeated_index,
                                    const char* importing_file, int line, int column);
void generate_c_code(ASTNode* node, FILE* out);
static int command_new(int argc, char** argv);
static int command_clean(int argc, char** argv);
static int command_repl(int argc, char** argv);

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

/* Like report_import_cycle but prefixes the message with the file:line:col
 * of the import statement that closed the cycle, matching the format used
 * by parser and semantic errors so the user can jump to it directly. */
static void report_import_cycle_at(const CompilationState* state, int repeated_index,
                                    const char* importing_file, int line, int column) {
    int cycle_start = -1;
    int i;

    for (i = 0; i < state->load_depth; i++) {
        if (state->load_stack[i] == repeated_index) {
            cycle_start = i;
            break;
        }
    }

    fprintf(stderr, "%s:%d:%d: import cycle: ", importing_file, line, column);

    if (cycle_start < 0) {
        fprintf(stderr, "%s already imported\n", state->file_paths[repeated_index]);
        return;
    }

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
            /* Pass the import site coordinates so cycle errors can point
             * to the exact import statement that closed the cycle. */
            if (!load_program_recursive_from(state, aggregate_program, resolved,
                                              importing_file, current->line, current->column)) {
                free(resolved);
                return 0;
            }
            free(resolved);
        }
    }
    return 1;
}

static void print_usage(const char* prog) {
    printf("Lamo v%s (with integrated package manager v%s)\n\n", VERSION, LAMPM_VERSION);
    printf("Usage:\n");
    printf("  %s run   <file.lamo> [more-files.lamo ...] [-o output]\n", prog);
    printf("  %s build <file.lamo> [more-files.lamo ...] [-o output]\n", prog);
    printf("  %s check <file.lamo> [more-files.lamo ...]\n", prog);
    printf("  %s eval  <file.lamo>   (interpret without compiling — instant feedback)\n", prog);
    printf("  %s repl                (interactive read-eval-print loop)\n", prog);
    printf("  %s new   <project-name>\n", prog);
    printf("  %s clean               (remove generated lamo_exec* artifacts)\n", prog);
    printf("\n");
    printf("Package manager subcommands (formerly `lampm`):\n");
    printf("  %s init [project-name]              Create a new lamo.pkg (and scaffold)\n", prog);
    printf("  %s install [owner/repo@ref] [alias] Install a dependency (or all)\n", prog);
    printf("  %s update [alias]                   Pull latest HEAD for one or all deps\n", prog);
    printf("  %s remove <alias>                   Remove a dependency and its install dir\n", prog);
    printf("  %s list                             List dependencies and their state\n", prog);
    printf("  %s info <alias>                     Show details about a dependency\n", prog);
    printf("  %s outdated                         Check which deps are behind remote HEAD\n", prog);
    printf("  %s why <alias>                      Alias for `info`\n", prog);
    printf("  %s lock                             Refresh the lockfile from installed deps\n", prog);
    printf("  %s cache <clean|list>               Manage the local packages directory\n", prog);
    printf("  %s doctor                           Verify your environment is set up\n", prog);
    printf("\n");
    printf("  %s help [command]\n", prog);
    printf("  %s version [--verbose]\n", prog);
    printf("\n");
    printf("Global options (apply to most commands):\n");
    printf("  --verbose   Show extra progress information (also: LAMO_VERBOSE=1)\n");
    printf("  --quiet     Suppress success messages (also: LAMO_QUIET=1)\n");
    printf("  --no-color  Disable ANSI color output (package-manager subcommands only)\n");
    printf("\n");
    printf("Environment variables:\n");
    printf("  LAMO_CC     C compiler to use for `run`/`build` (default: gcc)\n");
    printf("  LAMO_VERBOSE  Same as --verbose\n");
    printf("  LAMO_QUIET    Same as --quiet\n");
}

static void print_command_help(const char* prog, const char* command) {
    /* Delegate package-manager subcommands to the lampm help. */
    if (lampm_is_subcommand(command)) {
        char* fake_argv[3];
        fake_argv[0] = (char*)prog;
        fake_argv[1] = (char*)"help";
        fake_argv[2] = (char*)command;
        lampm_main(3, fake_argv);
        return;
    }

    if (strcmp(command, "run") == 0) {
        printf("run — Compile and execute a Lamo program.\n\n");
        printf("Usage: %s run <file.lamo> [more-files.lamo ...] [-o output]\n\n", prog);
        printf("Transpiles to C, invokes the C compiler (LAMO_CC, default gcc), then\n");
        printf("executes the resulting binary. With -o, the binary is saved under that\n");
        printf("name (plus a platform .exe suffix on Windows); otherwise it's named\n");
        printf("lamo_exec and removed after execution.\n");
    } else if (strcmp(command, "build") == 0) {
        printf("build — Compile a Lamo program to a binary, without running it.\n\n");
        printf("Usage: %s build <file.lamo> [more-files.lamo ...] [-o output]\n\n", prog);
        printf("Like `run`, but stops after the binary is built.\n");
    } else if (strcmp(command, "check") == 0) {
        printf("check — Parse and semantic-check a Lamo program without codegen.\n\n");
        printf("Usage: %s check <file.lamo> [more-files.lamo ...]\n\n", prog);
        printf("Fastest way to validate syntax and types. -o is rejected.\n");
    } else if (strcmp(command, "eval") == 0) {
        printf("eval — Interpret a Lamo program directly (no C compiler).\n\n");
        printf("Usage: %s eval <file.lamo>\n\n", prog);
        printf("Uses the built-in tree-walking interpreter. Best for quick feedback\n");
        printf("or environments without a C compiler. Not all runtime features\n");
        printf("(GUI, HTTP) are available in eval mode.\n");
    } else if (strcmp(command, "repl") == 0) {
        printf("repl — Interactive read-eval-print loop.\n\n");
        printf("Usage: %s repl\n\n", prog);
        printf("Reads one line at a time. Lines starting with `let`, `fn`, `if`,\n");
        printf("`while`, `for`, `return`, `break`, `continue`, or `import` are\n");
        printf("parsed as statements; everything else is parsed as an expression\n");
        printf("and its value is printed. Type .exit or .quit to leave.\n");
    } else if (strcmp(command, "new") == 0) {
        printf("new — Scaffold a new Lamo project.\n\n");
        printf("Usage: %s new <project-name>\n\n", prog);
        printf("Creates a directory <project-name> with a starter main.lamo,\n");
        printf("a .gitignore, and a lamo.pkg manifest ready for `lamo install`.\n");
    } else if (strcmp(command, "clean") == 0) {
        printf("clean — Remove generated build artifacts.\n\n");
        printf("Usage: %s clean\n\n", prog);
        printf("Deletes lamo_exec.c, lamo_exec, lamo_exec.exe (the intermediate C\n");
        printf("source and compiled binary). Source files are untouched.\n");
    } else if (strcmp(command, "help") == 0) {
        printf("help — Show help.\n\n");
        printf("Usage: %s help [command]\n\n", prog);
        printf("Without an argument, shows general help. With a command name,\n");
        printf("shows detailed help for that command. Package-manager subcommands\n");
        printf("(init, install, update, remove, list, info, outdated, why, lock,\n");
        printf("cache, doctor) delegate to the integrated lampm help.\n");
    } else if (strcmp(command, "version") == 0) {
        printf("version — Print the Lamo compiler version and exit.\n\n");
        printf("Usage: %s version [--verbose]\n\n", prog);
        printf("With --verbose, also prints the integrated lampm version and the\n");
        printf("C compiler that will be used.\n");
    } else {
        fprintf(stderr, "no help available for `%s`\n", command);
    }
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

/* lamo_cc: return the C compiler to use for `run`/`build`. Honors the
 * LAMO_CC environment variable (which can be set to "clang", "gcc-12",
 * "/usr/local/bin/cc", etc.). Returns "gcc" by default. */
static const char* lamo_cc(void) {
    const char* env = getenv("LAMO_CC");
    if (env && env[0] != '\0') {
        return env;
    }
    return "gcc";
}

/* command_new: scaffold a new Lamo project. */
static int command_new(int argc, char** argv) {
    const char* project_name = NULL;
    char* main_path = NULL;
    char* gitignore_path = NULL;
    char* manifest_path = NULL;
    FILE* mainf = NULL;
    FILE* gi = NULL;
    int i;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_command_help(argv[0], "new");
            return EXIT_SUCCESS_CODE;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return EXIT_COMPILE_ERROR;
        }
        if (!project_name) {
            project_name = argv[i];
        } else {
            fprintf(stderr, "unexpected extra argument: %s\n", argv[i]);
            return EXIT_COMPILE_ERROR;
        }
    }

    if (!project_name) {
        fprintf(stderr, "missing project name. Usage: %s new <project-name>\n", argv[0]);
        return EXIT_COMPILE_ERROR;
    }

    /* Validate the name — basic sanity, no path separators. */
    if (strchr(project_name, '/') != NULL || strchr(project_name, '\\') != NULL) {
        fprintf(stderr, "invalid project name `%s`: must not contain path separators\n", project_name);
        return EXIT_COMPILE_ERROR;
    }

#ifdef _WIN32
    if (_mkdir(project_name) != 0) {
#else
    if (mkdir(project_name, 0755) != 0) {
#endif
        fprintf(stderr, "failed to create project directory `%s`: %s\n",
                project_name, strerror(errno));
        return EXIT_COMPILE_ERROR;
    }

#ifdef _WIN32
    if (_chdir(project_name) != 0) {
#else
    if (chdir(project_name) != 0) {
#endif
        fprintf(stderr, "failed to enter project directory: %s\n", strerror(errno));
        return EXIT_COMPILE_ERROR;
    }

    main_path = duplicate_string("main.lamo");
    mainf = fopen(main_path, "w");
    if (!mainf) {
        fprintf(stderr, "failed to write %s: %s\n", main_path, strerror(errno));
        free(main_path);
        free(gitignore_path);
        free(manifest_path);
        return EXIT_COMPILE_ERROR;
    }
    fprintf(mainf, "// %s — entry point\n", project_name);
    fprintf(mainf, "fn main() {\n");
    fprintf(mainf, "    print(\"Hello from %s!\");\n", project_name);
    fprintf(mainf, "    return 0;\n");
    fprintf(mainf, "}\n");
    fprintf(mainf, "\n");
    fprintf(mainf, "main();\n");
    fclose(mainf);

    gitignore_path = duplicate_string(".gitignore");
    gi = fopen(gitignore_path, "w");
    if (gi) {
        fprintf(gi, "# Lamo project artifacts\n");
        fprintf(gi, "lamo_exec.c\n");
        fprintf(gi, "lamo_exec\n");
        fprintf(gi, "lamo_exec.exe\n");
        fprintf(gi, "lamo_modules/\n");
        fprintf(gi, "lamo.lock\n");
        fclose(gi);
    }

    manifest_path = duplicate_string("lamo.pkg");
    {
        FILE* mf = fopen(manifest_path, "w");
        if (mf) {
            fprintf(mf, "# Lamo package manifest.\n");
            fprintf(mf, "name = %s\n", project_name);
            fprintf(mf, "version = 0.1.0\n");
            fprintf(mf, "packages_dir = lamo_modules\n\n");
            fprintf(mf, "[dependencies]\n");
            fclose(mf);
        }
    }

    if (!g_quiet) {
        printf("Created Lamo project `%s`\n", project_name);
        printf("  %s/main.lamo     — entry point with a hello-world program\n", project_name);
        printf("  %s/.gitignore    — ignores lamo_exec* and lamo_modules/\n", project_name);
        printf("  %s/lamo.pkg      — package manifest (add deps with `lampm install owner/repo`)\n", project_name);
        printf("\nNext steps:\n");
        printf("  cd %s\n", project_name);
        printf("  lamo run main.lamo\n");
    }

    free(main_path);
    free(gitignore_path);
    free(manifest_path);
    return EXIT_SUCCESS_CODE;
}

/* command_clean: remove generated build artifacts. */
static int command_clean(int argc, char** argv) {
    static const char* artifacts[] = {
        "lamo_exec.c",
        "lamo_exec",
        "lamo_exec.exe",
        NULL
    };
    int i;
    int removed = 0;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_command_help(argv[0], "clean");
            return EXIT_SUCCESS_CODE;
        }
        fprintf(stderr, "unexpected argument: %s (clean takes no arguments)\n", argv[i]);
        return EXIT_COMPILE_ERROR;
    }

    for (i = 0; artifacts[i] != NULL; i++) {
        if (unlink(artifacts[i]) == 0) {
            if (g_verbose) {
                printf("[verbose] removed %s\n", artifacts[i]);
            }
            removed++;
        }
    }

    if (!g_quiet) {
        if (removed == 0) {
            printf("nothing to clean\n");
        } else {
            printf("removed %d artifact(s)\n", removed);
        }
    }
    return EXIT_SUCCESS_CODE;
}

/* command_repl: interactive read-eval-print loop using the eval module. */
static int command_repl(int argc, char** argv) {
    EvalEnv* env;
    char line[8192];
    int i;

    /* Parsed AST nodes that must stay alive for the duration of the REPL
     * session. The eval module stores raw pointers to ASTFnDecl nodes
     * inside the environment (eval_env_define_fn), so freeing them
     * between lines would leave dangling pointers. We collect every
     * parsed node here and free them all at exit. */
    ASTNode** keep_alive = NULL;
    size_t keep_alive_count = 0;
    size_t keep_alive_capacity = 0;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_command_help(argv[0], "repl");
            return EXIT_SUCCESS_CODE;
        }
        fprintf(stderr, "unexpected argument: %s (repl takes no arguments)\n", argv[i]);
        return EXIT_COMPILE_ERROR;
    }

    env = eval_env_new(NULL);

    if (!g_quiet) {
        printf("Lamo v%s REPL. Type .exit or .quit to leave, .help for commands.\n", VERSION);
    }

    while (1) {
        char* p;
        size_t len;
        int is_stmt = 0;
        char* source = NULL;
        Lexer* lexer = NULL;
        Parser* parser = NULL;
        ASTNode* parsed = NULL;

        if (!g_quiet) {
            fputs("lamo> ", stdout);
        }
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            if (!g_quiet) fputs("\n", stdout);
            break;
        }

        /* Trim leading whitespace. */
        p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        /* Trim trailing whitespace. */
        len = strlen(p);
        while (len > 0 && isspace((unsigned char)p[len - 1])) {
            p[--len] = '\0';
        }
        if (*p == '\0') continue;

        if (strcmp(p, ".exit") == 0 || strcmp(p, ".quit") == 0) {
            break;
        }
        if (strcmp(p, ".help") == 0) {
            printf(".exit / .quit  Leave the REPL\n");
            printf(".help          Show this help\n");
            printf("\n");
            printf("Type a Lamo expression to evaluate it (its value is printed),\n");
            printf("or a statement (let/fn/if/while/for/return/break/continue/import)\n");
            printf("to execute it. Statements that produce a value (e.g. `let x = 5;`)\n");
            printf("do not print anything.\n");
            continue;
        }

        /* Decide whether to parse as a statement or an expression. */
        if (strncmp(p, "let ", 4) == 0 || strncmp(p, "fn ", 3) == 0 ||
            strncmp(p, "if ", 3) == 0 || strncmp(p, "if(", 3) == 0 ||
            strncmp(p, "while ", 6) == 0 || strncmp(p, "while(", 6) == 0 ||
            strncmp(p, "for ", 4) == 0 || strncmp(p, "for(", 4) == 0 ||
            strncmp(p, "return", 6) == 0 ||
            strcmp(p, "break") == 0 || strcmp(p, "continue") == 0 ||
            strncmp(p, "import ", 7) == 0) {
            is_stmt = 1;
        }

        source = duplicate_string(p);
        if (!source) break;
        lexer = lexer_init(source);
        parser = parser_init(lexer);

        if (is_stmt) {
            parsed = (ASTNode*)parse_statement(parser);
        } else {
            parsed = (ASTNode*)parse_expression(parser);
        }

        if (parser_had_error(parser)) {
            /* Errors already printed by the parser. Free what we got. */
            if (parsed) ast_free(parsed);
            parsed = NULL;
        } else if (parsed) {
            EvalSignal sig = EVAL_SIG_NONE;
            EvalValue v = is_stmt
                ? eval_statement(parsed, env, &sig)
                : eval_expression(parsed, env, &sig);
            if (sig == EVAL_SIG_ERROR) {
                fprintf(stderr, "runtime error\n");
            } else if (!is_stmt && v.type != EVAL_VAL_VOID && v.type != EVAL_VAL_ERROR) {
                char* s = eval_value_to_string(v);
                printf("%s\n", s ? s : "");
                free(s);
            }
            eval_value_free(v);

            /* Keep the AST alive for the rest of the session (functions
             * registered in env point into it). */
            if (keep_alive_count == keep_alive_capacity) {
                size_t new_cap = keep_alive_capacity > 0 ? keep_alive_capacity * 2 : 16;
                ASTNode** resized = realloc(keep_alive, sizeof(ASTNode*) * new_cap);
                if (!resized) {
                    /* If realloc fails, free this node immediately to
                     * avoid a leak; this may cause use-after-free if the
                     * node was a function declaration that was just
                     * registered in env, but at least we don't crash on
                     * the realloc path. */
                    fprintf(stderr, "warning: out of memory for REPL history; freeing node\n");
                    ast_free(parsed);
                    parsed = NULL;
                } else {
                    keep_alive = resized;
                    keep_alive_capacity = new_cap;
                    keep_alive[keep_alive_count++] = parsed;
                    /* ownership transferred; don't free below */
                    parsed = NULL;
                }
            } else {
                keep_alive[keep_alive_count++] = parsed;
                parsed = NULL;
            }
        }

        parser_free(parser);
        lexer_free(lexer);
        free(source);
        if (parsed) ast_free(parsed);
    }

    /* Free all keep-alive AST nodes. */
    {
        size_t j;
        for (j = 0; j < keep_alive_count; j++) {
            ast_free(keep_alive[j]);
        }
        free(keep_alive);
    }

    eval_env_free(env);
    return EXIT_SUCCESS_CODE;
}

int main(int argc, char** argv) {
    LamoCommand command = COMMAND_RUN;
    const char** input_files = NULL;
    int input_file_count = 0;
    const char* output_path = NULL;
    int arg_index = 1;
    int i;

    /* Honor LAMO_VERBOSE / LAMO_QUIET env vars. */
    if (getenv("LAMO_VERBOSE") && getenv("LAMO_VERBOSE")[0] != '\0' &&
        strcmp(getenv("LAMO_VERBOSE"), "0") != 0) {
        g_verbose = 1;
    }
    if (getenv("LAMO_QUIET") && getenv("LAMO_QUIET")[0] != '\0' &&
        strcmp(getenv("LAMO_QUIET"), "0") != 0) {
        g_quiet = 1;
    }

    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_COMPILE_ERROR;
    }

    /* Consume leading global flags before the subcommand. */
    while (arg_index < argc && argv[arg_index][0] == '-' &&
           strcmp(argv[arg_index], "--") != 0) {
        if (strcmp(argv[arg_index], "--verbose") == 0) {
            g_verbose = 1;
            arg_index++;
        } else if (strcmp(argv[arg_index], "--quiet") == 0) {
            g_quiet = 1;
            arg_index++;
        } else if (strcmp(argv[arg_index], "--help") == 0 ||
                   strcmp(argv[arg_index], "-h") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS_CODE;
        } else if (strcmp(argv[arg_index], "--version") == 0 ||
                   strcmp(argv[arg_index], "-v") == 0) {
            printf("lamo %s\n", VERSION);
            if (g_verbose) printf("C compiler: %s\n", lamo_cc());
            return EXIT_SUCCESS_CODE;
        } else {
            fprintf(stderr, "unknown global option: %s\n", argv[arg_index]);
            return EXIT_COMPILE_ERROR;
        }
    }

    if (arg_index >= argc) {
        print_usage(argv[0]);
        return EXIT_COMPILE_ERROR;
    }

    /* Shift argv so the rest of main() can assume the subcommand is at argv[1].
     * We don't actually move memory — just adjust an offset and recompute.
     * Simpler approach: from now on, treat argv[arg_index] as the command. */
    {
        const char* subcommand = argv[arg_index];
        int sub_argc = argc - arg_index + 1;
        char** sub_argv = argv + arg_index - 1;

        /* Reuse the rest of main() by replacing argv[1] semantics.
         * Easier path: just rewrite argv[1] to be the subcommand and adjust
         * argv[] pointers. Since we can't easily do that, we proceed with
         * the original argv but skip past the flags by referencing argv[arg_index]. */

        if (strcmp(subcommand, "help") == 0) {
            if (argc >= arg_index + 2) {
                print_command_help(argv[0], argv[arg_index + 1]);
            } else {
                print_usage(argv[0]);
            }
            return EXIT_SUCCESS_CODE;
        }

        if (strcmp(subcommand, "version") == 0) {
            int show_verbose_version = 0;
            for (i = arg_index + 1; i < argc; i++) {
                if (strcmp(argv[i], "--verbose") == 0) {
                    show_verbose_version = 1;
                }
            }
            printf("lamo %s\n", VERSION);
            if (show_verbose_version || g_verbose) {
                printf("lampm (integrated) %s\n", LAMPM_VERSION);
                printf("C compiler: %s\n", lamo_cc());
            }
            return EXIT_SUCCESS_CODE;
        }

        /* Subcommand help: `lamo <command> --help` */
        for (i = arg_index + 1; i < argc; i++) {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_command_help(argv[0], subcommand);
                return EXIT_SUCCESS_CODE;
            }
        }

        /* Global flags can also appear after the subcommand. */
        for (i = arg_index + 1; i < argc; i++) {
            if (strcmp(argv[i], "--verbose") == 0) {
                g_verbose = 1;
            } else if (strcmp(argv[i], "--quiet") == 0) {
                g_quiet = 1;
            }
        }

        /* Package-manager subcommands (init, install, update, remove, list,
         * info, outdated, why, lock, cache, doctor) are handled by the
         * integrated lampm module. We hand off the full argv starting at
         * sub_argv (which has the subcommand at index 1) so lampm_main
         * sees the same shape it would as a standalone binary. */
        if (lampm_is_subcommand(subcommand)) {
            /* Configure lampm with the global flags parsed so far. Pass
             * -1 for color so lampm's own TTY detection runs. */
            lampm_configure(g_verbose, g_quiet, -1);
            return lampm_main(sub_argc, sub_argv);
        }

        if (strcmp(subcommand, "run") == 0) {
            command = COMMAND_RUN;
        } else if (strcmp(subcommand, "build") == 0) {
            command = COMMAND_BUILD;
        } else if (strcmp(subcommand, "check") == 0) {
            command = COMMAND_CHECK;
        } else if (strcmp(subcommand, "eval") == 0) {
            command = COMMAND_EVAL;
        } else if (strcmp(subcommand, "repl") == 0) {
            return command_repl(sub_argc, sub_argv);
        } else if (strcmp(subcommand, "new") == 0) {
            return command_new(sub_argc, sub_argv);
        } else if (strcmp(subcommand, "clean") == 0) {
            return command_clean(sub_argc, sub_argv);
        } else {
            fprintf(stderr, "unknown command: %s\n\n", subcommand);
            print_usage(argv[0]);
            return EXIT_COMPILE_ERROR;
        }

        /* Now parse the remaining args (files + -o). */
        arg_index++;
    }

    /* Skip global flags interleaved with files. */
    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "--verbose") == 0 ||
            strcmp(argv[arg_index], "--quiet") == 0) {
            arg_index++;
            continue;
        }
        if (strcmp(argv[arg_index], "-o") == 0) {
            arg_index++;
            if (arg_index >= argc) {
                fprintf(stderr, "missing output path after -o\n");
                free(input_files);
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

/* Variant called when loading an import that originated from a specific AST
 * node — carries the import site location so cycle errors point at the
 * exact `import "..."` statement that closed the cycle. */
static int load_program_recursive_from(CompilationState* state, ASTProgram* aggregate_program,
                                        const char* path, const char* imported_from,
                                        int import_line, int import_column) {
    char* normalized_path = normalize_path(path);
    char* raw_source;
    ASTProgram* parsed_program;
    int slot;
    int existing_index;
    int success = 0;

    if (!normalized_path) {
        fprintf(stderr, "%s:%d:%d: failed to resolve import: %s\n",
                imported_from, import_line, import_column, strerror(errno));
        return 0;
    }

    existing_index = find_loaded_file(state, normalized_path);
    if (existing_index >= 0) {
        if (state->load_states[existing_index] == FILE_LOADING) {
            report_import_cycle_at(state, existing_index,
                                   imported_from, import_line, import_column);
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
        fprintf(stderr, "%s:%d:%d: cannot open imported file \"%s\": %s\n",
                imported_from, import_line, import_column, path, strerror(errno));
        goto cleanup;
    }

    state->sources[slot] = raw_source;
    state->lexers[slot] = lexer_init(raw_source);
    state->parsers[slot] = parser_init_with_file(state->lexers[slot], normalized_path);
    parsed_program = parse_program_v2(state->parsers[slot]);

    if (parser_had_error(state->parsers[slot])) {
        fprintf(stderr, "%s: %d syntax error(s)\n",
                normalized_path,
                parser_error_count(state->parsers[slot]));
        load_imports_from_ast(state, aggregate_program, parsed_program->declarations, normalized_path);
        ast_free((ASTNode*)parsed_program);
        goto cleanup;
    }

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
        if (!g_quiet) {
            if (input_file_count == 1) {
                printf("check passed: %s\n", input_files[0]);
            } else {
                printf("check passed: %d file(s)\n", input_file_count);
            }
        }
        goto cleanup;
    }

    if (command == COMMAND_EVAL) {
        EvalEnv* env = eval_env_new(NULL);
        int ok = eval_program(program_ast, env);
        eval_env_free(env);
        if (!ok) exit_code = EXIT_BACKEND_ERROR;
        goto cleanup;
    }

    {
        const char* c_output_path = "lamo_exec.c";
        const char* binary_output_path = output_path ? output_path : "lamo_exec";
        FILE* out = fopen(c_output_path, "w");
        int needs_gui;
        int exit_status;
        char binary_with_suffix[1024];
        const char* cc = lamo_cc();

        if (!out) {
            fprintf(stderr, "failed to open %s for writing: %s\n", c_output_path, strerror(errno));
            exit_code = EXIT_COMPILE_ERROR;
            goto cleanup;
        }

        generate_c_code((ASTNode*)program_ast, out);
        fclose(out);

        if (g_verbose) {
            printf("[verbose] wrote C source: %s\n", c_output_path);
        }

        needs_gui = program_uses_gui(program_ast);
        if (g_verbose && needs_gui) {
            printf("[verbose] program uses GUI builtins; will link X11/GDI\n");
        }

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
            if (g_verbose) {
                printf("[verbose] invoking C compiler: %s -Wall -Wextra -std=c99 -o %s %s\n",
                       cc, binary_with_suffix, c_output_local);
            }
#ifdef _WIN32
            (void)needs_gui;
            /* On Windows the GUI runtime is always linked (GDI32 is always
             * present), so we don't gate on needs_gui. */
            char* argv[] = {
                (char*)cc, "-Wall", "-Wextra", "-std=c99",
                "-o", binary_with_suffix, c_output_local,
                "-lgdi32", "-luser32", "-lws2_32", "-lm",
                NULL
            };
            exit_status = run_argv(argv);
#else
            if (needs_gui) {
                char* argv[] = {
                    (char*)cc, "-Wall", "-Wextra", "-std=c99",
                    "-o", binary_with_suffix, c_output_local,
                    "-lX11", "-lm",
                    NULL
                };
                exit_status = run_argv(argv);
            } else {
                char* argv[] = {
                    (char*)cc, "-Wall", "-Wextra", "-std=c99",
                    "-o", binary_with_suffix, c_output_local,
                    "-lm",
                    NULL
                };
                exit_status = run_argv(argv);
            }
#endif
        }

        if (exit_status != 0) {
            fprintf(stderr, "backend compilation failed: %s returned exit code %d while building %s from %s\n",
                    cc, exit_status, binary_with_suffix, c_output_path);
            fprintf(stderr, "inspect %s for the generated C source, or run with --verbose for details.\n",
                    c_output_path);
            exit_code = EXIT_BACKEND_ERROR;
            goto cleanup;
        }

        if (command == COMMAND_BUILD) {
            if (!g_quiet) {
                printf("build succeeded: %s\n", binary_with_suffix);
            }
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
