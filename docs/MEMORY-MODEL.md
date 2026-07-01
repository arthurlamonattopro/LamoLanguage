# Lamo Memory Model

**Status:** Living document. Captures (1) the current memory model as of
compiler `2.2.0`, (2) the design of an opt-in mark-sweep GC, and (3) the
concrete policy that gates HTTP-server example promotion.

This document is the **single source of truth** for memory behavior. The
spec (`docs/SPEC.md` §9, §11, §12) defers to this document on memory
questions.

---

## 1. Current Model (compiler 2.2.0)

Lamo today has **no garbage collector** and **no per-value ownership
tracking**. Allocations are tracked in a single global arena and freed in
bulk at program exit.

### 1.1 The arena

```c
static void** lamo_string_arena = NULL;
static size_t lamo_string_arena_count = 0;
static size_t lamo_string_arena_capacity = 0;
```

Defined in `src/codegen/lamo_runtime.h`. Holds `void*` pointers to:

- `char*` buffers — strings from concatenation, `input_str`, struct field
  writes that produce a new string, etc.
- `LamoArray*` structs — every array literal and struct literal allocation.
- `LamoValue*` buffers — the `items` array inside each `LamoArray`.

The arena grows monotonically (no reuse of slots). `lamo_arena_alloc(size)`
appends a fresh `malloc`'d block to the arena. `lamo_arena_free_all()` walks
the arena and frees every block — called once, via `atexit`.

### 1.2 Lifetimes

Every heap allocation made by Lamo user code lives until program exit. There
is no `free(x)` exposed to user code, no `drop`, no scope-based destruction.

Concretely:

```lamo
let s = ""
let i = 0
while (i < 1_000_000) {
    s = s + "x"     // each iteration leaks the previous `s` buffer
    i += 1
}
// On program exit, all 1_000_000 buffers are freed at once.
```

For a script that runs for 100ms and exits, this is fine. For a process
that runs for an hour, this is a leak.

### 1.3 Why this was chosen

When Lamo was an educational transpiler prototype, the arena was the right
call:

- **Simple.** One global, one free list, one `atexit`. No GC algorithm, no
  root scanning, no write barriers.
- **Correct enough.** Programs that run-to-completion don't observe the leak;
  they exit and the OS reclaims everything.
- **Avoids the worst C bug.** Before the arena, `s = s + "x"` would leak
  silently. The arena made this "leak until exit" instead of "leak forever",
  which is a strict improvement.

### 1.4 Where it breaks

The model breaks for **long-running processes**: HTTP servers, GUI event
loops with string traffic, daemons, REPL sessions that build up state. The
README and CLAUDE.md both already warn about this:

> Known limitation: long-running programs (e.g. an HTTP server) still see the
> arena grow while they run, because the runtime has no way to know which
> strings are still reachable.

The HTTP example (`examples/http_server.lamo`) ships today with this warning.
Promoting it to an "official example" without addressing the leak would
mislead users into running real services with it.

---

## 2. Decision: opt-in mark-sweep GC

**Lamo will ship a simple, opt-in mark-sweep garbage collector.** The arena
stays as the default allocator (for backwards compatibility and for
short-lived programs where GC overhead is pure cost). The GC is enabled
explicitly by the program — typically at the top of a long-running entry
point — and runs on demand or on a periodic schedule.

### 2.1 Why mark-sweep (and not reference counting / ownership)

Three alternatives were on the table:

| Option                 | Pros                                              | Cons (for Lamo)                                                                 |
|------------------------|---------------------------------------------------|---------------------------------------------------------------------------------|
| **Refcounting**        | deterministic, no pauses                          | Cycles leak; need cycle collector anyway; refcount bumps on every assign = perf hit in generated code |
| **Ownership / `drop`** | zero runtime cost, like Rust                      | Massive language change: borrow checker, lifetimes, `&` vs `&mut`. Wrong fit for a script-y language. |
| **Mark-sweep**         | handles cycles; simple to add to existing arena; no per-assign cost | stop-the-world pauses; needs root enumeration                                   |
| **Tracing generational** | best perf for long-running                      | Way too much code for the current team size; premature                          |

**Mark-sweep wins** for Lamo because:

- The arena already centralizes every allocation, so adding a header + mark
  bit per allocation is cheap.
- Lamo values are `LamoValue` (a tagged union with explicit type), so root
  scanning is "walk every `LamoValue` on the C stack and in globals" —
  well-defined.
- Cycles exist (struct fields can point to arrays that point back; arrays can
  hold struct values that reference the array). Refcounting would need a
  separate cycle collector, at which point you've built mark-sweep anyway.
- Ownership would change the language identity. That's a bigger decision
  than memory management alone.

### 2.2 The design (high level)

Every allocation made by `lamo_arena_alloc` gets a small header:

```c
typedef struct LamoGcHeader {
    size_t size;                 // bytes, excluding header
    unsigned char mark : 1;      // set during mark, cleared during sweep
    unsigned char is_array : 1;  // 1 = LamoArray, 0 = opaque (string, items buffer)
    unsigned char in_use : 1;    // 0 = slot is free (after sweep), 1 = allocated
    struct LamoGcHeader* next;   // intrusive linked list of all allocations
} LamoGcHeader;
```

Allocations are tracked in an intrusive doubly-linked list (the GC's "heap").
On `gc_collect()`:

1. **Mark phase** — walk the roots (see §2.3), recursively marking every
   reachable `LamoValue`'s backing allocation. Marking is depth-first; deep
   graphs use an explicit stack to avoid C stack overflow.
2. **Sweep phase** — walk the heap list. Any allocation with `mark == 0`
   is freed; its slot is removed from the list. Allocations with `mark == 1`
   have their mark cleared for the next cycle.

### 2.3 Root enumeration

Roots are:

- **C-stack `LamoValue`s** — every local variable in generated C functions
  that holds a `LamoValue` is a root. The codegen emits a per-function root
  registry: at function entry, push each `LamoValue` local's address onto a
  thread-local root stack; at exit, pop them.
- **Global `LamoValue`s** — top-level `let`s become C globals. They're
  registered once at startup.
- **The `self` receiver** in method calls — pushed/popped like a local.
- **Function arguments** holding `LamoValue`s.

This is the classic "exact GC" approach (as opposed to conservative scanning
of the whole C stack, which is what Boehm GC does). Exact enumeration is
possible because the codegen knows the type of every local.

The per-function root push/pop is emitted by codegen. It looks like:

```c
LamoValue lamo_user_foo(LamoValue a, LamoValue b) {
    LAMO_GC_PUSH_ROOT(&a);
    LAMO_GC_PUSH_ROOT(&b);
    LamoValue local1 = ...;
    LAMO_GC_PUSH_ROOT(&local1);
    // ... body ...
    LAMO_GC_POP_ROOTS_N(3);
    return local1;
}
```

### 2.4 What about strings inside `LamoValue`?

Strings are immutable in Lamo. A `LamoValue` with `type == LAMO_VALUE_STRING`
holds a `const char*` pointing into an arena-allocated buffer. The buffer's
header has `is_array == 0` — it's opaque, no internal pointers to trace. The
GC marks it reachable if any live `LamoValue` references it.

If two `LamoValue`s reference the same string buffer (sharing is common),
both mark the same header — fine, marking is idempotent.

### 2.5 What about `LamoArray`?

A `LamoArray*` points to a `LamoArray` struct, which has an `items` buffer
holding `LamoValue`s. The GC traces:

```
LamoValue(array_value=p)
    -> header of p (mark)
        -> LamoArray { items, count, capacity }
            -> each items[i] is a LamoValue -> recurse
```

So the GC marks both the `LamoArray` struct allocation AND the `items`
buffer allocation (because the struct references it — we mark anything
reachable). Items that are themselves arrays or structs recurse.

### 2.6 Trigger policy

GC runs are explicit:

- **Manual**: `gc_collect()` — exposed as a builtin in user code. Programs
  that want full control call this at natural boundaries (end of HTTP
  request, after a batch of work, etc.).
- **Periodic**: `gc_set_threshold(N)` — the runtime counts bytes allocated
  since the last collection. When the count exceeds `N`, the next allocation
  triggers a `gc_collect()`. Default `N = 0` (never auto-trigger).
- **On `http_serve`**: the HTTP server loop calls `gc_collect()` after every
  N requests (configurable; default N=100). This is the fix that lets
  `examples/http_server.lamo` ship as an official example.

### 2.7 Backwards compatibility

- Programs that don't call any `gc_*` builtin get the **exact same behavior
  as 2.2.0**: arena allocations, freed at exit. The GC machinery exists but
  is never invoked.
- Programs that opt in via `gc_set_threshold(N)` with `N > 0`, or that call
  `gc_collect()` explicitly, get collection.
- The arena's `lamo_arena_free_all` (called at exit) is unchanged: it frees
  everything that's still allocated (i.e., what the GC didn't reclaim). This
  means even with GC enabled, exit-time cleanup is one bulk free, same as
  before.

### 2.8 Limitations of this GC (must be documented honestly)

1. **Stop-the-world.** No incremental collection yet. A `gc_collect()` call
   blocks the caller until done. For typical Lamo heaps (< 100MB) this is
   sub-millisecond; for very large heaps it can be tens of ms. Adequate for
   HTTP services handling tens of req/sec, not for games or real-time audio.

2. **Single-threaded only.** The current runtime is single-threaded; the GC
   assumes it. When (if) Lamo adds threads, the GC needs a stop-the-world
   handshake with every thread.

3. **Root enumeration is exact but compiler-driven.** Bugs in codegen's root
   push/pop will cause either missed roots (false frees → use-after-free) or
   excessive roots (memory not reclaimed). The runtime includes a
   `gc_verify_roots()` debug helper that walks the C stack conservatively
   and reports any `LamoValue`-shaped bytes not in the root registry — for
   testing only.

4. **Finalizers not supported.** If a Lamo value holds a resource (file
   handle, socket, etc.), the resource is NOT closed when the value is
   collected. User code must close explicitly. This is the same as Go (pre-
   `runtime finalizers`) and most scripting languages.

5. **No compaction.** The arena layout doesn't move objects. Long-running
   programs can fragment the C heap (not the arena — the arena is just a
   pointer list). This is rarely the bottleneck; if it becomes one, a
   compacting collector is a separate, large project.

---

## 3. Implementation plan

This is the rollout sequence. Each step is independently shippable.

### Step 1 — Document (this file, SPEC.md updates, README warning)

Done in this commit. No code changes yet; the policy in §4 below takes effect
immediately, so the HTTP example is **demoted** from "official" to "preview"
until Step 3 lands.

### Step 2 — GC skeleton in `lamo_runtime.h`

Add:

```c
typedef struct LamoGcHeader { ... } LamoGcHeader;
static LamoGcHeader* lamo_gc_heap_head = NULL;
static size_t lamo_gc_bytes_since_collect = 0;
static size_t lamo_gc_threshold = 0;          // 0 = disabled

static void* lamo_gc_alloc(size_t size);       // wraps arena + header
static void  lamo_gc_push_root(LamoValue* v);  // push to root stack
static void  lamo_gc_pop_roots_n(int n);       // pop n entries
static void  lamo_gc_collect(void);            // mark + sweep
static void  lamo_gc_set_threshold(size_t bytes);
```

`lamo_arena_alloc` becomes a thin wrapper around `lamo_gc_alloc` (so existing
callers don't change). The arena `free_all` at exit walks the GC heap list
instead of the old arena array (same effect, one less data structure).

Wire `gc_collect()` and `gc_set_threshold(N)` as builtins (`src/builtins.h`,
codegen case in `src/codegen/codegen.c`).

### Step 3 — Codegen emits root push/pop

In `generate_fn_decl()` (codegen.c), at function entry emit pushes for every
parameter and local `LamoValue`; at every return path emit the matching pop.
This is the bulk of the work.

A simpler interim step: emit root pushes ONLY for `LamoValue` variables that
hold GC-eligible types (string, array, struct). Skip ints, floats, bools.
This cuts root-stack pressure.

### Step 4 — Wire GC into `http_serve`

The HTTP server loop in `lamo_runtime.h` calls `lamo_gc_collect()` every N
requests. Configurable via `gc_set_http_gc_interval(N)`; default N=100.

### Step 5 — Promote HTTP example

Move `examples/http_server.lamo` from "preview" to "official" in the README,
update the warning from "don't use in production for long-running processes"
to "uses opt-in mark-sweep GC, configurable via `gc_set_threshold()`".

### Step 6 — Tests

Add `tests/runtime/gc_basic.lamo` — allocates lots of strings in a loop,
calls `gc_collect()`, asserts that heap size dropped.
Add `tests/runtime/gc_cycle.lamo` — two structs referencing each other,
collect, assert no use-after-free.

### Step 7 — Future (out of scope here)

- Generational: track age, promote surviving objects to an "old" heap that's
  collected less often.
- Incremental: split mark phase into slices, interleaved with user code.
- Concurrent: separate GC thread (needs the thread story first).

---

## 4. Policy: when can an HTTP example ship as "official"?

An example is "official" (linked from the README as a recommended pattern)
iff **all** of the following hold:

1. **Memory-safe for the workload.** Either the runtime reclaims memory
   during the run (GC enabled and tested), OR the example documents a hard
   upper bound on memory growth and the workload is short enough to fit
   inside it.

2. **Error paths defined.** What happens on malformed request? On port
   already in use? On client disconnect mid-request? All documented in the
   example file.

3. **Concurrency story documented.** Today: single-threaded, one request at
   a time. Documented in the example. (Multi-threaded is future work.)

4. **Tested under load.** A test script in `tests/runtime/` exercises the
   example for at least 1000 requests and asserts stable memory.

As of this commit, `examples/http_server.lamo` does NOT satisfy (1) — the
arena leaks during the run. Therefore:

- **HTTP example is officially demoted from "official" to "preview"** in the
  README, with a clear pointer to this document.
- Once Step 2–Step 4 above land, the example can be re-promoted.

This is the "or at least document clearly 'don't use in production for
long-running processes'" branch of the original ask. The GC path is the
preferred long-term answer.

---

## 5. FAQs

**Q: Why not just ship the arena leak as-is and document it?**
A: That was the status quo. The user asked us to "resolve the memory model
of real before shippar HTTP server as exemplo oficial" — accepting the leak
is a valid resolution, but only if we (a) clearly demote the example to
"preview" and (b) commit to the GC timeline. This document does both. The
GC skeleton in Step 2 is small enough to ship in the same PR; doing so moves
us from "leak, period" to "leak by default, collectable on demand".

**Q: Won't the GC slow down short-lived programs?**
A: No — GC is opt-in. Programs that don't call `gc_collect()` or
`gc_set_threshold(N > 0)` see zero GC overhead. The only cost is the
per-allocation header (16 bytes per allocation), which is negligible.

**Q: What about the `lamo eval` / `lamo repl` paths?**
A: Same runtime, same GC available. REPL sessions that build up state can
call `gc_collect()` manually.

**Q: Will GC break existing tests?**
A: No. Existing tests don't call `gc_*`; they get the arena behavior, which
is unchanged. New GC-specific tests are added in Step 6.

**Q: Why not just use Boehm GC?**
A: Dependency cost. Boehm is ~50k lines of C, conservative (scans the whole
stack, slower), and adds a build dependency. Lamo's exact GC, while simpler,
is ~500 lines and lives in the existing runtime header. For a language
Lamo's size, that's the right trade.

**Q: What about GUI programs that run forever?**
A: Same story: opt-in GC. The X11/Win32 event loop in `lamo_runtime.h` will
get the same periodic `gc_collect()` hook as `http_serve`, in a future step.

---

## 6. References

- `src/codegen/lamo_runtime.h` — current arena implementation.
- `examples/http_server.lamo` — the example being gated by §4.
- `docs/SPEC.md` §9 (Arrays), §11 (HTTP builtins), §12 (Runtime behavior).
- Mark-sweep GC: McCarthy 1960 ("Recursive Functions of Symbolic Expressions").
- Boehm GC — the conservative alternative we rejected.
- Python's `gc` module — inspiration for the `gc_collect()` /
  `gc_set_threshold()` API surface.
