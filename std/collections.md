# std.collections

Higher-level collection types built on top of Lamo's native arrays:
**List**, **Stack**, **Queue**, **HashMap**, and **HashSet**.

Because Lamo's static type inference cannot follow struct types across
module boundaries, the API uses module-level functions whose first
argument is the collection (similar to Python's `heapq` module).

Internally, all collections are represented as plain arrays:

| Type     | Layout                              |
|----------|-------------------------------------|
| List     | `[v0, v1, ...]`                     |
| Stack    | `[bottom, ..., top]` (push/pop end) |
| Queue    | `[front, ..., back]`                |
| HashMap  | `[k0, v0, k1, v1, ...]`             |
| HashSet  | `[v0, v1, ...]` (no duplicates)     |

## Function Reference

### List
- `newList()` — create an empty list.
- `listPush(list, value)` — append; returns new length.
- `listPop(list)` — remove and return the last element.
- `listGet(list, i)` / `listSet(list, i, value)` — index access.
- `listInsert(list, i, value)` — insert at index.
- `listRemoveAt(list, i)` — remove and return element at index.
- `listContains(list, value)` — `1` if present.
- `listIndexOf(list, value)` — index of first match, or `-1`.
- `listLen(list)` / `listIsEmpty(list)` / `listClear(list)`.

### Stack (LIFO)
- `newStack()` — create empty stack.
- `stackPush(stack, value)` — push; returns new length.
- `stackPop(stack)` — pop the top.
- `stackPeek(stack)` — return top without removing (returns `0` if empty).
- `stackLen` / `stackIsEmpty` / `stackClear`.

### Queue (FIFO)
- `newQueue()` — create empty queue.
- `queueEnqueue(queue, value)` — append; returns new length.
- `queueDequeue(queue)` — remove and return front (returns `0` if empty).
- `queuePeek(queue)` — return front without removing.
- `queueLen` / `queueIsEmpty` / `queueClear`.

### HashMap
- `newHashMap()` — create empty map.
- `mapPut(map, key, value)` — set `key` to `value`; returns `value`.
- `mapGet(map, key)` — return value for `key` (returns `0` if missing).
- `mapGetOr(map, key, default)` — return value or `default`.
- `mapHas(map, key)` — `1` if present.
- `mapRemove(map, key)` — `1` if removed, `0` if not found.
- `mapLen(map)` / `mapKeys(map)` / `mapValues(map)` / `mapClear(map)`.

### HashSet
- `newHashSet()` — create empty set.
- `setAdd(set, value)` — add if not present; returns new length.
- `setHas(set, value)` / `setContains(set, value)` — `1` if present.
- `setRemove(set, value)` — `1` if removed.
- `setLen` / `setIsEmpty` / `setClear` / `setToArray`.

## Examples

```lamo
import std.collections as collections

let list = collections.newList()
collections.listPush(list, 10)
collections.listPush(list, 20)
collections.listGet(list, 0)   // 10

let stack = collections.newStack()
collections.stackPush(stack, "a")
collections.stackPush(stack, "b")
collections.stackPop(stack)    // "b"

let map = collections.newHashMap()
collections.mapPut(map, "name", "lamo")
collections.mapGet(map, "name")  // "lamo"

let set = collections.newHashSet()
collections.setAdd(set, "apple")
collections.setHas(set, "apple")  // 1
```

## Notes

- HashMap keys may be any value comparable with `==` (strings, ints,
  floats). The map performs a linear scan, so it is best suited to
  small key sets.
- `queueDequeue` and `queuePeek` return `0` (not an error) on an empty
  queue — callers should check `queueIsEmpty` first if `0` is a valid
  element.
