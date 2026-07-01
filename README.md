# Lamo Language

Lamo is a small experimental programming language implemented in C. It has a lexer, parser, AST, a semantic-analysis pass, and a C backend with `run`, `build`, `check`, and `eval` commands. The repository also ships an integrated package manager (formerly a separate `lampm` binary) reachable through the same `lamo` executable as `lamo install`, `lamo update`, `lamo list`, etc.

> **Philosophy:** *As simple as an interpreted language, as fast as a compiled one.*
> Lamo transpiles to C and is built by GCC, so it runs at native speed. But the
> language surface stays tiny, readable, and easy to learn.

Licensed under the [MIT License](./LICENSE).

## Current Status

Implemented now:

- lexical analysis
- parsing into an AST
- semantic checks for scopes, functions, structs, enums, and methods
- C code generation (transpilation to C, then GCC)
- compile, build, and run CLI flow
- fixture-based compiler tests
- **namespaced module system** (`import "..." as alias;` + `alias.fn(args)`)
- **bare-identifier imports** (`import math` and `import math as m`)
- **error hints** (`hint: did you forget ...?` lines under error snippets)
- **ANSI color** in diagnostics (auto-detected; disable with `--no-color`)
- **`lamo test`** and **`lamo fmt`** CLI commands
- **Phase 2 language features:**
  - **structs** — `struct Player { name: string, hp: int }` with field access and assignment
  - **methods** — `impl Player { fn damage(amount: int) { self.hp -= amount } }` with implicit `self`
  - **arrays** — literals, indexing, `arr.push(x)`, `arr.pop()`, `arr.len()`, `arr[i] = value`
  - **enums** — `enum Color { Red, Green, Blue }` with variants as int constants
  - **match** — `match color { Red => print("red"), _ => print("other") }` with exhaustiveness warnings
  - **optional semicolons** — both `let x = 5;` and `let x = 5` work

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
- module member call validation (`math.sqrt(x)` resolves through the
  module registry; arity is checked the same way as regular calls)
- **struct field validation** (unknown fields are rejected at compile time)
- **method validation** (unknown methods and wrong arity are rejected)
- **enum variant validation** (match patterns must be known variants or `_`)
- **match exhaustiveness warnings** (non-exhaustive matches are flagged)

Not implemented yet:

- ~~a standard runtime/library design~~ — **implemented** in Phase 3 (see
  the [Standard Library](#standard-library-std) section below)
- AST-based pretty-printer in `lamo fmt` (the current formatter only
  normalizes whitespace, line endings, and trailing newlines)
- generics / traits
- pattern matching beyond simple variant equality (no destructuring, no literals)

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
- builtins such as `print`, `input` (int), `input_int`, `input_str`, `isnumber`, `isstring`, `isarray`, `exit`, and `abs`
- Windows GUI builtins: `gui_open`, `gui_should_close`, `gui_begin_frame`, `gui_draw_rect`, `gui_draw_text`, `gui_end_frame`, and `gui_close`
- HTTP server builtins: `http_route`, `http_serve`, and `http_serve_once`
- **Phase 3 (standard library)**: dotted module imports `import std.io`,
  `import std.math as math`, etc. — see [Standard Library](#standard-library-std) below

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

Native HTTP example (official — uses opt-in mark-sweep GC, see `docs/MEMORY-MODEL.md`):

```lamo
http_route("/", "Lamo HTTP server");
http_route("/health", "ok");

print("HTTP server on http://127.0.0.1:8080");
http_serve(8080);
```

The HTTP server loop calls `gc_collect()` every 100 requests, so
long-running `http_serve()` no longer grows the arena without bound.
Programs that want finer control can call `gc_collect()` manually or
set an auto-trigger threshold with `gc_set_threshold(N_bytes)`. See
`docs/MEMORY-MODEL.md` for the full GC design.

### Modules and Namespaced Imports

Lamo supports three import forms:

- **Legacy global merge** — `import "math.lamo";` keeps the previous
  behavior: every top-level declaration in `math.lamo` becomes
  available in the global namespace as if it had been defined in the
  importing file. Use this for quick scripts where name collisions
  aren't a concern.
- **Namespaced (string path)** — `import "math.lamo" as math;` exposes the imported
  file's top-level functions and globals under the `math` alias. You
  then call them as `math.sqrt(25)`, `math.add(3, 4)`, etc. The alias
  can be any valid identifier — `import "math.lamo" as m;` and
  `m.sqrt(25)` work too.
- **Bare identifier** (Phase 2) — `import math` is sugar for
  `import "math.lamo" as math`. The module name is resolved to a file
  `<name>.lamo` in the importing file's directory. Use
  `import math as m` to specify a different alias.

Example:

```lamo
// math.lamo
fn sqrt(n) { return n * n }    // not really sqrt, just for the example
fn add(a, b) { return a + b }
```

```lamo
// main.lamo
import math              // bare identifier — same as `import "math.lamo" as math`

print(math.sqrt(5))      // 25
print(math.add(3, 4))    // 7
```

The semantic pass validates member access against the module registry:
- calling `math.unknown(...)` produces a clear error pointing at the
  call site, with the available members listed
- arity is checked the same way as regular function calls
  (`math.sqrt(1, 2, 3)` is rejected at compile time)

Known limitation: the `lamo eval` and `lamo repl` paths do not load
modules through the registry, so `math.sqrt(x)` in those modes raises
a clear "use `lamo run` instead" error. Use `lamo run` for any program
that uses namespaced imports.

### Structs (Phase 2)

A struct is a user-defined record with named fields:

```lamo
struct Player {
    name: string,
    hp: int,
    level: int
}

let p = Player { name: "Arthur", hp: 100, level: 1 }
print(p.name)        // Arthur
print(p.hp)          // 100

p.hp -= 30           // field assignment with -=
print(p.hp)          // 70
```

- Fields can be separated by `,`, `;`, or just newlines.
- Field order in the literal does NOT need to match the declaration order.
- Missing fields default to `0` (a warning is emitted at compile time).
- Field types are annotations only — Lamo is dynamically typed at runtime.

### Methods (Phase 2)

Methods are functions attached to a struct via `impl`:

```lamo
impl Player {
    fn damage(amount: int) {
        self.hp -= amount
    }
    fn heal(amount: int) {
        self.hp += amount
    }
    fn is_alive() {
        return self.hp > 0
    }
}

let p = Player { name: "Hero", hp: 50, level: 1 }
p.damage(20)
print(p.hp)              // 30
print(p.is_alive())      // 1 (true)
p.damage(100)
print(p.is_alive())      // 0 (false)
```

- `self` is implicit inside `impl` method bodies — do NOT declare it as a parameter.
- Methods are called as `obj.method(args)`.
- Method arity is validated at compile time.
- Methods can return values and call other methods on `self`.

### Arrays (Phase 2)

Arrays are dynamic, heterogeneous lists:

```lamo
let numbers = [1, 2, 3]
print(numbers.len())        // 3

numbers.push(4)
print(numbers.len())        // 4
print(numbers[3])           // 4

numbers[0] = 99
print(numbers[0])           // 99

let last = numbers.pop()
print(last)                 // 4
print(numbers.len())        // 3

// Iteration
let sum = 0
for (let i = 0; i < numbers.len(); i++) {
    sum += numbers[i]
}
print(sum)                  // 104 (99 + 2 + 3)
```

- `arr.push(x)`, `arr.pop()`, `arr.len()` are method-style calls.
- `len(arr)`, `push(arr, x)`, `pop(arr)` are the equivalent builtin-function forms.
- `arr[i] = value` is supported (index assignment).
- `arr[i] += value` and `arr[i] -= value` work (read-modify-write).
- Negative indices count from the end (`arr[-1]` is the last element).

### Enums (Phase 2)

An enum is a set of named integer constants:

```lamo
enum Color {
    Red,
    Green,
    Blue
}

let c = Red
print(c)                // 0
print(c == Red)         // 1 (true)
print(c != Blue)        // 1 (true)
```

- Variants are stored as `int` values (0, 1, 2, ...).
- Variants are accessible by name at top level (no `Color::Red` qualifier needed).
- Variant uniqueness within an enum is checked at compile time.

### Match (Phase 2)

`match` performs pattern matching on a value, dispatching to the first
matching arm:

```lamo
enum Direction {
    North,
    South,
    East,
    West
}

let d = East
match d {
    North => print("going north"),
    South => print("going south"),
    East => print("going east"),
    West => print("going west")
}
```

- Patterns are enum variant names or `_` (wildcard, matches anything).
- Arms are separated by `,` (or just newlines — both work).
- Arm bodies can be a single statement or a block `{ ... }`.
- A `_` arm is required for non-exhaustive matches; without it, the
  compiler emits a warning if not all enum variants are covered.

```lamo
match color {
    Red => print("red"),
    _ => print("not red")
}
```

## Build And Run

Build the compiler:

```sh
make
```

Or compile directly with GCC:

```sh
gcc -Wall -Wextra -std=c99 -O2 -g -Isrc -Isrc/lexer -Isrc/parser -Isrc/ast -Isrc/codegen -Isrc/semantic -Isrc/eval \
    src/lamo_v2.c src/lexer/lexer.c src/parser/parser.c src/ast/ast.c src/codegen/codegen.c src/semantic/semantic.c src/eval/eval.c src/lampm/lampm.c src/modules.c src/codegen/lamo_runtime_data.c \
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

Run the test suite:

```sh
./lamo test
```

Invokes `tests/run_tests.sh` (POSIX) or `tests/run_tests.ps1` (Windows),
discovering `.lamo` files under `tests/valid/` (must `check` successfully),
`tests/invalid/` (must fail `check`), and `tests/runtime/` (must `run` and
produce stdout matching the sibling `.expected` file). With no argument, uses
`./lamo`; pass a path to use a different binary. Exits 0 on full pass, 1 on
any failure.

Normalize source formatting:

```sh
./lamo fmt main.lamo
./lamo fmt main.lamo lib.lamo        # multiple files
./lamo fmt --check main.lamo         # CI mode: print diff, exit non-zero if needs formatting
```

Rewrites each file in place with conservative, idempotent transformations:
CRLF → LF, tabs → 4 spaces, trailing whitespace stripped, file ends with
exactly one newline. Does **not** reflow expressions or reindent blocks — a
full AST-based pretty-printer is future work (see roadmap). Safe to run on
any `.lamo` file.

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
LAMO_CC         C compiler to use for `run`/`build` (default: gcc)
LAMO_VERBOSE    Same as --verbose
LAMO_QUIET      Same as --quiet
LAMO_NO_COLOR   Same as --no-color (disables ANSI color in diagnostics)
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

Since compiler 2.3.0, Lamo ships an **opt-in mark-sweep garbage collector**
(see `docs/MEMORY-MODEL.md`). Strings and arrays produced by dynamic
operations are tracked in a heap list with per-allocation headers. The GC
runs when:

- a program calls `gc_collect()` explicitly,
- the runtime has allocated `gc_set_threshold(N)` bytes since the last
  collection (auto-trigger, off by default),
- the HTTP server loop hits its every-100-requests checkpoint, or
- the GUI event loop hits its every-1000-frames checkpoint.

Programs that never call `gc_*` see the same behavior as 2.2.0:
allocations accumulate in the arena and are freed in one shot via
`atexit()` when the program exits. The only cost of having the GC
available is a 16-byte header per allocation (negligible).

The GC is exact (compiler-driven root enumeration — the codegen emits
`LAMO_GC_PUSH_ROOT`/`LAMO_GC_POP_ROOTS_N` for every `LamoValue` local),
stop-the-world, single-threaded, and handles cycles correctly (mark-sweep,
not refcounting). See `docs/MEMORY-MODEL.md` for the full design,
limitations, and rollout history.

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
- **standard library tests** (`std/tests/*.lamo`) — 15 test files covering
  every std module, run automatically as part of `make test`

## Standard Library (std/)

Lamo ships with a complete standard library under `std/`. It is
automatically available to every program via the dotted import syntax —
no installation required:

```lamo
import std.io              // alias `io` (default: last segment)
import std.math as math    // explicit alias
import std.fs as fs

io.println("Hello, std!")
io.println("sqrt(16) = " + math.sqrt(16))

if (fs.exists("config.txt")) {
    io.println(fs.readText("config.txt"))
}
```

### Available Modules

| Module              | Description                                                  |
|---------------------|--------------------------------------------------------------|
| `std.io`            | Console I/O — `println`, `eprint`, `readLine`, `write`       |
| `std.fs`            | File system — `exists`, `readText`, `writeText`, `listFiles` |
| `std.path`          | Path manipulation — `join`, `parent`, `filename`, `normalize`|
| `std.string`        | UTF-8 strings — `length`, `split`, `trim`, `replace`, `upper`|
| `std.math`          | Math — `sqrt`, `pow`, `sin`, `floor`, `PI`, `E`, `TAU`       |
| `std.random`        | PRNG — `int`, `float`, `bool`, `choice`, `shuffle`, `seed`   |
| `std.time`          | Time — `now`, `sleep`, `monotonic`, `timestamp`              |
| `std.collections`   | List, Stack, Queue, HashMap, HashSet                         |
| `std.process`       | Process — `run`, `exec`, `currentPid`, `exit`                |
| `std.env`           | Environment variables — `get`, `set`, `remove`, `has`        |
| `std.os`            | OS info — `name`, `arch`, `cpuCount`, `home`, `tempDir`      |
| `std.net`           | HTTP client — `get`, `post`                                  |
| `std.json`          | JSON — `parse`, `stringify`                                  |
| `std.testing`       | Test framework — `begin`, `end`, `assertEqual`, `summary`    |
| `std.debug`         | Debugging — `log`, `dump`, `time`, `timeEnd`, `trace`        |

See [std/README.md](./std/README.md) for the full module reference and
implementation notes. Each module also has its own `.md` file with a
function reference and runnable examples in `std/examples/`.

### How std/ Resolution Works

When the loader sees `import std.io`, it looks for `std/io.lamo` in:

1. `$LAMO_STD_DIR/io.lamo` (env override, dev/CI use)
2. `<bindir>/std/io.lamo` (shipped alongside the compiler binary)
3. `<bindir>/../std/io.lamo` (development layout — this is what `make`
   produces: the `lamo` binary sits at the repo root, and `std/` is
   right next to it)
4. `<bindir>/../share/lamo/std/io.lamo` (system install)
5. `./std/io.lamo` (current working directory)
6. `<importing_file_dir>/std/io.lamo` (local override)

The first match wins. This lets users override individual stdlib modules
by placing files in `./std/` next to their program.

## Repository Layout

- [src/lexer/lexer.c](./src/lexer/lexer.c) — tokenizer
- [src/parser/parser.c](./src/parser/parser.c) — recursive-descent parser
- [src/ast/ast.c](./src/ast/ast.c) — AST node constructors and free
- [src/semantic/semantic.c](./src/semantic/semantic.c) — scope + type analysis
- [src/codegen/codegen.c](./src/codegen/codegen.c) — AST → C transpiler
- [src/codegen/lamo_runtime.h](./src/codegen/lamo_runtime.h) — runtime (value type, arena, GUI, HTTP)
- [src/eval/eval.c](./src/eval/eval.c) — tree-walking interpreter (powers `lamo eval` and `lamo repl`)
- [src/lampm/](./src/lampm) — integrated package manager, split across:
  - [lampm.c](./src/lampm/lampm.c) — entry point (`lampm_main`, `lampm_is_subcommand`, `lampm_configure`, all `command_*` handlers, `install_dependency`)
  - [lampm.h](./src/lampm/lampm.h) — public API
  - [lampm_internal.h](./src/lampm/lampm_internal.h) — shared types (`Manifest`, `Lockfile`, `Dependency`, `LockEntry`) + helper decls
  - [lampm_util.c](./src/lampm/lampm_util.c) — string/fs helpers + output helpers
  - [lampm_manifest.c](./src/lampm/lampm_manifest.c) — `lamo.pkg` parsing/writing
  - [lampm_lockfile.c](./src/lampm/lampm_lockfile.c) — `lamo.lock` parsing/writing
  - [lampm_git.c](./src/lampm/lampm_git.c) — git operations + repo-spec parsing
- [src/modules.c](./src/modules.c) + [src/modules.h](./src/modules.h) — module registry backing the namespaced-import feature (`import "..." as alias;` + `alias.fn(args)`)
- [src/cli/](./src/cli) — CLI layer split out of `lamo_v2.c`:
  - [cli_options.h](./src/cli/cli_options.h) / [cli_options.c](./src/cli/cli_options.c) — global flags (`g_verbose`, `g_quiet`, `g_no_color`), `LamoCommand` enum, `VERSION`
  - [paths.h](./src/cli/paths.h) / [paths.c](./src/cli/paths.c) — path utilities, `resolve_import_path`, `read_file`, `lamo_cc`, `executable_suffix`
  - [import_resolver.h](./src/cli/import_resolver.h) / [import_resolver.c](./src/cli/import_resolver.c) — `CompilationState`, recursive loader, import cycle detection, module declaration renaming
  - [commands.h](./src/cli/commands.h) / [commands.c](./src/cli/commands.c) — `command_new`, `command_clean`, `command_repl`, `command_test`, `command_fmt`
  - [compile.h](./src/cli/compile.h) / [compile.c](./src/cli/compile.c) — `compile_sources` + helpers (GUI detection, source/module callbacks)
  - [help.h](./src/cli/help.h) / [help.c](./src/cli/help.c) — `print_usage`, `print_command_help`
- [src/lamo_v2.c](./src/lamo_v2.c) — thin CLI entry point: env-var parsing, global-flag parsing, subcommand dispatch (~380 lines)
- [docs/](./docs) — design documents:
  - [SPEC.md](./docs/SPEC.md) — authoritative language specification
  - [TYPE-SYSTEM.md](./docs/TYPE-SYSTEM.md) — type-system decision record (hybrid inference)
  - [MEMORY-MODEL.md](./docs/MEMORY-MODEL.md) — memory model + GC design + rollout plan
  - [RFC-generics.md](./docs/RFC-generics.md) — draft RFC for parametric generics
- [tests/run_tests.sh](./tests/run_tests.sh) — POSIX test runner
- [tests/run_tests.ps1](./tests/run_tests.ps1) — Windows PowerShell test runner
- [tests/valid/](./tests/valid) — programs that must `check` successfully
- [tests/invalid/](./tests/invalid) — programs that must fail `check`
- [tests/runtime/](./tests/runtime) — programs that must `run` and match expected stdout
- [tests/fixtures/](./tests/fixtures) — helper files imported by other tests (e.g. the module-alias helper)
- [std/](./std) — official standard library (Phase 3): 14 modules covering io, fs, path, string, math, random, time, collections, process, env, os, net, json, testing, and debug. See [std/README.md](./std/README.md).

## Contributor Note

The fastest inner loop is:

1. Build with `make`
2. Run `./lamo check <file.lamo>` while changing parser or semantics
3. Run `make test` before finishing changes

When changing syntax, update the parser, semantic pass, tests, and this README together so the documented language stays aligned with the compiler.
