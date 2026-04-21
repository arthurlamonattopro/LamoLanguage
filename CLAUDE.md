# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make          # Build the compiler (produces 'lamo' executable)
make clean    # Remove build artifacts
```

## Running

```bash
./lamo <file.lamo>    # Compile and run a Lamo source file
```

The compiler transpiles Lamo code to C, compiles it with GCC, and executes the result.

## Architecture

Lamo is a transpiled language that compiles to C. The pipeline is:

**Source (.lamo) → Lexer → Parser → AST → Codegen → C Code → GCC → Execution**

### Core Components

- **lexer_v2.c/h** - Tokenizes source into tokens (keywords, identifiers, literals, operators)
- **parser_v2.c/h** - Recursive descent parser producing an AST; handles expressions with operator precedence
- **ast.c/h** - AST node definitions and constructors for all language constructs (functions, variables, control flow, expressions)
- **codegen.c/h** - Transpiles AST to C code; manages indentation and function prototypes
- **lamo_v2.c** - Entry point: reads file, orchestrates compilation pipeline

### Language Features

- Variables: `let x = 10;`
- Functions: `fn name(params) { ... return expr; }`
- Control flow: `if/else`, `while`, `for`
- Built-ins: `print()`, `input()`, `exit()`, `abs()`, `isnumber()`, `isstring()`
- Operators: arithmetic, comparison, logical, assignment (including `+=`, `-=`, `++`, `--`)

### Code Style

- C99 standard with POSIX extensions
- Error handling via `fprintf(stderr)` and `exit(1)` for fatal errors
- Memory management: manual `malloc`/`free`; AST nodes freed after code generation