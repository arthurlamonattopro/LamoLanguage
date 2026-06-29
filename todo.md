# Lamo TODO

## Foundation

- [x] Review `README.md` and align it with the current implementation.
- [x] Remove outdated references to old file names and project structure.
- [ ] Define a clear directory strategy for compiler stages and future runtime/tests folders.
- [x] Add a short contributor note explaining how to build and run the compiler.

## Phase 1: Stabilize The Current Compiler

### Parser And Frontend Reliability

- [x] Remove silent token skipping in the parser and replace it with explicit syntax errors.
- [x] Audit parser branches for missing validation and edge cases.
- [x] Improve parser error messages with clearer expected/actual token output.
- [x] Add more source location coverage to frontend errors.
- [x] Move `import` parsing out of the CLI preprocessor and into the real frontend grammar.
- [x] Implement error recovery (synchronize-and-continue) so multiple syntax errors are reported in one pass.

### CLI And Build Flow

- [x] Standardize compiler exit codes for success, compile failure, and backend failure.
- [x] Improve the CLI usage text and error messages.
- [ ] Check whether GCC invocation failures are surfaced clearly to the user.
- [ ] Decide whether generated C files should always be emitted or only in debug/build mode.
- [x] Make the default Windows build produce `lamo.exe`.
- [x] Fix `make clean` on Windows.

### Regression Safety

- [x] Add regression coverage for CRLF and LF source files.
- [x] Add regression coverage for functions, recursion, loops, and assignments.
- [x] Add regression coverage for strings, booleans, and builtin functions.
- [x] Add regression coverage for invalid syntax cases.
- [x] Add regression coverage for imported multi-file programs.

## Phase 2: Testing Infrastructure

- [x] Create a `tests/` directory structure for valid and invalid `.lamo` programs.
- [x] Add a simple test runner script or Make target for compiler tests.
- [ ] Add parser smoke tests.
- [x] Add end-to-end tests that compile and run sample programs.
- [ ] Add snapshot or golden tests for generated C output where useful.
- [x] Add tests for expected compiler diagnostics.
- [ ] Add regression coverage for duplicate imports.
- [x] Add regression coverage for import cycles.

## Phase 3: Semantic Analysis

### Scopes And Symbols

- [x] Create a semantic analysis pass between parsing and code generation.
- [x] Add a symbol table implementation.
- [x] Support global scope.
- [x] Support function scope.
- [x] Support block scope.
- [ ] Track declarations by scope level.

### Semantic Validation

- [x] Detect use of undeclared variables.
- [x] Detect duplicate variable declarations in the same scope.
- [ ] Detect duplicate function declarations.
- [x] Validate function call argument counts.
- [ ] Validate that assignment targets are valid identifiers.
- [x] Validate `return` outside functions.
- [ ] Validate function names and variable names against reserved words if needed.
- [x] Extend semantic analysis across file boundaries.
- [ ] Define import-time duplicate symbol rules.

### Diagnostics

- [x] Add semantic error reporting with line and column.
- [x] Make semantic errors stop code generation.
- [ ] Add tests for every semantic error category.
- [ ] Show the originating file path for each node in merged multi-file semantic errors.

## Phase 4: Type System

### Type Model

- [ ] Define a `Type` representation in the compiler.
- [ ] Decide whether Lamo will use explicit typing, inference, or a mixed model.
- [x] Define the initial built-in types: `int` (int64), `float`, `bool`, `string`, `void`.
- [ ] Attach inferred or declared types to AST nodes after analysis.

### Type Rules

- [x] Validate arithmetic operators by type.
- [x] Validate comparison operators by type.
- [ ] Validate logical operators by type.
- [x] Validate unary operators by type.
- [x] Validate assignment compatibility.
- [ ] Validate function parameter types.
- [ ] Validate function return types.
- [x] Define equality semantics.
- [x] Define truthiness or require explicit booleans in conditions.
- [x] Decide whether implicit conversions exist.

### Backend Alignment

- [x] Stop hardcoding every variable as `int` in code generation.
- [x] Stop hardcoding every function signature as returning/accepting `int`.
- [ ] Make `print()` use semantic type information.
- [x] Make `input()` use semantic type information (split into `input_int` / `input_str`).
- [x] Make builtin type predicates reflect actual types instead of placeholder logic.
- [x] Emit top-level `let` declarations as C globals (with initializers in `main()`) so functions can reference them — fixes the leak of C behavior into language semantics.

## Phase 5: Runtime Design

### Runtime Basics

- [ ] Decide whether Lamo has a runtime support library.
- [ ] Create a minimal runtime layer for helper functions if needed.
- [ ] Define runtime conventions for strings.
- [ ] Define runtime conventions for booleans.
- [ ] Define runtime error behavior.

### Strings

- [ ] Decide whether strings are immutable.
- [ ] Decide how strings are stored and passed.
- [x] Support printing string variables reliably.
- [x] Support string input if the language will allow it.
- [x] Decide whether string comparison is by value.

### Memory Model

- [ ] Decide whether Lamo will expose manual memory control, ownership rules, or a managed model.
- [x] Document who owns allocated runtime values.
- [x] Make generated code follow the chosen ownership rules (string arena tracked and freed via `atexit`).

## Phase 6: Language Specification

- [ ] Write a small language spec for syntax, scope, evaluation, and imports.
- [ ] Document variable declaration semantics.
- [ ] Document function semantics.
- [ ] Document condition and loop semantics.
- [ ] Document operator precedence and associativity.
- [ ] Document builtin behavior.
- [ ] Document type rules and conversions.
- [ ] Document runtime error cases.
- [ ] Document import resolution and duplicate import behavior.

## Phase 7: Language Features

### High-Priority Features

- [ ] Decide whether typed variable declarations should be added.
- [ ] Decide whether typed function signatures should be added.
- [ ] Add `break`.
- [ ] Add `continue`.

### Data Structures

- [ ] Design array syntax and semantics.
- [ ] Add arrays only after type and runtime rules are ready.
- [ ] Decide whether to support structs/records.
- [ ] Decide whether maps/dictionaries belong in the core language or standard library.

### Expressions And Statements

- [ ] Decide whether assignment should remain statement-only or become an expression.
- [ ] Decide whether function declarations are hoisted or order-dependent.
- [x] Decide whether top-level `return` is invalid or has script semantics.

## Phase 8: Standard Library

- [ ] Define what belongs in the core language vs the standard library.
- [ ] Expand I/O builtins beyond `print` and `input` if needed.
- [ ] Add numeric helpers only when type semantics are stable.
- [ ] Add string helpers after string runtime behavior is defined.
- [ ] Plan module-based standard library organization.

## Phase 9: Developer Experience

### CLI

- [x] Add `lamo run`.
- [x] Add `lamo build`.
- [x] Add `lamo check`.
- [x] Add `lamo version`.
- [x] Add `lamo help`.

### Diagnostics And UX

- [ ] Show file path, line, and column in all compiler errors.
- [ ] Consider showing a source snippet with a caret marker.
- [ ] Improve backend failure messages when generated C fails to compile.
- [ ] Make successful compiler output less noisy unless verbose mode is enabled.

### Formatting And Style

- [ ] Define a canonical code style for `.lamo` files.
- [ ] Decide whether to build a formatter now or later.
- [ ] If not building a formatter yet, document style rules in the repo.

## Phase 10: Multi-File Projects

- [x] Design module/import syntax.
- [ ] Define symbol visibility rules.
- [x] Support compiling multiple `.lamo` files in one build.
- [x] Extend semantic analysis across file boundaries.
- [ ] Decide on project/package layout conventions.
- [x] Detect and report import cycles more clearly.
- [ ] Attach file ownership to AST nodes instead of using a merged program label.

## Phase 11: Backend Evolution

- [x] Keep the C backend as the short-term primary target.
- [ ] Refactor codegen so backend behavior depends on semantic information.
- [ ] Separate frontend and backend more clearly in the architecture.
- [ ] Decide whether to add an interpreter for faster feedback.
- [ ] Revisit VM or LLVM ideas only after semantics and runtime are mature.

## Cross-Cutting Work

- [x] Add tests whenever a new feature is added.
- [x] Keep docs updated as behavior changes.
- [x] Remove shortcuts that leak C behavior into language semantics.
- [x] Check Windows and Unix-like compatibility regularly.
- [ ] Avoid adding syntax before semantics are defined.

## Suggested Next Actions

- [x] Fix `make clean` on Windows.
- [x] Move `import` into the lexer/parser/AST instead of preprocessing source text in `src/lamo_v2.c`.
- [ ] Add duplicate-function and duplicate-import semantic tests.
- [x] Introduce a `Type` enum and start annotating AST nodes.
- [ ] Preserve per-file source ownership in multi-file diagnostics.

## Recently Fixed (Lint Pass)

- [x] Remove `lexer_peek_token` leak by deleting the unused function.
- [x] Replace `parser_error` exit-on-first-error with synchronize-and-continue (collects all errors).
- [x] Switch int literals to `long long` + `strtoll`; add lexer support for hex (`0x..`), binary (`0b..`), underscores (`1_000_000`) and floats.
- [x] Decode string escapes (`\n`, `\t`, `\r`, `\\`, `\"`, `\0`, `\xNN`) in the lexer; reject literal newlines in strings.
- [x] Emit top-level `let`s as C globals with initializers in `main()` so user functions can reference them.
- [x] Move `import` resolution from textual preprocessing to real AST nodes (`AST_IMPORT`) loaded after parsing.
- [x] Add an X11 backend for GUI builtins on Linux/macOS (Windows backend unchanged).
- [x] Treat `print`, `input`, `isnumber`, `isstring`, `exit`, `abs` as regular identifiers resolved via builtin table; allow shadowing.
- [x] Prefix all user identifiers in generated C with `lamo_u_` to avoid collisions with libc names (`abs`, `exit`, ...).
- [x] Add `_DEFAULT_SOURCE` feature macro so `realpath` is declared on Linux.
