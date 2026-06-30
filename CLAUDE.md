# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make          # Build the compiler (produces 'lamo' executable)
make clean    # Remove build artifacts
make test     # Run the regression suite (valid / invalid / runtime tests)
```

The build automatically re-runs `scripts/embed_runtime.py` whenever
`src/codegen/lamo_runtime.h` changes, regenerating
`src/codegen/lamo_runtime_data.c` (the runtime source as an embedded C string).
Python is required for this; if absent, the build proceeds with the stale
generated file (which is committed to the repo).

## Running

```bash
./lamo run   <file.lamo>            # Compile and run a Lamo source file
./lamo build <file.lamo> -o demo    # Compile to a binary, do not run
./lamo check <file.lamo>            # Parse + semantic-check only
./lamo test                          # Run the test suite (tests/run_tests.sh)
./lamo fmt   <file.lamo>            # Normalize source formatting in place
./lamo help                          # Show usage
./lamo version                       # Print version
```

The compiler transpiles Lamo to C (`lamo_exec.c`), compiles it with GCC, and
executes the result.

### Namespaced Imports (Sprint 4)

`import "math.lamo" as math;` exposes the imported file's top-level
declarations under the `math` alias. Call them with `math.sqrt(25)`.
The loader renames declarations to `lamo_mod_<alias>__<name>` and
registers them in `src/modules.c`'s `LamoModuleRegistry`. The semantic
pass and codegen consult this registry to resolve `AST_MEMBER_CALL`
nodes. Legacy `import "math.lamo";` (without `as`) still merges into
the global namespace as before.

## Architecture

Lamo is a transpiled language that compiles to C. The pipeline is:

**Source (.lamo) → Lexer → Parser → AST → Semantic → Codegen → C Code → GCC → Execution**

### Core Components

- **src/lexer/lexer.c** — Tokenizes source into tokens (keywords, identifiers, literals, operators).
  File names use no `_v2` suffix (older docs may still reference `lexer_v2.c`).
- **src/parser/parser.c** — Recursive descent parser producing an AST. Handles
  expressions with operator precedence. Records syntax errors with
  `file:line:col` format (matching semantic.c) and recovers via synchronize-and-continue.
- **src/ast/ast.c** — AST node definitions and constructors for all language
  constructs. Each node carries a `file_path` pointer so multi-file builds can
  report errors against the right source file (Bug #5 fix).
- **src/semantic/semantic.c** — Scope-resolved type inference + operator checks.
  Walks the AST, defines variables/functions per scope, flags type errors at
  compile time (e.g. `"abc" * 3` is rejected), uses `node->file_path` for
  multi-file diagnostics.
- **src/codegen/codegen.c** — Transpiles AST to C code. Manages indentation
  and forward declarations. The runtime library is **not** inlined as fprintf
  calls anymore — it lives in `src/codegen/lamo_runtime.h` and is emitted as
  a pre-built string constant (`lamo_runtime_source`) at the top of every
  generated .c file. This cut ~600 lines of fprintf out of codegen.c.
- **src/codegen/lamo_runtime.h** — The actual Lamo runtime (value type,
  arithmetic ops, string arena, GUI runtime, HTTP runtime). All functions are
  marked `LAMO_UNUSED` so programs that don't use every op don't trigger
  `-Wunused-function`.
- **src/codegen/lamo_runtime_data.{c,h}** — Auto-generated embedded-string
  version of `lamo_runtime.h`. Regenerate with `python3 scripts/embed_runtime.py`.
- **src/eval/eval.c** — Tree-walking AST interpreter. Powers `lamo eval`
  and `lamo repl`. Independent of the C codegen backend.
- **src/lampm/lampm.c** — Integrated package manager. Originally a separate
  `lampm` binary (LamoPacketManager repo); now compiled into the main `lamo`
  executable. Reachable through `lamo install`, `lamo update`, `lamo list`,
  etc. The single public entry point is `lampm_main()` (see
  `src/lampm/lampm.h`), which `lamo_v2.c::main()` dispatches to when the
  subcommand is one of the package-manager ones (init, install, update,
  remove, list, info, outdated, why, lock, cache, doctor).
- **src/modules.c** + **src/modules.h** — Module registry (Sprint 4) backing
  the namespaced-import feature. The loader (`lamo_v2.c`) renames top-level
  declarations of an aliased import to `lamo_mod_<alias>__<name>` and
  registers them here; the semantic pass and codegen look them up via
  `lamo_modules_resolve_member()` to resolve `alias.fn(args)` calls.
- **src/lamo_v2.c** — Entry point: reads files, orchestrates compilation
  pipeline, handles imports recursively with cycle detection. Also parses
  global flags (--verbose / --quiet) and dispatches subcommands, including
  the package-manager ones via `lampm_main()`.

### Language Features

- Variables: `let x = 10;`
- Functions: `fn name(params) { ... return expr; }`
- Control flow: `if/else`, `while`, `for` with `break` and `continue`
- Truthiness: **Python-like**, not strict-bool. `if (5)` and `if ("abc")` are
  valid. `if (0)`, `if (0.0)`, `if ("")` are false. See README "Truthiness".
- Built-ins: `print`, `input`, `input_int`, `input_str`, `isnumber`,
  `isstring`, `exit`, `abs` — these are treated as ordinary identifiers by
  the lexer and resolved as builtins in the semantic pass and codegen (they
  are shadowable by user functions).
- Operators: arithmetic, comparison, logical, assignment (including `+=`, `-=`,
  `++`, `--`)
- GUI builtins (Windows-native, X11 on Linux/Mac, no-op elsewhere):
  `gui_open`, `gui_should_close`, `gui_begin_frame`, `gui_draw_rect`,
  `gui_draw_text`, `gui_end_frame`, `gui_close`
- HTTP server builtins: `http_route`, `http_serve`, `http_serve_once`
- **Namespaced imports (Sprint 4)**: `import "..." as alias;` exposes the
  imported file's top-level declarations under `alias`. Member access uses
  `alias.fn(args)` syntax. Legacy `import "...";` (without `as`) still
  merges into the global namespace.
- **Phase 2 language expansion**: structs (`struct Name { field: type, ... }`
  + `Name { field: value, ... }` literals), methods (`impl Type { fn method(args) { self.field ... } }`
  with implicit `self`), arrays (`[1, 2, 3]`, `arr[i]`, `arr[i] = value`,
  `arr.push(x)`, `arr.pop()`, `arr.len()`), enums (`enum Name { Variant, ... }`
  — variants are global int constants), match (`match scrutinee { Pattern => body, _ => default }`
  with exhaustiveness warnings), bare-identifier imports (`import math` is
  sugar for `import "math.lamo" as math`), and optional semicolons.

### Diagnostics (Sprint 4)

Errors print `file:line:col: <kind> error: <message>`, followed by the
source line with a caret pointing at the column, followed by an optional
`hint: <advice>` line. The `<kind>` label is colored red+bold when stderr
is a TTY; disable with `--no-color` or `LAMO_NO_COLOR=1`. Use
`parser_error_with_hint(p, msg, hint)` (parser) and
`semantic_error_at_hint(ctx, line, col, msg, hint)` (semantic) to attach
a hint to a new error site.

### Code Style

- C99 standard with POSIX extensions
- Error handling via `fprintf(stderr)` and `exit(1)` for fatal errors
- Memory management: manual `malloc`/`free`; AST nodes freed after code generation
- All runtime functions marked `LAMO_UNUSED` to avoid -Wunused-function on
  programs that don't use every builtin
- `user_name1()` returns a pointer into a 4-entry ring buffer; safe for up to
  4 simultaneous uses in the same C expression. Use `user_name()` with an
  explicit buffer for cases needing more.
