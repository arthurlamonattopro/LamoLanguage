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
./lamo help                          # Show usage
./lamo version                       # Print version
```

The compiler transpiles Lamo to C (`lamo_exec.c`), compiles it with GCC, and
executes the result.

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

### Code Style

- C99 standard with POSIX extensions
- Error handling via `fprintf(stderr)` and `exit(1)` for fatal errors
- Memory management: manual `malloc`/`free`; AST nodes freed after code generation
- All runtime functions marked `LAMO_UNUSED` to avoid -Wunused-function on
  programs that don't use every builtin
- `user_name1()` returns a pointer into a 4-entry ring buffer; safe for up to
  4 simultaneous uses in the same C expression. Use `user_name()` with an
  explicit buffer for cases needing more.
