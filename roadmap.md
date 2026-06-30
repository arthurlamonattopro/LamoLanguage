# Lamo Roadmap

## Goal

Turn Lamo from an educational transpiler prototype into a small but real programming language with:

- a defined type system
- semantic validation
- predictable runtime behavior
- usable tooling
- a stable compilation pipeline

## Current State

Today, Lamo already has:

- a lexer
- a parser
- an AST
- a first semantic analysis pass with scope tracking
- a C backend
- `run`, `build`, `check`, `help`, and `version` commands
- fixture-based compiler tests
- basic multi-file support through relative `import "file.lamo";`

What still needs the most work:

- a real value/type model
- stronger diagnostics, especially per-file multi-file errors
- runtime semantics that are not just thin C assumptions
- clearer module/import rules
- more complete cross-platform polish

## Guiding Principles

- Keep the language small and coherent.
- Prefer correctness before adding a lot of syntax.
- Define behavior explicitly before optimizing it.
- Build features in vertical slices: syntax -> AST -> semantics -> backend -> tests -> docs.
- Replace temporary shortcuts once they start shaping language behavior.

## Phase 1: Stabilize The Core

Objective: make the current prototype predictable on real edits and real machines.

### Deliverables

- Parser behavior that rejects invalid syntax consistently.
- Cleaner CLI and backend failure paths.
- Better Windows and Unix-like build consistency.
- Documentation that stays aligned with the actual compiler.

### Tasks

- Remove the remaining parser fallback and ambiguous branches.
- Improve syntax errors with clearer expected/found output.
- Fix `make clean` and other portability rough edges on Windows.
- Decide whether generated C should always be emitted.
- Keep README, examples, TODO, and roadmap aligned with shipped behavior.

### Exit Criteria

- Invalid programs fail early and consistently.
- The normal edit/build/test loop behaves the same way across supported environments.
- There are no stale docs describing already-shipped or removed behavior.

## Phase 2: Strengthen Semantic Analysis

Objective: make Lamo responsible for language correctness instead of delegating too much to generated C.

### Deliverables

- Stable symbol resolution for globals, blocks, functions, and imported files.
- More complete semantic diagnostics.
- Regression coverage for the supported error categories.

### Tasks

- Detect duplicate function declarations explicitly.
- Validate assignment targets more directly in the frontend/semantic pipeline.
- Preserve per-file ownership in merged multi-file programs so diagnostics point to the real file.
- Define duplicate symbol behavior across imports.
- Add tests for each semantic error class, including imported programs.

### Exit Criteria

- Common user mistakes are rejected before C compilation.
- Multi-file programs fail with precise file-aware diagnostics.
- Name resolution rules are documented and tested.

## Phase 3: Define A Real Type System

Objective: give the language a coherent model for values instead of relying on C defaults.

### Deliverables

- A `Type` representation in the compiler.
- Type annotations or equivalent semantic typing data.
- A documented minimum type system.

### Minimum Types

- `int`
- `bool`
- `string`
- `void`

### Tasks

- Decide whether Lamo uses explicit types, inference, or both.
- Validate arithmetic, comparison, logical, and unary operators by type.
- Validate function parameter and return types.
- Define equality semantics.
- Define condition semantics for booleans and truthiness.
- Remove the current backend assumption that everything is `int`.

### Exit Criteria

- Type errors are reported by Lamo, not leaked through generated C.
- Builtins and expressions behave according to language rules, not C accidents.

## Phase 4: Build A Minimal Runtime

Objective: support non-trivial programs without abusing raw C assumptions.

### Deliverables

- Small runtime support for strings and helpers.
- Stable builtin behavior.
- Clear ownership and runtime error rules.

### Tasks

- Define how strings are represented and passed.
- Support printing string variables reliably.
- Decide whether string equality is by value.
- Implement runtime helpers where inline C becomes too brittle.
- Define behavior for invalid input and unsafe operations.

### Exit Criteria

- Strings are real language values, not just literals that happen to compile.
- Builtins behave consistently across platforms.

## Phase 5: Improve The Language Surface

Objective: make Lamo expressive enough for small real programs without outrunning its semantic model.

### Candidate Features

- typed variable declarations
- typed function signatures
- arrays
- structs or records
- `break` and `continue`
- standard library modules

### Tasks

- Choose a small feature set that fits the language identity.
- Move `import` from CLI preprocessing into the real language grammar and AST.
- Add each new construct only after semantics and backend support are designed.
- Avoid growing syntax faster than diagnostics and tests.

### Exit Criteria

- New language features feel consistent and are validated by the compiler.
- The module system is part of the language, not just a loader convenience.

## Phase 6: Tooling And Developer Experience

Objective: make Lamo pleasant to build, test, and extend.

### Deliverables

- stable fixture-based test harness
- better diagnostics
- clearer build flow
- style and contribution guidance
- REPL, project scaffolding, and clean commands

### Tasks

- [x] Add `lamo repl` (interactive read-eval-print loop using the eval module).
- [x] Add `lamo new <project-name>` (scaffolds main.lamo, .gitignore, lamo.pkg).
- [x] Add `lamo clean` (removes lamo_exec* artifacts).
- [x] Add per-command help (`lamo help run`, `lamo help new`, etc.).
- [x] Add `--verbose` / `--quiet` global flags and `LAMO_VERBOSE` / `LAMO_QUIET` env vars.
- [x] Add `LAMO_CC` env var to override the C compiler used by `run`/`build`.
- [x] Improve backend failure messages (now name the C compiler and exit code).
- [x] Add golden tests for emitted C where useful.
- [x] Improve diagnostic formatting with file, line, column, and source snippets.
- [x] Add a basic formatter (`lamo fmt`) that normalizes line endings, tabs, trailing whitespace, and trailing newlines. (A full AST-based pretty-printer is still future work.)
- [ ] Keep examples representative of supported features.
- [x] Reduce noisy success output unless verbose mode is requested.
- [x] Add `lamo test` to run the test suite without invoking `make test` or the shell script directly.
- [x] Add error hints (`hint: did you forget ...?` lines under error snippets) for common mistakes like missing initializers, undeclared variables, undeclared functions, and missing `let` on assignment.
- [x] Add ANSI color in diagnostics (auto-detected on TTY; disable with `--no-color` or `LAMO_NO_COLOR=1`).

### Exit Criteria

- Contributors can change the compiler with confidence.
- Users can understand compiler failures without reading generated C.

## Phase 7: Modules And Packaging

Objective: move from basic imported scripts to a deliberate multi-file language model.

### Deliverables

- import/module system defined in the frontend
- symbol visibility rules
- predictable project layout rules
- a working package manager with version pinning and a lockfile, integrated
  into the main `lamo` binary

### Tasks

- [x] **Merge `lampm` into `lamo`.** The package manager is now a set of
      subcommands of the main `lamo` binary (`lamo install`, `lamo update`,
      `lamo list`, etc.) rather than a separate executable. Implementation
      lives in `src/lampm/lampm.c` and is dispatched from `lamo_v2.c::main()`
      through `lampm_main()` (declared in `src/lampm/lampm.h`).
- [x] LamoPacketManager v0.2: version pinning (`@ref`), lockfile (`lamo.lock`),
      new commands (`update`, `outdated`, `info`, `doctor`, `cache`, `lock`, `why`),
      non-GitHub sources (gitlab/bitbucket/generic git), `.gitignore` scaffolding,
      per-subcommand help, `--verbose`/`--quiet`/`--no-color` global flags.
- [x] Fix `lampm` bugs: `_POSIX_C_SOURCE` feature-macro missing under `-std=c99`
      (broke `popen`/`pclose`/`S_IFDIR`); `2>nul` Windows-only null redirection
      used on POSIX (created a literal file named `nul`).
- [x] **Namespaced imports** (Sprint 4): `import "..." as alias;` exposes the
      imported file's top-level declarations under `alias`. Member access uses
      the `alias.fn(args)` syntax (new `AST_MEMBER_CALL` node). The loader
      renames declarations to `lamo_mod_<alias>__<name>` and registers them
      in a module registry (`src/modules.c`); the semantic pass and codegen
      consult the registry to resolve and emit member calls. Arity is
      validated the same way as regular function calls.
- [ ] Define whether imported symbols are all public by default or need explicit export rules.
- [ ] Decide how package or folder-based modules should work.
- [x] Detect and report import cycles explicitly.
- [ ] Define duplicate import behavior.
- [ ] Clarify entry file vs library file expectations.
- [ ] Extend `lamo eval` and `lamo repl` to load modules through the registry
      (today they emit a clear "use `lamo run` instead" error for member calls).

### Exit Criteria

- Real projects can be split into files without relying on accidental merge behavior.
- Module behavior is documented and deterministic.

## Phase 8: Backend Evolution

Objective: reduce dependence on C as a crutch and improve backend quality.

### Options

- Keep C as the primary backend, but make it more type-aware.
- Add an interpreter for fast feedback.
- Eventually add a bytecode VM or LLVM backend.

### Recommended Order

1. Keep improving the C backend first.
2. Make semantics and runtime explicit enough that backend behavior is interchangeable.
3. Add an interpreter for faster iteration.
4. Only consider a lower-level backend after semantics and runtime are mature.

### Exit Criteria

- Backend choice is an implementation detail, not the source of language behavior.

## Cross-Cutting Work

These should happen during every phase:

- Keep tests growing with each feature.
- Keep docs aligned with the actual compiler.
- Remove implementation shortcuts once they become design liabilities.
- Track compatibility on Windows and Unix-like systems.
- Avoid adding syntax without semantics.

## Suggested Near-Term Priority

If the goal is the fastest meaningful progress, do this next:

1. Tighten parser reliability and frontend diagnostics.
2. Finish the missing semantic validation gaps and per-file diagnostics.
3. Introduce real types.
4. Make builtins and codegen respect those types.
5. Turn imports/modules into a first-class frontend feature.
6. Expand runtime and library surface after that.

## Definition Of "Real Language"

Lamo becomes a real language when:

- the language behavior is defined by Lamo, not by accidental C behavior
- invalid programs fail early and clearly
- values and types have consistent rules
- multi-file programs have explicit module semantics
- the compiler is tested enough that changes are safe
- documentation describes reality

## Nice-To-Have Later

- REPL
- debugger integration
- package manager
- editor support
- language server
- optimization passes
- garbage collection or ownership system

## Final Note

The most important milestone is still not "more syntax". It is the moment when Lamo has a semantic model strong enough that changing the backend does not change what the language means.
