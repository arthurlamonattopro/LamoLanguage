# Lamo Language

Lamo is a small experimental programming language implemented in C. Today it has a lexer, parser, AST, a first semantic-analysis pass, and a C backend with `run`, `build`, and `check` commands.

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

Not implemented yet:

- typed variable or function declarations (types are inferred, not annotated)
- per-node file-aware diagnostics across merged multi-file builds
- a standard runtime/library design
- richer diagnostics with source snippets

## Language Features

Current syntax supported by the compiler includes:

- `let` variable declarations
- `fn` function declarations
- `if` / `else`
- `while`
- `for`
- `return`
- assignment with `=`, `+=`, `-=`, `++`, `--`
- integer, string, and boolean literals
- function calls
- builtins such as `print`, `input` (int), `input_int`, `input_str`, `isnumber`, `isstring`, `exit`, and `abs`
- Windows GUI builtins: `gui_open`, `gui_should_close`, `gui_begin_frame`, `gui_draw_rect`, `gui_draw_text`, `gui_end_frame`, and `gui_close`
- HTTP server builtins: `http_route`, `http_serve`, and `http_serve_once`

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
gcc -Wall -Wextra -std=c99 -Isrc -Isrc/lexer -Isrc/parser -Isrc/ast -Isrc/codegen -Isrc/semantic \
    src/lamo_v2.c src/lexer/lexer.c src/parser/parser.c src/ast/ast.c src/codegen/codegen.c src/semantic/semantic.c \
    -o lamo
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

Show help or version:

```sh
./lamo help
./lamo version
```

## Generated Output

The current backend emits C code to `lamo_exec.c`, then invokes `gcc` to build the executable. This is still a temporary backend strategy while the language model matures.

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

- [src/lexer/lexer.c](/l:/Codes/LamoLanguage/src/lexer/lexer.c)
- [src/parser/parser.c](/l:/Codes/LamoLanguage/src/parser/parser.c)
- [src/ast/ast.c](/l:/Codes/LamoLanguage/src/ast/ast.c)
- [src/semantic/semantic.c](/l:/Codes/LamoLanguage/src/semantic/semantic.c)
- [src/codegen/codegen.c](/l:/Codes/LamoLanguage/src/codegen/codegen.c)
- [src/lamo_v2.c](/l:/Codes/LamoLanguage/src/lamo_v2.c)
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
