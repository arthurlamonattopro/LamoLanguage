# Lamo Standard Library: Scope and Organization

**Status:** Official (Phase 8 decisions recorded here; tracked in `todo.md`
Phase 8). Companion reading: `std/README.md` for per-module API docs,
`docs/DIRECTORY-LAYOUT.md` for where things live.

## Phase 8 decision log

### 8.1 What belongs in the core language vs the stdlib (DECIDED)

The core language ships ONLY what the compiler itself must understand to
check or run a program:

| Core | Rationale |
|------|-----------|
| scalars (`int`, `float`, `bool`, `string`, `void`) + arrays/structs/enums | value model |
| control flow (`if`, `while`, `for`, `break`, `continue`, `match`) | semantics |
| declarations, imports, impl blocks | program structure |
| builtins with dedicated runtime/op support | see table below |

Core builtins are limited to: `print`, `input_int`, `input_str`, `isnumber`,
`isstring`, `isarray`, `abs`, `exit`, array primitives (`len`, `push`, `pop`),
GC controls. Everything else — file I/O, environment, network, time,
processes, math beyond operators, string transforms, collections, testing —
is stdlib. Test for inclusion: *would its removal make the compiler unable to
type-check or lower programs?* If no, it is stdlib material.

### 8.2 I/O expansion beyond print/input (RESOLVED: satisfied via std.io)

Expanded I/O lives in `std/io.lamo` (`println`, `eprint`, `read_line`,
`write`). The core keeps exactly one output builtin (`print`) because every
diagnostic path depends on it; anything users may want to REDIRECT or
disable belongs in stdlib.

### 8.3 Numeric helpers (RESOLVED: shipped in std.math; core stays operator-only)

`sqrt/pow/sin/cos/tan/floor/ceil/round/min/max/clamp` live in `std/math`,
implemented over the float-capable runtime helpers gated by
`LAMO_NEEDS_FLOAT_OPS`. Adding more is now purely additive at the module
layer (no compiler edits) — that was the gating condition in todo.md.

### 8.4 String helpers (RESOLVED: shipped in std.string)

Strings are immutable and arena-allocated (SPEC §5 / MEMORY-MODEL);
`length/upper/lower/starts_with/ends_with/contains/index_of/trim/substring/
replace/char_at/repeat` cover common cases in `std/string`. Further growth
(no regex) stays module-side.

### 8.5 Module organization plan (DECIDED)

- One directory per concern: `std/<module>.lamo`.
- Every module MUST ship: implementation, `<module>.md` API page, an example
  under `std/examples/`, tests under `std/tests/`. A module without all four
  is "preview" and says so in its header comment.
- Naming: lowercase, single word when possible (`math`, `fs`, `json`);
  user-visible functions are camelCase verbs/subjects (`mapGetOr`,
  `startsWith`).
- Module-level wrappers should be side-effect-free combinators over erased
  arrays (see PR4/PR5 patterns in `collections.lamo`); core builtins stay
  behind them.
- Cross-module dependencies between stdlib modules are currently forbidden —
  keeps each module independently compilable and testable.
