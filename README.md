# Lamo Language

Lamo is a small experimental programming language implemented in C. Today it has a lexer, parser, AST, a first semantic-analysis pass, and a C backend with `run`, `build`, `check`, and `eval` commands. The repository also ships an integrated package manager (formerly a separate `lampm` binary) reachable through the same `lamo` executable as `lamo install`, `lamo update`, `lamo list`, etc.

Licensed under the [MIT License](./LICENSE).

## Current Status

Implemented now:

- lexical analysis
- parsing into an AST
- semantic checks for scopes and functions
- C code generation
- compile, build, and run CLI flow
- fixture-based compiler tests

Implemented in semantics:

- block, function, and global scopes
- duplicate declaration errors in the same scope
- undeclared variable errors
- undeclared function call errors
- function call arity validation
- `return` validation outside functions
- compile-time type inference and operator checks (e.g. `"abc" * 3` is rejected at compile time, not at runtime)
- per-node file-aware diagnostics across merged multi-file builds (errors
  inside an imported file point to that file, not to `<multiple inputs>`)

Not implemented yet:

- typed variable or function declarations (types are inferred, not annotated)
- a standard runtime/library design
- richer diagnostics with source snippets
- arrays, structs, and maps (only `string` is a composite type today)
- module system with explicit exports (today all `import`ed declarations are
  merged into a single global namespace)

## Language Features

Current syntax supported by the compiler includes:

- `let` variable declarations
- `fn` function declarations
- `if` / `else`
- `while`
- `for`
- `return`
- `break` and `continue` inside `while` and `for` loops
- assignment with `=`, `+=`, `-=`, `++`, `--`
- integer, string, and boolean literals
- function calls
- builtins such as `print`, `input` (int), `input_int`, `input_str`, `isnumber`, `isstring`, `exit`, and `abs`
- Windows GUI builtins: `gui_open`, `gui_should_close`, `gui_begin_frame`, `gui_draw_rect`, `gui_draw_text`, `gui_end_frame`, and `gui_close`
- HTTP server builtins: `http_route`, `http_serve`, and `http_serve_once`

### Truthiness

Lamo uses **Python-like truthiness**, not strict-bool. `if (cond)` accepts any
type and treats it as follows:

| Type     | Truthy when                                  |
|----------|----------------------------------------------|
| `int`    | non-zero (`0` is false, `1`, `-5`, `42` are true) |
| `float`  | non-zero (`0.0` is false, `3.14` is true)    |
| `bool`   | the value itself (`true` / `false`)          |
| `string` | non-empty (`""` is false, any other string is true) |

So `if (5) { ... }` and `if ("abc") { ... }` are both valid and execute the
body. `if (0)`, `if (0.0)`, `if (false)`, and `if ("")` skip the body. The
logical operators `&&`, `||`, and `!` also use this rule. The same applies to
`while (cond)` and `for (...; cond; ...)`.

This is a deliberate language decision, not an accident: Lamo does not have a
distinct "boolean context" type. If you want strict bool, compare explicitly
(e.g. `if (n > 0)`, `if (s != "")`).

Example:

```lamo
let x = 10;
let y = 20;

fn add(a, b) {
    return a + b;
}

print(add(x, y));
```

Native GUI example:

```lamo
gui_open(640, 480, "Lamo GUI");

while (gui_should_close() == 0) {
    gui_begin_frame(245, 245, 245);
    gui_draw_rect(40, 40, 220, 80, 40, 120, 220);
    gui_draw_text("Hello from Lamo", 60, 65, 255, 255, 255);
    gui_end_frame();
}

gui_close();
```

Native HTTP example:

```lamo
http_route("/", "Lamo HTTP server");
http_route("/health", "ok");

print("HTTP server on http://127.0.0.1:8080");
http_serve(8080);
```

## Build And Run

Build the compiler:

```sh
make
```

Or compile directly with GCC:

```sh
gcc -Wall -Wextra -std=c99 -O2 -g -Isrc -Isrc/lexer -Isrc/parser -Isrc/ast -Isrc/codegen -Isrc/semantic -Isrc/eval \
    src/lamo_v2.c src/lexer/lexer.c src/parser/parser.c src/ast/ast.c src/codegen/codegen.c src/semantic/semantic.c src/eval/eval.c src/codegen/lamo_runtime_data.c \
    -o lamo -lm
```

Run a program:

```sh
./lamo run examples/test.lamo
```

Run multiple source files together:

```sh
./lamo run examples/main.lamo
```

And inside `examples/main.lamo`:

```lamo
import "math.lamo";
```

Build a program without running it:

```sh
./lamo build examples/test.lamo -o demo
```

Check parsing and semantics only:

```sh
./lamo check examples/test.lamo
```

Interpret a program without going through GCC (uses the built-in
tree-walking evaluator):

```sh
./lamo eval examples/test.lamo
```

Start an interactive REPL:

```sh
./lamo repl
```

In the REPL, lines starting with `let`, `fn`, `if`, `while`, `for`,
`return`, `break`, `continue`, or `import` are parsed as statements;
everything else is parsed as an expression and its value is printed:

```
lamo> 1 + 2
3
lamo> let x = 10;
lamo> x * 2
20
lamo> fn double(n) { return n * 2; }
lamo> double(21)
42
lamo> .exit
```

Scaffold a new Lamo project:

```sh
./lamo new my-app
cd my-app
./lamo run main.lamo
```

This creates `my-app/main.lamo` (a hello-world entry point), `my-app/.gitignore`
(ignoring `lamo_exec*` and `lamo_modules/`), and `my-app/lamo.pkg` (an empty
package manifest, ready for `lampm install`).

Remove generated build artifacts:

```sh
./lamo clean
```

This deletes `lamo_exec.c`, `lamo_exec`, and `lamo_exec.exe` from the current
directory. Source files are untouched.

Show help or version:

```sh
./lamo help
./lamo help run
./lamo version
./lamo version --verbose
```

### Global Options

```
--verbose   Show extra progress information (also: LAMO_VERBOSE=1)
--quiet     Suppress success messages (also: LAMO_QUIET=1)
```

### Environment Variables

```
LAMO_CC       C compiler to use for `run`/`build` (default: gcc)
LAMO_VERBOSE  Same as --verbose
LAMO_QUIET    Same as --quiet
```

Example: use `clang` instead of `gcc`:

```sh
LAMO_CC=clang ./lamo run examples/test.lamo
```

Example: see exactly what GCC command is being invoked:

```sh
./lamo --verbose run examples/test.lamo
```

## Package Manager (Integrated)

The package manager (originally a separate `lampm` binary) is now built into
the `lamo` executable. There is no separate binary to install — every
package-manager subcommand is available directly:

```
lamo init [project-name]              Create a new lamo.pkg (and scaffold)
lamo install [owner/repo@ref] [alias] Install a dependency (or all)
lamo update [alias]                   Pull latest HEAD for one or all deps
lamo remove <alias>                   Remove a dependency and its install dir
lamo list                             List dependencies and their state
lamo info <alias>                     Show details about a dependency
lamo outdated                         Check which deps are behind remote HEAD
lamo why <alias>                      Alias for `info`
lamo lock                             Refresh the lockfile from installed deps
lamo cache <clean|list>               Manage the local packages directory
lamo doctor                           Verify your environment is set up
```

Quick start:

```sh
lamo new my-app          # scaffold a project (creates my-app/main.lamo, .gitignore, lamo.pkg)
cd my-app
lamo install arthurlamonattopro/LamoLanguage   # add a dependency
lamo install arthurlamonattopro/LamoLanguage@v1.0.0   # pinned to a tag
lamo list                # see installed deps and locked commits
lamo run main.lamo       # build and run the project
lamo clean               # remove generated lamo_exec* artifacts
lamo cache clean         # remove lamo_modules/
```

Repository specs accepted by `lamo install`:

```
owner/repo                       GitHub shorthand (HEAD)
owner/repo@v1.0.0                pinned to tag/branch/commit
github.com/owner/repo
https://github.com/owner/repo[.git]
https://gitlab.com/owner/repo[.git]
git+https://example.com/foo/bar.git
git@github.com:owner/repo.git
```

The lockfile (`lamo.lock`) records the exact commit installed for each
dependency so that `lamo install` (no args) reproduces the same checkout
across machines. Commit it alongside `lamo.pkg`.

The package manager also adds the `--no-color` global flag (auto-disabled
on non-TTY output) on top of the compiler's `--verbose` / `--quiet` flags.

## Generated Output

The current backend emits C code to `lamo_exec.c`, then invokes the C
compiler (configurable via `LAMO_CC`, default `gcc`) to build the executable.
This is still a temporary backend strategy while the language model matures.
Use `lamo clean` to remove these generated artifacts.

On Windows, GUI builtins are lowered to Win32 + GDI in the generated C and can open real native windows. On non-Windows platforms they compile to no-op stubs with a runtime warning for now.

### Memory Model

The runtime does not have a garbage collector. Strings produced by dynamic operations (concatenation via `+`, `input_str`, etc.) are tracked in a small string arena that is freed in one shot via `atexit()` when the program exits. This avoids the leak that used to happen on `s = s + "x"` patterns inside loops.

Known limitation: long-running programs (e.g. an HTTP server) still see the arena grow while they run, because the runtime has no way to know which strings are still reachable. For a prototype educational language this is acceptable; a real GC or ownership model is future work.

## Tests

Run the regression suite with:

```sh
make test
```

On Windows the same target invokes `powershell -File tests/run_tests.ps1`; on Unix it runs `sh tests/run_tests.sh`. Both runners discover `.lamo` files under `tests/valid/` (must `check` successfully), `tests/invalid/` (must fail `check`), and `tests/runtime/` (must `run` and produce stdout matching the sibling `.expected` file). Runtime tests may also include a `.stdin` file to feed stdin to the program.

The tests cover:

- valid parse and semantic-check cases
- invalid syntax cases
- invalid semantic cases (including type errors like `"abc" * 3`)
- end-to-end compile-and-run behavior with stdout comparison
- CRLF and LF source handling

## Repository Layout

- [src/lexer/lexer.c](./src/lexer/lexer.c) — tokenizer
- [src/parser/parser.c](./src/parser/parser.c) — recursive-descent parser
- [src/ast/ast.c](./src/ast/ast.c) — AST node constructors and free
- [src/semantic/semantic.c](./src/semantic/semantic.c) — scope + type analysis
- [src/codegen/codegen.c](./src/codegen/codegen.c) — AST → C transpiler
- [src/codegen/lamo_runtime.h](./src/codegen/lamo_runtime.h) — runtime (value type, arena, GUI, HTTP)
- [src/eval/eval.c](./src/eval/eval.c) — tree-walking interpreter (powers `lamo eval` and `lamo repl`)
- [src/lampm/lampm.c](./src/lampm/lampm.c) — integrated package manager (formerly `lampm`)
- [src/lampm/lampm.h](./src/lampm/lampm.h) — public API for the package manager
- [src/lamo_v2.c](./src/lamo_v2.c) — CLI entry point: parses global flags, dispatches subcommands
- [tests/run_tests.sh](./tests/run_tests.sh) — POSIX test runner
- [tests/run_tests.ps1](./tests/run_tests.ps1) — Windows PowerShell test runner
- [tests/valid/](./tests/valid) — programs that must `check` successfully
- [tests/invalid/](./tests/invalid) — programs that must fail `check`
- [tests/runtime/](./tests/runtime) — programs that must `run` and match expected stdout

## Contributor Note

The fastest inner loop is:

1. Build with `make`
2. Run `./lamo check <file.lamo>` while changing parser or semantics
3. Run `make test` before finishing changes

When changing syntax, update the parser, semantic pass, tests, and this README together so the documented language stays aligned with the compiler.
