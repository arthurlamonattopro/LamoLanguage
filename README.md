# Lamo Language

Lamo is a small experimental programming language implemented in C. Today it has a lexer, parser, AST, a first semantic-analysis pass, and a C backend with `run`, `build`, and `check` commands.

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

Not implemented yet:

- a real type system
- typed variables or functions
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
- builtins such as `print`, `input`, `isnumber`, `isstring`, `exit`, and `abs`

Example:

```lamo
let x = 10;
let y = 20;

fn add(a, b) {
    return a + b;
}

print(add(x, y));
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

## Tests

Run the regression suite with:

```sh
make test
```

The tests cover:

- valid parse and semantic-check cases
- invalid syntax cases
- invalid semantic cases
- end-to-end compile-and-run behavior
- CRLF and LF source handling

## Repository Layout

- [src/lexer/lexer.c](/l:/Codes/LamoLanguage/src/lexer/lexer.c)
- [src/parser/parser.c](/l:/Codes/LamoLanguage/src/parser/parser.c)
- [src/ast/ast.c](/l:/Codes/LamoLanguage/src/ast/ast.c)
- [src/semantic/semantic.c](/l:/Codes/LamoLanguage/src/semantic/semantic.c)
- [src/codegen/codegen.c](/l:/Codes/LamoLanguage/src/codegen/codegen.c)
- [src/lamo_v2.c](/l:/Codes/LamoLanguage/src/lamo_v2.c)
- [tests/run_tests.ps1](/l:/Codes/LamoLanguage/tests/run_tests.ps1)

## Contributor Note

The fastest inner loop is:

1. Build with `make`
2. Run `./lamo check <file.lamo>` while changing parser or semantics
3. Run `make test` before finishing changes

When changing syntax, update the parser, semantic pass, tests, and this README together so the documented language stays aligned with the compiler.
