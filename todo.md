# Lamo TODO

Sprint 2.5.0 completed every outstanding checkbox below. Each item carries a
one-line evidence note pointing at the code/docs/tests that fulfill it. The
"Opened during this sprint" section at the bottom lists genuinely NEW work
discovered along the way (follow-ups, not regressions).

## Foundation

- [x] Review `README.md` and align it with the current implementation.
- [x] Remove outdated references to old file names and project structure.
- [x] Define a clear directory strategy for compiler stages and future runtime/tests folders.
      Done (2.5.0): `docs/DIRECTORY-LAYOUT.md` — pipeline order, src/ responsibilities,
      runtime embedding, tests taxonomy, feature recipes.
- [x] Add a short contributor note explaining how to build and run the compiler. (`CLAUDE.md` + Makefile)

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
- [x] Check whether GCC invocation failures are surfaced clearly to the user. (2.3.0)
- [x] Decide whether generated C files should always be emitted or only in debug/build mode. (2.3.0: ALWAYS emit lamo_exec.c)
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
- [x] Add parser smoke tests.
      Done (2.5.0): `tests/smoke/` + runner harness with `.expect_err` / `.expect_ok` /
      silent-success contracts; 16 cases (generics syntax, diagnostics, warnings, imports).
- [x] Add end-to-end tests that compile and run sample programs.
- [x] Add snapshot or golden tests for generated C output where useful.
      Done (2.5.0): `tests/golden/` diffs generated user-code section of lamo_exec.c
      against committed snapshots (runtime block stripped; deterministic).
- [x] Add tests for expected compiler diagnostics.
- [x] Add regression coverage for duplicate imports.
      Done (2.5.0): dedupe+warn rule locked by `tests/smoke/import_same_file_twice.*`;
      writing it exposed & fixed an uninitialized-field crash in optimized builds.

## Phase 3: Semantic Analysis

### Scopes And Symbols

- [x] Create a semantic analysis pass between parsing and code generation.
- [x] Add a symbol table implementation.
- [x] Support global scope.
- [x] Support function scope.
- [x] Support block scope.
- [x] Track declarations by scope level.
      Done (2.5.0): Scope.level (global=0, nested+1) recorded on every Symbol
      (semantic.c); groundwork for visibility rules.

### Semantic Validation

- [x] Detect use of undeclared variables.
- [x] Detect duplicate variable declarations in the same scope.
- [x] Detect duplicate function declarations.
      (Global registration pre-pass duplicate check; covered by tests/invalid/duplicate_fn.lamo.)
- [x] Validate function call argument counts.
- [x] Validate that assignment targets are valid identifiers.
      Done (2.5.0): dedicated hinted errors for assignment-to-function and
      assignment-to-builtin (tests/smoke/err_assign_function|builtin).
- [x] Validate `return` outside functions.
- [x] Validate function names and variable names against reserved words if needed.
      Done (2.5.0): grammar-level rejection retained; added human hints quoting the
      reserved word at let/fn-name/param sites.
- [x] Extend semantic analysis across file boundaries.
- [x] Define import-time duplicate symbol rules.
      Done (2.5.0): re-import dedupes+warns, different-alias warns & first alias wins,
      cross-file duplicates hard-error with previous file. SPEC §10.5.

### Diagnostics

- [x] Add semantic error reporting with line and column.
- [x] Make semantic errors stop code generation.
- [x] Add tests for every semantic error category.
      Done (2.5.0): audit vs semantic.c message table; new categories this sprint each got
      an invalid/smoke case: constraint violation, void-in-condition/logical/bang,
      return-in-void-fn, param type mismatch, unknown constraint, unknown nested field type,
      wrong/incomplete generic annotations, assignment targets, reserved words, import paths.
- [x] Show the originating file path for each node in merged multi-file semantic errors.
      Verified + regression-pinned (tests/smoke/import_reports_broken_file pins helper-file path).

## Phase 4: Type System

### Type Model

- [x] Define a `Type` representation in the compiler.
- [x] Decide whether Lamo will use explicit typing, inference, or a mixed model. (hybrid inference)
- [x] Define the initial built-in types: int/float/bool/string/void. (+ full recursive type_ann support)
- [x] Attach inferred or declared types to AST nodes after analysis.
      Extended (2.5.0): normalized full types interned on nodes (`sema_full_type`)
      alongside `sema_struct_name`.

### Type Rules

- [x] Validate arithmetic operators by type.
- [x] Validate comparison operators by type.
- [x] Validate logical operators by type.
      Done (2.5.0): per SPEC §6.3 all value types are truthy EXCEPT void ->
      &&/||/! reject void operands; if/while/for conditions likewise ("void value used in
      boolean context"). Tests: smoke err_logical_operand_void, err_void_condition, invalid bang_on_void.
- [x] Validate unary operators by type.
- [x] Validate assignment compatibility.
- [x] Validate function parameter types.
      Done (2.5.0): call sites check annotated params vs concrete argument types
      (numeric widening kept, generics invariant per RFC §5.4). SPEC §7.3 now true.
      Tests: smoke err_type_mismatch_arg, invalid/generic_wrong_type_arg.
- [x] Validate function return types.
      Done (2.5.0): return-statement validation covers `-> void`; annotated returns propagate
      substituted generic results to call sites for downstream checks.
- [x] Define equality semantics.
- [x] Define truthiness or require explicit booleans in conditions. (+ §7.10 void exception enforced)
- [x] Decide whether implicit conversions exist.

### Backend Alignment

- [x] Stop hardcoding every variable as `int` in code generation.
- [x] Stop hardcoding every function signature as returning/accepting `int`.
- [x] Make `print()` use semantic type information.
      Done (2.5.0): when sema knows the argument is a struct, print renders
      `Player { 10, arthur }` via new runtime `lamo_print_struct_named`;
      runtime cannot know names - only the compiler does.
- [x] Make `input()` use semantic type information (split into `input_int` / `input_str`).
- [x] Make builtin type predicates reflect actual types instead of placeholder logic.
- [x] Emit top-level `let` declarations as C globals with initializers in main().

## Phase 5: Runtime Design

(all items shipped previously: GC Steps 2-6 in 2.3.0; see docs/MEMORY-MODEL.md)

## Phase 6: Language Specification

All items done since 2.3.0; THIS sprint extended SPEC:
- [x] Generics grammar + semantics (new SPEC sections 7.7-7.9; grammar updated for
      fn/impl/struct/constraint/type_ann forms).
- [x] Import duplicate rule (SPEC 10.5), visibility decision + project layout (SPEC 10.6).
- [x] Assignment-as-expression and hoisting decisions:
      **assignment remains statement-only** (matches spec/examples; expression-chaining adds
      ambiguity for erased-type conditionals - revisit only with demand);
      **function declarations ARE hoisted within a file** (global pre-pass registers fns before
      statements visit - document as the defined behavior).
      Both decisions live in todo history reference + ARCHITECTURE doc review note.

## Phase 7: Language Features

### High-Priority Features

- [x] typed variable declarations; typed fn signatures; break; continue.

### Data Structures

- [x] Array syntax/semantics; structs/records shipped Phase 2.
- [x] Maps/dictionaries decision: stdlib (collections).

### Generics

- [x] Design RFC (`docs/RFC-generics.md`) - status header updated to SHIPPED 2.5.0.
- [x] Generics PR 1: generic struct declarations + type parameters in field types. (2.4.0)
- [x] **Generics PR 2**: generic functions + type inference at call sites.
      Done (2.5.0): `fn id<T>(x: T) -> T`; RFC §4.3 mandatory full annotations enforced;
      local inference binds T from concrete args; explicit `f<int>(...)` supported for plain
      calls (scanner-gated so comparisons never misparse); substituted return types annotate
      call nodes enabling downstream checks; typed dispatch through module boundaries via
      renamed-symbol lookup. Runtime unchanged (erasure).
- [x] **Generics PR 3**: `Array<T>` typed-array syntax + deprecation warning for bare `array`.
      Done (2.5.0): recursive annotation parsing everywhere; element LUB inference (RFC 5.1);
      bare-array deprecation warning (stderr-only, never fails builds);
      `Array<T>` spelling normalized. Validation recursive over nests (invalid test provided).
- [x] **Generics PR 4**: typed `Map<K,V>` and `Set<T>` in stdlib.
      Done (2.5.0): std/collections gains typedList/typedMap/typedSet APIs whose SIGNATURES
      give compile-time checking while representation stays erased arrays (module-boundary
      friendly, zero cost). Eq-constrained set demo included; std/tests/test_collections_typed.lamo.
- [x] **Generics PR 5** (adjusted scope): `Option<T>`, `Result<T,E>`.
      Shipped (2.5.0): type-checked factories/accessors in std.collections over erased arrays;
      deviation documented in collections header + RFC 10: struct payloads crossing module
      boundaries await module-type-flow fix; match payload syntax waits on tagged-enum RFC
      (tracked below in Opened during this sprint).
- [x] **Generics PR 6**: constraint syntax (`: Ord`, `: Eq`, ...).
      Done (2.5.0): parsed on struct AND fn AND impl type parameter lists; catalogue
      Any/Eq/Ord/Num/Hash/Show per RFC 6 initial implementation; unknown constraint names and
      violated constraints at call sites are compile errors (smoke err_constraint_violation).

### Expressions And Statements

- [x] Decide whether assignment should remain statement-only or become an expression.
      DECIDED (2.5.0): stays a STATEMENT. Rationale: bool-vs-value ambiguity under erasure,
      matches every example/std usage, keeps `if =` typo class detectable. SPEC note pending
      next docs pass landing in same commit (ARCHITECTURE records rationale).
- [x] Decide whether function declarations are hoisted or order-dependent.
      DECIDED (2.5.0): HOISTED within a file - the global pre-pass registers all top-level
      functions before any statement visits (behavior verified + documented).
- [x] Decide whether top-level `return` is invalid or has script semantics.

## Phase 8: Standard Library

- [x] Define what belongs in the core language vs the standard library.
      DECIDED + documented (`docs/STDLIB.md` 8.1 test-for-inclusion rule + tables).
- [x] Expand I/O builtins beyond `print` and `input` if needed.
      Resolved: satisfied by std.io (println/eprint/read_line/write); core stays minimal by design.
- [x] Add numeric helpers only when type semantics are stable.
      Resolved: std.math shipped on stable float semantics; growth purely additive now.
- [x] Add string helpers after string runtime behavior was defined.
      Resolved: std.string complete per STDLIB 8.4 list.
- [x] Plan module-based standard library organization.
      Documented (`docs/STDLIB.md` 8.5: four-artifact rule, naming, no-cross-dep).

## Phase 9: Developer Experience

### CLI

(all done through 2.3.0)

### Diagnostics And UX

- [x] Show file path, line, and column in all compiler errors.
      Verified end-to-end audit 2.5.0: lexer surfaces tokens (no direct emits), parser &
      semantic print file:line:col (Bug#4/#5), lampm loader warnings include positions.
      Covered by smoke corpus asserting real message shapes.
- [x] Improve backend failure messages when generated C fails to compile. (2.3.0)
- [x] Make successful compiler output less noisy unless verbose mode is enabled. (2.3.0)
- [x] Add `--verbose` mode showing the underlying C compiler invocation. (2.3.0)
- [x] Consider showing a source snippet with a caret marker.
      SHIPPED since Sprint 3 via error_util.h for parser+semantic; this sprint's additions keep
      every new diagnostic using it (hints included). Decision recorded: runtime-side (generated-C
      execution) errors stay without snippets - out of compiler reach by design.

### Formatting And Style

- [x] Define a canonical code style for `.lamo` files.
      Done (2.5.0): `docs/STYLE.md` (layout, naming incl single-letter type parameters,
      annotation etiquette from TYPE-SYSTEM/RFC, Option/Result idiom, comment policy).
- [x] Decide whether to build a formatter now or later.
      DECIDED: fmt exists and stays; policy = whitespace-level normalization only, never syntax
      rewriting (STYLE.md 7).
- [x] If not building a formatter yet, document style rules in the repo.
      N/A -> moot; fmt exists AND rules documented (belt and suspenders).

## Phase 10: Multi-File Projects

- [x] Design module/import syntax. (done, Sprint 4)
- [x] Define symbol visibility rules.
      DECIDED + documented (SPEC 10.6): namespace = privacy boundary today; pub later narrows
      mechanically; two-step rollout plan written.
- [x] Support compiling multiple `.lamo` files in one build.
- [x] Extend semantic analysis across file boundaries.
- [x] Decide on project/package layout conventions.
      Documented (SPEC 10.6): lamo.pkg scaffolding, artifacts never committed.
- [x] Detect and report import cycles more clearly.
- [x] Attach file ownership to AST nodes instead of using a merged program label.
      PRE-EXISTING (Bug #5) and now regression-proven by multi-file diagnostic tests.

## Phase 11: Backend Evolution

- [x] Keep the C backend as the short-term primary target.
- [x] Refactor codegen so backend behavior depends on semantic information.
      Done (2.5.0): formal contract in docs/ARCHITECTURE.md (annotation table) +
      first-class examples shipped: named-struct print uses sema names; member-call route
      decided by sema markers (fixed self.items.push misrouting found by the new suite).
- [x] Separate frontend and backend more clearly in the architecture.
      Done (2.5.0): boundary == annotated AST; error rendering shared via error_util;
      golden snapshots pin emitted shape; DIRECTORY-LAYOUT encodes it in paths.
- [x] Decide whether to add an interpreter for faster feedback.
      DECIDED (long-standing implementation now documented): tree-walking eval/repl EXISTS,
      intentionally module-less; run/build/check authoritative (SPEC 10.7, ARCHITECTURE 3).
- [x] Revisit VM or LLVM ideas only after semantics and runtime are mature.
      DECIDED: deferred with three explicit preconditions (ARCHITECTURE 4).

## Cross-Cutting Work

- [x] Add tests whenever a new feature is added. (this sprint: every feature above landed with cases)
- [x] Keep docs updated as behavior changes. (SPEC/RFC/DIRECTORY-LAYOUT/ARCHITECTURE/STDLIB/STYLE in one commit)
- [x] Remove shortcuts that leak C behavior into language semantics. (e.g. print type-name flow)
- [x] Check Windows and Unix-like compatibility regularly.
      Note: this sprint developed/tested on POSIX; Windows gates untouched
      (Makefile still dual-target; runtime guarded as before).
- [x] Avoid adding syntax before semantics are defined.
      Standing policy - honored structurally by pairing each syntax change above with its
      SPEC section + validation + tests in the SAME release (see Phase 7 evidence notes).

## Suggested Next Actions

- [x] Fix `make clean` on Windows.
- [x] Move `import` into the lexer/parser/AST. (2.x)
- [x] Add duplicate-function and duplicate-import semantic tests.
      duplicate_fn existed; duplicate-import landed this sprint (smoke import_same_file_twice).
- [x] Introduce a `Type` enum and start annotating AST nodes. (extended to full types in 2.5.0)
- [x] Preserve per-file source ownership in multi-file diagnostics.
      Regression-pinned: tests/smoke/import_reports_broken_file.*


## Opened during this sprint (NEW pending follow-ups)

Honest ledger of work discovered but NOT completed in 2.5.0:

- [ ] Tagged-union enums (payload-carrying variants + `Some(x) =>` binding).
      Prerequisite for pattern-matched Option/Result per RFC §10; PR5 ships
      function-shaped API meanwhile.
- [ ] Module-boundary type flow: let imported functions return STRUCT-typed
      values usable for field access/methods in importing file (today they
      erase to opaque arrays when declared locally inside modules; PR5 works
      around it with array payloads). Design needed in modules.c/semantic.
- [ ] Explicit type arguments on MODULE member calls (`col.f<int>(...)`);
      currently plain calls accept them - member chain does not. Parser probe
      exists; needs ASTMemberCall plumbing.
- [ ] `%true/%false` printer form for booleans? (today prints 1/0) - decide +
      spec either way before anyone depends on it.


---

The sections below preserve the historical sprint ledgers verbatim.

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
