/*
 * lampm.c — Lamo Packet Manager
 *
 * Originally a standalone binary (LamoPacketManager/src/main.c). Now
 * compiled into the main `lamo` executable and reachable through the
 * package-manager subcommands (lamo install, lamo update, lamo list, ...).
 * The single public entry point is lampm_main() declared in lampm.h.
 *
 * Improvement summary (v0.2.0):
 *   - Fixed POSIX build (added _POSIX_C_SOURCE / _DEFAULT_SOURCE so popen,
 *     pclose, S_IFDIR, PATH_MAX are exposed under -std=c99).
 *   - Fixed `2>nul` Windows-ism in git_head_commit; now uses platform null.
 *   - Version pinning: `lamo install owner/repo@ref` (branch/tag/commit).
 *   - Lockfile (lamo.lock) records the exact commit installed per dep, so
 *     `lamo install` (no args) reproduces the same checkout across machines.
 *   - New commands: update, outdated, info, doctor, cache, lock, why.
 *   - Non-GitHub sources: gitlab.com, bitbucket.org, git+https://, git@...:...
 *   - `lamo init` now scaffolds .gitignore (ignoring lamo_modules/ and
 *     lamo.lock optionally), an empty main.lamo, and a friendlier lamo.pkg.
 *   - Global flags: --verbose, --quiet, --no-color, --help/-h, --version/-v.
 *   - Per-command help: `lamo help <command>` or `lamo <command> --help`.
 *   - ANSI color output (auto-disabled on non-TTY or with --no-color).
 *   - Better diagnostics on git/network failures.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lampm.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define popen _popen
#define pclose _pclose
#define ACCESS _access
#define MKDIR(path) _mkdir(path)
#define PATH_SEP '\\'
#define IS_PATH_TTY(fd) _isatty(fd)
#define NULL_DEVICE "NUL"
#else
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#define ACCESS access
#define MKDIR(path) mkdir(path, 0755)
#define PATH_SEP '/'
#define IS_PATH_TTY(fd) isatty(fd)
#define NULL_DEVICE "/dev/null"
#endif

#define MANIFEST_FILE "lamo.pkg"
#define LOCKFILE "lamo.lock"
#define DEFAULT_PACKAGES_DIR "lamo_modules"

/* ── Global options ────────────────────────────────────────────────────── */

typedef struct {
    int verbose;     /* 0 = default, 1 = verbose */
    int quiet;       /* 0 = default, 1 = quiet (only errors) */
    int color;       /* 1 = use ANSI color, 0 = no color */
} Options;

static Options g_opts = { 0, 0, 1 };

void lampm_configure(int verbose, int quiet, int color) {
    if (verbose >= 0) g_opts.verbose = verbose;
    if (quiet >= 0) g_opts.quiet = quiet;
    if (color >= 0) g_opts.color = color;
}

int lampm_is_subcommand(const char* name) {
    if (!name) return 0;
    if (strcmp(name, "init") == 0) return 1;
    if (strcmp(name, "install") == 0) return 1;
    if (strcmp(name, "update") == 0) return 1;
    if (strcmp(name, "remove") == 0) return 1;
    if (strcmp(name, "list") == 0) return 1;
    if (strcmp(name, "info") == 0) return 1;
    if (strcmp(name, "outdated") == 0) return 1;
    if (strcmp(name, "why") == 0) return 1;
    if (strcmp(name, "lock") == 0) return 1;
    if (strcmp(name, "cache") == 0) return 1;
    if (strcmp(name, "doctor") == 0) return 1;
    return 0;
}

/* ── ANSI color helpers ─────────────────────────────────────────────────── */

#define COL_RESET   "\x1b[0m"
#define COL_RED     "\x1b[31m"
#define COL_GREEN   "\x1b[32m"
#define COL_YELLOW  "\x1b[33m"
#define COL_BLUE    "\x1b[34m"
#define COL_CYAN    "\x1b[36m"
#define COL_BOLD    "\x1b[1m"
#define COL_DIM     "\x1b[2m"

static const char* col(const char* code) {
    return g_opts.color ? code : "";
}

static void info_msg(const char* fmt, ...) {
    va_list ap;
    if (g_opts.quiet) return;
    va_start(ap, fmt);
    fputs(col(COL_CYAN), stdout);
    vfprintf(stdout, fmt, ap);
    fputs(col(COL_RESET), stdout);
    fputc('\n', stdout);
    va_end(ap);
}

static void success_msg(const char* fmt, ...) {
    va_list ap;
    if (g_opts.quiet) return;
    va_start(ap, fmt);
    fputs(col(COL_GREEN), stdout);
    vfprintf(stdout, fmt, ap);
    fputs(col(COL_RESET), stdout);
    fputc('\n', stdout);
    va_end(ap);
}

static void verbose_msg(const char* fmt, ...) {
    va_list ap;
    if (!g_opts.verbose) return;
    va_start(ap, fmt);
    fputs(col(COL_DIM), stdout);
    vfprintf(stdout, fmt, ap);
    fputs(col(COL_RESET), stdout);
    fputc('\n', stdout);
    va_end(ap);
}

static void error_msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs(col(COL_RED), stderr);
    fputs("error: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputs(col(COL_RESET), stderr);
    fputc('\n', stderr);
    va_end(ap);
}

/* ── Data types ────────────────────────────────────────────────────────── */

typedef struct {
    char* alias;
    char* repo;     /* e.g. "owner/repo" or full URL, no @ref */
    char* ref;      /* optional: branch/tag/commit (NULL = HEAD) */
} Dependency;

typedef struct {
    char* name;
    char* version;       /* project version (optional, informational) */
    char* packages_dir;
    Dependency* dependencies;
    size_t dependency_count;
    size_t dependency_capacity;
} Manifest;

typedef struct {
    char* alias;
    char* repo;
    char* ref;       /* the ref recorded in the lockfile (could be HEAD or pin) */
    char* commit;    /* the actual commit hash installed */
} LockEntry;

typedef struct {
    LockEntry* entries;
    size_t count;
    size_t capacity;
} Lockfile;

/* ── Forward declarations ──────────────────────────────────────────────── */

static void print_usage(const char* program_name);
static void print_command_help(const char* program_name, const char* command);
static void manifest_init(Manifest* manifest);
static void manifest_free(Manifest* manifest);
static int manifest_load(const char* path, Manifest* manifest);
static int manifest_write(const char* path, const Manifest* manifest);
static int manifest_add_or_update_dependency(Manifest* manifest, const char* alias,
                                              const char* repo, const char* ref);
static int manifest_remove_dependency(Manifest* manifest, const char* alias);
static int manifest_find_dependency(const Manifest* manifest, const char* alias);
static int ensure_manifest_dependency_capacity(Manifest* manifest, size_t required_count);
static void lockfile_init(Lockfile* lock);
static void lockfile_free(Lockfile* lock);
static int lockfile_load(const char* path, Lockfile* lock);
static int lockfile_write(const char* path, const Lockfile* lock);
static int lockfile_set(Lockfile* lock, const char* alias, const char* repo,
                         const char* ref, const char* commit);
static const LockEntry* lockfile_find(const Lockfile* lock, const char* alias);
static char* duplicate_string(const char* value);
static char* trim_whitespace(char* value);
static char* strip_optional_quotes(char* value);
static int split_key_value(char* line, char** key, char** value);
static int file_exists(const char* path);
static int directory_exists(const char* path);
static int ensure_directory(const char* path);
static int remove_directory_recursive(const char* path);
static char* join_path(const char* base, const char* leaf);
static char* current_directory_name(void);
static int parse_repo_spec(const char* raw, char** out_repo, char** out_ref);
static char* repo_clone_url(const char* repo);
static char* default_alias_from_repo(const char* repo);
static int run_command(const char* command);
static char* capture_command_output(const char* command);
static char* git_head_commit(const char* directory);
static int git_checkout(const char* directory, const char* ref);
static int install_dependency(const Manifest* manifest, const Dependency* dependency,
                               Lockfile* lock);
static int command_init(int argc, char** argv);
static int command_install(int argc, char** argv);
static int command_update(int argc, char** argv);
static int command_remove(int argc, char** argv);
static int command_list(int argc, char** argv);
static int command_info(int argc, char** argv);
static int command_outdated(int argc, char** argv);
static int command_doctor(int argc, char** argv);
static int command_cache(int argc, char** argv);
static int command_lock(int argc, char** argv);
static int command_why(int argc, char** argv);

/* ── String / path helpers ──────────────────────────────────────────────── */

static char* duplicate_string(const char* value) {
    size_t length;
    char* copy;

    if (!value) {
        return NULL;
    }

    length = strlen(value);
    copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

static char* trim_whitespace(char* value) {
    char* end;

    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
        value++;
    }

    end = value + strlen(value);
    while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }

    *end = '\0';
    return value;
}

static char* strip_optional_quotes(char* value) {
    size_t length = strlen(value);

    if (length >= 2 && value[0] == '"' && value[length - 1] == '"') {
        value[length - 1] = '\0';
        return value + 1;
    }

    return value;
}

static int split_key_value(char* line, char** key, char** value) {
    char* separator = strchr(line, '=');

    if (!separator) {
        return 0;
    }

    *separator = '\0';
    *key = trim_whitespace(line);
    *value = strip_optional_quotes(trim_whitespace(separator + 1));
    return 1;
}

static int file_exists(const char* path) {
    return ACCESS(path, 0) == 0;
}

static int directory_exists(const char* path) {
    struct stat info;

    if (stat(path, &info) != 0) {
        return 0;
    }

    return (info.st_mode & S_IFDIR) != 0;
}

static int ensure_directory(const char* path) {
    if (directory_exists(path)) {
        return 1;
    }

    if (MKDIR(path) == 0) {
        return 1;
    }

    error_msg("failed to create directory %s: %s", path, strerror(errno));
    return 0;
}

static char* join_path(const char* base, const char* leaf) {
    size_t base_length = strlen(base);
    size_t leaf_length = strlen(leaf);
    int needs_separator = base_length > 0 && base[base_length - 1] != '/' && base[base_length - 1] != '\\';
    char* joined = malloc(base_length + (size_t)needs_separator + leaf_length + 1);

    if (!joined) {
        return NULL;
    }

    memcpy(joined, base, base_length);
    if (needs_separator) {
        joined[base_length++] = PATH_SEP;
    }
    memcpy(joined + base_length, leaf, leaf_length);
    joined[base_length + leaf_length] = '\0';
    return joined;
}

static char* current_directory_name(void) {
    char buffer[4096];
    char* separator;

#ifdef _WIN32
    if (!_getcwd(buffer, sizeof(buffer))) {
#else
    if (!getcwd(buffer, sizeof(buffer))) {
#endif
        return duplicate_string("lamo-project");
    }

    separator = strrchr(buffer, '\\');
    if (!separator) {
        separator = strrchr(buffer, '/');
    }

    if (!separator || !separator[1]) {
        return duplicate_string(buffer);
    }

    return duplicate_string(separator + 1);
}

/* ── Manifest ──────────────────────────────────────────────────────────── */

static void manifest_init(Manifest* manifest) {
    memset(manifest, 0, sizeof(*manifest));
}

static void manifest_free(Manifest* manifest) {
    size_t i;

    free(manifest->name);
    free(manifest->version);
    free(manifest->packages_dir);

    for (i = 0; i < manifest->dependency_count; i++) {
        free(manifest->dependencies[i].alias);
        free(manifest->dependencies[i].repo);
        free(manifest->dependencies[i].ref);
    }

    free(manifest->dependencies);
    manifest_init(manifest);
}

static int ensure_manifest_dependency_capacity(Manifest* manifest, size_t required_count) {
    Dependency* resized;
    size_t new_capacity;

    if (required_count <= manifest->dependency_capacity) {
        return 1;
    }

    new_capacity = manifest->dependency_capacity > 0 ? manifest->dependency_capacity * 2 : 4;
    while (new_capacity < required_count) {
        new_capacity *= 2;
    }

    resized = realloc(manifest->dependencies, sizeof(Dependency) * new_capacity);
    if (!resized) {
        return 0;
    }

    manifest->dependencies = resized;
    manifest->dependency_capacity = new_capacity;
    return 1;
}

static int manifest_find_dependency(const Manifest* manifest, const char* alias) {
    size_t i;

    for (i = 0; i < manifest->dependency_count; i++) {
        if (strcmp(manifest->dependencies[i].alias, alias) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static int manifest_add_or_update_dependency(Manifest* manifest, const char* alias,
                                              const char* repo, const char* ref) {
    int index = manifest_find_dependency(manifest, alias);
    char* repo_copy;
    char* ref_copy;

    if (index >= 0) {
        repo_copy = duplicate_string(repo);
        ref_copy = ref ? duplicate_string(ref) : NULL;
        if (!repo_copy || (ref && !ref_copy)) {
            free(repo_copy);
            free(ref_copy);
            return 0;
        }

        free(manifest->dependencies[index].repo);
        free(manifest->dependencies[index].ref);
        manifest->dependencies[index].repo = repo_copy;
        manifest->dependencies[index].ref = ref_copy;
        return 1;
    }

    if (!ensure_manifest_dependency_capacity(manifest, manifest->dependency_count + 1)) {
        return 0;
    }

    {
        Dependency* d = &manifest->dependencies[manifest->dependency_count];
        d->alias = duplicate_string(alias);
        d->repo = duplicate_string(repo);
        d->ref = ref ? duplicate_string(ref) : NULL;
        if (!d->alias || !d->repo || (ref && !d->ref)) {
            free(d->alias);
            free(d->repo);
            free(d->ref);
            d->alias = NULL;
            d->repo = NULL;
            d->ref = NULL;
            return 0;
        }
    }

    manifest->dependency_count++;
    return 1;
}

static int manifest_remove_dependency(Manifest* manifest, const char* alias) {
    int index = manifest_find_dependency(manifest, alias);
    size_t i;

    if (index < 0) {
        return 0;
    }

    free(manifest->dependencies[index].alias);
    free(manifest->dependencies[index].repo);
    free(manifest->dependencies[index].ref);

    for (i = (size_t)index; i + 1 < manifest->dependency_count; i++) {
        manifest->dependencies[i] = manifest->dependencies[i + 1];
    }

    manifest->dependency_count--;
    return 1;
}

/* Manifest line syntax: alias = repo[@ref]
 * The @ref part is split off here so callers see a clean (repo, ref) pair. */
static int manifest_parse_dependency_value(char* value, char** out_repo, char** out_ref) {
    char* at = strchr(value, '@');

    if (at) {
        *at = '\0';
        *out_repo = trim_whitespace(value);
        *out_ref = trim_whitespace(at + 1);
        if (**out_ref == '\0') {
            *out_ref = NULL;
        }
    } else {
        *out_repo = trim_whitespace(value);
        *out_ref = NULL;
    }

    return 1;
}

static int manifest_load(const char* path, Manifest* manifest) {
    FILE* file = fopen(path, "r");
    char line[2048];
    int in_dependencies = 0;

    if (!file) {
        error_msg("failed to open %s: %s", path, strerror(errno));
        return 0;
    }

    manifest_init(manifest);

    while (fgets(line, sizeof(line), file)) {
        char* trimmed = trim_whitespace(line);
        char* key;
        char* value;

        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        if (strcmp(trimmed, "[dependencies]") == 0) {
            in_dependencies = 1;
            continue;
        }

        if (*trimmed == '[') {
            in_dependencies = 0;
            continue;
        }

        if (!split_key_value(trimmed, &key, &value)) {
            error_msg("invalid manifest line: %s", trimmed);
            fclose(file);
            manifest_free(manifest);
            return 0;
        }

        if (in_dependencies) {
            char* repo;
            char* ref;
            if (!manifest_parse_dependency_value(value, &repo, &ref)) {
                error_msg("failed to parse dependency value: %s", value);
                fclose(file);
                manifest_free(manifest);
                return 0;
            }
            if (!manifest_add_or_update_dependency(manifest, key, repo, ref)) {
                error_msg("failed to load dependency from manifest");
                fclose(file);
                manifest_free(manifest);
                return 0;
            }
        } else if (strcmp(key, "name") == 0) {
            free(manifest->name);
            manifest->name = duplicate_string(value);
        } else if (strcmp(key, "version") == 0) {
            free(manifest->version);
            manifest->version = duplicate_string(value);
        } else if (strcmp(key, "packages_dir") == 0) {
            free(manifest->packages_dir);
            manifest->packages_dir = duplicate_string(value);
        }
        /* Unknown keys are silently ignored to allow forward-compatible manifests. */
    }

    fclose(file);

    if (!manifest->name) {
        manifest->name = duplicate_string("lamo-project");
    }

    if (!manifest->packages_dir) {
        manifest->packages_dir = duplicate_string(DEFAULT_PACKAGES_DIR);
    }

    if (!manifest->name || !manifest->packages_dir) {
        manifest_free(manifest);
        return 0;
    }

    return 1;
}

static int manifest_write(const char* path, const Manifest* manifest) {
    FILE* file = fopen(path, "w");
    size_t i;

    if (!file) {
        error_msg("failed to write %s: %s", path, strerror(errno));
        return 0;
    }

    fprintf(file, "# Lamo package manifest. Edit by hand or use `lampm install/remove`.\n");
    fprintf(file, "name = %s\n", manifest->name ? manifest->name : "lamo-project");
    if (manifest->version) {
        fprintf(file, "version = %s\n", manifest->version);
    }
    fprintf(file, "packages_dir = %s\n\n",
            manifest->packages_dir ? manifest->packages_dir : DEFAULT_PACKAGES_DIR);
    fprintf(file, "[dependencies]\n");

    for (i = 0; i < manifest->dependency_count; i++) {
        if (manifest->dependencies[i].ref) {
            fprintf(file, "%s = %s@%s\n",
                    manifest->dependencies[i].alias,
                    manifest->dependencies[i].repo,
                    manifest->dependencies[i].ref);
        } else {
            fprintf(file, "%s = %s\n",
                    manifest->dependencies[i].alias,
                    manifest->dependencies[i].repo);
        }
    }

    fclose(file);
    return 1;
}

/* ── Lockfile ──────────────────────────────────────────────────────────── */

static void lockfile_init(Lockfile* lock) {
    memset(lock, 0, sizeof(*lock));
}

static void lockfile_free(Lockfile* lock) {
    size_t i;
    for (i = 0; i < lock->count; i++) {
        free(lock->entries[i].alias);
        free(lock->entries[i].repo);
        free(lock->entries[i].ref);
        free(lock->entries[i].commit);
    }
    free(lock->entries);
    lockfile_init(lock);
}

static int lockfile_ensure_capacity(Lockfile* lock, size_t required) {
    LockEntry* resized;
    size_t new_capacity;

    if (required <= lock->capacity) {
        return 1;
    }

    new_capacity = lock->capacity > 0 ? lock->capacity * 2 : 4;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    resized = realloc(lock->entries, sizeof(LockEntry) * new_capacity);
    if (!resized) {
        return 0;
    }
    lock->entries = resized;
    lock->capacity = new_capacity;
    return 1;
}

static const LockEntry* lockfile_find(const Lockfile* lock, const char* alias) {
    size_t i;
    for (i = 0; i < lock->count; i++) {
        if (strcmp(lock->entries[i].alias, alias) == 0) {
            return &lock->entries[i];
        }
    }
    return NULL;
}

static int lockfile_set(Lockfile* lock, const char* alias, const char* repo,
                         const char* ref, const char* commit) {
    LockEntry* entry = NULL;
    size_t i;
    char* repo_copy;
    char* ref_copy;
    char* commit_copy;

    for (i = 0; i < lock->count; i++) {
        if (strcmp(lock->entries[i].alias, alias) == 0) {
            entry = &lock->entries[i];
            break;
        }
    }

    repo_copy = duplicate_string(repo);
    ref_copy = ref ? duplicate_string(ref) : duplicate_string("HEAD");
    commit_copy = commit ? duplicate_string(commit) : NULL;

    if (!repo_copy || !ref_copy || (commit && !commit_copy)) {
        free(repo_copy);
        free(ref_copy);
        free(commit_copy);
        return 0;
    }

    if (entry) {
        free(entry->repo);
        free(entry->ref);
        free(entry->commit);
        entry->repo = repo_copy;
        entry->ref = ref_copy;
        entry->commit = commit_copy;
        return 1;
    }

    if (!lockfile_ensure_capacity(lock, lock->count + 1)) {
        free(repo_copy);
        free(ref_copy);
        free(commit_copy);
        return 0;
    }

    entry = &lock->entries[lock->count++];
    entry->alias = duplicate_string(alias);
    entry->repo = repo_copy;
    entry->ref = ref_copy;
    entry->commit = commit_copy;
    return entry->alias != NULL;
}

static int lockfile_load(const char* path, Lockfile* lock) {
    FILE* file = fopen(path, "r");
    char line[2048];

    lockfile_init(lock);
    if (!file) {
        return 0;
    }

    while (fgets(line, sizeof(line), file)) {
        char* trimmed = trim_whitespace(line);
        char* key;
        char* value;
        char* at;
        char* ref_start;
        char* commit_start;

        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        if (!split_key_value(trimmed, &key, &value)) {
            continue;
        }

        /* Format: alias = repo @ ref : commit
         * All of ref/commit are optional; only repo is required. */
        at = strchr(value, '@');
        if (at) {
            *at = '\0';
            ref_start = trim_whitespace(at + 1);
        } else {
            ref_start = NULL;
        }

        commit_start = strchr(ref_start ? ref_start : value, ':');
        if (commit_start) {
            *commit_start = '\0';
            commit_start = trim_whitespace(commit_start + 1);
        }

        {
            char* repo = trim_whitespace(value);
            char* ref = ref_start;
            char* commit = commit_start;
            lockfile_set(lock, key, repo, ref, commit);
        }
    }

    fclose(file);
    return 1;
}

static int lockfile_write(const char* path, const Lockfile* lock) {
    FILE* file = fopen(path, "w");
    size_t i;

    if (!file) {
        error_msg("failed to write %s: %s", path, strerror(errno));
        return 0;
    }

    fprintf(file, "# Lamo lockfile. Auto-generated by `lampm`. Do not edit by hand.\n");
    fprintf(file, "# Records the exact commit hash installed for each dependency,\n");
    fprintf(file, "# so `lampm install` (no args) reproduces the same checkout.\n");

    for (i = 0; i < lock->count; i++) {
        fprintf(file, "%s = %s @ %s",
                lock->entries[i].alias,
                lock->entries[i].repo,
                lock->entries[i].ref ? lock->entries[i].ref : "HEAD");
        if (lock->entries[i].commit) {
            fprintf(file, " : %s", lock->entries[i].commit);
        }
        fprintf(file, "\n");
    }

    fclose(file);
    return 1;
}

/* ── Repository spec parsing ────────────────────────────────────────────── */

/* parse_repo_spec: accept any of:
 *   owner/repo
 *   owner/repo@ref
 *   github.com/owner/repo
 *   github.com/owner/repo@ref
 *   https://github.com/owner/repo[.git]
 *   https://gitlab.com/owner/repo[.git]
 *   https://bitbucket.org/owner/repo[.git]
 *   git+https://host/path[.git]
 *   git@host:path[.git]
 *
 * On success, *out_repo holds the canonical repo string (with @ref removed)
 * and *out_ref holds the ref (or NULL). Both are heap-allocated; caller frees.
 */
static int parse_repo_spec(const char* raw, char** out_repo, char** out_ref) {
    const char* at;
    const char* ref;
    char* repo;
    size_t repo_len;
    const char* prefix_https = "https://";
    const char* prefix_git_https = "git+https://";
    const char* prefix_git_ssh = "git+ssh://";
    const char* github = "github.com/";
    const char* gitlab = "gitlab.com/";
    const char* bitbucket = "bitbucket.org/";

    if (!raw || !*raw) {
        return 0;
    }

    /* Split off @ref (but careful: git@host:path also has an @, so only treat
     * @ as a ref separator when it appears AFTER a path separator or slash). */
    at = NULL;
    if (strncmp(raw, "git@", 4) == 0) {
        /* git@host:path.git@ref — the second @ is the ref. */
        const char* p = raw + 4;
        while (*p && *p != ':' && *p != '/') p++;
        if (*p) {
            p++;
            while (*p && *p != '@') p++;
            if (*p == '@') at = p;
        }
    } else {
        at = strchr(raw, '@');
    }

    if (at) {
        repo_len = (size_t)(at - raw);
        ref = at + 1;
        if (!*ref) {
            ref = NULL;
        }
    } else {
        repo_len = strlen(raw);
        ref = NULL;
    }

    repo = malloc(repo_len + 1);
    if (!repo) {
        return 0;
    }
    memcpy(repo, raw, repo_len);
    repo[repo_len] = '\0';

    /* Strip a trailing .git (we'll re-add it when constructing the clone URL). */
    if (repo_len > 4 && strcmp(repo + repo_len - 4, ".git") == 0) {
        repo[repo_len - 4] = '\0';
    }

    /* If the user passed `owner/repo` (no scheme, no host), default to GitHub. */
    if (strncmp(repo, prefix_https, strlen(prefix_https)) != 0 &&
        strncmp(repo, prefix_git_https, strlen(prefix_git_https)) != 0 &&
        strncmp(repo, prefix_git_ssh, strlen(prefix_git_ssh)) != 0 &&
        strncmp(repo, "git@", 4) != 0 &&
        strstr(repo, "github.com/") != repo &&
        strstr(repo, "gitlab.com/") != repo &&
        strstr(repo, "bitbucket.org/") != repo) {
        /* No scheme — assume GitHub shorthand owner/repo. */
        if (!strchr(repo, '/')) {
            error_msg("invalid repository spec `%s`: expected owner/repo or a full git URL", raw);
            free(repo);
            return 0;
        }
        /* Already in canonical owner/repo form. */
    } else {
        /* Has a scheme/host — strip down to host/path for storage. */
        char* stripped = repo;
        if (strncmp(stripped, prefix_https, strlen(prefix_https)) == 0) {
            stripped += strlen(prefix_https);
        } else if (strncmp(stripped, prefix_git_https, strlen(prefix_git_https)) == 0) {
            stripped += strlen(prefix_git_https);
        } else if (strncmp(stripped, prefix_git_ssh, strlen(prefix_git_ssh)) == 0) {
            stripped += strlen(prefix_git_ssh);
        }
        /* git@host:path → host/path */
        if (strncmp(stripped, "git@", 4) == 0) {
            char* colon = strchr(stripped, ':');
            if (colon) {
                memmove(stripped, stripped + 4, (size_t)(colon - stripped) - 4 + 1);
                /* Now stripped looks like host:path, replace : with / */
                stripped[colon - stripped - 4] = '/';
            }
        }

        /* Move the stripped version to a fresh allocation so the caller can
         * always free *out_repo. */
        {
            char* canonical = duplicate_string(stripped);
            free(repo);
            repo = canonical;
        }
    }

    (void)github; (void)gitlab; (void)bitbucket;

    *out_repo = repo;
    *out_ref = ref ? duplicate_string(ref) : NULL;
    return 1;
}

static char* repo_clone_url(const char* repo) {
    /* repo is in canonical form: either "owner/repo" (GitHub shorthand) or
     * "host/path". Construct the HTTPS clone URL. The shorthand needs
     * github.com prepended; the full form already has the host. */
    const char* prefix_https = "https://";
    const char* github_host = "github.com/";
    const char* suffix = ".git";
    size_t length;
    char* clone_url;
    int need_host = 0;

    /* If repo is just "owner/repo" (single slash, no scheme/host), we need
     * to prepend github.com/. Heuristic: if the first segment before the
     * first slash contains a '.', it's a host (e.g. "gitlab.com/..."); else
     * it's an owner name and we default to GitHub. */
    {
        const char* first_slash = strchr(repo, '/');
        if (first_slash) {
            size_t first_seg_len = (size_t)(first_slash - repo);
            int has_dot = 0;
            size_t i;
            for (i = 0; i < first_seg_len; i++) {
                if (repo[i] == '.') {
                    has_dot = 1;
                    break;
                }
            }
            if (!has_dot) {
                need_host = 1;
            }
        }
    }

    length = strlen(prefix_https) +
             (need_host ? strlen(github_host) : 0) +
             strlen(repo) + strlen(suffix);
    clone_url = malloc(length + 1);
    if (!clone_url) {
        return NULL;
    }

    if (need_host) {
        sprintf(clone_url, "%s%s%s%s", prefix_https, github_host, repo, suffix);
    } else {
        sprintf(clone_url, "%s%s%s", prefix_https, repo, suffix);
    }
    return clone_url;
}

static char* default_alias_from_repo(const char* repo) {
    /* repo is "owner/repo" or "host/path/.../repo" — take the last component. */
    const char* slash = strrchr(repo, '/');
    const char* name = slash ? slash + 1 : repo;

    /* Strip a lamo- prefix if present, for nicer default aliases. */
    if (strncmp(name, "lamo-", 5) == 0) {
        return duplicate_string(name + 5);
    }

    return duplicate_string(name);
}

/* ── Command execution ──────────────────────────────────────────────────── */

static int run_command(const char* command) {
    int result = system(command);

    if (result != 0) {
        error_msg("command failed (exit %d): %s", result, command);
        return 0;
    }

    return 1;
}

static char* capture_command_output(const char* command) {
    FILE* pipe = popen(command, "r");
    char buffer[256];
    char* output = NULL;
    size_t output_length = 0;

    if (!pipe) {
        return NULL;
    }

    while (fgets(buffer, sizeof(buffer), pipe)) {
        size_t chunk_length = strlen(buffer);
        char* resized = realloc(output, output_length + chunk_length + 1);
        if (!resized) {
            free(output);
            pclose(pipe);
            return NULL;
        }

        output = resized;
        memcpy(output + output_length, buffer, chunk_length);
        output_length += chunk_length;
        output[output_length] = '\0';
    }

    pclose(pipe);

    return output;
}

/* git_head_commit: returns the short SHA of HEAD in `directory`, or NULL. */
static char* git_head_commit(const char* directory) {
    char command[4096];
    char* output;
    char* trimmed;

    /* NULL_DEVICE is "/dev/null" on POSIX, "NUL" on Windows. Using it
     * instead of the old `2>nul` Windows-ism fixes a real bug: the previous
     * code wrote a file literally named `nul` on Linux when git emitted
     * warnings to stderr. */
    snprintf(command, sizeof(command),
             "git -C \"%s\" rev-parse --short HEAD 2>%s",
             directory, NULL_DEVICE);
    output = capture_command_output(command);
    if (!output) {
        return NULL;
    }

    trimmed = trim_whitespace(output);
    if (*trimmed == '\0') {
        free(output);
        return NULL;
    }

    {
        char* commit = duplicate_string(trimmed);
        free(output);
        return commit;
    }
}

/* git_resolve_ref: returns the short SHA of `ref` in `directory`. */
#if 0
static char* git_resolve_ref(const char* directory, const char* ref) {
    char command[4096];
    char* output;
    char* trimmed;

    snprintf(command, sizeof(command),
             "git -C \"%s\" rev-parse --short %s 2>%s",
             directory, ref, NULL_DEVICE);
    output = capture_command_output(command);
    if (!output) {
        return NULL;
    }

    trimmed = trim_whitespace(output);
    if (*trimmed == '\0') {
        free(output);
        return NULL;
    }

    {
        char* commit = duplicate_string(trimmed);
        free(output);
        return commit;
    }
}
#endif

static int git_checkout(const char* directory, const char* ref) {
    char command[4096];

    snprintf(command, sizeof(command),
             "git -C \"%s\" checkout %s 2>%s",
             directory, ref, NULL_DEVICE);
    return run_command(command);
}

/* ── Directory removal (cross-platform) ─────────────────────────────────── */

#ifdef _WIN32
static int remove_directory_recursive(const char* path) {
    WIN32_FIND_DATAA entry;
    HANDLE handle;
    char pattern[4096];

    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    handle = FindFirstFileA(pattern, &entry);
    if (handle == INVALID_HANDLE_VALUE) {
        return RemoveDirectoryA(path) != 0;
    }

    do {
        char* child_path;

        if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0) {
            continue;
        }

        child_path = join_path(path, entry.cFileName);
        if (!child_path) {
            FindClose(handle);
            return 0;
        }

        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            SetFileAttributesA(child_path, FILE_ATTRIBUTE_NORMAL);
            if (!remove_directory_recursive(child_path)) {
                free(child_path);
                FindClose(handle);
                return 0;
            }
        } else {
            SetFileAttributesA(child_path, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileA(child_path)) {
                free(child_path);
                FindClose(handle);
                return 0;
            }
        }

        free(child_path);
    } while (FindNextFileA(handle, &entry));

    FindClose(handle);
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryA(path) != 0;
}
#else
static int remove_directory_recursive(const char* path) {
    DIR* directory = opendir(path);
    struct dirent* entry;

    if (!directory) {
        return rmdir(path) == 0;
    }

    while ((entry = readdir(directory)) != NULL) {
        char* child_path;
        struct stat info;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        child_path = join_path(path, entry->d_name);
        if (!child_path) {
            closedir(directory);
            return 0;
        }

        if (stat(child_path, &info) != 0) {
            free(child_path);
            closedir(directory);
            return 0;
        }

        if (S_ISDIR(info.st_mode)) {
            if (!remove_directory_recursive(child_path)) {
                free(child_path);
                closedir(directory);
                return 0;
            }
        } else if (unlink(child_path) != 0) {
            free(child_path);
            closedir(directory);
            return 0;
        }

        free(child_path);
    }

    closedir(directory);
    return rmdir(path) == 0;
}
#endif

/* ── Install ───────────────────────────────────────────────────────────── */

static int install_dependency(const Manifest* manifest, const Dependency* dependency,
                               Lockfile* lock) {
    char* install_dir;
    char* clone_url;
    char command[4096];
    int success;
    char* commit = NULL;
    const LockEntry* locked = NULL;

    if (!ensure_directory(manifest->packages_dir)) {
        return 0;
    }

    install_dir = join_path(manifest->packages_dir, dependency->alias);
    clone_url = repo_clone_url(dependency->repo);
    if (!install_dir || !clone_url) {
        free(install_dir);
        free(clone_url);
        return 0;
    }

    if (lock) {
        locked = lockfile_find(lock, dependency->alias);
    }

    if (directory_exists(install_dir)) {
        /* Already installed. If we have a pinned ref or a locked commit, check
         * it out. Otherwise pull. */
        if (dependency->ref) {
            info_msg("Checking out %s@%s", dependency->alias, dependency->ref);
            snprintf(command, sizeof(command), "git -C \"%s\" fetch origin 2>%s",
                     install_dir, NULL_DEVICE);
            if (!run_command(command)) {
                free(install_dir);
                free(clone_url);
                return 0;
            }
            if (!git_checkout(install_dir, dependency->ref)) {
                free(install_dir);
                free(clone_url);
                return 0;
            }
        } else if (locked && locked->commit && strcmp(locked->ref, "HEAD") != 0) {
            /* Reproduce the locked commit. */
            info_msg("Restoring %s @ %s", dependency->alias, locked->commit);
            snprintf(command, sizeof(command), "git -C \"%s\" fetch origin 2>%s",
                     install_dir, NULL_DEVICE);
            if (!run_command(command)) {
                free(install_dir);
                free(clone_url);
                return 0;
            }
            if (!git_checkout(install_dir, locked->commit)) {
                free(install_dir);
                free(clone_url);
                return 0;
            }
        } else {
            info_msg("Updating %s from %s", dependency->alias, dependency->repo);
            snprintf(command, sizeof(command), "git -C \"%s\" pull --ff-only 2>%s",
                     install_dir, NULL_DEVICE);
            if (!run_command(command)) {
                free(install_dir);
                free(clone_url);
                return 0;
            }
        }
    } else {
        info_msg("Installing %s from %s%s%s",
                 dependency->alias,
                 dependency->repo,
                 dependency->ref ? "@" : "",
                 dependency->ref ? dependency->ref : "");
        snprintf(command, sizeof(command), "git clone --depth 50 \"%s\" \"%s\" 2>%s",
                 clone_url, install_dir, NULL_DEVICE);
        if (!run_command(command)) {
            error_msg("clone failed for %s — check that the repository exists and is public",
                      dependency->repo);
            free(install_dir);
            free(clone_url);
            return 0;
        }
        if (dependency->ref) {
            if (!git_checkout(install_dir, dependency->ref)) {
                free(install_dir);
                free(clone_url);
                return 0;
            }
        } else if (locked && locked->commit) {
            /* Try to check out the locked commit (best-effort: depth-50 may
             * not include it; if it fails, we just stay at HEAD). */
            verbose_msg("Attempting to restore locked commit %s for %s",
                        locked->commit, dependency->alias);
            if (git_checkout(install_dir, locked->commit)) {
                /* Fine. */
            } else {
                verbose_msg("could not check out %s (probably outside depth-50 clone); staying at HEAD",
                            locked->commit);
            }
        }
    }

    commit = git_head_commit(install_dir);
    if (commit) {
        verbose_msg("%s @ %s", dependency->alias, commit);
        if (lock) {
            lockfile_set(lock, dependency->alias, dependency->repo,
                         dependency->ref ? dependency->ref : "HEAD", commit);
        }
    } else if (lock) {
        lockfile_set(lock, dependency->alias, dependency->repo,
                     dependency->ref ? dependency->ref : "HEAD", NULL);
    }

    success = 1;
    free(install_dir);
    free(clone_url);
    free(commit);
    return success;
}

/* ── Commands ──────────────────────────────────────────────────────────── */

static int command_init(int argc, char** argv) {
    Manifest manifest;
    const char* project_name = NULL;
    char* gitignore_path = NULL;
    char* main_path = NULL;
    FILE* gi = NULL;
    FILE* mainf = NULL;
    int i;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_command_help(argv[0], "init");
            return 0;
        }
        if (argv[i][0] == '-') {
            error_msg("unknown option: %s", argv[i]);
            return 1;
        }
        if (!project_name) {
            project_name = argv[i];
        } else {
            error_msg("unexpected extra argument: %s", argv[i]);
            return 1;
        }
    }

    if (file_exists(MANIFEST_FILE)) {
        error_msg("%s already exists in this directory", MANIFEST_FILE);
        return 1;
    }

    manifest_init(&manifest);
    manifest.name = project_name ? duplicate_string(project_name) : current_directory_name();
    manifest.version = duplicate_string("0.1.0");
    manifest.packages_dir = duplicate_string(DEFAULT_PACKAGES_DIR);

    if (!manifest.name || !manifest.packages_dir) {
        manifest_free(&manifest);
        return 1;
    }

    if (!ensure_directory(manifest.packages_dir)) {
        manifest_free(&manifest);
        return 1;
    }

    if (!manifest_write(MANIFEST_FILE, &manifest)) {
        manifest_free(&manifest);
        return 1;
    }

    /* Write a .gitignore if one doesn't already exist. */
    gitignore_path = duplicate_string(".gitignore");
    if (!file_exists(gitignore_path)) {
        gi = fopen(gitignore_path, "w");
        if (gi) {
            fprintf(gi, "# Lamo project\n");
            fprintf(gi, "%s/\n", manifest.packages_dir);
            fprintf(gi, "%s\n", LOCKFILE);
            fprintf(gi, "# Uncomment the next line to also ignore generated binaries:\n");
            fprintf(gi, "# lamo_exec*\n");
            fclose(gi);
            success_msg("wrote .gitignore");
        }
    }

    /* Scaffold a minimal main.lamo if the directory looks empty. */
    main_path = duplicate_string("main.lamo");
    if (!file_exists(main_path)) {
        mainf = fopen(main_path, "w");
        if (mainf) {
            fprintf(mainf, "// %s — entry point\n", manifest.name);
            fprintf(mainf, "fn main() {\n");
            fprintf(mainf, "    print(\"Hello from Lamo!\");\n");
            fprintf(mainf, "    return 0;\n");
            fprintf(mainf, "}\n");
            fprintf(mainf, "\n");
            fprintf(mainf, "main();\n");
            fclose(mainf);
            success_msg("wrote main.lamo");
        }
    }

    success_msg("Created %s for project %s%s%s",
                MANIFEST_FILE,
                col(COL_BOLD),
                manifest.name,
                col(COL_RESET));
    info_msg("Packages will be installed into %s/", manifest.packages_dir);
    info_msg("Next: `lampm install owner/repo` to add a dependency.");

    free(gitignore_path);
    free(main_path);
    manifest_free(&manifest);
    return 0;
}

static int command_install(int argc, char** argv) {
    Manifest manifest;
    Lockfile lock;
    int status = 0;
    int write_lock = 0;
    int i;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_command_help(argv[0], "install");
            return 0;
        }
        if (argv[i][0] == '-') {
            error_msg("unknown option: %s", argv[i]);
            return 1;
        }
        break;
    }

    if (!file_exists(MANIFEST_FILE)) {
        error_msg("missing %s. Run `%s init` first.", MANIFEST_FILE, argv[0]);
        return 1;
    }

    if (!manifest_load(MANIFEST_FILE, &manifest)) {
        return 1;
    }

    lockfile_init(&lock);
    (void)lockfile_load(LOCKFILE, &lock);

    if (argc >= 3) {
        char* repo = NULL;
        char* ref = NULL;
        char* alias = NULL;

        if (!parse_repo_spec(argv[2], &repo, &ref)) {
            manifest_free(&manifest);
            lockfile_free(&lock);
            return 1;
        }

        alias = argc >= 4 ? duplicate_string(argv[3]) : default_alias_from_repo(repo);
        if (!alias) {
            free(repo);
            free(ref);
            manifest_free(&manifest);
            lockfile_free(&lock);
            return 1;
        }

        if (!manifest_add_or_update_dependency(&manifest, alias, repo, ref)) {
            error_msg("failed to update manifest dependencies");
            free(alias);
            free(repo);
            free(ref);
            manifest_free(&manifest);
            lockfile_free(&lock);
            return 1;
        }

        if (!manifest_write(MANIFEST_FILE, &manifest)) {
            free(alias);
            free(repo);
            free(ref);
            manifest_free(&manifest);
            lockfile_free(&lock);
            return 1;
        }

        {
            int index = manifest_find_dependency(&manifest, alias);
            if (index < 0 || !install_dependency(&manifest, &manifest.dependencies[index], &lock)) {
                status = 1;
            }
            write_lock = 1;
        }

        free(alias);
        free(repo);
        free(ref);
    } else {
        size_t j;

        if (manifest.dependency_count == 0) {
            info_msg("No dependencies recorded in %s", MANIFEST_FILE);
            manifest_free(&manifest);
            lockfile_free(&lock);
            return 0;
        }

        for (j = 0; j < manifest.dependency_count; j++) {
            if (!install_dependency(&manifest, &manifest.dependencies[j], &lock)) {
                status = 1;
                break;
            }
        }
        write_lock = 1;
    }

    if (write_lock && !lockfile_write(LOCKFILE, &lock)) {
        /* Lockfile write failure is non-fatal. */
        verbose_msg("could not write %s", LOCKFILE);
    }

    if (status == 0) {
        success_msg("Install complete");
    }

    manifest_free(&manifest);
    lockfile_free(&lock);
    return status;
}

static int command_update(int argc, char** argv) {
    /* update [alias] — pull latest HEAD for one dep or all deps, ignoring
     * any locked commit. Updates the lockfile afterwards. */
    Manifest manifest;
    Lockfile lock;
    int status = 0;
    int i;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_command_help(argv[0], "update");
            return 0;
        }
        if (argv[i][0] == '-') {
            error_msg("unknown option: %s", argv[i]);
            return 1;
        }
        break;
    }

    if (!file_exists(MANIFEST_FILE)) {
        error_msg("missing %s. Run `%s init` first.", MANIFEST_FILE, argv[0]);
        return 1;
    }

    if (!manifest_load(MANIFEST_FILE, &manifest)) {
        return 1;
    }

    lockfile_init(&lock);

    if (argc >= 3) {
        int index = manifest_find_dependency(&manifest, argv[2]);
        if (index < 0) {
            error_msg("dependency `%s` not found in %s", argv[2], MANIFEST_FILE);
            manifest_free(&manifest);
            return 1;
        }
        /* Force update: pretend there's no pinned ref so install_dependency
         * pulls instead of checking out. We do this by temporarily clearing
         * the ref. */
        {
            char* saved_ref = manifest.dependencies[index].ref;
            manifest.dependencies[index].ref = NULL;
            if (!install_dependency(&manifest, &manifest.dependencies[index], &lock)) {
                status = 1;
            }
            manifest.dependencies[index].ref = saved_ref;
        }
    } else {
        size_t j;
        if (manifest.dependency_count == 0) {
            info_msg("No dependencies to update.");
            manifest_free(&manifest);
            return 0;
        }
        for (j = 0; j < manifest.dependency_count; j++) {
            char* saved_ref = manifest.dependencies[j].ref;
            manifest.dependencies[j].ref = NULL;
            if (!install_dependency(&manifest, &manifest.dependencies[j], &lock)) {
                status = 1;
            }
            manifest.dependencies[j].ref = saved_ref;
        }
    }

    if (!lockfile_write(LOCKFILE, &lock)) {
        verbose_msg("could not write %s", LOCKFILE);
    }

    if (status == 0) {
        success_msg("Update complete");
    }

    manifest_free(&manifest);
    lockfile_free(&lock);
    return status;
}

static int command_remove(int argc, char** argv) {
    Manifest manifest;
    char* install_dir;
    int i;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_command_help(argv[0], "remove");
            return 0;
        }
        if (argv[i][0] == '-') {
            error_msg("unknown option: %s", argv[i]);
            return 1;
        }
        break;
    }

    if (argc < 3) {
        error_msg("missing dependency alias");
        return 1;
    }

    if (!file_exists(MANIFEST_FILE)) {
        error_msg("missing %s. Run `%s init` first.", MANIFEST_FILE, argv[0]);
        return 1;
    }

    if (!manifest_load(MANIFEST_FILE, &manifest)) {
        return 1;
    }

    install_dir = join_path(manifest.packages_dir, argv[2]);
    if (!install_dir) {
        manifest_free(&manifest);
        return 1;
    }

    if (!manifest_remove_dependency(&manifest, argv[2])) {
        error_msg("dependency `%s` was not found in %s", argv[2], MANIFEST_FILE);
        free(install_dir);
        manifest_free(&manifest);
        return 1;
    }

    if (!manifest_write(MANIFEST_FILE, &manifest)) {
        free(install_dir);
        manifest_free(&manifest);
        return 1;
    }

    if (directory_exists(install_dir) && !remove_directory_recursive(install_dir)) {
        error_msg("failed to remove installed package directory %s", install_dir);
        free(install_dir);
        manifest_free(&manifest);
        return 1;
    }

    success_msg("Removed %s", argv[2]);
    free(install_dir);
    manifest_free(&manifest);
    return 0;
}

static int command_list(int argc, char** argv) {
    Manifest manifest;
    Lockfile lock;
    size_t i;

    for (i = 2; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_command_help(argv[0], "list");
            return 0;
        }
    }

    if (!file_exists(MANIFEST_FILE)) {
        error_msg("missing %s. Run `%s init` first.", MANIFEST_FILE, argv[0]);
        return 1;
    }

    if (!manifest_load(MANIFEST_FILE, &manifest)) {
        return 1;
    }

    lockfile_init(&lock);
    (void)lockfile_load(LOCKFILE, &lock);

    printf("%sProject:%s %s\n", col(COL_BOLD), col(COL_RESET), manifest.name);
    if (manifest.version) {
        printf("%sVersion:%s  %s\n", col(COL_BOLD), col(COL_RESET), manifest.version);
    }
    printf("%sModules dir:%s %s\n", col(COL_BOLD), col(COL_RESET), manifest.packages_dir);

    if (manifest.dependency_count == 0) {
        printf("%sDependencies:%s none\n", col(COL_BOLD), col(COL_RESET));
        manifest_free(&manifest);
        lockfile_free(&lock);
        return 0;
    }

    printf("%sDependencies:%s %zu\n", col(COL_BOLD), col(COL_RESET), manifest.dependency_count);
    for (i = 0; i < manifest.dependency_count; i++) {
        char* install_dir = join_path(manifest.packages_dir, manifest.dependencies[i].alias);
        char* commit = NULL;
        const LockEntry* locked = lockfile_find(&lock, manifest.dependencies[i].alias);

        if (install_dir && directory_exists(install_dir)) {
            commit = git_head_commit(install_dir);
        }

        printf("  %s%s%s -> %s%s%s%s",
               col(COL_CYAN), manifest.dependencies[i].alias, col(COL_RESET),
               manifest.dependencies[i].repo,
               manifest.dependencies[i].ref ? "@" : "",
               manifest.dependencies[i].ref ? manifest.dependencies[i].ref : "",
               col(COL_DIM));

        if (commit) {
            printf(" (installed @ %s)", commit);
            if (locked && locked->commit && strcmp(locked->commit, commit) != 0) {
                printf(" %s[locked: %s]%s", col(COL_YELLOW), locked->commit, col(COL_RESET));
            }
        } else if (install_dir && directory_exists(install_dir)) {
            printf(" (installed)");
        } else {
            printf(" %s(not installed)%s", col(COL_YELLOW), col(COL_RESET));
        }
        printf("%s\n", col(COL_RESET));

        free(commit);
        free(install_dir);
    }

    manifest_free(&manifest);
    lockfile_free(&lock);
    return 0;
}

static int command_info(int argc, char** argv) {
    Manifest manifest;
    Lockfile lock;
    int index;
    char* install_dir;
    char* commit = NULL;
    char* branch = NULL;
    char* remote = NULL;
    int i;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_command_help(argv[0], "info");
            return 0;
        }
        if (argv[i][0] == '-') {
            error_msg("unknown option: %s", argv[i]);
            return 1;
        }
        break;
    }

    if (argc < 3) {
        error_msg("missing dependency alias");
        return 1;
    }

    if (!file_exists(MANIFEST_FILE)) {
        error_msg("missing %s.", MANIFEST_FILE);
        return 1;
    }

    if (!manifest_load(MANIFEST_FILE, &manifest)) {
        return 1;
    }

    index = manifest_find_dependency(&manifest, argv[2]);
    if (index < 0) {
        error_msg("dependency `%s` not found in %s", argv[2], MANIFEST_FILE);
        manifest_free(&manifest);
        return 1;
    }

    lockfile_init(&lock);
    (void)lockfile_load(LOCKFILE, &lock);

    install_dir = join_path(manifest.packages_dir, argv[2]);

    printf("%sDependency:%s %s\n", col(COL_BOLD), col(COL_RESET), argv[2]);
    printf("%sRepository:%s %s\n", col(COL_BOLD), col(COL_RESET),
           manifest.dependencies[index].repo);
    printf("%sPinned ref:%s %s\n", col(COL_BOLD), col(COL_RESET),
           manifest.dependencies[index].ref ? manifest.dependencies[index].ref : "(HEAD)");
    printf("%sInstall path:%s %s\n", col(COL_BOLD), col(COL_RESET),
           install_dir ? install_dir : "?");

    if (install_dir && directory_exists(install_dir)) {
        commit = git_head_commit(install_dir);
        {
            char command[4096];
            char* out;
            snprintf(command, sizeof(command), "git -C \"%s\" rev-parse --abbrev-ref HEAD 2>%s",
                     install_dir, NULL_DEVICE);
            out = capture_command_output(command);
            if (out) {
                char* trimmed = trim_whitespace(out);
                if (*trimmed) {
                    branch = duplicate_string(trimmed);
                }
                free(out);
            }
        }
        {
            char command[4096];
            char* out;
            snprintf(command, sizeof(command), "git -C \"%s\" config --get remote.origin.url 2>%s",
                     install_dir, NULL_DEVICE);
            out = capture_command_output(command);
            if (out) {
                char* trimmed = trim_whitespace(out);
                if (*trimmed) {
                    remote = duplicate_string(trimmed);
                }
                free(out);
            }
        }

        printf("%sBranch:%s %s\n", col(COL_BOLD), col(COL_RESET),
               branch ? branch : "(detached)");
        printf("%sCommit:%s %s\n", col(COL_BOLD), col(COL_RESET),
               commit ? commit : "(unknown)");
        printf("%sRemote URL:%s %s\n", col(COL_BOLD), col(COL_RESET),
               remote ? remote : "(unknown)");
    } else {
        printf("%sStatus:%s %snot installed%s\n",
               col(COL_BOLD), col(COL_RESET),
               col(COL_YELLOW), col(COL_RESET));
    }

    {
        const LockEntry* locked = lockfile_find(&lock, argv[2]);
        if (locked) {
            printf("%sLocked commit:%s %s\n", col(COL_BOLD), col(COL_RESET),
                   locked->commit ? locked->commit : "(none)");
        }
    }

    free(install_dir);
    free(commit);
    free(branch);
    free(remote);
    manifest_free(&manifest);
    lockfile_free(&lock);
    return 0;
}

static int command_outdated(int argc, char** argv) {
    /* outdated: for each installed dep, compare local HEAD vs origin HEAD. */
    Manifest manifest;
    size_t i;
    int found_outdated = 0;
    int j;

    for (j = 2; j < argc; j++) {
        if (strcmp(argv[j], "--help") == 0 || strcmp(argv[j], "-h") == 0) {
            print_command_help(argv[0], "outdated");
            return 0;
        }
    }

    if (!file_exists(MANIFEST_FILE)) {
        error_msg("missing %s.", MANIFEST_FILE);
        return 1;
    }

    if (!manifest_load(MANIFEST_FILE, &manifest)) {
        return 1;
    }

    if (manifest.dependency_count == 0) {
        info_msg("No dependencies recorded.");
        manifest_free(&manifest);
        return 0;
    }

    printf("%-20s %-15s %-15s %s\n", "ALIAS", "LOCAL", "REMOTE", "STATUS");
    for (i = 0; i < manifest.dependency_count; i++) {
        char* install_dir = join_path(manifest.packages_dir, manifest.dependencies[i].alias);
        char* local = NULL;
        char* remote = NULL;
        char command[4096];

        if (!install_dir || !directory_exists(install_dir)) {
            printf("%-20s %-15s %-15s %s\n",
                   manifest.dependencies[i].alias, "-", "-", "not installed");
            free(install_dir);
            continue;
        }

        local = git_head_commit(install_dir);

        /* Fetch then resolve origin/HEAD (or origin/<branch>). */
        snprintf(command, sizeof(command), "git -C \"%s\" fetch --depth 50 origin 2>%s",
                 install_dir, NULL_DEVICE);
        (void)run_command(command);

        snprintf(command, sizeof(command),
                 "git -C \"%s\" rev-parse --short origin/HEAD 2>%s",
                 install_dir, NULL_DEVICE);
        {
            char* out = capture_command_output(command);
            if (out) {
                char* trimmed = trim_whitespace(out);
                if (*trimmed) {
                    remote = duplicate_string(trimmed);
                }
                free(out);
            }
        }

        if (!local || !remote || strcmp(local, remote) == 0) {
            printf("%-20s %-15s %-15s %s\n",
                   manifest.dependencies[i].alias,
                   local ? local : "?",
                   remote ? remote : "?",
                   "up to date");
        } else {
            printf("%-20s %-15s %-15s %s%s%s\n",
                   manifest.dependencies[i].alias,
                   local ? local : "?",
                   remote ? remote : "?",
                   col(COL_YELLOW), "outdated", col(COL_RESET));
            found_outdated = 1;
        }

        free(local);
        free(remote);
        free(install_dir);
    }

    manifest_free(&manifest);
    return found_outdated ? 0 : 0;  /* exit 0 either way; this is informational */
}

static int command_doctor(int argc, char** argv) {
    /* doctor: verify environment is set up correctly. */
    int problems = 0;
    int j;

    for (j = 2; j < argc; j++) {
        if (strcmp(argv[j], "--help") == 0 || strcmp(argv[j], "-h") == 0) {
            print_command_help(argv[0], "doctor");
            return 0;
        }
    }

    printf("Lamo Packet Manager v%s — environment check\n\n", LAMPM_VERSION);

    /* git installed? */
    {
        char command[256];
        snprintf(command, sizeof(command), "git --version 2>%s", NULL_DEVICE);
        {
            char* out = capture_command_output(command);
            if (out) {
                char* trimmed = trim_whitespace(out);
                printf("  [+] git: %s\n", trimmed);
                free(out);
            } else {
                printf("  %s[-] git: not found on PATH%s\n", col(COL_RED), col(COL_RESET));
                printf("      lampm needs git to clone dependencies.\n");
                problems++;
            }
        }
    }

    /* manifest present? */
    if (file_exists(MANIFEST_FILE)) {
        Manifest m;
        if (manifest_load(MANIFEST_FILE, &m)) {
            printf("  [+] manifest: %s (%zu dependencies)\n",
                   MANIFEST_FILE, m.dependency_count);
            if (!m.name || strcmp(m.name, "lamo-project") == 0) {
                printf("  %s[!] manifest name is still the default — set it in %s%s\n",
                       col(COL_YELLOW), MANIFEST_FILE, col(COL_RESET));
            }
            manifest_free(&m);
        } else {
            printf("  %s[-] manifest: %s exists but failed to parse%s\n",
                   col(COL_RED), MANIFEST_FILE, col(COL_RESET));
            problems++;
        }
    } else {
        printf("  %s[-] manifest: %s not found (run `%s init`)%s\n",
               col(COL_YELLOW), MANIFEST_FILE, argv[0], col(COL_RESET));
        problems++;
    }

    /* lockfile present? */
    if (file_exists(LOCKFILE)) {
        printf("  [+] lockfile: %s\n", LOCKFILE);
    } else {
        printf("  %s[!] lockfile: %s not found (run `%s install` to generate)%s\n",
               col(COL_YELLOW), LOCKFILE, argv[0], col(COL_RESET));
    }

    /* packages_dir present? */
    {
        Manifest m;
        const char* pkgs_dir = DEFAULT_PACKAGES_DIR;
        if (file_exists(MANIFEST_FILE) && manifest_load(MANIFEST_FILE, &m)) {
            pkgs_dir = m.packages_dir;
            if (directory_exists(pkgs_dir)) {
                printf("  [+] packages dir: %s/\n", pkgs_dir);
            } else {
                printf("  %s[!] packages dir: %s/ missing (run `%s install`)%s\n",
                       col(COL_YELLOW), pkgs_dir, argv[0], col(COL_RESET));
            }
            manifest_free(&m);
        } else {
            printf("  %s[!] packages dir: %s/ (default; manifest not loaded)%s\n",
                   col(COL_YELLOW), pkgs_dir, col(COL_RESET));
        }
    }

    printf("\n");
    if (problems == 0) {
        success_msg("No critical problems found.");
        return 0;
    } else {
        error_msg("%d problem(s) found.", problems);
        return 1;
    }
}

static int command_cache(int argc, char** argv) {
    /* cache clean: remove the packages_dir.
     * cache list: list contents of packages_dir. */
    Manifest manifest;
    int j;

    for (j = 2; j < argc; j++) {
        if (strcmp(argv[j], "--help") == 0 || strcmp(argv[j], "-h") == 0) {
            print_command_help(argv[0], "cache");
            return 0;
        }
        if (argv[j][0] == '-') {
            error_msg("unknown option: %s", argv[j]);
            return 1;
        }
        break;
    }

    if (argc < 3) {
        print_command_help(argv[0], "cache");
        return 1;
    }

    if (!file_exists(MANIFEST_FILE)) {
        error_msg("missing %s.", MANIFEST_FILE);
        return 1;
    }

    if (!manifest_load(MANIFEST_FILE, &manifest)) {
        return 1;
    }

    if (strcmp(argv[2], "clean") == 0 || strcmp(argv[2], "purge") == 0) {
        if (directory_exists(manifest.packages_dir)) {
            if (remove_directory_recursive(manifest.packages_dir)) {
                success_msg("Removed %s/", manifest.packages_dir);
                /* Recreate empty so subsequent installs don't fail. */
                ensure_directory(manifest.packages_dir);
            } else {
                error_msg("failed to remove %s/", manifest.packages_dir);
                manifest_free(&manifest);
                return 1;
            }
        } else {
            info_msg("%s/ does not exist.", manifest.packages_dir);
        }
    } else if (strcmp(argv[2], "list") == 0) {
        if (!directory_exists(manifest.packages_dir)) {
            info_msg("%s/ does not exist.", manifest.packages_dir);
            manifest_free(&manifest);
            return 0;
        }
#ifdef _WIN32
        {
            WIN32_FIND_DATAA entry;
            HANDLE handle;
            char pattern[4096];
            snprintf(pattern, sizeof(pattern), "%s\\*", manifest.packages_dir);
            handle = FindFirstFileA(pattern, &entry);
            if (handle == INVALID_HANDLE_VALUE) {
                info_msg("%s/ is empty.", manifest.packages_dir);
                manifest_free(&manifest);
                return 0;
            }
            do {
                if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0) continue;
                printf("  %s\n", entry.cFileName);
            } while (FindNextFileA(handle, &entry));
            FindClose(handle);
        }
#else
        {
            DIR* dir = opendir(manifest.packages_dir);
            struct dirent* entry;
            if (!dir) {
                info_msg("%s/ is empty.", manifest.packages_dir);
                manifest_free(&manifest);
                return 0;
            }
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                printf("  %s\n", entry->d_name);
            }
            closedir(dir);
        }
#endif
    } else {
        error_msg("unknown cache subcommand: %s (expected: clean | purge | list)", argv[2]);
        manifest_free(&manifest);
        return 1;
    }

    manifest_free(&manifest);
    return 0;
}

static int command_lock(int argc, char** argv) {
    /* lock: refresh the lockfile from the currently-installed deps. */
    Manifest manifest;
    Lockfile lock;
    size_t i;
    int j;

    for (j = 2; j < argc; j++) {
        if (strcmp(argv[j], "--help") == 0 || strcmp(argv[j], "-h") == 0) {
            print_command_help(argv[0], "lock");
            return 0;
        }
    }

    if (!file_exists(MANIFEST_FILE)) {
        error_msg("missing %s.", MANIFEST_FILE);
        return 1;
    }

    if (!manifest_load(MANIFEST_FILE, &manifest)) {
        return 1;
    }

    lockfile_init(&lock);

    for (i = 0; i < manifest.dependency_count; i++) {
        char* install_dir = join_path(manifest.packages_dir, manifest.dependencies[i].alias);
        char* commit = NULL;
        if (install_dir && directory_exists(install_dir)) {
            commit = git_head_commit(install_dir);
        }
        lockfile_set(&lock, manifest.dependencies[i].alias,
                     manifest.dependencies[i].repo,
                     manifest.dependencies[i].ref ? manifest.dependencies[i].ref : "HEAD",
                     commit);
        free(install_dir);
        free(commit);
    }

    if (!lockfile_write(LOCKFILE, &lock)) {
        manifest_free(&manifest);
        lockfile_free(&lock);
        return 1;
    }

    success_msg("Wrote %s (%zu entries)", LOCKFILE, lock.count);
    manifest_free(&manifest);
    lockfile_free(&lock);
    return 0;
}

static int command_why(int argc, char** argv) {
    /* why <alias>: show what we know about a dependency — basically an alias
     * for `info`, kept as a separate command because users coming from npm/cargo
     * expect it. */
    if (argc < 3) {
        error_msg("missing dependency alias");
        return 1;
    }
    return command_info(argc, argv);
}

/* ── Help ──────────────────────────────────────────────────────────────── */

static void print_usage(const char* program_name) {
    printf("Lamo Packet Manager v%s (integrated into `lamo`)\n\n", LAMPM_VERSION);
    printf("%sUsage:%s\n", col(COL_BOLD), col(COL_RESET));
    printf("  %s %s<command>%s [options] [args]\n\n",
           program_name, col(COL_BOLD), col(COL_RESET));
    printf("%sCommands:%s\n", col(COL_BOLD), col(COL_RESET));
    printf("  %sinit%s [project-name]        Create a new lamo.pkg in the current directory\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %sinstall%s [owner/repo@ref]   Install a dependency (or all if no arg)\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %supdate%s [alias]              Pull latest HEAD for one or all deps\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %sremove%s <alias>              Remove a dependency and its install dir\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %slist%s                        List dependencies and their state\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %sinfo%s <alias>                Show details about a dependency\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %soutdated%s                    Check which deps are behind remote HEAD\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %swhy%s <alias>                 Alias for `info`\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %slock%s                        Refresh the lockfile from installed deps\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %scache%s <clean|list>          Manage the local packages directory\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %sdoctor%s                      Verify your environment is set up\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %shelp%s [command]              Show help (per-command if given)\n",
           col(COL_CYAN), col(COL_RESET));
    printf("  %sversion%s                     Print version and exit\n\n",
           col(COL_CYAN), col(COL_RESET));
    printf("%sGlobal options:%s\n", col(COL_BOLD), col(COL_RESET));
    printf("  %s--verbose%s     Show extra progress information\n",
           col(COL_YELLOW), col(COL_RESET));
    printf("  %s--quiet%s       Suppress informational output (errors only)\n",
           col(COL_YELLOW), col(COL_RESET));
    printf("  %s--no-color%s    Disable ANSI color output\n",
           col(COL_YELLOW), col(COL_RESET));
    printf("  %s--help, -h%s    Show this help (or per-command help)\n",
           col(COL_YELLOW), col(COL_RESET));
    printf("  %s--version, -v%s Print version\n\n",
           col(COL_YELLOW), col(COL_RESET));
    printf("Manifest: %s   Lockfile: %s   Modules: %s/\n",
           MANIFEST_FILE, LOCKFILE, DEFAULT_PACKAGES_DIR);
}

static void print_command_help(const char* program_name, const char* command) {
    if (strcmp(command, "init") == 0) {
        printf("%sinit%s — Create a new Lamo project in the current directory.\n\n",
               col(COL_CYAN), col(COL_RESET));
        printf("Usage: %s init [project-name]\n\n", program_name);
        printf("Creates %s, %s/, .gitignore, and a minimal main.lamo\n",
               MANIFEST_FILE, DEFAULT_PACKAGES_DIR);
        printf("if they don't already exist. The project name defaults to the\n");
        printf("current directory name.\n");
    } else if (strcmp(command, "install") == 0) {
        printf("%sinstall%s — Install dependencies from GitHub/GitLab/etc.\n\n",
               col(COL_CYAN), col(COL_RESET));
        printf("Usage:\n");
        printf("  %s install <owner/repo[@ref]> [alias]\n", program_name);
        printf("  %s install                (install everything in lamo.pkg)\n\n", program_name);
        printf("Repository spec forms:\n");
        printf("  owner/repo                GitHub shorthand (HEAD)\n");
        printf("  owner/repo@v1.0.0         GitHub, pinned to tag/branch/commit\n");
        printf("  github.com/owner/repo\n");
        printf("  https://github.com/owner/repo[.git]\n");
        printf("  https://gitlab.com/owner/repo[.git]\n");
        printf("  git+https://example.com/foo/bar.git\n");
        printf("  git@github.com:owner/repo.git\n\n");
        printf("If no alias is given, the repo name (with `lamo-` prefix stripped)\n");
        printf("is used. After install, %s is updated to record the exact\n", LOCKFILE);
        printf("commit that was checked out, so the next `lampm install` reproduces it.\n");
    } else if (strcmp(command, "update") == 0) {
        printf("%supdate%s — Pull the latest HEAD for installed deps.\n\n",
               col(COL_CYAN), col(COL_RESET));
        printf("Usage:\n");
        printf("  %s update [alias]\n\n", program_name);
        printf("Without an alias, updates all deps. Ignores pinned refs and the\n");
        printf("lockfile, then refreshes %s with the new commit hashes.\n", LOCKFILE);
    } else if (strcmp(command, "remove") == 0) {
        printf("%sremove%s — Remove a dependency.\n\n", col(COL_CYAN), col(COL_RESET));
        printf("Usage: %s remove <alias>\n\n", program_name);
        printf("Removes the entry from %s and deletes the install dir.\n", MANIFEST_FILE);
    } else if (strcmp(command, "list") == 0) {
        printf("%slist%s — List dependencies and their state.\n\n",
               col(COL_CYAN), col(COL_RESET));
        printf("Usage: %s list\n\n", program_name);
        printf("Shows each dependency's alias, repo, pinned ref (if any), and\n");
        printf("installed commit. Differences between installed and locked\n");
        printf("commit are highlighted.\n");
    } else if (strcmp(command, "info") == 0) {
        printf("%sinfo%s — Show details about one dependency.\n\n",
               col(COL_CYAN), col(COL_RESET));
        printf("Usage: %s info <alias>\n\n", program_name);
        printf("Shows repo, pinned ref, install path, current branch, commit,\n");
        printf("and remote URL (if installed).\n");
    } else if (strcmp(command, "outdated") == 0) {
        printf("%soutdated%s — Check which deps are behind remote HEAD.\n\n",
               col(COL_CYAN), col(COL_RESET));
        printf("Usage: %s outdated\n\n", program_name);
        printf("For each installed dep, fetches origin and compares local HEAD\n");
        printf("against origin/HEAD.\n");
    } else if (strcmp(command, "doctor") == 0) {
        printf("%sdoctor%s — Verify your environment is set up.\n\n",
               col(COL_CYAN), col(COL_RESET));
        printf("Usage: %s doctor\n\n", program_name);
        printf("Checks git is installed, %s exists and parses, the modules\n",
               MANIFEST_FILE);
        printf("dir is present, etc.\n");
    } else if (strcmp(command, "cache") == 0) {
        printf("%scache%s — Manage the local packages directory.\n\n",
               col(COL_CYAN), col(COL_RESET));
        printf("Usage:\n");
        printf("  %s cache clean   (alias: purge)  Delete the modules dir\n", program_name);
        printf("  %s cache list                     List installed package dirs\n", program_name);
    } else if (strcmp(command, "lock") == 0) {
        printf("%slock%s — Refresh the lockfile from installed deps.\n\n",
               col(COL_CYAN), col(COL_RESET));
        printf("Usage: %s lock\n\n", program_name);
        printf("Walks each dependency in %s, reads the currently-checked-out\n",
               MANIFEST_FILE);
        printf("commit, and writes %s.\n", LOCKFILE);
    } else if (strcmp(command, "why") == 0) {
        printf("%swhy%s — Alias for `info`.\n", col(COL_CYAN), col(COL_RESET));
    } else if (strcmp(command, "help") == 0) {
        printf("%shelp%s — Show help.\n\n", col(COL_CYAN), col(COL_RESET));
        printf("Usage: %s help [command]\n\n", program_name);
        printf("Without an argument, shows general help. With a command name,\n");
        printf("shows detailed help for that command.\n");
    } else if (strcmp(command, "version") == 0) {
        printf("%sversion%s — Print lampm version and exit.\n",
               col(COL_CYAN), col(COL_RESET));
    } else {
        error_msg("no help available for `%s`", command);
    }
}

/* ── lampm_main ────────────────────────────────────────────────────────── */

int lampm_main(int argc, char** argv) {
    int arg_start = 1;
    int i;

    /* Auto-disable color if stdout/stderr isn't a TTY. The caller (lamo_v2.c
     * main) may have already configured g_opts via lampm_configure(); we
     * only force color off here when output isn't a TTY and the user didn't
     * explicitly pass --no-color. */
    if (!IS_PATH_TTY(fileno(stdout)) || !IS_PATH_TTY(fileno(stderr))) {
        g_opts.color = 0;
    }

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Skip leading global flags that the caller may not have stripped. */
    while (arg_start < argc && argv[arg_start][0] == '-' &&
           strcmp(argv[arg_start], "--") != 0) {
        if (strcmp(argv[arg_start], "--verbose") == 0) {
            g_opts.verbose = 1;
        } else if (strcmp(argv[arg_start], "--quiet") == 0) {
            g_opts.quiet = 1;
        } else if (strcmp(argv[arg_start], "--no-color") == 0) {
            g_opts.color = 0;
        } else if (strcmp(argv[arg_start], "--help") == 0 ||
                   strcmp(argv[arg_start], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[arg_start], "--version") == 0 ||
                   strcmp(argv[arg_start], "-v") == 0) {
            printf("lampm %s\n", LAMPM_VERSION);
            return 0;
        } else {
            error_msg("unknown global option: %s", argv[arg_start]);
            return 1;
        }
        arg_start++;
    }

    if (arg_start >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    /* Also accept trailing --verbose / --quiet / --no-color after the command. */
    for (i = arg_start + 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0) {
            g_opts.verbose = 1;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            g_opts.quiet = 1;
        } else if (strcmp(argv[i], "--no-color") == 0) {
            g_opts.color = 0;
        }
    }

    if (strcmp(argv[arg_start], "help") == 0) {
        if (arg_start + 1 < argc) {
            print_command_help(argv[0], argv[arg_start + 1]);
        } else {
            print_usage(argv[0]);
        }
        return 0;
    }

    if (strcmp(argv[arg_start], "version") == 0) {
        printf("lampm %s\n", LAMPM_VERSION);
        return 0;
    }

    if (strcmp(argv[arg_start], "init") == 0) {
        return command_init(argc, argv);
    }

    if (strcmp(argv[arg_start], "install") == 0) {
        return command_install(argc, argv);
    }

    if (strcmp(argv[arg_start], "update") == 0) {
        return command_update(argc, argv);
    }

    if (strcmp(argv[arg_start], "remove") == 0) {
        return command_remove(argc, argv);
    }

    if (strcmp(argv[arg_start], "list") == 0) {
        return command_list(argc, argv);
    }

    if (strcmp(argv[arg_start], "info") == 0) {
        return command_info(argc, argv);
    }

    if (strcmp(argv[arg_start], "outdated") == 0) {
        return command_outdated(argc, argv);
    }

    if (strcmp(argv[arg_start], "doctor") == 0) {
        return command_doctor(argc, argv);
    }

    if (strcmp(argv[arg_start], "cache") == 0) {
        return command_cache(argc, argv);
    }

    if (strcmp(argv[arg_start], "lock") == 0) {
        return command_lock(argc, argv);
    }

    if (strcmp(argv[arg_start], "why") == 0) {
        return command_why(argc, argv);
    }

    error_msg("unknown lampm subcommand: %s", argv[arg_start]);
    fprintf(stderr, "\n");
    print_usage(argv[0]);
    return 1;
}
