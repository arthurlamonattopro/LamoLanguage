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
- [x] **Check whether GCC invocation failures are surfaced clearly to the user.**
      Done (2.3.0): `compile.c` now emits a clear `lamo: backend compilation
      failed (gcc exit code N)` header, points at the generated C source,
      suggests `LAMO_CC` override, and recommends `--verbose` for the full
      cc invocation. GCC's own stderr still flows through directly.
- [x] **Decide whether generated C files should always be emitted or only
      in debug/build mode.** Decision (2.3.0): ALWAYS emit `lamo_exec.c`,
      for both `run` and `build` modes. Rationale: debuggability (the .c is
      the IR), transparency (Lamo is "as fast as C because it IS C"),
      consistency (same policy in both modes). Documented in `compile.c`
      and `SPEC.md`. Users who want clean workflows can `lamo check` (no
      .c), add to `.gitignore` (lamo init does this), or `rm lamo_exec.c`
      after `lamo build`.
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

- [x] Define a `Type` representation in the compiler.
- [x] Decide whether Lamo will use explicit typing, inference, or a mixed model.
      **Decision: hybrid inference** — see `docs/TYPE-SYSTEM.md` for the full
      rationale. `let` infers; `fn` annotations are optional but checked when
      present; struct fields require annotations. Public-API `fn` (when `pub`
      ships) MUST be fully annotated.
- [x] Define the initial built-in types: `int` (int64), `float`, `bool`, `string`, `void`.
- [x] Attach inferred or declared types to AST nodes after analysis.

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

- [x] Decide whether Lamo has a runtime support library.
- [x] Create a minimal runtime layer for helper functions if needed.
- [x] Define runtime conventions for strings.
- [x] Define runtime conventions for booleans.
- [x] Define runtime error behavior.

### Strings

- [x] Decide whether strings are immutable. **Yes — strings are immutable.**
- [x] Decide how strings are stored and passed. **Arena-allocated `char*`,
      passed by pointer; copied on assignment to a struct field or when
      stored in an array.**
- [x] Support printing string variables reliably.
- [x] Support string input if the language will allow it.
- [x] Decide whether string comparison is by value.

### Memory Model

- [x] Decide whether Lamo will expose manual memory control, ownership rules, or a managed model.
      **Decision: hybrid — arena by default, opt-in mark-sweep GC.**
      See `docs/MEMORY-MODEL.md` for the full design and rollout plan.
- [x] Document who owns allocated runtime values.
- [x] Make generated code follow the chosen ownership rules (string arena tracked and freed via `atexit`).
- [x] **GC rollout Step 2**: add `LamoGcHeader` + `lamo_gc_alloc` + `lamo_gc_collect` skeleton to `lamo_runtime.h`.
      Done (2.3.0): every arena allocation now carries a `LamoGcHeader`
      (size, mark bit, is_array bit, in_use bit, next pointer). The arena
      (`lamo_string_arena`) tracks payload pointers; the GC heap list
      (`lamo_gc_heap_head`) tracks headers. `lamo_gc_collect()` implements
      full mark-sweep: clear marks, walk the root stack marking reachable
      allocations, sweep freeing unmarked. New builtins: `gc_collect()`,
      `gc_set_threshold(N)`, `gc_heap_size()`, `gc_heap_count()`.
- [x] **GC rollout Step 3**: codegen emits `LAMO_GC_PUSH_ROOT` /
      `LAMO_GC_POP_ROOTS_N` for every `LamoValue` local.
      Done (2.3.0): `codegen.c` now maintains a compile-time scope stack
      (`lamo_gc_scope_stack`) tracking roots pushed per scope. At function
      entry, params (and `self` for methods) are pushed as roots. Each
      `let`-declared local is pushed after its declaration. Inner blocks
      (`{}`, `if`, `while`, `for` bodies) get their own sub-scope so
      their locals are popped at block exit. At every `return` (implicit
      or user-written), all active roots are popped. The return statement
      wraps pop+return in a block `{ ... }` so it acts as a single
      statement when used as an `if`/`match` arm body.
- [x] **GC rollout Step 4**: wire periodic `gc_collect()` into `http_serve`
      and the GUI event loop.
      Done (2.3.0): `lamo_http_run_server` calls `gc_collect()` every 100
      requests. `lamo_gui_should_close` (both Win32 and X11 backends)
      calls `gc_collect()` every 1000 frames. The route table in the HTTP
      runtime is allocated outside the GC heap (plain `malloc`), so routes
      stay alive across collections.
- [x] **GC rollout Step 5**: re-promote `examples/http_server.lamo` from
      "preview" to "official" once GC is wired and tested.
      Done (2.3.0): the example's preview warning header was replaced with
      an "official" header documenting the GC hook, concurrency story
      (single-threaded), and error paths (port in use, malformed request,
      client disconnect). `docs/SPEC.md` §11.2 updated to match.
- [x] **GC rollout Step 6**: add `tests/runtime/gc_basic.lamo` and
      `tests/runtime/gc_cycle.lamo`.
      Done (2.3.0): `gc_basic.lamo` verifies that allocating garbage in a
      loop + calling `gc_collect()` reclaims something, and that
      still-reachable strings survive collection. `gc_cycle.lamo` verifies
      that two arrays referencing each other (a cycle) are reclaimed after
      all external references drop — the classic mark-sweep vs refcounting
      differentiator. Both tests pass.

## Phase 6: Language Specification

- [x] Write a small language spec for syntax, scope, evaluation, and imports.
      **Authoritative spec: `docs/SPEC.md`.**
- [x] Document variable declaration semantics. (SPEC.md §3.2, §7.1)
- [x] Document function semantics. (SPEC.md §3.3)
- [x] Document condition and loop semantics. (SPEC.md §4)
- [x] Document operator precedence and associativity. (SPEC.md §6.2)
- [x] Document builtin behavior. (SPEC.md §8, §11)
- [x] Document type rules and conversions. (SPEC.md §7)
- [x] Document runtime error cases. (SPEC.md §12)
- [x] Document import resolution and duplicate import behavior. (SPEC.md §10)

## Phase 7: Language Features

### High-Priority Features

- [x] Decide whether typed variable declarations should be added. (Yes — `let x: int = 5`; SPEC.md §3.2, §7.1.)
- [x] Decide whether typed function signatures should be added. (Yes — `fn f(a: int) -> int`; SPEC.md §3.3, §7.1.)
- [x] Add `break`. (Shipped Phase 2; SPEC.md §4.5.)
- [x] Add `continue`. (Shipped Phase 2; SPEC.md §4.5.)

### Data Structures

- [x] Design array syntax and semantics. (Shipped Phase 2; documented in SPEC.md §9.)
- [x] Add arrays only after type and runtime rules are ready. (Done — see SPEC.md §9.)
- [x] Decide whether to support structs/records. (Yes — shipped Phase 2; SPEC.md §3.4.)
- [x] Decide whether maps/dictionaries belong in the core language or standard library.
      **Decision: stdlib.** `std.collections` ships `HashMap`/`HashSet` today.
      Typed generics (`Map<K,V>`, `Set<T>`) will follow the generics RFC
      (`docs/RFC-generics.md`).

### Generics

- [x] Design generics for Lamo. **RFC draft: `docs/RFC-generics.md`.**
      Parametric generics with monomorphization, invariant by default, with
      a small fixed constraint catalogue (`Ord`, `Eq`, `Hash`, `Show`, `Num`).
      Depends on the type-system decision (already shipped — see
      `docs/TYPE-SYSTEM.md`). Suggested rollout in 6 PRs (RFC §10).
- [x] **Generics PR 1**: generic struct declarations + type parameters in field types.
      **Done (2.4.0):** `struct Pair<A, B> { first: A, second: B }` is now
      parseable and checkable. `Pair<int, string> { first: 1, second: "x" }`
      is validated at the semantic pass — type arg count must match type
      param count, type args must be known types (builtins or declared
      structs), and field types must be builtins, declared structs, or
      one of the declared type params. The runtime representation is
      unchanged (all fields are `LamoValue`), so monomorphization is
      purely a compile-time concept for PR 1 — different instantiations
      share the same C layout. New tests: `tests/runtime/generics_structs.lamo`,
      `tests/valid/generics_structs.lamo`, and 5 `tests/invalid/generics_*.lamo`
      cases covering unknown type params, wrong type arg count, type args
      on non-generic structs, duplicate type params, and unknown type args.
      All 93 tests pass with zero warnings under `-Wall -Wextra`.
- [ ] **Generics PR 2**: generic functions + type inference at call sites.
- [ ] **Generics PR 3**: `Array<T>` typed-array syntax + deprecation warning for bare `array`.
- [ ] **Generics PR 4**: typed `Map<K,V>` and `Set<T>` in stdlib.
- [ ] **Generics PR 5** (depends on tagged-union enums): `Option<T>`, `Result<T, E>`.
- [ ] **Generics PR 6**: constraint syntax (`: Ord`, `: Eq`, ...).

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
- [x] Add `lamo eval`.
- [x] Add `lamo repl` — interactive read-eval-print loop using the eval module.
- [x] Add `lamo new <project-name>` — scaffold a project with main.lamo, .gitignore, lamo.pkg.
- [x] Add `lamo clean` — remove generated lamo_exec* artifacts.
- [x] Add `lamo version`.
- [x] Add `lamo help` with per-command help (`lamo help run`, `lamo help new`, etc.).
- [x] Add `--verbose` / `--quiet` global flags and `LAMO_VERBOSE` / `LAMO_QUIET` env vars.
- [x] Add `LAMO_CC` env var to override the C compiler used by `run`/`build`.

### Diagnostics And UX

- [ ] Show file path, line, and column in all compiler errors.
- [x] Improve backend failure messages when generated C fails to compile (now reports the C compiler name, exit code, and points to lamo_exec.c).
- [x] Make successful compiler output less noisy unless verbose mode is enabled (`--quiet` / `LAMO_QUIET=1`).
- [x] Add `--verbose` mode showing the underlying C compiler invocation.
- [ ] Consider showing a source snippet with a caret marker.

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

## Recently Added (CLI Tooling Pass)

- [x] `lamo new <name>` scaffolds a project (main.lamo, .gitignore, lamo.pkg).
- [x] `lamo clean` removes lamo_exec* artifacts.
- [x] `lamo repl` runs an interactive read-eval-print loop using the eval module.
- [x] `lamo help <command>` shows per-command help.
- [x] `--verbose` / `--quiet` global flags; `LAMO_VERBOSE` / `LAMO_QUIET` env vars.
- [x] `LAMO_CC` env var overrides the C compiler used for `run`/`build`.
- [x] Backend failure messages now name the C compiler and exit code.
- [x] **Merge `lampm` into the `lamo` binary.** The package manager is now
      reachable as `lamo install`, `lamo update`, `lamo list`, `lamo info`,
      `lamo outdated`, `lamo doctor`, `lamo cache`, `lamo lock`, `lamo why`,
      `lamo remove`, `lamo init`. There is no separate `lampm` binary to
      install. The implementation lives in `src/lampm/lampm.c` and the
      public API is `lampm_main()` declared in `src/lampm/lampm.h`.
- [x] LamoPacketManager (`lampm`) v0.2: version pinning (`@ref`), lockfile (`lamo.lock`),
      new commands (`update`, `outdated`, `info`, `doctor`, `cache`, `lock`, `why`),
      non-GitHub sources (gitlab/bitbucket/generic git), `.gitignore` scaffolding,
      per-subcommand help, `--verbose`/`--quiet`/`--no-color` global flags,
      fix for the `2>nul` Windows-only null redirection bug, and fix for the
      `_POSIX_C_SOURCE` feature-macro bug that broke `-std=c99` builds.

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

## Recently Added (Engineering Sprint — spec, types, memory, refactor, generics RFC)

This sprint resolved the five "what should we lock down before adding more
syntax" items that were blocking further language growth.

- [x] **Type-system decision (Phase 4 / Phase 6)** — formalized the existing
      hybrid inference model as official. `let` infers, `fn` annotations are
      optional but checked, struct fields require annotations. Full rationale
      and rejected alternatives documented in `docs/TYPE-SYSTEM.md`. No
      compiler changes (the implementation already matches the decision);
      formalizes "what the parser does today" as "what the language means".
- [x] **Language specification (Phase 6)** — wrote `docs/SPEC.md`, the
      authoritative spec covering lexical structure, grammar, declarations,
      statements, expressions, operators (with full precedence table),
      builtins, type rules, scopes, modules/imports, platform-specific
      builtins (GUI/HTTP), and runtime behavior. Future compilers must
      conform to this spec, not the other way around.
- [x] **Memory model resolution (Phase 5)** — wrote `docs/MEMORY-MODEL.md`
      with the full design of an opt-in mark-sweep GC, a 7-step rollout
      plan, and a concrete policy ("an example is 'official' iff memory is
      reclaimed during the run, OR a hard upper bound is documented")
      gating HTTP-server example promotion. Demoted
      `examples/http_server.lamo` from "official" to "preview" with a
      prominent warning header. The actual GC implementation is tracked as
      concrete TODO items in Phase 5 above (Steps 2–6).
- [x] **Refactor: split `lamo_v2.c` and `lampm.c` into smaller modules.**
      `lamo_v2.c` went from 2563 → 379 lines (-85%); the import resolver,
      path utilities, subcommand handlers, compile pipeline, and CLI help
      now live in dedicated files under `src/cli/`. `lampm.c` went from
      2473 → 1337 lines (-46%); the manifest, lockfile, git, and utility
      code now live in dedicated files under `src/lampm/`. Public API
      (`lampm.h`) is unchanged. All 84 tests still pass with zero warnings
      under `-Wall -Wextra`.
- [x] **Generics design (Phase 7)** — wrote `docs/RFC-generics.md`, a draft
      RFC for parametric generics with monomorphization, invariant by
      default, with a small fixed constraint catalogue. Ships a 6-PR rollout
      plan; PR 1 (generic structs) is the next concrete step. Depends on the
      type-system decision (already shipped). Tagged-union enums are a
      separate prerequisite for `Option<T>` / `Result<T, E>` (PR 5).

## Recently Added (2.3.0 — GC ship, eval/run spec, lampm scope cut, quick wins)

This sprint resolved the five items the user flagged as blocking further
language growth. The GC is the headline; the rest are cleanup that the
GC unlock made possible.

- [x] **GC shipped (Steps 2–6 of `docs/MEMORY-MODEL.md`).** Opt-in
      mark-sweep garbage collector now lives in `src/codegen/lamo_runtime.h`.
      Every arena allocation carries a `LamoGcHeader` (size, mark, is_array,
      in_use, next). The codegen emits `LAMO_GC_PUSH_ROOT(&v)` for every
      `LamoValue` parameter and local, with a compile-time scope stack
      tracking pushes per scope and emitting matching `LAMO_GC_POP_ROOTS_N(n)`
      at block exit and function return. The HTTP server loop and the GUI
      event loop call `gc_collect()` automatically (every 100 requests /
      1000 frames). New builtins: `gc_collect()`, `gc_set_threshold(N)`,
      `gc_heap_size()`, `gc_heap_count()`. Programs that never call `gc_*`
      see zero GC overhead (the per-allocation header is the only cost).
      `examples/http_server.lamo` re-promoted from "preview" to "official".
      Tests: `tests/runtime/gc_basic.lamo`, `tests/runtime/gc_cycle.lamo`
      (the latter verifies cycles are reclaimed — the classic
      mark-sweep vs refcounting differentiator).
- [x] **eval/repl vs run divergence formalized (SPEC.md §10.7).** The
      two execution paths are now formally documented as distinct by
      design: `run`/`build`/`check` use the full pipeline with
      `LamoModuleRegistry` support; `eval`/`repl` use the tree-walking
      interpreter for fast feedback and do NOT load modules. Calling
      `math.sqrt(x)` in eval/repl produces a clear error pointing at
      `lamo run`. Bringing the interpreter to full parity would
      sacrifice the fast-feedback property that justifies having a
      separate interpreter; we made the limitation explicit and
      documented instead.
- [x] **lampm scope reduced.** Cut `why` (pure alias of `info` —
      maintenance cost with no user benefit) and `outdated` (fragile
      network-dependent check easy to do manually via `lamo info` +
      `git log`). `lampm_is_subcommand` returns 0 for both;
      `lampm_main` no longer dispatches to either. Help text updated.
      The command surface is now: init, install, update, remove, list,
      info, lock, cache, doctor (9 commands, down from 11).
- [x] **Quick win: GCC failure reporting.** `compile.c` now emits a
      clear `lamo: backend compilation failed (gcc exit code N)` header,
      points at the generated C source, suggests `LAMO_CC` override,
      and recommends `--verbose` for the full cc invocation. GCC's own
      stderr still flows through directly.
- [x] **Quick win: .c emission policy decided.** Always emit
      `lamo_exec.c` for both `run` and `build` modes. Rationale:
      debuggability (the .c is the IR), transparency (Lamo is "as fast
      as C because it IS C"), consistency. Documented in `compile.c`
      and `SPEC.md`. Users who want clean workflows can `lamo check`
      (no .c) or add to `.gitignore` (lamo init does this).
- [x] **RFC-generics PR 1 NOT started.** Per the user's note: generics
      add monomorphization, which multiplies the surface of generated
      code. Starting PR 1 before the GC was at least at Step 3 would
      have meant changing the memory model and the codegen shape at
      the same time — a recipe for hard-to-trace bugs. Now that the GC
      is shipped (Step 6), PR 1 (generic struct declarations + type
      parameters in field types) is unblocked and is the suggested next
      sprint.
