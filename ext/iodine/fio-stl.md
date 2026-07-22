# facil.io STL — Core

Welcome to the heart of the facil.io C STL. This is where the boring-but-essential stuff lives: version numbers, compiler incantations, OS patches, atomics, memory primitives, and the low-level helpers that everything else builds on. It is not glamorous, but without it the rest of the library would be a pile of undefined behavior and segfaults.

The core group covers the first two slices of the library — `000 core.h` and `001 header.h` — plus their specialized sidekicks. Each file below is a focused guide to one corner of that foundation.

## Core documentation index

- [001 version and settings](./001%20version%20and%20settings.md) — Version macros and the compile-time knobs that tune the library's behavior.
- [001 compiler attributes](./001%20compiler%20attributes.md) — Compiler detection, C/C++ attribute helpers, and pointer-math macros.
- [001 os detection](./001%20os%20detection.md) — POSIX/Windows detection, process helpers, and portable type patches.
- [001 atomics and locks](./001%20atomics%20and%20locks.md) — Atomic operations, spinlocks, atomic bitmap access, and thread-yield hints.
- [001 static alloc](./001%20static%20alloc.md) — The round-robin scratch allocator for short-lived temporary buffers.
- [001 logging](./001%20logging.md) — Log levels, assertion macros, and the leak counter.
- [001 endian](./001%20endian.md) — Byte-order detection, swaps, and conversions between host and network/little-endian layouts.
- [001 memory primitives](./001%20memory%20primitives.md) — Low-level copy/move/compare helpers and unaligned buffer accessors.
- [001 memalt](./001%20memalt.md) — Portable fallback memory routines and the environment helpers they lean on.
- [001 vector math](./001%20vector%20math.md) — Wide integer unions from 128 to 4096 bits and their lane-wise arithmetic.
- [001 simd](./001%20simd.md) — Fake/portable x86 intrinsics and SIMD helpers built on the wide vector types.
- [001 linked lists](./001%20linked%20lists.md) — Pointer-based and indexed linked-list macros.
- [001 constant time](./001%20constant%20time.md) — Branchless selection, bitwise comparisons, and other security-sensitive primitives.
- [001 bit operations](./001%20bit%20operations.md) — Rotations, popcount, bitmap access, and byte-value bit tricks.
- [001 math](./001%20math.md) — Hash primes, modular arithmetic, and multi-precision add/sub/mul helpers.
- [001 random core](./001%20random%20core.md) — The deterministic 128-bit PRNG and its reseeding/cycle-counter hooks.
- [001 string info](./001%20string%20info.md) — Lightweight string and buffer descriptors plus equality helpers.
- [001 utf8](./001%20utf8.md) — Encoding, decoding, and validation helpers for UTF-8 text.

Everything after the core builds on these pages. Pick a topic and dig in.
# Atomics and Locks

Concurrency primitives live in [`./000 core.h`](./000%20core.h). The lock selector that chooses between spinlocks and OS mutexes is in [`./001 header.h`](./001%20header.h).

## Atomic operations

All atomic macros use sequentially consistent ordering. They work on a pointer to the object and return the **previous** value, unless the name ends in `_fetch`, in which case they return the **new** value.

### Load and exchange

| Macro | Returns | Notes |
|---|---|---|
| `fio_atomic_load(dest, p_obj)` | assigns value to `dest` | Atomic read of `*p_obj`. |
| `fio_atomic_exchange(p_obj, value)` | previous value | Atomically sets `*p_obj = value`. |
| `fio_atomic_compare_exchange_p(p_obj, p_expected, p_desired)` | `1` on success, `0` on failure | System-specific compare-and-swap. `p_expected` may be overwritten with the current value. |

### Arithmetic and logic

| Macro | Operation | Returns |
|---|---|---|
| `fio_atomic_add(p_obj, value)` | `*p_obj += value` | previous value |
| `fio_atomic_sub(p_obj, value)` | `*p_obj -= value` | previous value |
| `fio_atomic_and(p_obj, value)` | `*p_obj &= value` | previous value |
| `fio_atomic_or(p_obj, value)` | `*p_obj \|= value` | previous value |
| `fio_atomic_xor(p_obj, value)` | `*p_obj ^= value` | previous value |
| `fio_atomic_nand(p_obj, value)` | `*p_obj = ~(prev & value)` | previous value |
| `fio_atomic_add_fetch(p_obj, value)` | `*p_obj += value` | new value |
| `fio_atomic_sub_fetch(p_obj, value)` | `*p_obj -= value` | new value |
| `fio_atomic_and_fetch(p_obj, value)` | `*p_obj &= value` | new value |
| `fio_atomic_or_fetch(p_obj, value)` | `*p_obj \|= value` | new value |
| `fio_atomic_xor_fetch(p_obj, value)` | `*p_obj ^= value` | new value |
| `fio_atomic_nand_fetch(p_obj, value)` | `*p_obj = ~(prev & value)` | new value |

The implementation prefers GCC/Clang `__atomic` builtins, falls back to legacy `__sync` builtins, then C11 `stdatomic.h`, then MSVC intrinsics.

## Spinlocks

The default lock is an 8-bit spinlock with one main lock and seven sub-locks packed into the same byte.

| Type / macro | Meaning |
|---|---|
| `fio_lock_i` | `volatile unsigned char` spinlock type. |
| `FIO_LOCK_INIT` | Initializer (`0`). |
| `FIO_LOCK_SUBLOCK(i)` | Bit mask for sub-lock `i` (`0` to `7`). Combine with `\|`. |

### Single lock API

| Function | Behavior |
|---|---|
| `uint8_t fio_trylock(fio_lock_i *lock)` | Try to acquire the main lock. Returns `0` on success, `1` on failure. |
| `void fio_lock(fio_lock_i *lock)` | Busy-wait until the main lock is acquired. Avoid long waits. |
| `void fio_unlock(fio_lock_i *lock)` | Release the main lock. |
| `uint8_t fio_is_locked(fio_lock_i *lock)` | Returns non-zero if the main lock is held. |

### Group lock API

| Function | Behavior |
|---|---|
| `uint8_t fio_trylock_group(fio_lock_i *lock, uint8_t group)` | Try to acquire a group of sub-locks. Returns `0` on success, `1` on failure. |
| `void fio_lock_group(fio_lock_i *lock, uint8_t group)` | Busy-wait until the whole group is acquired. |
| `void fio_unlock_group(fio_lock_i *lock, uint8_t group)` | Release a group of sub-locks. |
| `uint8_t fio_is_group_locked(fio_lock_i *lock, uint8_t group)` | Returns non-zero if any sub-lock in `group` is held. |
| `uint8_t fio_trylock_full(fio_lock_i *lock)` | Try to lock all sub-locks at once. |
| `void fio_lock_full(fio_lock_i *lock)` | Busy-wait until every sub-lock is acquired. |
| `void fio_unlock_full(fio_lock_i *lock)` | Release every sub-lock. |

Example:

```c
fio_lock_i lock = FIO_LOCK_INIT;
if (!fio_trylock_group(&lock,
                       FIO_LOCK_SUBLOCK(1) | FIO_LOCK_SUBLOCK(2))) {
  /* critical section */
  fio_unlock_group(&lock, FIO_LOCK_SUBLOCK(1) | FIO_LOCK_SUBLOCK(2));
}
```

## Atomic bitmap access

Operate on a single bit inside a byte array. All four helpers are atomic at the byte level.

| Function | Behavior |
|---|---|
| `uint8_t fio_atomic_bit_get(void *map, size_t bit)` | Read bit `bit` from `map`. |
| `void fio_atomic_bit_set(void *map, size_t bit)` | Set bit `bit` to `1`. |
| `void fio_atomic_bit_unset(void *map, size_t bit)` | Set bit `bit` to `0`. |
| `void fio_atomic_bit_flip(void *map, size_t bit)` | Toggle bit `bit`. |

## Lock selector

[`./001 header.h`](./001%20header.h) chooses between facil.io spinlocks and OS mutexes. The default is spinlocks unless `FIO_USE_THREAD_MUTEX` is set to `1`.

| Macro | Purpose |
|---|---|
| `FIO_USE_THREAD_MUTEX_TMP` | Per-include override. Defaults to `FIO_USE_THREAD_MUTEX`. |
| `FIO_THREADS` | Defined when OS mutexes are selected. |
| `FIO___LOCK_NAME` | Human-readable name of the selected lock type. |
| `FIO___LOCK_TYPE` | The selected lock type (`fio_thread_mutex_t` or `fio_lock_i`). |
| `FIO___LOCK_INIT` | Initializer for the selected lock type. |
| `FIO___LOCK_DESTROY(lock)` | Destroy a lock instance. |
| `FIO___LOCK_LOCK(lock)` | Acquire the lock. |
| `FIO___LOCK_TRYLOCK(lock)` | Try to acquire the lock. |
| `FIO___LOCK_UNLOCK(lock)` | Release the lock. |

These macros are used by modules that need thread safety but do not care which lock type is active.

## Thread scheduling hints

Defined in [`./000 core.h`](./000%20core.h) and used by the spinlock back-off loops.

| Macro | Behavior |
|---|---|
| `FIO_THREAD_WAIT(nano_sec)` | Sleep the current thread for about `nano_sec` nanoseconds. POSIX uses `nanosleep`; Windows uses `Sleep` (rounded up to at least 1 ms). |
| `FIO_THREAD_YIELD()` | Hint to the CPU that the thread is spinning. Emits `pause` on x86, `yield` on ARM, calls `YieldProcessor()` on MSVC, or falls back to `sched_yield()` on POSIX. |
| `FIO_THREAD_RESCHEDULE()` | Short sleep via `FIO_THREAD_WAIT(4)`. Used by `fio_lock_group` to back off without fully suspending the thread. |
# Bit Operations

Bit-twiddling helpers defined in [`./000 core.h`](./000%20core.h).

## Bit rotation

### Generic rotation macros

```c
#define FIO_LROT(i, bits)
#define FIO_RROT(i, bits)
```

Rotate the value `i` left or right by `bits`. The rotation count is masked to the width of `i`, so these macros work for any integer type.

### Typed rotation helpers

Each width has a left and right variant. When the compiler provides a rotate builtin it is used; otherwise a portable shift-or expression is emitted. All are marked `FIO_CONST`.

Note: the `fio_rrot8` builtin guard has a typo (`__has_builtin(__builtin_rotatrightt8)`), so the 8-bit right rotation always falls back to the portable implementation.

| Width | Left | Right |
|---|---|---|
| 8-bit | `fio_lrot8(i, bits)` | `fio_rrot8(i, bits)` |
| 16-bit | `fio_lrot16(i, bits)` | `fio_rrot16(i, bits)` |
| 32-bit | `fio_lrot32(i, bits)` | `fio_rrot32(i, bits)` |
| 64-bit | `fio_lrot64(i, bits)` | `fio_rrot64(i, bits)` |
| 128-bit | `fio_lrot128(i, bits)` | `fio_rrot128(i, bits)` |

The 128-bit variants are only available when the compiler supports `__int128`/`__uint128_t`.

### Endian-aware forward rotation

```c
fio_frot16(i, bits)
fio_frot32(i, bits)
fio_frot64(i, bits)
```

On little-endian systems `fio_frot*` maps to the right-rotation helpers; on big-endian systems it maps to the left-rotation helpers. These are macros, not functions.

## Combined rotation-XOR operations

Common SHA-2/BLAKE2-style combinations kept as separate operations so the compiler can optimize them as a unit.

```c
uint32_t fio_xor_rrot3_32(uint32_t x, uint8_t a, uint8_t b, uint8_t c);
uint64_t fio_xor_rrot3_64(uint64_t x, uint8_t a, uint8_t b, uint8_t c);
```

Computes `ROTR(x,a) ^ ROTR(x,b) ^ ROTR(x,c)`. Used by SHA-256/SHA-512 Sigma functions.

```c
uint32_t fio_xor_rrot2_shr_32(uint32_t x, uint8_t a, uint8_t b, uint8_t c);
uint64_t fio_xor_rrot2_shr_64(uint64_t x, uint8_t a, uint8_t b, uint8_t c);
```

Computes `ROTR(x,a) ^ ROTR(x,b) ^ (x >> c)`. Used by SHA-256/SHA-512 lowercase sigma functions for message scheduling.

## XOR masking

```c
void fio_xmask(void *buf, size_t len, uint64_t mask);
void fio_xmask_cpy(char *restrict dest,
                   const char *src,
                   size_t len,
                   uint64_t mask);
```

XOR every byte of a buffer with a repeating 64-bit `mask`. `fio_xmask` works in place; `fio_xmask_cpy` reads from `src` and writes to `dest`. If `dest == src`, `fio_xmask_cpy` behaves like `fio_xmask`.

Both functions process data in 64-bit chunks for speed and fall back to a byte-sized tail for the last `0`–`7` bytes. Performance is best when the buffer is aligned.

## Popcount and Hamming distance

```c
int fio_popcount(uint64_t n);
```

Returns the number of `1` bits in `n`. Uses `__builtin_popcountll` or MSVC `__popcnt64` when available; otherwise falls back to a portable parallel bit-count implementation.

```c
#define fio_hemming_dist(n1, n2) \
    fio_popcount(((uint64_t)(n1) ^ (uint64_t)(n2)))
```

Returns the Hamming distance between `n1` and `n2` — the number of bits that differ. The macro is named `fio_hemming_dist` in the source (the conventional spelling is "Hamming").

## Bit isolation and indexing

```c
uint64_t fio_bits_lsb(uint64_t i);
uint64_t fio_bits_msb(uint64_t i);
```

* `fio_bits_lsb(i)` isolates the least-significant set bit.
* `fio_bits_msb(i)` isolates the most-significant set bit.

```c
size_t fio_bits_lsb_index(uint64_t i);
size_t fio_bits_msb_index(uint64_t i);
```

Return the bit index of the least-significant or most-significant set bit. Both return `(size_t)-1` when `i` is `0`.

```c
size_t fio_lsb_index_unsafe(uint64_t i);
size_t fio_msb_index_unsafe(uint64_t i);
```

Same as above but assume `i != 0`; calling them with `0` is undefined behavior. They use `__builtin_ctzll`/`__builtin_clzll` when available and fall back to `fio___single_bit_index_unsafe` — a `switch` over 64 power-of-two constants — otherwise.

## Byte detection in vectors

These helpers detect special byte values inside a packed integer. In the result, a matching byte lane is set to `0x80`; all other lanes are `0x00`.

### 32-bit vectors

```c
uint32_t fio_has_zero_byte32(uint32_t row);
uint32_t fio_has_byte32(uint32_t row, uint8_t byte);
uint32_t fio_has_full_byte32(uint32_t row);
```

* `fio_has_zero_byte32` detects `0x00` bytes.
* `fio_has_byte32` detects an arbitrary byte value.
* `fio_has_full_byte32` detects `0x00` bytes — the source's inline comment claims `0xFF`, but the implementation calls `fio_has_zero_byte32(row)`.

### 64-bit vectors

```c
uint64_t fio_has_zero_byte64(uint64_t row);
uint64_t fio_has_zero_byte_alt64(uint64_t row);
uint64_t fio_has_byte64(uint64_t row, uint8_t byte);
uint64_t fio_has_full_byte64(uint64_t row);
uint64_t fio_has_byte2bitmap(uint64_t result);
```

* `fio_has_zero_byte64` detects `0x00` bytes.
* `fio_has_zero_byte_alt64` is an alternate entry point with the same behavior as `fio_has_zero_byte64`; the source's warning about bitmap safety may be stale.
* `fio_has_byte64` detects an arbitrary byte value.
* `fio_has_full_byte64` detects `0xFF` bytes.
* `fio_has_byte2bitmap` packs a `fio_has_byte*` result into a single 8-bit bitmap.

## Bitmap access

Operate on a single bit inside a byte array. These are **not** atomic; use the [`fio_atomic_bit_*`](./001%20atomics%20and%20locks.md#atomic-bitmap-access) helpers for shared mutable bitmaps.

```c
uint8_t fio_bit_get(void *map, size_t bit);
void    fio_bit_set(void *map, size_t bit);
void    fio_bit_unset(void *map, size_t bit);
void    fio_bit_flip(void *map, size_t bit);
```

* `fio_bit_get` returns `0` or `1`.
* `fio_bit_set` sets the bit to `1`.
* `fio_bit_unset` sets the bit to `0`.
* `fio_bit_flip` toggles the bit.

## See also

- [`./000 core.h`](./000%20core.h) — source of truth for all macros and functions above.
- [`./001 atomics and locks.md`](./001%20atomics%20and%20locks.md) — atomic bitmap helpers and locks.
- [`./001 constant time.md`](./001%20constant%20time.md) — branchless bitwise primitives.
- [`./001 endian.md`](./001%20endian.md) — byte ordering helpers.
# Compiler Attributes

The macros in [`./000 core.h`](./000%20core.h) and [`./001 header.h`](./001%20header.h) paper over differences between C and C++, compilers, and inclusion models. This page is a quick reference for that portability scaffolding.

## C++ compatibility

Both core slices open an `extern "C"` block when compiled as C++. [`./000 core.h`](./000%20core.h) also neutralizes C-only keywords inside that block:

- `register` is defined away (deprecated in C++).
- `restrict` is defined away (not valid in C++).
- `_Bool` is mapped to `bool`.

No action is required; including the headers gives C++ code a C-compatible surface.

## Keyword shims

Some compilers need a little help with C99/C11 keywords:

- **`inline`** — mapped to `__inline` when the compiler does not support the C99 keyword directly (mainly older MSVC).
- **`__thread`** — mapped to `__declspec(thread)` on MSVC and `_Thread_local` on C11 compilers; otherwise left as `__thread` when the compiler supports it.

## Compiler detection and no-op macros

When the compiler is not GCC or Clang, `__attribute__`, `__has_include`, `__has_builtin`, and `__has_attribute` are defined as no-ops so the rest of the code can use them unconditionally.

- **`FIO_NOOP`** — empty macro that adds whitespace. Useful to stop a function-like macro from being expanded.
- **`FIO_NOOP_FN(...)`** — empty variadic macro, handy as a default callback.
- **`FIO_NOOP_FN_NAME`** — expands to `(void)`.

## OS detection

Defined in [`./000 core.h`](./000%20core.h); see [`./001 os detection.md`](./001%20os%20detection.md) for full details.

- **`FIO_OS_POSIX`** — 1 on POSIX systems.
- **`FIO_OS_WIN`** — 1 on Windows.
- **`FIO_HAVE_UNIX_TOOLS`** — 0 (none), 1 (POSIX), 2 (MinGW), or 3 (Cygwin).

## Function attributes

Defined in [`./000 core.h`](./000%20core.h) and used throughout the library.

- **`FIO_MAYBE_UNUSED`** — `__attribute__((unused))` on GCC/Clang, empty otherwise.
- **`FIO_SFUNC`** — `static FIO_MAYBE_UNUSED`. Default internal linkage.
- **`FIO_IFUNC`** — `FIO_SFUNC inline`. Static inline helper.
- **`FIO_WARN_UNUSED`** — `__attribute__((warn_unused_result))` or empty.
- **`FIO_MIFN`** — `FIO_IFUNC FIO_WARN_UNUSED`. Inline math-style function that warns if the return value is ignored.
- **`FIO_CONST`** — function result depends only on arguments; no globals or side effects.
- **`FIO_PURE`** — no side effects, but may read global memory.
- **`FIO_WEAK`** — weak symbol. Empty on MinGW.
- **`DEPRECATED(reason)`** — `__attribute__((deprecated(reason)))`, with a fallback for GCC older than 4.5.
- **`FIO_CONSTRUCTOR(fname)`** — runs before `main` on GCC/Clang; on MSVC it places a pointer in `.CRT$XCU`.
- **`FIO_DESTRUCTOR(fname)`** — runs after `main` on GCC/Clang; on MSVC it is implemented as a constructor that registers `atexit(fname)`.
- **`FIO_LIKELY(cond)`** / **`FIO_UNLIKELY(cond)`** — branch-prediction hints. No-op on unknown compilers.
- **`FIO_PREFETCH(ptr)`** / **`FIO_PREFETCH_W(ptr)`** / **`FIO_PREFETCH_NT(ptr)`** — cache prefetch hints for read, write-exclusive, and non-temporal streaming access.
- **`FIO___PRINTF_STYLE(string_index, check_index)`** — tells the compiler to check `printf`-style format arguments. Empty on pure MSVC; uses the MinGW/Cygwin format attribute on those compilers; otherwise uses the GCC/Clang `printf` format attribute.

## Variable and type attributes

Defined in [`./000 core.h`](./000%20core.h).

- **`FIO_ALIGN(bytes)`** — alignment hint. Uses `__attribute__((aligned(bytes)))` on GCC/Clang; currently empty on other compilers.
- **`FIO_WEAK_VAR`** — weak global variable. Uses `__declspec(selectany)` on Windows when selectany support is detected (MSVC or otherwise); `__attribute__((weak))` elsewhere.

## Macro stringifier

Defined in [`./000 core.h`](./000%20core.h).

- **`FIO_MACRO2STR(macro)`** — expands `macro` and stringifies the result. Uses a two-step indirection so tokens like `__LINE__` are expanded first.

## Static assertions

Defined in [`./000 core.h`](./000%20core.h).

- **`FIO_ASSERT_STATIC(cond, msg)`** — compile-time assertion. Uses C11 `_Static_assert` when available; otherwise falls back to a negative-array-size trick that produces a compile error on failure.

## Naming helpers

Defined in [`./000 core.h`](./000%20core.h).

- **`FIO_NAME(prefix, postfix)`** → `prefix_postfix`.
- **`FIO_NAME2(prefix, postfix)`** → `prefix2postfix` (for conversion functions).
- **`FIO_NAME_BL(prefix, postfix)`** → `prefix_is_postfix` (for boolean tests).
- **`FIO_NAME_TEST(prefix, postfix)`** → internal test-function name (`fio___test_prefix_postfix`).

## Pointer math

All defined in [`./000 core.h`](./000%20core.h).

- **`FIO_PTR_MATH_LMASK(T, ptr, bits)`** — keep the low `bits` of an address.
- **`FIO_PTR_MATH_RMASK(T, ptr, bits)`** — keep the high bits, zero the low `bits`.
- **`FIO_PTR_MATH_ADD(T, ptr, offset)`** — add `offset` bytes and cast to `T *`.
- **`FIO_PTR_MATH_SUB(T, ptr, offset)`** — subtract `offset` bytes and cast to `T *`.
- **`FIO_PTR_FIELD_OFFSET(T, field)`** — byte offset of `field` inside `T`. Uses `0xFF00` as a base address to keep AddressSanitizer happy.
- **`FIO_PTR_FROM_FIELD(T, field, ptr)`** — recover the containing `T` object from a pointer to one of its fields.

## Pointer tagging

Defined in [`./001 header.h`](./001%20header.h); see [`./001 version and settings.md`](./001%20version%20and%20settings.md) for full details.

- **`FIO_PTR_TAG(p)`** / **`FIO_PTR_UNTAG(p)`** — default no-ops; override to embed tags in pointers.
- **`FIO_PTR_TAG_TYPE`** — when defined, pointer-returning functions use this type instead.
- **`FIO_PTR_TAG_VALIDATE(ptr)`** — validates a tagged pointer; default accepts non-NULL.
- **`FIO_PTR_TAG_VALID_OR_RETURN(tagged_ptr, value)`** — return `value` if validation fails.
- **`FIO_PTR_TAG_VALID_OR_RETURN_VOID(tagged_ptr)`** — return void if validation fails.
- **`FIO_PTR_TAG_VALID_OR_GOTO(tagged_ptr, label)`** — jump to `label` if validation fails.
- **`FIO_PTR_TAG_GET_UNTAGGED(untagged_type, tagged_ptr)`** — cast `FIO_PTR_UNTAG(tagged_ptr)` to `untagged_type *`.

## Get/set helpers

Generate typed getters and setters for struct fields.

- **`FIO_DEF_GETSET_FUNC(static, namespace, T_type, F_type, F_name, on_set)`** — defines both `namespace_F_name` (getter) and `namespace_F_name_set` (setter).
- **`FIO_DEF_GETSET_FUNC_DEC(static, namespace, T_type, F_type, F_name)`** — declarations only.
- **`FIO_IFUNC_DEF_GETSET(namespace, T_type, F_type, F_name, on_set)`** — same, but uses `FIO_IFUNC` as the linkage. `FIO_IFUNC_DEF_GET` and `FIO_IFUNC_DEF_SET` exist for the individual pieces.

The setter returns the old value and calls `on_set(o)` after assignment.

## Recursive inclusion and linkage

facil.io headers are designed to be included more than once to generate code. [`./001 header.h`](./001%20header.h) manages whether generated functions are `static` or `extern`.

- **`SFUNC_`** / **`IFUNC_`** — internal linkage selectors. On the first include they become `FIO_SFUNC`/`FIO_IFUNC` unless `FIO_EXTERN` is defined, in which case they are empty.
- **`SFUNC`** / **`IFUNC`** — public aliases for the current linkage selectors.
- **`FIO_EXTERN`** — define before including a module to emit non-static declarations and inline functions.
- **`FIO___RECURSIVE_INCLUDE`** — internal flag. Setting it to `99` before a recursive `#include` preserves the original `SFUNC`/`IFUNC` linkage instead of forcing it back to `static`.

## Compiler memory barriers

- **`FIO_COMPILER_GUARD`** — clobber memory and prevent the compiler from reordering instructions around it.
- **`FIO_COMPILER_GUARD_INSTRUCTION`** — prevent instruction reordering without the full memory clobber.

On GCC/Clang these are inline `asm volatile` barriers; on MSVC they map to `_ReadWriteBarrier` and `_WriteBarrier`.

## Thread scheduling

Defined in [`./000 core.h`](./000%20core.h); see [`./001 atomics and locks.md`](./001%20atomics%20and%20locks.md) for full details.

- **`FIO_THREAD_WAIT(nano_sec)`** — sleep for about `nano_sec` nanoseconds (`Sleep` on Windows, `nanosleep` on POSIX).
- **`FIO_THREAD_YIELD()`** — yield the CPU (`pause`/`yield` instruction or `sched_yield`).
- **`FIO_THREAD_RESCHEDULE()`** — short sleep via `FIO_THREAD_WAIT(4)`.

## Address-sanitizer scaffolding

- **`FIO___ASAN_DETECTED`** — set when AddressSanitizer is active.
- **`FIO___ASAN_AVOID`** — expands to `no_sanitize_address` when ASan is detected; otherwise empty.

## Loop helpers

- **`FIO_FOR(i, count)`** — `for (size_t i = 0; i < (count); ++i)`.
- **`FIO_FOR_UNROLL(iterations, size_of_loop, i, action)`** — splits a loop into a small remainder and 256-byte chunks to nudge the compiler toward auto-vectorization. Used heavily by the memory primitives and vector-math templates. The SIMD file covers the portable x86 intrinsics that build on these helpers.
# Constant-Time Helpers

Branchless, timing-aware primitives defined in [`./000 core.h`](./000%20core.h).

These helpers avoid data-dependent branches and early exits so that execution time does not leak information about the values being processed. They are used by the cryptographic modules, authentication code, and anywhere else a compiler might otherwise turn a secret into a branch.

## Boolean constants

```c
uintmax_t fio_ct_true(uintmax_t cond);
uintmax_t fio_ct_false(uintmax_t cond);
```

* `fio_ct_true(cond)` returns `1` if `cond` is non-zero, else `0`.
* `fio_ct_false(cond)` returns `1` if `cond` is zero, else `0`.

Both return only the value `0` or `1`, suitable for feeding into `fio_ct_if_bool`.

## Conditional selection

```c
uintmax_t fio_ct_if_bool(uintmax_t cond, uintmax_t a, uintmax_t b);
uintmax_t fio_ct_if(uintmax_t cond, uintmax_t a, uintmax_t b);
```

* `fio_ct_if_bool` returns `a` when `cond == 1`, otherwise `b`.
* `fio_ct_if` first normalizes `cond` with `fio_ct_true`, then returns `a` when `cond` is non-zero, otherwise `b`.

Both are branchless: they compute `b ^ (mask & (a ^ b))`.

## Min, max, and absolute value

```c
intmax_t  fio_ct_max(intmax_t a, intmax_t b);
intmax_t  fio_ct_min(intmax_t a, intmax_t b);
uintmax_t fio_ct_abs(intmax_t i);
```

* `fio_ct_max` / `fio_ct_min` perform signed comparisons without branches.
* `fio_ct_abs` returns the absolute value of `i`.

## ASCII case conversion

```c
char fio_ct_tolower(char c);
```

Returns the lowercase form of `c` when `c` is an ASCII uppercase letter (`A`–`Z`); otherwise returns `c` unchanged. Branchless and locale-independent.

## Bitwise mux, majority, and three-way XOR

```c
uint32_t fio_ct_mux32(uint32_t x, uint32_t y, uint32_t z);
uint64_t fio_ct_mux64(uint64_t x, uint64_t y, uint64_t z);

uint32_t fio_ct_maj32(uint32_t x, uint32_t y, uint32_t z);
uint64_t fio_ct_maj64(uint64_t x, uint64_t y, uint64_t z);

uint32_t fio_ct_xor3_32(uint32_t x, uint32_t y, uint32_t z);
uint64_t fio_ct_xor3_64(uint64_t x, uint64_t y, uint64_t z);
```

| Function | Operation | Formula |
|----------|-----------|---------|
| `fio_ct_mux32/64` | bitwise choose | `z ^ (x & (y ^ z))` |
| `fio_ct_maj32/64` | bitwise majority | `(x & y) \| (z & (x \| y))` |
| `fio_ct_xor3_32/64` | bitwise parity | `x ^ y ^ z` |

These are the SHA-style `Ch`, `Maj`, and parity functions, implemented without branches.

## Constant-time equality

```c
_Bool fio_ct_is_eq(const void *a, const void *b, size_t bytes);
```

Compares two memory regions and returns `1` if they are byte-for-byte identical, `0` otherwise. The comparison is timing-attack resistant: it always reads every byte and accumulates differences into a single flag before returning, regardless of where the first mismatch occurs.

## Secure zero and stack wipe

```c
void fio_secure_zero(void *a_, size_t bytes);
```

Zeros `bytes` starting at `a_`. The write is performed through a `volatile` pointer so the compiler cannot optimize it away. Use this for passwords, keys, and other sensitive material that must be erased from memory.

```c
#define FIO_MEM_STACK_WIPE(pages)
```

Allocates a volatile stack array of `(pages) * 4096` bytes and initializes it to zero. Useful for clearing sensitive stack scratch space; the `(void)stack_mem` use keeps the array alive through the scope.
# Endian and Byte Ordering

Macros and inline helpers for detecting and converting byte order. All are defined in [`./000 core.h`](./000%20core.h).

facil.io assumes one of the two common orderings: big-endian or little-endian. Mixed-endian systems are not supported.

## Compile-Time Detection

The library normalizes two common compiler-provided symbols so downstream code can test endianness with simple `#if` checks.

- **`__BIG_ENDIAN__`** — set to `1` on big-endian systems, `0` on little-endian systems.
- **`__LITTLE_ENDIAN__`** — set to `1` on little-endian systems, `0` on big-endian systems.

The library also checks `__BYTE_ORDER__` as an input hint when the compiler provides it. If the compiler already defines `__BIG_ENDIAN__`, `__LITTLE_ENDIAN__`, or `__BYTE_ORDER__`, those values are honored. Otherwise facil.io defines `FIO_ENDIAN_ORDER_TEST` (`'1234'`), `FIO_LITTLE_ENDIAN_TEST` (`0x31323334UL`), and `FIO_BIG_ENDIAN_TEST` (`0x34333231UL`), then compares the unprefixed names in `#if ENDIAN_ORDER_TEST == LITTLE_ENDIAN_TEST` and `#elif ENDIAN_ORDER_TEST == BIG_ENDIAN_TEST`. If neither matches, compilation fails with an error.

The comparison deliberately uses the unprefixed names `ENDIAN_ORDER_TEST`, `LITTLE_ENDIAN_TEST`, and `BIG_ENDIAN_TEST`; those exact identifiers are not defined by facil.io. This mirrors the source.

These are constants, not variables; use them in `#if __BIG_ENDIAN__` rather than runtime `if` statements when possible.

## Runtime Detection

```c
unsigned int fio_is_little_endian(void);
unsigned int fio_is_big_endian(void);
```

Runtime tests that return a non-zero value when the machine matches the named ordering. `fio_is_big_endian` is implemented as `!fio_is_little_endian()`. Compilers usually constant-fold the result, but the functions are provided for cases where a runtime check is clearer.

## Byte Swapping

Reverse the byte order of a fixed-width integer. Each macro expands to the corresponding compiler builtin when available; otherwise it falls back to a portable bit-shifting implementation.

- `fio_bswap8(i)` — identity, defined so generic code can use the `8`-bit width without special cases.
- `fio_bswap16(i)` — swap bytes of a 16-bit integer.
- `fio_bswap32(i)` — swap bytes of a 32-bit integer.
- `fio_bswap64(i)` — swap bytes of a 64-bit integer.
- `fio_bswap128(i)` — swap bytes of a 128-bit integer. Only available when the compiler supports `__int128`/`__uint128_t`.

## Host ↔ Network Conversions

Network byte order is big-endian. These macros convert between the local byte order and network byte order.

- `fio_lton16(i)`, `fio_lton32(i)`, `fio_lton64(i)` — local to network.
- `fio_ntol16(i)`, `fio_ntol32(i)`, `fio_ntol64(i)` — network to local.
- `fio_lton128(i)` — local to network, defined only on little-endian systems where the compiler supports `__int128`/`__uint128_t`.
- `fio_ntol128(i)` — network to local, defined only when the compiler supports `__int128`/`__uint128_t`.

On big-endian systems the 16/32/64-bit macros are no-ops. On little-endian systems they byte-swap.

`fio_lton8(i)` and `fio_ntol8(i)` are also defined as identities so width-generic code can treat 8-bit values uniformly.

## Host ↔ Little-Endian Conversions

These helpers force values into little-endian byte order, which is also the ordering used by several internal data structures.

- `fio_ltole16(i)`, `fio_ltole32(i)`, `fio_ltole64(i)` — local to little-endian.
- `fio_ltole128(i)` — local to little-endian, only available when the compiler supports `__int128`/`__uint128_t`.

On little-endian systems they are no-ops. On big-endian systems they byte-swap.

`fio_ltole8(i)` is defined as an identity for generic width handling.

## Endian-Aware Shifts

A pair of shift macros whose direction depends on the host endianness. They are used in code that processes multi-byte values as bit patterns where the conceptual "forward" or "backward" direction reverses with endianness.

On little-endian systems:

```c
#define FIO_SHIFT_FORWARDS(i, bits)  ((i) << (bits))
#define FIO_SHIFT_BACKWARDS(i, bits) ((i) >> (bits))
```

On big-endian systems they are defined only when the compiler supports `__int128`/`__uint128_t`:

```c
#define FIO_SHIFT_FORWARDS(i, bits)  ((i) >> (bits))
#define FIO_SHIFT_BACKWARDS(i, bits) ((i) << (bits))
```

## See Also

- [`./000 core.h`](./000%20core.h) — source of truth for all macros and functions above.
- [`./000 core.md`](./000%20core.md) — overview of core types and helpers.
# Linked Lists

```c
#include "fio-stl.h"
```

Small, sharp linked-list macros from [`./000 core.h`](./000%20core.h). There are two families:

- pointer-based circular doubly linked lists, using `next` / `prev` pointers;
- indexed circular doubly linked lists, using integer offsets into an array.

The macros do not allocate memory, do not own stored objects, and do not check that nodes are initialized. Fast tools, pointy edges.

### Types

#### `fio_list_node_s`

```c
typedef struct fio_list_node_s {
  struct fio_list_node_s *next;
  struct fio_list_node_s *prev;
} fio_list_node_s;
```

The pointer-based list link. Embed it inside the struct you want to place in a list.

#### `FIO_LIST_NODE`

```c
#define FIO_LIST_NODE fio_list_node_s
```

Semantic alias for an embedded node field.

#### `FIO_LIST_HEAD`

```c
#define FIO_LIST_HEAD fio_list_node_s
```

Semantic alias for a list head/root. A list head is also a node, and an empty list points back to itself.

#### `fio_index32_node_s`

```c
typedef struct fio_index32_node_s {
  uint32_t next;
  uint32_t prev;
} fio_index32_node_s;
```

A 32-bit indexed-list link. It stores indexes into a caller-owned array.

#### `fio_index16_node_s`

```c
typedef struct fio_index16_node_s {
  uint16_t next;
  uint16_t prev;
} fio_index16_node_s;
```

A 16-bit indexed-list link.

#### `fio_index8_node_s`

```c
typedef struct fio_index8_node_s {
  uint8_t next;
  uint8_t prev;
} fio_index8_node_s;
```

An 8-bit indexed-list link.

#### `FIO_INDEXED_LIST32_NODE`, `FIO_INDEXED_LIST32_HEAD`

```c
#define FIO_INDEXED_LIST32_NODE fio_index32_node_s
#define FIO_INDEXED_LIST32_HEAD uint32_t
```

32-bit indexed-list node and head index types.

#### `FIO_INDEXED_LIST16_NODE`, `FIO_INDEXED_LIST16_HEAD`

```c
#define FIO_INDEXED_LIST16_NODE fio_index16_node_s
#define FIO_INDEXED_LIST16_HEAD uint16_t
```

16-bit indexed-list node and head index types.

#### `FIO_INDEXED_LIST8_NODE`, `FIO_INDEXED_LIST8_HEAD`

```c
#define FIO_INDEXED_LIST8_NODE fio_index8_node_s
#define FIO_INDEXED_LIST8_HEAD uint8_t
```

8-bit indexed-list node and head index types.

### API Functions

#### `FIO_LIST_INIT`

```c
#define FIO_LIST_INIT(obj)                                                     \
  (fio_list_node_s) { .next = &(obj), .prev = &(obj) }
```

Initializer for a pointer-list node or head. The node points to itself.

Use it for heads:

```c
FIO_LIST_HEAD list = FIO_LIST_INIT(list);
```

and for standalone nodes before insertion:

```c
item_s item = {.node = FIO_LIST_INIT(item.node)};
```

#### `FIO_LIST_EACH`

```c
#define FIO_LIST_EACH(type, node_name, head, pos)                              \
  for (type *pos = FIO_PTR_FROM_FIELD(type, node_name, (head)->next),          \
            *next____p_ls_##pos =                                              \
                FIO_PTR_FROM_FIELD(type, node_name, (head)->next->next);       \
       pos != FIO_PTR_FROM_FIELD(type, node_name, (head));                     \
       (pos = next____p_ls_##pos),                                             \
            (next____p_ls_##pos =                                              \
                 FIO_PTR_FROM_FIELD(type,                                      \
                                    node_name,                                 \
                                    next____p_ls_##pos->node_name.next)))
```

Iterates forward over every object after `head`. `pos` is declared by the macro as `type *`.

The next pointer is cached before the loop body, so removing the current `pos` is supported.

#### `FIO_LIST_EACH_REVERSED`

```c
#define FIO_LIST_EACH_REVERSED(type, node_name, head, pos)                     \
  for (type *pos = FIO_PTR_FROM_FIELD(type, node_name, (head)->prev),          \
            *next____p_ls_##pos =                                              \
                FIO_PTR_FROM_FIELD(type, node_name, (head)->next->prev);       \
       pos != FIO_PTR_FROM_FIELD(type, node_name, (head));                     \
       (pos = next____p_ls_##pos),                                             \
            (next____p_ls_##pos =                                              \
                 FIO_PTR_FROM_FIELD(type,                                      \
                                    node_name,                                 \
                                    next____p_ls_##pos->node_name.prev)))
```

Attempts to iterate backward over every object before `head`. `pos` is declared by the macro as `type *`.

**Note:** the current implementation caches the next pointer from `(head)->next->prev`, which equals `head` for a normal list, so the loop stops after visiting the first reversed node. Treat full reverse iteration as limited/buggy until the source macro is fixed.

#### `FIO_LIST_PUSH`

```c
#define FIO_LIST_PUSH(head, n)                                                 \
  do {                                                                         \
    (n)->prev = (head)->prev;                                                  \
    (n)->next = (head);                                                        \
    (head)->prev->next = (n);                                                  \
    (head)->prev = (n);                                                        \
  } while (0)
```

Pushes node `n` to the end of the circular list, just before `head`.

Both `head` and `n` must point to initialized `FIO_LIST_NODE` / `FIO_LIST_HEAD` objects. The macro does not check whether `n` already belongs to another list.

#### `FIO_LIST_REMOVE`

```c
#define FIO_LIST_REMOVE(n)                                                     \
  do {                                                                         \
    (n)->prev->next = (n)->next;                                               \
    (n)->next->prev = (n)->prev;                                               \
  } while (0)
```

Removes `n` from its current list. The node's own `next` and `prev` fields are left pointing at the old neighbors.

#### `FIO_LIST_REMOVE_RESET`

```c
#define FIO_LIST_REMOVE_RESET(n)                                               \
  do {                                                                         \
    (n)->prev->next = (n)->next;                                               \
    (n)->next->prev = (n)->prev;                                               \
    (n)->next = (n)->prev = (n);                                               \
  } while (0)
```

Removes `n`, then resets it to a self-linked node.

#### `FIO_LIST_POP`

```c
#define FIO_LIST_POP(type, node_name, dest_ptr, head)                          \
  do {                                                                         \
    (dest_ptr) = FIO_PTR_FROM_FIELD(type, node_name, ((head)->next));          \
    FIO_LIST_REMOVE(&(dest_ptr)->node_name);                                   \
  } while (0)
```

Removes the first object after `head` and stores the containing object pointer in `dest_ptr`.

Do not call this on an empty list. It will treat the head as a stored object and wander into undefined behavior.

#### `FIO_LIST_IS_EMPTY`

```c
#define FIO_LIST_IS_EMPTY(head)                                                \
  ((!(head)) || ((!(head)->next) | ((head)->next == (head))))
```

Returns non-zero if `head` is `NULL`, `head->next` is `NULL`, or the head points to itself.

#### `FIO_INDEXED_LIST_PUSH`

```c
#define FIO_INDEXED_LIST_PUSH(root, node_name, head, i)                        \
  do {                                                                         \
    register const size_t n__ = (i);                                           \
    (root)[n__].node_name.prev = (root)[(head)].node_name.prev;                \
    (root)[n__].node_name.next = (head);                                       \
    (root)[(root)[(head)].node_name.prev].node_name.next = (n__);              \
    (root)[(head)].node_name.prev = (n__);                                     \
  } while (0)
```

Adds index `i` to the end of the indexed circular list, just before `head`. `head` is not changed.

`root` is the array containing all elements, and `node_name` is the embedded indexed-list node field.

#### `FIO_INDEXED_LIST_UNSHIFT`

```c
#define FIO_INDEXED_LIST_UNSHIFT(root, node_name, head, i)                     \
  do {                                                                         \
    register const size_t n__ = (i);                                           \
    (root)[n__].node_name.next = (root)[(head)].node_name.next;                \
    (root)[n__].node_name.prev = (head);                                       \
    (root)[(root)[(head)].node_name.next].node_name.prev = (n__);              \
    (root)[(head)].node_name.next = (n__);                                     \
    (head) = (n__);                                                            \
  } while (0)
```

Adds index `i` to the beginning of the list and assigns `head = i`. The `head` argument must be an assignable lvalue.

#### `FIO_INDEXED_LIST_REMOVE`

```c
#define FIO_INDEXED_LIST_REMOVE(root, node_name, i)                            \
  do {                                                                         \
    register const size_t n__ = (i);                                           \
    (root)[(root)[n__].node_name.prev].node_name.next =                        \
        (root)[n__].node_name.next;                                            \
    (root)[(root)[n__].node_name.next].node_name.prev =                        \
        (root)[n__].node_name.prev;                                            \
  } while (0)
```

Removes index `i` from its current indexed list. The removed node keeps its previous links.

#### `FIO_INDEXED_LIST_REMOVE_RESET`

```c
#define FIO_INDEXED_LIST_REMOVE_RESET(root, node_name, i)                      \
  do {                                                                         \
    register const size_t n__ = (i);                                           \
    (root)[(root)[n__].node_name.prev].node_name.next =                        \
        (root)[n__].node_name.next;                                            \
    (root)[(root)[n__].node_name.next].node_name.prev =                        \
        (root)[n__].node_name.prev;                                            \
    (root)[n__].node_name.next = (root)[n__].node_name.prev = (n__);           \
  } while (0)
```

Removes index `i`, then resets its indexed node so `next == prev == i`.

#### `FIO_INDEXED_LIST_EACH`

```c
#define FIO_INDEXED_LIST_EACH(root, node_name, head, pos)                      \
  for (size_t pos = (head),                                                    \
              stooper___hd = (head),                                           \
              stopper___ils___ = 0,                                            \
              pos##___nxt = (root)[(head)].node_name.next;                     \
       !stopper___ils___;                                                      \
       (stopper___ils___ = ((pos = pos##___nxt) == stooper___hd)),             \
              pos##___nxt = (root)[pos].node_name.next)
```

Iterates forward over an indexed circular list, including `head`. `pos` is declared by the macro as `size_t`.

`head` must be a valid index. Empty-list sentinels are a caller convention, not something this macro handles.

#### `FIO_INDEXED_LIST_EACH_REVERSED`

```c
#define FIO_INDEXED_LIST_EACH_REVERSED(root, node_name, head, pos)             \
  for (size_t pos = ((root)[(head)].node_name.prev),                           \
              pos##___nxt =                                                    \
                  ((root)[((root)[(head)].node_name.prev)].node_name.prev),    \
              stooper___hd = (head),                                           \
              stopper___ils___ = 0;                                            \
       !stopper___ils___;                                                      \
       ((stopper___ils___ = (pos == stooper___hd)),                            \
        (pos = pos##___nxt),                                                   \
        (pos##___nxt = (root)[pos##___nxt].node_name.prev)))
```

Iterates backward over an indexed circular list, including the tail and eventually the head.

### Examples

#### Pointer list

```c
#include "fio-stl.h"

typedef struct item_s {
  FIO_LIST_NODE node;
  int value;
} item_s;

int main(void) {
  FIO_LIST_HEAD list = FIO_LIST_INIT(list);
  item_s a = {.node = FIO_LIST_INIT(a.node), .value = 1};
  item_s b = {.node = FIO_LIST_INIT(b.node), .value = 2};

  FIO_LIST_PUSH(&list, &a.node);
  FIO_LIST_PUSH(&list, &b.node);

  int sum = 0;
  FIO_LIST_EACH(item_s, node, &list, pos) {
    sum += pos->value;
  }

  item_s *first = 0;
  FIO_LIST_POP(item_s, node, first, &list);

  return (sum == 3 && first == &a) ? 0 : 1;
}
```

#### Safe removal while iterating

```c
#include "fio-stl.h"

typedef struct item_s {
  FIO_LIST_NODE node;
  int value;
} item_s;

int main(void) {
  FIO_LIST_HEAD list = FIO_LIST_INIT(list);
  item_s a = {.node = FIO_LIST_INIT(a.node), .value = 1};
  item_s b = {.node = FIO_LIST_INIT(b.node), .value = 2};

  FIO_LIST_PUSH(&list, &a.node);
  FIO_LIST_PUSH(&list, &b.node);

  FIO_LIST_EACH(item_s, node, &list, pos) {
    if (pos->value == 1)
      FIO_LIST_REMOVE_RESET(&pos->node);
  }

  return FIO_LIST_IS_EMPTY(&list) ? 1 : 0;
}
```

#### Indexed list

```c
#include "fio-stl.h"

typedef struct slot_s {
  FIO_INDEXED_LIST16_NODE node;
  int value;
} slot_s;

int main(void) {
  slot_s slots[3] = {{{0}}};
  FIO_INDEXED_LIST16_HEAD head = 0;

  slots[0].node.next = slots[0].node.prev = 0;
  slots[0].value = 10;

  slots[1].node.next = slots[1].node.prev = 1;
  slots[1].value = 20;

  slots[2].node.next = slots[2].node.prev = 2;
  slots[2].value = 30;

  FIO_INDEXED_LIST_PUSH(slots, node, head, 1);
  FIO_INDEXED_LIST_PUSH(slots, node, head, 2);

  int sum = 0;
  FIO_INDEXED_LIST_EACH(slots, node, head, pos) {
    sum += slots[pos].value;
  }

  FIO_INDEXED_LIST_REMOVE_RESET(slots, node, 1);

  return (sum == 60 && slots[1].node.next == 1) ? 0 : 1;
}
```

### Safety Notes

#### Initialization

Every head or node must be initialized before use. Pointer lists have `FIO_LIST_INIT`; indexed lists do not have a dedicated init macro, so initialize an indexed node with:

```c
root[i].node.next = root[i].node.prev = i;
```

#### Empty lists

Pointer lists have a natural empty state: `head->next == head`. `FIO_LIST_IS_EMPTY` checks it.

Indexed lists usually reserve an out-of-range value, often `(type)-1`, to mean empty. The indexed macros do not check for this sentinel; call them only when `head` is a valid index.

#### Index width

The indexed macros use `size_t` temporaries, then store into `uint32_t`, `uint16_t`, or `uint8_t` node fields. Keep every index representable by the chosen node type.

#### Removal and iteration

The iteration macros cache the next node/index before running the loop body, so removing the current non-head item is supported. If you remove or reset the current head in an indexed list, update your `head` value deliberately.

#### Memory ownership

Lists own no memory. Removing or popping unlinks nodes only; it does not destroy containing objects.

#### Thread safety

Mutating a list is not atomic. Multiple threads may read immutable lists, but concurrent mutation needs external synchronization.

#### Macro expansion

All APIs here are macros. Arguments may be evaluated more than once, and helper variable names are introduced inside loops. Pass simple lvalues/pointers, not expressions with side effects.
# Logging and Assertions

```c
#define FIO_LOG
#include "fio-stl.h"
```

Heap-allocation-free logging macros and assertion utilities. `FIO_LOG2STDERR` and the `FIO_LOG_*` macros are functional when `FIO_LOG` or `FIO_LEAK_COUNTER` is defined; otherwise they are no-ops.

`FIO_LOG` uses `libc` functions (`vsnprintf`, `fwrite`) and the library helper `fio_memcpy32`. If you are building without `libc`, provide your own implementations or shadowing macros before including the module.

## Log Levels

```c
#define FIO_LOG_LEVEL_NONE    0  /* no logging */
#define FIO_LOG_LEVEL_FATAL   1  /* fatal errors */
#define FIO_LOG_LEVEL_ERROR   2  /* errors and above */
#define FIO_LOG_LEVEL_WARNING 3  /* warnings and above */
#define FIO_LOG_LEVEL_INFO    4  /* info and above */
#define FIO_LOG_LEVEL_DEBUG   5  /* everything, including debug */
```

## Log Level Control

### `FIO_LOG_LEVEL_SET`

```c
#define FIO_LOG_LEVEL_SET(new_level) fio___log_level_set(new_level)
```

Sets the application-wide logging level. Returns the new level.

### `FIO_LOG_LEVEL_GET`

```c
#define FIO_LOG_LEVEL_GET() ((fio___log_level()))
```

Returns the current logging level as an integer.

```c
FIO_LOG_LEVEL_SET(FIO_LOG_LEVEL_WARNING);
int level = FIO_LOG_LEVEL_GET(); /* 3 */
```

## Configuration

### `FIO_LOG_LEVEL_DEFAULT`

Initial level. Defaults to `FIO_LOG_LEVEL_INFO` unless `DEBUG` is defined and truthy (`defined(DEBUG) && DEBUG`), in which case it is `FIO_LOG_LEVEL_DEBUG`.

### `FIO_STDERR_FILE`

```c
#define FIO_STDERR_FILE stderr
```

Output destination for all logging. Define it before including `./fio-stl.h` to redirect logs to another `FILE*`.

### `FIO_LOG_LENGTH_LIMIT`

```c
#define FIO_LOG_LENGTH_LIMIT 1024
```

Log buffer size. Keep this above `128`.

- When `FIO_LOG_LENGTH_LIMIT > 128`, the formatted message is limited to `FIO_LOG_LENGTH_LIMIT - 34` bytes, followed by a 32-byte truncation warning.
- When `FIO_LOG_LENGTH_LIMIT <= 128`, `vsnprintf` is capped at `FIO_LOG_LENGTH_LIMIT - 2` bytes; the truncation warning is appended afterward.
- Values `<= 2` are dangerous: the `vsnprintf` size argument underflows/overflows.

## Core Logging Function

### `FIO_LOG2STDERR`

```c
void FIO_LOG2STDERR(const char *format, ...);
```

`printf`-style output to `FIO_STDERR_FILE` using only stack-allocated memory. Truncation follows the two `FIO_LOG_LENGTH_LIMIT` paths described above. In `./fio-stl/001 logging.h` the name is `#undef`-ed before the `static` function definition, so a pre-include macro is removed and a post-inclusion function definition would conflict. Override it after inclusion only by defining `FIO_LOG2STDERR` as a macro.

## Log Macros

Most macros check the current log level before printing. `FIO_LOG_WRITE` has no level check and always prefixes output with the file and line number.

| Macro | Level | Prefix |
|-------|-------|--------|
| `FIO_LOG_FATAL(...)` | ≥ `FATAL` | `FATAL:` (bold inverse) |
| `FIO_LOG_ERROR(...)` | ≥ `ERROR` | `ERROR:` (bold) |
| `FIO_LOG_SECURITY(...)` | ≥ `ERROR` | `SECURITY:` (bold) |
| `FIO_LOG_WARNING(...)` | ≥ `WARNING` | `WARNING:` (dim) |
| `FIO_LOG_INFO(...)` | ≥ `INFO` | `INFO:` |
| `FIO_LOG_DEBUG(...)` | ≥ `DEBUG` | `DEBUG:` + file:line |
| `FIO_LOG_DEBUG2(...)` | ≥ `DEBUG` | `DEBUG:` |
| `FIO_LOG_WRITE(...)` | always | file:line |

## Debug-Only Log Macros

These expand to their non-`D` counterparts when `DEBUG` is defined, and to no-ops otherwise.

- `FIO_LOG_DDEBUG(...)`
- `FIO_LOG_DDEBUG2(...)`
- `FIO_LOG_DERROR(...)`
- `FIO_LOG_DSECURITY(...)`
- `FIO_LOG_DWARNING(...)`
- `FIO_LOG_DINFO(...)`

## Assertions

### `FIO_ASSERT`

```c
#define FIO_ASSERT(cond, ...)
```

If `cond` is false, prints a fatal message, prints `errno` and its string, sends `SIGINT` in debug builds, and calls `exit(-1)`.

### `FIO_ASSERT_ALLOC`

```c
#define FIO_ASSERT_ALLOC(ptr) FIO_ASSERT((ptr), "memory allocation failed.")
```

Convenience wrapper for allocation failures.

### `FIO_ASSERT_DEBUG`

```c
#define FIO_ASSERT_DEBUG(cond, ...)
```

Active only when `DEBUG` is defined. On failure it behaves like `FIO_ASSERT`, printing the file and line number before terminating.

### `FIO_ASSERT_STATIC`

```c
#define FIO_ASSERT_STATIC(cond, msg)
```

Compile-time assertion. `cond` must be a constant expression; `msg` is a string literal with no format specifiers. Falls back to a sized-array trick on pre-C11 compilers.

See also: [Compiler Attributes](./001 compiler attributes.md).

## Leak Counter Helpers

Enabled by `FIO_LEAK_COUNTER` (default `1`). `FIO_NO_LOG` and `FIO_LEAK_COUNTER` are mutually exclusive because leak reports are printed through the log.

### `FIO_LEAK_COUNTER_DEF`

```c
#define FIO_LEAK_COUNTER_DEF(name)
```

Defines a named counter and a cleanup function that reports remaining allocations at process exit.

### `FIO_LEAK_COUNTER_ON_ALLOC`

```c
#define FIO_LEAK_COUNTER_ON_ALLOC(name)
```

Increment the named counter. Call after a successful allocation.

### `FIO_LEAK_COUNTER_ON_FREE`

```c
#define FIO_LEAK_COUNTER_ON_FREE(name)
```

Decrement the named counter. Call when memory is freed. Detects double-free.

### `FIO_LEAK_COUNTER_COUNT`

```c
#define FIO_LEAK_COUNTER_COUNT(name)
```

Returns the current value of the named counter.

### `FIO_LEAK_COUNTER_SKIP_EXIT`

```c
#define FIO_LEAK_COUNTER_SKIP_EXIT 0
```

Set to `1` to prevent automatic leak reporting at exit.

## Example

```c
#define FIO_LOG
#include "fio-stl.h"

int main(void) {
  FIO_LOG_LEVEL_SET(FIO_LOG_LEVEL_WARNING);

  FIO_LOG_INFO("not printed");
  FIO_LOG_WARNING("number invalid: %d", 42);

  FIO_LOG2STDERR("direct message");

  void *ptr = malloc(100);
  FIO_ASSERT_ALLOC(ptr);
  FIO_ASSERT(ptr != NULL, "expected a pointer");

  free(ptr);
  return 0;
}
```
# Core Math Helpers

Low-level integer arithmetic, prime testing, and multi-precision primitives defined in [`./000 core.h`](./000%20core.h).

These functions work on plain `uint64_t` arrays. The higher-level division, shift, and inversion helpers live in [`./002 math.h`](./002%20math.h), and the wide-union wrappers are documented in [`./001 vector math.md`](./001%20vector%20math.md).

## Hash primes

Four families of pre-selected prime constants, indexed `0` through `31`:

```c
FIO_U8_HASH_PRIME0  ... FIO_U8_HASH_PRIME31
FIO_U16_HASH_PRIME0 ... FIO_U16_HASH_PRIME31
FIO_U32_HASH_PRIME0 ... FIO_U32_HASH_PRIME31
FIO_U64_HASH_PRIME0 ... FIO_U64_HASH_PRIME31
```

Each fits in the named unsigned type and has about half of its bits set (`3`–`5` bits for the 8-bit primes). They are useful as hash mixers and for picking small prime bases.

## Modular arithmetic

```c
uint64_t fio_math_mod_mul64(uint64_t a, uint64_t b, uint64_t mod);
```

Computes `(a * b) % mod` without overflowing 64 bits. Uses `__uint128_t` when available; otherwise falls back to a double-and-add loop.

```c
uint64_t fio_math_mod_expo64(uint64_t base, uint64_t exp, uint64_t mod);
```

Right-to-left binary modular exponentiation. Calls `fio_math_mod_mul64` for each set bit of `exp`.

## Primality tests

```c
bool fio_math_is_uprime(uint64_t n);
bool fio_math_is_iprime(int64_t n);
```

`fio_math_is_uprime` tests whether `n` is probably prime. `fio_math_is_iprime` tests the absolute value of `n`.

For `n < FIO_PRIME_TABLE_LIMIT` (default `1024`) the test is deterministic using a small sieve. Larger values use Miller–Rabin with `10` rounds. Internal helpers `fio___is_prime_table` and `fio___is_prime_maybe` implement the two paths.

## 64-bit add / sub / mul with carry

```c
uint64_t fio_math_addc64(uint64_t a, uint64_t b,
                         uint64_t carry_in, uint64_t *carry_out);

uint64_t fio_math_subc64(uint64_t a, uint64_t b,
                         uint64_t borrow_in, uint64_t *borrow_out);

uint64_t fio_math_mulc64(uint64_t a, uint64_t b,
                         uint64_t *carry_out);
```

Return the low 64 bits of `a + b + carry_in`, `a - b - borrow_in`, or `a * b`; the outgoing carry/borrow/high word is written to `*carry_out`. They use compiler builtins where available and fall back to portable word arithmetic.

## Multi-precision add / sub / mul

All multi-precision routines treat arrays as little-endian `uint64_t` words: index `0` holds the least-significant word. Inputs are expected to have the same `len` (pad shorter inputs with zero).

```c
bool     fio_math_add(uint64_t *dest,
                      const uint64_t *a,
                      const uint64_t *b,
                      size_t len);

uint64_t fio_math_sub(uint64_t *dest,
                      const uint64_t *a,
                      const uint64_t *b,
                      size_t len);

void     fio_math_mul(uint64_t *restrict dest,
                      const uint64_t *a,
                      const uint64_t *b,
                      size_t len);
```

* `fio_math_add` writes `a + b` to `dest` and returns the final carry.
* `fio_math_sub` writes `a - b` to `dest` and returns the final borrow.
* `fio_math_mul` writes the full `len * 2` word product of `a` and `b` to `dest`.

When `__uint128_t` is available, `fio_math_addc128` and `fio_math_add2` provide the same carry-style addition for 128-bit words.

## Internal multiplication helpers

`fio_math_mul` dispatches to specialized helpers depending on `len`:

* `fio___math_mul_long` — optimized schoolbook multiplication, loop-unrolled, with a `__uint128_t` fast path.
* `fio___math_mul_256` — fully unrolled 4-word (256-bit) multiply.
* `fio___math_mul_512` — semi-unrolled 8-word (512-bit) multiply.
* `fio___math_mul_1024` — semi-unrolled 16-word (1024-bit) multiply, mostly used as a fallback.

For large numbers:

```c
#define FIO___MATH_KARATSUBA_THRESHOLD 32
```

When `len` is at least this threshold and even, `fio_math_mul` calls `fio___math_mul_karatsuba`.

```c
void fio___math_mul_karatsuba(uint64_t *restrict dest,
                              const uint64_t *a,
                              const uint64_t *b,
                              size_t len);
```

`fio___math_mul_karatsuba` splits `a` and `b` into low/high halves, computes `z0`, `z1`, and `z2`, then combines them as `z2*B^2 + z1*B + z0`. It uses `fio___math_mul_norecurse` for the three half-sized sub-products and allocates scratch buffers on the stack. `len` must be even; on MSVC or pre-C++14 the stack buffers are fixed at 256 words, so inputs are limited accordingly.

## See also

* [`./000 core.h`](./000%20core.h) — source of truth.
* [`./002 math.h`](./002%20math.h) — higher-level division, shift, and inversion helpers.
* [`./001 vector math.md`](./001%20vector%20math.md) — wide-union `fio_uXXX` arithmetic built on these primitives.
# Memory Alternatives and OS Patches

Defines portable fallback memory routines and small OS compatibility patches. Implemented in [`./001 memalt.h`](./001%20memalt.h) and [`./001 patches.h`](./001%20patches.h); wired into the selector macros in [`./000 core.h`](./000%20core.h).

## When `FIO_MEMALT` is used

Define `FIO_MEMALT` before including the STL to substitute standard-library memory and string calls with facil.io fallbacks:

```c
#define FIO_MEMALT
#include "fio-stl/include.h"
```

This is useful for freestanding or restricted environments, or when you want every memory call to go through the same portable implementation. The fallbacks are correct but usually slower than a good libc, so the default is to use compiler builtins or libc instead.

## Selector wiring

When `FIO_MEMALT` is defined, [`./000 core.h`](./000%20core.h) routes any unset selector to the matching fallback:

| Selector | Fallback |
|----------|----------|
| `FIO_MEMCPY`  | `fio_memcpy` |
| `FIO_MEMMOVE` | `fio_memcpy` |
| `FIO_MEMCMP`  | `fio_memcmp` |
| `FIO_MEMCHR`  | `fio_memchr` |
| `FIO_MEMSET`  | `fio_memset` |
| `FIO_STRLEN`  | `fio_strlen` |

If `FIO_MEMALT` is not defined, the selectors default to `__builtin_*` when available, otherwise to the standard `memcpy`, `memset`, `memchr`, `memcmp`, and `strlen`. You can also override any selector directly without enabling `FIO_MEMALT`.

## Fallback memory routines

All routines are declared in [`./001 memalt.h`](./001%20memalt.h).

### `fio_memcpy`

```c
void *fio_memcpy(void *dest, const void *src, size_t bytes);
```

Copies `bytes` from `src` to `dest`. Handles overlapping regions like `memmove`. Returns `dest` immediately if `dest == src`, `bytes` is zero, or either pointer is `NULL`.

For small, non-overlapping copies (≤64 bytes) it uses overlapping fixed-width copies. For larger or overlapping regions it uses a 64-byte buffered copy, choosing forward or backward direction as needed.

### `fio_memset`

```c
void *fio_memset(void *restrict dest, uint64_t data, size_t bytes);
```

Fills `bytes` at `dest` with the 8-byte pattern `data`. If `data` is below `0x100`, it is broadcast to every byte to match classic `memset` behavior. Returns `dest`, or `NULL` if `dest` is `NULL`.

### `fio_memchr`

```c
void *fio_memchr(const void *buffer, const char token, size_t len);
```

Finds the first byte equal to `token` in the first `len` bytes of `buffer`. Returns a pointer to the match, or `NULL` if not found, `buffer` is `NULL`, or `len` is zero.

Picks the best available implementation at compile time:

- AVX2 on x86-64 with intrinsics
- SSE2 on x86 with intrinsics
- NEON on ARM with intrinsics
- Scalar 128-bit block fallback otherwise

### `fio_memcmp`

```c
int fio_memcmp(const void *a, const void *b, size_t len);
```

Compares the first `len` bytes of `a` and `b`. Returns `1` if `a > b`, `-1` if `a < b`, and `0` if equal. Returns `0` immediately if `a == b` or `len` is zero.

Processes 64-byte blocks for large buffers, 8-byte blocks for medium buffers, and byte-by-byte for the tail.

### `fio_strlen`

```c
size_t fio_strlen(const char *str);
```

Returns the length of the NUL-terminated string `str`, or `0` if `str` is `NULL`. The implementation is a simple byte loop the compiler can auto-vectorize. Building with Address Sanitizer may flag speculative reads.

## OS patch environment helpers

[`./001 patches.h`](./001%20patches.h) provides `fio_sys_env` and `fio_sys_env_set` as portable replacements for `getenv` and `setenv`.

```c
char *fio_sys_env(const char *name);
int   fio_sys_env_set(const char *name, const char *value, int overwrite);
```

- `fio_sys_env(name)` returns the value of the environment variable, or `NULL`.
- `fio_sys_env_set(name, value, overwrite)` sets the variable. If `overwrite` is zero and the variable already exists, it is left unchanged. Returns `0` on success, `-1` on failure.

On POSIX these are thin wrappers around `getenv`/`setenv`. On Windows they call `GetEnvironmentVariableA` and `SetEnvironmentVariableA` directly, bypassing macro redefinitions such as the one in Ruby's `win32.h` and avoiding `wchar_t` conversion issues.

## Other OS patches

[`./001 patches.h`](./001%20patches.h) also fills small portability gaps:

- **macOS < 10.12**: defines a `clock_gettime` fallback using `gettimeofday`, plus `CLOCK_REALTIME` and `CLOCK_MONOTONIC` placeholders.
- **Windows startup**: enables virtual terminal processing for stdout/stderr and sets binary file mode by default.
- **Windows symbol gaps**: supplies inline patches for `strcasecmp`, `write`, `read`, `clock_gettime`, and `pwrite`; maps `O_*`/`S_*` constants, `fstat`/`stat`/`lseek`/`unlink`, and defines `PATH_MAX`.
- **POSIX branch**: present but currently has no extra patches beyond the environment helpers.
# Memory Primitives

Low-level memory helpers used everywhere else in the STL. Most are defined in [`./000 core.h`](./000%20core.h); the allocator abstraction macros live in [`./001 header.h`](./001%20header.h).

## Memory-Function Selector Macros

These macros route standard-library-style calls to either compiler builtins or libc functions. Define any of them before inclusion to override the default.

| Macro | Default | Use |
|-------|---------|-----|
| `FIO_MEMCPY(dest, src, n)` | `__builtin_memcpy` if available, else `memcpy` | Variable-length copy. |
| `FIO_MEMMOVE(dest, src, n)` | `__builtin_memmove` if available, else `memmove` | Overlap-safe copy. |
| `FIO_MEMSET(dest, c, n)` | `__builtin_memset` if available, else `memset` | Fill memory. |
| `FIO_MEMCHR(ptr, c, n)` | `__builtin_memchr` if available, else `memchr` | Find byte. |
| `FIO_STRLEN(str)` | `__builtin_strlen` if available, else `strlen` | C string length. |
| `FIO_MEMCMP(a, b, n)` | `__builtin_memcmp` if available, else `memcmp` | Compare memory. |

If `FIO_MEMALT` is defined, any selector that was not already set is routed to the matching facil.io fallback:

```c
#define FIO_MEMCPY  fio_memcpy
#define FIO_MEMMOVE fio_memcpy
#define FIO_MEMCMP  fio_memcmp
#define FIO_MEMCHR  fio_memchr
#define FIO_MEMSET  fio_memset
#define FIO_STRLEN  fio_strlen
```

Those fallback routines are documented in `./001 memalt.h` (the memalt module).

## Fixed-Size Copy Helpers

These functions copy an exact number of bytes from `src` to `dest`. They are implemented with the fixed-size `FIO_MEMCPY` path when builtins are available, otherwise with a compact byte loop.

- When `__builtin_memcpy` is available, each helper returns `dest` (the `__builtin_memcpy` return value).
- In the non-builtin fallback, each helper returns `dest + n`.

`fio_memcpy0` always returns `dest`.

```c
void *fio_memcpy0 (void *restrict dest, const void *restrict src);
void *fio_memcpy1 (void *restrict dest, const void *restrict src);
void *fio_memcpy2 (void *restrict dest, const void *restrict src);
void *fio_memcpy3 (void *restrict dest, const void *restrict src);
void *fio_memcpy4 (void *restrict dest, const void *restrict src);
void *fio_memcpy5 (void *restrict dest, const void *restrict src);
void *fio_memcpy6 (void *restrict dest, const void *restrict src);
void *fio_memcpy7 (void *restrict dest, const void *restrict src);
void *fio_memcpy8 (void *restrict dest, const void *restrict src);
void *fio_memcpy16(void *restrict dest, const void *restrict src);
void *fio_memcpy32(void *restrict dest, const void *restrict src);
void *fio_memcpy64(void *restrict dest, const void *restrict src);
void *fio_memcpy128(void *restrict dest, const void *restrict src);
void *fio_memcpy256(void *restrict dest, const void *restrict src);
void *fio_memcpy512(void *restrict dest, const void *restrict src);
void *fio_memcpy1024(void *restrict dest, const void *restrict src);
void *fio_memcpy2048(void *restrict dest, const void *restrict src);
void *fio_memcpy4096(void *restrict dest, const void *restrict src);
```

### Tail helpers

`fio_memcpyNx` copies `len & N` bytes — useful for handling the tail end of a larger copy without explicit branches.

```c
void *fio_memcpy0x  (void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy7x  (void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy15x (void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy31x (void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy63x (void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy127x(void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy255x(void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy511x(void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy1023x(void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy2047x(void *restrict dest, const void *restrict src, size_t len);
void *fio_memcpy4095x(void *restrict dest, const void *restrict src, size_t len);
```

### Internal unsafe helpers

Two internal helpers back the tail functions. They assume the regions do not overlap and perform no validation.

```c
void *fio___memcpy_unsafe_63x(void *restrict dest, const void *restrict src, size_t len);
void *fio___memcpy_unsafe_x (void *restrict dest, const void *restrict src, size_t len);
```

- `fio___memcpy_unsafe_63x` is optimized for lengths up to 63 bytes.
- `fio___memcpy_unsafe_x` handles arbitrary lengths, copying 128-byte blocks (or 256-byte blocks when `FIO_LIMIT_INTRINSIC_BUFFER` is `0`) and finishing with a 64-byte tail.

These are `static`/`static inline` helpers; do not use them directly unless you are implementing a higher-level routine and have already checked the inputs.

## Unaligned Buffer Accessors

Read and write integers from byte buffers that may not be naturally aligned. The suffix controls byte order:

- `u` — native (unspecified) byte order.
- `_le` — little-endian.
- `_be` — big-endian / network byte order.

```c
uint8_t  fio_buf2u8u (const void *buf);
uint8_t  fio_buf2u8_le(const void *buf);
uint8_t  fio_buf2u8_be(const void *buf);
void     fio_u2buf8u (void *buf, uint8_t  v);
void     fio_u2buf8_le(void *buf, uint8_t  v);
void     fio_u2buf8_be(void *buf, uint8_t  v);

uint16_t fio_buf2u16u (const void *buf);
uint16_t fio_buf2u16_le(const void *buf);
uint16_t fio_buf2u16_be(const void *buf);
void     fio_u2buf16u (void *buf, uint16_t v);
void     fio_u2buf16_le(void *buf, uint16_t v);
void     fio_u2buf16_be(void *buf, uint16_t v);

uint32_t fio_buf2u24u (const void *buf);
uint32_t fio_buf2u24_le(const void *buf);
uint32_t fio_buf2u24_be(const void *buf);
void     fio_u2buf24u (void *buf, uint32_t v);
void     fio_u2buf24_le(void *buf, uint32_t v);
void     fio_u2buf24_be(void *buf, uint32_t v);

uint32_t fio_buf2u32u (const void *buf);
uint32_t fio_buf2u32_le(const void *buf);
uint32_t fio_buf2u32_be(const void *buf);
void     fio_u2buf32u (void *buf, uint32_t v);
void     fio_u2buf32_le(void *buf, uint32_t v);
void     fio_u2buf32_be(void *buf, uint32_t v);

uint64_t fio_buf2u64u (const void *buf);
uint64_t fio_buf2u64_le(const void *buf);
uint64_t fio_buf2u64_be(const void *buf);
void     fio_u2buf64u (void *buf, uint64_t v);
void     fio_u2buf64_le(void *buf, uint64_t v);
void     fio_u2buf64_be(void *buf, uint64_t v);

size_t   fio_buf2zu(const void *buf);
void     fio_u2bufzu(void *buf, size_t v);
```

`fio_buf2u24u` / `fio_u2buf24u` and `fio_buf2zu` / `fio_u2bufzu` select the correct implementation at compile time based on host endianness and `size_t` width.

Example:

```c
uint8_t packet[8] = {0};
fio_u2buf32_be(packet + 2, 0x12345678);
uint32_t n = fio_buf2u32_be(packet + 2);  // 0x12345678
```

## Memory Allocator Abstraction

The STL types use these macros instead of calling `malloc`/`free` directly, so a custom allocator can be plugged in by defining them before inclusion. They are reset and redefined in [`./001 header.h`](./001%20header.h) for each inclusion cycle.

```c
#define FIO_MEM_REALLOC(ptr, old_size, new_size, copy_len)  realloc((ptr), (new_size))
#define FIO_MEM_FREE(ptr, size)                             free((ptr))
#define FIO_MEM_ALIGNMENT_SIZE                              sizeof(long double)
#define FIO_MEM_REALLOC_IS_SAFE                             0
```

- `FIO_MEM_REALLOC` — reallocates `ptr` from `old_size` to `new_size`, copying at least `copy_len` bytes if the object moves. `NULL` `ptr` acts like `malloc`; `new_size == 0` acts like `free`.
- `FIO_MEM_FREE` — frees memory allocated with `FIO_MEM_REALLOC`.
- `FIO_MEM_ALIGNMENT_SIZE` — the alignment the allocator guarantees.
- `FIO_MEM_REALLOC_IS_SAFE` — non-zero when the allocator zero-initializes returned memory.

If the custom `fio_malloc` allocator has already been included (`H___FIO_MALLOC___H` is defined), the defaults are routed to it instead:

```c
#define FIO_MEM_REALLOC(ptr, old_size, new_size, copy_len)  fio_realloc2((ptr), (new_size), (copy_len))
#define FIO_MEM_FREE(ptr, size)                             fio_free((ptr))
#define FIO_MEM_ALIGNMENT_SIZE                              fio_malloc_alignment()
#define FIO_MEM_REALLOC_IS_SAFE                             fio_realloc_is_safe()
```

Define `FIO_MEM_RESET` before inclusion to force these defaults to be re-evaluated. Define `FIO_MEMORY_DISABLE` or `FIO_MALLOC_TMP_USE_SYSTEM` to force the system allocator for an inclusion cycle.

### Recursive variants

The underscore-suffixed macros exist for use during recursive `#include` cycles:

```c
FIO_MEM_REALLOC_
FIO_MEM_FREE_
FIO_MEM_ALIGNMENT_SIZE_
FIO_MEM_REALLOC_IS_SAFE_
```

They normally mirror the non-underscore versions. When `FIO_MALLOC_TMP_USE_SYSTEM` is set they are forced to the system `realloc`/`free` for that cycle only, while the non-underscore macros keep their outer meaning.

## Unaligned Memory Access Knob

```c
#define FIO_UNALIGNED_MEMORY_ACCESS_ENABLED 1
```

Defined in [`./000 core.h`](./000%20core.h). It is auto-detected: if `FIO_UNALIGNED_ACCESS` is `1` (the default) and the target CPU is known to tolerate unaligned access — x86, x86_64, ARM64, or another architecture that defines `__ARM_FEATURE_UNALIGNED` — then `FIO_UNALIGNED_MEMORY_ACCESS_ENABLED` becomes `1`. Otherwise it is `0`.

Set `FIO_UNALIGNED_ACCESS` to `0` before inclusion to disable the detection attempt, or define `FIO_UNALIGNED_MEMORY_ACCESS_ENABLED` directly to override the result.

## See Also

- `./001 memalt.h` (the memalt module) — variable-length fallback memory routines (`fio_memcpy`, `fio_memset`, `fio_memcmp`, `fio_memchr`, `fio_strlen`).
- [`./001 endian.md`](./001%20endian.md) — byte-order detection and swap helpers.
- [`./000 core.h`](./000%20core.h) — source of truth for copy helpers and the unaligned-access knob.
- [`./001 header.h`](./001%20header.h) — source of truth for the allocator macros.
# OS Detection

[`./000 core.h`](./000%20core.h) detects the host OS and defines a few portable helpers so the rest of the library can use one code path per platform.

## Platform macros

| Macro | POSIX value | Windows value | Notes |
|---|---|---|---|
| `FIO_OS_POSIX` | `1` | `0` | Set on Linux, macOS, BSD, and other `__unix__`/`__linux__`/`__APPLE__` systems. |
| `FIO_OS_WIN` | `0` | `1` | Set on Windows, including MinGW, MSYS2, Cygwin, and Borland. |

The Windows check is evaluated first, because MinGW/MSYS2 define both `_WIN32` and `__unix__`. Treating the system as Windows avoids calling POSIX socket functions on Winsock `SOCKET` handles.

After detection, `FIO_OS_WIN` always wins over `FIO_OS_POSIX`:

```c
#if FIO_OS_WIN
#undef FIO_OS_POSIX
#define FIO_OS_POSIX 0
#endif
```

## Unix-tools level

`FIO_HAVE_UNIX_TOOLS` tells the library how Unix-like the environment is:

- `0` — no Unix tools (pure MSVC).
- `1` — full POSIX.
- `2` — MinGW.
- `3` — Cygwin.

It is used to decide whether functions such as `pipe()`, `fcntl()`, and POSIX signal APIs are available.

## Process helpers

| Macro | POSIX | Windows |
|---|---|---|
| `fio_getpid` | `getpid` | `_getpid` |
| `FIO_KILL_SELF()` | `kill(0, SIGINT)` | `GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)` |

`FIO_KILL_SELF` is guarded by `#ifndef`, so you can define your own version before including the header. The default implementation is used by the assertion/debug machinery to deliver a signal to the running process.

## Thread sleep and yield

| Macro | POSIX | Windows |
|---|---|---|
| `FIO_THREAD_WAIT(nano_sec)` | `nanosleep` | `Sleep` |
| `FIO_THREAD_YIELD()` | see below | see below |
| `FIO_THREAD_RESCHEDULE()` | `FIO_THREAD_WAIT(4)` | `FIO_THREAD_WAIT(4)` |

`FIO_THREAD_WAIT(nano_sec)` sleeps the current thread for the requested number of nanoseconds. On POSIX it builds a `struct timespec` and calls `nanosleep`. On Windows it converts the interval to milliseconds and calls `Sleep`; any non-zero request is rounded up to at least 1 ms.

`FIO_THREAD_YIELD()` hints the processor that the thread is in a spin loop. On x86/x86-64 with GCC or Clang it emits a `pause` instruction; on ARM/AArch64 with GCC or Clang it emits a `yield` instruction; with MSVC it calls `YieldProcessor()`; otherwise it falls back to `sched_yield()` on POSIX.

`FIO_THREAD_RESCHEDULE()` yields the thread by sleeping for 4 nanoseconds (`FIO_THREAD_WAIT(4)`). It is used by the lock code to back off busy waits without fully suspending the thread.

## Portable type patches

On pure MSVC `ssize_t` is missing, so the core header defines it from `SSIZE_T`:

```c
typedef SSIZE_T ssize_t;
```

`SSIZE_MAX` and `SSIZE_MIN` are also provided if the system headers do not:

```c
#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)((~(size_t)0) >> 1))
#endif
#ifndef SSIZE_MIN
#define SSIZE_MIN ((ssize_t)(~((~(size_t)0) >> 1)))
#endif
```

## `printf`-style format checking

The `FIO___PRINTF_STYLE(string_index, check_index)` macro is defined differently on each toolchain so the compiler can check `printf`-style arguments. MinGW/Cygwin use the MinGW format attribute, pure MSVC leaves it empty, and POSIX/GCC/Clang use the standard `printf` format attribute. It is documented in [`./001 compiler attributes.md`](./001%20compiler%20attributes.md).
# Core Random PRNG

An optionally-deterministic 128-bit pseudo-random number generator defined by a macro. It is a low-level building block for hash functions, test harnesses, and other internals that need an optionally repeatable, fast stream of bits.

For the high-level random module — `fio_rand64`, secure bytes, hashing, etc. — see [`./002 random.h`](./002%20random.h).

The implementation is in [`./000 core.h`](./000%20core.h).

## `fio_cycle_counter`

```c
uint64_t fio_cycle_counter(void);
```

Returns a hardware cycle counter when one is available, or `0` otherwise.

- On x86/x86_64 it reads `rdtsc`.
- On AArch64 it reads `cntvct_el0`.
- On other platforms it returns `0`.

In non-deterministic mode, the PRNG uses this for timing-based jitter during reseeding.

## `FIO_DEFINE_RANDOM128_FN`

```c
#define FIO_DEFINE_RANDOM128_FN(extern, name, reseed_log, seed_offset)
```

Defines a named PRNG instance and its API. The most common deterministic form looks like:

```c
FIO_DEFINE_RANDOM128_FN(static, my_rng, 0, 0x1234)
```

Parameters:

- `extern` — linkage prefix, e.g. `static` or `FIO_SFUNC`.
- `name` — base name for all generated functions and internal state.
- `reseed_log` — automatic reseed interval, `2^reseed_log` calls. Set to `0` for a fully deterministic stream.
- `seed_offset` — arbitrary offset added to the default seed constants.

### Non-Deterministic Mode

```c
FIO_DEFINE_RANDOM128_FN(static, my_rng, 5, 0x1234)
```

When `reseed_log` is non-zero (and less than `64`), the generator calls `name_reseed()` automatically every `2^reseed_log` calls, mixing in timing data and the cycle counter. This makes the stream non-deterministic, and improves entropy.

In this example, reseeding is set to `5`, resulting in reseeding every 32 calls (collecting new entropy every 4,096 bytes).

## Generated API

For a definition `FIO_DEFINE_RANDOM128_FN(static, my_rng, 0, 0)` the following functions are generated:

### `my_rng128`

```c
fio_u128 my_rng128(void);
```

Returns 128 pseudo-random bits. The return type is [`fio_u128`](./001%20vector%20math.md), so the result can also be read through `.u64[0]`, `.u64[1]`, `.u8`, etc.

### `my_rng64`

```c
uint64_t my_rng64(void);
```

Returns 64 pseudo-random bits. Each call consumes half of a 128-bit result; a new 128-bit value is generated only every other call.

### `my_rng_bytes`

```c
void my_rng_bytes(void *dest, size_t len);
```

Fills `dest` with `len` pseudo-random bytes. Does nothing if `dest` is `NULL` or `len` is zero.

### `my_rng_reset`

```c
void my_rng_reset(void);
```

Resets the generator to its initial seeded state.

### `my_rng_reseed`

```c
void my_rng_reseed(void);
```

Re-seeds the generator using `CLOCK_MONOTONIC`, address/bits of local state, and `fio_cycle_counter()`. Useful after `fork()` or when you want to de-correlate separate processes.

### `my_rng_on_fork`

```c
void my_rng_on_fork(void *is_null);
```

Calls `my_rng_reseed()`. Fork-handler helper that takes an ignored `void *` argument.

## Example

```c
FIO_DEFINE_RANDOM128_FN(static, my_rng, 0, 0)

void roll_die(int n) {
  my_rng_reset();
  for (int i = 0; i < n; ++i) {
    uint64_t r = my_rng64();
    printf("roll %d: %llu\n", i, (unsigned long long)(r % 6 + 1));
  }
}
```

Because `reseed_log` is `0`, the same seed always produces the same sequence.

## Determinism and fork safety

- Set `reseed_log = 0` for deterministic, repeatable output.
- Set `reseed_log` to a practical value such as `31` for a non-deterministic stream that reseeds automatically.
- After `fork()`, call `name_reseed()` or register a wrapper that calls `name_on_fork()` with `pthread_atfork()` to avoid duplicated sequences in child processes.

## Testing

Test results for this generator are recorded in [`./721 rand128 macro.md`](./721%20rand128%20macro.md).

## See also

- [`./002 random.h`](./002%20random.h) — high-level random/hashing module.
- [`./001 vector math.md`](./001%20vector%20math.md) — the `fio_u128` return type.
- [`./000 core.h`](./000%20core.h) — source of truth.
# Fake x86 SIMD

```c
#define FIO_FAKE_X86 /* or FIO_FAKE_X86_SHADOW */
#include "fio-stl.h"
```

Portable x86 SIMD intrinsics for code that wants the x86 spell book even when the CPU does not. The implementation lives in [`./001 fx86.h`](./001%20fx86.h) and uses the core wide unions from [`./000 core.h`](./000%20core.h), especially `fio_u128` and `fio_u256`.

The module is compiled only when `FIO_FAKE_X86` or `FIO_FAKE_X86_SHADOW` is defined before including the STL.

### Configuration Macros

#### `FIO_FAKE_X86`

```c
#define FIO_FAKE_X86
```

Enables the namespaced fake-intrinsic API: `fio_fx86_*` and `fio_fx86_256_*`.

On real x86 with `FIO___HAS_X86_INTRIN`, supported functions pass through to native intrinsics when the matching feature macro exists (`__SSE2__`, `__AES__`, `__AVX2__`, and friends). Otherwise the fallback code copies through `fio_u128` / `fio_u256` and performs lane-wise C operations.

#### `FIO_FAKE_X86_SHADOW`

```c
#define FIO_FAKE_X86_SHADOW
```

Enables the same API as `FIO_FAKE_X86`, then shadows real intrinsic names with macros:

```c
#define __m128i fio_fx86_m128i
#define __m256i fio_fx86_m256i
#define _mm_xor_si128(a, b) fio_fx86_xor_si128((a), (b))
#define _mm256_xor_si256(a, b) fio_fx86_256_xor_si256((a), (b))
```

Shadow mode is meant for compiling x86 code paths on non-x86 machines. It also undefines ARM intrinsic guard macros such as `FIO___HAS_ARM_INTRIN`, `FIO___HAS_ARM_SHA3_INTRIN`, `__ARM_NEON`, and `__ARM_FEATURE_CRYPTO`, then defines the x86 feature guards used by downstream code (`FIO___HAS_X86_INTRIN`, `__SSE2__`, `__SSSE3__`, `__SSE4_1__`, `__AES__`, `__PCLMUL__`, `__SHA__`, `__SHA512__`, `__AVX2__`, etc.).

Shadowed names are macros. Keep arguments simple and avoid depending on compiler-specific intrinsic types outside the code path being shadowed.

#### `FIO___HAS_X86_INTRIN`

```c
#define FIO___HAS_X86_INTRIN 1
#include <immintrin.h>
```

Core detection flag from `000 core.h`. It is defined on x86_64 when intrinsics are allowed and `immintrin.h` is available.

Intrinsic detection is disabled when `DEBUG` or `NO_INTRIN` is defined.

#### `FIO___HAS_ARM_INTRIN`

```c
#define FIO___HAS_ARM_INTRIN 1
#include <arm_acle.h>
#include <arm_neon.h>
```

Core detection flag from `000 core.h`. It is defined when ARM crypto + NEON headers are available. `FIO_FAKE_X86_SHADOW` undefines it so fake x86 code paths can be selected instead of ARM paths.

#### `FIO_FOR_UNROLL_GROUP_SIZE`

```c
#define FIO_FOR_UNROLL_GROUP_SIZE ((size_t)256U)
```

The byte stride used by `FIO_FOR_UNROLL` for compiler-friendly batches.

Most CPUs should be able to hold at least 512 bytes in their registers. The default value assumes half that is available for the optimized loop, as the action within each loop will likely require some available registers.

#### `FIO_FOR_UNROLL`

```c
#define FIO_FOR_UNROLL(iterations, size_of_loop, i, action)                    \
  do {                                                                         \
    size_t i = 0;                                                              \
    const size_t fio___unroll_remainder__ =                                    \
        ((iterations) & ((FIO_FOR_UNROLL_GROUP_SIZE / (size_of_loop)) - 1));            \
    if (fio___unroll_remainder__)                                              \
      for (; i < fio___unroll_remainder__; ++i)                                \
        action;                                                                \
    if (iterations)                                                            \
      for (; !((iterations) + 1) || (i < (iterations));)                       \
        for (size_t j__loop__ = 0;                                             \
             j__loop__ < (FIO_FOR_UNROLL_GROUP_SIZE / (size_of_loop));                  \
             ++j__loop__, ++i)                                                 \
          action;                                                              \
  } while (0)
```

Runs a remainder loop, then fixed 256-byte batches. The macro declares `size_t i` inside the expansion; `action` may use that name.

#### `_MM_HINT_T0`, `_MM_HINT_T1`, `_MM_HINT_T2`, `_MM_HINT_NTA`

```c
#define _MM_HINT_T0  3
#define _MM_HINT_T1  2
#define _MM_HINT_T2  1
#define _MM_HINT_NTA 0
```

Defined in fake mode when native x86 types are not present, so `fio_fx86_prefetch` and shadowed `_mm_prefetch` calls can compile.

### Types

#### `fio_u128`

```c
typedef union fio_u128 {
  FIO___UXXX_UGRP_DEF(128);
#if defined(__SIZEOF_INT128__)
  __uint128_t alignment_for_u128_[1];
#endif
} fio_u128 FIO_ALIGN(16);
```

The 128-bit storage union used by the fallback implementation. It exposes unsigned, signed, floating-point, and vector views (`u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, `x8`, `x16`, `x32`, `x64`, etc.).

#### `fio_u256`

```c
typedef union fio_u256 {
  fio_u128 u128[2];
  FIO___UXXX_UGRP_DEF(256);
#if defined(__SIZEOF_INT256__)
  __uint256_t alignment_for_u256_[1];
#endif
} fio_u256 FIO_ALIGN(32);
```

The 256-bit storage union used by AVX2 and SHA-512 fallbacks. It includes all `fio_u128`-style views plus `u128[2]`.

#### `FIO_UXXX_X64_DEF`, `FIO_UXXX_X32_DEF`, `FIO_UXXX_X16_DEF`, `FIO_UXXX_X8_DEF`

```c
#define FIO_UXXX_X64_DEF(name, bits) /* platform-specific */
#define FIO_UXXX_X32_DEF(name, bits) /* platform-specific */
#define FIO_UXXX_X16_DEF(name, bits) /* platform-specific */
#define FIO_UXXX_X8_DEF(name, bits)  /* platform-specific */
```

Core vector-member definition macros. On ARM they use NEON vector arrays. On GCC/Clang with `vector_size`, they use a single full-width vector. Otherwise they are plain C arrays. The fake x86 API itself uses `fio_u128` / `fio_u256` conversions instead of assuming a specific vector layout.

#### `fio_fx86_m128i`

```c
typedef __m128i fio_fx86_m128i;
/* or */
typedef fio_u128 fio_fx86_m128i;
```

The canonical 128-bit integer SIMD type for this module. It is native `__m128i` when available, otherwise `fio_u128`.

#### `fio_fx86_m256i`

```c
typedef __m256i fio_fx86_m256i;
/* or */
typedef fio_u256 fio_fx86_m256i;
```

The canonical 256-bit integer SIMD type for this module. It is native `__m256i` when available, otherwise `fio_u256`.

#### `FIO___FX86_HAS_NATIVE_M128I`, `FIO___FX86_HAS_NATIVE_M256I`, `FIO___FX86_HAS_NATIVE_TYPES`

```c
#define FIO___FX86_HAS_NATIVE_M128I 1 /* or 0 */
#define FIO___FX86_HAS_NATIVE_M256I 1 /* or 0 */
#define FIO___FX86_HAS_NATIVE_TYPES                                            \
  (FIO___FX86_HAS_NATIVE_M128I && FIO___FX86_HAS_NATIVE_M256I)
```

Internal feature flags used to choose native x86 types vs. `fio_uXXX` unions.

### API Functions

#### `fio___fx86_to_u128`

```c
FIO_IFUNC fio_u128 fio___fx86_to_u128(fio_fx86_m128i v);
```

Copies a `fio_fx86_m128i` into a `fio_u128` using `fio_memcpy16`. Use this when you need portable lane access and the type might be native `__m128i`.

#### `fio___fx86_from_u128`

```c
FIO_IFUNC fio_fx86_m128i fio___fx86_from_u128(fio_u128 u);
```

Copies a `fio_u128` into the canonical 128-bit fake x86 type.

#### `fio___fx86_to_u256`

```c
FIO_IFUNC fio_u256 fio___fx86_to_u256(fio_fx86_m256i v);
```

Copies a `fio_fx86_m256i` into a `fio_u256` using `fio_memcpy32`.

#### `fio___fx86_from_u256`

```c
FIO_IFUNC fio_fx86_m256i fio___fx86_from_u256(fio_u256 u);
```

Copies a `fio_u256` into the canonical 256-bit fake x86 type.

#### `fio_fx86_loadu_si128`, `fio_fx86_load_si128`, `fio_fx86_storeu_si128`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_loadu_si128(const void *p);
FIO_IFUNC fio_fx86_m128i fio_fx86_load_si128(const void *p);
FIO_IFUNC void fio_fx86_storeu_si128(void *p, fio_fx86_m128i a);
```

SSE2-style 128-bit load/store helpers. The fallback copies 16 bytes. The aligned load name mirrors `_mm_load_si128`; the fallback does not check alignment.

#### `fio_fx86_add_epi32`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_add_epi32(fio_fx86_m128i a,
                                            fio_fx86_m128i b);
```

Adds four unsigned 32-bit lanes with normal wraparound.

#### `fio_fx86_xor_si128`, `fio_fx86_or_si128`, `fio_fx86_and_si128`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_xor_si128(fio_fx86_m128i a,
                                            fio_fx86_m128i b);
FIO_IFUNC fio_fx86_m128i fio_fx86_or_si128(fio_fx86_m128i a,
                                           fio_fx86_m128i b);
FIO_IFUNC fio_fx86_m128i fio_fx86_and_si128(fio_fx86_m128i a,
                                            fio_fx86_m128i b);
```

Bitwise operations over the whole 128-bit value.

#### `fio_fx86_slli_si128`, `fio_fx86_srli_si128`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_slli_si128(fio_fx86_m128i a, int imm8);
FIO_IFUNC fio_fx86_m128i fio_fx86_srli_si128(fio_fx86_m128i a, int imm8);
```

Shifts the whole 128-bit register by bytes, filling with zero. Values greater than or equal to 16 return zero in the fallback.

#### `fio_fx86_slli_epi32`, `fio_fx86_srli_epi32`, `fio_fx86_slli_epi64`, `fio_fx86_srli_epi64`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_slli_epi32(fio_fx86_m128i a, int imm8);
FIO_IFUNC fio_fx86_m128i fio_fx86_srli_epi32(fio_fx86_m128i a, int imm8);
FIO_IFUNC fio_fx86_m128i fio_fx86_slli_epi64(fio_fx86_m128i a, int imm8);
FIO_IFUNC fio_fx86_m128i fio_fx86_srli_epi64(fio_fx86_m128i a, int imm8);
```

Per-lane logical shifts for 32-bit and 64-bit lanes. Fallback shifts at or past the lane width produce zero.

#### `fio_fx86_shuffle_epi32`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_shuffle_epi32(fio_fx86_m128i a, int imm8);
```

Selects each output 32-bit lane from `a` using two bits per lane from `imm8`.

#### `fio_fx86_cmpeq_epi8`, `fio_fx86_movemask_epi8`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_cmpeq_epi8(fio_fx86_m128i a,
                                             fio_fx86_m128i b);
FIO_IFUNC int fio_fx86_movemask_epi8(fio_fx86_m128i a);
```

Byte equality produces `0xFF` or `0x00` per byte. The movemask packs the high bit of each byte into a 16-bit integer result.

#### `fio_fx86_set_epi32`, `fio_fx86_set_epi64x`, `fio_fx86_set_epi8`, `fio_fx86_set1_epi8`, `fio_fx86_setzero_si128`, `fio_fx86_cvtsi32_si128`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_set_epi32(int e3, int e2, int e1, int e0);
FIO_IFUNC fio_fx86_m128i fio_fx86_set_epi64x(int64_t e1, int64_t e0);
FIO_IFUNC fio_fx86_m128i fio_fx86_set_epi8(char e15, char e14, char e13,
                                           char e12, char e11, char e10,
                                           char e9, char e8, char e7, char e6,
                                           char e5, char e4, char e3, char e2,
                                           char e1, char e0);
FIO_IFUNC fio_fx86_m128i fio_fx86_set1_epi8(char a);
FIO_IFUNC fio_fx86_m128i fio_fx86_setzero_si128(void);
FIO_IFUNC fio_fx86_m128i fio_fx86_cvtsi32_si128(int a);
```

SSE2 constructors. The `set` functions follow x86 argument order: high lanes first, low lanes last.

#### `fio_fx86_shuffle_epi8`, `fio_fx86_alignr_epi8`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_shuffle_epi8(fio_fx86_m128i a,
                                               fio_fx86_m128i b);
FIO_IFUNC fio_fx86_m128i fio_fx86_alignr_epi8(fio_fx86_m128i a,
                                              fio_fx86_m128i b,
                                              int imm8);
```

SSSE3 helpers. `shuffle_epi8` performs byte lookup, zeroing output bytes whose control byte has bit 7 set. `alignr_epi8` concatenates `b:a`, shifts right by `imm8` bytes, and returns 16 bytes.

#### `fio_fx86_blend_epi16`, `fio_fx86_extract_epi32`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_blend_epi16(fio_fx86_m128i a,
                                              fio_fx86_m128i b,
                                              int imm8);
FIO_IFUNC int fio_fx86_extract_epi32(fio_fx86_m128i a, int imm8);
```

SSE4.1 helpers. `blend_epi16` selects each 16-bit lane from `a` or `b` by `imm8` bits. `extract_epi32` returns lane `imm8 & 3`.

#### `fio_fx86_prefetch`

```c
FIO_IFUNC void fio_fx86_prefetch(const void *p, int hint);
```

Prefetches on native SSE2. It is a no-op in fake mode.

#### `fio_fx86_aesenc_si128`, `fio_fx86_aesenclast_si128`, `fio_fx86_aeskeygenassist_si128`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_aesenc_si128(fio_fx86_m128i a,
                                               fio_fx86_m128i roundkey);
FIO_IFUNC fio_fx86_m128i fio_fx86_aesenclast_si128(fio_fx86_m128i a,
                                                   fio_fx86_m128i roundkey);
FIO_IFUNC fio_fx86_m128i fio_fx86_aeskeygenassist_si128(fio_fx86_m128i a,
                                                        int imm8);
```

AES-NI helpers. The fallback implements AES round logic with an S-box table. It is correct for portability tests, but the table lookups are cache-dependent; do not treat fake AES-NI as constant-time production crypto.

#### `fio_fx86_clmulepi64_si128`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_clmulepi64_si128(fio_fx86_m128i a,
                                                   fio_fx86_m128i b,
                                                   int imm8);
```

PCLMULQDQ carry-less multiplication. `imm8 & 1` selects the 64-bit lane from `a`; `(imm8 >> 4) & 1` selects the lane from `b`. The result is a 128-bit GF(2) product.

#### `fio_fx86_sha256rnds2_epu32`, `fio_fx86_sha256msg1_epu32`, `fio_fx86_sha256msg2_epu32`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_sha256rnds2_epu32(fio_fx86_m128i cdgh,
                                                    fio_fx86_m128i abef,
                                                    fio_fx86_m128i msg);
FIO_IFUNC fio_fx86_m128i fio_fx86_sha256msg1_epu32(fio_fx86_m128i msg0,
                                                   fio_fx86_m128i msg1);
FIO_IFUNC fio_fx86_m128i fio_fx86_sha256msg2_epu32(fio_fx86_m128i msg4,
                                                   fio_fx86_m128i msg1);
```

SHA-NI SHA-256 helpers. The round function expects `cdgh`, `abef`, and message-plus-constant layout matching Intel SHA extensions.

#### `fio_fx86_sha1rnds4_epu32`, `fio_fx86_sha1nexte_epu32`, `fio_fx86_sha1msg1_epu32`, `fio_fx86_sha1msg2_epu32`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_sha1rnds4_epu32(fio_fx86_m128i abcd,
                                                  fio_fx86_m128i e0,
                                                  int func);
FIO_IFUNC fio_fx86_m128i fio_fx86_sha1nexte_epu32(fio_fx86_m128i a,
                                                  fio_fx86_m128i b);
FIO_IFUNC fio_fx86_m128i fio_fx86_sha1msg1_epu32(fio_fx86_m128i msg0,
                                                 fio_fx86_m128i msg1);
FIO_IFUNC fio_fx86_m128i fio_fx86_sha1msg2_epu32(fio_fx86_m128i msg5,
                                                 fio_fx86_m128i msg1);
```

SHA-NI SHA-1 helpers. `func & 3` selects Ch, Parity, Maj, or Parity for `sha1rnds4`.

#### `fio_fx86_256_loadu_si256`, `fio_fx86_256_load_si256`, `fio_fx86_256_storeu_si256`

```c
FIO_IFUNC fio_fx86_m256i fio_fx86_256_loadu_si256(const void *p);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_load_si256(const void *p);
FIO_IFUNC void fio_fx86_256_storeu_si256(void *p, fio_fx86_m256i a);
```

AVX2-style 256-bit load/store helpers. The fallback copies 32 bytes.

#### `fio_fx86_256_add_epi16`, `fio_fx86_256_sub_epi16`, `fio_fx86_256_mullo_epi16`, `fio_fx86_256_add_epi32`, `fio_fx86_256_sub_epi32`, `fio_fx86_256_mullo_epi32`, `fio_fx86_256_add_epi64`, `fio_fx86_256_sub_epi64`

```c
FIO_IFUNC fio_fx86_m256i fio_fx86_256_add_epi16(fio_fx86_m256i a,
                                                fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_sub_epi16(fio_fx86_m256i a,
                                                fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_mullo_epi16(fio_fx86_m256i a,
                                                  fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_add_epi32(fio_fx86_m256i a,
                                                fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_sub_epi32(fio_fx86_m256i a,
                                                fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_mullo_epi32(fio_fx86_m256i a,
                                                  fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_add_epi64(fio_fx86_m256i a,
                                                fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_sub_epi64(fio_fx86_m256i a,
                                                fio_fx86_m256i b);
```

AVX2 packed integer arithmetic. Operations wrap by lane width; `mullo` keeps the low half of each product.

#### `fio_fx86_256_slli_epi32`, `fio_fx86_256_srai_epi32`

```c
FIO_IFUNC fio_fx86_m256i fio_fx86_256_slli_epi32(fio_fx86_m256i a, int imm8);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_srai_epi32(fio_fx86_m256i a, int imm8);
```

Per-lane 32-bit shifts. `srai` is arithmetic right shift; fallback values at or past 32 bits become the sign bit extended value.

#### `fio_fx86_256_xor_si256`, `fio_fx86_256_and_si256`, `fio_fx86_256_or_si256`

```c
FIO_IFUNC fio_fx86_m256i fio_fx86_256_xor_si256(fio_fx86_m256i a,
                                                fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_and_si256(fio_fx86_m256i a,
                                                fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_or_si256(fio_fx86_m256i a,
                                               fio_fx86_m256i b);
```

Bitwise operations over the whole 256-bit value.

#### `fio_fx86_256_cmpeq_epi8`, `fio_fx86_256_movemask_epi8`

```c
FIO_IFUNC fio_fx86_m256i fio_fx86_256_cmpeq_epi8(fio_fx86_m256i a,
                                                 fio_fx86_m256i b);
FIO_IFUNC int fio_fx86_256_movemask_epi8(fio_fx86_m256i a);
```

AVX2 byte equality and 32-bit byte movemask.

#### `fio_fx86_256_set1_epi8`, `fio_fx86_256_set1_epi16`, `fio_fx86_256_set1_epi32`, `fio_fx86_256_set1_epi64x`, `fio_fx86_256_set_epi64x`, `fio_fx86_256_set_epi8`, `fio_fx86_256_setzero_si256`

```c
FIO_IFUNC fio_fx86_m256i fio_fx86_256_set1_epi8(char a);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_set1_epi16(short a);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_set1_epi32(int a);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_set1_epi64x(long long a);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_set_epi64x(long long e3,
                                                 long long e2,
                                                 long long e1,
                                                 long long e0);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_set_epi8(char e31, char e30, char e29,
                                               char e28, char e27, char e26,
                                               char e25, char e24, char e23,
                                               char e22, char e21, char e20,
                                               char e19, char e18, char e17,
                                               char e16, char e15, char e14,
                                               char e13, char e12, char e11,
                                               char e10, char e9, char e8,
                                               char e7, char e6, char e5,
                                               char e4, char e3, char e2,
                                               char e1, char e0);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_setzero_si256(void);
```

AVX2 constructors. Like Intel intrinsics, the explicit `set` functions receive high lanes first and low lanes last.

#### `fio_fx86_256_shuffle_epi8`, `fio_fx86_256_alignr_epi8`, `fio_fx86_256_permute4x64_epi64`

```c
FIO_IFUNC fio_fx86_m256i fio_fx86_256_shuffle_epi8(fio_fx86_m256i a,
                                                   fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_alignr_epi8(fio_fx86_m256i a,
                                                  fio_fx86_m256i b,
                                                  int imm8);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_permute4x64_epi64(fio_fx86_m256i a,
                                                        int imm8);
```

AVX2 shuffle/permute helpers. `shuffle_epi8` and `alignr_epi8` operate inside each 128-bit lane. `permute4x64_epi64` selects among the four 64-bit lanes across the full 256-bit value.

#### `fio_fx86_256_packs_epi32`, `fio_fx86_256_cvtepi16_epi32`

```c
FIO_IFUNC fio_fx86_m256i fio_fx86_256_packs_epi32(fio_fx86_m256i a,
                                                  fio_fx86_m256i b);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_cvtepi16_epi32(fio_fx86_m128i a);
```

AVX2 pack/convert helpers. `packs_epi32` saturates 32-bit signed lanes to 16-bit signed lanes, per 128-bit lane. `cvtepi16_epi32` sign-extends eight 16-bit values from the low 128 bits into eight 32-bit lanes.

#### `fio_fx86_256_castsi256_si128`, `fio_fx86_256_extracti128_si256`

```c
FIO_IFUNC fio_fx86_m128i fio_fx86_256_castsi256_si128(fio_fx86_m256i a);
FIO_IFUNC fio_fx86_m128i fio_fx86_256_extracti128_si256(fio_fx86_m256i a,
                                                        int imm8);
```

Extract 128-bit halves from a 256-bit value. `extracti128` uses `imm8 & 1`.

#### `fio_fx86_256_sha512rnds2_epi64`, `fio_fx86_256_sha512msg1_epi64`, `fio_fx86_256_sha512msg2_epi64`

```c
FIO_IFUNC fio_fx86_m256i fio_fx86_256_sha512rnds2_epi64(fio_fx86_m256i state1,
                                                        fio_fx86_m256i state0,
                                                        fio_fx86_m256i wk);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_sha512msg1_epi64(fio_fx86_m256i w0,
                                                       fio_fx86_m256i w1);
FIO_IFUNC fio_fx86_m256i fio_fx86_256_sha512msg2_epi64(fio_fx86_m256i w0,
                                                       fio_fx86_m256i w1);
```

Intel SHA-512 extension helpers using four 64-bit lanes.

### Shadowed Intrinsic Families

#### `_mm_*`

With `FIO_FAKE_X86_SHADOW`, these SSE/SHA/AES names are mapped to the matching `fio_fx86_*` functions:

```c
_mm_loadu_si128, _mm_load_si128, _mm_storeu_si128,
_mm_add_epi32,
_mm_xor_si128, _mm_or_si128, _mm_and_si128,
_mm_slli_si128, _mm_srli_si128,
_mm_slli_epi32, _mm_srli_epi32,
_mm_slli_epi64, _mm_srli_epi64,
_mm_shuffle_epi32, _mm_shuffle_epi8, _mm_alignr_epi8,
_mm_cmpeq_epi8, _mm_movemask_epi8,
_mm_set_epi32, _mm_set_epi64x, _mm_set_epi8,
_mm_set1_epi8, _mm_setzero_si128, _mm_cvtsi32_si128,
_mm_blend_epi16, _mm_extract_epi32, _mm_prefetch,
_mm_aesenc_si128, _mm_aesenclast_si128, _mm_aeskeygenassist_si128,
_mm_clmulepi64_si128,
_mm_sha256rnds2_epu32, _mm_sha256msg1_epu32, _mm_sha256msg2_epu32,
_mm_sha1rnds4_epu32, _mm_sha1nexte_epu32,
_mm_sha1msg1_epu32, _mm_sha1msg2_epu32
```

#### `_mm256_*`

With `FIO_FAKE_X86_SHADOW`, these AVX2/SHA-512 names are mapped to the matching `fio_fx86_256_*` functions:

```c
_mm256_loadu_si256, _mm256_load_si256, _mm256_storeu_si256,
_mm256_add_epi16, _mm256_sub_epi16, _mm256_mullo_epi16,
_mm256_add_epi32, _mm256_sub_epi32, _mm256_mullo_epi32,
_mm256_add_epi64, _mm256_sub_epi64,
_mm256_slli_epi32, _mm256_srai_epi32,
_mm256_xor_si256, _mm256_and_si256, _mm256_or_si256,
_mm256_cmpeq_epi8, _mm256_movemask_epi8,
_mm256_set1_epi8, _mm256_set1_epi16, _mm256_set1_epi32,
_mm256_set1_epi64x, _mm256_set_epi64x, _mm256_set_epi8,
_mm256_setzero_si256,
_mm256_shuffle_epi8, _mm256_alignr_epi8, _mm256_permute4x64_epi64,
_mm256_packs_epi32, _mm256_cvtepi16_epi32,
_mm256_castsi256_si128, _mm256_extracti128_si256,
_mm256_sha512rnds2_epi64, _mm256_sha512msg1_epi64,
_mm256_sha512msg2_epi64
```

### Examples

#### 128-bit add

```c
#define FIO_FAKE_X86
#include "fio-stl.h"
#include <stdint.h>

int main(void) {
  uint32_t a_in[4] = {1, 2, 3, 4};
  uint32_t b_in[4] = {10, 20, 30, 40};
  uint32_t out[4];

  fio_fx86_m128i a = fio_fx86_loadu_si128(a_in);
  fio_fx86_m128i b = fio_fx86_loadu_si128(b_in);
  fio_fx86_m128i c = fio_fx86_add_epi32(a, b);
  fio_fx86_storeu_si128(out, c);

  return (out[0] == 11 && out[3] == 44) ? 0 : 1;
}
```

#### AVX2-style byte equality

```c
#define FIO_FAKE_X86
#include "fio-stl.h"
#include <stdint.h>

int main(void) {
  uint8_t a[32] = {0};
  uint8_t b[32] = {0};
  b[7] = 1;

  fio_fx86_m256i va = fio_fx86_256_loadu_si256(a);
  fio_fx86_m256i vb = fio_fx86_256_loadu_si256(b);
  fio_fx86_m256i eq = fio_fx86_256_cmpeq_epi8(va, vb);
  int mask = fio_fx86_256_movemask_epi8(eq);

  return ((mask & (1 << 7)) == 0) ? 0 : 1;
}
```

#### Shadowing x86 names

```c
#define FIO_FAKE_X86_SHADOW
#include "fio-stl.h"
#include <stdint.h>

int main(void) {
  uint32_t in[4] = {0x01020304, 0, 0, 0};
  uint32_t out[4];

  __m128i v = _mm_loadu_si128((const __m128i *)in);
  v = _mm_slli_epi32(v, 8);
  _mm_storeu_si128((__m128i *)out, v);

  return out[0] == 0x02030400 ? 0 : 1;
}
```

### Safety Notes

#### Memory ownership

The fake x86 API does not allocate or free memory. Load and store functions copy fixed-size byte ranges from caller-owned buffers.

#### Thread safety

Functions are value-based and use only caller-provided memory plus read-only tables. They are thread-safe when callers synchronize shared buffers as usual.

#### Constant-time behavior

The portable AES fallback uses lookup tables and is not constant-time. SHA and PCLMUL fallbacks are portable C implementations intended for correctness and test coverage, not a promise of native-intrinsic timing.

#### Macro expansion

`FIO_FOR_UNROLL` declares loop variables inside the macro and expands `action` many times. Shadow-mode `_mm*` names are macros that call the `fio_fx86*` functions; avoid side-effect-heavy arguments even when they appear to be evaluated once today.
# Static Scratch Allocator

A tiny, statically-backed allocator for short-lived temporary buffers.

Use it when you need a quick buffer you can return to a caller that should not be freed by the caller, or as a safer alternative to `alloca` for data that only needs to stay valid until the next few function calls.

The implementation is in [`./000 core.h`](./000%20core.h).

## `FIO_STATIC_ALLOC_SAFE_CONCURRENCY_MAX`

```c
#ifndef FIO_STATIC_ALLOC_SAFE_CONCURRENCY_MAX
#define FIO_STATIC_ALLOC_SAFE_CONCURRENCY_MAX 256
#endif
```

Maximum safe concurrent calls for all static allocators defined with `FIO_STATIC_ALLOC_DEF`.

The allocator is a round-robin buffer. Once the number of in-flight allocations exceeds this multiplier, new allocations may reuse memory that a previous caller still holds. Raise this value if you use many static allocator calls or many threads and see buffer reuse too early.

## `FIO_STATIC_ALLOC_DEF`

```c
#define FIO_STATIC_ALLOC_DEF(name, type_T, size_per_allocation, allocations_per_thread)
```

Defines a static allocator named `name`.

```c
static type_T *name(size_t count);
```

The generated function takes a `count` and returns a `type_T *` to a buffer of `sizeof(type_T) * count * size_per_allocation` bytes, aligned for `type_T`.

- `name` — the allocator function name.
- `type_T` — the element type the pointer is cast to.
- `size_per_allocation` — base size multiplier for each allocation unit.
- `allocations_per_thread` — extra headroom per logical caller.

The total safe buffer size before reuse is:

```
FIO_STATIC_ALLOC_SAFE_CONCURRENCY_MAX * allocations_per_thread *
    sizeof(type_T) * size_per_allocation
```

## Generated API

The macro produces a single function:

- **`type_T *name(size_t count)`** — returns a pointer to the next slice of the static round-robin buffer. No matching free exists; the memory is static and reused automatically.

There is no generated `name##_free`, `name##_reset`, or similar helper. The buffer is managed automatically by advancing an atomic position counter and wrapping around.

## Example

```c
FIO_STATIC_ALLOC_DEF(numer2hex_allocator, char, 19, 1);

char *ntos16(uint16_t n) {
  char *buf = numer2hex_allocator(1);
  buf[0] = '0';
  buf[1] = 'x';
  fio_ltoa16u(buf + 2, n, 16);
  buf[18] = 0;
  return buf;
}
```

The returned string is valid only until the allocator wraps around. A similar pattern is used by `fiobj_num2cstr` for temporary conversions.

## Thread Safety

The allocator is only "good enough" thread-safe. The atomic position counter protects the round-robin index, but the safety window is bounded by `FIO_STATIC_ALLOC_SAFE_CONCURRENCY_MAX`. Keep allocations short-lived and do not hold the returned pointer across too many other static allocator calls or across too many threads.
# String and Buffer Information

Lightweight descriptors for byte ranges. They are defined in [`./000 core.h`](./000%20core.h) and are used throughout the library to pass around strings and buffers without taking ownership.

## Descriptor Types

### `fio_str_info_s`

A string descriptor that carries length, buffer pointer, and capacity.

```c
typedef struct fio_str_info_s {
  size_t len;   /* string length, in bytes */
  char *buf;    /* pointer to the first byte, or NULL on error */
  size_t capa;  /* buffer capacity; 0 means read-only */
} fio_str_info_s;
```

`capa` tells callers how much room is available in the underlying buffer. When `capa` is `0`, the buffer is considered read-only and must not be written to through the descriptor.

### `fio_buf_info_s`

A lighter buffer descriptor with no capacity field.

```c
typedef struct fio_buf_info_s {
  size_t len;   /* buffer length, in bytes */
  char *buf;    /* pointer to the first byte, may be NULL */
} fio_buf_info_s;
```

Use this when only a length/buffer pair is needed, such as for input data or temporary views.

## Construction Macros

### `FIO_STR_INFO0`

An all-zero `fio_str_info_s`.

```c
#define FIO_STR_INFO0 ((fio_str_info_s){0})
```

### `FIO_STR_INFO1(str)`

Build an `fio_str_info_s` from a NUL-terminated C string. Computes the length with `FIO_STRLEN`. If `str` is `NULL`, the length is `0`.

```c
#define FIO_STR_INFO1(str) \
  ((fio_str_info_s){.len = ((str) ? FIO_STRLEN((str)) : 0), .buf = (str)})
```

### `FIO_STR_INFO2(str, length)`

Build an `fio_str_info_s` from a buffer and a known length.

```c
#define FIO_STR_INFO2(str, length) \
  ((fio_str_info_s){.len = (length), .buf = (str)})
```

### `FIO_STR_INFO3(str, length, capacity)`

Build a fully initialized `fio_str_info_s`.

```c
#define FIO_STR_INFO3(str, length, capacity) \
  ((fio_str_info_s){.len = (length), .buf = (str), .capa = (capacity)})
```

### `FIO_BUF_INFO0`

An all-zero `fio_buf_info_s`.

```c
#define FIO_BUF_INFO0 ((fio_buf_info_s){0})
```

### `FIO_BUF_INFO1(str)`

Build an `fio_buf_info_s` from a NUL-terminated C string. If `str` is `NULL`, the length is `0`.

```c
#define FIO_BUF_INFO1(str) \
  ((fio_buf_info_s){.len = ((str) ? FIO_STRLEN((str)) : 0), .buf = (str)})
```

### `FIO_BUF_INFO2(str, length)`

Build an `fio_buf_info_s` from a buffer and a known length.

```c
#define FIO_BUF_INFO2(str, length) \
  ((fio_buf_info_s){.len = (length), .buf = (str)})
```

## Equality

### `FIO_STR_INFO_IS_EQ(s1, s2)`

Returns a true value if two `fio_str_info_s` objects have the same content.

The comparison is a fast-path short-circuit: equal length and either both empty, the same pointer, or matching first byte and `FIO_MEMCMP` equal.

```c
#define FIO_STR_INFO_IS_EQ(s1, s2) \
  ((s1).len == (s2).len && \
   (!(s1).len || (s1).buf == (s2).buf || \
    ((s1).buf && (s2).buf && (s1).buf[0] == (s2).buf[0] && \
     !FIO_MEMCMP((s1).buf, (s2).buf, (s1).len))))
```

### `FIO_BUF_INFO_IS_EQ(s1, s2)`

Same comparison, routed to `FIO_STR_INFO_IS_EQ`.

```c
#define FIO_BUF_INFO_IS_EQ(s1, s2) FIO_STR_INFO_IS_EQ((s1), (s2))
```

## Conversion

### `FIO_BUF2STR_INFO(buf_info)`

Convert an `fio_buf_info_s` to an `fio_str_info_s`. The resulting descriptor has the same `len` and `buf`; `capa` is left as `0`.

```c
#define FIO_BUF2STR_INFO(buf_info) \
  ((fio_str_info_s){.len = (buf_info).len, .buf = (buf_info).buf})
```

### `FIO_STR2BUF_INFO(str_info)`

Convert an `fio_str_info_s` to an `fio_buf_info_s`, dropping the capacity.

```c
#define FIO_STR2BUF_INFO(str_info) \
  ((fio_buf_info_s){.len = (str_info).len, .buf = (str_info).buf})
```

## Temporary Stack Variables

### `FIO_STR_INFO_TMP_VAR(name, capacity)`

Creates a stack-allocated `fio_str_info_s` named `name` with `capacity` writable bytes. The macro allocates `capacity + 1` bytes so a trailing NUL guard is always present, and initializes `name.buf` and `name.capa`.

```c
#define FIO_STR_INFO_TMP_VAR(name, capacity) \
  char fio___stack_mem___##name[(capacity) + 1]; \
  fio___stack_mem___##name[0] = 0; /* guard */ \
  fio_str_info_s name = (fio_str_info_s) { \
    .buf = fio___stack_mem___##name, .capa = (capacity) \
  }
```

Use this when a function needs a writable working buffer that normally fits on the stack but may be reallocated by downstream helpers.

### `FIO_STR_INFO_TMP_IS_REALLOCATED(name)`

Returns a true value if the temporary variable `name` no longer points to its original stack buffer, which indicates that a helper reallocated the memory onto the heap.

```c
#define FIO_STR_INFO_TMP_IS_REALLOCATED(name) \
  (fio___stack_mem___##name != name.buf)
```

If this returns true, the buffer must eventually be freed by whatever allocator the helper used (usually the facil.io allocator or the override set via `FIO_MEM_REALLOC`).
# UTF-8 Helpers

Single-character encoding, decoding, and validation helpers for UTF-8 text. Defined in [`./000 core.h`](./000%20core.h).

These functions work on one code point at a time and do not check memory bounds. Use them carefully — make sure the buffer has enough space for writes and is padded or NUL-terminated for reads.

## Compile-Time Flag

### `FIO_UTF8_ALLOW_IF`

Controls whether the UTF-8 helpers may use branchy (`if`) fast paths.

- **`1`** (default): ASCII-biased fast paths. Best when most input is ASCII.
- **`0`**: Branchless, constant-time-ish paths. Better when input distribution is unpredictable and branch mispredictions are a concern.

Defined before including [`./000 core.h`](./000%20core.h) to override the default.

## Length Helpers

### `fio_utf8_code_len`

```c
unsigned fio_utf8_code_len(uint32_t u);
```

Returns the number of bytes required to encode code point `u` in UTF-8.

Returns `1`–`4` for code points up to `0x1FFFFF`, and returns `0` only above that bound.

This bound is above the valid Unicode range (`0x10FFFF`), so the function accepts code points in the range `0x110000`–`0x1FFFFF` as a consequence of its bitmask. `fio_utf8_write` will encode those code points as 4-byte sequences.

### `fio_utf8_char_len_unsafe`

```c
unsigned fio_utf8_char_len_unsafe(uint8_t c);
```

Classifies a single leading byte without validating continuation bytes.

Returns:

- `1`–`4`: the expected total byte length of the UTF-8 character.
- `8`: a continuation byte (middle of a multi-byte character).
- `0`: an invalid leading byte.

Use this only to recover length information after a successful `fio_utf8_char_len`, `fio_utf8_write`, or similar call where validity is already known.

### `fio_utf8_char_len`

```c
unsigned fio_utf8_char_len(const void *str);
```

Returns the number of valid UTF-8 bytes used by the first character at `str`, validating continuation bytes.

Returns `0` if `str` does not point to a valid UTF-8-encoded code point. Returns `1`–`4` for a valid character.

## Write Helper

### `fio_utf8_write`

```c
unsigned fio_utf8_write(void *dest, uint32_t u);
```

Writes the UTF-8 encoding of code point `u` to `dest`. Returns the number of bytes written (`0`–`4`).

`u` is treated as described for `fio_utf8_code_len`: code points up to `0x1FFFFF` are encoded (including the non-Unicode range `0x110000`–`0x1FFFFF`), and higher values result in `0` bytes written.

The caller must ensure `dest` has room for up to 4 bytes. Typical use advances the destination pointer:

```c
dest += fio_utf8_write(dest, u);
```

## Read Helper

### `fio_utf8_read`

```c
uint32_t fio_utf8_read(char **str);
```

Decodes the first UTF-8 character at `*str` and returns its code point. Advances `*str` by the number of bytes consumed.

Returns `0` if the byte at `*str` is not a valid UTF-8 start byte or continuation sequence.

## Peek Helper

### `fio_utf8_peek`

```c
uint32_t fio_utf8_peek(const char *str);
```

Decodes the first UTF-8 character at `str` and returns its code point. Does not modify `str`.

Returns `0` for invalid input, same as `fio_utf8_read`.

## See Also

- [`./000 core.md`](./000%20core.md) — overview of core helpers.
- [`./000 core.h`](./000%20core.h) — source of truth for the UTF-8 helpers.
- [`./001 string info.md`](./001%20string%20info.md) — string and buffer descriptors used with text helpers.
# Vector Math

Wide integer unions and lane-wise arithmetic helpers defined in [`./000 core.h`](./000%20core.h). These types are used for SIMD-style processing, big-integer arithmetic, hashing, and cryptography.

## Wide Union Types

These unions expose the same data through arrays of different widths, plus platform-specific vector views when SIMD is available. They are sized and aligned as follows:

| Type | Size | Alignment |
|------|------|-----------|
| `fio_u128`  | 16 bytes  | 16 |
| `fio_u256`  | 32 bytes  | 32 |
| `fio_u512`  | 64 bytes  | 64 |
| `fio_u1024` | 128 bytes | 64 |
| `fio_u2048` | 256 bytes | 64 |
| `fio_u4096` | 512 bytes | 64 |

All types include these overlapping members (counts vary by element width):

```c
size_t      uz[N];    ssize_t     iz[N];
uint64_t    u64[N];   int64_t     i64[N];
uint32_t    u32[N];   int32_t     i32[N];
uint16_t    u16[N];   int16_t     i16[N];
uint8_t     u8[N];    int8_t      i8[N];
float       f[N];
double      d[N];
long double ld[total_bytes / sizeof(long double)];
```

`total_bytes` is the type size from the table above, and each `N` is `total_bytes / sizeof(element)`. Larger types also contain smaller ones, for example `fio_u1024` provides `u128[8]`, `u256[4]`, and `u512[2]` in addition to the generic arrays.

When the compiler supports GCC/Clang `vector_size` attributes or ARM NEON intrinsics, the unions also expose vector views named `x64`, `x32`, `x16`, and `x8`. The internal layout adapts to the platform:

- On GCC/Clang, `x64[1]` is a single full-width vector.
- On ARM NEON, `x64[]` is an array of 128-bit `uint64x2_t` vectors.
- On platforms without vector support, `x64[]` falls back to a plain `uint64_t` array.

## Initialization Macros

Initialize a wide union from a list of lane values. Unused lanes are zeroed by the compiler.

```c
fio_u128_init8(0x01, 0x02, ...)
fio_u128_init16(...)
fio_u128_init32(...)
fio_u128_init64(...)

fio_u256_init8(...)
fio_u256_init16(...)
fio_u256_init32(...)
fio_u256_init64(...)

fio_u512_init8(...)
fio_u512_init16(...)
fio_u512_init32(...)
fio_u512_init64(...)

fio_u1024_init8(...)
fio_u1024_init16(...)
fio_u1024_init32(...)
fio_u1024_init64(...)

fio_u2048_init8(...)
fio_u2048_init16(...)
fio_u2048_init32(...)
fio_u2048_init64(...)

fio_u4096_init8(...)
fio_u4096_init16(...)
fio_u4096_init32(...)
fio_u4096_init64(...)
```

## Load, Store, and Byte Swap

All loads and stores copy whole vectors to and from memory using the fixed-size `fio_memcpyNN` helpers.

```c
fio_u128  fio_u128_load(const void *buf);
void      fio_u128_store(void *buf, const fio_u128 a);

fio_u256  fio_u256_load(const void *buf);
void      fio_u256_store(void *buf, const fio_u256 a);

fio_u512  fio_u512_load(const void *buf);
void      fio_u512_store(void *buf, const fio_u512 a);

fio_u1024 fio_u1024_load(const void *buf);
void      fio_u1024_store(void *buf, const fio_u1024 a);

fio_u2048 fio_u2048_load(const void *buf);
void      fio_u2048_store(void *buf, const fio_u2048 a);

fio_u4096 fio_u4096_load(const void *buf);
void      fio_u4096_store(void *buf, const fio_u4096 a);
```

Endian-aware load helpers read each lane from the given byte order and convert it to local order. Store variants are not provided here; use the byte-swap helpers before `store` if needed.

```c
fio_u128  fio_u128_load_le16(const void *buf);
fio_u128  fio_u128_load_be16(const void *buf);
fio_u128  fio_u128_load_le32(const void *buf);
fio_u128  fio_u128_load_be32(const void *buf);
fio_u128  fio_u128_load_le64(const void *buf);
fio_u128  fio_u128_load_be64(const void *buf);

fio_u256  fio_u256_load_le16(...); fio_u256  fio_u256_load_be16(...);
fio_u256  fio_u256_load_le32(...); fio_u256  fio_u256_load_be32(...);
fio_u256  fio_u256_load_le64(...); fio_u256  fio_u256_load_be64(...);

fio_u512  fio_u512_load_le16(...); fio_u512  fio_u512_load_be16(...);
fio_u512  fio_u512_load_le32(...); fio_u512  fio_u512_load_be32(...);
fio_u512  fio_u512_load_le64(...); fio_u512  fio_u512_load_be64(...);

fio_u1024 fio_u1024_load_le16(...); fio_u1024 fio_u1024_load_be16(...);
fio_u1024 fio_u1024_load_le32(...); fio_u1024 fio_u1024_load_be32(...);
fio_u1024 fio_u1024_load_le64(...); fio_u1024 fio_u1024_load_be64(...);

fio_u2048 fio_u2048_load_le16(...); fio_u2048 fio_u2048_load_be16(...);
fio_u2048 fio_u2048_load_le32(...); fio_u2048 fio_u2048_load_be32(...);
fio_u2048 fio_u2048_load_le64(...); fio_u2048 fio_u2048_load_be64(...);

fio_u4096 fio_u4096_load_le16(...); fio_u4096 fio_u4096_load_be16(...);
fio_u4096 fio_u4096_load_le32(...); fio_u4096 fio_u4096_load_be32(...);
fio_u4096 fio_u4096_load_le64(...); fio_u4096 fio_u4096_load_be64(...);
```

Byte-swap every lane of a vector, returning the result by value:

```c
fio_u128  fio_u128_bswap16(fio_u128 a);
fio_u128  fio_u128_bswap32(fio_u128 a);
fio_u128  fio_u128_bswap64(fio_u128 a);

fio_u256  fio_u256_bswap16(...);  fio_u256  fio_u256_bswap32(...);  fio_u256  fio_u256_bswap64(...);
fio_u512  fio_u512_bswap16(...);  fio_u512  fio_u512_bswap32(...);  fio_u512  fio_u512_bswap64(...);
fio_u1024 fio_u1024_bswap16(...); fio_u1024 fio_u1024_bswap32(...); fio_u1024 fio_u1024_bswap64(...);
fio_u2048 fio_u2048_bswap16(...); fio_u2048 fio_u2048_bswap32(...); fio_u2048 fio_u2048_bswap64(...);
fio_u4096 fio_u4096_bswap16(...); fio_u4096 fio_u4096_bswap32(...); fio_u4096 fio_u4096_bswap64(...);
```

## Lane-Wise Arithmetic

The arithmetic functions write lane-wise results into `*target`. They work on any `fio_uXXX` type at 8, 16, 32, and 64-bit lane widths. The compiler vectorizes the loops when SIMD is available.

```c
void fio_u128_add8(fio_u128 *target, const fio_u128 *a, const fio_u128 *b);
void fio_u128_add16(fio_u128 *target, const fio_u128 *a, const fio_u128 *b);
void fio_u128_add32(fio_u128 *target, const fio_u128 *a, const fio_u128 *b);
void fio_u128_add64(fio_u128 *target, const fio_u128 *a, const fio_u128 *b);

void fio_u128_sub8(...); void fio_u128_sub16(...); void fio_u128_sub32(...); void fio_u128_sub64(...);
void fio_u128_mul8(...); void fio_u128_mul16(...); void fio_u128_mul32(...); void fio_u128_mul64(...);
void fio_u128_and8(...); void fio_u128_and16(...); void fio_u128_and32(...); void fio_u128_and64(...);
void fio_u128_or8(...);  void fio_u128_or16(...);  void fio_u128_or32(...);  void fio_u128_or64(...);
void fio_u128_xor8(...); void fio_u128_xor16(...); void fio_u128_xor32(...); void fio_u128_xor64(...);
```

These are generated for all wide types: `fio_u256`, `fio_u512`, `fio_u1024`, `fio_u2048`, and `fio_u4096`.

Constant-operand variants apply the same operation with a scalar in place of `*b`:

```c
void fio_u128_cadd8(fio_u128 *target, const fio_u128 *a, uint8_t  b);
void fio_u128_cadd16(fio_u128 *target, const fio_u128 *a, uint16_t b);
void fio_u128_cadd32(fio_u128 *target, const fio_u128 *a, uint32_t b);
void fio_u128_cadd64(fio_u128 *target, const fio_u128 *a, uint64_t b);

void fio_u128_csub8(...); void fio_u128_csub16(...); void fio_u128_csub32(...); void fio_u128_csub64(...);
void fio_u128_cmul8(...); void fio_u128_cmul16(...); void fio_u128_cmul32(...); void fio_u128_cmul64(...);
void fio_u128_cand8(...); void fio_u128_cand16(...); void fio_u128_cand32(...); void fio_u128_cand64(...);
void fio_u128_cor8(...);  void fio_u128_cor16(...);  void fio_u128_cor32(...);  void fio_u128_cor64(...);
void fio_u128_cxor8(...); void fio_u128_cxor16(...); void fio_u128_cxor32(...); void fio_u128_cxor64(...);
```

Reduction functions fold all lanes of the given width into a single scalar:

```c
uint8_t  fio_u128_reduce_add8(const fio_u128 *a);
uint16_t fio_u128_reduce_add16(const fio_u128 *a);
uint32_t fio_u128_reduce_add32(const fio_u128 *a);
uint64_t fio_u128_reduce_add64(const fio_u128 *a);

uint8_t  fio_u128_reduce_sub8(...);  uint16_t fio_u128_reduce_sub16(...);
uint32_t fio_u128_reduce_sub32(...); uint64_t fio_u128_reduce_sub64(...);
uint8_t  fio_u128_reduce_mul8(...);  uint16_t fio_u128_reduce_mul16(...);
uint32_t fio_u128_reduce_mul32(...); uint64_t fio_u128_reduce_mul64(...);
uint8_t  fio_u128_reduce_and8(...);  uint16_t fio_u128_reduce_and16(...);
uint32_t fio_u128_reduce_and32(...); uint64_t fio_u128_reduce_and64(...);
uint8_t  fio_u128_reduce_or8(...);   uint16_t fio_u128_reduce_or16(...);
uint32_t fio_u128_reduce_or32(...);  uint64_t fio_u128_reduce_or64(...);
uint8_t  fio_u128_reduce_xor8(...);  uint16_t fio_u128_reduce_xor16(...);
uint32_t fio_u128_reduce_xor32(...); uint64_t fio_u128_reduce_xor64(...);
```

Whole-vector variants that operate on 64-bit lanes without a width suffix are also provided for bitwise operations:

```c
void fio_u128_and(fio_u128 *target, const fio_u128 *a, const fio_u128 *b);
void fio_u128_or (fio_u128 *target, const fio_u128 *a, const fio_u128 *b);
void fio_u128_xor(fio_u128 *target, const fio_u128 *a, const fio_u128 *b);
```

## Rotations

Lane-wise right and left rotations. Variable rotations read the per-lane shift from an array; constant rotations use a single `uint8_t` for all lanes.

```c
void fio_u128_rrot8 (fio_u128 *target, const fio_u128 *a, const uint8_t rotations[16]);
void fio_u128_rrot16(fio_u128 *target, const fio_u128 *a, const uint8_t rotations[8]);
void fio_u128_rrot32(fio_u128 *target, const fio_u128 *a, const uint8_t rotations[4]);
void fio_u128_rrot64(fio_u128 *target, const fio_u128 *a, const uint8_t rotations[2]);

void fio_u128_lrot8 (fio_u128 *target, const fio_u128 *a, const uint8_t rotations[16]);
void fio_u128_lrot16(fio_u128 *target, const fio_u128 *a, const uint8_t rotations[8]);
void fio_u128_lrot32(fio_u128 *target, const fio_u128 *a, const uint8_t rotations[4]);
void fio_u128_lrot64(fio_u128 *target, const fio_u128 *a, const uint8_t rotations[2]);

void fio_u128_crrot8 (fio_u128 *target, const fio_u128 *a, const uint8_t bts);
void fio_u128_crrot16(fio_u128 *target, const fio_u128 *a, const uint8_t bts);
void fio_u128_crrot32(fio_u128 *target, const fio_u128 *a, const uint8_t bts);
void fio_u128_crrot64(fio_u128 *target, const fio_u128 *a, const uint8_t bts);

void fio_u128_clrot8 (fio_u128 *target, const fio_u128 *a, const uint8_t bts);
void fio_u128_clrot16(fio_u128 *target, const fio_u128 *a, const uint8_t bts);
void fio_u128_clrot32(fio_u128 *target, const fio_u128 *a, const uint8_t bts);
void fio_u128_clrot64(fio_u128 *target, const fio_u128 *a, const uint8_t bts);
```

All rotation families exist for `fio_u256`, `fio_u512`, `fio_u1024`, `fio_u2048`, and `fio_u4096` with appropriate array sizes.

## Ternary Lane-Wise Operations

These compute common three-input bit functions across each lane.

```c
// target[i] = z[i] ^ (x[i] & (y[i] ^ z[i]))
void fio_u128_mux32(fio_u128 *target, const fio_u128 *x, const fio_u128 *y, const fio_u128 *z);
void fio_u128_mux64(fio_u128 *target, const fio_u128 *x, const fio_u128 *y, const fio_u128 *z);

// target[i] = (x[i] & y[i]) | (z[i] & (x[i] | y[i]))
void fio_u128_maj32(fio_u128 *target, const fio_u128 *x, const fio_u128 *y, const fio_u128 *z);
void fio_u128_maj64(fio_u128 *target, const fio_u128 *x, const fio_u128 *y, const fio_u128 *z);

// target[i] = x[i] ^ y[i] ^ z[i]
void fio_u128_3xor32(fio_u128 *target, const fio_u128 *x, const fio_u128 *y, const fio_u128 *z);
void fio_u128_3xor64(fio_u128 *target, const fio_u128 *x, const fio_u128 *y, const fio_u128 *z);
```

Whole-vector 64-bit variants without a width suffix are also provided:

```c
void fio_u128_mux(fio_u128 *target, const fio_u128 *x, const fio_u128 *y, const fio_u128 *z);
void fio_u128_maj(fio_u128 *target, const fio_u128 *x, const fio_u128 *y, const fio_u128 *z);
void fio_u128_3xor(fio_u128 *target, const fio_u128 *x, const fio_u128 *y, const fio_u128 *z);
```

All ternary functions exist for the other wide types: `fio_u256`, `fio_u512`, `fio_u1024`, `fio_u2048`, and `fio_u4096`.

## Whole-Vector Value Operations

A small set of convenience functions return results by value instead of writing through a pointer. They operate on 64-bit lanes and are designed to let the compiler use SIMD directly.

```c
fio_u128 fio_u128_xorv(fio_u128 a, fio_u128 b);
fio_u128 fio_u128_andv(fio_u128 a, fio_u128 b);
fio_u128 fio_u128_orv (fio_u128 a, fio_u128 b);
fio_u128 fio_u128_addv64(fio_u128 a, fio_u128 b);

fio_u256 fio_u256_xorv(...);  fio_u256 fio_u256_andv(...);  fio_u256 fio_u256_orv(...);  fio_u256 fio_u256_addv64(...);
fio_u512 fio_u512_xorv(...);  fio_u512 fio_u512_andv(...);  fio_u512 fio_u512_orv(...);  fio_u512 fio_u512_addv64(...);
fio_u1024 fio_u1024_xorv(...); fio_u1024 fio_u1024_andv(...); fio_u1024 fio_u1024_orv(...); fio_u1024 fio_u1024_addv64(...);
fio_u2048 fio_u2048_xorv(...); fio_u2048 fio_u2048_andv(...); fio_u2048 fio_u2048_orv(...); fio_u2048 fio_u2048_addv64(...);
fio_u4096 fio_u4096_xorv(...); fio_u4096 fio_u4096_andv(...); fio_u4096 fio_u4096_orv(...); fio_u4096 fio_u4096_addv64(...);
```

## Constant-Time Helpers

```c
bool fio_u128_is_eq(const fio_u128 *a, const fio_u128 *b);
```

Returns `true` if every byte of `*a` equals the corresponding byte of `*b`, otherwise `false`. The comparison is branchless over the vector body.

```c
void fio_u128_inv(fio_u128 *target, const fio_u128 *a);
```

Bitwise NOT: `target[i] = ~a[i]` on 64-bit lanes.

```c
void fio_u128_ct_swap_if(bool cond, fio_u128 *a, fio_u128 *b);
```

Swaps `*a` and `*b` when `cond` is true, without branching on `cond`. Uses a mask derived from `(uint64_t)0 - cond`.

All three helpers exist for `fio_u256`, `fio_u512`, `fio_u1024`, `fio_u2048`, and `fio_u4096`.

## Multi-Precision Math

These functions treat the wide unions as unsigned big integers made of little-endian `u64` words.

```c
uint64_t fio_u128_add(fio_u128 *result, const fio_u128 *a, const fio_u128 *b);
uint64_t fio_u128_sub(fio_u128 *result, const fio_u128 *a, const fio_u128 *b);
int      fio_u128_cmp(const fio_u128 *a, const fio_u128 *b);

uint64_t fio_u256_add(...); uint64_t fio_u256_sub(...); int fio_u256_cmp(...);
uint64_t fio_u512_add(...); uint64_t fio_u512_sub(...); int fio_u512_cmp(...);
uint64_t fio_u1024_add(...); uint64_t fio_u1024_sub(...); int fio_u1024_cmp(...);
uint64_t fio_u2048_add(...); uint64_t fio_u2048_sub(...); int fio_u2048_cmp(...);
uint64_t fio_u4096_add(...); uint64_t fio_u4096_sub(...); int fio_u4096_cmp(...);
```

- `fio_uXXX_add` returns the carry bit (`1` or `0`).
- `fio_uXXX_sub` returns the borrow bit (`1` or `0`).
- `fio_uXXX_cmp` returns `-1` if `a < b`, `0` if equal, and `1` if `a > b`.

Multiplication doubles the width of the result:

```c
void fio_u128_mul(fio_u256 *result, const fio_u128 *a, const fio_u128 *b);
void fio_u256_mul(fio_u512 *result, const fio_u256 *a, const fio_u256 *b);
void fio_u512_mul(fio_u1024 *result, const fio_u512 *a, const fio_u512 *b);
void fio_u1024_mul(fio_u2048 *result, const fio_u1024 *a, const fio_u1024 *b);
void fio_u2048_mul(fio_u4096 *result, const fio_u2048 *a, const fio_u2048 *b);
```

Montgomery multiplication is provided for RSA-like modular arithmetic:

```c
void fio_u128_montgomery_mul(fio_u128 *result, const fio_u128 *a, const fio_u128 *b,
                             const fio_u128 *N, const fio_u128 *N_dash);
void fio_u256_montgomery_mul(fio_u256 *result, const fio_u256 *a, const fio_u256 *b,
                             const fio_u256 *N, const fio_u256 *N_dash);
void fio_u512_montgomery_mul(fio_u512 *result, const fio_u512 *a, const fio_u512 *b,
                             const fio_u512 *N, const fio_u512 *N_dash);
void fio_u1024_montgomery_mul(fio_u1024 *result, const fio_u1024 *a, const fio_u1024 *b,
                              const fio_u1024 *N, const fio_u1024 *N_dash);
void fio_u2048_montgomery_mul(fio_u2048 *result, const fio_u2048 *a, const fio_u2048 *b,
                              const fio_u2048 *N, const fio_u2048 *N_dash);
```

The multiplication uses `fio_math_mul` from [`./002 math.h`](./002%20math.h).

## Shuffle and Reduce on Native Arrays

A separate family of helpers works on plain C arrays of fixed size. They are useful when the data is not wrapped in a `fio_uXXX` union.

### Shuffle

```c
void fio_u8x4_reshuffle  (uint8_t  *v, uint8_t indx[4]);
void fio_u8x8_reshuffle  (uint8_t  *v, uint8_t indx[8]);
void fio_u8x16_reshuffle (uint8_t  *v, uint8_t indx[16]);
void fio_u8x32_reshuffle (uint8_t  *v, uint8_t indx[32]);
void fio_u8x64_reshuffle (uint8_t  *v, uint8_t indx[64]);
void fio_u8x128_reshuffle(uint8_t  *v, uint8_t indx[128]);
void fio_u8x256_reshuffle(uint8_t  *v, uint8_t indx[256]);

void fio_u16x2_reshuffle (uint16_t *v, uint8_t indx[2]);
void fio_u16x4_reshuffle (uint16_t *v, uint8_t indx[4]);
void fio_u16x8_reshuffle (uint16_t *v, uint8_t indx[8]);
void fio_u16x16_reshuffle(uint16_t *v, uint8_t indx[16]);
void fio_u16x32_reshuffle(uint16_t *v, uint8_t indx[32]);
void fio_u16x64_reshuffle(uint16_t *v, uint8_t indx[64]);
void fio_u16x128_reshuffle(uint16_t *v, uint8_t indx[128]);

void fio_u32x2_reshuffle (uint32_t *v, uint8_t indx[2]);
void fio_u32x4_reshuffle (uint32_t *v, uint8_t indx[4]);
void fio_u32x8_reshuffle (uint32_t *v, uint8_t indx[8]);
void fio_u32x16_reshuffle(uint32_t *v, uint8_t indx[16]);
void fio_u32x32_reshuffle(uint32_t *v, uint8_t indx[32]);
void fio_u32x64_reshuffle(uint32_t *v, uint8_t indx[64]);

void fio_u64x2_reshuffle (uint64_t *v, uint8_t indx[2]);
void fio_u64x4_reshuffle (uint64_t *v, uint8_t indx[4]);
void fio_u64x8_reshuffle (uint64_t *v, uint8_t indx[8]);
void fio_u64x16_reshuffle(uint64_t *v, uint8_t indx[16]);
void fio_u64x32_reshuffle(uint64_t *v, uint8_t indx[32]);

void fio_floatx2_reshuffle (float  *v, uint8_t indx[2]);
void fio_floatx4_reshuffle (float  *v, uint8_t indx[4]);
void fio_floatx8_reshuffle (float  *v, uint8_t indx[8]);
void fio_floatx16_reshuffle(float  *v, uint8_t indx[16]);
void fio_floatx32_reshuffle(float  *v, uint8_t indx[32]);
void fio_floatx64_reshuffle(float  *v, uint8_t indx[64]);

void fio_dblx2_reshuffle  (double *v, uint8_t indx[2]);
void fio_dblx4_reshuffle  (double *v, uint8_t indx[4]);
void fio_dblx8_reshuffle  (double *v, uint8_t indx[8]);
void fio_dblx16_reshuffle (double *v, uint8_t indx[16]);
void fio_dblx32_reshuffle (double *v, uint8_t indx[32]);
```

Indices are masked with `len - 1`, so out-of-range indices wrap safely.

Variadic macro wrappers let you pass the index list inline:

```c
fio_u8x4_reshuffle(v, 3, 2, 1, 0);
fio_u32x4_reshuffle(v, 0, 2, 1, 3);
```

### Reduce

Integer reductions provide add, subtract, multiply, bitwise and/or/xor, and min/max:

```c
uint8_t  fio_u8x4_reduce_add (uint8_t  *v);  uint8_t  fio_u8x4_reduce_sub (uint8_t  *v);
uint8_t  fio_u8x4_reduce_mul (uint8_t  *v);  uint8_t  fio_u8x4_reduce_and (uint8_t  *v);
uint8_t  fio_u8x4_reduce_or  (uint8_t  *v);  uint8_t  fio_u8x4_reduce_xor (uint8_t  *v);
uint8_t  fio_u8x4_reduce_max (uint8_t  *v);  uint8_t  fio_u8x4_reduce_min (uint8_t  *v);

// Same pattern for u8 x 8, 16, 32, 64, 128, 256
// Same pattern for u16 x 2, 4, 8, 16, 32, 64, 128
// Same pattern for u32 x 2, 4, 8, 16, 32, 64
// Same pattern for u64 x 2, 4, 8, 16, 32
```

Floating-point reductions provide add, multiply, and min/max (no bitwise operations):

```c
float fio_floatx2_reduce_add(float *v);   float fio_floatx2_reduce_mul(float *v);
float fio_floatx2_reduce_max(float *v);   float fio_floatx2_reduce_min(float *v);

// Same pattern for float x 4, 8, 16, 32, 64
// Same pattern for dbl x 2, 4, 8, 16, 32
```

### Lane-wise operations on native arrays

Each integer reduction family also defines matching in-place binary operators on the arrays themselves:

```c
void fio_u8x4_add (uint8_t  *dest, uint8_t  *a, uint8_t  *b);
void fio_u8x4_sub (uint8_t  *dest, uint8_t  *a, uint8_t  *b);
void fio_u8x4_mul (uint8_t  *dest, uint8_t  *a, uint8_t  *b);
void fio_u8x4_and (uint8_t  *dest, uint8_t  *a, uint8_t  *b);
void fio_u8x4_or  (uint8_t  *dest, uint8_t  *a, uint8_t  *b);
void fio_u8x4_xor (uint8_t  *dest, uint8_t  *a, uint8_t  *b);

// Same pattern for all u8/u16/u32/u64 widths and lengths.
```

Floating-point array families define add and multiply only:

```c
void fio_floatx2_add(float *dest, float *a, float *b);
void fio_floatx2_mul(float *dest, float *a, float *b);

// Same pattern for float x 4, 8, 16, 32, 64 and dbl x 2, 4, 8, 16, 32.
```

## See Also

- [`./000 core.h`](./000%20core.h) — source of truth for all vector definitions.
- [`./000 core.md`](./000%20core.md) — core documentation index.
- `001 fx86.h` — fake/portable x86 intrinsics built on these types.
- [`./002 math.h`](./002%20math.h) — multi-precision `fio_math_mul` and related helpers.
# Version and Settings

The knobs that control how facil.io identifies itself and behaves at compile time.

For core types and everyday helpers, see [`./000 core.md`](./000%20core.md). This page covers version macros and core behavioral defaults; for per-inclusion hooks such as allocator routing and pointer tagging, see [`./001 header.h`](./001%20header.h).

## Version Macros

facil.io follows [semantic versioning](https://semver.org). These macros are defined in [`./000 core.h`](./000%20core.h) and can be used to detect the library version at compile time.

- **`FIO_VERSION_MAJOR`** — Major version. API/ABI breaking changes bump this.  
  Default: `0`
- **`FIO_VERSION_MINOR`** — Minor version. Significant features or deprecations. May break ABI.  
  Default: `8`
- **`FIO_VERSION_PATCH`** — Patch version. Bug fixes and small additions.  
  Default: `0`
- **`FIO_VERSION_BUILD`** — Optional build metadata, such as `"rc.02"`.  
  Default: `"rc.02"`
- **`FIO_VERSION_STRING`** — Full version as a string literal, built from the macros above. The guard is `#ifdef FIO_VERSION_BUILD`, so the build metadata is included whenever `FIO_VERSION_BUILD` is defined, including when defined as an empty string: `"0.8.0-rc.02"`. If `FIO_VERSION_BUILD` is undefined, the string reads `"0.8.0"`.  
  Because [`./000 core.h`](./000%20core.h) unconditionally `#define`s `FIO_VERSION_BUILD "rc.02"` before the `#ifdef` guard, omitting the suffix requires `#undef FIO_VERSION_BUILD` before inclusion, not merely leaving it undefined.

Override these only when you are vendoring a fork or need to report a custom build string.

## Compile-Time Behavioral Defaults

Most defaults are guarded with `#ifndef`; define them before including the header to override the default. `FIO_NO_LOG` and `DEBUG` are presence flags rather than `#ifndef` defaults.

### Memory and Safety

- **`FIO_LEAK_COUNTER`** — Enables memory-leak counting. When enabled, the leak counter tracks allocations made through facil.io allocators and prints a summary at exit.  
  Default: `1` (enabled).  
  Set to `0` to disable counting and the exit report.  
  Note: this conflicts with `FIO_NO_LOG`, because the leak report uses the logging layer.
- **`FIO_LEAK_COUNTER_SKIP_EXIT`** — Skip the automatic leak report at exit when leak counting is enabled.  
  Default: `0` (report at exit).
- **`FIO_LEAK_COUNTER_DEF(name)`** — Declares a named leak counter.
- **`FIO_LEAK_COUNTER_ON_ALLOC(name)`** — Records an allocation against the named counter.
- **`FIO_LEAK_COUNTER_ON_FREE(name)`** — Records a free against the named counter.
- **`FIO_LEAK_COUNTER_COUNT(name)`** — Returns the current value of the named counter.
- **`FIO_MEMORY_INITIALIZE_ALLOCATIONS_DEFAULT`** — When non-zero, facil.io allocators zero-initialize returned memory by default.  
  Default: `1` (secure by default).  
  Set to `0` if you want raw allocations and prefer to do your own initialization.
- **`FIO_MEM_PAGE_SIZE_LOG`** — Log₂ of the OS memory page size. The memory allocator uses this to round allocations and map memory efficiently.  
  Default: `12` (4096-byte pages).  
  Change it if you are targeting a system with a different page size.
- **`FIO_STATIC_ALLOC_SAFE_CONCURRENCY_MAX`** — Maximum safe concurrent calls for static allocators created with `FIO_STATIC_ALLOC_DEF`.  
  Default: `256`.  
  Raise it if you use many static allocator calls or threads and are seeing reused buffers.

### Concurrency

- **`FIO_USE_THREAD_MUTEX`** — Choose the locking primitive used by thread-safe modules.  
  Default: `0` (use facil.io spinlocks).  
  Set to `1` to use OS native mutexes (`pthread_mutex_t` on POSIX) instead.  
  If you need this decision to apply to only a single `#include`, use `FIO_USE_THREAD_MUTEX_TMP` in [`./001 header.h`](./001%20header.h) instead.

### Performance and Alignment

- **`FIO_UNALIGNED_ACCESS`** — Allows facil.io to attempt unaligned memory access on CPUs that support it. When enabled, the library may use faster paths in helpers such as `fio_buf2uXX`.  
  Default: `1` (attempt detection).  
  Set to `0` on strict-alignment architectures or if you want to forbid unaligned access entirely. The actual capability is reflected by `FIO_UNALIGNED_MEMORY_ACCESS_ENABLED`, which is derived at compile time.
- **`FIO_LIMIT_INTRINSIC_BUFFER`** — Limits register pressure in some pseudo-intrinsic loops by processing smaller batches and looping more.  
  Default: `1`.  
  Set to `0` if you prefer larger unrolled buffers and do not mind the extra register use.

### Maps

- **`FIO_MAP_WARNING_BITSIZE`** — Bit size at which hash-map allocations trigger an internal warning threshold in the `imap`/`map` templates.  
  Default: `24`.  
  Raise or lower it to tune when the library starts worrying about map size during growth.

### Logging

- **`FIO_NO_LOG`** — Define this to remove all logging output. Cannot be combined with `FIO_LEAK_COUNTER`; the two are mutually exclusive.
- **`FIO_LOG2STDERR`** — Low-level sink used by the logging macros.  
  Default: no-op.  
  Override to print to `stderr` or route messages to a custom destination.
- **`FIO_LOG_LENGTH_LIMIT`** — Approximate cap on a single formatted log message, limiting stack memory use.  
  Default: `1024`.
- **`DEBUG`** — Define this to enable debug-only log macros (`FIO_LOG_D*`) and active `FIO_ASSERT_DEBUG` checks. Also disables SIMD/Neon intrinsics in [`./000 core.h`](./000%20core.h).

### Memory allocator routing

These macros are defined in [`./001 header.h`](./001%20header.h) and are re-evaluated on every inclusion cycle. Define them before the first fio-stl include to replace memory routing globally.

- **`FIO_MEM_REALLOC(ptr, old_size, new_size, copy_len)`** — Reallocate a block, copying at least `copy_len` bytes if a move is required. Defaults to the system `realloc` unless the facil.io allocator is available, in which case it routes to `fio_realloc2`. Override to plug in a custom allocator.
- **`FIO_MEM_FREE(ptr, size)`** — Free a block. Defaults to the system `free` or `fio_free`. Override alongside `FIO_MEM_REALLOC`.
- **`FIO_MEMORY_DISABLE`** — Define to disable all custom facil.io allocators and force system `malloc`/`free` for temporary allocations.
- **`FIO_MALLOC_TMP_USE_SYSTEM`** — Force the recursive allocator variant to use the system `realloc`/`free`. Useful when a custom `FIO_MEM_REALLOC` needs an allocation path that will not recurse back into facil.io.
- **`FIO_MEM_REALLOC_` / `FIO_MEM_FREE_`** — Recursive-safe copies of `FIO_MEM_REALLOC`/`FIO_MEM_FREE`. Override these when the custom allocator itself needs to allocate or free memory without re-entering the override.

### Pointer tagging

These macros are defined in [`./001 header.h`](./001%20header.h) and let included types store a small marker in the unused low bits of aligned pointers.

- **`FIO_PTR_TAG(p)`** — Apply the tag to a pointer. Default is the identity function.
- **`FIO_PTR_UNTAG(p)`** — Remove the tag from a pointer. Default is the identity function.
- **`FIO_PTR_TAG_TYPE`** — If defined, functions returning a type's pointer return this type instead.
- **`FIO_PTR_TAG_VALIDATE(ptr)`** — Returns a true value if the tagged pointer is valid. Default checks for non-`NULL`.
- **`FIO_PTR_TAG_VALID_OR_RETURN(tagged_ptr, value)`** — Validates the tagged pointer and returns `value` if it fails validation.
- **`FIO_PTR_TAG_VALID_OR_RETURN_VOID(tagged_ptr)`** — Validates the tagged pointer and returns `void` if it fails validation.
- **`FIO_PTR_TAG_VALID_OR_GOTO(tagged_ptr, label)`** — Validates the tagged pointer and jumps to `label` if it fails validation.
- **`FIO_PTR_TAG_GET_UNTAGGED(untagged_type, tagged_ptr)`** — Cast the untagged pointer to `untagged_type *`.

Pointer tagging is used by dynamic-type wrappers (for example, FIOBJ) to distinguish object kinds without a separate type field.

## See Also

- [`./000 core.md`](./000%20core.md) — documentation for core types and helpers.
- [`./000 core.h`](./000%20core.h) — the source of truth for version macros and the defaults above.
- [`./001 header.h`](./001%20header.h) — per-inclusion overrides such as `FIO_USE_THREAD_MUTEX_TMP` and allocator routing macros.
# String / Number Conversion

```c
#define FIO_ATOL
#include "fio-stl.h"
```

String-to-number and number-to-string helpers. These are the grunts that parse integers, floats, hex, binary, and arbitrary bases, then turn them back into text. Fast, greedy, and mostly guard-less — give them a valid buffer and a terminating character.

**Note:** functions that write to a buffer also write a NUL terminator. `fio_atol*` functions assume the buffer ends with an invalid character (such as NUL) and that allocations are aligned enough for multi-byte reads.

### Configuration Macros

#### `FIO_ATOL_ALLOW_UNDERSCORE_DIVIDER`

```c
#define FIO_ATOL_ALLOW_UNDERSCORE_DIVIDER 1
```

When `1` (default), underscores act as digit separators: `1_000_000` parses as `1000000`. Set to `0` to disable.

### Types

#### `fio_aton_s`

```c
typedef struct {
  union {
    int64_t i;
    double f;
    uint64_t u;
  };
  int is_float;
  int err;
} fio_aton_s;
```

Result container for `fio_aton`. Read the union member that matches `is_float`, and check `err` for overflow or parse failures.

### Universal Parsing

#### `fio_aton`

```c
FIO_SFUNC fio_aton_s fio_aton(char **pstr);
```

Auto-detects integers and floats. Skips leading whitespace, recognizes `0x` / `0b` / octal prefixes, and accepts `inf`, `infinity`, and `nan`. Updates `*pstr` to the first unconverted character. Sets `.err` on overflow or bad format.

**Note:** not an exact `strtod` replacement; rounding differences are possible.

### Signed Conversion

#### `fio_atol`

```c
SFUNC int64_t fio_atol(char **pstr);
```

Parses a signed `int64_t`. Accepts base 10, octal (`0...`), hex (`0x...` or `x...`), and binary (`0b...` or `b...`). Updates `*pstr` past the number.

#### `fio_atof`

```c
SFUNC double fio_atof(char **pstr);
```

Parses a double. Wraps `strtod` for most inputs. The source also attempts to accept a raw `0b...` binary bit-pattern, but the detection condition looks fragile.

#### `fio_ftoa`

```c
SFUNC size_t fio_ftoa(char *dest, double num, uint8_t base);
```

Writes `num` to `dest` in `base` (2, 10, or 16; unsupported bases silently fall back to 10). No prefixes are added. Returns bytes written excluding NUL.

**Note:** provide at least 130 bytes for base 2. Special values `inf` and `nan` produce `"Infinity"` / `"NaN"`.

#### `fio_ltoa`

```c
SFUNC size_t fio_ltoa(char *dest, int64_t num, uint8_t base);
```

Writes `num` to `dest` in `base` (2, 8, 10, 16, or any base up to 36). Adds `0x`, `0b`, or `0` prefixes for the built-in bases. Returns bytes written excluding NUL. If `dest` is `NULL`, writes to an internal scratch buffer and still returns the length. Logs an error and returns `0` for unsupported bases.

**Note:** provide at least 68 bytes for base 2.

#### `fio_ltoa10`

```c
FIO_IFUNC void fio_ltoa10(char *dest, int64_t i, size_t digits);
```

Writes a signed base-10 number using exactly `digits` bytes plus NUL. Use `fio_digits10()` to compute `digits`.

#### `fio_atol10`

```c
SFUNC int64_t fio_atol10(char **pstr);
```

Reads a signed base-10 number.

### Unsigned Conversion

#### `fio_atol8u`

```c
SFUNC uint64_t fio_atol8u(char **pstr);
```

Reads an unsigned octal number. May overflow the buffer if no terminator is present.

#### `fio_atol10u`

```c
SFUNC uint64_t fio_atol10u(char **pstr);
```

Reads an unsigned base-10 number.

#### `fio_atol16u`

```c
SFUNC uint64_t fio_atol16u(char **pstr);
```

Reads an unsigned hex number, with optional `0x` prefix.

#### `fio_atol_bin`

```c
SFUNC uint64_t fio_atol_bin(char **pstr);
```

Reads an unsigned binary number, with optional `0b` prefix.

#### `fio_atol_xbase`

```c
SFUNC uint64_t fio_atol_xbase(char **pstr, size_t base);
```

Reads an unsigned number in any base up to 36.

#### `fio_ltoa8u`

```c
FIO_IFUNC void fio_ltoa8u(char *dest, uint64_t i, size_t digits);
```

Writes an unsigned octal number using `digits` bytes plus NUL.

#### `fio_ltoa10u`

```c
FIO_IFUNC void fio_ltoa10u(char *dest, uint64_t i, size_t digits);
```

Writes an unsigned base-10 number using `digits` bytes plus NUL.

#### `fio_ltoa16u`

```c
FIO_IFUNC void fio_ltoa16u(char *dest, uint64_t i, size_t digits);
```

Writes an unsigned hex number using `digits` bytes plus NUL. `digits` is rounded up to an even number.

#### `fio_ltoa_bin`

```c
FIO_IFUNC void fio_ltoa_bin(char *dest, uint64_t i, size_t digits);
```

Writes an unsigned binary number using `digits` bytes plus NUL.

#### `fio_ltoa_xbase`

```c
FIO_IFUNC void fio_ltoa_xbase(char *dest,
                              uint64_t i,
                              size_t digits,
                              size_t base);
```

Writes an unsigned number in `base` (up to 36) using `digits` bytes plus NUL.

### Helpers

#### `fio_c2i`

```c
IFUNC uint8_t fio_c2i(unsigned char c);
```

Maps a character to its numeric value (`0-9` → 0-9, `A-Z`/`a-z` → 10-35). Returns `255` for out-of-range characters.

#### `fio_i2c`

```c
IFUNC uint8_t fio_i2c(unsigned char i);
```

Maps a numeric value `0-35` to a character (`0-9`, `A-Z`). Out-of-range values above 35 produce undefined behavior; accepts values up to 63 by masking.

#### `fio_digits10`

```c
FIO_IFUNC size_t fio_digits10(int64_t i);
```

Returns the number of base-10 digits needed for `i`, including the sign.

#### `fio_digits10u`

```c
FIO_SFUNC size_t fio_digits10u(uint64_t i);
```

Returns the number of base-10 digits needed for an unsigned number.

#### `fio_digits8u`

```c
FIO_SFUNC size_t fio_digits8u(uint64_t i);
```

Returns the number of base-8 digits needed for an unsigned number.

#### `fio_digits16u`

```c
FIO_SFUNC size_t fio_digits16u(uint64_t i);
```

Returns the number of base-16 digits needed for an unsigned number, always an even count (2, 4, 6, ... 16).

#### `fio_digits_bin`

```c
FIO_SFUNC size_t fio_digits_bin(uint64_t i);
```

Returns the number of base-2 digits needed for an unsigned number, rounded up to an even count.

#### `fio_digits_xbase`

```c
FIO_SFUNC size_t fio_digits_xbase(uint64_t i, size_t base);
```

Returns the number of digits needed for an unsigned number in `base` (must be < 65).

#### `fio_u2i_limit`

```c
FIO_IFUNC int64_t fio_u2i_limit(uint64_t val, size_t invert);
```

Converts unsigned `val` to signed with overflow protection. If `invert` is zero, clamps to `INT64_MAX` and sets `errno = E2BIG` on overflow. If `invert` is non-zero, produces the negative value and clamps to `INT64_MIN`.

### IEEE 754 Helpers

#### `fio_i2d`

```c
FIO_IFUNC double fio_i2d(int64_t mant, int64_t exponent_in_base_2);
```

Converts a signed 64-bit mantissa and base-2 exponent to a `double`.

#### `fio_u2d`

```c
FIO_IFUNC double fio_u2d(uint64_t mant, int64_t exponent_in_base_2);
```

Converts an unsigned 64-bit mantissa and base-2 exponent to a `double`.

### Big Number Hex Conversion

#### `fio_u128_hex_read` / `fio_u256_hex_read` / `fio_u512_hex_read`

```c
SFUNC fio_u128 fio_u128_hex_read(char **pstr);
SFUNC fio_u256 fio_u256_hex_read(char **pstr);
SFUNC fio_u512 fio_u512_hex_read(char **pstr);
```

Reads a hex string and initializes the corresponding wide integer. Updates `*pstr` past the consumed input.

#### `fio_u1024_hex_read` / `fio_u2048_hex_read` / `fio_u4096_hex_read`

```c
SFUNC fio_u1024 fio_u1024_hex_read(char **pstr);
SFUNC fio_u2048 fio_u2048_hex_read(char **pstr);
SFUNC fio_u4096 fio_u4096_hex_read(char **pstr);
```

Same as above for 1024-, 2048-, and 4096-bit integers.

#### `fio_u128_hex_write` / `fio_u256_hex_write` / `fio_u512_hex_write`

```c
SFUNC size_t fio_u128_hex_write(char *dest, const fio_u128 *u);
SFUNC size_t fio_u256_hex_write(char *dest, const fio_u256 *u);
SFUNC size_t fio_u512_hex_write(char *dest, const fio_u512 *u);
```

Writes a wide integer as a hex string to `dest`. Returns bytes written excluding NUL.

#### `fio_u1024_hex_write` / `fio_u2048_hex_write` / `fio_u4096_hex_write`

```c
SFUNC size_t fio_u1024_hex_write(char *dest, const fio_u1024 *u);
SFUNC size_t fio_u2048_hex_write(char *dest, const fio_u2048 *u);
SFUNC size_t fio_u4096_hex_write(char *dest, const fio_u4096 *u);
```

Same as above for 1024-, 2048-, and 4096-bit integers.

### Example

```c
#define FIO_ATOL
#include "fio-stl.h"

int main(void) {
  char *p = "0x1F 0b1010 42";
  char buf[80];

  printf("hex: %lld\n", (long long)fio_atol(&p));
  ++p; /* skip space */
  printf("bin: %lld\n", (long long)fio_atol(&p));
  ++p;
  printf("dec: %lld\n", (long long)fio_atol(&p));

  fio_ltoa(buf, -255, 16);
  printf("back to hex: %s\n", buf);
  return 0;
}
```

------------------------------------------------------------
# CRC32

```c
#define FIO_CRC32
#include "fio-stl.h"
```

Fast CRC32 using the ITU-T V.42 / ISO 3309 / gzip polynomial (`0xEDB88320`). Useful for gzip, PNG, and anything else that expects this particular CRC32 flavor.

The implementation picks the best available path at compile time:

- ARM aarch64 with the CRC32 extension uses scalar CRC32 instructions, with a PMULL bulk-fold path when the crypto / PMULL feature is available.
- x86 / x64 with SSE4.2 and PCLMULQDQ uses a PCLMULQDQ bulk path and the software tail path.
- Other builds use the always-present slicing-by-16 software fallback.

**Note:** this is **not** CRC32-C (Castagnoli, polynomial `0x82F63B78`). The ARM scalar path uses the gzip-polynomial CRC32 instructions (`__crc32b`, `__crc32w`, `__crc32d`), not the Castagnoli variants.

### API Functions

#### `fio_crc32`

```c
SFUNC uint32_t fio_crc32(const void *data, size_t len, uint32_t initial_crc);
```

Computes the CRC32 of `len` bytes at `data`. Pass `0` for `initial_crc` to start fresh, or pass a previous result to continue an incremental checksum across multiple buffers.

**Returns:** the CRC32 checksum.

### Example — single buffer

```c
#define FIO_CRC32
#include "fio-stl.h"

int main(void) {
  const char *msg = "Hello, World!";
  uint32_t crc = fio_crc32(msg, strlen(msg), 0);
  printf("CRC32: 0x%08X\n", crc);
  return 0;
}
```

### Example — incremental

```c
uint32_t crc = 0;
crc = fio_crc32("Hello, ", 7, crc);
crc = fio_crc32("World!", 6, crc);
/* crc matches a single call over "Hello, World!" */
```

------------------------------------------------------------
# Glob Matching

```c
#define FIO_GLOB_MATCH
#include "fio-stl.h"
```

Binary glob matching for wildcard patterns. Bytes in, bytes out — no UTF-8 awareness, no allocation, just `*`, `?`, `[...]`, and escapes.

### API Functions

#### `fio_glob_match`

```c
SFUNC uint8_t fio_glob_match(fio_str_info_s pattern, fio_str_info_s string);
```

Returns `1` if `string` matches `pattern`, otherwise `0`.

### Supported Patterns

#### `*`

Matches any byte sequence, including an empty one.

#### `?`

Matches any single byte.

#### `[...]`

Matches any single byte inside the brackets. Ranges work with `-`, e.g. `[a-z]`. The first character may be `]` without closing the class. Use `\` to escape `]`, `-`, and `\` itself.

#### `[!...]` or `[^...]`

Inverted character class: matches any single byte **not** in the brackets.

#### `\`

Escapes the next character so it is matched literally.

### Example

```c
#define FIO_GLOB_MATCH
#define FIO_STR
#include "fio-stl.h"

int main(void) {
  printf("%d\n", fio_glob_match(FIO_STR_INFO1("*.txt"),
                                FIO_STR_INFO1("notes.txt")));      /* 1 */
  printf("%d\n", fio_glob_match(FIO_STR_INFO1("log_????"),
                                FIO_STR_INFO1("log_2024")));       /* 1 */
  printf("%d\n", fio_glob_match(FIO_STR_INFO1("[!0-9]*"),
                                FIO_STR_INFO1("abc123")));         /* 1 */
  printf("%d\n", fio_glob_match(FIO_STR_INFO1("file\\*"),
                                FIO_STR_INFO1("file*")));          /* 1 */
  return 0;
}
```

------------------------------------------------------------
# Index Mapped Array (iMap)

```c
#define FIO_IMAP_CORE
#include "fio-stl.h"
```

A macro-generated hash-map-backed array. Keeps insertion order in a dense array while the index map provides near-O(1) lookups. Mostly used internally by other STL modules; for public map needs see [`FIO_MAP_NAME`](./210%20map.md).

**Note:** iMap does not manage object lifetimes. Clean up stored resources before calling destroy.

### Configuration Macros

#### `FIO_TYPEDEF_IMAP_REALLOC`

```c
#ifndef FIO_TYPEDEF_IMAP_REALLOC
#define FIO_TYPEDEF_IMAP_REALLOC FIO_MEM_REALLOC
#endif
```

Allocator used for iMap growth. Defaults to `FIO_MEM_REALLOC`.

#### `FIO_TYPEDEF_IMAP_REALLOC_IS_SAFE`

```c
#ifndef FIO_TYPEDEF_IMAP_REALLOC_IS_SAFE
#define FIO_TYPEDEF_IMAP_REALLOC_IS_SAFE FIO_MEM_REALLOC_IS_SAFE
#endif
```

Set if the realloc zeroes new memory; otherwise iMap zeroes it explicitly.

#### `FIO_TYPEDEF_IMAP_FREE`

```c
#ifndef FIO_TYPEDEF_IMAP_FREE
#define FIO_TYPEDEF_IMAP_FREE FIO_MEM_FREE
#endif
```

Deallocator used by `array_name_destroy`.

### Helper Macros

#### `FIO_IMAP_ALWAYS_VALID`

```c
#define FIO_IMAP_ALWAYS_VALID(o) (1)
```

Validity helper that treats every object as valid.

#### `FIO_IMAP_VALID_NON_ZERO`

```c
#define FIO_IMAP_VALID_NON_ZERO(o) (!!((o)[0]))
```

Validity helper that treats an object as valid if its first element is non-zero.

#### `FIO_IMAP_ALWAYS_CMP_TRUE` / `FIO_IMAP_ALWAYS_CMP_FALSE`

```c
#define FIO_IMAP_ALWAYS_CMP_TRUE(a, b) (1)
#define FIO_IMAP_ALWAYS_CMP_FALSE(a, b) (0)
```

Comparison helpers that always return true or false.

#### `FIO_IMAP_SIMPLE_CMP`

```c
#define FIO_IMAP_SIMPLE_CMP(a, b) ((a)[0] == (b)[0])
```

Compares two objects by their first element.

#### `FIO_IMAP_EACH`

```c
#define FIO_IMAP_EACH(array_name, map_ptr, i)                          \
  for (size_t i = 0; i < (map_ptr)->w; ++i)                             \
    if (!FIO_NAME(array_name, is_valid)((map_ptr)->ary + i))            \
      continue;                                                        \
    else
```

Iterates valid elements in insertion order. Use it as a loop header:

```c
FIO_IMAP_EACH(my_map, &map, i) {
  printf("%llu\n", (unsigned long long)map.ary[i].key);
}
```

### Type Definition Macro

#### `FIO_TYPEDEF_IMAP_ARRAY`

```c
#define FIO_TYPEDEF_IMAP_ARRAY(array_name,                            \
                               array_type,                            \
                               imap_type,                             \
                               hash_fn,                               \
                               cmp_fn,                                \
                               is_valid_fn)
```

Generates the iMap container type and functions prefixed with `array_name`. The callbacks take **pointers** to elements:

- `hash_fn(pobj)` returns an `imap_type` hash.
- `cmp_fn(a_ptr, b_ptr)` returns non-zero on match.
- `is_valid_fn(pobj)` returns non-zero if the element is valid.

Reserved index-map values: `0` means empty, `~0` means freed.

### Generated Types

#### `array_name_s`

```c
typedef struct {
  array_type *ary;
  imap_type count;
  imap_type w;
  uint32_t capa_bits;
} array_name_s;
```

- `ary` — dense array of elements.
- `count` — number of valid (non-removed) elements.
- `w` — next write position / logical size.
- `capa_bits` — log2 of capacity; actual capacity is `1 << capa_bits`.

#### `array_name_seeker_s`

```c
typedef struct {
  imap_type pos;
  imap_type ipos;
  imap_type set_val;
} array_name_seeker_s;
```

- `pos` — array index, or `w` if not found.
- `ipos` — index-map slot, or `~0` if no room.
- `set_val` — value to write into the index map on insert.

### Generated Functions

#### `array_name_is_valid`

```c
FIO_IFUNC int array_name_is_valid(array_type *pobj);
```

Wraps the `is_valid_fn` supplied to the macro.

#### `array_name_capa`

```c
FIO_IFUNC size_t array_name_capa(array_name_s *a);
```

Returns `1 << a->capa_bits`, or `0` if not allocated.

#### `array_name_imap`

```c
FIO_IFUNC imap_type *array_name_imap(array_name_s *a);
```

Returns a pointer to the index map stored immediately after the array.

#### `array_name_destroy`

```c
FIO_IFUNC void array_name_destroy(array_name_s *a);
```

Frees the array + index map and zeros the container. Does not run destructors on elements.

#### `array_name_seek`

```c
FIO_SFUNC array_name_seeker_s array_name_seek(array_name_s *a,
                                              array_type *pobj);
```

Finds an element or the place to insert it.

#### `array_name_reserve`

```c
FIO_IFUNC int array_name_reserve(array_name_s *a, imap_type min);
```

Grows the backing storage to at least `min` slots. Returns `0` on success, `-1` on failure.

#### `array_name_rehash`

```c
FIO_IFUNC int array_name_rehash(array_name_s *a);
```

Rebuilds the index map from the current array contents. Call after sorting or other position-changing operations. Returns `0` on success, `-1` on failure.

#### `array_name_set`

```c
FIO_IFUNC array_type *array_name_set(array_name_s *a,
                                     array_type obj,
                                     int overwrite);
```

Inserts or updates an element. If `overwrite` is zero and the key already exists, returns the existing element. Returns a pointer to the stored element or `NULL` on failure.

#### `array_name_get`

```c
FIO_IFUNC array_type *array_name_get(array_name_s *a, array_type obj);
```

Looks up an element. Returns a pointer to the match or `NULL`.

#### `array_name_remove`

```c
FIO_IFUNC int array_name_remove(array_name_s *a, array_type obj);
```

Zeros the matching element and marks its index-map slot as freed. Returns `0` on success, `-1` if not found.

### Internal Seeker Types

#### `fio___imapN_seeker_s`

```c
typedef struct {
  uintN_t pos;
  uintN_t ipos;
  uintN_t set_val;
  bool is_valid;
} fio___imapN_seeker_s;
```

Where `N` is `8`, `16`, `32`, or `64`. Low-level seeker used by other STL internals.

#### `fio___imapN_seek`

```c
FIO_SFUNC fio___imapN_seeker_s fio___imapN_seek(
    void *ary,
    uintN_t *imap,
    const uintN_t capa_bits,
    void *pobj,
    uintN_t hash,
    bool cmp_fn(void *arry, void *obj, uintN_t indx),
    const size_t max_attempts);
```

Low-level seek through an index map. Generally used internally.

#### `fio___imapN_set`

```c
FIO_IFUNC void fio___imapN_set(uintN_t *imap,
                               uintN_t ipos,
                               uintN_t set_val);
```

Writes `set_val` into `imap[ipos]`.

### Example

```c
#define FIO_IMAP_CORE
#include "fio-stl.h"

typedef struct { uint64_t key; int value; } kv_s;

static uint64_t kv_hash(kv_s *p)   { return fio_risky_num(p->key, 0); }
static int      kv_cmp(kv_s *a, kv_s *b) { return a->key == b->key; }
static int      kv_valid(kv_s *p)  { return p->key != 0; }

FIO_TYPEDEF_IMAP_ARRAY(kv, kv_s, uint32_t, kv_hash, kv_cmp, kv_valid)

int main(void) {
  kv_s map = {0};
  kv_set(&map, (kv_s){.key = 1, .value = 100}, 1);
  kv_set(&map, (kv_s){.key = 2, .value = 200}, 1);

  kv_s *found = kv_get(&map, (kv_s){.key = 2});
  if (found) printf("value: %d\n", found->value);

  FIO_IMAP_EACH(kv, &map, i) {
    printf("%llu -> %d\n",
           (unsigned long long)map.ary[i].key, map.ary[i].value);
  }

  kv_remove(&map, (kv_s){.key = 2});
  kv_destroy(&map);
  return 0;
}
```

------------------------------------------------------------
# Multi-Precision Math

```c
#define FIO_MATH
#include "fio-stl.h"
```

Extra multi-precision operations on little-endian `uint64_t` arrays: division, shifts, two's-complement inverse, and bit-index queries. The core add/sub/mul helpers (`fio_math_add`, `fio_math_sub`, `fio_math_mul`, `fio_math_addc64`, etc.) and the wide union types (`fio_u128`, `fio_u256`, …) live in [`000 core.h`](./000%20core.md) and do not require `FIO_MATH`.

**Note:** this module assumes the CPU performs 64-bit multiply in constant time, which is not guaranteed on all hardware.

### API Functions

#### `fio_math_div`

```c
FIO_IFUNC void fio_math_div(uint64_t *dest,
                            uint64_t *reminder,
                            const uint64_t *a,
                            const uint64_t *b,
                            const size_t number_array_length);
```

Divides the `number_array_length`-word number `a` by `b`, storing the quotient in `dest` and the remainder in `reminder`. Either output may be `NULL` if you only need the other one.

**Note:** not constant time. Uses binary long division, which is simple but not the fastest algorithm on earth.

If `b` is zero, logs an error and fills both outputs with `0xFF`.

#### `fio_math_shr`

```c
FIO_IFUNC void fio_math_shr(uint64_t *dest,
                            uint64_t *n,
                            const size_t right_shift_bits,
                            size_t number_array_length);
```

Shifts `n` right by `right_shift_bits` and stores the result in `dest`.

#### `fio_math_shl`

```c
FIO_IFUNC void fio_math_shl(uint64_t *dest,
                            uint64_t *n,
                            const size_t left_shift_bits,
                            const size_t number_array_length);
```

Shifts `n` left by `left_shift_bits` and stores the result in `dest`.

#### `fio_math_inv`

```c
FIO_IFUNC void fio_math_inv(uint64_t *dest, uint64_t *n, size_t len);
```

Two's-complement negation of the `len`-word number `n`.

#### `fio_math_msb_index`

```c
FIO_MIFN size_t fio_math_msb_index(uint64_t *n, const size_t len);
```

Returns the zero-based index of the most significant set bit, or `(size_t)-1` if the number is zero. Useful for measuring bit length.

#### `fio_math_lsb_index`

```c
FIO_MIFN size_t fio_math_lsb_index(uint64_t *n, const size_t len);
```

Returns the zero-based index of the least significant set bit, or `(size_t)-1` if the number is zero.

### Example

```c
#define FIO_MATH
#include "fio-stl.h"

int main(void) {
  uint64_t a[2] = {0, 1};          /* 2^64 */
  uint64_t b[2] = {3, 0};          /* 3 */
  uint64_t q[2] = {0};
  uint64_t r[2] = {0};

  fio_math_div(q, r, a, b, 2);
  printf("quot: %016llX%016llX\n",
         (unsigned long long)q[1], (unsigned long long)q[0]);
  printf("rem:  %016llX%016llX\n",
         (unsigned long long)r[1], (unsigned long long)r[0]);
  return 0;
}
```

------------------------------------------------------------
# Pseudo-Random and Hashing

```c
#define FIO_RAND
#include "fio-stl.h"
```

A non-cryptographic pseudo-random generator plus RiskyHash and Stable Hash. The PRNG is seeded from system jitter (`getrusage`, clocks) and hashed with RiskyHash. It is faster and more random than typical `rand()` implementations, but **do not use it for cryptography** — use `fio_rand_bytes_secure` for that.

RiskyHash is an ephemeral, non-cryptographic hash family that may change between releases. Stable Hash is frozen and safe for persistent data. RiskyHash 256/512 and HMAC variants are also provided.

### Pseudo-Random Functions

#### `fio_rand64`

```c
SFUNC uint64_t fio_rand64(void);
```

Returns 64 pseudo-random bits.

#### `fio_rand128`

```c
SFUNC fio_u128 fio_rand128(void);
```

Returns 128 pseudo-random bits as a `fio_u128`.

#### `fio_rand_bytes`

```c
SFUNC void fio_rand_bytes(void *target, size_t len);
```

Fills `target` with `len` pseudo-random bytes.

#### `fio_rand_bytes_secure`

```c
SFUNC int fio_rand_bytes_secure(void *target, size_t len);
```

Fills `target` with `len` cryptographically secure random bytes from the system CSPRNG (`arc4random_buf` on BSD/macOS, `/dev/urandom` fallback elsewhere). Returns `0` on success, `-1` on failure.

Use this for keys, nonces, and anything security-sensitive.

#### `fio_rand_reseed`

```c
SFUNC void fio_rand_reseed(void);
```

Rotates the PRNG state with fresh entropy. Call after `fork` to avoid duplicated sequences.

### Risky / Stable Hash

#### `FIO_USE_STABLE_HASH_WHEN_CALLING_RISKY_HASH`

```c
#define FIO_USE_STABLE_HASH_WHEN_CALLING_RISKY_HASH 0
```

When set to `1`, `fio_risky_hash` redirects to `fio_stable_hash`.

#### `fio_risky_hash`

```c
SFUNC uint64_t fio_risky_hash(const void *buf, size_t len, uint64_t seed);
```

64-bit RiskyHash v.3. Ephemeral — hash values may change in future releases. Good for hash-map keys, not for storage.

#### `fio_risky_ptr`

```c
FIO_IFUNC uint64_t fio_risky_ptr(void *ptr);
```

Fast entropy mix for pointer values.

#### `fio_risky_num`

```c
FIO_IFUNC uint64_t fio_risky_num(uint64_t number, uint64_t seed);
```

Fast entropy mix for integer values.

#### `fio_stable_hash`

```c
SFUNC uint64_t fio_stable_hash(const void *data, size_t len, uint64_t seed);
```

64-bit Stable Hash. Frozen after 1.0; safe for on-disk or cross-system use.

#### `fio_stable_hash128`

```c
SFUNC void fio_stable_hash128(void *restrict dest,
                              const void *restrict data,
                              size_t len,
                              uint64_t seed);
```

Writes a 128-bit Stable Hash into `dest` (16 bytes).

#### `fio_risky256`

```c
SFUNC fio_u256 fio_risky256(const void *data, uint64_t len);
```

256-bit RiskyHash. Based on the A3 (Zero-Copy ILP) design: two 512-bit states, 128 bytes per iteration, multiply-fold with cross-lane mixing. Returns a `fio_u256`.

#### `fio_risky512`

```c
SFUNC fio_u512 fio_risky512(const void *data, uint64_t len);
```

512-bit RiskyHash. The first 256 bits are identical to `fio_risky256`; the rest come from an extra squeeze round.

#### `fio_risky256_hmac`

```c
SFUNC fio_u256 fio_risky256_hmac(const void *key,
                                 uint64_t key_len,
                                 const void *msg,
                                 uint64_t msg_len);
```

RFC 2104 HMAC using `fio_risky256` as the hash, 64-byte block size. Keys longer than 64 bytes are hashed first. Securely zeros intermediates.

**Note:** the underlying hash is non-cryptographic; standard HMAC security proofs do not apply. Use Blake2 for cryptographic HMAC.

#### `fio_risky512_hmac`

```c
SFUNC fio_u512 fio_risky512_hmac(const void *key,
                                 uint64_t key_len,
                                 const void *msg,
                                 uint64_t msg_len);
```

Same construction as `fio_risky256_hmac`, but with `fio_risky512` and a 64-byte digest.

### Example

```c
#define FIO_RAND
#include "fio-stl.h"

int main(void) {
  printf("rand64: %016llX\n", (unsigned long long)fio_rand64());

  uint8_t key[32];
  fio_rand_bytes_secure(key, sizeof(key));

  uint64_t h = fio_stable_hash("hello", 5, 0);
  printf("stable: %016llX\n", (unsigned long long)h);

  fio_u256 r = fio_risky256("hello", 5);
  printf("risky256: %016llX...\n",
         (unsigned long long)r.u64[0]);
  return 0;
}
```

------------------------------------------------------------
# Signal Monitoring

```c
#define FIO_SIGNAL
#include "fio-stl.h"
```

A small wrapper around OS signals. It sets a handler, records that the signal fired, and lets you call the real callback later from normal execution context — the safe pattern, since signal handlers are heavily restricted. POSIX and Windows are supported.

### Configuration Macros

#### `FIO_SIGNAL_MONITOR_MAX`

```c
#ifndef FIO_SIGNAL_MONITOR_MAX
#define FIO_SIGNAL_MONITOR_MAX 24
#endif
```

Maximum number of signals that can be monitored at once.

### Portable Signal Aliases

#### `FIO_SIGNAL_USER1` / `FIO_SIGNAL_USER2` / `FIO_SIGNAL_USER_UNREGISTERED`

```c
#if FIO_OS_POSIX
#define FIO_SIGNAL_USER1             SIGUSR1
#define FIO_SIGNAL_USER2             SIGUSR2
#define FIO_SIGNAL_USER_UNREGISTERED SIGUSR2
#elif FIO_OS_WIN
#define FIO_SIGNAL_USER1             SIGBREAK
#define FIO_SIGNAL_USER2             SIGABRT
#define FIO_SIGNAL_USER_UNREGISTERED SIGBREAK
#endif
```

Maps user signals across POSIX and Windows.

### Types

#### `fio_signal_monitor_args_s`

```c
typedef struct {
  int sig;
  void (*callback)(int sig, void *udata);
  void *udata;
  bool propagate;
  bool immediate;
} fio_signal_monitor_args_s;
```

- `sig` — signal number to watch.
- `callback` — function to run; `NULL` ignores the signal.
- `udata` — opaque pointer passed to the callback.
- `propagate` — if true, call any previous handler after recording the signal.
- `immediate` — if true, run the callback inside the signal handler; otherwise defer to `fio_signal_review`.

### API Functions

#### `fio_signal_monitor`

```c
SFUNC int fio_signal_monitor(fio_signal_monitor_args_s args);
#define fio_signal_monitor(...)                                                \
  fio_signal_monitor((fio_signal_monitor_args_s){__VA_ARGS__})
```

Starts monitoring `sig`. Updating an existing monitor replaces its callback and `udata`. Returns `0` on success, `-1` on error.

**Note:** `immediate` callbacks must be async-signal-safe. Most code is not.

#### `fio_signal_review`

```c
SFUNC int fio_signal_review(void);
```

Calls deferred callbacks for any signals that fired since the last review. Returns the number of callbacks invoked.

#### `fio_signal_forget`

```c
SFUNC int fio_signal_forget(int sig);
```

Stops monitoring `sig` and restores the default handler. Returns `0` on success, `-1` on error.

### Example

```c
#define FIO_SIGNAL
#define FIO_LOG
#include "fio-stl.h"

static volatile int running = 1;

static void on_sigint(int sig, void *udata) {
  (void)sig; (void)udata;
  FIO_LOG_INFO("SIGINT received, shutting down...");
  running = 0;
}

int main(void) {
  fio_signal_monitor(.sig = SIGINT,
                     .callback = on_sigint,
                     .udata = NULL);

  while (running) {
    fio_signal_review();
    /* do work */
  }

  fio_signal_forget(SIGINT);
  return 0;
}
```

------------------------------------------------------------
# Sorting

```c
#define FIO_SORT_NAME num
#define FIO_SORT_TYPE size_t
#include "fio-stl.h"
```

A macro-generated quicksort + insert-sort combo. Define `FIO_SORT_NAME` and `FIO_SORT_TYPE` before including the header to get a set of sort functions named after your type. Include the header multiple times to sort different types.

### Configuration Macros

#### `FIO_SORT_NAME`

```c
#define FIO_SORT_NAME num
```

Prefix for generated function names: `num_sort`, `num_qsort`, `num_isort`.

#### `FIO_SORT_TYPE`

```c
#define FIO_SORT_TYPE size_t
```

The element type. **Required.**

#### `FIO_SORT_IS_BIGGER`

```c
#ifndef FIO_SORT_IS_BIGGER
#define FIO_SORT_IS_BIGGER(a, b) ((a) > (b))
#endif
```

Must evaluate to `1` if `a > b`, `0` otherwise. Override for custom ordering.

#### `FIO_SORT_SWAP`

```c
#ifndef FIO_SORT_SWAP
#define FIO_SORT_SWAP(a, b)                                                    \
  do {                                                                         \
    FIO_SORT_TYPE tmp__ = (a);                                                 \
    (a) = (b);                                                                 \
    (b) = tmp__;                                                               \
  } while (0)
#endif
```

Swaps two elements. Override if your type needs a custom swap.

#### `FIO_SORT_THRESHOLD`

```c
#ifndef FIO_SORT_THRESHOLD
#define FIO_SORT_THRESHOLD 96
#endif
```

Partition size below which quicksort switches to insert-sort.

### API Functions

#### `FIO_SORT_NAME_sort`

```c
FIO_IFUNC void FIO_NAME(FIO_SORT_NAME, sort)(FIO_SORT_TYPE *array,
                                             size_t count);
```

Sorts `array[0..count-1]` in ascending order. Currently dispatches to quicksort.

#### `FIO_SORT_NAME_qsort`

```c
SFUNC void FIO_NAME(FIO_SORT_NAME, qsort)(FIO_SORT_TYPE *array, size_t count);
```

Non-recursive quicksort with median-of-three pivot selection. Falls back to insert-sort for small partitions.

#### `FIO_SORT_NAME_isort`

```c
SFUNC void FIO_NAME(FIO_SORT_NAME, isort)(FIO_SORT_TYPE *array, size_t count);
```

Insert-sort for small or nearly-sorted arrays. O(n²), so avoid large inputs.

### Example

```c
#define FIO_SORT_NAME int
#define FIO_SORT_TYPE int
#include "fio-stl.h"

int main(void) {
  int numbers[] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
  size_t count = sizeof(numbers) / sizeof(numbers[0]);

  int_sort(numbers, count);

  for (size_t i = 0; i < count; ++i)
    printf("%d ", numbers[i]);
  printf("\n");
  return 0;
}
```

------------------------------------------------------------
# Portable Threads

```c
#define FIO_THREADS
#include "fio-stl.h"
```

A thin, portable wrapper over POSIX pthreads and Windows threads. Provides thread creation, mutexes, condition variables, priorities, and a few process helpers. The wrappers intentionally return no thread exit value.

### Types

#### `fio_thread_t`

```c
/* POSIX */ typedef pthread_t fio_thread_t;
/* Windows */ typedef uintptr_t fio_thread_t;
```

Thread handle. On Windows this is the numeric thread ID; the handle is opened transiently only when needed.

#### `fio_thread_pid_t`

```c
/* POSIX */ typedef pid_t fio_thread_pid_t;
/* Windows */ typedef DWORD fio_thread_pid_t;
```

Process ID type.

#### `fio_thread_mutex_t`

```c
/* POSIX */ typedef pthread_mutex_t fio_thread_mutex_t;
/* Windows */ typedef SRWLOCK fio_thread_mutex_t;
```

Mutex type.

#### `fio_thread_cond_t`

```c
/* POSIX */ typedef pthread_cond_t fio_thread_cond_t;
/* Windows */ typedef CONDITION_VARIABLE fio_thread_cond_t;
```

Condition-variable type.

#### `fio_thread_priority_e`

```c
typedef enum {
  FIO_THREAD_PRIORITY_ERROR = -1,
  FIO_THREAD_PRIORITY_LOWEST = 0,
  FIO_THREAD_PRIORITY_LOW,
  FIO_THREAD_PRIORITY_NORMAL,
  FIO_THREAD_PRIORITY_HIGH,
  FIO_THREAD_PRIORITY_HIGHEST,
} fio_thread_priority_e;
```

Thread priority levels.

### Configuration Macros

#### `FIO_THREADS_BYO`

If defined, the thread creation/join/detach/current/yield functions are declared but not implemented — bring your own.

#### `FIO_THREADS_FORK_BYO`

If defined, the process helpers (`fio_thread_fork`, `getpid`, `kill`, `waitpid`) are declared but not implemented.

#### `FIO_THREADS_MUTEX_BYO`

If defined, the mutex functions are declared but not implemented.

#### `FIO_THREADS_COND_BYO`

If defined, the condition-variable functions are declared but not implemented.

#### `FIO_THREAD_MUTEX_INIT`

```c
/* POSIX */ #define FIO_THREAD_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
/* Windows */ #define FIO_THREAD_MUTEX_INIT { 0 }
```

Static initializer for a mutex.

### Process Functions

#### `fio_thread_fork`

```c
FIO_IFUNC fio_thread_pid_t fio_thread_fork(void);
```

Behaves like POSIX `fork`. On Windows logs an error and returns `-1`.

#### `fio_thread_getpid`

```c
FIO_IFUNC fio_thread_pid_t fio_thread_getpid(void);
```

Behaves like POSIX `getpid`.

#### `fio_thread_kill`

```c
FIO_IFUNC int fio_thread_kill(fio_thread_pid_t pid, int sig);
```

Behaves like POSIX `kill`.

#### `fio_thread_waitpid`

```c
FIO_IFUNC int fio_thread_waitpid(fio_thread_pid_t pid,
                                 int *stat_loc,
                                 int options);
```

Behaves like POSIX `waitpid`.

### Thread Functions

#### `fio_thread_create`

```c
FIO_IFUNC int fio_thread_create(fio_thread_t *t,
                                void *(*fn)(void *),
                                void *arg);
```

Starts a new thread running `fn(arg)`. Returns `0` on success, `-1` on failure.

#### `fio_thread_join`

```c
FIO_IFUNC int fio_thread_join(fio_thread_t *t);
```

Waits for the thread to finish.

#### `fio_thread_detach`

```c
FIO_IFUNC int fio_thread_detach(fio_thread_t *t);
```

Detaches the thread so resources are reclaimed automatically.

#### `fio_thread_exit`

```c
FIO_IFUNC void fio_thread_exit(void);
```

Exits the current thread.

#### `fio_thread_equal`

```c
FIO_IFUNC int fio_thread_equal(fio_thread_t *a, fio_thread_t *b);
```

Returns non-zero if `a` and `b` refer to the same thread.

#### `fio_thread_current`

```c
FIO_IFUNC fio_thread_t fio_thread_current(void);
```

Returns a handle for the current thread.

#### `fio_thread_yield`

```c
FIO_IFUNC void fio_thread_yield(void);
```

Yields the CPU.

#### `fio_thread_priority`

```c
FIO_SFUNC fio_thread_priority_e fio_thread_priority(void);
```

Returns the current thread's priority, or `FIO_THREAD_PRIORITY_ERROR` on failure.

#### `fio_thread_priority_set`

```c
FIO_SFUNC int fio_thread_priority_set(fio_thread_priority_e pr);
```

Sets the current thread's priority. Returns `0` on success, `-1` on error.

### Mutex Functions

#### `fio_thread_mutex_init`

```c
FIO_IFUNC int fio_thread_mutex_init(fio_thread_mutex_t *m);
```

Initializes a mutex. You can also use `FIO_THREAD_MUTEX_INIT` for static initialization.

#### `fio_thread_mutex_lock`

```c
FIO_IFUNC int fio_thread_mutex_lock(fio_thread_mutex_t *m);
```

Locks `m`, blocking if necessary.

#### `fio_thread_mutex_trylock`

```c
FIO_IFUNC int fio_thread_mutex_trylock(fio_thread_mutex_t *m);
```

Tries to lock `m` without blocking. Returns `0` on success.

#### `fio_thread_mutex_unlock`

```c
FIO_IFUNC int fio_thread_mutex_unlock(fio_thread_mutex_t *m);
```

Unlocks `m`.

#### `fio_thread_mutex_destroy`

```c
FIO_IFUNC void fio_thread_mutex_destroy(fio_thread_mutex_t *m);
```

Destroys `m`.

### Condition Variable Functions

#### `fio_thread_cond_init`

```c
FIO_IFUNC int fio_thread_cond_init(fio_thread_cond_t *c);
```

Initializes a condition variable.

#### `fio_thread_cond_wait`

```c
FIO_IFUNC int fio_thread_cond_wait(fio_thread_cond_t *c,
                                   fio_thread_mutex_t *m);
```

Waits on `c`. `m` must be locked; it is released while waiting and re-acquired on return.

#### `fio_thread_cond_timedwait`

```c
FIO_IFUNC int fio_thread_cond_timedwait(fio_thread_cond_t *c,
                                        fio_thread_mutex_t *m,
                                        size_t milliseconds);
```

Waits up to `milliseconds`. Returns `0` if signaled, non-zero on timeout/error.

#### `fio_thread_cond_signal`

```c
FIO_IFUNC int fio_thread_cond_signal(fio_thread_cond_t *c);
```

Wakes one waiter.

#### `fio_thread_cond_destroy`

```c
FIO_IFUNC void fio_thread_cond_destroy(fio_thread_cond_t *c);
```

Destroys `c`.

### Example

```c
#define FIO_THREADS
#include "fio-stl.h"

static fio_thread_mutex_t lock = FIO_THREAD_MUTEX_INIT;
static int counter = 0;

static void *worker(void *arg) {
  (void)arg;
  for (int i = 0; i < 100000; ++i) {
    fio_thread_mutex_lock(&lock);
    ++counter;
    fio_thread_mutex_unlock(&lock);
  }
  return NULL;
}

int main(void) {
  fio_thread_t t1, t2;
  fio_thread_create(&t1, worker, NULL);
  fio_thread_create(&t2, worker, NULL);
  fio_thread_join(&t1);
  fio_thread_join(&t2);
  printf("counter: %d\n", counter);
  fio_thread_mutex_destroy(&lock);
  return 0;
}
```

------------------------------------------------------------
# URL / URI Parsing

```c
#define FIO_URL
#include "fio-stl.h"
```

Zero-copy, zero-allocation URI parser. Returns `fio_buf_info_s` slices pointing into the original string. No decoding is performed; pass the raw, encoded URI.

### Types

#### `fio_url_s`

```c
typedef struct {
  fio_buf_info_s scheme;
  fio_buf_info_s user;
  fio_buf_info_s password;
  fio_buf_info_s host;
  fio_buf_info_s port;
  fio_buf_info_s path;
  fio_buf_info_s query;
  fio_buf_info_s target;
} fio_url_s;
```

Parsed URL components. Empty components have `.buf == NULL` and `.len == 0`. Strings are **not** NUL terminated.

#### `fio_url_tls_info_s`

```c
typedef struct {
  fio_buf_info_s key;
  fio_buf_info_s cert;
  fio_buf_info_s pass;
  bool tls;
} fio_url_tls_info_s;
```

TLS material extracted from scheme or query parameters.

#### `fio_url_query_each_s`

```c
typedef struct {
  fio_buf_info_s name;
  fio_buf_info_s value;
  fio_buf_info_s private___;
} fio_url_query_each_s;
```

Iterator state for `FIO_URL_QUERY_EACH`. Do not touch `private___`.

### API Functions

#### `fio_url_parse`

```c
SFUNC fio_url_s fio_url_parse(const char *url, size_t len);
```

Parses `url` of length `len` into components. Accepts many shapes:

- `/path?query#target`
- `host:port/path?query#target`
- `user:password@host:port/path?query#target`
- `scheme://user:password@host:port/path?query#target`

Invalid input produces unpredictable results — the parser does not validate.

**Note:** `file`, `unix`, and `priv` schemes are treated as file paths; the parser puts the whole remainder into `path` and clears host/port/user/password.

#### `fio_url_is_tls`

```c
SFUNC fio_url_tls_info_s fio_url_is_tls(fio_url_s u);
```

Detects TLS from scheme or query. Implicit TLS schemes include `wss`, `https`, `sses`, `ssse`, `tcps`, `stcp`, `tls`, `ssl`, `udps`, `sudp`. Explicit TLS is enabled by query keys `tls`, `key`, `cert`, `pass`, or `password`.

### Macros

#### `FIO_URL_QUERY_EACH`

```c
#define FIO_URL_QUERY_EACH(query_buf, i)                                       \
  for (fio_url_query_each_s i = fio_url_query_each_next(                       \
           (fio_url_query_each_s){.private___ = (query_buf)});                 \
       i.name.buf;                                                             \
       i = fio_url_query_each_next(i))
```

Iterates over query parameters. The iterator does not URL-decode names or values.

### Helper Functions

#### `fio_url_query_each_next`

```c
FIO_SFUNC fio_url_query_each_s fio_url_query_each_next(fio_url_query_each_s i);
```

Advances the query iterator. Usually called only through `FIO_URL_QUERY_EACH`.

### Example

```c
#define FIO_URL
#include "fio-stl.h"

int main(void) {
  const char *s = "https://user:pass@example.com:8443/path?k=v#section";
  fio_url_s u = fio_url_parse(s, strlen(s));

  printf("scheme: %.*s\n", (int)u.scheme.len, u.scheme.buf);
  printf("host:   %.*s\n", (int)u.host.len, u.host.buf);
  printf("port:   %.*s\n", (int)u.port.len, u.port.buf);
  printf("path:   %.*s\n", (int)u.path.len, u.path.buf);

  FIO_URL_QUERY_EACH(u.query, p) {
    printf("%.*s = %.*s\n",
           (int)p.name.len, p.name.buf,
           (int)p.value.len, p.value.buf);
  }

  fio_url_tls_info_s tls = fio_url_is_tls(u);
  printf("tls: %d\n", tls.tls);
  return 0;
}
```

------------------------------------------------------------
# ML Entity Decoding

```c
#define FIO_ENTITY
#include "fio-stl.h"
```

Decodes a single Markup Language entity (`&name;`, `&#digits;`, `&#xhex;`) into UTF-8. Handy when you need to unescape one entity at a time, for example while scanning HTML or XML text.

### API Functions

#### `fio_entity`

```c
SFUNC size_t fio_entity(char *dest, const char *src, size_t len);
```

Decodes the entity starting at `src` (length `len`). Writes the UTF-8 result to `dest`, which must be at least 8 bytes. A NUL terminator is written only if the result is shorter than 8 bytes.

**Returns:** the number of bytes written (excluding NUL), or `0` if `src` does not start with a valid entity.

Supported forms:

- Named: `&name;` — case-insensitive, ~40 common entities such as `&lt;`, `&amp;`, `&copy;`, `&mdash;`.
- Decimal: `&#digits;`
- Hex: `&#xhex;` or `&#Xhex;`

`&#0;` is replaced with the Unicode replacement character (`U+FFFD`). Code points above `0x10FFFF` are rejected.

### Example

```c
#define FIO_ENTITY
#include "fio-stl.h"

int main(void) {
  const char *s = "&copy; 2026 &#x1F600;";
  char buf[8];
  size_t n = fio_entity(buf, s, strlen(s));
  printf("decoded: %.*s\n", (int)n, buf);
  return 0;
}
```

------------------------------------------------------------
# File Helpers

```c
#define FIO_FILES
#include "fio-stl.h"
```

POSIX-style file helpers: open, read, write, size, type, path safety, filename parsing, and temporary files. Nothing fancy — just the usual paperwork. Implemented in [`./004 files.h`](./004%20files.h).

### Configuration Macros

#### `FIO_FD_FIND_BLOCK`

```c
#ifndef FIO_FD_FIND_BLOCK
#define FIO_FD_FIND_BLOCK 4096
#endif
```

Stack buffer size used by `fio_fd_find_next` for each read cycle. Override before inclusion if you want larger or smaller scans.

### Types

#### `fio_filename_s`

```c
typedef struct {
  fio_buf_info_s folder;
  fio_buf_info_s basename;
  fio_buf_info_s ext;
} fio_filename_s;
```

Zero-copy result from `fio_filename_parse` / `fio_filename_parse2`. All members point into the original path string.

**Members:**
- `folder` - directory path including trailing separator, or `{NULL,0}` if none
- `basename` - file name without extension, or `{NULL,0}` if empty
- `ext` - extension without leading `.`, or `{NULL,0}` if none

### Macros

#### `FIO_FOLDER_SEPARATOR`

```c
#if FIO_OS_WIN
#define FIO_FOLDER_SEPARATOR '\\'
#else
#define FIO_FOLDER_SEPARATOR '/'
#endif
```

Native path separator. On Windows the parser also accepts `/`.

#### `fio_file_dup`

```c
#if FIO_OS_WIN
#define fio_file_dup(fd) _dup(fd)
#else
#define fio_file_dup(fd) dup(fd)
#endif
```

Duplicates a file descriptor.

#### `fio_filename_is_folder`

```c
#define fio_filename_is_folder(filename) \
  (fio_filename_type((filename)) == S_IFDIR)
```

Returns non-zero if `filename` is a directory, `0` otherwise (including errors).

#### `FIO_FD_FIND_EOF`

```c
#define FIO_FD_FIND_EOF ((size_t)-1)
```

Sentinel returned by `fio_fd_find_next` when the token is not found before EOF.

### Opening and Creating Files

#### `fio_filename_open`

```c
SFUNC int fio_filename_open(const char *filename, int flags);
```

Opens `filename` with POSIX `open` semantics. If `filename` starts with `"~/"` (or `"~\\"` on Windows), it is resolved relative to `$HOME`.

**Parameters:**
- `filename` - path to open
- `flags` - `O_RDONLY`, `O_RDWR | O_CREAT | O_TRUNC`, etc.

**Returns:** file descriptor on success, `-1` on error.

#### `fio_filename_tmp`

```c
SFUNC int fio_filename_tmp(void);
```

Creates a temporary file and returns its descriptor. Tries `TMPDIR`, `TMP`, `TEMP`, `P_tmpdir`, then the current directory. On Linux it attempts `O_TMPFILE` first; otherwise it builds a name prefixed with `facil_io_tmp_` and opens with `O_CREAT | O_EXCL`.

**Returns:** file descriptor on success, `-1` on error.

#### `fio_filename_overwrite`

```c
FIO_IFUNC int fio_filename_overwrite(const char *filename,
                                     const void *buf,
                                     size_t len);
```

Opens `filename` with `O_RDWR | O_CREAT | O_TRUNC`, writes `len` bytes from `buf`, and closes the file.

**Returns:** `0` on success, `-1` on error. On error the file state is undefined.

### Reading and Writing

#### `fio_fd_write`

```c
FIO_IFUNC ssize_t fio_fd_write(int fd, const void *buf, size_t len);
```

Writes `len` bytes to `fd`, fragmenting the syscall into `write` blocks of up to `1 << 17` bytes. Retries on `EINTR`.

**Returns:** number of bytes written, or `-1` on error. If `fd == -1`, `buf == NULL`, or `len == 0`, returns `-1`.

**Note:** for non-blocking descriptors, check `errno` for `EAGAIN` / `EWOULDBLOCK`.

#### `fio_fd_read`

```c
FIO_IFUNC size_t fio_fd_read(int fd, void *buf, size_t len, off_t start_at);
```

Reads up to `len` bytes from `fd` starting at `start_at`. On POSIX systems with `pread`, the file offset is preserved; otherwise it seeks. Reads are fragmented to `1 << 27` byte chunks. Retries on `EINTR`.

**Parameters:**
- `fd` - file descriptor
- `buf` - destination buffer
- `len` - maximum bytes to read
- `start_at` - offset; negative values seek from end

**Returns:** bytes read, or `0` on error / EOF. Sets `errno = ENOENT` if `fd == -1`, `buf == NULL`, or `len == 0`.

### File Information

#### `fio_filename_size`

```c
FIO_IFUNC size_t fio_filename_size(const char *filename);
```

Returns file size in bytes, or `0` on error or empty file.

#### `fio_fd_size`

```c
FIO_IFUNC size_t fio_fd_size(int fd);
```

Returns file size for an open descriptor, or `0` on error or empty file.

#### `fio_filename_type`

```c
FIO_IFUNC size_t fio_filename_type(const char *filename);
```

Returns the file type bits (`st_mode & S_IFMT`), e.g. `S_IFREG`, `S_IFDIR`, or `0` on error.

#### `fio_fd_type`

```c
FIO_IFUNC size_t fio_fd_type(int fd);
```

Same as `fio_filename_type`, but for an open descriptor.

#### `fio_filename_stat`

```c
FIO_IFUNC int fio_filename_stat(const char *filename, struct stat *stat_buf);
```

Populates `stat_buf` via `stat`. Returns `0` on success, `-1` on error (including `filename == NULL` or `stat_buf == NULL`).

### Path Safety

#### `fio_filename_is_unsafe`

```c
SFUNC int fio_filename_is_unsafe(const char *path);
```

Returns `1` if `path` possibly folds backwards or contains double separators:
- leading `../`
- `/../` or trailing `/..`
- `//`

Uses the OS separator (`\` on Windows, `/` elsewhere). Returns `0` for `NULL` paths.

#### `fio_filename_is_unsafe_url`

```c
SFUNC int fio_filename_is_unsafe_url(const char *path);
```

Same check as `fio_filename_is_unsafe`, but always uses `/` as the separator. Use this for URL paths.

### Filename Parsing

#### `fio_filename_parse`

```c
SFUNC fio_filename_s fio_filename_parse(const char *filename);
```

Splits a NUL-terminated path into folder, basename, and extension without copying.

**Returns:** `fio_filename_s` with pointers into `filename`.

#### `fio_filename_parse2`

```c
SFUNC fio_filename_s fio_filename_parse2(const char *filename, size_t len);
```

Same as `fio_filename_parse`, but reads at most `len` bytes. Use when the input is not NUL-terminated.

### File Search

#### `fio_fd_find_next`

```c
SFUNC size_t fio_fd_find_next(int fd, char token, size_t start_at);
```

Scans `fd` for the next occurrence of `token` starting at `start_at`. Reads in `FIO_FD_FIND_BLOCK` byte chunks.

**Returns:** byte offset of the match, or `FIO_FD_FIND_EOF` if not found.

### Example

```c
#define FIO_FILES
#define FIO_LOG
#include "fio-stl.h"

int main(void) {
  fio_filename_s parts = fio_filename_parse("/var/log/app.log");
  printf("folder=%.*s base=%.*s ext=%.*s\n",
         (int)parts.folder.len, parts.folder.buf,
         (int)parts.basename.len, parts.basename.buf,
         (int)parts.ext.len, parts.ext.buf);

  const char *unsafe = "/var/www/../etc/passwd";
  printf("unsafe? %s\n", fio_filename_is_unsafe(unsafe) ? "yes" : "no");

  const char *msg = "hello, files";
  if (fio_filename_overwrite("/tmp/fio_test.txt", msg, strlen(msg)) == 0) {
    printf("wrote %zu bytes\n", strlen(msg));
  }

  size_t sz = fio_filename_size("/tmp/fio_test.txt");
  printf("size = %zu\n", sz);

  int tmp = fio_filename_tmp();
  if (tmp != -1) {
    fio_fd_write(tmp, "temp", 4);
    close(tmp);
  }
  return 0;
}
```

------------------------------------------------------------
# JSON Parser

```c
#define FIO_JSON
#include "fio-stl.h"
```

A non-strict, streaming, callback-based JSON parser. It does not build a tree for you; it calls your callbacks for each value and lets you decide what to construct. Implemented in [`./004 json.h`](./004%20json.h). Depends on `FIO_ATOL`, which is included automatically.

The parser tolerates trailing commas, comments (`//`, `/* */`, `#`), newlines inside strings, hex/octal/binary numbers, `NaN`, and `Infinity`.

### Configuration Macros

#### `FIO_JSON_MAX_DEPTH`

```c
#ifndef FIO_JSON_MAX_DEPTH
#define FIO_JSON_MAX_DEPTH 128
#endif
```

Maximum JSON nesting depth. Must be less than `65536`. Deeper input aborts with an error.

#### `FIO_JSON_USE_FIO_ATON`

```c
#ifndef FIO_JSON_USE_FIO_ATON
#define FIO_JSON_USE_FIO_ATON 0
#endif
```

Set to `1` to use `fio_aton` for number parsing instead of the built-in logic.

### Types

#### `fio_json_parser_callbacks_s`

```c
typedef struct {
  void *(*on_null)(void *udata);
  void *(*on_true)(void *udata);
  void *(*on_false)(void *udata);
  void *(*on_number)(void *udata, int64_t i);
  void *(*on_float)(void *udata, double f);
  void *(*on_string)(void *udata, const void *start, size_t len);
  void *(*on_string_simple)(void *udata, const void *start, size_t len);
  void *(*on_map)(void *udata, void *ctx, void *at);
  void *(*on_array)(void *udata, void *ctx, void *at);
  int (*map_push)(void *udata, void *ctx, void *key, void *value);
  int (*array_push)(void *udata, void *ctx, void *value);
  int (*array_finished)(void *udata, void *ctx);
  int (*map_finished)(void *udata, void *ctx);
  int (*is_array)(void *udata, void *ctx);
  int (*is_map)(void *udata, void *ctx);
  void (*free_unused_object)(void *udata, void *ctx);
  void *(*on_error)(void *udata, void *ctx);
} fio_json_parser_callbacks_s;
```

Callback table for the parser. All callbacks receive `udata` as their first argument, so the same `static const` table can be shared across concurrent calls.

**Required callbacks:** `on_null`, `on_true`, `on_false`, `on_number`, `on_float`, `on_string` (or `on_string_simple`), `on_map`, `on_array`, `map_push`, `array_push`, `free_unused_object`.

**Optional callbacks:** `on_string_simple`, `array_finished`, `map_finished`, `is_array`, `is_map`, `on_error`. Missing optional callbacks default to no-ops. If only one of `on_string` / `on_string_simple` is provided, the other uses it as a fallback.

**Ownership:** callbacks that return `void *` transfer ownership to the parser. The parser passes objects to `map_push` / `array_push` or calls `free_unused_object` for discarded objects. `on_error` receives ownership of any partial result.

#### `fio_json_result_s`

```c
typedef struct {
  void *ctx;
  size_t stop_pos;
  int err;
} fio_json_result_s;
```

Parse result.

**Members:**
- `ctx` - root object returned by callbacks, or `NULL`
- `stop_pos` - number of bytes consumed from the input
- `err` - non-zero if parsing stopped because of an error

### API Functions

#### `fio_json_parse`

```c
SFUNC fio_json_result_s fio_json_parse(fio_json_parser_callbacks_s *settings,
                                       void *udata,
                                       const char *json_string,
                                       const size_t len);
```

Parses `json_string` up to `len` bytes. Stops as soon as a complete top-level value is consumed or an error occurs.

**Parameters:**
- `settings` - callback table
- `udata` - per-call user data threaded through every callback
- `json_string` - input buffer
- `len` - input length in bytes

**Returns:** `fio_json_result_s` with the root context, bytes consumed, and error flag.

**Note:** a UTF-8 BOM at the start of the buffer is skipped automatically.

#### `fio_json_parse_update`

```c
SFUNC fio_json_result_s fio_json_parse_update(fio_json_parser_callbacks_s *s,
                                              void *udata,
                                              void *ctx,
                                              const char *start,
                                              const size_t len);
```

Merges JSON data into an existing object (`ctx`). The input must be wrapped in an aggregate of the same type as `ctx`. Requires `is_array` and `is_map` callbacks.

**Parameters:**
- `s` - callback table
- `udata` - per-call user data
- `ctx` - existing object to update
- `start` - input buffer
- `len` - input length

**Returns:** parse result with the (possibly updated) context.

### Example

```c
#define FIO_JSON
#define FIO_LOG
#include "fio-stl.h"

typedef enum { T_NULL, T_TRUE, T_FALSE, T_NUM, T_STR, T_ARR, T_MAP } type_e;

typedef struct obj_s {
  type_e type;
  union {
    int64_t i;
    struct { char *buf; size_t len; } s;
    struct { struct obj_s **items; size_t len; } a;
    struct { struct obj_s **keys; struct obj_s **vals; size_t len; } m;
  };
} obj_s;

static obj_s *mk(type_e t) {
  obj_s *o = calloc(1, sizeof(*o));
  o->type = t;
  return o;
}

static void *on_null(void *u) { (void)u; return mk(T_NULL); }
static void *on_true(void *u) { (void)u; return mk(T_TRUE); }
static void *on_false(void *u) { (void)u; return mk(T_FALSE); }
static void *on_number(void *u, int64_t i) { (void)u; obj_s *o = mk(T_NUM); o->i = i; return o; }
static void *on_string(void *u, const void *s, size_t l) {
  (void)u; obj_s *o = mk(T_STR); o->s.buf = malloc(l+1); memcpy(o->s.buf, s, l); o->s.buf[l] = 0; o->s.len = l; return o;
}
static void *on_array(void *u, void *c, void *a) { (void)u; (void)c; (void)a; return mk(T_ARR); }
static void *on_map(void *u, void *c, void *a) { (void)u; (void)c; (void)a; return mk(T_MAP); }
static int array_push(void *u, void *c, void *v) {
  (void)u; obj_s *a = c; a->a.items = realloc(a->a.items, (a->a.len+1)*sizeof(*a->a.items)); a->a.items[a->a.len++] = v; return 0;
}
static int map_push(void *u, void *c, void *k, void *v) {
  (void)u; obj_s *m = c;
  m->m.keys = realloc(m->m.keys, (m->m.len+1)*sizeof(*m->m.keys));
  m->m.vals = realloc(m->m.vals, (m->m.len+1)*sizeof(*m->m.vals));
  m->m.keys[m->m.len] = k; m->m.vals[m->m.len] = v; m->m.len++; return 0;
}
static void free_obj(void *u, void *c) { (void)u; free(c); }

int main(void) {
  static const fio_json_parser_callbacks_s cb = {
    .on_null = on_null, .on_true = on_true, .on_false = on_false,
    .on_number = on_number, .on_string = on_string, .on_string_simple = on_string,
    .on_array = on_array, .on_map = on_map,
    .array_push = array_push, .map_push = map_push,
    .free_unused_object = free_obj,
  };
  const char *json = "{ \"n\": 42, \"s\": \"hi\", \"a\": [1, 2] }";
  fio_json_result_s r = fio_json_parse(&cb, NULL, json, strlen(json));
  if (r.err) {
    FIO_LOG_ERROR("parse error at %zu", r.stop_pos);
    return 1;
  }
  printf("parsed %zu bytes, root ctx %p\n", r.stop_pos, r.ctx);
  return 0;
}
```

------------------------------------------------------------
# MIME Multipart Parser

```c
#define FIO_MULTIPART
#include "fio-stl.h"
```

A non-allocating, streaming, callback-based `multipart/form-data` parser. It finds parts, extracts `Content-Disposition` and `Content-Type`, then calls your callbacks for each field or file chunk. Implemented in [`./004 multipart.h`](./004%20multipart.h).

The parser does not decode transfer encodings or unescape headers — it just slices the original buffer into `fio_buf_info_s` ranges.

### Types

#### `fio_multipart_parser_callbacks_s`

```c
typedef struct {
  void *(*on_field)(void *udata,
                    fio_buf_info_s name,
                    fio_buf_info_s value,
                    fio_buf_info_s content_type);
  void *(*on_field_start)(void *udata,
                          fio_buf_info_s name,
                          fio_buf_info_s content_type);
  int (*on_field_data)(void *udata, void *field_ctx, fio_buf_info_s data);
  void (*on_field_end)(void *udata, void *field_ctx);
  void *(*on_file_start)(void *udata,
                         fio_buf_info_s name,
                         fio_buf_info_s filename,
                         fio_buf_info_s content_type);
  int (*on_file_data)(void *udata, void *file_ctx, fio_buf_info_s data);
  void (*on_file_end)(void *udata, void *file_ctx);
  void (*on_error)(void *udata);
} fio_multipart_parser_callbacks_s;
```

Callback table. All callbacks are optional; unset ones become no-ops.

**Members:**
- `on_field` - called for regular form fields (no `filename`). Ignored if `on_field_start` is provided.
- `on_field_start` - optional streaming field start. If set, `on_field` is ignored and `on_field_data` / `on_field_end` are used instead.
- `on_field_data` - called with a field data chunk. Return non-zero to abort.
- `on_field_end` - called when a streaming field ends.
- `on_file_start` - called when a file upload starts. Returns a context pointer for this file.
- `on_file_data` - called with a file data chunk. Return non-zero to abort.
- `on_file_end` - called when a file upload ends.
- `on_error` - called on parse error (optional).

#### `fio_multipart_result_s`

```c
typedef struct {
  size_t consumed;
  size_t field_count;
  size_t file_count;
  int err;
} fio_multipart_result_s;
```

Parse result.

**Members:**
- `consumed` - bytes consumed from the input buffer
- `field_count` - regular form fields parsed in this call
- `file_count` - file uploads parsed in this call
- `err` - `0` success, `-1` error, `-2` need more data

### API Functions

#### `fio_multipart_parse`

```c
SFUNC fio_multipart_result_s
fio_multipart_parse(const fio_multipart_parser_callbacks_s *callbacks,
                    void *udata,
                    fio_buf_info_s boundary,
                    const char *data,
                    size_t len);
```

Parses `data` as MIME multipart input. `boundary` is the boundary string **without** the leading `"--"` and must not exceed 250 bytes.

**Parameters:**
- `callbacks` - callback table (typically `static const`)
- `udata` - user data passed to every callback
- `boundary` - boundary value from the `Content-Type` header
- `data` - input buffer
- `len` - input length in bytes

**Returns:** parse result.

**Streaming behavior:**
- `err == 0` - parsing complete; closing boundary found.
- `err == -2` - need more data. Keep `data + result.consumed ... data + len` and append new data before calling again.
- `err == -1` - unrecoverable parse error or callback abort.

### Examples

#### Basic field and file parsing

```c
#define FIO_MULTIPART
#include "fio-stl.h"

static void *on_field(void *u, fio_buf_info_s n, fio_buf_info_s v, fio_buf_info_s ct) {
  (void)u; (void)ct;
  printf("field %.*s = %.*s\n", (int)n.len, n.buf, (int)v.len, v.buf);
  return NULL;
}
static void *on_file_start(void *u, fio_buf_info_s n, fio_buf_info_s fn, fio_buf_info_s ct) {
  (void)u; (void)ct;
  printf("file %.*s / %.*s\n", (int)n.len, n.buf, (int)fn.len, fn.buf);
  return NULL;
}
static int on_file_data(void *u, void *c, fio_buf_info_s d) {
  (void)u; (void)c;
  printf("  chunk %zu bytes\n", d.len);
  return 0;
}
static void on_file_end(void *u, void *c) { (void)u; (void)c; }

int main(void) {
  static const fio_multipart_parser_callbacks_s cb = {
    .on_field = on_field,
    .on_file_start = on_file_start,
    .on_file_data = on_file_data,
    .on_file_end = on_file_end,
  };
  const char *body =
    "--B\r\n"
    "Content-Disposition: form-data; name=\"x\"\r\n\r\n"
    "hello\r\n"
    "--B\r\n"
    "Content-Disposition: form-data; name=\"f\"; filename=\"t.txt\"\r\n"
    "Content-Type: text/plain\r\n\r\n"
    "data\r\n"
    "--B--\r\n";
  fio_multipart_result_s r = fio_multipart_parse(
      &cb, NULL, FIO_BUF_INFO2("B", 1), body, strlen(body));
  printf("fields=%zu files=%zu err=%d\n", r.field_count, r.file_count, r.err);
  return 0;
}
```

#### Streaming field callbacks

```c
static void *field_start(void *u, fio_buf_info_s n, fio_buf_info_s ct) {
  (void)u; (void)ct;
  printf("field start %.*s\n", (int)n.len, n.buf);
  return NULL;
}
static int field_data(void *u, void *c, fio_buf_info_s d) {
  (void)u; (void)c;
  printf("  field chunk %zu bytes\n", d.len);
  return 0;
}
static void field_end(void *u, void *c) { (void)u; (void)c; }

static const fio_multipart_parser_callbacks_s cb_stream = {
  .on_field_start = field_start,
  .on_field_data = field_data,
  .on_field_end = field_end,
};
```

------------------------------------------------------------
# RESP3 Parser

```c
#define FIO_RESP3
#include "fio-stl.h"
```

A streaming, callback-based [RESP3](https://github.com/redis/redis-specifications/blob/master/protocol/RESP3.md) parser for Redis 6+ and compatible servers. Implemented in [`./004 resp3.h`](./004%20resp3.h). Depends on `FIO_ATOL`, included automatically.

The parser pushes values through a small internal stack, calling your callbacks for primitives, containers, and streaming strings. It does not allocate on its own.

### Configuration Macros

#### `FIO_RESP3_MAX_NESTING`

```c
#ifndef FIO_RESP3_MAX_NESTING
#define FIO_RESP3_MAX_NESTING 32
#endif
```

Maximum nesting depth. Valid range is 2 to 32,768. Determines the size of `fio_resp3_parser_s.stack`.

### RESP3 Type Constants

```c
#define FIO_RESP3_SIMPLE_STR '+'   /* +<string>\r\n           */
#define FIO_RESP3_SIMPLE_ERR '-'   /* -<string>\r\n           */
#define FIO_RESP3_NUMBER     ':'   /* :<number>\r\n           */
#define FIO_RESP3_NULL       '_'   /* _\r\n                   */
#define FIO_RESP3_DOUBLE     ','   /* ,<double>\r\n           */
#define FIO_RESP3_BOOL       '#'   /* #t\r\n or #f\r\n       */
#define FIO_RESP3_BIGNUM     '('   /* (<big number>\r\n      */
#define FIO_RESP3_BLOB_STR   '$'   /* $<len>\r\n<bytes>\r\n  */
#define FIO_RESP3_BLOB_ERR   '!'   /* !<len>\r\n<bytes>\r\n  */
#define FIO_RESP3_VERBATIM   '='   /* =<len>\r\n<type:><bytes>\r\n */
#define FIO_RESP3_ARRAY      '*'   /* *<count>\r\n...         */
#define FIO_RESP3_MAP        '%'   /* %<count>\r\n...         */
#define FIO_RESP3_SET        '~'   /* ~<count>\r\n...         */
#define FIO_RESP3_PUSH       '>'   /* ><count>\r\n...         */
#define FIO_RESP3_ATTR       '|'   /* |<count>\r\n...         */
#define FIO_RESP3_STREAM_CHUNK ';' /* ;<len>\r\n<bytes>\r\n   */
#define FIO_RESP3_STREAM_END '.'   /* .\r\n                   */
```

### Types

#### `fio_resp3_frame_s`

```c
typedef struct {
  void *ctx;
  void *key;
  int64_t remaining;
  uint8_t type;
  uint8_t streaming;
  uint8_t expecting_value;
  uint8_t set_as_map;
} fio_resp3_frame_s;
```

Internal stack frame for a nested container. You do not need to touch this directly, but it is exposed in `fio_resp3_parser_s`.

#### `fio_resp3_parser_s`

```c
typedef struct {
  void *udata;
  uint32_t depth;
  uint8_t error;
  uint8_t streaming_string;
  uint8_t streaming_string_type;
  uint8_t reserved[1];
  void *streaming_string_ctx;
  fio_resp3_frame_s stack[FIO_RESP3_MAX_NESTING];
} fio_resp3_parser_s;
```

Parser state. Initialize with `{.udata = my_context}` before the first call and reuse it for continuation after partial parses.

**Members:**
- `udata` - user data passed to all callbacks
- `depth` - current nesting depth
- `error` - set to non-zero after a protocol error; the parser will refuse further input
- `streaming_string` - non-zero while a streamed string (`$?`) is in progress
- `streaming_string_type` - type of the streaming string in progress
- `streaming_string_ctx` - context returned by `on_start_string`
- `stack` - nested container frames

#### `fio_resp3_callbacks_s`

```c
typedef struct {
  /* primitives */
  void *(*on_null)(void *udata);
  void *(*on_bool)(void *udata, int is_true);
  void *(*on_number)(void *udata, int64_t num);
  void *(*on_double)(void *udata, double num);
  void *(*on_bignum)(void *udata, const void *data, size_t len);
  void *(*on_string)(void *udata, const void *data, size_t len, uint8_t type);
  void *(*on_error)(void *udata, const void *data, size_t len, uint8_t type);
  /* containers */
  void *(*on_array)(void *udata, void *parent_ctx, int64_t len);
  void *(*on_map)(void *udata, void *parent_ctx, int64_t len);
  void *(*on_set)(void *udata, void *parent_ctx, int64_t len);
  void *(*on_push)(void *udata, void *parent_ctx, int64_t len);
  void *(*on_attr)(void *udata, void *parent_ctx, int64_t len);
  /* push into containers */
  int (*array_push)(void *udata, void *ctx, void *value);
  int (*map_push)(void *udata, void *ctx, void *key, void *value);
  int (*set_push)(void *udata, void *ctx, void *value);
  int (*push_push)(void *udata, void *ctx, void *value);
  int (*attr_push)(void *udata, void *ctx, void *key, void *value);
  /* finalize containers */
  void *(*array_done)(void *udata, void *ctx);
  void *(*map_done)(void *udata, void *ctx);
  void *(*set_done)(void *udata, void *ctx);
  void *(*push_done)(void *udata, void *ctx);
  void *(*attr_done)(void *udata, void *ctx);
  /* errors */
  void (*free_unused)(void *udata, void *obj);
  void *(*on_error_protocol)(void *udata);
  /* streaming strings */
  void *(*on_start_string)(void *udata, size_t len, uint8_t type);
  int (*on_string_write)(void *udata, void *ctx, const void *data, size_t len);
  void *(*on_string_done)(void *udata, void *ctx, uint8_t type);
} fio_resp3_callbacks_s;
```

Callback table. Designed to be `static const`. Non-streaming callbacks left `NULL` are replaced with safe no-ops. Primitive no-ops return a non-NULL sentinel; container no-ops do the same; push no-ops return `0`; done no-ops return the context unchanged. The streaming-string callbacks (`on_start_string`, `on_string_write`, and `on_string_done`) are not filled with no-op fallbacks.

If `on_set`, `set_push`, and `set_done` are all `NULL` but map callbacks are provided, the parser treats RESP3 Sets as Maps, calling `map_push(ctx, value, value)` and `map_done`.

#### `fio_resp3_result_s`

```c
typedef struct {
  void *obj;
  size_t consumed;
  int err;
} fio_resp3_result_s;
```

Parse result.

**Members:**
- `obj` - top-level object, or `NULL` while incomplete / on error
- `consumed` - bytes consumed from the buffer
- `err` - non-zero if a protocol error occurred

### API Functions

#### `fio_resp3_parse`

```c
SFUNC fio_resp3_result_s fio_resp3_parse(fio_resp3_parser_s *parser,
                                         const fio_resp3_callbacks_s *callbacks,
                                         const void *buf,
                                         size_t len);
```

Parses as much RESP3 data as possible. State is preserved in `parser`, so you can call again with the remaining bytes after a partial read.

**Parameters:**
- `parser` - parser state
- `callbacks` - callback table (may be `NULL`; non-streaming callbacks become no-ops, while streaming-string callbacks remain unset)
- `buf` - input buffer
- `len` - input length

**Returns:** parse result.

**Note:** streamed strings (`$?`) require `on_start_string`, `on_string_write`, and `on_string_done` to be usable together. If `on_start_string` is missing or returns `NULL`, the parser sets `err` because it cannot buffer an unknown-length value. Once `on_start_string` returns a context, the parser enters streaming-string mode and later calls `on_string_write` and `on_string_done` directly, so those callbacks must also be supplied. For fixed-length blob, blob-error, and verbatim strings, `on_start_string` is optional; if it is missing or returns `NULL`, the parser falls back to `on_string` or `on_error`. If it returns a context for a fixed-length string, `on_string_write` and `on_string_done` are also called directly.

**Note:** top-level attributes (`|`) are delivered via callbacks but do not become the returned `obj`; parsing continues for the following reply.

### Example

```c
#define FIO_RESP3
#include "fio-stl.h"

static void *on_null(void *u) { (void)u; return (void *)1; }
static void *on_number(void *u, int64_t n) { (void)u; printf("num %lld\n", (long long)n); return (void *)1; }
static void *on_string(void *u, const void *d, size_t l, uint8_t t) {
  (void)u; (void)t;
  printf("str %.*s\n", (int)l, (const char *)d);
  return (void *)1;
}
static void *on_array(void *u, void *c, int64_t l) { (void)u; (void)c; (void)l; return (void *)2; }
static int array_push(void *u, void *c, void *v) { (void)u; (void)c; (void)v; return 0; }
static void *array_done(void *u, void *c) { (void)u; return c; }

int main(void) {
  static const fio_resp3_callbacks_s cb = {
    .on_null = on_null, .on_number = on_number, .on_string = on_string,
    .on_array = on_array, .array_push = array_push, .array_done = array_done,
  };
  const char *data = "*2\r\n:42\r\n$5\r\nhello\r\n";
  fio_resp3_parser_s p = {0};
  fio_resp3_result_s r = fio_resp3_parse(&p, &cb, data, strlen(data));
  printf("consumed=%zu err=%d obj=%p\n", r.consumed, r.err, r.obj);
  return 0;
}
```

------------------------------------------------------------
# Basic Socket Helpers

```c
#define FIO_SOCK
#include "fio-stl.h"
```

Portable socket helpers for POSIX and Windows: open, bind, connect, listen, wait, read, write, close, and a few sharp socket-shaped knives. Implemented in [`./004 sock.h`](./004%20sock.h).

`FIO_SOCK` depends on URL parsing internally and pulls in what it needs.

### Configuration Macros

#### `FIO_SOCK_DEFAULT_MAXIMIZE_LIMIT`

```c
#ifndef FIO_SOCK_DEFAULT_MAXIMIZE_LIMIT
#define FIO_SOCK_DEFAULT_MAXIMIZE_LIMIT (1ULL << 24)
#endif
```

Default target used by `fio_sock_maximize_limits(0)`.

#### `FIO_SOCK_AVOID_UMASK`

```c
/* optional compile-time flag */
#define FIO_SOCK_AVOID_UMASK
```

If defined before including the implementation, Unix socket creation avoids the temporary `umask` call used when making public Unix sockets. This avoids `umask`'s process-global race, but may affect permissions on systems where `chmod` on Unix socket files is not enough.

Windows behaves as if this concern does not apply.

### Types and Flags

#### `fio_socket_i`

```c
#if FIO_OS_WIN
typedef SOCKET fio_socket_i;
#else
typedef int fio_socket_i;
#endif
```

Native socket handle type. Use this instead of `int` when writing portable code.

#### `FIO_SOCKET_INVALID`

```c
#if FIO_OS_WIN
#define FIO_SOCKET_INVALID INVALID_SOCKET
#else
#define FIO_SOCKET_INVALID (-1)
#endif
```

Invalid socket sentinel.

#### `FIO_SOCK_FD_ISVALID`

```c
#define FIO_SOCK_FD_ISVALID(fd) ((fio_socket_i)(fd) != FIO_SOCKET_INVALID)
```

Returns non-zero if `fd` is not the invalid sentinel.

#### `fio_sock_open_flags_e`

```c
typedef enum {
  FIO_SOCK_SERVER = 0,
  FIO_SOCK_CLIENT = 1,
  FIO_SOCK_NONBLOCK = 2,
  FIO_SOCK_TCP = 4,
  FIO_SOCK_UDP = 8,
#ifdef AF_UNIX
  FIO_SOCK_UNIX = 16,
  FIO_SOCK_UNIX_PRIVATE = (16 | 32),
#else
#define FIO_SOCK_UNIX         0
#define FIO_SOCK_UNIX_PRIVATE 0
#endif
} fio_sock_open_flags_e;
```

Flags for `fio_sock_open`, `fio_sock_open2`, and `fio_sock_open_unix`.

**Values:**
- `FIO_SOCK_SERVER` - bind a local socket; TCP / stream sockets also call `listen`
- `FIO_SOCK_CLIENT` - connect a remote socket
- `FIO_SOCK_NONBLOCK` - set non-blocking mode
- `FIO_SOCK_TCP` - stream socket
- `FIO_SOCK_UDP` - datagram socket
- `FIO_SOCK_UNIX` - Unix domain socket where supported, otherwise `0`
- `FIO_SOCK_UNIX_PRIVATE` - Unix socket with private permissions where supported, otherwise `0`

`FIO_SOCK_TCP` and `FIO_SOCK_UDP` are exclusive. If neither is set, network sockets default to TCP.

### Opening Sockets

#### `fio_sock_open`

```c
FIO_IFUNC fio_socket_i fio_sock_open(const char *restrict address,
                                     const char *restrict port,
                                     uint16_t flags);
```

Creates a socket from an address / port pair and flags.

For `FIO_SOCK_UNIX`, `port` is ignored and `address` is treated as the Unix socket path. For server TCP sockets, `address == NULL` binds to all interfaces.

**Returns:** a socket handle, or `FIO_SOCKET_INVALID` on error.

#### `fio_sock_open2`

```c
SFUNC fio_socket_i fio_sock_open2(const char *url, uint16_t flags);
```

Creates a socket from a URL-ish string. If no port is present, the URL scheme is used as the port / service name. Schemes such as `tcp`, `udp`, `http`, and `https` can also imply socket type when flags omit it.

Unix path URLs are detected when supported. A `priv` scheme selects `FIO_SOCK_UNIX_PRIVATE`; other path-only Unix forms select `FIO_SOCK_UNIX`.

**Returns:** a socket handle, or `FIO_SOCKET_INVALID` on error.

#### `fio_sock_address_new`

```c
FIO_IFUNC struct addrinfo *fio_sock_address_new(const char *restrict address,
                                                const char *restrict port,
                                                int sock_type);
```

Resolves `address` / `port` into an `addrinfo` list for `sock_type`, usually `SOCK_STREAM` or `SOCK_DGRAM`.

Common service names are normalized before `getaddrinfo`: `ws` / `http` / `sse` to port `80`, and `wss` / `https` / `sses` to port `443`.

**Ownership:** free the result with `fio_sock_address_free`.

#### `fio_sock_address_free`

```c
FIO_IFUNC void fio_sock_address_free(struct addrinfo *a);
```

Frees an address list returned by `fio_sock_address_new`.

#### `fio_sock_open_local`

```c
SFUNC fio_socket_i fio_sock_open_local(struct addrinfo *addr, int nonblock);
```

Creates a network socket from an `addrinfo` list and binds it locally. `SO_REUSEADDR` is enabled. If `nonblock` is non-zero, non-blocking mode is requested before binding.

**Returns:** a socket handle, or `FIO_SOCKET_INVALID`.

#### `fio_sock_open_remote`

```c
SFUNC fio_socket_i fio_sock_open_remote(struct addrinfo *addr, int nonblock);
```

Creates a network socket from an `addrinfo` list and connects it remotely. Non-blocking connect progress (`EINPROGRESS` / Windows equivalents) returns the socket.

**Returns:** a socket handle, or `FIO_SOCKET_INVALID`.

#### `fio_sock_open_unix`

```c
SFUNC fio_socket_i fio_sock_open_unix(const char *address, uint16_t flags);
```

Creates a Unix domain socket where supported. Server sockets unlink an existing path before binding. Non-UDP server sockets call `listen`.

If Unix sockets are unsupported, the function asserts and returns `FIO_SOCKET_INVALID`.

### Portable Socket Operations

Use these wrappers when code should run on both POSIX and WinSock2.

#### `fio_sock_write`

```c
FIO_IFUNC ssize_t fio_sock_write(fio_socket_i fd, const void *buf, size_t len);
```

Acts like POSIX `write` on POSIX and uses `send` on Windows.

#### `fio_sock_read`

```c
FIO_IFUNC ssize_t fio_sock_read(fio_socket_i fd, void *buf, size_t len);
```

Acts like POSIX `read` on POSIX and uses `recv` on Windows.

#### `fio_sock_sendto`

```c
FIO_IFUNC ssize_t fio_sock_sendto(fio_socket_i fd,
                                  const void *buf,
                                  size_t len,
                                  int flags,
                                  const struct sockaddr *addr,
                                  socklen_t addrlen);
```

Portable `sendto` wrapper.

#### `fio_sock_recvfrom`

```c
FIO_IFUNC ssize_t fio_sock_recvfrom(fio_socket_i fd,
                                    void *buf,
                                    size_t len,
                                    int flags,
                                    struct sockaddr *addr,
                                    socklen_t *addrlen);
```

Portable `recvfrom` wrapper.

#### `fio_sock_close`

```c
FIO_IFUNC int fio_sock_close(fio_socket_i fd);
```

Portable close wrapper: `close` on POSIX, `closesocket` on Windows.

#### `fio_sock_accept`

```c
#if FIO_OS_WIN
IFUNC fio_socket_i fio_sock_accept(fio_socket_i s,
                                   struct sockaddr *addr,
                                   int *addrlen);
#else
#define fio_sock_accept(fd, addr, addrlen) accept(fd, addr, addrlen)
#endif
```

Portable accept wrapper.

#### `fio_sock_dup`

```c
FIO_IFUNC fio_socket_i fio_sock_dup(fio_socket_i fd);
```

Duplicates a socket handle. POSIX uses `dup` and sets `FD_CLOEXEC`; Windows uses `WSADuplicateSocket` and `WSASocket`.

#### `fio_sock_bind`

```c
FIO_IFUNC int fio_sock_bind(fio_socket_i fd,
                            const struct sockaddr *addr,
                            socklen_t addrlen);
```

Portable `bind` wrapper.

#### `fio_sock_connect`

```c
FIO_IFUNC int fio_sock_connect(fio_socket_i fd,
                               const struct sockaddr *addr,
                               socklen_t addrlen);
```

Portable `connect` wrapper.

#### `fio_sock_listen`

```c
FIO_IFUNC int fio_sock_listen(fio_socket_i fd, int backlog);
```

Portable `listen` wrapper.

#### `fio_sock_setsockopt`

```c
FIO_IFUNC int fio_sock_setsockopt(fio_socket_i fd,
                                  int level,
                                  int optname,
                                  const void *optval,
                                  socklen_t optlen);
```

Portable `setsockopt` wrapper. Pointer and length type differences are handled internally.

#### `fio_sock_socketpair`

```c
FIO_IFUNC int fio_sock_socketpair(fio_socket_i fds[2]);
```

Creates a connected socket pair. POSIX uses `socketpair(AF_UNIX, SOCK_STREAM, 0, fds)`. Windows creates a loopback TCP pair.

#### `fio_sock_pipe`

```c
FIO_IFUNC int fio_sock_pipe(fio_socket_i fds[2]);
```

Creates a pipe-like pair. POSIX uses `pipe`; Windows delegates to `fio_sock_socketpair` so the handles are usable with WinSock polling.

### Socket State and Waiting

#### `fio_sock_set_non_block`

```c
SFUNC int fio_sock_set_non_block(fio_socket_i fd);
```

Sets a socket to non-blocking mode.

**Returns:** `0` on success, `-1` on error.

#### `fio_sock_maximize_limits`

```c
SFUNC size_t fio_sock_maximize_limits(size_t maximum_limit);
```

Attempts to raise the open-file limit up to `maximum_limit`. Passing `0` uses `FIO_SOCK_DEFAULT_MAXIMIZE_LIMIT`.

On Windows this returns the advisory cap because there is no `RLIMIT_NOFILE` equivalent for sockets.

#### `fio_sock_wait_io`

```c
SFUNC short fio_sock_wait_io(fio_socket_i fd, short events, int timeout);
```

Waits for `events` on one socket using `poll` / `WSAPoll`. `timeout` is in milliseconds; `0` returns immediately.

**Returns:** `0` on timeout, `-1` on error, or event bits such as `POLLIN`, `POLLOUT`, `POLLHUP`, and `POLLNVAL`.

#### `FIO_SOCK_WAIT_RW`

```c
#define FIO_SOCK_WAIT_RW(fd, timeout_) \
  fio_sock_wait_io(fd, POLLIN | POLLOUT, timeout_)
```

Waits for read or write readiness.

#### `FIO_SOCK_WAIT_R`

```c
#define FIO_SOCK_WAIT_R(fd, timeout_) fio_sock_wait_io(fd, POLLIN, timeout_)
```

Waits for read readiness.

#### `FIO_SOCK_WAIT_W`

```c
#define FIO_SOCK_WAIT_W(fd, timeout_) fio_sock_wait_io(fd, POLLOUT, timeout_)
```

Waits for write readiness.

#### `FIO_SOCK_IS_OPEN`

```c
#ifdef POLLRDHUP
#define FIO_SOCK_IS_OPEN(fd)                                                   \
  (!(fio_sock_wait_io(fd, (POLLOUT | POLLRDHUP), 0) &                          \
     (POLLRDHUP | POLLHUP | POLLNVAL)))
#else
#define FIO_SOCK_IS_OPEN(fd)                                                   \
  (!(fio_sock_wait_io(fd, POLLOUT, 0) & (POLLHUP | POLLNVAL)))
#endif
```

Best-effort test for whether a socket still appears open.

#### `fio_sock_peer_addr`

```c
SFUNC fio_buf_info_s fio_sock_peer_addr(fio_socket_i s);
```

Returns a numeric, human-readable peer address.

On error, returns `{NULL, 0}`. The returned buffer is internal static storage, limited to 63 bytes. Thread-safety is limited to 128 threads / calls.

### Windows Notes

On Windows, the module initializes WinSock in a constructor and stores direct function pointers loaded from `Ws2_32.dll`. Wrapper functions translate common WinSock errors into `errno` values.

The `fio___sock_wsa*` helpers are internal implementation details, not regular application API. Tiny dragons; leave them napping.

### Ownership and Thread-Safety

Socket handles are owned by the caller and should be closed with `fio_sock_close`. `fio_sock_address_new` results are owned by the caller and should be freed with `fio_sock_address_free`.

Socket operations are as thread-safe as the operating system calls they wrap. `fio_sock_peer_addr` uses shared static buffers and has the 128-call/thread caveat described above. Unix socket creation may touch process-global `umask` unless `FIO_SOCK_AVOID_UMASK` is defined.

### Example

```c
#define FIO_SOCK
#include "fio-stl.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  fio_socket_i fds[2];
  char buf[8] = {0};

  if (fio_sock_pipe(fds))
    return 1;

  fio_sock_write(fds[1], "ping", 4);
  fio_sock_read(fds[0], buf, sizeof(buf) - 1);
  printf("%s\n", buf);

  fio_sock_close(fds[0]);
  fio_sock_close(fds[1]);
  return 0;
}
```

### TCP Server Sketch

```c
#define FIO_SOCK
#include "fio-stl.h"

int main(void) {
  fio_socket_i s = fio_sock_open(NULL, "8080",
                                 FIO_SOCK_SERVER | FIO_SOCK_TCP |
                                     FIO_SOCK_NONBLOCK);
  if (!FIO_SOCK_FD_ISVALID(s))
    return 1;

  if (FIO_SOCK_WAIT_R(s, 1000) & POLLIN) {
    fio_socket_i c = fio_sock_accept(s, NULL, NULL);
    if (FIO_SOCK_FD_ISVALID(c))
      fio_sock_close(c);
  }

  fio_sock_close(s);
  return 0;
}
```

---
# State Callbacks

```c
#define FIO_STATE
#include "fio-stl.h"
```

A small global event bus for lifecycle callbacks: initialize, fork, start, idle, shutdown, exit, and two user queues. Modules can listen without the caller knowing they exist. Sneaky, but polite. Implemented in [`./004 state callbacks.h`](./004%20state%20callbacks.h).

Use this as a global `FIO_EXTERN` module when multiple translation units should share the same callback registry.

### Types

#### `fio_state_event_type_e`

```c
typedef enum {
  FIO_CALL_ON_INITIALIZE,
  FIO_CALL_PRE_START,
  FIO_CALL_BEFORE_FORK,
  FIO_CALL_IN_CHILD,
  FIO_CALL_IN_MASTER,
  FIO_CALL_AFTER_FORK,
  FIO_CALL_ON_WORKER_THREAD_START,
  FIO_CALL_ON_START,
  FIO_CALL_RESERVED1,
  FIO_CALL_RESERVED2,
  FIO_CALL_ON_USER1,
  FIO_CALL_ON_USER2,
  FIO_CALL_ON_IDLE,
  FIO_CALL_ON_USER1_REVERSE,
  FIO_CALL_ON_USER2_REVERSE,
  FIO_CALL_RESERVED1_REVERSED,
  FIO_CALL_RESERVED2_REVERSED,
  FIO_CALL_ON_SHUTDOWN,
  FIO_CALL_ON_PARENT_CRUSH,
  FIO_CALL_ON_CHILD_CRUSH,
  FIO_CALL_ON_WORKER_THREAD_END,
  FIO_CALL_ON_STOP,
  FIO_CALL_AT_EXIT,
  FIO_CALL_AFTER_EXIT,
  FIO_CALL_NEVER
} fio_state_event_type_e;
```

Event queues. `FIO_CALL_NEVER` is the sentinel and should not be used as an event.

**Ordering:**
- events `<= FIO_CALL_ON_IDLE` run in registration order
- events `>= FIO_CALL_ON_SHUTDOWN` run in reverse registration order

`FIO_CALL_ON_USER1` / `FIO_CALL_ON_USER2` and their reverse variants are available for user code.

### Callback Management

#### `fio_state_callback_add`

```c
SFUNC void fio_state_callback_add(fio_state_event_type_e,
                                  void (*func)(void *),
                                  void *arg);
```

Adds `func(arg)` to an event queue. Adding the same function / argument pair replaces the existing map entry rather than creating a duplicate.

If `FIO_CALL_ON_INITIALIZE` already ran, adding another initialize callback calls it immediately.

#### `fio_state_callback_remove`

```c
SFUNC int fio_state_callback_remove(fio_state_event_type_e,
                                    void (*func)(void *),
                                    void *arg);
```

Removes `func(arg)` from an event queue.

**Returns:** the internal map removal result, or `-1` if the event is invalid.

#### `fio_state_callback_clear`

```c
SFUNC void fio_state_callback_clear(fio_state_event_type_e e);
```

Clears all callbacks for `e`. Invalid events are ignored.

This function is implemented in the header's implementation section; with `FIO_EXTERN`, expose the implementation in exactly one translation unit as usual.

#### `fio_state_callback_force`

```c
SFUNC void fio_state_callback_force(fio_state_event_type_e);
```

Runs the callbacks for an event as if the event happened.

During a forced event, the callback list is copied first. Additions and removals for the same event do not affect the current run.

Special behavior:
- `FIO_CALL_ON_INITIALIZE` runs only once and reserves internal maps
- `FIO_CALL_IN_CHILD` reinitializes internal locks and reseeds randomness
- `FIO_CALL_AFTER_FORK` reseeds randomness

### Debug Helpers

#### `fio_state_callback_print_state`

```c
FIO_IFUNC void fio_state_callback_print_state(void);
```

Prints callback counts and capacities for every event queue to `stderr`.

### Thread-Safety

Each event queue has its own lock. Adding, removing, clearing, and forcing callbacks are synchronized while touching the queue. Callback execution happens after the queue has been copied and unlocked.

### Example

```c
#define FIO_STATE
#include "fio-stl.h"
#include <stdio.h>

static void hello(void *arg) {
  printf("hello %s\n", (const char *)arg);
}

int main(void) {
  fio_state_callback_add(FIO_CALL_ON_USER1, hello, "callbacks");
  fio_state_callback_force(FIO_CALL_ON_USER1);
  fio_state_callback_remove(FIO_CALL_ON_USER1, hello, "callbacks");
  return 0;
}
```

---
# Time Helpers

```c
#define FIO_TIME
#include "fio-stl.h"
```

Wall-clock and monotonic time helpers, UTC calendar conversion, and a handful of date formatters. Tiny clock toolbox, no sundial required. Implemented in [`./004 time.h`](./004%20time.h).

### Collecting Time

#### `fio_time_real`

```c
FIO_IFUNC struct timespec fio_time_real(void);
```

Returns human / watch time using `CLOCK_REALTIME`. Good for timestamps, less good for measuring elapsed time because the system clock may jump.

#### `fio_time_mono`

```c
FIO_IFUNC struct timespec fio_time_mono(void);
```

Returns monotonic time using `CLOCK_MONOTONIC`. Prefer this for intervals and deadlines.

#### `fio_time_nano`

```c
FIO_IFUNC int64_t fio_time_nano(void);
```

Returns monotonic time in nanoseconds.

#### `fio_time_micro`

```c
FIO_IFUNC int64_t fio_time_micro(void);
```

Returns monotonic time in microseconds.

#### `fio_time_milli`

```c
FIO_IFUNC int64_t fio_time_milli(void);
```

Returns monotonic time in milliseconds.

### Conversion

#### `fio_time2milli`

```c
FIO_IFUNC int64_t fio_time2milli(struct timespec);
```

Converts a `struct timespec` to milliseconds.

#### `fio_time2micro`

```c
FIO_IFUNC int64_t fio_time2micro(struct timespec);
```

Converts a `struct timespec` to microseconds.

#### `fio_time2gm`

```c
SFUNC struct tm fio_time2gm(time_t time);
```

Converts `time` to a UTC `struct tm`. This is a faster, less localized alternative to `gmtime_r`.

#### `fio_gm2time`

```c
SFUNC time_t fio_gm2time(struct tm tm);
```

Converts a UTC `struct tm` to seconds. If `tm.tm_isdst > 0`, one hour is subtracted. On platforms with `tm_gmtoff`, that offset is added.

### Date Formatting

All formatting functions write to `target`, NUL-terminate the output, and return the number of bytes written before the NUL. Provide a buffer with enough space; `64` bytes is comfortably boring.

#### `fio_time2rfc7231`

```c
SFUNC size_t fio_time2rfc7231(char *target, time_t time);
```

Writes an RFC 7231 / HTTP date, usually 29 characters:

```text
Sun, 06 Nov 1994 08:49:37 GMT
```

#### `fio_time2rfc2109`

```c
SFUNC size_t fio_time2rfc2109(char *target, time_t time);
```

Writes an RFC 2109 cookie date, usually 31 characters:

```text
Sun, 06 Nov 1994 08:49:37 -0000
```

#### `fio_time2rfc2822`

```c
SFUNC size_t fio_time2rfc2822(char *target, time_t time);
```

Writes an RFC 2822 date, usually 28-29 characters:

```text
Sun, 6-Nov-1994 08:49:37 GMT
```

#### `fio_time2log`

```c
SFUNC size_t fio_time2log(char *target, time_t time);
```

Writes common log format:

```text
[DD/MMM/yyyy:hh:mm:ss +0000]
```

Usually needs 29 bytes including the NUL terminator.

#### `fio_time2iso`

```c
SFUNC size_t fio_time2iso(char *target, time_t time);
```

Writes an ISO-ish representation. The header comment says `YYYY-MM-DD HH:MM:SS`; the implementation writes a 3-letter month name:

```text
YYYY-MMM-DD HH:MM:SS
```

Usually needs 20 bytes including the NUL terminator.

### Arithmetic

#### `fio_time_add`

```c
FIO_IFUNC struct timespec fio_time_add(struct timespec t, struct timespec t2);
```

Adds two `struct timespec` values and normalizes the nanoseconds field.

#### `fio_time_add_milli`

```c
FIO_IFUNC struct timespec fio_time_add_milli(struct timespec t, int64_t milli);
```

Adds milliseconds to `t` and normalizes the result. The implementation splits milliseconds with a `1024`-millisecond approximation before normalization; cute, but be aware.

#### `fio_time_cmp`

```c
FIO_IFUNC int fio_time_cmp(struct timespec t1, struct timespec t2);
```

Compares two `struct timespec` values.

**Returns:** `-1` if `t1 < t2`, `0` if equal, `1` if `t1 > t2`.

### Example

```c
#define FIO_TIME
#include "fio-stl.h"
#include <stdio.h>
#include <time.h>

int main(void) {
  struct timespec start = fio_time_mono();
  struct timespec later = fio_time_add_milli(start, 500);

  printf("now: %lld ms\n", (long long)fio_time_milli());
  printf("later is after start: %d\n", fio_time_cmp(later, start) > 0);

  char date[64];
  fio_time2rfc7231(date, fio_time_real().tv_sec);
  printf("http date: %s\n", date);

  return 0;
}
```

---
# URL-Encoded Parser

```c
#define FIO_URL_ENCODED
#include "fio-stl.h"
```

A non-allocating parser for `application/x-www-form-urlencoded` data. It splits `name=value&...` pairs and leaves decoding to you. No copies, no drama. Implemented in [`./004 urlencoded.h`](./004%20urlencoded.h).

### Types

#### `fio_url_encoded_parser_callbacks_s`

```c
typedef struct {
  void *(*on_pair)(void *udata, fio_buf_info_s name, fio_buf_info_s value);
  void (*on_error)(void *udata);
} fio_url_encoded_parser_callbacks_s;
```

Parser callbacks. `udata` is passed to callbacks and updated from `on_pair`'s return value.

**Members:**
- `on_pair` - called for each pair. `name` and `value` point into the original input and are **not decoded**. Return non-NULL to continue, or `NULL` to stop.
- `on_error` - optional and currently unused; reserved because parsers enjoy having a rainy-day pocket.

If `callbacks` or a member is `NULL`, an internal no-op callback is used.

#### `fio_url_encoded_result_s`

```c
typedef struct {
  size_t consumed;
  size_t count;
  int err;
} fio_url_encoded_result_s;
```

Parse result.

**Members:**
- `consumed` - number of input bytes consumed
- `count` - number of non-empty pairs reported
- `err` - non-zero when parsing stopped because `on_pair` returned `NULL`

### Parsing

#### `fio_url_encoded_parse`

```c
SFUNC fio_url_encoded_result_s
fio_url_encoded_parse(const fio_url_encoded_parser_callbacks_s *callbacks,
                      void *udata,
                      const char *data,
                      size_t len);
```

Parses `data[0..len)` as URL-encoded form data.

Rules from the implementation:
- pairs are separated by `&`
- the first `=` in a pair separates name and value
- `name=` is valid and has an empty value
- `name` is valid and has an empty value
- `=value` is valid and has an empty name
- empty pairs such as leading `&` or `&&` are skipped
- percent escapes are not decoded

Use a string helper such as `fio_string_write_url_dec` when you need decoded values.

**Ownership:** callback buffers borrow the original `data`; keep `data` alive while parsing and copy anything you need later.

**Thread-safety:** parsing uses only caller-provided state and stack locals.

### Example

```c
#define FIO_URL_ENCODED
#include "fio-stl.h"
#include <stdio.h>
#include <string.h>

static void *on_pair(void *udata, fio_buf_info_s name, fio_buf_info_s value) {
  printf("%.*s = %.*s\n",
         (int)name.len, name.buf,
         (int)value.len, value.buf);
  return udata;
}

int main(void) {
  const char form[] = "name=Bo&empty=&encoded=a%20b&&solo";
  static const fio_url_encoded_parser_callbacks_s cb = {
      .on_pair = on_pair,
  };

  fio_url_encoded_result_s r =
      fio_url_encoded_parse(&cb, (void *)1, form, strlen(form));

  printf("pairs=%zu consumed=%zu err=%d\n", r.count, r.consumed, r.err);
  return r.err;
}
```

---
# CLI Helpers

```c
#define FIO_CLI
#include "fio-stl.h"
```

A small command-line parser with auto-generated help, typed flags, and unnamed-argument support. It is easier to set up than `getopt` and stays out of the way once you have your values.

Defining `FIO_CLI` also pulls in `FIO_ATOL`, `FIO_RAND`, and `FIO_IMAP` because the parser needs them internally.

**Note:** the module keeps a global data table. Parsing and setting values is **not** thread-safe. Reading values is safe after parsing is complete.

---

## Types

#### `fio_cli_arg_e`

```c
typedef enum {
  FIO_CLI_ARG_STRING,
  FIO_CLI_ARG_BOOL,
  FIO_CLI_ARG_INT,
  FIO_CLI_ARG_PRINT,
  FIO_CLI_ARG_PRINT_LINE,
  FIO_CLI_ARG_PRINT_HEADER,
} fio_cli_arg_e;
```

Argument type tags used internally and returned by `fio_cli_type`.

**Values:**
- `FIO_CLI_ARG_STRING` — accepts any string value.
- `FIO_CLI_ARG_BOOL` — a flag; presence means true.
- `FIO_CLI_ARG_INT` — validates the value as an integer.
- `FIO_CLI_ARG_PRINT` — indented help text line.
- `FIO_CLI_ARG_PRINT_LINE` — unindented help text line.
- `FIO_CLI_ARG_PRINT_HEADER` — section header in help output.

---

## Argument Definition Macros

Use these inside the `fio_cli_start` macro to describe each flag and help line.

#### `FIO_CLI_STRING`

```c
#define FIO_CLI_STRING(line) /* ... */
```

Declares a string argument.

```c
FIO_CLI_STRING("-o -output (stdout) output file path")
```

#### `FIO_CLI_INT`

```c
#define FIO_CLI_INT(line) /* ... */
```

Declares an integer argument. The parser validates the value with `fio_atol`.

```c
FIO_CLI_INT("-p -port (8080) the port number to use")
```

#### `FIO_CLI_BOOL`

```c
#define FIO_CLI_BOOL(line) /* ... */
```

Declares a boolean flag. Boolean flags cannot have default values. They can be chained, e.g. `-abc` is treated as `-a -b -c`.

```c
FIO_CLI_BOOL("-v -verbose enable verbose logging")
```

#### `FIO_CLI_PRINT`

```c
#define FIO_CLI_PRINT(line) /* ... */
```

Prints an extra indented help line after the previous argument.

```c
FIO_CLI_INT("-p -port (8080) the port number"),
FIO_CLI_PRINT("Set to 0 for Unix socket mode.")
```

#### `FIO_CLI_PRINT_LINE`

```c
#define FIO_CLI_PRINT_LINE(line) /* ... */
```

Prints a help line without indentation.

#### `FIO_CLI_PRINT_HEADER`

```c
#define FIO_CLI_PRINT_HEADER(line) /* ... */
```

Prints a section header in the help output.

```c
FIO_CLI_PRINT_HEADER("Network Options:")
```

---

## Initialization and Cleanup

#### `fio_cli_start`

```c
#define fio_cli_start(argc, argv, unnamed_min, unnamed_max, description, ...)  \
  fio_cli_start((argc),                                                        \
                (argv),                                                        \
                (unnamed_min),                                                 \
                (unnamed_max),                                                 \
                (description),                                                 \
                (fio___cli_line_s[]){__VA_ARGS__, {0}})

SFUNC void fio_cli_start FIO_NOOP(int argc,
                                  char const *argv[],
                                  int unnamed_min,
                                  int unnamed_max,
                                  char const *description,
                                  fio___cli_line_s *arguments);
```

Parses `argv` into a dictionary of named and unnamed arguments, then prints help and exits on `-h`, `-?`, `-help`, or `--help`.

**Named Arguments:**
- `argc`, `argv` — values passed to `main`.
- `unnamed_min` — minimum required unnamed arguments. `-1` means no limit on unnamed arguments.
- `unnamed_max` — maximum allowed unnamed arguments. `-1` means unlimited.
- `description` — program description shown in help. The text `NAME` is replaced with `argv[0]`.
- `...` — argument description lines using the macros above.

Argument names must start with `-`. The first non-`-` word begins the description. Optional defaults go in parentheses: `(default)` or `("literal default")`.

Accepted input formats:

```text
app -t=1 -p3000 -a localhost
app -t 1 -p 3000 -a localhost
app --threads=1 --port=3000 --address=localhost
```

**Note:** this function is **not** thread-safe.

```c
#define FIO_CLI
#include "fio-stl.h"

int main(int argc, char const *argv[]) {
  fio_cli_start(argc, argv, 0, -1,
                "NAME - a small CLI example.\n",
                FIO_CLI_PRINT_HEADER("Options:"),
                FIO_CLI_STRING("-s -str (hello) a string"),
                FIO_CLI_INT("-i -int (42) an integer"),
                FIO_CLI_BOOL("-b -bool a boolean flag"),
                FIO_CLI_PRINT("Boolean flags do not take values."));

  fprintf(stderr, "string: %s\n", fio_cli_get("-s"));
  fprintf(stderr, "int: %" PRId64 "\n", fio_cli_get_i("-i"));
  fprintf(stderr, "bool: %d\n", (int)fio_cli_get_bool("-b"));

  fio_cli_end();
  return 0;
}
```

#### `fio_cli_end`

```c
SFUNC void fio_cli_end(void);
```

Frees the parsed CLI dictionary.

A destructor runs this automatically at program exit, but calling it earlier lets the memory be reused.

**Note:** this function is **not** thread-safe.

---

## Getting Values

#### `fio_cli_get`

```c
SFUNC char const *fio_cli_get(char const *name);
```

Returns the argument value as a NUL-terminated C string, or `NULL` if the argument was not provided. If `name` is `NULL`, returns the first unnamed argument.

#### `fio_cli_get_str`

```c
SFUNC fio_buf_info_s fio_cli_get_str(char const *name);
```

Returns the argument value as a `fio_buf_info_s` with length. If `name` is `NULL`, returns the first unnamed argument.

#### `fio_cli_get_i`

```c
SFUNC int64_t fio_cli_get_i(char const *name);
```

Returns the argument value parsed as an integer, or `0` if it was not provided. Integer parsing accepts decimal, hex, octal, and binary prefixes.

#### `fio_cli_get_bool`

```c
#define fio_cli_get_bool(name) (fio_cli_get((name)) != NULL)
```

Returns non-zero if the argument was provided, including by default.

#### `fio_cli_type`

```c
SFUNC fio_cli_arg_e fio_cli_type(char const *name);
```

Returns the declared argument type, or `FIO_CLI_ARG_NONE` if the name is unknown.

---

## Unnamed Arguments

#### `fio_cli_unnamed_count`

```c
SFUNC unsigned int fio_cli_unnamed_count(void);
```

Returns the number of unnamed arguments collected.

#### `fio_cli_unnamed`

```c
SFUNC char const *fio_cli_unnamed(unsigned int index);
```

Returns the unnamed argument at `index` as a NUL-terminated string, or `NULL` if `index` is out of bounds.

#### `fio_cli_unnamed_str`

```c
SFUNC fio_buf_info_s fio_cli_unnamed_str(unsigned int index);
```

Returns the unnamed argument at `index` as a `fio_buf_info_s`, or an empty `fio_buf_info_s` if out of bounds.

---

## Setting Values

#### `fio_cli_set`

```c
SFUNC void fio_cli_set(char const *name, char const *value);
```

Sets or overwrites a named argument's value. If `name` is `NULL`, appends `value` as a new unnamed argument.

**Note:** this function is **not** thread-safe.

#### `fio_cli_set_i`

```c
SFUNC void fio_cli_set_i(char const *name, int64_t i);
```

Sets a named argument to a base-10 string representation of `i`.

**Note:** this function is **not** thread-safe.

#### `fio_cli_set_unnamed`

```c
SFUNC unsigned int fio_cli_set_unnamed(unsigned int index, const char *value);
```

Sets or appends an unnamed argument. If `index` is past the end, the value is appended. Returns the stored index, or `(unsigned int)-1` if `value` is `NULL` or empty.

**Note:** this function is **not** thread-safe.

---

## Iteration

#### `fio_cli_each`

```c
SFUNC size_t fio_cli_each(int (*task)(fio_buf_info_s name,
                                      fio_buf_info_s value,
                                      fio_cli_arg_e arg_type,
                                      void *udata),
                          void *udata);
```

Calls `task` for every argument that has a value, returning the number of calls made. If `task` returns non-zero, iteration stops. For unnamed arguments, `name.buf` is `NULL` and `name.len` is `0`.

------------------------------------------------------------
# Memory Allocator

```c
#define FIO_MEMORY_NAME fio
#include "fio-stl.h"
```

A concurrent, per-arena memory allocator that groups objects with similar lifetimes. It tries to keep related allocations in the same chunk so cleanup is cheap and locality is decent. Define `FIO_MEMORY_DISABLE` to route everything to the system `malloc`/`free` instead.

The prefix `fio` comes from `FIO_MEMORY_NAME`. All public function names are built with `FIO_NAME(FIO_MEMORY_NAME, ...)`, so a custom prefix produces `my_malloc`, `my_free`, and so on.

The module also sets the temporary allocator macros (`FIO_MEM_REALLOC_`, `FIO_MEM_FREE_`, `FIO_MEM_REALLOC_IS_SAFE_`, `FIO_MEM_ALIGNMENT_SIZE_`) for any facil.io type defined in the same `include` scope.

**Note:** `FIO_MALLOC` is a shortcut that defines this allocator with prefix `fio` and general-purpose tuning.

---

## Core API

#### `fio_malloc`

```c
SFUNC void *FIO_MEM_ALIGN_NEW FIO_NAME(FIO_MEMORY_NAME, malloc)(size_t size);
```

Allocates `size` bytes aligned to `FIO_MEMORY_ALIGN_SIZE`. Memory is zeroed if `FIO_MEMORY_INITIALIZE_ALLOCATIONS` is enabled. Allocations above the arena limit use big-block mode or `mmap`.

**Parameters:**
- `size` — bytes to allocate.

**Returns:** pointer to allocated memory, or `NULL` on failure.

#### `fio_calloc`

```c
SFUNC void *FIO_MEM_ALIGN_NEW FIO_NAME(FIO_MEMORY_NAME,
                                       calloc)(size_t size_per_unit,
                                               size_t unit_count);
```

Equivalent to `fio_malloc(size_per_unit * unit_count)`.

#### `fio_realloc`

```c
SFUNC void *FIO_MEM_ALIGN FIO_NAME(FIO_MEMORY_NAME, realloc)(void *ptr,
                                                             size_t new_size);
```

Reallocates `ptr` to `new_size`. Copy-free expansion is only attempted for large allocations. Prefer `fio_realloc2` for predictable zeroing.

**Parameters:**
- `ptr` — existing allocation, or `NULL`.
- `new_size` — requested new size.

**Returns:** pointer to the new block, or `NULL` on failure.

#### `fio_realloc2`

```c
SFUNC void *FIO_MEM_ALIGN FIO_NAME(FIO_MEMORY_NAME, realloc2)(void *ptr,
                                                              size_t new_size,
                                                              size_t copy_len);
```

Reallocates `ptr` to `new_size`, copying at most `copy_len` bytes. Memory beyond `copy_len` is zeroed when `FIO_MEMORY_INITIALIZE_ALLOCATIONS` is enabled.

**Parameters:**
- `ptr` — existing allocation, or `NULL`.
- `new_size` — requested new size.
- `copy_len` — maximum bytes to copy from the old block.

**Returns:** pointer to the new block, or `NULL` on failure.

#### `fio_free`

```c
SFUNC void FIO_NAME(FIO_MEMORY_NAME, free)(void *ptr);
```

Frees memory allocated by this allocator. Freeing a pointer from a different allocator, or after cleanup, is undefined behavior.

#### `fio_mmap`

```c
SFUNC void *FIO_MEM_ALIGN_NEW FIO_NAME(FIO_MEMORY_NAME, mmap)(size_t size);
```

Allocates directly from the system with `mmap` semantics. Use this for large, long-lived objects. The allocation is slower but avoids arena bookkeeping.

**Parameters:**
- `size` — bytes to allocate.

**Returns:** pointer to allocated memory, or `NULL` on failure.

#### `fio_malloc_after_fork`

```c
SFUNC void FIO_NAME(FIO_MEMORY_NAME, malloc_after_fork)(void);
```

Reinitializes allocator locks after `fork` in the child process. Best effort only: forking a multi-threaded process is unsafe. Prefer calling `fio_state_callback_force(FIO_CALL_IN_CHILD);` to reset all registered allocators.

#### `fio_realloc_is_safe`

```c
FIO_IFUNC size_t FIO_NAME(FIO_MEMORY_NAME, realloc_is_safe)(void);
```

Returns non-zero if the allocator zeroes allocations, which makes `fio_realloc2` safe for the uncopied tail.

---

## Configuration Macros

#### `FIO_MEMORY_NAME`

```c
#define FIO_MEMORY_NAME fio
```

Required prefix for the allocator's public symbols. Must be defined before including the header.

#### `FIO_MALLOC`

```c
#define FIO_MALLOC
```

Shortcut that defines the allocator with prefix `fio` and tunes it for general use. It also redefines `FIO_MEM_REALLOC`, `FIO_MEM_FREE`, `FIO_MEM_REALLOC_IS_SAFE`, and `FIO_MEM_ALIGNMENT_SIZE`.

#### `FIO_MEMORY_DISABLE`

```c
#define FIO_MEMORY_DISABLE
```

Bypasses the custom allocator and routes all allocations to the system `malloc`/`free`.

#### `FIO_MEMORY_ALIGN_LOG`

```c
#define FIO_MEMORY_ALIGN_LOG 6
```

Log2 of the allocation alignment. Clamped to the range `3`–`10` (8 to 1024 bytes). Default is `6` (64 bytes).

#### `FIO_MEMORY_SYS_ALLOCATION_SIZE_LOG`

```c
#define FIO_MEMORY_SYS_ALLOCATION_SIZE_LOG 21
```

Log2 of the chunk size allocated from the system. Clamped to `17`–`24`. Default is `21` (~2 MB).

#### `FIO_MEMORY_BLOCKS_PER_ALLOCATION_LOG`

```c
#define FIO_MEMORY_BLOCKS_PER_ALLOCATION_LOG 2
```

Log2 of the number of blocks per system chunk. Range `0`–`4`. Affects fragmentation and the threshold for big allocations.

#### `FIO_MEMORY_ENABLE_BIG_ALLOC`

```c
#define FIO_MEMORY_ENABLE_BIG_ALLOC 1
```

When enabled, large allocations can consume an entire chunk as a single big block. May increase fragmentation for long-lived objects.

#### `FIO_MEMORY_ARENA_COUNT`

```c
#define FIO_MEMORY_ARENA_COUNT -1
```

Number of arenas. Negative or zero means dynamic selection based on CPU core count, capped by `FIO_MEMORY_ARENA_COUNT_MAX` and fallback `FIO_MEMORY_ARENA_COUNT_FALLBACK`.

#### `FIO_MEMORY_ARENA_COUNT_FALLBACK`

```c
#define FIO_MEMORY_ARENA_COUNT_FALLBACK 24
```

Default arena count when dynamic CPU detection fails.

#### `FIO_MEMORY_ARENA_COUNT_MAX`

```c
#define FIO_MEMORY_ARENA_COUNT_MAX 64
```

Upper bound for dynamic arena count.

#### `FIO_MEMORY_USE_THREAD_MUTEX`

```c
#define FIO_MEMORY_USE_THREAD_MUTEX 0
```

When `1`, arenas protect themselves with `pthread` mutexes instead of spinlocks. Default is `1` when `FIO_MEMORY_ARENA_COUNT` is positive.

#### `FIO_MEMORY_INITIALIZE_ALLOCATIONS`

```c
#define FIO_MEMORY_INITIALIZE_ALLOCATIONS \
        FIO_MEMORY_INITIALIZE_ALLOCATIONS_DEFAULT
```

When `1`, allocations return zeroed memory and `realloc2` zeroes uncopied bytes. Default is `1`.

#### `FIO_MEMORY_INITIALIZE_ALLOCATIONS_DEFAULT`

```c
#define FIO_MEMORY_INITIALIZE_ALLOCATIONS_DEFAULT 1
```

Default value for `FIO_MEMORY_INITIALIZE_ALLOCATIONS`.

#### `FIO_MEMORY_CACHE_SLOTS`

```c
#define FIO_MEMORY_CACHE_SLOTS 4
```

Number of freed chunks to keep cached before returning memory to the system.

#### `FIO_MEMORY_WARMUP`

```c
#define FIO_MEMORY_WARMUP 0
```

If set to a positive number, that many arenas pre-allocate a block at startup. Usually best left at `0`.

#### `FIO_MEM_SYS_ALLOC`, `FIO_MEM_SYS_REALLOC`, `FIO_MEM_SYS_FREE`

```c
#define FIO_MEM_SYS_ALLOC(pages, alignment_log) /* ... */
#define FIO_MEM_SYS_REALLOC(ptr, old_pages, new_pages, alignment_log) /* ... */
#define FIO_MEM_SYS_FREE(ptr, pages) /* ... */
```

Override all three to replace the system allocation backend. Alignment is essential and may be large.

---

## Introspection

These functions are exposed for debugging. Treat them as unstable.

#### `fio_malloc_arenas`

```c
SFUNC size_t FIO_NAME(FIO_MEMORY_NAME, malloc_arenas)(void);
```

Returns the number of arenas.

#### `fio_malloc_block_size`

```c
SFUNC size_t FIO_NAME(FIO_MEMORY_NAME, malloc_block_size)(void);
```

Returns the block size used for arena allocations.

#### `fio_malloc_sys_alloc_size`

```c
FIO_IFUNC size_t FIO_NAME(FIO_MEMORY_NAME, malloc_sys_alloc_size)(void);
```

Returns the system chunk size in bytes.

#### `fio_malloc_cache_slots`

```c
FIO_IFUNC size_t FIO_NAME(FIO_MEMORY_NAME, malloc_cache_slots)(void);
```

Returns the configured number of cache slots.

#### `fio_malloc_alignment`

```c
FIO_IFUNC size_t FIO_NAME(FIO_MEMORY_NAME, malloc_alignment)(void);
```

Returns the allocation alignment in bytes.

#### `fio_malloc_alignment_log`

```c
FIO_IFUNC size_t FIO_NAME(FIO_MEMORY_NAME, malloc_alignment_log)(void);
```

Returns log2 of the allocation alignment.

#### `fio_malloc_alloc_limit`

```c
FIO_IFUNC size_t FIO_NAME(FIO_MEMORY_NAME, malloc_alloc_limit)(void);
```

Returns the size above which allocations bypass arena blocks.

#### `fio_malloc_arena_alloc_limit`

```c
FIO_IFUNC size_t FIO_NAME(FIO_MEMORY_NAME, malloc_arena_alloc_limit)(void);
```

Returns the arena block size limit.

#### `fio_malloc_print_state`

```c
SFUNC void FIO_NAME(FIO_MEMORY_NAME, malloc_print_state)(void);
```

Prints allocator state to `stderr`.

#### `fio_malloc_print_free_block_list`

```c
SFUNC void FIO_NAME(FIO_MEMORY_NAME, malloc_print_free_block_list)(void);
```

Prints the free-block list to `stderr`.

#### `fio_malloc_print_settings`

```c
SFUNC void FIO_NAME(FIO_MEMORY_NAME, malloc_print_settings)(void);
```

Prints allocator settings to `stderr`.

---

## Example

```c
#define FIO_MALLOC
#include "fio-stl.h"

int main(void) {
  char *buf = fio_malloc(256);
  if (!buf) return -1;

  fio_memcpy(buf, "hello", 5);
  fprintf(stderr, "%s\n", buf);

  char *buf2 = fio_realloc2(buf, 512, 5);
  if (!buf2) {
    fio_free(buf);
    return -1;
  }
  buf = buf2;

  fio_free(buf);
  return 0;
}
```

------------------------------------------------------------
# String Core

```c
#define FIO_STR
#include "fio-stl.h"
```

Binary-safe string helpers for building, editing, escaping, encoding, and comparing text. This module works with the lightweight `fio_str_info_s` and `fio_buf_info_s` descriptors, plus two owned string types: copy-on-write `fio_bstr` and hash-map key string `fio_keystr_s`.

All authoring helpers take a `fio_string_realloc_fn` callback so they can grow either a stack buffer or a heap allocation.

---

## Types

#### `fio_string_realloc_fn`

```c
typedef int (*fio_string_realloc_fn)(fio_str_info_s *dest, size_t len);
```

Callback that grows `dest->buf` to at least `len + 1` bytes and updates `dest->capa`. Returns `0` on success, `-1` on failure.

#### `fio_string_write_s`

```c
typedef struct {
  size_t klass;
  union {
    struct { size_t len; const char *buf; } str;
    double f;
    int64_t i;
    uint64_t u;
  } info;
} fio_string_write_s;
```

Argument item used by `fio_string_write2` and `fio_bstr_write2`. The `klass` field selects the union member: `1` = string, `2` = signed int, `3` = unsigned int, `4` = hex, `5` = binary, `6` = double.

---

## Authorship Helpers

#### `fio_string_write`

```c
SFUNC int fio_string_write(fio_str_info_s *dest,
                           fio_string_realloc_fn reallocate,
                           const void *restrict src,
                           size_t len);
```

Appends `len` bytes from `src` to `dest`. Grows `dest` if `reallocate` is provided; otherwise truncates when full. Always NUL-terminates on success.

**Parameters:**
- `dest` — target string descriptor. Must have a valid buffer and `capa >= len`.
- `reallocate` — growth callback, may be `NULL`.
- `src` — data to append.
- `len` — bytes to append.

**Returns:** `0` on success, `-1` if reallocation was needed and failed.

#### `fio_string_replace`

```c
SFUNC int fio_string_replace(fio_str_info_s *dest,
                             fio_string_realloc_fn reallocate,
                             intptr_t start_pos,
                             size_t overwrite_len,
                             const void *src,
                             size_t len);
```

Inserts, overwrites, or deletes a slice. Negative `start_pos` counts from the end (`-1` = last byte). When `overwrite_len` is `0`, the data is inserted. When `len` is `0`, the slice is deleted.

**Parameters:**
- `start_pos` — byte offset where editing begins.
- `overwrite_len` — bytes to overwrite or remove.
- `src` — replacement data, ignored if `len == 0`.
- `len` — replacement length.

**Returns:** `0` on success, `-1` on failure.

#### `fio_string_write2`

```c
SFUNC int fio_string_write2(fio_str_info_s *restrict dest,
                            fio_string_realloc_fn reallocate,
                            const fio_string_write_s srcs[]);

#define fio_string_write2(dest, reallocate, ...)                               \
  fio_string_write2((dest),                                                    \
                    (reallocate),                                              \
                    (fio_string_write_s[]){__VA_ARGS__, {0}})
```

Appends a mixed list of strings and numbers in a single pass. The macro builds a temporary `fio_string_write_s[]` array and appends a zero terminator.

```c
fio_string_write2(&str, my_realloc,
                  FIO_STRING_WRITE_STR1("The answer is: "),
                  FIO_STRING_WRITE_NUM(42),
                  FIO_STRING_WRITE_STR2(" (0x", 4),
                  FIO_STRING_WRITE_HEX(42),
                  FIO_STRING_WRITE_STR2(")", 1));
```

#### `FIO_STRING_WRITE_*`

```c
#define FIO_STRING_WRITE_STR1(str_) /* string with FIO_STRLEN length */
#define FIO_STRING_WRITE_STR2(str_, len_) /* string with explicit length */
#define FIO_STRING_WRITE_STR_INFO(str_) /* from fio_str_info_s */
#define FIO_STRING_WRITE_NUM(num)     /* signed integer */
#define FIO_STRING_WRITE_UNUM(num)    /* unsigned integer */
#define FIO_STRING_WRITE_HEX(num)     /* hex representation */
#define FIO_STRING_WRITE_BIN(num)     /* binary representation */
#define FIO_STRING_WRITE_FLOAT(num)   /* double */
```

Helper macros that build `fio_string_write_s` items for `fio_string_write2`.

---

## Numeral Helpers

#### `fio_string_write_i`

```c
SFUNC int fio_string_write_i(fio_str_info_s *dest,
                             fio_string_realloc_fn reallocate,
                             int64_t i);
```

Appends a signed base-10 integer.

#### `fio_string_write_u`

```c
SFUNC int fio_string_write_u(fio_str_info_s *dest,
                             fio_string_realloc_fn reallocate,
                             uint64_t i);
```

Appends an unsigned base-10 integer.

#### `fio_string_write_hex`

```c
SFUNC int fio_string_write_hex(fio_str_info_s *dest,
                               fio_string_realloc_fn reallocate,
                               uint64_t i);
```

Appends a lowercase hex representation.

#### `fio_string_write_bin`

```c
SFUNC int fio_string_write_bin(fio_str_info_s *dest,
                               fio_string_realloc_fn reallocate,
                               uint64_t i);
```

Appends a binary representation.

---

## printf Support

#### `fio_string_printf`

```c
SFUNC FIO___PRINTF_STYLE(3, 0) int fio_string_printf(
    fio_str_info_s *dest,
    fio_string_realloc_fn reallocate,
    const char *format,
    ...);
```

Appends formatted output, growing the buffer as needed.

#### `fio_string_vprintf`

```c
SFUNC FIO___PRINTF_STYLE(3, 0) int fio_string_vprintf(
    fio_str_info_s *dest,
    fio_string_realloc_fn reallocate,
    const char *format,
    va_list argv);
```

`va_list` variant of `fio_string_printf`.

---

## Escaping and Encoding

#### `fio_string_write_escape`

```c
SFUNC int fio_string_write_escape(fio_str_info_s *restrict dest,
                                  fio_string_realloc_fn reallocate,
                                  const void *raw,
                                  size_t raw_len);
```

Escapes `raw` using JSON string rules and appends the result.

#### `fio_string_write_unescape`

```c
SFUNC int fio_string_write_unescape(fio_str_info_s *dest,
                                    fio_string_realloc_fn reallocate,
                                    const void *enscaped,
                                    size_t enscaped_len);
```

Unescapes JSON-style data and appends it.

#### `fio_string_write_base32enc` / `fio_string_write_base32dec`

```c
SFUNC int fio_string_write_base32enc(fio_str_info_s *dest,
                                     fio_string_realloc_fn reallocate,
                                     const void *raw,
                                     size_t raw_len);
SFUNC int fio_string_write_base32dec(fio_str_info_s *dest,
                                     fio_string_realloc_fn reallocate,
                                     const void *encoded,
                                     size_t encoded_len);
```

Base32 encode / decode.

#### `fio_string_write_base64enc` / `fio_string_write_base64dec`

```c
SFUNC int fio_string_write_base64enc(fio_str_info_s *dest,
                                     fio_string_realloc_fn reallocate,
                                     const void *raw,
                                     size_t raw_len,
                                     uint8_t url_encoded);
SFUNC int fio_string_write_base64dec(fio_str_info_s *dest,
                                     fio_string_realloc_fn reallocate,
                                     const void *encoded,
                                     size_t encoded_len);
```

Base64 encode / decode. Pass `url_encoded = 1` to use URL-safe alphabet.

#### `fio_string_write_url_enc` / `fio_string_write_url_dec` / `fio_string_write_path_dec`

```c
SFUNC int fio_string_write_url_enc(fio_str_info_s *dest,
                                   fio_string_realloc_fn reallocate,
                                   const void *raw,
                                   size_t raw_len);
SFUNC int fio_string_write_url_dec(fio_str_info_s *dest,
                                   fio_string_realloc_fn reallocate,
                                   const void *encoded,
                                   size_t encoded_len);
SFUNC int fio_string_write_path_dec(fio_str_info_s *dest,
                                    fio_string_realloc_fn reallocate,
                                    const void *encoded,
                                    size_t encoded_len);
```

URL (percent) encoding and decoding. `url_dec` converts `+` to space; `path_dec` does not.

#### `fio_string_write_html_escape` / `fio_string_write_html_unescape`

```c
SFUNC int fio_string_write_html_escape(fio_str_info_s *dest,
                                       fio_string_realloc_fn reallocate,
                                       const void *raw,
                                       size_t raw_len);
SFUNC int fio_string_write_html_unescape(fio_str_info_s *dest,
                                         fio_string_realloc_fn reallocate,
                                         const void *enscaped,
                                         size_t enscaped_len);
```

HTML escape / unescape. The unescape implementation is minimal and incomplete.

---

## File Reading

#### `fio_string_readfd`

```c
SFUNC int fio_string_readfd(fio_str_info_s *dest,
                            fio_string_realloc_fn reallocate,
                            int fd,
                            intptr_t start_at,
                            size_t limit);
```

Reads up to `limit` bytes from seekable file descriptor `fd` starting at `start_at`. Negative `start_at` counts from EOF. `limit == 0` reads to EOF.

#### `fio_string_readfile`

```c
SFUNC int fio_string_readfile(fio_str_info_s *dest,
                              fio_string_realloc_fn reallocate,
                              const char *filename,
                              intptr_t start_at,
                              size_t limit);
```

Opens `filename` and reads its contents (or a slice) into `dest`.

#### `fio_string_getdelim_fd`

```c
SFUNC int fio_string_getdelim_fd(fio_str_info_s *dest,
                                 fio_string_realloc_fn reallocate,
                                 int fd,
                                 intptr_t start_at,
                                 char delim,
                                 size_t limit);
```

Reads from `fd` until `delim` or EOF.

#### `fio_string_getdelim_file`

```c
SFUNC int fio_string_getdelim_file(fio_str_info_s *dest,
                                   fio_string_realloc_fn reallocate,
                                   const char *filename,
                                   intptr_t start_at,
                                   char delim,
                                   size_t limit);
```

Same as `fio_string_getdelim_fd` but opens and closes the file.

---

## Memory Helpers

#### `fio_string_capa4len`

```c
FIO_IFUNC size_t fio_string_capa4len(size_t new_len);
```

Returns a 16-byte-aligned capacity for `new_len`.

#### Default Allocator Callbacks

```c
#define FIO_STRING_SYS_REALLOC   fio_string_sys_reallocate
#define FIO_STRING_REALLOC       fio_string_default_reallocate
#define FIO_STRING_ALLOC_COPY    fio_string_default_allocate_copy
#define FIO_STRING_ALLOC_KEY     fio_string_default_key_alloc
#define FIO_STRING_FREE          fio_string_default_free
#define FIO_STRING_FREE2         fio_string_default_free2
#define FIO_STRING_FREE_KEY      fio_string_default_free_key
#define FIO_STRING_FREE_NOOP     fio_string_default_free_noop
#define FIO_STRING_FREE_NOOP2    fio_string_default_free_noop2
```

Convenience macros pointing to the default callbacks.

#### `fio_string_default_reallocate`

```c
SFUNC int fio_string_default_reallocate(fio_str_info_s *dst, size_t len);
```

Grows `dst` using the current facil.io allocator (`FIO_MEM_REALLOC_`).

#### `fio_string_default_allocate_copy`

```c
SFUNC int fio_string_default_allocate_copy(fio_str_info_s *dest, size_t new_capa);
```

Allocates a new buffer and copies existing data into it.

#### `fio_string_default_free`

```c
SFUNC void fio_string_default_free(void *);
```

Frees a buffer allocated with the default callbacks.

---

## UTF-8 Helpers

#### `fio_string_utf8_valid`

```c
SFUNC bool fio_string_utf8_valid(fio_str_info_s str);
```

Returns `true` if `str` is valid UTF-8.

#### `fio_string_utf8_len`

```c
SFUNC size_t fio_string_utf8_len(fio_str_info_s str);
```

Returns the number of Unicode code points, or `0` if invalid.

#### `fio_string_utf8_valid_code_point`

```c
SFUNC size_t fio_string_utf8_valid_code_point(const void *u8c, size_t buf_len);
```

Returns the byte length of the UTF-8 code point at `u8c` (`1`–`4`), or `0` if invalid.

#### `fio_string_utf8_select`

```c
SFUNC int fio_string_utf8_select(fio_str_info_s str,
                                 intptr_t *pos,
                                 size_t *len);
```

Converts a UTF-8 character range (`pos`, `len`) into raw byte offsets. Returns `-1` on error and sets `pos` to `-1` if the string is invalid before the selection.

---

## Comparison Helpers

#### `fio_string_is_greater_buf`

```c
SFUNC int fio_string_is_greater_buf(fio_buf_info_s a, fio_buf_info_s b);
```

Returns `1` if `a` is lexicographically greater than `b`; `0` otherwise (including equality).

#### `fio_string_is_greater`

```c
FIO_IFUNC int fio_string_is_greater(fio_str_info_s a, fio_str_info_s b);
```

Same comparison for `fio_str_info_s`.

---

## Binary String (`fio_bstr`)

A reference-counted, copy-on-write, NUL-terminated binary string. The public pointer points at the first character; metadata lives just before it.

#### `fio_bstr_reserve`

```c
FIO_IFUNC char *fio_bstr_reserve(char *bstr, size_t len);
```

Ensures at least `len` additional bytes are available.

#### `fio_bstr_copy`

```c
FIO_IFUNC char *fio_bstr_copy(char *bstr);
```

Returns a shallow copy, incrementing the reference count. Falls back to a real copy if the reference count overflows.

#### `fio_bstr_free`

```c
FIO_IFUNC void fio_bstr_free(char *bstr);
```

Decrements the reference count and frees memory when it reaches zero.

#### `fio_bstr_info` / `fio_bstr_buf` / `fio_bstr_len` / `fio_bstr_len_set`

```c
FIO_IFUNC fio_str_info_s fio_bstr_info(const char *bstr);
FIO_IFUNC fio_buf_info_s fio_bstr_buf(const char *bstr);
FIO_IFUNC size_t fio_bstr_len(const char *bstr);
FIO_IFUNC char *fio_bstr_len_set(char *bstr, size_t len);
```

Inspect or resize the string. `fio_bstr_len_set` may reallocate if the string is shared or too small.

#### `fio_bstr_write` and Friends

```c
FIO_IFUNC char *fio_bstr_write(char *bstr, const void *src, size_t len);
FIO_IFUNC char *fio_bstr_replace(char *bstr, intptr_t start_pos,
                                 size_t overwrite_len, const void *src,
                                 size_t len);
FIO_IFUNC char *fio_bstr_write2(char *bstr, const fio_string_write_s srcs[]);
#define fio_bstr_write2(bstr, ...) \
  fio_bstr_write2(bstr, (fio_string_write_s[]){__VA_ARGS__, {0}})
FIO_IFUNC char *fio_bstr_write_i(char *bstr, int64_t num);
FIO_IFUNC char *fio_bstr_write_u(char *bstr, uint64_t num);
FIO_IFUNC char *fio_bstr_write_hex(char *bstr, uint64_t num);
FIO_IFUNC char *fio_bstr_write_bin(char *bstr, uint64_t num);
FIO_IFUNC char *fio_bstr_write_escape(char *bstr, const void *src, size_t len);
FIO_IFUNC char *fio_bstr_write_unescape(char *bstr, const void *src, size_t len);
FIO_IFUNC char *fio_bstr_write_base64enc(char *bstr, const void *src,
                                         size_t len, uint8_t url_encoded);
FIO_IFUNC char *fio_bstr_write_base64dec(char *bstr, const void *src, size_t len);
FIO_IFUNC char *fio_bstr_write_url_enc(char *bstr, const void *data, size_t len);
FIO_IFUNC char *fio_bstr_write_url_dec(char *bstr, const void *encoded, size_t len);
FIO_IFUNC char *fio_bstr_write_html_escape(char *bstr, const void *raw, size_t len);
FIO_IFUNC char *fio_bstr_write_html_unescape(char *bstr, const void *escaped,
                                             size_t len);
FIO_IFUNC char *fio_bstr_readfd(char *bstr, int fd, intptr_t start_at,
                                intptr_t limit);
FIO_IFUNC char *fio_bstr_readfile(char *bstr, const char *filename,
                                  intptr_t start_at, intptr_t limit);
FIO_IFUNC char *fio_bstr_getdelim_file(char *bstr, const char *filename,
                                       intptr_t start_at, char delim,
                                       size_t limit);
FIO_IFUNC char *fio_bstr_getdelim_fd(char *bstr, int fd, intptr_t start_at,
                                     char delim, size_t limit);
FIO_IFUNC char *fio_bstr_printf(char *bstr, const char *format, ...);
```

Every `fio_bstr_*` function returns the possibly-reallocated string pointer. Always assign the result back.

#### `fio_bstr_is_greater` / `fio_bstr_is_eq` / `fio_bstr_is_eq2info` / `fio_bstr_is_eq2buf`

```c
FIO_SFUNC int fio_bstr_is_greater(const char *a, const char *b);
FIO_SFUNC int fio_bstr_is_eq(const char *a_, const char *b_);
FIO_SFUNC int fio_bstr_is_eq2info(const char *a_, fio_str_info_s b);
FIO_SFUNC int fio_bstr_is_eq2buf(const char *a_, fio_buf_info_s b);
```

Comparison helpers.

#### `fio_bstr_reallocate`

```c
SFUNC int fio_bstr_reallocate(fio_str_info_s *dest, size_t len);
```

Default reallocate callback for `fio_bstr`.

---

## Key String (`fio_keystr_s`)

A compact key type used by hash maps. Small strings are embedded in the struct; larger ones store a pointer and length. Not NUL-terminated.

#### `fio_keystr_tmp`

```c
FIO_IFUNC fio_keystr_s fio_keystr_tmp(const char *buf, uint32_t len);
```

Returns a temporary key referencing `buf`/`len`.

#### `fio_keystr_init`

```c
FIO_SFUNC fio_keystr_s fio_keystr_init(fio_str_info_s str,
                                       void *(*alloc_func)(size_t len));
```

Copies `str` into a persistent key. Strings short enough are embedded; otherwise `alloc_func` is used. Pass `capa == FIO_KEYSTR_CONST` to store a borrowed pointer.

#### `fio_keystr_destroy`

```c
FIO_SFUNC void fio_keystr_destroy(fio_keystr_s *key,
                                  void (*free_func)(void *, size_t));
```

Releases a key allocated by `fio_keystr_init`.

#### `fio_keystr_buf` / `fio_keystr_info` / `fio_keystr_is_eq` / `fio_keystr_is_eq2` / `fio_keystr_is_eq3` / `fio_keystr_hash`

```c
FIO_IFUNC fio_buf_info_s fio_keystr_buf(fio_keystr_s *str);
FIO_IFUNC fio_str_info_s fio_keystr_info(fio_keystr_s *str);
FIO_IFUNC int fio_keystr_is_eq(fio_keystr_s a, fio_keystr_s b);
FIO_IFUNC int fio_keystr_is_eq2(fio_keystr_s a_, fio_str_info_s b);
FIO_IFUNC int fio_keystr_is_eq3(fio_keystr_s a_, fio_buf_info_s b);
FIO_IFUNC uint64_t fio_keystr_hash(fio_keystr_s a);
```

Inspect, compare, and hash key strings.

#### `FIO_KEYSTR_CONST`

```c
#define FIO_KEYSTR_CONST ((size_t)-1LL)
```

Magic `capa` value for `fio_keystr_init` that keeps a borrowed pointer instead of allocating.

---

## Example

```c
#define FIO_STR
#include "fio-stl.h"

int my_realloc(fio_str_info_s *dest, size_t len) {
  size_t capa = fio_string_capa4len(len);
  char *tmp = realloc(dest->buf, capa);
  if (!tmp) return -1;
  dest->buf = tmp;
  dest->capa = capa;
  return 0;
}

int main(void) {
  char buf[32];
  fio_str_info_s str = FIO_STR_INFO3(buf, 0, 32);

  fio_string_write(&str, my_realloc, "answer: ", 8);
  fio_string_write_i(&str, my_realloc, 42);
  fprintf(stderr, "%s\n", str.buf);

  char *b = NULL;
  b = fio_bstr_write(b, "hello ", 6);
  b = fio_bstr_write(b, "world", 5);
  fprintf(stderr, "%s (len=%zu)\n", b, fio_bstr_len(b));
  fio_bstr_free(b);
  return 0;
}
```

------------------------------------------------------------
# GFM Markdown Parser

```c
#define FIO_GFM
#include "fio-stl.h"
```

A flat-state, non-recursive parser for GitHub Flavored Markdown. It emits block and inline events through three callbacks — push, write, pop — without allocating heap memory itself. `fio_gfm_parse` is the only public entry point.

The parser depends on `FIO_ENTITY` for HTML entity decoding.

---

## Configuration Macros

#### `FIO_GFM_MAX_DEPTH`

```c
#define FIO_GFM_MAX_DEPTH 255
```

Maximum container nesting depth. Must fit in a byte (≤ 255).

#### `FIO_GFM_MAX_TABLE_COLUMNS`

```c
#define FIO_GFM_MAX_TABLE_COLUMNS 64
```

Maximum table columns. Alignment is bit-packed, so four columns fit in one byte.

#### `FIO_GFM_REF_CACHE_SIZE`

```c
#define FIO_GFM_REF_CACHE_SIZE 128
```

Maximum reference definitions held in the fast inline cache.

---

## Error Codes

#### `FIO_GFM_ERR_GENERIC`, `FIO_GFM_ERR_DEPTH`, `FIO_GFM_ERR_INPUT`

```c
#define FIO_GFM_ERR_GENERIC -1
#define FIO_GFM_ERR_DEPTH   -2
#define FIO_GFM_ERR_INPUT   -3
```

Negative error codes used internally and returned through `fio_gfm_parse`.

---

## Flags

#### `FIO_GFM_F_TIGHT`

```c
#define FIO_GFM_F_TIGHT ((uint8_t)1U << 0)
```

The list is tight (no blank lines between items).

#### `FIO_GFM_F_LOOSE_SEEN`

```c
#define FIO_GFM_F_LOOSE_SEEN ((uint8_t)1U << 1)
```

A blank line was seen between list items.

#### `FIO_GFM_F_TASK`

```c
#define FIO_GFM_F_TASK ((uint8_t)1U << 2)
```

The list item is a task-list item.

#### `FIO_GFM_F_TASK_CHECKED`

```c
#define FIO_GFM_F_TASK_CHECKED ((uint8_t)1U << 3)
```

The task-list marker is checked (`[x]`).

---

## Types

#### `fio_gfm_type_e`

```c
typedef enum {
  FIO_GFM_PARAGRAPH = 1,
  FIO_GFM_HEADING,
  FIO_GFM_THEMATIC_BREAK,
  FIO_GFM_BLOCKQUOTE,
  FIO_GFM_LIST_UNORDERED,
  FIO_GFM_LIST_ORDERED,
  FIO_GFM_LIST_ITEM,
  FIO_GFM_CODE_BLOCK,
  FIO_GFM_HTML_BLOCK,
  FIO_GFM_TABLE,
  FIO_GFM_TABLE_ROW,
  FIO_GFM_TABLE_CELL,
  FIO_GFM_EMPHASIS,
  FIO_GFM_STRONG,
  FIO_GFM_STRIKETHROUGH,
  FIO_GFM_LINK,
  FIO_GFM_TEXT,
  FIO_GFM_SOFT_BREAK,
  FIO_GFM_HARD_BREAK,
  FIO_GFM_CODE_SPAN,
  FIO_GFM_IMAGE,
  FIO_GFM_AUTOLINK,
  FIO_GFM_INLINE_HTML,
  FIO_GFM_FOOTNOTE_REF,
} fio_gfm_type_e;
```

Event type. Values `1`–`12` are block sections; `13`–`16` are inline sections; `17`–`23` are text and leaf content emitted with `write`.

#### `fio_gfm_align_e`

```c
typedef enum {
  FIO_GFM_ALIGN_NONE = 0,
  FIO_GFM_ALIGN_LEFT,
  FIO_GFM_ALIGN_RIGHT,
  FIO_GFM_ALIGN_CENTER,
} fio_gfm_align_e;
```

Table cell alignment.

#### `fio_gfm_event_s`

```c
typedef struct {
  void *udata;
  fio_buf_info_s source;
  fio_buf_info_s text;
  fio_buf_info_s marker;
  fio_buf_info_s info;
  fio_buf_info_s destination;
  fio_buf_info_s title;
  fio_buf_info_s reference;
  uint32_t list_start;
  uint16_t columns;
  uint16_t column;
  uint8_t type;
  uint8_t heading_level;
  uint8_t flags;
  uint8_t align;
  uint8_t padding;
} fio_gfm_event_s;
```

Event passed to every callback.

**Fields:**
- `udata` — live user pointer; the parser copies it back after each callback.
- `source` — full source slice for this event.
- `text` — literal text / label / code content.
- `marker` — marker slice (`#`, fence, list bullet, etc.).
- `info` — fenced code info string (language tag).
- `destination` — link/image/autolink URL.
- `title` — link/image title.
- `reference` — reference label for ref-style links/images.
- `list_start` — ordered list start number.
- `columns` / `column` — table column count and cell index.
- `type` — `fio_gfm_type_e` value.
- `heading_level` — `1`–`6` for headings.
- `flags` — `FIO_GFM_F_*`.
- `align` — `fio_gfm_align_e` for table cells.
- `padding` — virtual leading spaces from tab expansion.

#### `fio_gfm_callbacks_s`

```c
typedef struct {
  int (*push)(fio_gfm_event_s *e);
  int (*write)(fio_gfm_event_s *e);
  int (*pop)(fio_gfm_event_s *e);
} fio_gfm_callbacks_s;
```

Parser callbacks. All are optional. Return `0` to continue, non-zero to abort. Positive values are user errors; negative values are reserved for parser errors.

---

## Parsing

#### `fio_gfm_parse`

```c
SFUNC size_t fio_gfm_parse(const fio_gfm_callbacks_s *callbacks,
                           void *udata,
                           fio_buf_info_s source);
```

Parses a complete Markdown document and emits events. The document must be fully available in `source`; streaming is not supported.

**Parameters:**
- `callbacks` — pointer to callback struct.
- `udata` — user pointer copied into every event; callbacks may update it.
- `source` — full document bytes.

**Returns:** the number of bytes consumed. If this equals `source.len`, parsing completed. A smaller value means a callback aborted or a parser error occurred.

---

## Example

```c
#define FIO_GFM
#include "fio-stl.h"

static int on_push(fio_gfm_event_s *e) {
  fprintf(stderr, "open type=%u\n", e->type);
  return 0;
}

static int on_write(fio_gfm_event_s *e) {
  fprintf(stderr, "text: %.*s\n", (int)e->text.len, e->text.buf);
  return 0;
}

static int on_pop(fio_gfm_event_s *e) {
  fprintf(stderr, "close type=%u\n", e->type);
  return 0;
}

int main(void) {
  fio_gfm_callbacks_s cb = {
      .push = on_push,
      .write = on_write,
      .pop = on_pop,
  };
  fio_buf_info_s src = FIO_BUF_INFO2("# Hello\n\nworld.\n", 16);
  size_t n = fio_gfm_parse(&cb, NULL, src);
  fprintf(stderr, "consumed %zu of %zu bytes\n", n, src.len);
  return 0;
}
```

------------------------------------------------------------
# POSIX Portable Polling API

```c
#define FIO_POLL
#include "fio-stl.h"
```

A small abstraction over `poll`, `epoll`, and `kqueue`. Define `FIO_POLL_ENGINE_POLL`, `FIO_POLL_ENGINE_EPOLL`, or `FIO_POLL_ENGINE_KQUEUE` before inclusion to force a backend; otherwise the best available engine is selected automatically.

The API is the same for every backend. Backend-specific details live in:

- [`102 poll poll.md`](./102%20poll%20poll.md)
- [`102 poll epoll.md`](./102%20poll%20epoll.md)
- [`102 poll kqueue.md`](./102%20poll%20kqueue.md)

---

## Configuration Macros

#### `FIO_POLL_POSSIBLE_FLAGS`

```c
#define FIO_POLL_POSSIBLE_FLAGS (POLLIN | POLLOUT | POLLPRI)
```

Mask of event flags the polling layer recognizes.

#### `FIO_POLL_MAX_EVENTS`

```c
#define FIO_POLL_MAX_EVENTS 128
```

Maximum events returned in one `fio_poll_review` call. Relevant for epoll and kqueue. Larger on 32-bit platforms.

---

## Types

#### `fio_poll_s`

```c
typedef struct fio_poll_s fio_poll_s;
```

Opaque polling object. The actual layout depends on the selected backend.

#### `fio_poll_settings_s`

```c
typedef struct {
  void (*on_data)(void *udata);
  void (*on_ready)(void *udata);
  void (*on_close)(void *udata);
} fio_poll_settings_s;
```

Callback settings. Each callback receives the `udata` pointer supplied when the file descriptor was monitored.

- `on_data` — data is available to read.
- `on_ready` — the socket is ready to write.
- `on_close` — connection closed or an error occurred.

---

## Functions

#### `fio_poll_engine`

```c
FIO_IFUNC const char *fio_poll_engine(void);
```

Returns the name of the active backend (`"epoll"`, `"kqueue"`, or `"poll"`) as a constant string.

#### `fio_poll_init`

```c
FIO_IFUNC void fio_poll_init(fio_poll_s *p, fio_poll_settings_s settings);

#define fio_poll_init(p, ...) \
  fio_poll_init((p), (fio_poll_settings_s){__VA_ARGS__})
```

Initializes the polling object. The macro form accepts designated initializers.

**Parameters:**
- `p` — pointer to the polling object.
- `settings` — callback configuration.

```c
fio_poll_init(&poll,
              .on_data = on_data,
              .on_ready = on_ready,
              .on_close = on_close);
```

#### `fio_poll_destroy`

```c
FIO_IFUNC void fio_poll_destroy(fio_poll_s *p);
```

Frees backend resources held by the polling object.

#### `fio_poll_monitor`

```c
SFUNC int fio_poll_monitor(fio_poll_s *p,
                           fio_socket_i fd,
                           void *udata,
                           unsigned short flags);
```

Adds `fd` to the poll set, updates its `udata`, or adds events. Recognized flags are `POLLIN`, `POLLOUT`, and `POLLPRI`; other flags may be ignored. Monitoring is one-shot: once an event fires, it is removed until re-armed.

**Returns:** `0` on success, `-1` on error.

#### `fio_poll_review`

```c
SFUNC int fio_poll_review(fio_poll_s *p, size_t timeout);
```

Waits up to `timeout` milliseconds for events and dispatches callbacks. Adding a file descriptor from another thread while `fio_poll_review` is running will not include that descriptor until the next call.

**Returns:** the number of events dispatched, or `-1` on error.

#### `fio_poll_forget`

```c
SFUNC int fio_poll_forget(fio_poll_s *p, fio_socket_i fd);
```

Removes `fd` from the poll set.

**Returns:** `0` on success, `-1` on error.

---

## Example

```c
#define FIO_POLL
#include "fio-stl.h"

static void on_data(void *udata) { fprintf(stderr, "readable\n"); }
static void on_ready(void *udata) { fprintf(stderr, "writable\n"); }
static void on_close(void *udata) { fprintf(stderr, "closed\n"); }

int main(void) {
  fio_poll_s p;
  fio_poll_init(&p,
                .on_data = on_data,
                .on_ready = on_ready,
                .on_close = on_close);

  fio_poll_monitor(&p, some_fd, NULL, POLLIN);
  for (int i = 0; i < 10; ++i)
    fio_poll_review(&p, 1000);

  fio_poll_destroy(&p);
  return 0;
}
```

------------------------------------------------------------
# POSIX Polling — epoll Backend

```c
#define FIO_POLL_ENGINE_EPOLL
#define FIO_POLL
#include "fio-stl.h"
```

Implementation of the portable polling API on top of Linux `epoll`. Normally selected automatically on Linux; define `FIO_POLL_ENGINE_EPOLL` to force it.

The public API is documented in [`102 poll api.md`](./102%20poll%20api.md). This file describes backend-specific behavior.

---

## Backend Details

#### `struct fio_poll_s`

```c
struct fio_poll_s {
  fio_poll_settings_s settings;
  struct pollfd fds[2];
  int fd[2];
};
```

Two internal `epoll` file descriptors are used: one for `POLLOUT` readiness and one for `POLLIN`/error events. A `pollfd` pair wakes the internal `poll` used to wait on both epoll FDs at once.

#### `fio_poll_engine`

Returns `"epoll"`.

#### Fork Safety

A state callback re-creates the epoll FDs in the child process after `fork`.

---

## Notes

- Events are monitored in one-shot mode (`EPOLLONESHOT`). After an event fires, the descriptor must be re-armed with `fio_poll_monitor`.
- Error and hang-up events are dispatched through the read-side epoll FD and delivered to `on_close`.
- `EPOLLRDHUP` and `EPOLLHUP` are always included in the monitored events.

------------------------------------------------------------
# POSIX Polling — kqueue Backend

```c
#define FIO_POLL_ENGINE_KQUEUE
#define FIO_POLL
#include "fio-stl.h"
```

Implementation of the portable polling API on top of BSD/macOS `kqueue`. Normally selected automatically when `<sys/event.h>` is available; define `FIO_POLL_ENGINE_KQUEUE` to force it.

The public API is documented in [`102 poll api.md`](./102%20poll%20api.md). This file describes backend-specific behavior.

---

## Backend Details

#### `struct fio_poll_s`

```c
struct fio_poll_s {
  fio_poll_settings_s settings;
  int fd;
};
```

A single kqueue descriptor is used for both read and write filters.

#### `fio_poll_engine`

Returns `"kqueue"`.

#### Fork Safety

A state callback re-creates the kqueue descriptor in the child process after `fork`.

---

## Notes

- Read and write filters are registered with `EV_ADD | EV_ENABLE | EV_CLEAR | EV_ONESHOT`.
- `EV_EOF` and `EV_ERROR` flags are routed to `on_close`.
- `fio_poll_review` uses a `timespec` built from the millisecond timeout.

------------------------------------------------------------
# POSIX Polling — poll Backend

```c
#define FIO_POLL_ENGINE_POLL
#define FIO_POLL
#include "fio-stl.h"
```

Implementation of the portable polling API on top of POSIX `poll()` / Windows `WSAPoll`. This is the fallback backend and is selected automatically when neither epoll nor kqueue is available.

The public API is documented in [`102 poll api.md`](./102%20poll%20api.md). This file describes backend-specific behavior.

---

## Backend Details

#### `struct fio_poll_s`

```c
struct fio_poll_s {
  fio_poll_settings_s settings;
  fio___poll_map_s map;
  FIO___LOCK_TYPE lock;
};
```

The poll backend keeps monitored descriptors in an internal imap (`fio___poll_map_s`) and takes a snapshot before each `poll()` call. Descriptors added or re-armed during `fio_poll_review` are merged with surviving one-shot flags when the call returns.

Do not rely on `fio_poll_forget` from another thread to cancel a descriptor that is already inside the in-flight snapshot: if the descriptor does not fire, the surviving snapshot entry can be merged back.

#### `fio_poll_engine`

Returns `"poll"`.

#### `fio_poll_close_all`

```c
SFUNC void fio_poll_close_all(fio_poll_s *p);
```

Additional helper available only in the poll backend. Closes every monitored socket and calls `on_close` for each one. Useful during shutdown.

---

## Notes

- `POLLRDHUP` is used when available; otherwise the backend relies on `POLLHUP`, `POLLERR`, and `POLLNVAL` for close/error detection.
- On Windows, `POLLPRI` is omitted because `WSAPoll` rejects it.
- Fired events are stripped from the descriptor’s flags (one-shot semantics). Surviving flags are merged back into the poll set after `poll()` returns; additions / re-arms merge cleanly, while concurrent removals of in-flight snapshot entries are not a cancellation guarantee.

------------------------------------------------------------
# Task and Timer Queues

```c
#define FIO_QUEUE
#include "fio-stl.h"
```

A thread-safe task queue built from linked ring buffers, plus a timer queue that moves due events into a task queue. Worker threads can be attached to drain the queue in the background.

---

## Configuration

#### `FIO_QUEUE_TASKS_PER_ALLOC`

```c
#define FIO_QUEUE_TASKS_PER_ALLOC 168 /* 338 on 32-bit */
```

Tasks per ring-buffer chunk. Default is chosen so `fio_queue_s` fits in one page. Must not exceed 65535.

---

## Task Queue Types

#### `fio_queue_task_s`

```c
typedef struct {
  void (*fn)(void *, void *);
  void *udata1;
  void *udata2;
} fio_queue_task_s;
```

A single task. `fn` receives both `udata` pointers. Tasks with `fn == NULL` are ignored.

#### `fio_queue_s`

```c
typedef struct {
  fio___task_ring_s *r;
  fio___task_ring_s *w;
  uint32_t count;
  FIO___LOCK_TYPE lock;
  FIO_LIST_NODE consumers;
  fio___task_ring_s mem;
} fio_queue_s;
```

Queue object. Treat as opaque. Allocate on the stack or with `fio_queue_new`.

---

## Task Queue API

#### `FIO_QUEUE_STATIC_INIT`

```c
#define FIO_QUEUE_STATIC_INIT(queue) /* ... */
```

Static initializer for a global queue. Prefer `fio_queue_init` at runtime when possible — it initializes only the bytes that matter.

#### `fio_queue_init`

```c
FIO_IFUNC void fio_queue_init(fio_queue_s *q);
```

Initializes `q` in place.

#### `fio_queue_destroy`

```c
SFUNC void fio_queue_destroy(fio_queue_s *q);
```

Frees ring buffers, stops and joins workers, and re-initializes `q`. After destruction the queue may be reused, but on some platforms the lock may need explicit re-initialization.

#### `fio_queue_new`

```c
SFUNC fio_queue_s *fio_queue_new(void);
```

Allocates and initializes a queue on the heap.

#### `fio_queue_free`

```c
SFUNC void fio_queue_free(fio_queue_s *q);
```

Destroys `q` and frees the queue object itself.

#### `fio_queue_push`

```c
SFUNC int fio_queue_push(fio_queue_s *q, fio_queue_task_s task);
#define fio_queue_push(q, ...) \
  fio_queue_push((q), (fio_queue_task_s){__VA_ARGS__})
```

Pushes a task to the tail of the queue. The macro accepts named arguments.

**Returns:** `0` on success, `-1` on memory error.

```c
fio_queue_push(&q, .fn = my_task, .udata1 = arg);
```

#### `fio_queue_push_urgent`

```c
SFUNC int fio_queue_push_urgent(fio_queue_s *q, fio_queue_task_s task);
#define fio_queue_push_urgent(q, ...) \
  fio_queue_push_urgent((q), (fio_queue_task_s){__VA_ARGS__})
```

Pushes a task to the head of the queue (LIFO).

**Returns:** `0` on success, `-1` on memory error.

#### `fio_queue_pop`

```c
SFUNC fio_queue_task_s fio_queue_pop(fio_queue_s *q);
```

Removes and returns the next task (FIFO). The returned task has `fn == NULL` if the queue was empty.

#### `fio_queue_perform`

```c
SFUNC int fio_queue_perform(fio_queue_s *q);
```

Pops and performs one task. Returns `-1` if the queue was empty.

#### `fio_queue_perform_all`

```c
SFUNC void fio_queue_perform_all(fio_queue_s *q);
```

Performs every task currently in the queue.

#### `fio_queue_count`

```c
FIO_IFUNC uint32_t fio_queue_count(fio_queue_s *q);
```

Returns the number of pending tasks.

---

## Worker Threads

#### `fio_queue_workers_add`

```c
SFUNC int fio_queue_workers_add(fio_queue_s *q, size_t count);
```

Spawns `count` consumer threads that automatically perform tasks as they arrive. Threads sleep on a condition variable when idle.

**Returns:** `0` on success, `-1` on thread creation failure.

#### `fio_queue_workers_stop`

```c
SFUNC void fio_queue_workers_stop(fio_queue_s *q);
```

Signals all workers to stop. Returns immediately without waiting.

#### `fio_queue_workers_join`

```c
SFUNC void fio_queue_workers_join(fio_queue_s *q);
```

Signals workers to stop and blocks until they terminate.

#### `fio_queue_workers_wake`

```c
SFUNC void fio_queue_workers_wake(fio_queue_s *q);
```

Wakes all workers to check for new tasks. Called automatically on push.

---

## Timer Queue Types

#### `fio_timer_queue_s`

```c
typedef struct {
  fio___timer_event_s *next;
  FIO___LOCK_TYPE lock;
} fio_timer_queue_s;
```

Opaque timer queue.

#### `FIO_TIMER_QUEUE_INIT`

```c
#define FIO_TIMER_QUEUE_INIT /* ... */
```

Static initializer for a timer queue.

#### `fio_timer_schedule_args_s`

```c
typedef struct {
  int (*fn)(void *, void *);
  void *udata1;
  void *udata2;
  void (*on_finish)(void *, void *);
  uint32_t every;
  int32_t repetitions;
  int64_t start_at;
} fio_timer_schedule_args_s;
```

Timer schedule arguments.

**Members:**
- `fn` — callback. Return non-zero to stop the timer.
- `udata1`, `udata2` — opaque data passed to `fn` and `on_finish`.
- `on_finish` — called when the timer stops.
- `every` — interval in milliseconds.
- `repetitions` — repeat count; `-1` means forever.
- `start_at` — base time in milliseconds; `0` uses `fio_time_milli()`.

---

## Timer Queue API

#### `fio_timer_schedule`

```c
SFUNC void fio_timer_schedule(fio_timer_queue_s *timer_queue,
                              fio_timer_schedule_args_s args);
#define fio_timer_schedule(timer_queue, ...) \
  fio_timer_schedule((timer_queue), (fio_timer_schedule_args_s){__VA_ARGS__})
```

Adds a timed event to the timer queue. The macro accepts named arguments.

```c
fio_timer_schedule(&timers,
                   .fn = tick,
                   .udata1 = ctx,
                   .every = 1000,
                   .repetitions = -1);
```

#### `fio_timer_push2queue`

```c
SFUNC size_t fio_timer_push2queue(fio_queue_s *queue,
                                  fio_timer_queue_s *timer_queue,
                                  int64_t now_in_milliseconds);
```

Moves all due timer events into `queue`. Pass `0` for `now_in_milliseconds` to use `fio_time_milli()`.

**Returns:** number of timers pushed.

#### `fio_timer_next_at`

```c
FIO_IFUNC int64_t fio_timer_next_at(fio_timer_queue_s *timer_queue);
```

Returns the due time of the next event, or `INT64_MAX` if the queue is empty.

#### `fio_timer_destroy`

```c
SFUNC void fio_timer_destroy(fio_timer_queue_s *timer_queue);
```

Cancels all pending timers. Do not free the timer queue while timer tasks may still be queued in a `fio_queue_s`, because repeating timers reschedule themselves.

---

## Example

```c
#define FIO_QUEUE
#include "fio-stl.h"

void work(void *a, void *b) {
  (void)a; (void)b;
  fprintf(stderr, "working\n");
}

int tick(void *a, void *b) {
  (void)a; (void)b;
  fprintf(stderr, "tick\n");
  return 0;
}

int main(void) {
  fio_queue_s q = {0};
  fio_queue_init(&q);

  fio_queue_push(&q, .fn = work);
  fio_queue_perform(&q);

  fio_timer_queue_s t = FIO_TIMER_QUEUE_INIT;
  fio_timer_schedule(&t, .fn = tick, .every = 100, .repetitions = 3);
  fio_timer_push2queue(&q, &t, 0);
  fio_queue_perform_all(&q);

  fio_timer_destroy(&t);
  fio_queue_destroy(&q);
  return 0;
}
```

------------------------------------------------------------
# Packet Data Stream

```c
#define FIO_STREAM
#include "fio-stl.h"
```

A packet-based byte stream for buffering data from memory buffers and file descriptors. It is useful when writes are partial: add packets to the stream, read the next contiguous chunk into or from a scratch buffer, write what you can, then advance by the number of bytes written.

For the low-level helpers commonly used with streams, see [`004 files.md`](./004%20files.md), [`004 sock.md`](./004%20sock.md), and [`102 poll api.md`](./102%20poll%20api.md).

---

## Configuration Macros

#### `FIO_STREAM_COPY_PER_PACKET`

```c
#define FIO_STREAM_COPY_PER_PACKET 98304
```

Maximum copied payload per packet. Larger copied buffers are split into multiple packets so memory can be released progressively. Override before including the implementation.

#### `FIO_STREAM_ALWAYS_COPY_IF_LESS_THAN`

```c
#define FIO_STREAM_ALWAYS_COPY_IF_LESS_THAN 116 /* 8 in DEBUG */
```

Small buffers are copied even when `copy_buffer == 0`, improving locality and avoiding tiny external references.

---

## Types

#### `fio_stream_s`

```c
typedef struct {
  fio_stream_packet_s *next;
  fio_stream_packet_s **pos;
  size_t consumed;
  size_t length;
} fio_stream_s;
```

Stream object. Treat the fields as private. Allocate on the stack with `FIO_STREAM_INIT` or on the heap with `fio_stream_new`.

#### `fio_stream_packet_s`

```c
typedef struct fio_stream_packet_s fio_stream_packet_s;
```

Opaque packed data node. Packets are prepared separately, then transferred to a stream with `fio_stream_add`.

---

## Lifecycle

#### `FIO_STREAM_INIT`

```c
#define FIO_STREAM_INIT(s) { .next = NULL, .pos = &(s).next }
```

Initializer for an in-place stream.

```c
fio_stream_s stream = FIO_STREAM_INIT(stream);
```

#### `fio_stream_new`

```c
fio_stream_s *fio_stream_new(void);
```

Allocates and initializes a heap stream.

**Returns:** stream pointer, or `NULL` on allocation failure.

Not declared when `FIO_REF_CONSTRUCTOR_ONLY` is defined.

#### `fio_stream_free`

```c
int fio_stream_free(fio_stream_s *stream);
```

Destroys `stream`, frees the stream object itself, and returns `0`.

Not declared when `FIO_REF_CONSTRUCTOR_ONLY` is defined.

#### `fio_stream_destroy`

```c
void fio_stream_destroy(fio_stream_s *stream);
```

Frees all queued packets and re-initializes the stream object. Safe to call with `NULL`.

---

## Packing Data

#### `fio_stream_pack_data`

```c
fio_stream_packet_s *fio_stream_pack_data(void *buf,
                                          size_t len,
                                          size_t offset,
                                          uint8_t copy_buffer,
                                          void (*dealloc_func)(void *));
```

Packs `len` bytes starting at `((char *)buf + offset)`.

- If `copy_buffer` is non-zero, or `len < FIO_STREAM_ALWAYS_COPY_IF_LESS_THAN`, bytes are copied into stream-owned packets.
- Otherwise the packet references `buf` and calls `dealloc_func(buf)` when the packet is freed, if `dealloc_func` is not `NULL`.
- Copied data larger than `FIO_STREAM_COPY_PER_PACKET` is split into multiple packets.

**Returns:** packet pointer, or `NULL` if `buf == NULL`, `len == 0`, the length is too large for the packet format, or allocation fails. If `dealloc_func` is provided, it is called on both success-after-copy and failure.

#### `fio_stream_pack_fd`

```c
fio_stream_packet_s *fio_stream_pack_fd(int fd,
                                        size_t len,
                                        size_t offset,
                                        uint8_t keep_open);
```

Packs bytes from an open file descriptor. Reads are performed later by `fio_stream_read` using `fio_fd_read`.

- `len == 0` auto-detects the remaining file size with `fio_fd_size(fd)`.
- `offset` is the starting file offset.
- If `keep_open == 0`, the descriptor is closed when the packet is freed, or on pack failure after ownership was accepted.
- If `keep_open != 0`, the caller remains responsible for closing `fd`.

**Returns:** packet pointer, or `NULL` on invalid input, size detection failure / oversized auto-detected size when `len == 0`, or allocation failure.

#### `fio_stream_add`

```c
void fio_stream_add(fio_stream_s *stream, fio_stream_packet_s *packet);
```

Appends `packet` to `stream` and transfers packet ownership to the stream. If `stream` or `packet` is `NULL`, the packet is freed.

This is not thread-safe.

#### `fio_stream_pack_free`

```c
void fio_stream_pack_free(fio_stream_packet_s *packet);
```

Frees a packet that was not added to a stream. Do not call this after `fio_stream_add` succeeds.

---

## Reading and Consuming

#### `fio_stream_read`

```c
void fio_stream_read(fio_stream_s *stream, char **buf, size_t *len);
```

Reads the next available bytes without consuming them.

Before the call, `*buf` must point to a scratch buffer with at least `*len` bytes. This buffer is required when data spans multiple packets or comes from a file descriptor.

After the call:

- On empty stream or error, `*buf == NULL` and `*len == 0`.
- Otherwise `*buf` either still points to the supplied scratch buffer or points directly into stream-owned memory.
- `*len` is updated to the number of readable bytes available at `*buf`.

Reset both `*buf` and `*len` before each read if reusing the same scratch buffer. This is not thread-safe.

#### `fio_stream_advance`

```c
void fio_stream_advance(fio_stream_s *stream, size_t len);
```

Consumes `len` bytes from the stream, freeing fully consumed packets. Usually pass the number of bytes actually written to the destination.

This is not thread-safe.

#### `fio_stream_any`

```c
uint8_t fio_stream_any(fio_stream_s *stream);
```

Returns non-zero when `stream` has pending packets. Returns `0` for `NULL` or empty streams. This is not truly thread-safe.

#### `fio_stream_length`

```c
size_t fio_stream_length(fio_stream_s *stream);
```

Returns the number of bytes waiting in the stream. Call with a valid stream object. This is not truly thread-safe.

---

## Ownership and Thread-Safety

Packing creates a packet owned by the caller. `fio_stream_add` transfers that ownership to the stream. `fio_stream_destroy`, `fio_stream_free`, and `fio_stream_advance` free packets as needed.

For referenced memory packets, the referenced buffer must remain valid until the packet is freed. Pass `copy_buffer != 0` when that lifetime is not guaranteed. For file descriptor packets, keep the descriptor readable until the packet is consumed or destroyed; `keep_open` controls who closes it.

Packing packets can be done before taking the stream lock, but mutating or consuming a single `fio_stream_s` (`add`, `read`, `advance`, and length checks) should be externally synchronized when shared across threads.

---

## Example

```c
#define FIO_STREAM
#include "fio-stl.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  fio_stream_s stream = FIO_STREAM_INIT(stream);
  char scratch[4096];

  char msg[] = "hello, stream\n";
  fio_stream_packet_s *p = fio_stream_pack_data(msg, strlen(msg), 0, 1, NULL);
  fio_stream_add(&stream, p);

  while (fio_stream_any(&stream)) {
    char *buf = scratch;
    size_t len = sizeof(scratch);

    fio_stream_read(&stream, &buf, &len);
    if (!len)
      break;

    size_t written = fwrite(buf, 1, len, stdout);
    fio_stream_advance(&stream, written);

    if (written != len)
      break;
  }

  fio_stream_destroy(&stream);
  return 0;
}
```

### File Descriptor Packet

```c
#define FIO_STREAM
#define FIO_FILES
#include "fio-stl.h"

fio_stream_s stream = FIO_STREAM_INIT(stream);
int fd = fio_filename_open("./asset.bin", O_RDONLY);
fio_stream_packet_s *p = fio_stream_pack_fd(fd, 0, 0, 0); /* stream closes fd */
fio_stream_add(&stream, p);

/* ... read / write packets ... */
fio_stream_destroy(&stream);
```

------------------------------------------------------------
# Markdown to HTML bstr Renderer

```c
#define FIO_MD2HTML
#include "fio-stl.h"
```

A complete-document Markdown / GFM renderer that turns parser events into escaped HTML stored in an owned `fio_bstr`. Markdown goes in, tags come out. Implemented in [`./103 md2html.h`](./103%20md2html.h).

Defining `FIO_MD2HTML` automatically includes:
- `FIO_STR` for `fio_bstr`
- `FIO_GFM` for Markdown / GFM parsing

### Error Codes

#### `FIO_MD2HTML_ERR_ALLOC`

```c
#ifndef FIO_MD2HTML_ERR_ALLOC
#define FIO_MD2HTML_ERR_ALLOC 1
#endif
```

Internal renderer error used when output allocation fails. Override before inclusion only if you need a different non-zero value.

### Rendering

#### `fio_md2html`

```c
SFUNC char *fio_md2html(char *bstr_target, fio_buf_info_s source);
```

Renders a complete Markdown / GFM document into a `fio_bstr`.

**Parameters:**
- `bstr_target` - existing `fio_bstr` to append to, or `NULL` to allocate a new one
- `source` - Markdown bytes to parse

**Returns:** the output `fio_bstr`, or `NULL` on parser / allocation failure.

**Ownership:** the returned buffer is owned by the caller and should be released with `fio_bstr_free`. If `bstr_target == NULL` and rendering fails, the newly allocated output is freed. If `bstr_target` was provided, ownership stays with the caller.

### Rendering Behavior

From the implementation:
- normal text, code, and attribute values are escaped
- raw Markdown HTML blocks / inline HTML are preserved, with GFM tagfilter applied
- blocked tag names include `iframe`, `noembed`, `noframes`, `plaintext`, `style`, `textarea`, `title`, and `xmp`; HTML blocks only filter `textarea` and `xmp`
- fenced code info strings use the first word as `class="language-..."`
- GFM tables render `<table><thead>...` and open `<tbody>` for body rows
- table cell alignment uses `align="left"`, `align="right"`, or `align="center"`
- GFM task list items render disabled checkbox inputs
- autolinks add `mailto:` for bare email addresses and `http://` for `www.` links
- image alt text is escaped and strips nested link / emphasis markup

### Thread-Safety

The renderer uses caller-provided input and a local renderer state. It is thread-safe as long as separate calls do not mutate the same `bstr_target` at the same time.

### Example

```c
#define FIO_MD2HTML
#include "fio-stl.h"
#include <stdio.h>

int main(void) {
  char md[] = "# Demo\n\nHello **world**.\n";
  char *html = fio_md2html(NULL, FIO_BUF_INFO1(md));
  if (!html)
    return 1;

  fwrite(html, 1, fio_bstr_len(html), stdout);
  fio_bstr_free(html);
  return 0;
}
```

---
# Mustache-ish Template Engine

```c
#define FIO_MUSTACHE
#include "fio-stl.h"
```

A Mustache-ish template parser and renderer with variables, sections, inverted sections, partials, delimiter changes, comments, and optional YAML front matter callbacks. Logic-less-ish, because C callbacks still get a vote. Implemented in [`./104 mustache.h`](./104%20mustache.h).

Defining `FIO_MUSTACHE` pulls in string / `fio_bstr` helpers as needed.

### Configuration Macros

#### `FIO_MUSTACHE_MAX_DEPTH`

```c
#ifndef FIO_MUSTACHE_MAX_DEPTH
#define FIO_MUSTACHE_MAX_DEPTH 128
#endif
```

Maximum parser / builder nesting depth for sections and partials.

#### `FIO_MUSTACHE_PRESERVE_PADDING`

```c
#ifndef FIO_MUSTACHE_PRESERVE_PADDING
#define FIO_MUSTACHE_PRESERVE_PADDING 0
#endif
```

When enabled, preserves padding for stand-alone variables and partial templates.

#### `FIO_MUSTACHE_LAMBDA_SUPPORT`

```c
#ifndef FIO_MUSTACHE_LAMBDA_SUPPORT
#define FIO_MUSTACHE_LAMBDA_SUPPORT 0
#endif
```

When enabled, stores raw section text for lambda-style section handling through `is_lambda`.

#### `FIO_MUSTACHE_ISOLATE_PARTIALS`

```c
#ifndef FIO_MUSTACHE_ISOLATE_PARTIALS
#define FIO_MUSTACHE_ISOLATE_PARTIALS 1
#endif
```

When enabled, limits partial lookup scope to the context of the partial's section.

#### `FIO_MUSTACHE_SECURE_PATH`

```c
#ifndef FIO_MUSTACHE_SECURE_PATH
#define FIO_MUSTACHE_SECURE_PATH 1
#endif
```

When enabled, skips partial names that attempt `../` path traversal.

### Syntax Quick Map

| Syntax | Meaning |
| --- | --- |
| `{{name}}` | escaped variable |
| `{{{name}}}` | raw variable |
| `{{&name}}` | raw variable |
| `{{#items}}...{{/items}}` | truthy section / array iteration |
| `{{^items}}...{{/items}}` | inverted section |
| `{{>partial}}` | partial template include |
| `{{! comment}}` | comment |
| `{{=<% %>=}}` | delimiter change |
| `{{.}}` | current context |

Dot lookup such as `person.name` is supported by repeatedly calling `get_var` for path segments.

### Types

#### `fio_mustache_s`

```c
typedef struct fio_mustache_s fio_mustache_s;
```

Opaque parsed template object. Internally it is stored as a `fio_bstr` instruction stream.

**Ownership:** `fio_mustache_load` returns an owned template. Free it with `fio_mustache_free`. `fio_mustache_dup` creates a copy; it is not a shared reference counter in this header.

#### `fio_mustache_bargs_s`

```c
typedef struct fio_mustache_bargs_s fio_mustache_bargs_s;
```

Forward declaration for build arguments. The full struct is listed below.

#### `fio_mustache_load_args_s`

```c
typedef struct {
  fio_buf_info_s data;
  fio_buf_info_s filename;
  fio_buf_info_s (*load_file_data)(fio_buf_info_s filename, void *udata);
  void (*free_file_data)(fio_buf_info_s file_data, void *udata);
  void (*on_yaml_front_matter)(fio_buf_info_s yaml_front_matter, void *udata);
  void *udata;
} fio_mustache_load_args_s;
```

Load / parse settings.

**Members:**
- `data` - preloaded template bytes
- `filename` - file name, also used as base path for partials
- `load_file_data` - callback that loads file contents
- `free_file_data` - callback that frees loaded file contents
- `on_yaml_front_matter` - called when front matter is found
- `udata` - user data for load callbacks

If both `load_file_data` and `free_file_data` are missing, defaults use `fio_bstr_readfile` and `fio_bstr_free`. If one is custom, provide the matching cleanup. Either `filename` or `data` is required.

#### `fio_mustache_bargs_s`

```c
struct fio_mustache_bargs_s {
  void *(*write_text)(void *udata, fio_buf_info_s txt);
  void *(*write_text_escaped)(void *udata, fio_buf_info_s raw);
  void *(*get_var)(void *ctx, fio_buf_info_s name);
  size_t (*array_length)(void *ctx);
  void *(*get_var_index)(void *ctx, size_t index);
  fio_buf_info_s (*var2str)(void *var);
  int (*var_is_truthful)(void *ctx);
  void (*release_var)(void *ctx);
  int (*is_lambda)(void **udata,
                   void *ctx,
                   fio_buf_info_s raw_template_section);
  void *ctx;
  void *udata;
};
```

Build / render callbacks.

**Members:**
- `write_text` - writes raw template text
- `write_text_escaped` - writes escaped variable text
- `get_var` - returns a value for `name` from `ctx`
- `array_length` - returns array length, or `0` for non-array values
- `get_var_index` - returns an item context by index
- `var2str` - returns a string view for a value
- `var_is_truthful` - returns non-zero for truthy values
- `release_var` - releases values returned by callbacks
- `is_lambda` - handles lambda sections when lambda support is enabled
- `ctx` - root render context
- `udata` - output / user pointer; the final value is returned by `fio_mustache_build`

If both writer callbacks are missing, defaults append to a `fio_bstr` in `udata`. The returned pointer should be freed with `fio_bstr_free`.

### Loading

#### `fio_mustache_load`

```c
SFUNC fio_mustache_s *fio_mustache_load(fio_mustache_load_args_s settings);
#define fio_mustache_load(...) \
  fio_mustache_load((fio_mustache_load_args_s){__VA_ARGS__})
```

Loads and parses a template from `settings.data` or `settings.filename`.

If `filename` is provided and `data` is empty, the loader callback is used. If both are provided, `data` is parsed and `filename` is kept for partial path resolution.

**Returns:** a parsed template object, or `NULL` on error.

#### `fio_mustache_free`

```c
SFUNC void fio_mustache_free(fio_mustache_s *m);
```

Frees a parsed template. Accepts `NULL`.

#### `fio_mustache_dup`

```c
SFUNC fio_mustache_s *fio_mustache_dup(fio_mustache_s *m);
```

Copies a parsed template using `fio_bstr_copy`.

**Returns:** the copied template, or `NULL` if `m == NULL`.

### Rendering

#### `fio_mustache_build`

```c
SFUNC void *fio_mustache_build(fio_mustache_s *m, fio_mustache_bargs_s);
#define fio_mustache_build(m, ...) \
  fio_mustache_build((m), ((fio_mustache_bargs_s){__VA_ARGS__}))
```

Renders `m` with the supplied context and callbacks.

**Returns:** the final `udata` value. If `m == NULL`, returns `args.udata` unchanged.

During rendering:
- escaped variables call `write_text_escaped`
- raw variables call `write_text`
- sections render once for truthy non-arrays, or once per array item
- inverted sections render when `var_is_truthful` returns zero
- values returned from lookup / array callbacks are released with `release_var`

### Partials and Files

Partials are loaded by name through `load_file_data`. The implementation tries each name with these suffixes:

1. `.mustache`
2. `.html`
3. no suffix

Relative partials are searched against the including template's path chain, then as provided. With `FIO_MUSTACHE_SECURE_PATH`, `../` partials are skipped.

### YAML Front Matter

If a template begins with `---` followed by a line break, the parser scans until a closing `---` line and calls:

```c
void (*on_yaml_front_matter)(fio_buf_info_s yaml_front_matter, void *udata);
```

The front matter bytes are not rendered. The engine does not parse YAML for you. It has enough hobbies.

### Thread-Safety

Parsed templates are immutable during `fio_mustache_build`, so separate builds can use the same template as long as your callbacks and contexts are safe. Loading and freeing are caller-owned operations.

### Example

```c
#define FIO_MUSTACHE
#include "fio-stl.h"
#include <stdio.h>
#include <string.h>

typedef struct {
  const char *name;
} data_s;

static void *get_var(void *ctx, fio_buf_info_s name) {
  data_s *d = (data_s *)ctx;
  if (name.len == 4 && !memcmp(name.buf, "name", 4))
    return (void *)d->name;
  return NULL;
}

static fio_buf_info_s var2str(void *var) {
  char *s = (char *)var;
  return FIO_BUF_INFO2(s, strlen(s));
}

int main(void) {
  char template_text[] = "Hello, {{name}}!\n";
  data_s data = {.name = "World"};

  fio_mustache_s *m = fio_mustache_load(
      .data = FIO_BUF_INFO1(template_text));
  if (!m)
    return 1;

  char *out = fio_mustache_build(m,
      .ctx = &data,
      .get_var = get_var,
      .var2str = var2str);

  if (out) {
    fwrite(out, 1, fio_bstr_len(out), stdout);
    fio_bstr_free(out);
  }

  fio_mustache_free(m);
  return 0;
}
```

---
# Crypto Core

```c
#define FIO_CRYPTO_CORE
#include "fio-stl.h"
```

The crypto slice is a small, zero-dependency toolbox for code that needs to hash, sign, encrypt, parse certificates, or derive keys without dragging in a larger library. Handy? yes. Magic security dust? no.

`FIO_CRYPTO_CORE` itself is the shared glue. It defines the AEAD function pointer types used by the symmetric ciphers and by higher-level helpers that accept “any AEAD with this call shape”. Most users include a specific module, or define `FIO_CRYPTO` to pull in the crypto set.

## Map of the Crypto Modules

| Class | Docs | What lives there |
| --- | --- | --- |
| Hash | [SHA-1](./152%20sha1.md), [SHA-2](./152%20sha2.md), [SHA-3 / SHAKE](./152%20sha3.md), [BLAKE2](./152%20blake2.md) | Digest functions, streaming hash contexts, SHA/BLAKE HMAC helpers where provided. SHA-1 is legacy-only glue. |
| Symmetric | [ChaCha20-Poly1305](./152%20chacha20poly1305.md), [AES-GCM](./153%20aes.md) | AEAD encryption, stream cipher helpers, and the shared in-place authenticated encryption shape. |
| Asymmetric | [Ed25519 & X25519](./154%20ed25519.md), [P-256](./154%20p256.md), [P-384](./154%20p384.md), [RSA](./155%20rsa.md) | Signatures, key exchange, ECIES-style X25519 encryption, and RSA signatures for TLS-style use cases. |
| PKI | [ASN.1 DER](./155%20der.md), [X.509](./155%20x509.md), [PEM](./156%20pem.md) | DER/PEM parsing, certificate fields, hostname checks, signature checks, and certificate chain validation helpers. |
| KDF | [HKDF](./152%20hkdf.md), [Argon2](./159%20argon2.md), [Lyra2](./159%20lyra2.md), [OTP](./159%20otp.md), [Secrets](./159%20secret.md) | Key derivation, password hashing, TOTP codes, and hashing a process secret into a stable internal value. |
| Post-Quantum | [ML-KEM-768](./156%20mlkem.md) | ML-KEM-768 key encapsulation and the X25519MLKEM768 hybrid key exchange shape used by TLS drafts. |

## The Shared AEAD Shape

The core header defines two function pointer types:

```c
typedef void(fio_crypto_enc_fn)(void *restrict mac,
                                void *restrict data,
                                size_t len,
                                const void *ad,
                                size_t adlen,
                                const void *key,
                                const void *nonce);

typedef int(fio_crypto_dec_fn)(void *restrict mac,
                               void *restrict data,
                               size_t len,
                               const void *ad,
                               size_t adlen,
                               const void *key,
                               const void *nonce);
```

Current AEAD implementations that match this shape include:

- `fio_chacha20_poly1305_enc` / `fio_chacha20_poly1305_dec`
- `fio_xchacha20_poly1305_enc` / `fio_xchacha20_poly1305_dec`
- `fio_aes128_gcm_enc` / `fio_aes128_gcm_dec`
- `fio_aes256_gcm_enc` / `fio_aes256_gcm_dec`

These functions encrypt or decrypt `data` in place. `ad` is authenticated but not encrypted. `mac` is the authentication tag buffer; the current AEAD modules use 16-byte tags. Key and nonce sizes belong to the selected cipher, not to the function pointer type.

Decryption returns `0` when authentication succeeds and `-1` when it fails. Treat any failure as “message not trusted”; do not parse, log in detail, or partially use the plaintext.

## Safe Use, Plainly

This module helps when you need portable crypto building blocks inside the STL: hashing buffers, deriving session keys, signing messages, checking certificates, or doing AEAD encryption with a known protocol design.

It does **not** promise that your protocol is safe. It does not manage long-term keys for you, pick nonces, rotate secrets, maintain a root trust store, protect keys in hardware, provide FIPS validation, or substitute for an external security review. Several public-key and PKI headers explicitly note that they have not been independently audited.

Use this toolbox when:

- you need the zero-dependency facil.io STL path;
- the protocol is already designed and reviewed;
- inputs, key sizes, nonce rules, and failure paths are controlled;
- a compact embedded copy is more important than delegating to a platform stack.

Prefer audited platform/security libraries such as OpenSSL, BoringSSL, libsodium, CommonCrypto, platform TLS, or OS key stores when:

- you are building TLS, PKI, payment, identity, or compliance-sensitive systems;
- private keys live for a long time or leave the process boundary;
- certificate trust decisions must track OS/browser policy;
- side-channel hardening, hardware acceleration policy, or formal validation matters.

In short: this is a sharp little knife. Useful. Keep fingers clear.

## Implementation Notes

The crypto headers use optional CPU-specific paths where available, including SHA intrinsics, AES-NI/PCLMULQDQ, ARM crypto extensions, NEON, and AVX2. The public APIs stay the same when the implementation falls back to portable C.

The SIMD notes in `150 crypto core.h` are documentation for maintainers: ChaCha20-Poly1305 can process blocks in parallel, Curve25519 uses vectorized field add/sub/cswap where useful, and ML-KEM vectorizes NTT/reduction paths. These are implementation details, not extra guarantees.
# BLAKE2

```c
#define FIO_BLAKE2
#include "fio-stl.h"
```

BLAKE2b (64-bit, up to 64-byte digest) and BLAKE2s (32-bit, up to 32-byte digest). Both are fast, modern cryptographic hashes with built-in keyed hashing, standard HMAC wrappers, and a streaming init/consume/finalize API.

### Types

#### `fio_blake2b_s`

```c
typedef struct {
  uint64_t h[8];    /* state */
  uint64_t t[2];    /* total bytes processed (128-bit counter) */
  uint64_t f[2];    /* finalization flags */
  uint8_t buf[128]; /* input buffer */
  size_t buflen;    /* bytes in buffer */
  size_t outlen;    /* digest length */
} fio_blake2b_s;
```

Streaming BLAKE2b context. Treat it as opaque and initialize with `fio_blake2b_init`.

#### `fio_blake2s_s`

```c
typedef struct {
  uint32_t h[8];   /* state */
  uint32_t t[2];   /* total bytes processed (64-bit counter) */
  uint32_t f[2];   /* finalization flags */
  uint8_t buf[64]; /* input buffer */
  size_t buflen;   /* bytes in buffer */
  size_t outlen;   /* digest length */
} fio_blake2s_s;
```

Streaming BLAKE2s context. Treat it as opaque and initialize with `fio_blake2s_init`.

### BLAKE2b Functions

#### `fio_blake2b`

```c
FIO_IFUNC fio_u512 fio_blake2b(const void *data, uint64_t len);
```

One-shot BLAKE2b with the full 64-byte digest.

**Parameters:**
- `data` — bytes to hash.
- `len` — number of bytes.

**Returns:** the 64-byte digest as `fio_u512`.

**Note:** signature matches `fio_sha512`, so the two are interchangeable as `fio_u512 (*)(const void *, uint64_t)`.

#### `fio_blake2b_hash`

```c
SFUNC void fio_blake2b_hash(void *restrict out,
                            size_t outlen,
                            const void *restrict data,
                            size_t len,
                            const void *restrict key,
                            size_t keylen);
```

Flexible-output BLAKE2b with optional keyed hashing.

**Parameters:**
- `out` — destination buffer (at least `outlen` bytes).
- `outlen` — digest length (1–64; defaults to 64 if 0).
- `data` — bytes to hash.
- `len` — data length.
- `key` — optional secret key (NULL for unkeyed).
- `keylen` — key length (0–64; clamped to 64 if larger).

**Note:** keyed BLAKE2b is a built-in MAC mode; for RFC-2104 HMAC, use `fio_blake2b_hmac`.

#### `fio_blake2b_hmac`

```c
SFUNC fio_u512 fio_blake2b_hmac(const void *key,
                                uint64_t key_len,
                                const void *msg,
                                uint64_t msg_len);
```

Standard HMAC-BLAKE2b with a 64-byte digest.

**Parameters:**
- `key` — secret key.
- `key_len` — key length.
- `msg` — message to authenticate.
- `msg_len` — message length.

**Returns:** 64-byte authentication code as `fio_u512`.

#### `fio_blake2b_init`

```c
SFUNC fio_blake2b_s fio_blake2b_init(size_t outlen,
                                     const void *key,
                                     size_t keylen);
```

Initializes a BLAKE2b streaming context.

**Parameters:**
- `outlen` — desired digest length (1–64; defaults to 64 if 0).
- `key` — optional secret key (NULL for unkeyed).
- `keylen` — key length (0–64; clamped to 64 if larger).

#### `fio_blake2b_consume`

```c
SFUNC void fio_blake2b_consume(fio_blake2b_s *restrict h,
                               const void *restrict data,
                               size_t len);
```

Feeds more data into a BLAKE2b streaming context. May be called repeatedly.

#### `fio_blake2b_finalize`

```c
SFUNC fio_u512 fio_blake2b_finalize(fio_blake2b_s *h);
```

Finalizes the stream and returns the digest.

**Returns:** digest as `fio_u512`. Only the first `outlen` bytes are valid.

### BLAKE2s Functions

#### `fio_blake2s`

```c
FIO_IFUNC fio_u256 fio_blake2s(const void *data, uint64_t len);
```

One-shot BLAKE2s with the full 32-byte digest.

#### `fio_blake2s_hash`

```c
SFUNC void fio_blake2s_hash(void *restrict out,
                            size_t outlen,
                            const void *restrict data,
                            size_t len,
                            const void *restrict key,
                            size_t keylen);
```

Flexible-output BLAKE2s with optional keyed hashing. `outlen` is 1–32 (defaults to 32 if 0).

#### `fio_blake2s_hmac`

```c
SFUNC fio_u256 fio_blake2s_hmac(const void *key,
                                uint64_t key_len,
                                const void *msg,
                                uint64_t msg_len);
```

Standard HMAC-BLAKE2s with a 32-byte digest.

#### `fio_blake2s_init`

```c
SFUNC fio_blake2s_s fio_blake2s_init(size_t outlen,
                                     const void *key,
                                     size_t keylen);
```

Initializes a BLAKE2s streaming context. `outlen` is 1–32 (defaults to 32 if 0).

#### `fio_blake2s_consume`

```c
SFUNC void fio_blake2s_consume(fio_blake2s_s *restrict h,
                               const void *restrict data,
                               size_t len);
```

Feeds more data into a BLAKE2s streaming context.

#### `fio_blake2s_finalize`

```c
SFUNC fio_u256 fio_blake2s_finalize(fio_blake2s_s *h);
```

Finalizes the stream and returns the digest.

### Examples

```c
#define FIO_BLAKE2
#include "fio-stl.h"

void examples(void) {
  const char *msg = "Hello, BLAKE2!";

  /* One-shot */
  fio_u512 b2b = fio_blake2b(msg, strlen(msg));
  fio_u256 b2s = fio_blake2s(msg, strlen(msg));

  /* Flexible output with key */
  uint8_t digest[16];
  fio_blake2b_hash(digest, 16, msg, strlen(msg), "key", 3);

  /* Streaming */
  fio_blake2b_s ctx = fio_blake2b_init(64, NULL, 0);
  fio_blake2b_consume(&ctx, "Hello, ", 7);
  fio_blake2b_consume(&ctx, "BLAKE2!", 7);
  fio_u512 result = fio_blake2b_finalize(&ctx);

  /* HMAC */
  fio_u512 mac = fio_blake2b_hmac("key", 3, msg, strlen(msg));
}
```

### BLAKE2b vs BLAKE2s

| Property | BLAKE2b | BLAKE2s |
|---|---|---|
| Word size | 64-bit | 32-bit |
| Block size | 128 bytes | 64 bytes |
| Max digest | 64 bytes | 32 bytes |
| Max key | 64 bytes | 32 bytes |
| Rounds | 12 | 10 |
| Result type | `fio_u512` | `fio_u256` |

------------------------------------------------------------
# ChaCha20 & Poly1305

```c
#define FIO_CHACHA
#include "fio-stl.h"
```

ChaCha20 stream cipher, Poly1305 authenticator, and the ChaCha20-Poly1305 AEAD combination. Also includes XChaCha20-Poly1305 with a 192-bit nonce, which is safer for random nonces than the 96-bit variant.

**Security note:** this implementation has not been independently audited. Use at your own risk, and prefer a tested cryptographic library when one is available.

### ChaCha20-Poly1305 API

#### `fio_chacha20_poly1305_enc`

```c
SFUNC void fio_chacha20_poly1305_enc(void *restrict mac,
                                     void *restrict data,
                                     size_t len,
                                     const void *ad,
                                     size_t adlen,
                                     const void *key,
                                     const void *nonce);
```

In-place encryption with a 16-byte Poly1305 tag.

**Parameters:**
- `mac` — output buffer; must have at least 16 writable bytes.
- `data` — plaintext buffer; encrypted in place.
- `len` — data length.
- `ad` — additional authenticated data (not encrypted); may be `NULL`.
- `adlen` — length of `ad`.
- `key` — 32-byte key.
- `nonce` — 12-byte nonce.

#### `fio_chacha20_poly1305_dec`

```c
SFUNC int fio_chacha20_poly1305_dec(void *restrict mac,
                                    void *restrict data,
                                    size_t len,
                                    const void *ad,
                                    size_t adlen,
                                    const void *key,
                                    const void *nonce);
```

In-place decryption with tag verification.

**Returns:** `0` on success, `-1` if authentication fails. On failure `data` is zeroed.

#### `fio_chacha20_poly1305_auth`

```c
SFUNC void fio_chacha20_poly1305_auth(void *restrict mac,
                                      void *restrict data,
                                      size_t len,
                                      const void *ad,
                                      size_t adlen,
                                      const void *key,
                                      const void *nonce);
```

Computes the Poly1305 tag for already-encrypted data without decrypting it.

### Standalone ChaCha20 / Poly1305

#### `fio_chacha20`

```c
SFUNC void fio_chacha20(void *restrict data,
                        size_t len,
                        const void *key,
                        const void *nonce,
                        uint32_t counter);
```

In-place ChaCha20 encryption or decryption.

**Parameters:**
- `data` — buffer to transform in place.
- `len` — buffer length.
- `key` — 32-byte key.
- `nonce` — 12-byte nonce.
- `counter` — block counter; usually `1` unless resuming mid-ciphertext.

#### `fio_poly1305_auth`

```c
SFUNC void fio_poly1305_auth(void *restrict mac_dest,
                             void *restrict message,
                             size_t len,
                             const void *ad,
                             size_t ad_len,
                             const void *key256bits);
```

Computes a Poly1305 MAC for `message` and `ad` using a 32-byte key.

**Parameters:**
- `mac_dest` — output buffer; must have at least 16 writable bytes.
- `message` — message to authenticate; may be `NULL` if `len` is 0.
- `len` — message length.
- `ad` — additional data; may be `NULL`.
- `ad_len` — additional data length.
- `key256bits` — 32-byte Poly1305 key.

### XChaCha20-Poly1305 API

XChaCha20 uses a 24-byte nonce, making random nonces safe from birthday-bound collisions.

#### `fio_xchacha20_poly1305_enc`

```c
SFUNC void fio_xchacha20_poly1305_enc(void *restrict mac,
                                      void *restrict data,
                                      size_t len,
                                      const void *ad,
                                      size_t adlen,
                                      const void *key,
                                      const void *nonce);
```

Same shape as `fio_chacha20_poly1305_enc`, but `nonce` must be 24 bytes.

#### `fio_xchacha20_poly1305_dec`

```c
SFUNC int fio_xchacha20_poly1305_dec(void *restrict mac,
                                     void *restrict data,
                                     size_t len,
                                     const void *ad,
                                     size_t adlen,
                                     const void *key,
                                     const void *nonce);
```

Same shape as `fio_chacha20_poly1305_dec`, but `nonce` must be 24 bytes.

#### `fio_xchacha20`

```c
SFUNC void fio_xchacha20(void *restrict data,
                         size_t len,
                         const void *key,
                         const void *nonce,
                         uint32_t counter);
```

In-place XChaCha20 encryption or decryption.

### Example

```c
#define FIO_CHACHA
#include "fio-stl.h"

int main(void) {
  uint8_t key[32] = {0};
  uint8_t nonce[12] = {0};
  uint8_t msg[32] = "hello, chacha world!";
  uint8_t tag[16];

  fio_rand_bytes_secure(key, sizeof(key));
  fio_rand_bytes_secure(nonce, sizeof(nonce));

  fio_chacha20_poly1305_enc(tag, msg, sizeof(msg), NULL, 0, key, nonce);

  if (fio_chacha20_poly1305_dec(tag, msg, sizeof(msg), NULL, 0, key, nonce)) {
    printf("authentication failed\n");
    return 1;
  }
  return 0;
}
```

------------------------------------------------------------
# HKDF

```c
#define FIO_HKDF
#include "fio-stl.h"
```

HMAC-based Key Derivation Function per [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869). HKDF extracts entropy from input keying material into a pseudorandom key, then expands that key into one or more output keys.

Supports SHA-256 and SHA-384 as the underlying HMAC.

**Note:** HKDF needs `fio_sha256_hmac` and `fio_sha512_hmac`. Define `FIO_SHA2` before `FIO_HKDF`, or use `FIO_CRYPTO` to pull in everything.

### Constants

#### `FIO_HKDF_SHA256_HASH_LEN`

```c
#define FIO_HKDF_SHA256_HASH_LEN 32
```

SHA-256 hash length in bytes.

#### `FIO_HKDF_SHA384_HASH_LEN`

```c
#define FIO_HKDF_SHA384_HASH_LEN 48
```

SHA-384 hash length in bytes.

### Functions

#### `fio_hkdf_extract`

```c
SFUNC void fio_hkdf_extract(void *restrict prk,
                            const void *restrict salt,
                            size_t salt_len,
                            const void *restrict ikm,
                            size_t ikm_len,
                            int use_sha384);
```

HKDF-Extract: `PRK = HMAC-Hash(salt, IKM)`.

**Parameters:**
- `prk` — output buffer (32 bytes for SHA-256, 48 for SHA-384).
- `salt` — optional salt; if `NULL`, a zero string of hash length is used.
- `salt_len` — salt length.
- `ikm` — input keying material.
- `ikm_len` — IKM length.
- `use_sha384` — non-zero for SHA-384, zero for SHA-256.

#### `fio_hkdf_expand`

```c
SFUNC void fio_hkdf_expand(void *restrict okm,
                           size_t okm_len,
                           const void *restrict prk,
                           size_t prk_len,
                           const void *restrict info,
                           size_t info_len,
                           int use_sha384);
```

HKDF-Expand: `OKM = HKDF-Expand(PRK, info, L)`.

**Parameters:**
- `okm` — output buffer.
- `okm_len` — desired output length (max `255 * hash_len`).
- `prk` — pseudorandom key from Extract.
- `prk_len` — 32 for SHA-256, 48 for SHA-384.
- `info` — optional context/info string; may be `NULL`.
- `info_len` — info length.
- `use_sha384` — non-zero for SHA-384, zero for SHA-256.

#### `fio_hkdf`

```c
SFUNC void fio_hkdf(void *restrict okm,
                    size_t okm_len,
                    const void *restrict salt,
                    size_t salt_len,
                    const void *restrict ikm,
                    size_t ikm_len,
                    const void *restrict info,
                    size_t info_len,
                    int use_sha384);
```

Combined HKDF: Extract followed by Expand in one call.

### Example

```c
#define FIO_SHA2
#define FIO_HKDF
#include "fio-stl.h"

int main(void) {
  const char *ikm = "input keying material";
  const char *salt = "salt";
  const char *info = "app context";
  uint8_t key[32];

  fio_hkdf(key, sizeof(key), salt, strlen(salt),
           ikm, strlen(ikm), info, strlen(info), 0);
  return 0;
}
```

------------------------------------------------------------
# SHA-1

```c
#define FIO_SHA1
#include "fio-stl.h"
```

SHA-1 hashing and HMAC. It is cryptographically broken, so do not use it for anything that needs security. Some protocols (hello, WebSockets) still ask for it, so it lives here for compatibility.

### Types

#### `fio_sha1_s`

```c
typedef union {
#ifdef __SIZEOF_INT128__
  __uint128_t align__;
#else
  uint64_t align__;
#endif
  uint32_t v[5];
  uint8_t digest[20];
} fio_sha1_s;
```

Container for the 20-byte SHA-1 digest.

**Members:**
- `align__` - alignment padding.
- `v` - the 5 × 32-bit state words.
- `digest` - the 20-byte digest as raw bytes.

### API Functions

#### `fio_sha1`

```c
fio_sha1_s fio_sha1(const void *data, uint64_t len);
```

One-shot SHA-1 hash of `len` bytes at `data`.

**Parameters:**
- `data` - pointer to the data to hash.
- `len` - length of the data in bytes.

**Returns:** a `fio_sha1_s` containing the 20-byte digest.

#### `fio_sha1_hmac`

```c
fio_sha1_s fio_sha1_hmac(const void *key,
                         uint64_t key_len,
                         const void *msg,
                         uint64_t msg_len);
```

Computes HMAC-SHA1, producing a 20-byte authentication code.

**Parameters:**
- `key` - pointer to the secret key.
- `key_len` - length of the key in bytes.
- `msg` - pointer to the message to authenticate.
- `msg_len` - length of the message in bytes.

**Returns:** a `fio_sha1_s` containing the 20-byte HMAC.

**Note:** keys longer than 64 bytes are first hashed with SHA-1.

#### `fio_sha1_len`

```c
size_t fio_sha1_len(void);
```

**Returns:** the SHA-1 digest length in bytes (20).

#### `fio_sha1_digest`

```c
uint8_t *fio_sha1_digest(fio_sha1_s *s);
```

**Parameters:**
- `s` - pointer to a `fio_sha1_s` result.

**Returns:** a pointer to the 20-byte digest inside `s`.

### Examples

#### One-shot hash

```c
#define FIO_SHA1
#include "fio-stl.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  const char *msg = "hello";
  fio_sha1_s r = fio_sha1(msg, strlen(msg));
  for (size_t i = 0; i < fio_sha1_len(); ++i)
    printf("%02x", fio_sha1_digest(&r)[i]);
  printf("\n");
  return 0;
}
```

#### HMAC

```c
fio_sha1_s mac = fio_sha1_hmac("secret", 6, "hello", 5);
```

**Note:** SHA-1 is broken. Prefer SHA-256, SHA-3, or another modern hash for security-sensitive work.

------------------------------------------------------------
# SHA-2

```c
#define FIO_SHA2
#include "fio-stl.h"
```

SHA-256 and SHA-512 hashing, with both one-shot and streaming APIs plus HMAC. These are the workhorse hashes for checksums, signatures, and anything that needs a collision-resistant digest.

### Types

#### `fio_sha256_s`

```c
typedef struct {
  fio_u256 hash;
  fio_u512 cache;
  uint64_t total_len;
} fio_sha256_s;
```

Streaming SHA-256 state. Initialize with `fio_sha256_init`, feed with `fio_sha256_consume`, and finish with `fio_sha256_finalize`.

#### `fio_sha512_s`

```c
typedef struct {
  fio_u512 hash;
  fio_u1024 cache;
  uint64_t total_len;
} fio_sha512_s;
```

Streaming SHA-512 state. Initialize with `fio_sha512_init`, feed with `fio_sha512_consume`, and finish with `fio_sha512_finalize`.

### SHA-256 Functions

#### `fio_sha256`

```c
fio_u256 fio_sha256(const void *data, uint64_t len);
```

One-shot SHA-256 of `len` bytes at `data`.

**Parameters:**
- `data` - pointer to the data to hash.
- `len` - length of the data in bytes.

**Returns:** a `fio_u256` containing the 32-byte digest.

#### `fio_sha256_init`

```c
fio_sha256_s fio_sha256_init(void);
```

**Returns:** a freshly initialized SHA-256 streaming state.

#### `fio_sha256_consume`

```c
void fio_sha256_consume(fio_sha256_s *h, const void *data, uint64_t len);
```

Feeds more data into a streaming SHA-256 hash. May be called repeatedly.

**Parameters:**
- `h` - pointer to the streaming state.
- `data` - pointer to the data to add.
- `len` - length of the data in bytes.

#### `fio_sha256_finalize`

```c
fio_u256 fio_sha256_finalize(fio_sha256_s *h);
```

Finalizes the hash and returns the 32-byte digest.

**Parameters:**
- `h` - pointer to the streaming state.

**Returns:** a `fio_u256` containing the digest.

**Note:** after finalization, `h` should not be reused without re-initialization.

### SHA-512 Functions

#### `fio_sha512`

```c
fio_u512 fio_sha512(const void *data, uint64_t len);
```

One-shot SHA-512 of `len` bytes at `data`.

**Parameters:**
- `data` - pointer to the data to hash.
- `len` - length of the data in bytes.

**Returns:** a `fio_u512` containing the 64-byte digest.

#### `fio_sha512_init`

```c
fio_sha512_s fio_sha512_init(void);
```

**Returns:** a freshly initialized SHA-512 streaming state.

#### `fio_sha512_consume`

```c
void fio_sha512_consume(fio_sha512_s *h, const void *data, uint64_t len);
```

Feeds more data into a streaming SHA-512 hash. May be called repeatedly.

**Parameters:**
- `h` - pointer to the streaming state.
- `data` - pointer to the data to add.
- `len` - length of the data in bytes.

#### `fio_sha512_finalize`

```c
fio_u512 fio_sha512_finalize(fio_sha512_s *h);
```

Finalizes the hash and returns the 64-byte digest.

**Parameters:**
- `h` - pointer to the streaming state.

**Returns:** a `fio_u512` containing the digest.

**Note:** after finalization, `h` should not be reused without re-initialization.

### SHA-384 Functions

SHA-384 is SHA-512 with different initial values, truncated to 48 bytes
(truncating a SHA-512 *result* is NOT the same as SHA-384).

#### `fio_sha384`

```c
fio_u512 fio_sha384(const void *data, uint64_t len);
```

Computes SHA-384 in a single call.

**Returns:** a `fio_u512` whose first 48 bytes hold the digest.

#### `fio_sha384_init` / `fio_sha384_consume` / `fio_sha384_finalize`

```c
fio_sha512_s fio_sha384_init(void);
void fio_sha384_consume(fio_sha512_s *h, const void *data, uint64_t len);
fio_u512 fio_sha384_finalize(fio_sha512_s *h);
```

Streaming SHA-384: same state type as SHA-512; `fio_sha384_init` seeds the
SHA-384 initial values, consume/finalize alias the SHA-512 operations.

### HMAC Functions

#### `fio_sha256_hmac`

```c
fio_u256 fio_sha256_hmac(const void *key,
                         uint64_t key_len,
                         const void *msg,
                         uint64_t msg_len);
```

Computes HMAC-SHA256, producing a 32-byte authentication code.

**Parameters:**
- `key` - pointer to the secret key.
- `key_len` - length of the key in bytes.
- `msg` - pointer to the message to authenticate.
- `msg_len` - length of the message in bytes.

**Returns:** a `fio_u256` containing the 32-byte HMAC.

**Note:** keys longer than 64 bytes are hashed first.

#### `fio_sha512_hmac`

```c
fio_u512 fio_sha512_hmac(const void *key,
                         uint64_t key_len,
                         const void *msg,
                         uint64_t msg_len);
```

Computes HMAC-SHA512, producing a 64-byte authentication code.

**Parameters:**
- `key` - pointer to the secret key.
- `key_len` - length of the key in bytes.
- `msg` - pointer to the message to authenticate.
- `msg_len` - length of the message in bytes.

**Returns:** a `fio_u512` containing the 64-byte HMAC.

**Note:** keys longer than 128 bytes are hashed first.

### Examples

#### One-shot SHA-256

```c
#define FIO_SHA2
#include "fio-stl.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  const char *msg = "hello";
  fio_u256 r = fio_sha256(msg, strlen(msg));
  for (size_t i = 0; i < sizeof(r.u8); ++i)
    printf("%02x", r.u8[i]);
  printf("\n");
  return 0;
}
```

#### Streaming SHA-256

```c
fio_sha256_s h = fio_sha256_init();
fio_sha256_consume(&h, "hello, ", 7);
fio_sha256_consume(&h, "world", 5);
fio_u256 r = fio_sha256_finalize(&h);
```

#### HMAC-SHA256

```c
fio_u256 mac = fio_sha256_hmac("secret", 6, "hello", 5);
```

**Note:** all state lives in caller-owned variables. The functions do not allocate heap memory and are thread-safe as long as each thread uses its own state.

------------------------------------------------------------
# SHA-3

```c
#define FIO_SHA3
#include "fio-stl.h"
```

The SHA-3 family (SHA3-224/256/384/512) and the SHAKE128/SHAKE256 extendable-output functions, built on Keccak-f[1600]. All variants share one streaming context type.

### Types

#### `fio_sha3_s`

```c
typedef struct {
  uint64_t state[25];
  uint8_t buf[200];
  size_t buflen;
  size_t rate;
  size_t outlen;
  uint8_t delim;
} fio_sha3_s;
```

Streaming context for every SHA-3 and SHAKE variant.

**Members:**
- `state` - the 25-word Keccak-f[1600] state.
- `buf` - partial-block input buffer.
- `buflen` - bytes currently buffered.
- `rate` - absorption/squeezing rate in bytes, set by the init function.
- `outlen` - fixed output length in bytes for SHA-3; 0 for SHAKE.
- `delim` - domain separator (`0x06` for SHA-3, `0x1F` for SHAKE).

**Note:** treat this as opaque. Always initialize it with one of the `fio_sha3_*_init` or `fio_shake*_init` functions.

### SHA-3 Fixed-Output Functions

#### `fio_sha3_224_init`

```c
fio_sha3_s fio_sha3_224_init(void);
```

**Returns:** a SHA3-224 context (28-byte output, rate 144).

#### `fio_sha3_256_init`

```c
fio_sha3_s fio_sha3_256_init(void);
```

**Returns:** a SHA3-256 context (32-byte output, rate 136).

#### `fio_sha3_384_init`

```c
fio_sha3_s fio_sha3_384_init(void);
```

**Returns:** a SHA3-384 context (48-byte output, rate 104).

#### `fio_sha3_512_init`

```c
fio_sha3_s fio_sha3_512_init(void);
```

**Returns:** a SHA3-512 context (64-byte output, rate 72).

#### `fio_sha3_consume`

```c
void fio_sha3_consume(fio_sha3_s *restrict h,
                      const void *restrict data,
                      size_t len);
```

Feeds `len` bytes into a SHA-3 context. May be called repeatedly.

**Parameters:**
- `h` - pointer to the streaming context.
- `data` - pointer to the data to absorb.
- `len` - length of the data in bytes.

#### `fio_sha3_finalize`

```c
void fio_sha3_finalize(fio_sha3_s *restrict h, void *restrict out);
```

Pads, permutes, and writes `h->outlen` digest bytes to `out`.

**Parameters:**
- `h` - pointer to the streaming context.
- `out` - output buffer, must hold at least `h->outlen` bytes.

**Note:** do not call this on a SHAKE context. Use `fio_shake_squeeze` instead.

#### `fio_sha3_224`

```c
void fio_sha3_224(void *restrict out,
                  const void *restrict data,
                  size_t len);
```

One-shot SHA3-224.

**Parameters:**
- `out` - output buffer (28 bytes).
- `data` - pointer to the data to hash.
- `len` - length of the data in bytes.

#### `fio_sha3_256`

```c
void fio_sha3_256(void *restrict out,
                  const void *restrict data,
                  size_t len);
```

One-shot SHA3-256.

**Parameters:**
- `out` - output buffer (32 bytes).
- `data` - pointer to the data to hash.
- `len` - length of the data in bytes.

#### `fio_sha3_384`

```c
void fio_sha3_384(void *restrict out,
                  const void *restrict data,
                  size_t len);
```

One-shot SHA3-384.

**Parameters:**
- `out` - output buffer (48 bytes).
- `data` - pointer to the data to hash.
- `len` - length of the data in bytes.

#### `fio_sha3_512`

```c
void fio_sha3_512(void *restrict out,
                  const void *restrict data,
                  size_t len);
```

One-shot SHA3-512.

**Parameters:**
- `out` - output buffer (64 bytes).
- `data` - pointer to the data to hash.
- `len` - length of the data in bytes.

### SHAKE Extendable-Output Functions

#### `fio_shake128_init`

```c
fio_sha3_s fio_shake128_init(void);
```

**Returns:** a SHAKE128 context (variable output, rate 168).

#### `fio_shake256_init`

```c
fio_sha3_s fio_shake256_init(void);
```

**Returns:** a SHAKE256 context (variable output, rate 136).

#### `fio_shake_consume`

```c
#define fio_shake_consume fio_sha3_consume
```

Alias for `fio_sha3_consume`. Feeds data into a SHAKE context.

#### `fio_shake_squeeze`

```c
void fio_shake_squeeze(fio_sha3_s *restrict h,
                       void *restrict out,
                       size_t outlen);
```

Squeezes `outlen` bytes from a SHAKE context. On the first call it applies padding and runs the first permutation; later calls continue squeezing and re-permute as needed.

**Parameters:**
- `h` - pointer to the SHAKE context.
- `out` - output buffer, must hold at least `outlen` bytes.
- `outlen` - number of bytes to squeeze.

#### `fio_shake128`

```c
void fio_shake128(void *restrict out,
                  size_t outlen,
                  const void *restrict data,
                  size_t len);
```

One-shot SHAKE128.

**Parameters:**
- `out` - output buffer.
- `outlen` - number of bytes to produce.
- `data` - pointer to the data to hash.
- `len` - length of the data in bytes.

#### `fio_shake256`

```c
void fio_shake256(void *restrict out,
                  size_t outlen,
                  const void *restrict data,
                  size_t len);
```

One-shot SHAKE256.

**Parameters:**
- `out` - output buffer.
- `outlen` - number of bytes to produce.
- `data` - pointer to the data to hash.
- `len` - length of the data in bytes.

### Examples

#### One-shot SHA3-256

```c
#define FIO_SHA3
#include "fio-stl.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  const char *msg = "hello";
  uint8_t digest[32];
  fio_sha3_256(digest, msg, strlen(msg));
  for (size_t i = 0; i < sizeof(digest); ++i)
    printf("%02x", digest[i]);
  printf("\n");
  return 0;
}
```

#### Streaming SHA3-512

```c
fio_sha3_s h = fio_sha3_512_init();
fio_sha3_consume(&h, "hello, ", 7);
fio_sha3_consume(&h, "world", 5);
uint8_t digest[64];
fio_sha3_finalize(&h, digest);
```

#### SHAKE256 extendable output

```c
const char *seed = "hello";
uint8_t out[128];
fio_sha3_s h = fio_shake256_init();
fio_shake_consume(&h, seed, strlen(seed));
fio_shake_squeeze(&h, out, sizeof(out));
```

**Note:** state is caller-owned; no heap allocation occurs. Keep SHA-3 and SHAKE contexts separate at finalization time (`fio_sha3_finalize` vs. `fio_shake_squeeze`).

------------------------------------------------------------
# AES-GCM

```c
#define FIO_AES
#include "fio-stl.h"
```

Authenticated encryption with associated data (AEAD) using AES-128-GCM and AES-256-GCM. The API is in-place and matches [`fio_chacha20_poly1305`](./152%20chacha20poly1305.md), so the two cipher suites can be swapped with the same call shape.

Hardware acceleration is used when available (x86 AES-NI + PCLMULQDQ, ARM crypto extensions), with a portable fallback otherwise.

**Security note:** GCM nonces must never be reused with the same key. A 96-bit nonce runs out of uniqueness long before the key does; generate a fresh nonce for every message.

### API Functions

#### `fio_aes128_gcm_enc`

```c
SFUNC void fio_aes128_gcm_enc(void *restrict mac,
                              void *restrict data,
                              size_t len,
                              const void *ad,
                              size_t adlen,
                              const void *key,
                              const void *nonce);
```

Encrypts `data` in place with AES-128-GCM and writes a 16-byte authentication tag to `mac`.

**Parameters:**
- `mac` — output buffer; must have at least 16 writable bytes.
- `data` — plaintext buffer; encrypted in place. May be `NULL` if `len` is 0.
- `len` — plaintext length in bytes.
- `ad` — additional authenticated data; not encrypted but covered by the tag. May be `NULL`.
- `adlen` — length of `ad`.
- `key` — 16-byte AES-128 key.
- `nonce` — 12-byte nonce.

#### `fio_aes128_gcm_dec`

```c
SFUNC int fio_aes128_gcm_dec(void *restrict mac,
                             void *restrict data,
                             size_t len,
                             const void *ad,
                             size_t adlen,
                             const void *key,
                             const void *nonce);
```

Decrypts `data` in place and verifies the 16-byte tag in `mac`.

**Parameters:** same as `fio_aes128_gcm_enc`.

**Returns:** `0` on success, `-1` if authentication fails. On failure `data` is left unchanged.

#### `fio_aes256_gcm_enc`

```c
SFUNC void fio_aes256_gcm_enc(void *restrict mac,
                              void *restrict data,
                              size_t len,
                              const void *ad,
                              size_t adlen,
                              const void *key,
                              const void *nonce);
```

Same as `fio_aes128_gcm_enc` but uses a 32-byte AES-256 key.

#### `fio_aes256_gcm_dec`

```c
SFUNC int fio_aes256_gcm_dec(void *restrict mac,
                             void *restrict data,
                             size_t len,
                             const void *ad,
                             size_t adlen,
                             const void *key,
                             const void *nonce);
```

Same as `fio_aes128_gcm_dec` but uses a 32-byte AES-256 key.

### Example

```c
#define FIO_AES
#include "fio-stl.h"

int main(void) {
  uint8_t key[32] = {0};
  uint8_t nonce[12] = {0};
  uint8_t msg[32] = "hello, gcm world!";
  uint8_t tag[16];

  fio_rand_bytes_secure(key, sizeof(key));
  fio_rand_bytes_secure(nonce, sizeof(nonce));

  fio_aes256_gcm_enc(tag, msg, sizeof(msg), NULL, 0, key, nonce);
  /* msg is now ciphertext; tag authenticates it. */

  if (fio_aes256_gcm_dec(tag, msg, sizeof(msg), NULL, 0, key, nonce)) {
    printf("authentication failed\n");
    return 1;
  }
  /* msg is restored plaintext. */
  return 0;
}
```

------------------------------------------------------------
# Ed25519 & X25519

```c
#define FIO_ED25519
#include "fio-stl.h"
```

Elliptic-curve cryptography over Curve25519: Ed25519 for signatures and X25519 for key exchange. Both provide 128-bit security with compact 32-byte keys.

**Security note:** this implementation has not been independently audited. Use at your own risk, and prefer a tested cryptographic library when available.

### Ed25519 Digital Signatures

- Secret key: 32 bytes
- Public key: 32 bytes
- Signature: 64 bytes

#### `fio_ed25519_keypair`

```c
SFUNC void fio_ed25519_keypair(uint8_t secret_key[32], uint8_t public_key[32]);
```

Generates a new random Ed25519 key pair.

#### `fio_ed25519_public_key`

```c
SFUNC void fio_ed25519_public_key(uint8_t public_key[32],
                                  const uint8_t secret_key[32]);
```

Derives the public key from a secret key.

#### `fio_ed25519_sign`

```c
SFUNC int fio_ed25519_sign(uint8_t signature[64],
                           const void *message,
                           size_t len,
                           const uint8_t secret_key[32],
                           const uint8_t public_key[32]);
```

Signs a message. Returns `0` on success, `-1` on failure. The signature is deterministic.

#### `fio_ed25519_verify`

```c
SFUNC int fio_ed25519_verify(const uint8_t signature[64],
                             const void *message,
                             size_t len,
                             const uint8_t public_key[32]);
```

Verifies a signature. Returns `0` on success, `-1` on failure.

### X25519 Key Exchange

- Secret key: 32 bytes
- Public key: 32 bytes
- Shared secret: 32 bytes

#### `fio_x25519_keypair`

```c
SFUNC void fio_x25519_keypair(uint8_t secret_key[32], uint8_t public_key[32]);
```

Generates a new random X25519 key pair.

#### `fio_x25519_public_key`

```c
SFUNC void fio_x25519_public_key(uint8_t public_key[32],
                                 const uint8_t secret_key[32]);
```

Derives the public key from a secret key.

#### `fio_x25519_shared_secret`

```c
SFUNC int fio_x25519_shared_secret(uint8_t shared_secret[32],
                                   const uint8_t secret_key[32],
                                   const uint8_t their_public_key[32]);
```

Computes a shared secret. Returns `0` on success, `-1` if the result is the all-zero point.

**Note:** pass the shared secret through a KDF such as HKDF before using it as an encryption key.

### Key Conversion

#### `fio_ed25519_sk_to_x25519`

```c
SFUNC void fio_ed25519_sk_to_x25519(uint8_t x_secret_key[32],
                                    const uint8_t ed_secret_key[32]);
```

Converts an Ed25519 secret key to an X25519 secret key.

#### `fio_ed25519_pk_to_x25519`

```c
SFUNC void fio_ed25519_pk_to_x25519(uint8_t x_public_key[32],
                                    const uint8_t ed_public_key[32]);
```

Converts an Ed25519 public key to an X25519 public key.

### ECIES Public-Key Encryption

#### `FIO_X25519_CIPHERTEXT_LEN`

```c
#define FIO_X25519_CIPHERTEXT_LEN(message_len) ((message_len) + 48)
```

Plaintext plus 32-byte ephemeral public key and 16-byte MAC.

#### `FIO_X25519_PLAINTEXT_LEN`

```c
#define FIO_X25519_PLAINTEXT_LEN(ciphertext_len)                               \
  ((ciphertext_len) > 48 ? ((ciphertext_len)-48) : 0)
```

Reverses `FIO_X25519_CIPHERTEXT_LEN`. Returns `0` for invalid ciphertext.

#### `fio_x25519_encrypt`

```c
SFUNC int fio_x25519_encrypt(uint8_t *ciphertext,
                             const void *message,
                             size_t message_len,
                             fio_crypto_enc_fn encryption_function,
                             const uint8_t recipient_pk[32]);
```

Encrypts a message to an X25519 public key. Ciphertext format: `ephemeral_pk || mac || encrypted_data`.

**Parameters:**
- `ciphertext` — output buffer (`message_len + 48` bytes).
- `message` — plaintext.
- `message_len` — plaintext length.
- `encryption_function` — e.g., `fio_chacha20_poly1305_enc`.
- `recipient_pk` — recipient's 32-byte X25519 public key.

**Returns:** `0` on success, `-1` on failure.

#### `fio_x25519_decrypt`

```c
SFUNC int fio_x25519_decrypt(uint8_t *plaintext,
                             const uint8_t *ciphertext,
                             size_t ciphertext_len,
                             fio_crypto_dec_fn decryption_function,
                             const uint8_t recipient_sk[32]);
```

Decrypts a message using the recipient's X25519 secret key.

**Parameters:**
- `plaintext` — output buffer (`ciphertext_len - 48` bytes).
- `ciphertext` — ciphertext from `fio_x25519_encrypt`.
- `ciphertext_len` — total ciphertext length (must be at least 48).
- `decryption_function` — e.g., `fio_chacha20_poly1305_dec`.
- `recipient_sk` — recipient's 32-byte X25519 secret key.

**Returns:** `0` on success, `-1` on failure.

### Example

```c
#define FIO_ED25519
#include "fio-stl.h"

int main(void) {
  uint8_t sk[32], pk[32], sig[64];
  fio_ed25519_keypair(sk, pk);

  const char *msg = "hello, ed25519";
  fio_ed25519_sign(sig, msg, strlen(msg), sk, pk);

  if (fio_ed25519_verify(sig, msg, strlen(msg), pk)) {
    printf("bad signature\n");
    return 1;
  }
  return 0;
}
```

------------------------------------------------------------
# P-256 (secp256r1)

```c
#define FIO_P256
#include "fio-stl.h"
```

P-256 is the NIST `secp256r1` curve used by TLS and certificate tooling. This module covers two jobs: ECDSA signatures over SHA-256 message hashes, and ECDH shared-secret creation.

**Security note:** this implementation has not been independently audited. Use it when the zero-dependency STL path matters; prefer a tested crypto library for long-lived keys, compliance, or high-value trust decisions.

## What It Provides

| Use | Function | Notes |
| --- | --- | --- |
| Sign | `fio_ecdsa_p256_sign` | Creates a DER-encoded ECDSA signature from a 32-byte SHA-256 hash. |
| Verify DER signature | `fio_ecdsa_p256_verify` | Accepts `SEQUENCE { r INTEGER, s INTEGER }` and a 65-byte uncompressed public key. |
| Verify raw signature | `fio_ecdsa_p256_verify_raw` | Accepts raw 32-byte `r`, `s`, `x`, and `y` values. |
| Key pair | `fio_p256_keypair` | Produces a 32-byte secret key and a 65-byte uncompressed public key. |
| Shared secret | `fio_p256_shared_secret` | Accepts compressed or uncompressed peer public keys and returns the x-coordinate. |

The curve parameters are the standard P-256 parameters from NIST FIPS 186-4:

- `p = 2^256 - 2^224 + 2^192 + 2^96 - 1`
- `n = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551`
- curve equation: `y² = x³ - 3x + b (mod p)`

## ECDSA Signatures

### `fio_ecdsa_p256_sign`

```c
SFUNC int fio_ecdsa_p256_sign(uint8_t *sig,
                              size_t *sig_len,
                              size_t sig_capacity,
                              const uint8_t msg_hash[32],
                              const uint8_t secret_key[32]);
```

Signs a 32-byte SHA-256 message hash. The function writes a DER-encoded signature to `sig` and stores the actual length in `sig_len`.

- `sig_capacity` must be at least `72` bytes.
- `secret_key` is a 32-byte big-endian scalar and must be in the valid range `1..n-1`.
- The function generates a random ECDSA nonce internally and retries invalid nonces.
- Returns `0` on success, `-1` on invalid input or signing failure.

It signs the hash you give it; it does not hash the original message. No sneaky kitchen work here.

### `fio_ecdsa_p256_verify`

```c
SFUNC int fio_ecdsa_p256_verify(const uint8_t *sig,
                                size_t sig_len,
                                const uint8_t *msg_hash,
                                const uint8_t *pubkey,
                                size_t pubkey_len);
```

Verifies a DER-encoded ECDSA signature against a 32-byte SHA-256 hash.

- `sig` is a DER `SEQUENCE { r INTEGER, s INTEGER }`.
- `msg_hash` is exactly 32 bytes.
- `pubkey` must be an uncompressed P-256 public key: `0x04 || x || y`.
- `pubkey_len` must be `65`.
- Returns `0` for a valid signature, `-1` for invalid input or a bad signature.

### `fio_ecdsa_p256_verify_raw`

```c
SFUNC int fio_ecdsa_p256_verify_raw(const uint8_t r[32],
                                    const uint8_t s[32],
                                    const uint8_t msg_hash[32],
                                    const uint8_t pubkey_x[32],
                                    const uint8_t pubkey_y[32]);
```

Verifies a signature when `r` and `s` are already decoded. Public-key coordinates are 32-byte big-endian values.

The verifier checks that `r` and `s` are in range, checks the public point is on the P-256 curve, performs the ECDSA verification equation, and returns `0` only when the signature matches.

## ECDH Key Exchange

### `fio_p256_keypair`

```c
SFUNC int fio_p256_keypair(uint8_t secret_key[32], uint8_t public_key[65]);
```

Generates a P-256 key pair.

- `secret_key` receives a 32-byte scalar.
- `public_key` receives `0x04 || x || y` (65 bytes).
- Returns `0` on success, `-1` on bad arguments or random generation failure.

### `fio_p256_shared_secret`

```c
SFUNC int fio_p256_shared_secret(uint8_t shared_secret[32],
                                 const uint8_t secret_key[32],
                                 const uint8_t *their_public_key,
                                 size_t their_public_key_len);
```

Computes a P-256 ECDH shared secret using your secret key and their public key.

- `secret_key` is a 32-byte scalar in the valid range `1..n-1`.
- `their_public_key` may be uncompressed (`65` bytes, `0x04 || x || y`) or compressed (`33` bytes, `0x02/0x03 || x`).
- The peer point is decompressed when needed and checked against the curve.
- `shared_secret` receives the 32-byte x-coordinate of the result.
- Returns `0` on success, `-1` on invalid input, invalid point, point at infinity, or all-zero result.

Run the shared secret through a KDF such as HKDF before using it as an encryption key. Raw ECDH output is an ingredient, not dinner.

## Example

```c
#define FIO_P256
#define FIO_SHA2
#include "fio-stl.h"

int main(void) {
  uint8_t secret_key[32];
  uint8_t public_key[65];
  if (fio_p256_keypair(secret_key, public_key))
    return 1;

  const char *message = "hello, p-256";
  fio_u256 hash = fio_sha256(message, strlen(message));

  uint8_t sig[72];
  size_t sig_len = 0;
  if (fio_ecdsa_p256_sign(sig, &sig_len, sizeof(sig), hash.u8, secret_key))
    return 1;

  if (fio_ecdsa_p256_verify(sig, sig_len, hash.u8, public_key, 65))
    return 1;

  return 0;
}
```

------------------------------------------------------------
# P-384 (secp384r1)

```c
#define FIO_P384
#include "fio-stl.h"
```

P-384 is the NIST `secp384r1` curve. In this STL, it is a verifier: it checks ECDSA P-384 signatures, mainly for TLS 1.3 certificate chains and CA roots that use P-384.

There is no signing or key-generation API here. Just verification. Nice and focused.

**Security note:** this implementation has not been independently audited. It is meant for verification of public data; prefer a tested crypto library when certificate trust is central to your application.

## What It Provides

| Use | Function | Notes |
| --- | --- | --- |
| Verify DER signature | `fio_ecdsa_p384_verify` | Accepts `SEQUENCE { r INTEGER, s INTEGER }` and a 97-byte uncompressed public key. |
| Verify raw signature | `fio_ecdsa_p384_verify_raw` | Accepts raw 48-byte `r`, `s`, `x`, and `y` values. |

Curve parameters from NIST FIPS 186-4:

- `p = 2^384 - 2^128 - 2^96 + 2^32 - 1`
- `n = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973`
- curve equation: `y² = x³ - 3x + b (mod p)`
- coordinate size: 48 bytes

## Verification API

### `fio_ecdsa_p384_verify`

```c
SFUNC int fio_ecdsa_p384_verify(const uint8_t *sig,
                                size_t sig_len,
                                const uint8_t *msg_hash,
                                const uint8_t *pubkey,
                                size_t pubkey_len);
```

Verifies a DER-encoded ECDSA P-384 signature over a 48-byte SHA-384 hash.

- `sig` is a DER `SEQUENCE { r INTEGER, s INTEGER }`.
- `sig_len` is the signature length in bytes.
- `msg_hash` is the SHA-384 hash of the message, exactly 48 bytes.
- `pubkey` must be an uncompressed public key: `0x04 || x || y`.
- `pubkey_len` must be `97`.
- Returns `0` for a valid signature, `-1` for invalid input or a bad signature.

The function validates the public-key wrapper, decodes `r` and `s` from DER, then calls `fio_ecdsa_p384_verify_raw`.

Compressed public keys are not accepted by this API.

### `fio_ecdsa_p384_verify_raw`

```c
SFUNC int fio_ecdsa_p384_verify_raw(const uint8_t r[48],
                                    const uint8_t s[48],
                                    const uint8_t msg_hash[48],
                                    const uint8_t pubkey_x[48],
                                    const uint8_t pubkey_y[48]);
```

Verifies an ECDSA P-384 signature when the signature and public key are already decoded.

All inputs are fixed-width big-endian values:

- `r` and `s`: 48-byte signature scalars.
- `msg_hash`: 48-byte SHA-384 hash.
- `pubkey_x` and `pubkey_y`: 48-byte public-key coordinates.

The verifier checks that `r` and `s` are in range, checks the public point is on the P-384 curve, computes the ECDSA verification equation, and returns `0` only when `r == R.x mod n`.

## Example

```c
#define FIO_P384
#include "fio-stl.h"

int verify_cert_sig(const uint8_t *sig,
                    size_t sig_len,
                    const uint8_t hash[48],
                    const uint8_t pubkey[97]) {
  if (fio_ecdsa_p384_verify(sig, sig_len, hash, pubkey, 97))
    return -1;
  return 0;
}
```

## Implementation Notes

The implementation uses 6 × 64-bit limbs for field elements, Jacobian coordinates for point work, P-384-specific modular reduction, and a double-scalar multiplication path for ECDSA verification.

The scalar multiplication is for public verification data. Do not treat this header as a P-384 signing toolkit; there is no public signing API, and private-key use should go to an audited library.

------------------------------------------------------------
# ASN.1 DER

```c
#define FIO_DER
#include "fio-stl.h"
```

ASN.1 DER is the small binary grammar hiding inside X.509 certificates, keys, signatures, and TLS messages. This module parses and writes the pieces facil.io needs for that world.

The parser is non-allocating. Parsed elements point into the original DER buffer, so keep that buffer alive while you use the results. No buffer, no treasure map.

## Core Types

### `fio_der_element_s`

```c
typedef struct {
  const uint8_t *data;
  size_t len;
  uint8_t tag;
  uint8_t is_constructed;
  uint8_t tag_class;
  uint8_t tag_number;
} fio_der_element_s;
```

A parsed DER element. `data` points to the content bytes, after the tag and length fields. `len` is the content length. The remaining fields describe the tag byte.

### `fio_der_iterator_s`

```c
typedef struct {
  const uint8_t *pos;
  const uint8_t *end;
} fio_der_iterator_s;
```

Iterator state for walking a `SEQUENCE` or `SET` element.

### Tags and Classes

`fio_der_tag_e` defines the common universal tags, including `FIO_DER_INTEGER`, `FIO_DER_BIT_STRING`, `FIO_DER_OID`, `FIO_DER_SEQUENCE`, `FIO_DER_SET`, string types, `FIO_DER_UTC_TIME`, `FIO_DER_GENERALIZED_TIME`, and context wrappers `FIO_DER_CONTEXT_0` through `FIO_DER_CONTEXT_3`.

`fio_der_class_e` names the tag classes: universal, application, context-specific, and private.

## OID Values

OID constants live in the X.509 module (`FIO_X509_OID_*`, see the X.509
documentation). An OID value is a plain `fio_u128` where bytes 0-14 hold the
DER content bytes (zero-padded) and byte 15 holds the content length. The
length is folded in because `0x00` is a legal OID content byte (arc 0), so
padding alone could not distinguish `{..0B}` from `{..0B 00}` - different
OIDs. Oversized OIDs (content > 15 bytes) simply never match, which is safe:
AlgorithmIdentifier OIDs are attacker-controlled and RFC 5280 requires exact
match.

Build a value once per parsed element with `fio___der_oid_value` (internal),
then compare it against any number of constants with `fio___der_oid_eq`
(internal) - two u64-lane `==` comparisons, no memcmp, no call:

```c
fio_u128 oid = fio___der_oid_value(&elem); /* build ONCE per element */
if (fio___der_oid_eq(oid, FIO_X509_OID_COMMON_NAME)) { /* ... */ }
```

## Parsing

### `fio_der_parse`

```c
SFUNC const uint8_t *fio_der_parse(fio_der_element_s *elem,
                                    const uint8_t *data,
                                    size_t data_len);
```

Parses one DER element. On success, fills `elem` and returns a pointer to the next element. On error, returns `NULL`.

It rejects invalid DER lengths, truncated buffers, unsupported high-tag-number encodings, and indefinite lengths.

### `fio_der_element_total_len`

```c
FIO_IFUNC size_t fio_der_element_total_len(const fio_der_element_s *elem,
                                            const uint8_t *data);
```

Returns the full encoded length of an element: tag + length + content. `data` must be the original pointer used to parse the element.

## Type Parsers

### `fio_der_parse_integer`

```c
SFUNC int fio_der_parse_integer(const fio_der_element_s *elem,
                                 uint64_t *value);
```

Parses an ASN.1 `INTEGER`. For small integers, pass `value` and receive the 64-bit value. For large integers such as RSA moduli, pass `NULL` and use `elem->data` / `elem->len` directly. Leading zero bytes for positive integers are handled.

Returns `0` on success, `-1` on error.

### `fio_der_parse_bit_string`

```c
SFUNC int fio_der_parse_bit_string(const fio_der_element_s *elem,
                                    const uint8_t **bits,
                                    size_t *bit_len,
                                    uint8_t *unused_bits);
```

Parses an ASN.1 `BIT STRING`. `bits` points into the element data after the unused-bit count byte. `bit_len` is the byte length of the bit payload. `unused_bits` is the number of unused bits in the last byte.

### `fio_der_parse_oid`

```c
SFUNC int fio_der_parse_oid(const fio_der_element_s *elem,
                             char *buf,
                             size_t buf_len);
```

Converts an ASN.1 `OBJECT IDENTIFIER` into dotted text such as `1.2.840.113549.1.1.11`.

Returns the number of characters written, excluding the NUL byte, or `-1` on error.

### `fio_der_parse_time`

```c
SFUNC int fio_der_parse_time(const fio_der_element_s *elem,
                              int64_t *unix_time);
```

Parses `UTCTime` or `GeneralizedTime` into a Unix timestamp. Times must be UTC (`Z`). Fractional seconds are skipped when present.

### `fio_der_parse_string`

```c
FIO_IFUNC const char *fio_der_parse_string(const fio_der_element_s *elem,
                                            size_t *len);
```

Returns a pointer to the string bytes for supported ASN.1 string tags, with the byte length in `len`. This does not validate or transcode the text; it simply gives you the payload.

### `fio_der_parse_boolean`

```c
FIO_IFUNC int fio_der_parse_boolean(const fio_der_element_s *elem,
                                     int *value);
```

Parses a DER boolean and writes `0` or non-zero to `value`.

## Walking Sequences and Sets

### `fio_der_iterator_init`

```c
FIO_IFUNC void fio_der_iterator_init(fio_der_iterator_s *it,
                                      const fio_der_element_s *sequence);
```

Initializes an iterator over the content bytes of a parsed `SEQUENCE` or `SET`.

### `fio_der_iterator_next`

```c
SFUNC int fio_der_iterator_next(fio_der_iterator_s *it,
                                 fio_der_element_s *elem);
```

Parses the next child element and advances the iterator. Returns `0` when an element was read, `-1` at end or on parse error.

### `fio_der_iterator_has_next`

```c
FIO_IFUNC int fio_der_iterator_has_next(const fio_der_iterator_s *it);
```

Returns `1` when the iterator still has bytes to parse, otherwise `0`.

## Tag Helpers

```c
FIO_IFUNC int fio_der_is_tag(const fio_der_element_s *elem, uint8_t tag);
FIO_IFUNC int fio_der_is_context_tag(const fio_der_element_s *elem,
                                      uint8_t tag_num);
FIO_IFUNC uint8_t fio_der_tag_number(const fio_der_element_s *elem);
```

Use these to keep parser code readable:

- `fio_der_is_tag` checks a universal tag such as `FIO_DER_INTEGER`.
- `fio_der_is_context_tag` checks `[0]`, `[1]`, and friends.
- `fio_der_tag_number` returns the decoded tag number.

## Encoding

All encoder functions return the number of bytes written or needed. Pass `NULL` as `buf` to calculate the encoded length before writing.

### Basic Encoders

```c
SFUNC size_t fio_der_encode_length(uint8_t *buf, size_t len);
SFUNC size_t fio_der_encode_integer(uint8_t *buf,
                                     const uint8_t *data,
                                     size_t data_len);
SFUNC size_t fio_der_encode_integer_small(uint8_t *buf, uint64_t value);
SFUNC size_t fio_der_encode_null(uint8_t *buf);
SFUNC size_t fio_der_encode_boolean(uint8_t *buf, int value);
```

These write DER length fields, positive integers, `NULL`, and booleans. `fio_der_encode_integer` expects big-endian integer bytes and adds the leading zero byte when DER needs one to keep the integer positive.

OIDs are encoded with the internal `fio___der_encode_oid(buf, fio_u128 oid)`,
which TLV-wraps an OID value (see OID Values above) without any dot-string
parsing.

### String and Byte Encoders

```c
SFUNC size_t fio_der_encode_utf8_string(uint8_t *buf,
                                         const char *str,
                                         size_t str_len);
SFUNC size_t fio_der_encode_printable_string(uint8_t *buf,
                                              const char *str,
                                              size_t str_len);
SFUNC size_t fio_der_encode_bit_string(uint8_t *buf,
                                        const uint8_t *bits,
                                        size_t bit_len,
                                        uint8_t unused_bits);
SFUNC size_t fio_der_encode_octet_string(uint8_t *buf,
                                          const uint8_t *data,
                                          size_t data_len);
```

These write the tag, DER length, and payload for common primitive values.

### Header Encoders

```c
SFUNC size_t fio_der_encode_sequence_header(uint8_t *buf, size_t content_len);
SFUNC size_t fio_der_encode_set_header(uint8_t *buf, size_t content_len);
SFUNC size_t fio_der_encode_context_header(uint8_t *buf,
                                            uint8_t tag_num,
                                            size_t content_len,
                                            int constructed);
```

These write only the wrapper tag and length. Write the content bytes immediately after the returned header length.

### Time Encoders

```c
SFUNC size_t fio_der_encode_utc_time(uint8_t *buf, int64_t unix_time);
SFUNC size_t fio_der_encode_generalized_time(uint8_t *buf, int64_t unix_time);
```

These encode Unix timestamps as DER `UTCTime` or `GeneralizedTime`.

## Example

```c
#define FIO_DER
#include "fio-stl.h"

void scan_sequence(const uint8_t *der, size_t der_len) {
  fio_der_element_s seq;
  if (!fio_der_parse(&seq, der, der_len))
    return;
  if (!fio_der_is_tag(&seq, FIO_DER_SEQUENCE))
    return;

  fio_der_iterator_s it;
  fio_der_iterator_init(&it, &seq);

  fio_der_element_s elem;
  while (fio_der_iterator_next(&it, &elem) == 0) {
    if (fio_der_is_tag(&elem, FIO_DER_OID)) {
      fio_u128 oid = fio___der_oid_value(&elem);
      /* compare with FIO_X509_OID_* constants via fio___der_oid_eq, or */
      char dot[128];
      if (fio_der_parse_oid(&elem, dot, sizeof(dot)) > 0) {
        /* dotted string available for diagnostics */
      }
    }
  }
}
```

------------------------------------------------------------
# RSA Signatures

```c
#define FIO_RSA
#include "fio-stl.h"
```

RSA here means signature work for TLS and X.509: verify PKCS#1 v1.5 signatures, verify RSA-PSS signatures, and create RSA-PSS signatures for TLS 1.3 `CertificateVerify`.

Supported modulus sizes are 2048, 3072, and 4096 bits. Smaller keys do not get a party hat.

**Security note:** this implementation has not been independently audited. Prefer a tested crypto library for high-value private keys, compliance, HSM integration, or anything that keeps auditors awake.

## Constants and Hash IDs

```c
#define FIO_RSA_MAX_BITS  4096
#define FIO_RSA_MAX_BYTES (FIO_RSA_MAX_BITS / 8)
#define FIO_RSA_MAX_WORDS (FIO_RSA_MAX_BYTES / 8)
```

```c
typedef enum {
  FIO_RSA_HASH_SHA256 = 0,
  FIO_RSA_HASH_SHA384 = 1,
  FIO_RSA_HASH_SHA512 = 2,
} fio_rsa_hash_e;
```

`hash_len` must match the selected hash: 32 bytes for SHA-256, 48 for SHA-384, and 64 for SHA-512.

## Key Types

### `fio_rsa_pubkey_s`

```c
typedef struct {
  const uint8_t *n;
  size_t n_len;
  const uint8_t *e;
  size_t e_len;
} fio_rsa_pubkey_s;
```

Public key for verification. `n` is the modulus and `e` is the public exponent, both as big-endian byte arrays. This matches the DER layout used in X.509 certificates.

### `fio_rsa_privkey_s`

```c
typedef struct {
  const uint8_t *n; size_t n_len;
  const uint8_t *d; size_t d_len;
  const uint8_t *e; size_t e_len;
  const uint8_t *p; size_t p_len;
  const uint8_t *q; size_t q_len;
  const uint8_t *dP; size_t dP_len;
  const uint8_t *dQ; size_t dQ_len;
  const uint8_t *qInv; size_t qInv_len;
} fio_rsa_privkey_s;
```

Private key for RSA-PSS signing. `n` and `d` are required. The public exponent `e` and CRT fields (`p`, `q`, `dP`, `dQ`, `qInv`) are optional.

When CRT parameters are available, signing uses CRT with message blinding. If only `n` and `d` are available, signing falls back to the non-CRT path.

## Verification

### `fio_rsa_verify_pkcs1`

```c
SFUNC int fio_rsa_verify_pkcs1(const uint8_t *sig,
                               size_t sig_len,
                               const uint8_t *msg_hash,
                               size_t hash_len,
                               fio_rsa_hash_e hash_alg,
                               const fio_rsa_pubkey_s *key);
```

Verifies an RSA PKCS#1 v1.5 signature with `DigestInfo` encoding.

This covers the X.509 signature algorithms commonly named:

- `sha256WithRSAEncryption`
- `sha384WithRSAEncryption`
- `sha512WithRSAEncryption`

Parameters:

- `sig`: signature bytes; length must match the modulus length.
- `sig_len`: signature length in bytes.
- `msg_hash`: pre-computed message hash.
- `hash_len`: 32, 48, or 64 bytes.
- `hash_alg`: matching `FIO_RSA_HASH_*` value.
- `key`: RSA public key.

Returns `0` for a valid signature, `-1` on invalid input or a bad signature.

### `fio_rsa_verify_pss`

```c
SFUNC int fio_rsa_verify_pss(const uint8_t *sig,
                             size_t sig_len,
                             const uint8_t *msg_hash,
                             size_t hash_len,
                             fio_rsa_hash_e hash_alg,
                             const fio_rsa_pubkey_s *key);
```

Verifies an RSA-PSS signature. This is the RSA signature style TLS 1.3 uses for `CertificateVerify`.

The implementation uses:

- MGF1 with the same hash function;
- salt length equal to the hash length;
- trailer byte `0xBC`.

Returns `0` for a valid signature, `-1` on invalid input or a bad signature.

## Signing

### `fio_rsa_sign_pss`

```c
SFUNC int fio_rsa_sign_pss(uint8_t *signature,
                           size_t *sig_len,
                           const uint8_t *msg_hash,
                           size_t hash_len,
                           fio_rsa_hash_e hash_alg,
                           const fio_rsa_privkey_s *key);
```

Creates an RSA-PSS signature using `RSASSA-PSS-SIGN` from RFC 8017, with the TLS 1.3 settings listed above.

- `signature` must have room for `key->n_len` bytes.
- `sig_len` receives the signature length, which equals `key->n_len` on success.
- `msg_hash` is already hashed; this function does not hash the message.
- `key` must include `n` and `d`; CRT fields are optional.

Returns `0` on success, `-1` on error.

PKCS#1 v1.5 signing is not exposed. TLS 1.3 does not want it for `CertificateVerify`, and this header agrees.

## Example: Verify an RSA-PSS Signature

```c
#define FIO_RSA
#define FIO_SHA2
#include "fio-stl.h"

int verify_pss(const uint8_t *sig,
               size_t sig_len,
               const uint8_t *modulus,
               size_t modulus_len,
               const uint8_t *exponent,
               size_t exponent_len,
               const void *message,
               size_t message_len) {
  fio_rsa_pubkey_s key = {
      .n = modulus,
      .n_len = modulus_len,
      .e = exponent,
      .e_len = exponent_len,
  };

  fio_u256 hash = fio_sha256(message, message_len);
  return fio_rsa_verify_pss(sig, sig_len, hash.u8, 32,
                            FIO_RSA_HASH_SHA256, &key);
}
```

## Practical Notes

- The signature length must match the modulus length.
- The public exponent is usually `65537` (`0x010001`), but the API accepts DER-style big-endian bytes.
- The module works on pre-computed hashes. Pick the hash to match the certificate or protocol signature algorithm.
- For X.509 parsing, this module is normally used through [X.509](./155%20x509.md), which extracts the RSA key and dispatches verification.

------------------------------------------------------------
# ML-KEM-768

```c
#define FIO_MLKEM
#include "fio-stl.h"
```

ML-KEM-768 is the FIPS 203 module-lattice key encapsulation mechanism: a post-quantum way to agree on a shared secret. This header also exposes the TLS hybrid `X25519MLKEM768` shape, which combines ML-KEM-768 with X25519.

**Security note:** this implementation has not been independently audited. Use it carefully, and prefer audited crypto stacks for high-value deployments.

## ML-KEM-768 Sizes

```c
#define FIO_MLKEM768_PUBLICKEYBYTES  1184
#define FIO_MLKEM768_SECRETKEYBYTES  2400
#define FIO_MLKEM768_CIPHERTEXTBYTES 1088
#define FIO_MLKEM768_SSBYTES         32
#define FIO_MLKEM768_SYMBYTES        32
```

Parameters: `n = 256`, `k = 3`, `q = 3329`, `eta1 = 2`, `eta2 = 2`, `d_u = 10`, `d_v = 4`.

## ML-KEM-768 API

### `fio_mlkem768_keypair`

```c
SFUNC int fio_mlkem768_keypair(uint8_t pk[1184], uint8_t sk[2400]);
```

Generates a random ML-KEM-768 key pair using the system CSPRNG. Returns `0` on success, `-1` on failure.

### `fio_mlkem768_keypair_derand`

```c
SFUNC int fio_mlkem768_keypair_derand(uint8_t pk[1184],
                                      uint8_t sk[2400],
                                      const uint8_t coins[64]);
```

Generates a deterministic key pair. `coins` is exactly 64 bytes: `d || z`. This is mostly for test vectors and reproducible checks; production code should normally use `fio_mlkem768_keypair`.

### `fio_mlkem768_encaps`

```c
SFUNC int fio_mlkem768_encaps(uint8_t ct[1088],
                              uint8_t ss[32],
                              const uint8_t pk[1184]);
```

Creates a ciphertext and shared secret for the holder of `pk`. Randomness comes from the system CSPRNG. Send `ct`; keep `ss` as the shared secret input to your KDF or protocol.

### `fio_mlkem768_encaps_derand`

```c
SFUNC int fio_mlkem768_encaps_derand(uint8_t ct[1088],
                                     uint8_t ss[32],
                                     const uint8_t pk[1184],
                                     const uint8_t coins[32]);
```

Deterministic encapsulation using 32 bytes of caller-provided randomness. Useful for known-answer tests. Boring in production, which is exactly the point.

### `fio_mlkem768_decaps`

```c
SFUNC int fio_mlkem768_decaps(uint8_t ss[32],
                              const uint8_t ct[1088],
                              const uint8_t sk[2400]);
```

Recovers the shared secret from a ciphertext and secret key.

ML-KEM uses implicit rejection: invalid ciphertexts produce a pseudorandom shared secret derived from the secret key and ciphertext instead of a loud failure. That prevents chosen-ciphertext games from turning your error path into an oracle.

Returns `0` for well-formed inputs.

## X25519MLKEM768 Hybrid

`X25519MLKEM768` is the TLS 1.3 hybrid key exchange shape from the ECDHE/ML-KEM draft. It needs `FIO_ED25519` too, because this STL implements X25519 in the Ed25519/Curve25519 header.

The name starts with X25519, but the bytes start with ML-KEM. Yes, naming is hard.

```c
#define FIO_X25519MLKEM768_PUBLICKEYBYTES  (32 + 1184) /* 1216 */
#define FIO_X25519MLKEM768_SECRETKEYBYTES  (32 + 2400) /* 2432 */
#define FIO_X25519MLKEM768_CIPHERTEXTBYTES (32 + 1088) /* 1120 */
#define FIO_X25519MLKEM768_SSBYTES         64
```

| Value | Layout |
| --- | --- |
| Public key | `ML-KEM-768_ek` (1184) `||` `X25519_pk` (32) |
| Secret key | `ML-KEM-768_dk` (2400) `||` `X25519_sk` (32) |
| Ciphertext | `ML-KEM-768_ct` (1088) `||` `X25519_ephemeral_pk` (32) |
| Shared secret | `ML-KEM-768_ss` (32) `||` `X25519_ss` (32) |

### `fio_x25519mlkem768_keypair`

```c
SFUNC int fio_x25519mlkem768_keypair(uint8_t pk[1216], uint8_t sk[2432]);
```

Generates both the ML-KEM-768 and X25519 key pairs with system randomness. Returns `0` on success.

### `fio_x25519mlkem768_encaps`

```c
SFUNC int fio_x25519mlkem768_encaps(uint8_t ct[1120],
                                    uint8_t ss[64],
                                    const uint8_t pk[1216]);
```

Performs ML-KEM encapsulation and X25519 ephemeral key exchange. The returned shared secret is the concatenation `ML-KEM-768_ss || X25519_ss`.

### `fio_x25519mlkem768_decaps`

```c
SFUNC int fio_x25519mlkem768_decaps(uint8_t ss[64],
                                    const uint8_t ct[1120],
                                    const uint8_t sk[2432]);
```

Decapsulates the ML-KEM part and computes the X25519 shared secret. Returns `0` on success, or `-1` if X25519 rejects the peer point. ML-KEM invalid ciphertexts still use implicit rejection.

## Example

```c
#define FIO_MLKEM
#include "fio-stl.h"

int roundtrip(void) {
  uint8_t pk[FIO_MLKEM768_PUBLICKEYBYTES];
  uint8_t sk[FIO_MLKEM768_SECRETKEYBYTES];
  uint8_t ct[FIO_MLKEM768_CIPHERTEXTBYTES];
  uint8_t sender_ss[FIO_MLKEM768_SSBYTES];
  uint8_t receiver_ss[FIO_MLKEM768_SSBYTES];

  if (fio_mlkem768_keypair(pk, sk))
    return -1;
  if (fio_mlkem768_encaps(ct, sender_ss, pk))
    return -1;
  if (fio_mlkem768_decaps(receiver_ss, ct, sk))
    return -1;

  return fio_memcmp(sender_ss, receiver_ss, sizeof(sender_ss));
}
```

## Implementation Notes

The implementation uses ML-KEM-768 parameters, NTT arithmetic, Montgomery and Barrett reduction, and optional NEON / AVX2 paths for vectorized polynomial work. Those SIMD paths are speed knobs, not different APIs.

------------------------------------------------------------
# X.509 Certificates

```c
#define FIO_X509
#include "fio-stl.h"
```

This module parses DER-encoded X.509v3 certificates for TLS 1.3 style certificate checks. It is a compact parser, verifier, trust-store checker, TLS Certificate-message splitter, and small self-signed certificate generator.

It is non-allocating while parsing: `fio_x509_cert_s` stores pointers into the original DER bytes. Keep those bytes alive. Certificates dislike disappearing floors.

**Scope note:** this is a minimal TLS-oriented X.509 implementation, not a full PKI policy engine. Not all X.509 features are supported.

## Supported Pieces

- RSA, ECDSA P-256, ECDSA P-384, and Ed25519 public keys.
- RSA PKCS#1 v1.5, RSA-PSS, ECDSA, and Ed25519 signature verification.
- Validity period checks.
- Hostname matching using SAN DNS names and CN fallback, with one-label wildcards.
- Basic Constraints and Key Usage.
- Certificate chain validation against an optional trust store.
- TLS 1.3 Certificate message parsing.
- Self-signed certificate generation for Ed25519 and P-256 keys.

## Main Types

### Key and Signature Enums

```c
typedef enum {
  FIO_X509_KEY_UNKNOWN = 0,
  FIO_X509_KEY_RSA = 1,
  FIO_X509_KEY_ECDSA_P256 = 2,
  FIO_X509_KEY_ECDSA_P384 = 3,
  FIO_X509_KEY_ED25519 = 4,
} fio_x509_key_algo_e;
```

```c
typedef enum {
  FIO_X509_SIGNATURE_UNKNOWN = 0,
  FIO_X509_SIGNATURE_RSA_PKCS1_SHA256 = 1,
  FIO_X509_SIGNATURE_RSA_PKCS1_SHA384 = 2,
  FIO_X509_SIGNATURE_RSA_PKCS1_SHA512 = 3,
  FIO_X509_SIGNATURE_RSA_PSS_SHA256 = 4,
  FIO_X509_SIGNATURE_RSA_PSS_SHA384 = 5,
  FIO_X509_SIGNATURE_RSA_PSS_SHA512 = 6,
  FIO_X509_SIGNATURE_ECDSA_SHA256 = 7,
  FIO_X509_SIGNATURE_ECDSA_SHA384 = 8,
  FIO_X509_SIGNATURE_ED25519 = 9,
} fio_x509_signature_algo_e;
```

### Key Usage and Errors

`fio_x509_key_usage_e` defines RFC 5280 key-usage bits such as `FIO_X509_KU_DIGITAL_SIGNATURE`, `FIO_X509_KU_KEY_ENCIPHERMENT`, and `FIO_X509_KU_KEY_CERT_SIGN`. ASN.1 bit strings use MSB-first ordering, so the constants look a little backwards until they save you from off-by-one sadness.

`fio_x509_error_e` gives chain validation results: `FIO_X509_OK`, parse failure, expired/not-yet-valid, signature failure, issuer mismatch, not-a-CA, no trust anchor, hostname mismatch, empty chain, and chain-too-long.

### `fio_x509_trust_store_s`

```c
typedef struct {
  const uint8_t **roots;
  const size_t *root_lens;
  size_t root_count;
} fio_x509_trust_store_s;
```

A simple root store: arrays of DER certificate pointers and lengths.

### `fio_tls_cert_entry_s`

```c
typedef struct {
  const uint8_t *cert;
  size_t cert_len;
} fio_tls_cert_entry_s;
```

One certificate entry extracted from a TLS 1.3 `Certificate` handshake message.

### `fio_x509_cert_s`

```c
typedef struct fio_x509_cert_s fio_x509_cert_s;
```

The parsed certificate structure uses non-owning buffer views
(`fio_buf_info_s` / `fio_ubuf_info_s`) and packs to 256 bytes with no
interior padding:

- `verified`: non-zero when a TLS backend verified this certificate's chain
  (peer inspection only; always zero after `fio_x509_parse`);
- `der`: a reference to the original DER bytes (not a copy);
- `fingerprint`: 32-byte SHA-256 of the DER bytes (see `fio_x509_fingerprint`);
- `version`, `serial`, and validity timestamps;
- raw subject and issuer DNs;
- subject CN;
- public key type and key data;
- signature algorithm and signature bytes;
- TBS certificate bytes used for verification;
- Basic Constraints (`is_ca`), Key Usage (`has_key_usage`, `key_usage`), and
  SAN data (first DNS name, first IP address, and the raw SAN extension for
  iterating the rest).

All view pointers reference the original DER buffer.

## Parsing and Single-Certificate Checks

### `fio_x509_parse`

```c
SFUNC int fio_x509_parse(fio_x509_cert_s *cert,
                         const uint8_t *der_data,
                         size_t der_len);
```

Parses one DER certificate. `cert` is zeroed first. Returns `0` on success, `-1` on parse error.

### `fio_x509_fingerprint`

```c
SFUNC void fio_x509_fingerprint(fio_x509_cert_s *cert);
```

Computes the SHA-256 of the certificate's DER bytes into
`cert->fingerprint` (32 raw bytes). Call after `fio_x509_parse`; hashing is
lazy so parsing never pays for it. Requires the SHA2 module.

### `fio_x509_verify_signature`

```c
SFUNC int fio_x509_verify_signature(const fio_x509_cert_s *cert,
                                    const fio_x509_cert_s *issuer);
```

Verifies that `issuer` signed `cert`, using the issuer public key and the certificate signature algorithm. Returns `0` when the signature is valid, `-1` otherwise.

### `fio_x509_check_validity`

```c
FIO_IFUNC int fio_x509_check_validity(const fio_x509_cert_s *cert,
                                      int64_t current_time);
```

Checks `not_before <= current_time <= not_after`. Returns `0` when the certificate is in its validity window.

### `fio_x509_match_hostname`

```c
SFUNC int fio_x509_match_hostname(const fio_x509_cert_s *cert,
                                  const char *hostname,
                                  size_t hostname_len);
```

Checks whether a certificate matches a hostname. SAN DNS entries are supported, with CN fallback. Wildcards are one-label wildcards such as `*.example.com`; they do not swallow `a.b.example.com`.

Returns `0` for a match, `-1` for no match.

Distinguished Names are compared as raw DER with the core
`FIO_BUF_INFO_IS_EQ` macro (e.g.,
`FIO_BUF_INFO_IS_EQ(cert.issuer, root.subject)`).

## Chain Validation

### `fio_x509_verify_chain`

```c
SFUNC int fio_x509_verify_chain(const uint8_t **certs,
                                const size_t *cert_lens,
                                size_t cert_count,
                                const char *hostname,
                                int64_t current_time,
                                fio_x509_trust_store_s *trust_store);
```

Validates a certificate chain for TLS-style use.

Expected order:

1. `certs[0]`: end-entity / server certificate.
2. `certs[1]`: intermediate that signed `certs[0]`.
3. `certs[n-1]`: closest-to-root certificate.

Validation parses all certificates, checks validity periods, optionally matches the hostname, verifies each signature with the next certificate, checks issuer/subject DN links, requires CA certificates where needed, and optionally checks the final certificate against `trust_store`.

Trust anchors are matched by (subject DN, public key): the anchor's key must
verify the final certificate's signature. This also covers self-signed
certificates — a same-named impostor with a different key is rejected.

Returns `FIO_X509_OK` (`0`) on success, or a `FIO_X509_ERR_*` code.

### `fio_x509_is_trusted`

```c
SFUNC int fio_x509_is_trusted(const fio_x509_cert_s *cert,
                              fio_x509_trust_store_s *trust_store);
```

Checks whether `cert` appears in the trust store by subject DN match. Returns `0` if trusted, `-1` if not found.

### `fio_x509_error_str`

```c
FIO_IFUNC const char *fio_x509_error_str(int error);
```

Returns a static string for a chain validation error code.

## TLS Certificate Messages

```c
#define FIO_TLS_CERT_PARSE_ERROR ((size_t)-1)
```

### `fio_tls_parse_certificate_message`

```c
SFUNC size_t fio_tls_parse_certificate_message(fio_tls_cert_entry_s *entries,
                                               size_t max_entries,
                                               const uint8_t *data,
                                               size_t data_len);
```

Parses a TLS 1.3 `Certificate` message body, after the handshake header. It extracts certificate entries and skips per-certificate extensions.

Returns the number of certificates parsed, or `FIO_TLS_CERT_PARSE_ERROR` on malformed input.

## Self-Signed Certificate Generation

Generation supports Ed25519 and P-256 key pairs.

```c
typedef enum {
  FIO_X509_KEYPAIR_ED25519 = 1,
  FIO_X509_KEYPAIR_P256 = 2,
} fio_x509_keypair_type_e;
```

`fio_x509_keypair_s` stores the selected key type, secret key bytes, public key bytes, and their lengths. Use `fio_x509_keypair_clear` when done.

`fio_x509_cert_options_s` controls the subject CN, organization, organizational unit, country, validity window, SAN DNS names, CA flag, and key-usage bits.

### `fio_x509_keypair_ed25519`

```c
SFUNC int fio_x509_keypair_ed25519(fio_x509_keypair_s *keypair);
```

Generates an Ed25519 key pair for certificate signing. Requires the Ed25519 module to be included. Returns `0` on success.

### `fio_x509_keypair_p256`

```c
SFUNC int fio_x509_keypair_p256(fio_x509_keypair_s *keypair);
```

Generates a P-256 key pair for certificate signing. Requires the P-256 module to be included. Returns `0` on success.

### `fio_x509_self_signed_cert`

```c
SFUNC size_t fio_x509_self_signed_cert(uint8_t *buf,
                                       size_t buf_len,
                                       const fio_x509_keypair_s *keypair,
                                       const fio_x509_cert_options_s *options);
```

Writes a DER-encoded self-signed X.509v3 certificate. Call with `buf == NULL` to get a worst-case buffer size, then call again with a real buffer.

Returns bytes written, or `0` on error.

### `fio_x509_keypair_clear`

```c
FIO_IFUNC void fio_x509_keypair_clear(fio_x509_keypair_s *keypair);
```

Securely clears key material and zeros the structure.

## Example: Parse and Check Hostname

```c
#define FIO_X509
#include "fio-stl.h"

int check_cert(const uint8_t *der, size_t der_len, int64_t now) {
  fio_x509_cert_s cert;
  if (fio_x509_parse(&cert, der, der_len))
    return -1;
  if (fio_x509_check_validity(&cert, now))
    return -1;
  if (fio_x509_match_hostname(&cert, "example.com", 11))
    return -1;
  return 0;
}
```

## Practical Notes

- Define the crypto modules you need before including the STL: RSA, P-256, P-384, Ed25519, SHA-2, and ASN.1 may all matter depending on certificate algorithms.
- `trust_store == NULL` skips root trust checking in `fio_x509_verify_chain`; useful for structural tests, not for real trust decisions.
- Hostname checks are DNS-name focused. IP address SAN handling is not a full replacement for platform certificate validation.
- Parsed strings and DNs are raw certificate data. Normalize policy elsewhere if your application needs browser-grade PKI behavior.

------------------------------------------------------------
# PEM Parser

```c
#define FIO_PEM
#include "fio-stl.h"
```

PEM wraps DER bytes in Base64 armor:

```text
-----BEGIN <label>-----
<base64 DER>
-----END <label>-----
```

This module extracts PEM blocks, decodes certificates, and parses private keys used by the TLS/X.509 helpers.

**Limit:** encrypted private keys are not supported. If the key asks for a password, this parser politely leaves the room.

## Supported Labels

- `CERTIFICATE`
- `PRIVATE KEY` (PKCS#8)
- `RSA PRIVATE KEY` (legacy PKCS#1)
- `EC PRIVATE KEY` (legacy SEC1)

Private-key parsing supports RSA, P-256, and Ed25519 where the required crypto modules are available.

## Types

### `fio_pem_key_type_e`

```c
typedef enum {
  FIO_PEM_KEY_UNKNOWN = 0,
  FIO_PEM_KEY_RSA = 1,
  FIO_PEM_KEY_ECDSA_P256 = 2,
  FIO_PEM_KEY_ED25519 = 3,
} fio_pem_key_type_e;
```

### `fio_pem_s`

```c
typedef struct {
  const uint8_t *der;
  size_t der_len;
  const char *label;
  size_t label_len;
} fio_pem_s;
```

A decoded PEM block. `der` points into the caller-provided DER output buffer. `label` points into the original PEM input.

### `fio_pem_private_key_s`

```c
typedef struct fio_pem_private_key_s fio_pem_private_key_s;
```

Parsed private-key material. The active union member depends on `type`:

- RSA: fixed-size byte arrays for `n`, `e`, `d`, optional `p`, `q`, `dP`, `dQ`, and `qInv`, each with a length.
- P-256: 32-byte private scalar, optional 65-byte uncompressed public key.
- Ed25519: 32-byte seed, optional 32-byte public key.

Call `fio_pem_private_key_clear` when done.

## API

### `fio_pem_parse`

```c
SFUNC size_t fio_pem_parse(fio_pem_s *out,
                           uint8_t *der_buf,
                           size_t der_buf_len,
                           const char *pem_data,
                           size_t pem_len);
```

Finds the next PEM block, decodes its Base64 body into `der_buf`, and fills `out`.

Returns the number of bytes consumed from `pem_data`, or `0` on error. To parse a file with multiple PEM blocks, advance by the returned count and call again.

### `fio_pem_parse_certificate`

```c
SFUNC int fio_pem_parse_certificate(fio_x509_cert_s *cert,
                                    const char *pem_data,
                                    size_t pem_len);
```

Finds a `CERTIFICATE` block, decodes it, and parses the DER certificate with the X.509 parser. Returns `0` on success, `-1` on error.

Requires the X.509 module for the certificate parse.

### `fio_pem_parse_private_key`

```c
SFUNC int fio_pem_parse_private_key(fio_pem_private_key_s *key,
                                    const char *pem_data,
                                    size_t pem_len);
```

Parses a PEM private key into `key`.

Supported forms:

| PEM label | Format | Supported keys |
| --- | --- | --- |
| `PRIVATE KEY` | PKCS#8 | RSA, P-256, Ed25519 |
| `RSA PRIVATE KEY` | PKCS#1 | RSA |
| `EC PRIVATE KEY` | SEC1 | P-256 |

Returns `0` on success, `-1` on unsupported format, missing dependency, encrypted key, or malformed input.

### `fio_pem_get_certificate_der`

```c
SFUNC size_t fio_pem_get_certificate_der(uint8_t *der_out,
                                         size_t der_out_len,
                                         const char *pem_data,
                                         size_t pem_len);
```

Extracts a `CERTIFICATE` block as raw DER without parsing the X.509 structure. Returns the DER length written, or `0` on error.

### `fio_pem_private_key_clear`

```c
FIO_IFUNC void fio_pem_private_key_clear(fio_pem_private_key_s *key);
```

Securely zeros a parsed private-key structure.

## Example: Extract Certificate DER

```c
#define FIO_PEM
#include "fio-stl.h"

size_t load_der(uint8_t *der, size_t der_cap, const char *pem, size_t pem_len) {
  return fio_pem_get_certificate_der(der, der_cap, pem, pem_len);
}
```

## Example: Parse Multiple Blocks

```c
#define FIO_PEM
#include "fio-stl.h"

void scan_pem(const char *pem, size_t len) {
  uint8_t der[8192];
  while (len) {
    fio_pem_s block;
    size_t used = fio_pem_parse(&block, der, sizeof(der), pem, len);
    if (!used)
      break;

    /* block.label / block.label_len name the PEM type. */
    /* block.der / block.der_len hold the decoded DER bytes. */

    pem += used;
    len -= used;
  }
}
```

## Dependency Notes

- Generic `fio_pem_parse` needs only the PEM/base64 helpers.
- Certificate parsing needs X.509 support.
- Private key parsing uses ASN.1, and RSA/P-256/Ed25519 support depends on the matching crypto headers.

------------------------------------------------------------
# Argon2

```c
#define FIO_ARGON2
#include "fio-stl.h"
```

Argon2 is a memory-hard password hashing function from RFC 9106. It uses BLAKE2b underneath and lets you tune time, memory, and lane count so attackers have to pay real rent.

This implementation supports Argon2d, Argon2i, and Argon2id. It is single-threaded: `parallelism` affects the Argon2 lane layout, but lanes are processed sequentially.

When comparing Argon2 outputs, use `fio_ct_is_eq` or another constant-time comparison.

## Types

### `fio_argon2_type_e`

```c
typedef enum {
  FIO_ARGON2D = 0,
  FIO_ARGON2I = 1,
  FIO_ARGON2ID = 2,
} fio_argon2_type_e;
```

- `FIO_ARGON2D`: data-dependent access, faster, weaker against side-channel observers.
- `FIO_ARGON2I`: data-independent access.
- `FIO_ARGON2ID`: hybrid mode, commonly recommended for password hashing.

### `fio_argon2_args_s`

```c
typedef struct {
  fio_buf_info_s password;
  fio_buf_info_s salt;
  fio_buf_info_s secret;
  fio_buf_info_s ad;
  uint32_t t_cost;
  uint32_t m_cost;
  uint32_t parallelism;
  uint32_t outlen;
  fio_argon2_type_e type;
} fio_argon2_args_s;
```

Arguments:

| Field | Meaning |
| --- | --- |
| `password` | Password / message `P`. |
| `salt` | Salt / nonce `S`; use a unique random salt. |
| `secret` | Optional secret key `K`. |
| `ad` | Optional associated data `X`. |
| `t_cost` | Pass count, minimum `1`. |
| `m_cost` | Memory cost in KiB, minimum `8 * parallelism`. |
| `parallelism` | Lane count, minimum `1`; processed sequentially here. |
| `outlen` | Output length in bytes, minimum `4`, default `32`. |
| `type` | `FIO_ARGON2D`, `FIO_ARGON2I`, or `FIO_ARGON2ID`. |

## API

### `fio_argon2`

```c
SFUNC fio_u512 fio_argon2(fio_argon2_args_s args);
#define fio_argon2(...) fio_argon2((fio_argon2_args_s){__VA_ARGS__})
```

Computes an Argon2 hash and returns up to 64 bytes in `fio_u512`. Use the first `outlen` bytes; if `outlen` is `0`, the default is 32 bytes.

For outputs longer than 64 bytes, use `fio_argon2_hash`.

### `fio_argon2_hash`

```c
SFUNC int fio_argon2_hash(void *out, fio_argon2_args_s args);
#define fio_argon2_hash(out, ...)                                              \
  fio_argon2_hash(out, (fio_argon2_args_s){__VA_ARGS__})
```

Writes the Argon2 output into a caller-provided buffer. Supports arbitrary output lengths of at least 4 bytes.

Returns `0` on success, `-1` on error.

## Example

```c
#define FIO_ARGON2
#include "fio-stl.h"

int check_password(const char *password,
                   size_t password_len,
                   const void *salt,
                   size_t salt_len,
                   const uint8_t expected[32]) {
  fio_u512 hash = fio_argon2(
      .password = FIO_BUF_INFO2((void *)password, password_len),
      .salt = FIO_BUF_INFO2((void *)salt, salt_len),
      .t_cost = 3,
      .m_cost = 65536,     /* 64 MiB */
      .parallelism = 4,
      .outlen = 32,
      .type = FIO_ARGON2ID);

  return fio_ct_is_eq(hash.u8, expected, 32) ? 0 : -1;
}
```

## Practical Notes

- Store the salt and cost settings with the password hash. Future you will not remember them. Future you never does.
- Pick `FIO_ARGON2ID` for password hashing unless you have a specific reason not to.
- Raise `m_cost` and `t_cost` until the slow path is acceptable for your login flow.
- Do not compare hashes with `memcmp`; use constant-time comparison.

------------------------------------------------------------
# Lyra2

```c
#define FIO_LYRA2
#include "fio-stl.h"
```

Lyra2 is a memory-hard password hashing scheme built around a BLAKE2b sponge. It lets you tune time cost and memory matrix size so password checks are cheap for you and annoying for attackers. A fair trade.

This implementation matches the reference C setup with `nPARALLEL == 1`, `SPONGE == 0`, and `RHO == 1`. It is single-threaded.

When comparing Lyra2 outputs, use `fio_ct_is_eq` or another constant-time comparison.

## Arguments

```c
typedef struct {
  fio_buf_info_s password;
  fio_buf_info_s salt;
  uint64_t t_cost;
  uint64_t m_cost;
  size_t outlen;
  size_t n_cols;
} fio_lyra2_args_s;
```

| Field | Meaning |
| --- | --- |
| `password` | Password to hash. |
| `salt` | Salt for the hash; use a unique random salt. |
| `t_cost` | Time cost / number of rounds, minimum `1`. |
| `m_cost` | Memory cost / number of matrix rows, minimum `3`. |
| `outlen` | Output length in bytes; default `32` when `0`. |
| `n_cols` | Matrix column count; default `256` when `0`. |

## API

### `fio_lyra2`

```c
SFUNC fio_u512 fio_lyra2(fio_lyra2_args_s args);
#define fio_lyra2(...) fio_lyra2((fio_lyra2_args_s){__VA_ARGS__})
```

Computes a Lyra2 hash and returns up to 64 bytes in `fio_u512`. Use the first `outlen` bytes; when `outlen` is `0`, use the default 32 bytes.

For outputs longer than 64 bytes, use `fio_lyra2_hash`.

### `fio_lyra2_hash`

```c
SFUNC int fio_lyra2_hash(void *out, fio_lyra2_args_s args);
#define fio_lyra2_hash(out, ...)                                               \
  fio_lyra2_hash(out, (fio_lyra2_args_s){__VA_ARGS__})
```

Writes the Lyra2 output into a caller-provided buffer. Supports arbitrary output lengths.

Returns `0` on success, `-1` on error.

## Example

```c
#define FIO_LYRA2
#include "fio-stl.h"

int check_password(const char *password,
                   size_t password_len,
                   const void *salt,
                   size_t salt_len,
                   const uint8_t expected[32]) {
  fio_u512 hash = fio_lyra2(
      .password = FIO_BUF_INFO2((void *)password, password_len),
      .salt = FIO_BUF_INFO2((void *)salt, salt_len),
      .t_cost = 3,
      .m_cost = 1024,
      .outlen = 32);

  return fio_ct_is_eq(hash.u8, expected, 32) ? 0 : -1;
}
```

## Practical Notes

- Store the salt, `t_cost`, `m_cost`, `n_cols`, and `outlen` next to the hash.
- Increase `m_cost` first when you want more memory pressure.
- Use constant-time comparison for verification.
- If you need the standardized RFC 9106 family, see [Argon2](./159%20argon2.md). Lyra2 is a different memory-hard tool.

------------------------------------------------------------
# OTP (TOTP)

```c
#define FIO_OTP
#include "fio-stl.h"
```

Time-based One-Time Password helper. Implements the TOTP algorithm over HMAC-SHA-1, compatible with common authenticator apps. It can generate keys, encode them as Base32, and compute codes for the current time or a fixed timestamp.

### Types

#### `fio_otp_settings_s`

```c
typedef struct {
  /** The time interval for TOTP rotation. */
  size_t interval; /* 30 == Google OTP */
  /** The number of digits in the OTP. */
  size_t digits; /* 6 == Google OTP */
  /** The time offset (in `interval` units) from the current time. */
  int64_t offset; /* 0 == Google OTP */
  /** Set to true if the secret / key is in Hex instead of Base32. */
  uint8_t is_hex;
  /** Set to true if the secret / key is raw bit data (no encoding). */
  uint8_t is_raw;
} fio_otp_settings_s;
```

Configuration for OTP generation. Fields omitted in the named-argument macro call default to zero; the implementation then substitutes the Google-OTP defaults (30-second interval, 6 digits, zero offset).

### API Functions

#### `fio_otp_generate_key`

```c
FIO_IFUNC fio_u128 fio_otp_generate_key(void);
```

Generates a cryptographically secure random 128-bit key.

**Returns:** a 16-byte random key suitable for TOTP secrets.

#### `fio_otp_print_key`

```c
FIO_IFUNC size_t fio_otp_print_key(char *dest, uint8_t *key, size_t len);
```

Encodes a key as a Base32 string for sharing with authenticator apps.

**Parameters:**
- `dest` — output buffer. Must be at least `len * 2 + 1` bytes.
- `key` — key bytes; if `NULL`, a new random key is generated and encoded.
- `len` — key length in bytes.

**Returns:** length of the encoded string written to `dest`.

#### `fio_otp`

```c
SFUNC uint32_t fio_otp(fio_buf_info_s secret, fio_otp_settings_s settings);
/* Named arguments using macro. */
#define fio_otp(secret, ...) fio_otp(secret, (fio_otp_settings_s){__VA_ARGS__})
```

Computes a TOTP for the current wall-clock time.

**Parameters:**
- `secret` — shared secret as a `fio_buf_info_s`.

**Named Arguments:**
| Argument | Type | Description |
|---|---|---|
| `interval` | `size_t` | rotation interval in seconds; defaults to 30 |
| `digits` | `size_t` | number of output digits; defaults to 6 |
| `offset` | `int64_t` | interval offset from now; defaults to 0 |
| `is_hex` | `uint8_t` | set if `secret` is Hex encoded |
| `is_raw` | `uint8_t` | set if `secret` is raw bytes |

**Returns:** the OTP value.

#### `fio_otp_at`

```c
SFUNC uint32_t fio_otp_at(fio_buf_info_s secret,
                          uint64_t unix_time,
                          fio_otp_settings_s settings);
/* Named arguments using macro. */
#define fio_otp_at(secret, unix_time, ...)                                     \
  fio_otp_at(secret, unix_time, (fio_otp_settings_s){__VA_ARGS__})
```

Computes a TOTP for a specific Unix timestamp. Useful for tests and RFC vectors.

**Parameters:**
- `secret` — shared secret as a `fio_buf_info_s`.
- `unix_time` — timestamp to compute the OTP for.

**Named Arguments:** same as `fio_otp`.

**Returns:** the OTP value.

### Example

```c
#define FIO_OTP
#include "fio-stl.h"

int main(void) {
  /* Generate and print a new Base32 secret. */
  char key_str[64];
  fio_otp_print_key(key_str, NULL, 0);
  printf("secret: %s\n", key_str);

  /* Compute the current 6-digit code. */
  uint32_t code = fio_otp(FIO_BUF_INFO1(key_str));
  printf("code: %06u\n", code);
  return 0;
}
```

------------------------------------------------------------
# Secrets

```c
#define FIO_SECRET
#include "fio-stl.h"
```

A small program-wide secret helper. It reads an optional `SECRET` environment variable at startup, hashes it with SHA-512, and stores the result masked in memory so the raw secret does not show up in core dumps. If no secret is provided, a random one is generated.

This is **not** a key-management vault — it is a convenience for deriving a stable program-wide token from something outside the source tree.

### Environment Variables

At startup (via `FIO_CONSTRUCTOR`), the module checks:

- `SECRET` — the secret value, plain text or Hex encoded.
- `SECRET_LENGTH` — optional byte length; if omitted, `strlen` is used.

If `SECRET` is unset or empty, a random 512-bit secret is generated.

### API Functions

#### `fio_secret_is_random`

```c
SFUNC bool fio_secret_is_random(void);
```

Returns `true` if the global secret was randomly generated (rather than loaded from the environment).

#### `fio_secret`

```c
SFUNC fio_u512 fio_secret(void);
```

Returns the global secret as a masked SHA-512 hash. The value is unmasked on return; keep the result on the stack and erase it promptly.

**Returns:** the SHA-512 hash of the program secret, unmasked.

#### `fio_secret_set`

```c
SFUNC void fio_secret_set(char *str, size_t len, bool is_random);
```

Sets the global secret from `str` and stores its masked SHA-512 hash.

**Parameters:**
- `str` — secret bytes; may be plain text or Hex encoded. Whitespace is ignored during Hex decoding.
- `len` — length of `str` in bytes.
- `is_random` — set `true` if the secret is randomly generated.

If `str` is `NULL` or `len` is `0`, a random secret is generated and `is_random` is forced to `true`.

#### `fio_secret_set_at`

```c
SFUNC void fio_secret_set_at(fio_u512 *secret, char *str, size_t len);
```

Hashes `str` with SHA-512 and stores the masked result in `secret`.

**Parameters:**
- `secret` — destination hash. If `NULL`, the function returns silently.
- `str` — secret bytes; Hex decoding and whitespace handling apply as in `fio_secret_set`.
- `len` — length of `str`.

If `str` is `NULL` or `len` is `0`, random bytes are hashed instead.

#### `fio_secret_at`

```c
SFUNC fio_u512 fio_secret_at(fio_u512 *secret);
```

Unmasks a secret hash stored by `fio_secret_set_at`.

**Parameters:**
- `secret` — pointer to a masked `fio_u512`.

**Returns:** the unmasked SHA-512 hash.

### Example

```c
#define FIO_SECRET
#include "fio-stl.h"

int main(void) {
  /* Set a custom secret and read it back. */
  fio_secret_set("hunter2", 7, 0);
  fio_u512 s = fio_secret();
  /* s now holds SHA-512("hunter2"), masked while in global storage. */
  fio_secure_zero(&s, sizeof(s));
  return 0;
}
```

------------------------------------------------------------
# Brotli

```c
#define FIO_BROTLI
#include "fio-stl.h"
```

Brotli support for RFC 7932 data. The decoder handles the real format: Huffman tables, context modes, static dictionary, transforms, and the usual compressed-data acrobatics. The encoder provides practical quality levels 1–6.

## API

### `fio_brotli_decompress`

```c
SFUNC size_t fio_brotli_decompress(void *out,
                                   size_t out_len,
                                   const void *in,
                                   size_t in_len);
```

Decompresses Brotli data.

Return values:

| Condition | Return value |
| --- | --- |
| Success | Decompressed byte count, `<= out_len`. |
| Output too small | Required buffer size, `> out_len`. |
| Corrupt input | `0`. |
| Size query | Required size, or `0` if corrupt / not knowable by scan. |

Pass `out == NULL` or `out_len == 0` to query the decompressed size. The query path scans meta-block lengths without allocating.

### `fio_brotli_decompress_bound`

```c
FIO_IFUNC size_t fio_brotli_decompress_bound(size_t in_len);
```

Returns a conservative decompression bound. The practical formula is `in_len * 1032 + 1024`, capped at 4GB for very large inputs. Prefer the size-query mode of `fio_brotli_decompress` when possible.

### `fio_brotli_compress`

```c
SFUNC size_t fio_brotli_compress(void *out,
                                 size_t out_len,
                                 const void *in,
                                 size_t in_len,
                                 int quality);
```

Compresses data using Brotli. `quality` is clamped to 1–6:

- `1–2`: fast greedy matching.
- `3–4`: lazy matching.
- `5–6`: hash chain, context modeling, and static dictionary use.

Use an output buffer sized with `fio_brotli_compress_bound`. Returns compressed length on success, `0` on error.

### `fio_brotli_compress_bound`

```c
FIO_IFUNC size_t fio_brotli_compress_bound(size_t in_len);
```

Returns a generous compression bound: `in_len + (in_len >> 2) + 1024`.

## Example

```c
#define FIO_BROTLI
#include "fio-stl.h"

int roundtrip(const void *src, size_t src_len) {
  size_t cap = fio_brotli_compress_bound(src_len);
  uint8_t *compressed = malloc(cap);
  if (!compressed)
    return -1;

  size_t compressed_len = fio_brotli_compress(compressed, cap, src, src_len, 5);
  if (!compressed_len) {
    free(compressed);
    return -1;
  }

  size_t plain_len = fio_brotli_decompress(NULL, 0, compressed, compressed_len);
  uint8_t *plain = malloc(plain_len);
  if (!plain) {
    free(compressed);
    return -1;
  }

  size_t actual = fio_brotli_decompress(plain, plain_len, compressed, compressed_len);

  free(plain);
  free(compressed);
  return actual == src_len ? 0 : -1;
}
```

## Implementation Notes

The decompressor uses a 64-bit LSB-first bit reader, two-level packed Huffman tables with an 8-bit first level, the RFC 7932 static dictionary, 121 transforms, and context-dependent literal decoding.

The compressor emits Brotli streams with bounded memory use and quality-dependent matching. For tiny input, simpler paths may win because being clever has overhead too.

------------------------------------------------------------
# DEFLATE / Gzip

```c
#define FIO_DEFLATE
#include "fio-stl.h"
```

Raw DEFLATE compression/decompression (RFC 1951), gzip wrappers (RFC 1952), and a streaming API for WebSocket `permessage-deflate` (RFC 7692).

This module is built around a 64-bit bit buffer, packed Huffman tables, a 32KB sliding window, and word-at-a-time match copies. Translation: it tries hard not to be slow.

## Raw DEFLATE

### `fio_deflate_decompress`

```c
SFUNC size_t fio_deflate_decompress(void *out,
                                    size_t out_len,
                                    const void *in,
                                    size_t in_len);
```

Decompresses raw DEFLATE data with no zlib or gzip wrapper.

Return values:

| Condition | Return value |
| --- | --- |
| Success | Decompressed byte count, `<= out_len`. |
| Output too small | Required buffer size, `> out_len`. |
| Corrupt input | `0`. |
| Size query | Required size, or `0` if corrupt. |

Pass `out == NULL` or `out_len == 0` to run a decode pass that counts output bytes without writing them.

### `fio_deflate_decompress_bound`

```c
FIO_IFUNC size_t fio_deflate_decompress_bound(size_t in_len);
```

Returns a conservative decompression bound. For exact sizing, prefer the `fio_deflate_decompress(NULL, 0, ...)` query mode.

### `fio_deflate_compress`

```c
SFUNC size_t fio_deflate_compress(void *out,
                                  size_t out_len,
                                  const void *in,
                                  size_t in_len,
                                  int level);
```

Compresses raw DEFLATE data.

Compression levels:

- `0`: store only.
- `1–3`: fast.
- `4–6`: normal.
- `7–9`: best compression.

Returns compressed length on success, `0` on error.

### `fio_deflate_compress_bound`

```c
FIO_IFUNC size_t fio_deflate_compress_bound(size_t in_len);
```

Returns an upper bound for raw DEFLATE output size.

## Gzip Wrappers

### `fio_gzip_compress`

```c
SFUNC size_t fio_gzip_compress(void *out,
                               size_t out_len,
                               const void *in,
                               size_t in_len,
                               int level);
```

Compresses with a gzip wrapper for uses such as HTTP `Content-Encoding: gzip`. The output includes the gzip header, DEFLATE payload, CRC32, and original size trailer.

Returns total output length, or `0` on error.

### `fio_gzip_decompress`

```c
SFUNC size_t fio_gzip_decompress(void *out,
                                 size_t out_len,
                                 const void *in,
                                 size_t in_len);
```

Decompresses gzip data, including wrapper validation and CRC/size checks when data is actually decoded.

Return modes mirror `fio_deflate_decompress`: byte count, required size, or `0` on invalid input.

## Streaming API

### `fio_deflate_s`

```c
typedef struct fio_deflate_s fio_deflate_s;
```

Opaque streaming compression/decompression state. It keeps window state across calls for context takeover.

### `fio_deflate_new`

```c
SFUNC fio_deflate_s *fio_deflate_new(int level, int is_compress);
```

Creates a streaming state. `is_compress != 0` creates a compressor; `0` creates a decompressor. Returns `NULL` on allocation failure.

### `fio_deflate_free`

```c
SFUNC void fio_deflate_free(fio_deflate_s *s);
```

Frees a streaming state.

### `fio_deflate_destroy`

```c
SFUNC void fio_deflate_destroy(fio_deflate_s *s);
```

Resets a streaming context while keeping allocated memory.

### `fio_deflate_push`

```c
SFUNC size_t fio_deflate_push(fio_deflate_s *s,
                              void *out,
                              size_t out_len,
                              const void *in,
                              size_t in_len,
                              int flush);
```

Compresses or decompresses the next input chunk.

- `flush == 0`: normal streaming.
- `flush == 1`: sync flush, useful at WebSocket frame boundaries.

For decompression, a return value greater than `out_len` means “retry with this much output space”; buffered input is preserved for that retry.

## Example: Raw Roundtrip

```c
#define FIO_DEFLATE
#include "fio-stl.h"

int roundtrip(const void *src, size_t src_len) {
  size_t cap = fio_deflate_compress_bound(src_len);
  uint8_t *compressed = malloc(cap);
  if (!compressed)
    return -1;

  size_t compressed_len = fio_deflate_compress(compressed, cap, src, src_len, 6);
  if (!compressed_len) {
    free(compressed);
    return -1;
  }

  size_t plain_len = fio_deflate_decompress(NULL, 0, compressed, compressed_len);
  uint8_t *plain = malloc(plain_len);
  if (!plain) {
    free(compressed);
    return -1;
  }

  size_t actual = fio_deflate_decompress(plain, plain_len, compressed, compressed_len);

  free(plain);
  free(compressed);
  return actual == src_len ? 0 : -1;
}
```

------------------------------------------------------------
# TLS 1.3 Standalone Library

```c
#define FIO_TLS13
#include "fio-stl.h"
```

Standalone TLS 1.3 crypto library — for IO-layer TLS see [`405 tls13.md`](./405%20tls13.md).

This header implements TLS 1.3 building blocks and standalone client/server state machines: key schedule, record protection, handshake message helpers, alerts, KeyUpdate, certificate-request helpers, and client/server encrypt/decrypt flows.

It is not the facil.io IO integration layer. If you want TLS plugged into the evented IO stack, use the `405` layer. If you want the raw TLS 1.3 machinery, this is the sharp box.

**Security note:** this implementation has not been independently audited. Prefer audited platform TLS for production trust decisions when practical.

## Requirements

The key schedule needs HKDF, which needs SHA-2. The full TLS paths also depend on the selected AEAD, key exchange, certificate, and signature modules. `FIO_CRYPTO` is the easy button when you want the whole crypto set.

Common dependencies:

- `FIO_HKDF` / `FIO_SHA2` for key derivation.
- `FIO_AES` for AES-GCM suites.
- `FIO_CHACHA` for ChaCha20-Poly1305.
- `FIO_ED25519` for X25519 and Ed25519.
- `FIO_P256`, `FIO_P384`, `FIO_RSA`, `FIO_X509`, `FIO_PEM` as certificate/key support requires.

## Constants

```c
#define FIO_TLS13_SHA256_HASH_LEN 32
#define FIO_TLS13_SHA384_HASH_LEN 48
#define FIO_TLS13_MAX_HASH_LEN    48
#define FIO_TLS13_AES128_KEY_LEN  16
#define FIO_TLS13_AES256_KEY_LEN  32
#define FIO_TLS13_CHACHA_KEY_LEN  32
#define FIO_TLS13_IV_LEN          12
```

The record layer uses 12-byte IVs and 16-byte AEAD tags. Maximum TLS plaintext is 16 KiB.

## Main Types

The public types include:

- `fio_tls13_content_type_e` — alert, handshake, application data, and legacy change-cipher-spec content types.
- `fio_tls13_handshake_type_e` — ClientHello, ServerHello, Certificate, Finished, KeyUpdate, and related handshake message IDs.
- `fio_tls13_cipher_suite_e` — `TLS_AES_128_GCM_SHA256`, `TLS_AES_256_GCM_SHA384`, and `TLS_CHACHA20_POLY1305_SHA256`.
- `fio_tls13_cipher_type_e` — the AEAD implementation selector for record keys.
- `fio_tls13_record_keys_s` — per-direction write key, IV, sequence number, key length, and cipher type.
- Parsed handshake structs such as `fio_tls13_server_hello_s`, `fio_tls13_encrypted_extensions_s`, `fio_tls13_certificate_s`, `fio_tls13_certificate_verify_s`, and `fio_tls13_certificate_request_s`.
- `fio_tls13_client_s` and `fio_tls13_server_s` — standalone client/server TLS state.

## Key Schedule

These functions implement RFC 8446 section 7 derivation rules:

| Function | Purpose |
| --- | --- |
| `fio_tls13_hkdf_expand_label` | TLS-specific `HKDF-Expand-Label`. |
| `fio_tls13_derive_secret` | `Derive-Secret(secret, label, transcript_hash)`. |
| `fio_tls13_derive_early_secret` | Derive Early Secret from PSK or empty PSK. |
| `fio_tls13_derive_handshake_secret` | Derive Handshake Secret from Early Secret and ECDHE. |
| `fio_tls13_derive_master_secret` | Derive Master Secret. |
| `fio_tls13_derive_traffic_keys` | Derive write key and IV from a traffic secret. |
| `fio_tls13_derive_finished_key` | Derive the Finished MAC key. |
| `fio_tls13_compute_finished` | Compute Finished verify data. |
| `fio_tls13_update_traffic_secret` | Advance application traffic secret for KeyUpdate. |

`use_sha384 != 0` selects SHA-384; otherwise SHA-256 is used.

## Record Layer

| Function | Purpose |
| --- | --- |
| `fio_tls13_build_nonce` | XOR sequence number into IV to build the AEAD nonce. |
| `fio_tls13_record_keys_init` | Initialize `fio_tls13_record_keys_s`. |
| `fio_tls13_record_keys_clear` | Securely clear record keys. |
| `fio_tls13_record_encrypt` | Encrypt one TLS record and advance sequence number. |
| `fio_tls13_record_decrypt` | Decrypt one TLS record, recover inner content type, and advance sequence number. |

Record encryption appends the inner content type before AEAD protection. Decryption strips padding and reports the recovered `fio_tls13_content_type_e`.

## Handshake Helpers

| Function | Purpose |
| --- | --- |
| `fio_tls13_write_handshake_header` | Write a TLS handshake header. |
| `fio_tls13_build_client_hello` | Build a ClientHello with X25519 key share, SNI, and cipher suites. |
| `fio_tls13_parse_server_hello` | Parse ServerHello. |
| `fio_tls13_parse_encrypted_extensions` | Parse EncryptedExtensions. |
| `fio_tls13_parse_certificate` | Parse TLS Certificate handshake body. |
| `fio_tls13_parse_certificate_verify` | Parse CertificateVerify. |
| `fio_tls13_build_finished` | Build Finished handshake message from verify data. |
| `fio_tls13_parse_finished` | Verify a Finished message against expected verify data. |
| `fio_tls13_parse_certificate_request` | Parse CertificateRequest. |
| `fio_tls13_build_certificate_request` | Build CertificateRequest. |

## Alerts and KeyUpdate

| Function | Purpose |
| --- | --- |
| `fio_tls13_build_alert` | Build a plaintext alert payload. |
| `fio_tls13_send_alert` | Build and encrypt an alert record. |
| `fio_tls13_send_alert_plaintext` | Build a plaintext alert record. |
| `fio_tls13_build_key_update` | Build a KeyUpdate handshake message. |
| `fio_tls13_parse_key_update` | Parse KeyUpdate and extract request flag. |
| `fio_tls13_process_key_update` | Process peer KeyUpdate and rotate read/write state as needed. |
| `fio_tls13_send_key_update_response` | Send the required response update when requested. |

## Standalone Client API

| Function | Purpose |
| --- | --- |
| `fio_tls13_client_init` | Initialize client state for a server name. |
| `fio_tls13_client_destroy` | Clear client state. |
| `fio_tls13_client_set_trust_store` | Attach an X.509 trust store pointer. |
| `fio_tls13_client_skip_verification` | Enable/disable certificate verification skipping. Handle with tongs. |
| `fio_tls13_client_get_cert_error` | Return the last certificate validation error. |
| `fio_tls13_client_is_cert_verified` | Report certificate and chain verification status. |
| `fio_tls13_client_start` | Build the initial ClientHello. |
| `fio_tls13_client_process` | Process incoming handshake/record bytes and optionally emit output. |
| `fio_tls13_client_encrypt` | Encrypt application data. |
| `fio_tls13_client_decrypt` | Decrypt application data. |
| `fio_tls13_client_is_connected` | Test for connected state. |
| `fio_tls13_client_is_error` | Test for error state. |
| `fio_tls13_client_alpn_set` | Configure ALPN protocols. |
| `fio_tls13_client_set_cert` | Configure client certificate/private key. |
| `fio_tls13_client_set_public_key` | Configure client public key. |
| `fio_tls13_client_cert_requested` | Report whether the server requested a client certificate. |

## Standalone Server API

| Function | Purpose |
| --- | --- |
| `fio_tls13_server_init` | Initialize server state. |
| `fio_tls13_server_destroy` | Clear server state. |
| `fio_tls13_server_set_cert_chain` | Configure the server certificate chain. |
| `fio_tls13_server_set_private_key` | Configure the private key. |
| `fio_tls13_server_process` | Process incoming handshake/record bytes and optionally emit output. |
| `fio_tls13_server_encrypt` | Encrypt application data. |
| `fio_tls13_server_decrypt` | Decrypt application data. |
| `fio_tls13_server_is_connected` | Test for connected state. |
| `fio_tls13_server_is_error` | Test for error state. |
| `fio_tls13_server_alpn_set` | Configure ALPN protocols. |
| `fio_tls13_server_require_client_cert` | Configure client-certificate request/require mode. |
| `fio_tls13_server_client_cert_received` | Report whether a client certificate arrived. |
| `fio_tls13_server_client_cert_verified` | Report whether the client certificate verified. |

## Example Shape

```c
#define FIO_TLS13
#define FIO_CRYPTO
#include "fio-stl.h"

void client_start(uint8_t *out, size_t cap) {
  fio_tls13_client_s client;
  fio_tls13_client_init(&client, "example.com");

  int len = fio_tls13_client_start(&client, out, cap);
  if (len < 0) {
    /* handle error */
  }

  fio_tls13_client_destroy(&client);
}
```

## Practical Notes

- `190 tls13.md` documents the standalone TLS 1.3 implementation.
- [`405 tls13.md`](./405%20tls13.md) is the IO-layer integration point.
- Skipping certificate verification is for tests and controlled environments, not normal trust.
- Keep traffic secrets and record keys out of logs. They are not collectibles.

------------------------------------------------------------
# Dynamic Types — Module Group 200

The 200-range modules provide the core dynamic/generic types in the facil.io C STL: strings, arrays, maps, reference counting, and the FIOBJ soft-type system.

All five modules share the same design: **define a name macro, then include the header**. The preprocessor does the rest, generating a fully typed, prefixed API from static-inline functions and macros.

---

## The Pattern

```c
/* 1. Set the name (and any optional configuration macros). */
#define FIO_STR_NAME my_str

/* 2. Include the STL (single-header or multi-header). */
#include "fio-stl.h"

/* 3. Use the generated API — everything is prefixed with your name. */
my_str_s s = {0};
my_str_write(&s, "hello", 5);
my_str_destroy(&s);
```

Each inclusion is independent. You can define the same module multiple times in the same translation unit as long as you use a different name macro each time.

---

## Defining a Custom Type

| Name macro | What you get |
|---|---|
| `FIO_STR_NAME` | Binary-safe dynamic string type and API |
| `FIO_ARRAY_NAME` | Dynamic array for an element type you choose |
| `FIO_MAP_NAME` | Hash map or set (ordered or unordered) |
| `FIO_REF_NAME` | Reference-counted wrapper around any type |
| `FIO_FIOBJ` | Soft/dynamic object system (no extra name needed) |

Optional companion macros (e.g. `FIO_ARRAY_TYPE`, `FIO_MAP_KEY`, `FIO_MAP_VALUE`, `FIO_REF_TYPE`) let you configure element types, comparison functions, copy/destroy hooks, and more — consult the individual module docs for the full list.

---

## Modules

| Module | Name macro | Doc |
|---|---|---|
| Dynamic Strings | `FIO_STR_NAME` (or `FIO_STR_SMALL`) | [→ 201 string.md](./201 string.md) |
| Dynamic Arrays | `FIO_ARRAY_NAME` | [→ 202 array.md](./202 array.md) |
| Hash Maps / Sets | `FIO_MAP_NAME` | [→ 210 map.md](./210 map.md) |
| Reference Counting | `FIO_REF_NAME` | [→ 249 reference counter.md](./249 reference counter.md) |
| FIOBJ Soft Types | `FIO_FIOBJ` | [→ 250 fiobj.md](./250 fiobj.md) |

> **Note:** `210 map2.h` is a legacy file kept for backward compatibility and is not part of this active module group.

---

## Quick Example — Three Types, One File

```c
/* A small immutable-optimized string */
#define FIO_STR_SMALL tag_str
#include "fio-stl.h"

/* A dynamic array of those strings */
#define FIO_ARRAY_NAME tag_list
#define FIO_ARRAY_TYPE tag_str_s
#define FIO_ARRAY_TYPE_DESTROY(s) tag_str_destroy(&(s))
#include "fio-stl.h"

/* A map from string keys to integer values */
#define FIO_MAP_NAME   word_count
#define FIO_MAP_KEY_KSTR          /* use fio_keystr_s keys */
#define FIO_MAP_VALUE  size_t
#include "fio-stl.h"
```

Each `#include` is processed independently. Define → include → use. That's it.
# Dynamic Strings — Module 201

Binary-safe dynamic strings, generated from a name macro.

See also: [← 200 types-overview.md](./200 types-overview.md)

---

## Setup

```c
#define FIO_STR_NAME my_str   /* required — sets the type and function prefix */
#include "fio-stl.h"
```

This generates `my_str_s` and a full API prefixed `my_str_*`.

Throughout this document the placeholder `STR` stands for whatever name you chose. All examples use `fio_str` as the name.

---

## Flavors

### Default — `FIO_STR_NAME`

The default struct stores capacity, length, and a buffer pointer (or embeds the string directly when it's short enough). On a 64-bit system the struct is 32 bytes, letting strings up to **30 bytes** live entirely inside the struct with no heap allocation.

```c
#define FIO_STR_NAME fio_str
#include "fio-stl.h"
```

Best for: strings that are written to multiple times, or strings in the 15–30 byte range that fit in the embedded slot.

### Small / immutable-optimized — `FIO_STR_SMALL`

```c
#define FIO_STR_SMALL key   /* also sets FIO_STR_NAME = key */
#include "fio-stl.h"
```

Sets `FIO_STR_OPTIMIZE4IMMUTABILITY 1`, which drops the capacity field to shrink the struct to 16 bytes. Short strings (≤ 14 bytes on 64-bit) are still embedded. Editing long strings is slower because capacity must be recalculated on each write.

Best for: map keys, mostly-immutable strings, or any context where struct size matters more than write throughput.

---

## Configuration Macros

Set these **before** `#include`, after `FIO_STR_NAME`:

| Macro | Default | Effect |
|---|---|---|
| `FIO_STR_OPTIMIZE_EMBEDDED` | `0` | Each unit adds `sizeof(char*)` to the struct, growing the embedded-string capacity. Max 4 (default) or 1 (`FIO_STR_SMALL`). |
| `FIO_STR_OPTIMIZE4IMMUTABILITY` | `0` | Drops the capacity field; set automatically by `FIO_STR_SMALL`. |

---

## Initialization Macros

| Macro | Notes |
|---|---|
| `FIO_STR_INIT` | Zero-initializes the struct. Use for stack or heap allocation. |
| `FIO_STR_INIT_EXISTING(buf, len, capa)` | Wraps an already-allocated buffer. **Not valid for `FIO_STR_SMALL`**. |
| `FIO_STR_INIT_STATIC(cstr)` | Wraps a string literal; `free` won't touch the buffer. **Not valid for `FIO_STR_SMALL`**. |
| `FIO_STR_INIT_STATIC2(buf, len)` | Same as above with explicit length. **Not valid for `FIO_STR_SMALL`**. |

```c
fio_str_s s = FIO_STR_INIT;           /* stack, zero-initialized */

fio_str_s *p = malloc(sizeof(*p));
*p = (fio_str_s)FIO_STR_INIT;         /* heap, zero-initialized */

fio_str_s ro = FIO_STR_INIT_STATIC("read-only literal");
```

---

## Lifecycle

### Heap allocation

```c
fio_str_s *STR_new(void);          /* allocate + zero-initialize */
void       STR_free(fio_str_s *s); /* destroy content + free struct */
```

### Stack / embedded destruction

```c
void STR_destroy(fio_str_s *s);    /* free content, re-initialize struct */
```

### Initialization helpers

```c
/* Wrap a static/const buffer. Copies if it fits embedded, otherwise borrows. */
fio_str_info_s STR_init_const(fio_str_s *s, const char *str, size_t len);

/* Always copies src into s. */
fio_str_info_s STR_init_copy(fio_str_s *s, const char *str, size_t len);

/* Copies another STR object into dest. */
fio_str_info_s STR_init_copy2(fio_str_s *dest, fio_str_s *src);
```

### Detach / hand-off

```c
/* Remove string data from s, return a heap-allocated C string. s is reset. */
char *STR_detach(fio_str_s *s);

/* Free a pointer returned by STR_detach. */
void  STR_dealloc(void *ptr);
```

`detach` always returns a dynamically allocated buffer (never embedded data). Free it with `STR_dealloc`, not plain `free`, unless you know the allocator matches.

---

## State Inspection

```c
fio_str_info_s STR_info(const fio_str_s *s);   /* buf + len + capa */
fio_buf_info_s STR_buf (const fio_str_s *s);   /* buf + len only   */
char          *STR_ptr (fio_str_s *s);          /* char * to data   */
size_t         STR_len (fio_str_s *s);          /* byte length      */
size_t         STR_capa(fio_str_s *s);          /* allocated capacity */
```

`fio_str_info_s` is a plain struct: `{ size_t len; char *buf; size_t capa; }`. When `capa == 0` the buffer is read-only (frozen or static). Data is always NUL-terminated but may contain embedded NUL bytes — don't use `strlen` on it.

```c
void     STR_freeze    (fio_str_s *s);          /* make read-only          */
uint8_t  STR_is_frozen (fio_str_s *s);          /* 1 if frozen             */
int      STR_is_allocated(const fio_str_s *s);  /* 1 if heap buffer in use */
int      STR_is_eq     (const fio_str_s *a,
                        const fio_str_s *b);     /* binary equality         */
uint64_t STR_hash      (const fio_str_s *s,
                        uint64_t seed);          /* Risky Hash              */
```

---

## Memory Management

```c
/* Resize without reallocation (capped by current capacity).
   Returns updated state. Shrinking may lose data beyond the new length. */
fio_str_info_s STR_resize(fio_str_s *s, size_t size);

/* Try to release unused capacity. Effect depends on allocator. */
void STR_compact(fio_str_s *s);

/* Reserve at least `amount` additional bytes beyond current length.
   NOT available for FIO_STR_SMALL types. */
fio_str_info_s STR_reserve(fio_str_s *s, size_t amount);
```

`reserve` + manual write + `resize` is the fastest pattern for bulk construction:

```c
fio_str_s s = FIO_STR_INIT;
fio_str_info_s info = fio_str_reserve(&s, 128);
size_t written = my_encoder(info.buf + fio_str_len(&s), 128, src);
fio_str_resize(&s, fio_str_len(&s) + written);
```

---

## Write / Append

All write functions append to the **end** of the string and return the updated `fio_str_info_s`.

```c
fio_str_info_s STR_write(fio_str_s *s, const void *src, size_t src_len);
fio_str_info_s STR_concat(fio_str_s *dest, fio_str_s *src); /* alias: STR_join */
fio_str_info_s STR_replace(fio_str_s *s,
                            intptr_t start_pos, /* negative = from end */
                            size_t   old_len,   /* 0 = insert only     */
                            const void *src,
                            size_t   src_len);  /* 0 = erase only      */
```

### Multi-write macro

```c
FIO_STR_WRITE2(str_name, dest_ptr, ...fio_string_write_s items...);
```

Combines multiple writes in one call using `fio_string_write_s` descriptors:

```c
#define FIO_STR_NAME fio_str
#include "fio-stl.h"

fio_str_s s = FIO_STR_INIT;
FIO_STR_WRITE2(fio_str, &s,
    FIO_STRING_WRITE_STR1("Hello "),
    FIO_STRING_WRITE_STR1("World!"));
/* s now contains "Hello World!" */
fio_str_destroy(&s);
```

---

## Number Writing

```c
fio_str_info_s STR_write_i  (fio_str_s *s, int64_t num); /* decimal     */
fio_str_info_s STR_write_hex(fio_str_s *s, int64_t num); /* raw hex digits, no 0x prefix */
fio_str_info_s STR_write_bin(fio_str_s *s, int64_t num); /* binary repr */
```

---

## Formatted Writes

```c
fio_str_info_s STR_printf (fio_str_s *s, const char *fmt, ...);
fio_str_info_s STR_vprintf(fio_str_s *s, const char *fmt, va_list argv);
```

Both append to the end of `s`.

```c
fio_str_s msg = FIO_STR_INIT;
fio_str_printf(&msg, "items: %zu, rate: %.2f\n", count, rate);
puts(fio_str_ptr(&msg));
fio_str_destroy(&msg);
```

---

## UTF-8

```c
size_t STR_utf8_valid (fio_str_s *s);                          /* char count if valid, 0 if not */
size_t STR_utf8_len   (fio_str_s *s);                          /* character count   */
int    STR_utf8_select(fio_str_s *s, intptr_t *pos, size_t *len); /* char→byte slice */
```

`utf8_select` converts a UTF-8 character position and length to byte offsets in-place. Sets `*pos = -1` on invalid UTF-8 before the selection point. Returns -1 on error, 0 on success.

---

## Escaping

### JSON / C escaping

```c
fio_str_info_s STR_write_escape  (fio_str_s *s, const void *data, size_t len);
fio_str_info_s STR_write_unescape(fio_str_s *s, const void *escaped, size_t len);
```

### HTML escaping

```c
fio_str_info_s STR_write_html_escape  (fio_str_s *s, const void *raw, size_t len);
fio_str_info_s STR_write_html_unescape(fio_str_s *s, const void *escaped, size_t len);
```

`write_html_unescape` handles numeric code-points and a minimal set of named entities — it is intentionally incomplete.

---

## Base64

```c
fio_str_info_s STR_write_base64enc(fio_str_s *s,
                                    const void *data, size_t data_len,
                                    uint8_t url_encoded); /* 1 for URL-safe variant */
fio_str_info_s STR_write_base64dec(fio_str_s *s,
                                    const void *encoded, size_t encoded_len);
```

---

## File / FD Reading

```c
/* Append from open file descriptor (regular files only; fd stays open). */
fio_str_info_s STR_readfd(fio_str_s *s, int fd,
                           intptr_t start_at, intptr_t limit);

/* Open filename, append slice, close. limit==0 reads to EOF. */
fio_str_info_s STR_readfile(fio_str_s *s, const char *filename,
                             intptr_t start_at, intptr_t limit);
```

On failure `STR_readfile` returns a state with `buf == NULL`.

---

## Quick Example

```c
#define FIO_STR_NAME fio_str
#include "fio-stl.h"

void greet(void) {
    fio_str_s s = FIO_STR_INIT;

    fio_str_write(&s, "Hello", 5);
    fio_str_write(&s, ", ", 2);
    fio_str_write(&s, "world!", 6);

    printf("%.*s\n", (int)fio_str_len(&s), fio_str_ptr(&s));
    /* → Hello, world! */

    fio_str_replace(&s, 0, 5, "Goodbye", 7);
    printf("%.*s\n", (int)fio_str_len(&s), fio_str_ptr(&s));
    /* → Goodbye, world! */

    fio_str_destroy(&s);
}
```

### Heap-allocated with reference counting

```c
#define FIO_STR_NAME fio_str
#define FIO_REF_NAME fio_str
#define FIO_REF_CONSTRUCTOR_ONLY
#define FIO_REF_DESTROY(s) fio_str_destroy(&(s))
#include "fio-stl.h"

void example(void) {
    fio_str_s *s = fio_str_new();
    fio_str_write(s, "shared", 6);

    fio_str_s *ref = fio_str_dup(s); /* increment ref count */
    fio_str_free(ref);               /* decrement; destroys string at zero */
    fio_str_free(s);
}
```

---

## Embedded-string Sizes (64-bit reference)

| Variant | Struct size | Max embedded string |
|---|---|---|
| `FIO_STR_NAME` (default) | 32 bytes | 30 bytes |
| `FIO_STR_NAME` + `FIO_STR_OPTIMIZE_EMBEDDED 1` | 40 bytes | 38 bytes |
| `FIO_STR_SMALL` | 16 bytes | 14 bytes |

Two bytes are always reserved for the metadata byte and the NUL terminator.

---

See [→ 200 types-overview.md](./200 types-overview.md) for the shared define-include-use pattern and how to combine this type with arrays, maps, and reference counters.
# Dynamic Arrays — Module 202

Type-safe dynamic arrays, generated from a name macro.

See also: [← 200 types-overview.md](./200 types-overview.md)

---

## Setup

```c
#define FIO_ARRAY_NAME ary         /* required — sets the type and function prefix */
#define FIO_ARRAY_TYPE int         /* optional — element type; default void * */
#include "fio-stl.h"
```

This generates `ary_s` and a full API prefixed `ary_*`.

Throughout this document `ARY` stands for whatever name you chose. `ARY_PTR` is shorthand for `FIO_ARRAY_PTR` (usually `ARY_s *`). All examples use `fio_ary` as the name.

---

## Configuration Macros

Set these **before** `#include`, after `FIO_ARRAY_NAME`.

### Element type

| Macro | Default | Effect |
|---|---|---|
| `FIO_ARRAY_TYPE` | `void *` | The element type stored in the array. |
| `FIO_ARRAY_TYPE_INVALID` | `NULL` / `{0}` | Sentinel value returned for out-of-bounds access. |
| `FIO_ARRAY_TYPE_INVALID_SIMPLE` | `1` if `{0}` | Set to `1` if the invalid element is all-zero bytes. |
| `FIO_ARRAY_TYPE_STR` | — | Shortcut: configures the array for `fio_keystr_s` string elements (sets type, copy, destroy, and compare automatically). |

### Element hooks

| Macro | Default | Effect |
|---|---|---|
| `FIO_ARRAY_TYPE_COPY(dest, src)` | `dest = src` | Called when copying an element into the array. |
| `FIO_ARRAY_TYPE_DESTROY(obj)` | _(nothing)_ | Called when an element is removed or the array is destroyed. |
| `FIO_ARRAY_TYPE_CMP(a, b)` | `a == b` | Called by `find`, `remove2`, etc. |
| `FIO_ARRAY_TYPE_CONCAT_COPY(dest, src)` | same as `COPY` | Used during `concat`; override if concat needs different copy semantics. |
| `FIO_ARRAY_DESTROY_AFTER_COPY` | `1` if both hooks are non-trivial | When set, `DESTROY` is called on an element after it has been copied to an `old` pointer. |

### Memory / growth

| Macro | Default | Effect |
|---|---|---|
| `FIO_ARRAY_PADDING` | `4` | Extra empty slots allocated beyond the requested capacity. |
| `FIO_ARRAY_EXPONENTIAL` | `0` | Set to `1` for exponential (doubling) growth. Linear growth with padding is the default. |
| `FIO_ARRAY_ENABLE_EMBEDDED` | `1` | Store small arrays directly inside the struct (no heap allocation). Values > 1 add extra struct fields to increase the embedded capacity. |

---

## Initialization

```c
ARY_s a = FIO_ARRAY_INIT;      /* stack — zero-initialize */

ARY_s *p = ARY_new();          /* heap — allocate + zero-initialize */
```

`FIO_ARRAY_INIT` expands to `{0}`, so a zero-initializing `memset` also works.

---

## Lifecycle

```c
ARY_s *ARY_new(void);          /* allocate + initialize on the heap */
void   ARY_free(ARY_PTR ary);  /* destroy elements + free the container */
void   ARY_destroy(ARY_PTR ary); /* destroy elements, reset to empty (stack-safe) */
```

Use `ARY_destroy` for stack-allocated arrays. Use `ARY_new` / `ARY_free` for heap.

```c
#define FIO_ARRAY_NAME fio_ary
#define FIO_ARRAY_TYPE int
#include "fio-stl.h"

void example(void) {
    fio_ary_s a = FIO_ARRAY_INIT;
    fio_ary_push(&a, 1);
    fio_ary_push(&a, 2);
    fio_ary_push(&a, 3);
    /* ... use ... */
    fio_ary_destroy(&a);
}
```

---

## State Inspection

```c
uint32_t       ARY_count(ARY_PTR ary);   /* number of elements */
uint32_t       ARY_capa (ARY_PTR ary);   /* current capacity   */
int            ARY_is_embedded(ARY_PTR ary); /* 1 = embedded, 0 = heap alloc, -1 = error */
FIO_ARRAY_TYPE *ARY2ptr(ARY_PTR ary);        /* raw C pointer to the first element */
```

`ARY_is_embedded` reports whether the array currently uses in-struct storage. `ARY2ptr` returns a pointer directly into the backing array. It is valid until the next mutating call. Useful for bulk reads or passing to functions that expect a C array.

---

## Memory Management

```c
/* Reserve capacity for at least `capa` additional elements.
   Negative capa reserves space at the beginning of the array.
   Returns the new capacity. */
uint32_t ARY_reserve(ARY_PTR ary, int64_t capa);

/* Attempt to shrink memory to fit the current element count. */
void ARY_compact(ARY_PTR ary);
```

Growth is linear by default (current count + padding). Exponential growth can be enabled with `FIO_ARRAY_EXPONENTIAL 1`. Always call `ARY_reserve` before large batch inserts to avoid repeated reallocation.

---

## Push / Pop (end of array)

```c
/* Append an element. Returns a pointer to the new element, or NULL on error. */
FIO_ARRAY_TYPE *ARY_push(ARY_PTR ary, FIO_ARRAY_TYPE data);

/* Remove the last element.
   If `old` is not NULL, the removed value is copied there.
   Returns 0 on success, -1 if the array is empty. */
int ARY_pop(ARY_PTR ary, FIO_ARRAY_TYPE *old);
```

```c
fio_ary_push(&a, 42);

int out;
if (!fio_ary_pop(&a, &out))
    printf("popped: %d\n", out);
```

---

## Unshift / Shift (beginning of array)

```c
/* Prepend an element. Returns a pointer to the new element, or NULL on error.
   May trigger memmove. */
FIO_ARRAY_TYPE *ARY_unshift(ARY_PTR ary, FIO_ARRAY_TYPE data);

/* Remove the first element.
   If `old` is not NULL, the removed value is copied there.
   Returns 0 on success, -1 if the array is empty. */
int ARY_shift(ARY_PTR ary, FIO_ARRAY_TYPE *old);
```

Both `push`/`pop` and heap-backed `unshift`/`shift` are O(1) amortized. The implementation maintains headroom at both ends of the heap buffer to keep prepend operations cheap; the tiny embedded path may move elements.

---

## Random Access

```c
/* Set the element at `index`. Grows the array if needed, filling gaps with
   FIO_ARRAY_TYPE_INVALID.
   Negative index counts from the end (-1 == last element).
   If `old` is not NULL, the previous value is copied there before being
   destroyed.
   Returns a pointer to the set element, or NULL on error. */
FIO_ARRAY_TYPE *ARY_set(ARY_PTR ary, int64_t index, FIO_ARRAY_TYPE data,
                        FIO_ARRAY_TYPE *old);

/* Return the element at `index` (no copy performed).
   Negative index counts from the end.
   Returns FIO_ARRAY_TYPE_INVALID if out of bounds. */
FIO_ARRAY_TYPE ARY_get(ARY_PTR ary, int64_t index);
```

```c
fio_ary_set(&a, 5, 99, NULL);    /* extend + set index 5 */
int v = fio_ary_get(&a, -1);     /* last element */
int prev;
fio_ary_set(&a, 0, 7, &prev);   /* replace index 0, capture old value */
```

---

## Concat

```c
/* Append all elements from `src` to `dest`.
   `src` is not modified.
   Returns `dest`, or NULL on allocation failure. */
ARY_PTR ARY_concat(ARY_PTR dest, ARY_PTR src);
```

Uses `FIO_ARRAY_TYPE_CONCAT_COPY` (defaults to `FIO_ARRAY_TYPE_COPY`) for each element, so managed types are properly duplicated.

---

## Search and Removal

```c
/* Find the first occurrence of `data` starting at `start_at`.
   Negative `start_at` seeks backwards (-1 == last element).
   Returns the index, or FIO_ARRAY_NOT_FOUND ((uint32_t)-1). */
uint32_t ARY_find(ARY_PTR ary, FIO_ARRAY_TYPE data, int64_t start_at);

/* Remove element at `index`. All subsequent elements shift left (memmove).
   If `old` is not NULL, the removed value is copied there.
   Returns 0 on success, -1 on error. O(n). */
int ARY_remove(ARY_PTR ary, int64_t index, FIO_ARRAY_TYPE *old);

/* Remove all occurrences of `data`. Elements are compacted in-place.
   Returns the count of removed items. O(n). */
uint32_t ARY_remove2(ARY_PTR ary, FIO_ARRAY_TYPE data);
```

```c
#define FIO_ARRAY_NOT_FOUND ((uint32_t)-1)

uint32_t idx = fio_ary_find(&a, 42, 0);
if (idx != FIO_ARRAY_NOT_FOUND)
    fio_ary_remove(&a, idx, NULL);
```

---

## Iteration

### Callback-based

```c
/** Iteration info passed to the callback. */
typedef struct ARY_each_s {
    ARY_PTR const parent;   /* the array being iterated */
    uint64_t      index;    /* current element index */
    int (*task)(struct ARY_each_s *info); /* the callback; may be updated mid-cycle */
    void         *udata;    /* opaque user data */
    FIO_ARRAY_TYPE value;   /* the current element's value */
    uint64_t      padding;  /* reserved */
} ARY_each_s;

/* Call `task` for each element starting at `start_at`.
   Returning -1 from `task` stops the loop.
   Returns the stop position (elements processed + start). */
uint32_t ARY_each(ARY_PTR ary,
                  int (*task)(ARY_each_s *info),
                  void *udata,
                  int64_t start_at);
```

### Pointer iteration

```c
/* Returns a pointer to the next element.
   Pass NULL for `pos` to get the first element.
   `first` tracks the base pointer across reallocations.
   Returns NULL when the end is reached. */
FIO_ARRAY_TYPE *ARY_each_next(ARY_PTR ary,
                               FIO_ARRAY_TYPE **first,
                               FIO_ARRAY_TYPE *pos);
```

### Loop macro

```c
/* Iterates with a typed `pos` pointer.
   `array_name` must be the literal macro name used when generating the type.
   Avoid structural mutations inside the loop (push/pop/remove may invalidate pos). */
FIO_ARRAY_EACH(array_name, array_ptr, pos) { /* pos is a FIO_ARRAY_TYPE * */ }
```

```c
#define FIO_ARRAY_NAME fio_ary
#define FIO_ARRAY_TYPE int
#include "fio-stl.h"

void print_all(fio_ary_s *a) {
    FIO_ARRAY_EACH(fio_ary, a, pos) {
        printf("%d\n", *pos);
    }
}
```

---

## Embedded Arrays

When `FIO_ARRAY_ENABLE_EMBEDDED` is set (the default), small arrays are stored directly inside the `ARY_s` struct, skipping heap allocation. For `void *` arrays on a 64-bit system, the struct has room for 2 pointers embedded.

Setting `FIO_ARRAY_ENABLE_EMBEDDED` to a value greater than 1 adds extra fields to the struct, increasing embedded capacity at the cost of a larger struct.

`ARY_is_embedded(ary)` returns `1` when the array is currently in embedded mode. `ARY_compact` will re-embed an array if its count fits within the embedded capacity.

---

## Practical Example — String Pointer Array

```c
#define FIO_ARRAY_NAME str_list
#define FIO_ARRAY_TYPE const char *
#include "fio-stl.h"

void example(void) {
    str_list_s list = FIO_ARRAY_INIT;

    str_list_push(&list, "apple");
    str_list_push(&list, "banana");
    str_list_push(&list, "cherry");

    FIO_ARRAY_EACH(str_list, &list, pos) {
        printf("%s\n", *pos);
    }

    str_list_destroy(&list);
}
```

---

## Practical Example — Struct Array with Custom Hooks

```c
typedef struct { int id; float val; } point_s;

#define FIO_ARRAY_NAME          pt_ary
#define FIO_ARRAY_TYPE          point_s
#define FIO_ARRAY_TYPE_CMP(a,b) ((a).id == (b).id)
#include "fio-stl.h"

void example(void) {
    pt_ary_s a = FIO_ARRAY_INIT;

    pt_ary_push(&a, (point_s){.id = 1, .val = 1.5f});
    pt_ary_push(&a, (point_s){.id = 2, .val = 2.5f});

    point_s key = {.id = 1};
    uint32_t idx = pt_ary_find(&a, key, 0);
    if (idx != FIO_ARRAY_NOT_FOUND)
        printf("found at %u\n", idx);

    pt_ary_destroy(&a);
}
```

---

## Performance Notes

- **Access / set**: O(1).
- **Iteration**: excellent cache locality.
- **push / pop**: O(1) amortized.
- **unshift / shift**: O(1) amortized for heap-backed arrays; the tiny embedded path may move elements.
- **find**: O(n) worst case.
- **remove / remove2**: O(n) — involve `memmove`.
- Growth is **linear** by default (count + `FIO_ARRAY_PADDING`). Use `FIO_ARRAY_EXPONENTIAL 1` or call `ARY_reserve` up-front to avoid repeated reallocations on large batch inserts.
- Capacity is limited to **32 bits** (max ~4 billion elements).

---

See [→ 200 types-overview.md](./200 types-overview.md) for the shared define-include-use pattern and how to combine arrays with strings, maps, and reference counters.
# Hash Maps and Sets — Module 210

Type-safe hash maps and sets, generated from a name macro. One header covers both unordered and ordered (FIFO/LRU) variants.

See also: [← 200 types-overview.md](./200 types-overview.md)

---

## Setup

```c
#define FIO_MAP_NAME my_map    /* required — sets the type and function prefix */
#define FIO_MAP_KEY  uint64_t  /* optional — key type; default is fio_str_info_s (bstr) */
#define FIO_MAP_VALUE void *   /* optional — omit for a Set instead of a Map */
#include "fio-stl.h"
```

This generates `my_map_s` and a full API prefixed `my_map_*`.

Throughout this document `MAP` stands for whatever name you chose. All examples use `map` as the name.

---

## Sets vs. Maps

- **Set**: define only `FIO_MAP_NAME` (and optionally `FIO_MAP_KEY`). Each element is the key itself.
- **Hash Map**: also define `FIO_MAP_VALUE`. Elements are key→value pairs.

If `FIO_MAP_KEY` is left undefined (or `FIO_MAP_KEY_BSTR` is defined), keys default to binary strings via `fio_str_info_s` / `fio_bstr`.

---

## Configuration Macros

Set these **before** `#include`, after `FIO_MAP_NAME`.

### Naming shortcuts

| Macro | Effect |
|---|---|
| `FIO_MAP_NAME name` | Required. Sets the type and function prefix. |
| `FIO_OMAP_NAME name` | Shortcut: defines `FIO_MAP_NAME` and forces `FIO_MAP_ORDERED 1`. |
| `FIO_UMAP_NAME name` | Shortcut: defines `FIO_MAP_NAME` and forces `FIO_MAP_ORDERED 0`. |

### Key configuration

| Macro | Default | Effect |
|---|---|---|
| `FIO_MAP_KEY` | `fio_str_info_s` | External (API) key type. |
| `FIO_MAP_KEY_BSTR` | — | Shortcut: API keys are `fio_str_info_s`; internally stored as `fio_bstr` (heap-allocated, NUL-terminated). This is the default when `FIO_MAP_KEY` is not defined. |
| `FIO_MAP_KEY_KSTR` | — | Shortcut: API keys are `fio_str_info_s`; internally stored as `fio_keystr_s` with small-string optimisation (≤14 bytes inline on 64-bit). Better cache locality than `bstr`. |
| `FIO_MAP_KEY_INTERNAL` | `FIO_MAP_KEY` | Internal storage type (if different from the API type). |
| `FIO_MAP_KEY_FROM_INTERNAL(k)` | `k` | Converts internal storage → API type. |
| `FIO_MAP_KEY_COPY(dest, src)` | `(dest) = (src)` | Copies an API key into internal storage. |
| `FIO_MAP_KEY_CMP(a, b)` | `(a) == (b)` | Compares an internal key with an API key. |
| `FIO_MAP_KEY_DESTROY(key)` | _(nothing)_ | Destroys a key in internal storage (free resources). |
| `FIO_MAP_KEY_DISCARD(key)` | _(nothing)_ | Destroys an API key that was not inserted (e.g. if you pre-allocated or ref-counted it). |

### Value configuration

| Macro | Default | Effect |
|---|---|---|
| `FIO_MAP_VALUE` | _(none — Set)_ | External (API) value type. Defining this makes the template a hash map. |
| `FIO_MAP_VALUE_BSTR` | — | Shortcut: binary-string values via `fio_bstr`. |
| `FIO_MAP_VALUE_INTERNAL` | `FIO_MAP_VALUE` | Internal storage type. |
| `FIO_MAP_VALUE_FROM_INTERNAL(v)` | `v` | Converts internal storage → API type. |
| `FIO_MAP_VALUE_COPY(dest, src)` | `(dest) = (src)` | Copies an API value into internal storage. |
| `FIO_MAP_VALUE_DESTROY(v)` | _(nothing)_ | Destroys a value in internal storage. |
| `FIO_MAP_VALUE_DISCARD(v)` | _(nothing)_ | Destroys an API value that was not stored. |

### Hashing

| Macro | Default | Effect |
|---|---|---|
| `FIO_MAP_HASH_FN(key)` | _(not defined)_ | When defined, the map computes its own hash from the API key. Callers no longer pass a `hash` argument. |
| `FIO_MAP_RECALC_HASH` | `0` | Set to `1` to skip caching the hash per-node (saves 8 bytes/node). Requires `FIO_MAP_HASH_FN`. |

When `FIO_MAP_HASH_FN` is **not** defined, every `get`/`set`/`remove` call takes an explicit `uint64_t hash` argument — letting you salt the hash per-map for defence against hash-flooding.

### Ordering

| Macro | Default | Effect |
|---|---|---|
| `FIO_MAP_ORDERED` | `0` | Set to `1` (or define without a value) for insertion-order (FIFO) iteration and `evict`. Adds 8 bytes per node. |
| `FIO_MAP_LRU` | _(not defined)_ | Implies `FIO_MAP_ORDERED 1` and keeps LRU-style iteration order. The value is ignored. Does **not** auto-evict; call `MAP_evict()` yourself to enforce a size limit. |

### Limits

| Macro | Default | Effect |
|---|---|---|
| `FIO_MAP_ATTACK_LIMIT` | `16` | Max full hash collisions before the map assumes a hash-flooding attack and starts overwriting stale entries. |
| `FIO_MAP_CAPA_BITS_LIMIT` | `31` | Max exponent for internal capacity (2^31 ≈ 2 billion elements). Cannot exceed 31 without rewriting internal code. |

---

## Initialization

```c
MAP_s m = FIO_MAP_INIT;      /* stack — zero-initialize */

MAP_s *p = MAP_new();        /* heap — allocate + zero-initialize */
```

`FIO_MAP_INIT` expands to `{0}`.

---

## Lifecycle

```c
MAP_s *MAP_new(void);          /* allocate + initialize on the heap */
void   MAP_free(MAP_PTR map);  /* destroy elements + free the container */
void   MAP_destroy(MAP_PTR map); /* destroy elements, reset to empty (stack-safe) */
```

Use `MAP_destroy` for stack-allocated maps. Use `MAP_new` / `MAP_free` for heap.

---

## State

```c
uint32_t MAP_count(MAP_PTR map);       /* number of stored elements */
uint32_t MAP_capa (MAP_PTR map);       /* theoretical capacity (always a power of 2) */
void     MAP_reserve(MAP_PTR map, size_t capa); /* ensure at least capa slots exist */
void     MAP_compact(MAP_PTR map);     /* shrink allocation to fit current count */
```

---

## Get, Set, Remove

The exact signature depends on whether `FIO_MAP_HASH_FN` is defined and whether `FIO_MAP_VALUE` is defined. Below `[hash,]` means the `uint64_t hash` argument is present only when `FIO_MAP_HASH_FN` is **not** defined.

```c
/* Returns the stored value (or key for a Set). Zero-value if not found. */
MAP_GET_T MAP_get(MAP_PTR map, [uint64_t hash,] MAP_KEY key);

/* Insert or overwrite. Returns the resulting stored value/key.
   For hash maps: `old` (optional) receives the previous value before it is destroyed. */
MAP_GET_T MAP_set(MAP_PTR map, [uint64_t hash,]
                  MAP_KEY key [, MAP_VALUE val, MAP_VALUE_INTERNAL *old]);

/* Insert only if the key is absent. Returns current value/key. */
MAP_GET_T MAP_set_if_missing(MAP_PTR map, [uint64_t hash,]
                             MAP_KEY key [, MAP_VALUE val]);

/* Remove. Returns 0 on success, -1 if not found.
   `old` (optional): receives the removed value/key before it is destroyed. */
/* For hash maps: */
int MAP_remove(MAP_PTR map, [uint64_t hash,] MAP_KEY key,
               MAP_VALUE_INTERNAL *old);

/* For sets: */
int MAP_remove(MAP_PTR map, [uint64_t hash,] MAP_KEY key,
               MAP_KEY_INTERNAL *old);

/* Evict `n` elements. Order depends on FIO_MAP_LRU / FIO_MAP_ORDERED. */
void MAP_evict(MAP_PTR map, size_t n);

/* Remove all elements without freeing the internal buffer. */
void MAP_clear(MAP_PTR map);
```

---

## Low-Level Node Access

`MAP_get_ptr` and `MAP_set_ptr` return a pointer into internal storage. Use when you need to mutate a value in place or avoid a copy.

```c
/* Returns internal node pointer, or NULL if missing. */
MAP_node_s *MAP_get_ptr(MAP_PTR map, [uint64_t hash,] MAP_KEY key);

/* Core insert/overwrite. `overwrite=1` replaces an existing value; `overwrite=0` keeps it.
   Returns internal node pointer, or NULL on error. */
MAP_node_s *MAP_set_ptr(MAP_PTR map, [uint64_t hash,]
                        MAP_KEY key [, MAP_VALUE val,
                        MAP_VALUE_INTERNAL *old, int overwrite]);
```

Accessors for node pointers:

```c
MAP_KEY           MAP_node2key    (MAP_node_s *node); /* external key   */
MAP_VALUE         MAP_node2val    (MAP_node_s *node); /* external value (or key for Sets) */
uint64_t          MAP_node2hash   (MAP_node_s *node); /* hash value     */
MAP_KEY_INTERNAL  *MAP_node2key_ptr(MAP_node_s *node); /* pointer to internal key   */
MAP_VALUE_INTERNAL*MAP_node2val_ptr(MAP_node_s *node); /* pointer to internal value */
```

All return a zeroed/NULL result when `node` is NULL.

---

## Iteration

### `FIO_MAP_EACH` / `FIO_MAP_EACH_REVERSED` — for-loop macros (preferred)

```c
FIO_MAP_EACH(map_name, map_ptr, i) {
  /* i.key   — current key  (API type) */
  /* i.value — current value (API type; absent for Sets) */
  /* i.hash  — current hash  (absent if FIO_MAP_RECALC_HASH is set) */
}

FIO_MAP_EACH_REVERSED(map_name, map_ptr, i) { /* same, backward */ }
```

`i` is declared inside the loop as a `MAP_iterator_s`.

**Example:**

```c
#define FIO_MAP_NAME     wcount
#define FIO_MAP_KEY_KSTR         /* fio_str_info_s API keys, fio_keystr_s storage */
#define FIO_MAP_VALUE    size_t
#define FIO_MAP_HASH_FN(k) fio_risky_hash((k).buf, (k).len, (uintptr_t)wcount_destroy)
#include "fio-stl.h"

void print_counts(wcount_s *m) {
  FIO_MAP_EACH(wcount, m, it)
    printf("%.*s => %zu\n", (int)it.key.len, it.key.buf, it.value);
}
```

### Manual iterator API

```c
/* Returns the first iterator (NULL current_pos) or the next one. */
MAP_iterator_s MAP_get_next(MAP_PTR map, MAP_iterator_s *current_pos);

/* Reverse: returns the last (NULL) or the previous one. */
MAP_iterator_s MAP_get_prev(MAP_PTR map, MAP_iterator_s *current_pos);

/* Returns 1 if valid, 0 if exhausted or invalidated. */
int MAP_iterator_is_valid(MAP_iterator_s *iter);

/* Returns the internal node pointer for an iterator. */
MAP_node_s *MAP_iterator2node(MAP_PTR map, MAP_iterator_s *iter);
```

Modifying the map (insert, rehash) between iterator steps is safe but may invalidate or reorder iterators for unordered maps.

### Callback API

```c
/* Calls task(info) for each element starting at `start_at`.
   Returning -1 from the callback stops the loop.
   Returns the final position (items processed + start_at). */
uint32_t MAP_each(MAP_PTR map,
                  int (*task)(MAP_each_s *info),
                  void *udata,
                  ssize_t start_at);
```

The callback receives a `MAP_each_s` pointer with:

```c
typedef struct {
  MAP_PTR const parent;   /* the map being iterated */
  uint64_t      index;    /* current element index  */
  int (*task)(MAP_each_s *); /* the callback (may be swapped mid-loop) */
  void         *udata;    /* opaque user data */
  MAP_VALUE     value;    /* current value (absent for Sets) */
  MAP_KEY       key;      /* current key  */
} MAP_each_s;
```

---

## Practical Examples

### String-key dictionary (default keys, bstr values)

```c
#define FIO_MAP_NAME       dict
#define FIO_MAP_VALUE_BSTR        /* string values via fio_bstr */
#include "fio-stl.h"

/* Wrap set/get to salt the hash with the map pointer for security. */
static inline fio_str_info_s dict_put(dict_s *m, fio_str_info_s k, fio_str_info_s v) {
  return dict_set(m, fio_risky_hash(k.buf, k.len, (uint64_t)(uintptr_t)m), k, v, NULL);
}
static inline fio_str_info_s dict_fetch(dict_s *m, fio_str_info_s k) {
  return dict_get(m, fio_risky_hash(k.buf, k.len, (uint64_t)(uintptr_t)m), k);
}

void example(void) {
  dict_s d = FIO_MAP_INIT;
  dict_put(&d, FIO_STR_INFO1("hello"), FIO_STR_INFO1("world"));
  fio_str_info_s v = dict_fetch(&d, FIO_STR_INFO1("hello"));
  printf("%.*s\n", (int)v.len, v.buf);  /* "world" */
  dict_destroy(&d);
}
```

### Automatic hashing (FIO_MAP_HASH_FN)

```c
#define FIO_MAP_NAME     imap
#define FIO_MAP_KEY      int
#define FIO_MAP_VALUE    float
#define FIO_MAP_HASH_FN(k) fio_risky_num((uint64_t)(k), 0)
#include "fio-stl.h"

void example(void) {
  imap_s m = FIO_MAP_INIT;
  imap_set(&m, 42, 3.14f, NULL);
  printf("%f\n", imap_get(&m, 42));  /* 3.14 */
  imap_destroy(&m);
}
```

### LRU cache (manual eviction)

```c
#define FIO_MAP_NAME       cache
#define FIO_MAP_KEY_KSTR           /* efficient internal string storage */
#define FIO_MAP_VALUE      void *
#define FIO_MAP_LRU                       /* enables LRU ordering; value is ignored */
#define FIO_MAP_HASH_FN(k) fio_risky_hash((k).buf, (k).len, 0)
#include "fio-stl.h"
```

`FIO_MAP_LRU` only controls ordering. Enforce the size limit yourself after inserting:

```c
static inline void cache_put(cache_s *c, fio_str_info_s k, void *v) {
  cache_set(c, k, v, NULL);
  if (cache_count(c) > (1ULL << 12))
    cache_evict(c, 1);
}
```

---

## Notes

- **`210 map2.h`** is a legacy implementation. It is not documented here and should not be used in new code.
- Pointer tagging (`FIO_PTR_TAG`) and reference counting (`FIO_REF_NAME`) are supported, same as all 200-range modules.
- The map internally stores a compact 8-bit hash fingerprint alongside each node to speed up probing. Full hash is cached per-node unless `FIO_MAP_RECALC_HASH 1` is set.
- Against hash-flooding: after `FIO_MAP_ATTACK_LIMIT` full collisions the map falls back to overwriting stale entries. Always salt hashes with a per-map value (e.g., the map pointer) when keys come from untrusted input.

---

See also: [← 200 types-overview.md](./200 types-overview.md) · [201 string.md](./201 string.md) · [202 array.md](./202 array.md)
# Reference Counting Wrapper — Module 249

Heap-allocate any type with an atomic reference count attached, generated from a name macro.

See also: [← 200 types-overview.md](./200 types-overview.md)

---

## Setup

```c
#define FIO_REF_NAME  my_obj           /* required */
#define FIO_REF_TYPE  my_obj_s         /* optional — defaults to FIO_REF_NAME_s */
#define FIO_REF_CONSTRUCTOR_ONLY       /* optional — see below */
#include "fio-stl.h"
```

This generates a thin wrapper struct that sits **before** the `FIO_REF_TYPE` object in memory. The caller only ever sees a `FIO_REF_TYPE *` (or the tagged pointer type if `FIO_PTR_TAG_TYPE` is set). The reference count, optional metadata, and optional flex-array length are all hidden in the prefix.

Throughout this document `REF` stands for whatever name you chose (e.g. `my_obj`).

> **Requires:** the `FIO_ATOMIC` helpers must be available (included automatically when using `fio-stl.h` as a single header).

---

## Configuration Macros

Set these **before** `#include`, after `FIO_REF_NAME`.

### Wrapped type

| Macro | Default | Effect |
|---|---|---|
| `FIO_REF_TYPE` | `FIO_REF_NAME_s` | The C type to wrap and reference-count. |

### Constructor / destructor naming

| Macro | Effect |
|---|---|
| _(not defined)_ | Generates `REF_new2`, `REF_dup2`, `REF_free2` — the `2` suffix leaves room for a separately-defined `REF_new` / `REF_free`. |
| `FIO_REF_CONSTRUCTOR_ONLY` | Generates `REF_new`, `REF_dup`, `REF_free` — use this when the ref-counted constructor **is** the primary constructor. |

### Object lifecycle hooks

| Macro | Default | Effect |
|---|---|---|
| `FIO_REF_INIT(obj)` | zero-fill if allocator doesn't guarantee it | Called on the newly allocated `FIO_REF_TYPE` object after allocation. `obj` is the dereferenced object (not a pointer). |
| `FIO_REF_DESTROY(obj)` | _(nothing)_ | Called on the `FIO_REF_TYPE` object just before memory is freed (when the last reference drops). |

### Metadata

| Macro | Default | Effect |
|---|---|---|
| `FIO_REF_METADATA` | _(not defined)_ | A type to embed as hidden metadata alongside the ref-counted object. Access it via `REF_metadata()`. |
| `FIO_REF_METADATA_INIT(meta)` | zero-fill if needed | Called on the metadata field after allocation. |
| `FIO_REF_METADATA_DESTROY(meta)` | _(nothing)_ | Called on the metadata field just before memory is freed. |

### Flexible array

| Macro | Default | Effect |
|---|---|---|
| `FIO_REF_FLEX_TYPE` | _(not defined)_ | When defined, the constructor allocates extra memory for a `FIO_REF_FLEX_TYPE[]` array immediately after the main struct. The constructor accepts a `size_t members` argument and stores the count internally. The `members` value is also available inside `FIO_REF_INIT`. |

> **Note:** `FIO_REF_FLEX_TYPE` shrinks the reference counter to 32 bits (instead of the native word size). Do not combine this with a custom `FIO_MEM_FREE` that depends on the byte-count argument, because the flex free call reports a size that omits `sizeof(FIO_REF_TYPE)`.

### Pointer tagging

If `FIO_PTR_TAG_TYPE` is defined (globally, before including), all generated functions accept and return `FIO_PTR_TAG_TYPE` instead of `FIO_REF_TYPE *`. Tags are applied on allocation (`FIO_PTR_TAG`) and stripped internally (`FIO_PTR_UNTAG`).

---

## Generated API

### Constructor

```c
/* standard */
FIO_REF_TYPE *REF_new(void);                  /* FIO_REF_CONSTRUCTOR_ONLY */
FIO_REF_TYPE *REF_new2(void);                 /* default */

/* with FIO_REF_FLEX_TYPE */
FIO_REF_TYPE *REF_new(size_t members);
FIO_REF_TYPE *REF_new2(size_t members);
```

Allocates and zero-initializes the wrapper header, calls `FIO_REF_METADATA_INIT` (if any), then calls `FIO_REF_INIT` on the wrapped object. Returns `NULL` on allocation failure. Initial reference count is **1**.

### Increment reference count

```c
FIO_REF_TYPE *REF_dup(const FIO_REF_TYPE *obj);    /* FIO_REF_CONSTRUCTOR_ONLY */
FIO_REF_TYPE *REF_dup2(const FIO_REF_TYPE *obj);   /* default */
```

Atomically increments the reference count. Returns the same pointer, or `NULL` if the input is `NULL`. Thread-safe.

### Decrement reference count / free

```c
void REF_free(FIO_REF_TYPE *obj);             /* FIO_REF_CONSTRUCTOR_ONLY */
void REF_free2(FIO_REF_TYPE *obj);            /* default */
```

Atomically decrements the reference count. When the count reaches zero, calls `FIO_REF_DESTROY(object)`, then `FIO_REF_METADATA_DESTROY(metadata)` (if any), then frees the backing memory. Thread-safe. No-op on `NULL`.

### Metadata access

```c
FIO_REF_METADATA *REF_metadata(FIO_REF_TYPE *obj);
```

Returns a pointer to the hidden metadata field. Only generated when `FIO_REF_METADATA` is defined.

### Flex array length

```c
uint32_t REF_metadata_flex_len(FIO_REF_TYPE *obj);
```

Returns the number of `FIO_REF_FLEX_TYPE` members allocated alongside the object. Only generated when `FIO_REF_FLEX_TYPE` is defined. Returns `0` for `NULL`.

### Debugging helper

```c
size_t REF_references(FIO_REF_TYPE *obj);
```

Returns the current reference count. **Do not use for program logic** — the value is inherently unstable in a concurrent context. Useful for assertions and leak-hunting.

---

## Examples

### Basic reference-counted object

```c
typedef struct { int x; int y; } point_s;

#define FIO_REF_NAME             point
#define FIO_REF_TYPE             point_s
#define FIO_REF_CONSTRUCTOR_ONLY          /* point_new / point_dup / point_free */
#define FIO_REF_INIT(obj)        (obj).x = 0; (obj).y = 0
#include "fio-stl.h"

void example(void) {
    point_s *p = point_new();     /* ref count = 1 */
    p->x = 10; p->y = 20;

    point_s *alias = point_dup(p); /* ref count = 2 */

    point_free(alias);            /* ref count = 1 — not freed yet */
    point_free(p);                /* ref count = 0 — freed */
}
```

### Using the `2`-suffix when a primary constructor already exists

```c
/* Suppose FIO_STR_NAME already generated str_new / str_free. */
#define FIO_REF_NAME  str
#define FIO_REF_TYPE  str_s
/* No FIO_REF_CONSTRUCTOR_ONLY — generates str_new2 / str_dup2 / str_free2 */
#define FIO_REF_DESTROY(obj) str_destroy(&(obj))
#include "fio-stl.h"

str_s *s = str_new2();           /* ref-counted allocation */
str_write(s, "hello", 5);        /* use the string API directly */
str_s *s2 = str_dup2(s);
str_free2(s2);
str_free2(s);                    /* calls str_destroy, then frees memory */
```

### Hidden metadata

```c
typedef struct { char name[64]; } widget_s;
typedef struct { uint64_t created_at; } widget_meta_s;

#define FIO_REF_NAME              widget
#define FIO_REF_TYPE              widget_s
#define FIO_REF_CONSTRUCTOR_ONLY
#define FIO_REF_METADATA          widget_meta_s
#define FIO_REF_METADATA_INIT(m)  (m).created_at = fio_time_real().tv_sec
#include "fio-stl.h"

void example(void) {
    widget_s *w = widget_new();
    widget_meta_s *m = widget_metadata(w);
    printf("created at: %llu\n", (unsigned long long)m->created_at);
    widget_free(w);
}
```

### Flexible array suffix

```c
typedef struct { size_t len; double data[]; } vec_s;

#define FIO_REF_NAME              vec
#define FIO_REF_TYPE              vec_s
#define FIO_REF_CONSTRUCTOR_ONLY
#define FIO_REF_FLEX_TYPE         double
#define FIO_REF_INIT(obj)         (obj).len = members  /* 'members' is in scope */
#include "fio-stl.h"

void example(void) {
    vec_s *v = vec_new(8);       /* allocates vec_s + 8 doubles */
    uint32_t n = vec_metadata_flex_len(v); /* returns 8 */
    for (uint32_t i = 0; i < n; i++) v->data[i] = (double)i;
    vec_free(v);
}
```

---

## Memory Layout

```
 [ _wrapper_s header ] [ FIO_REF_TYPE object ] [ FIO_REF_FLEX_TYPE[] ] (optional)
       ^                        ^
   hidden prefix         pointer returned to caller
```

The `_wrapper_s` header holds the reference count (and optional `flx_size` / `metadata` fields). Callers never see it — they only receive a pointer to the `FIO_REF_TYPE` that immediately follows.

---

## Notes

- All reference-count operations use **atomic** instructions and are thread-safe.
- The module must be placed after any other type modules it wraps (e.g., after `FIO_STR_NAME` or `FIO_ARRAY_NAME` inclusions) when used together in one translation unit.
- Each `FIO_REF_NAME` definition is independent: you can wrap multiple types in the same file.
- `REF_references()` is a debugging aid; its return value is transient and must not drive control flow.
# FIOBJ — Soft Dynamic Types — Module 250

A unified soft-type system built on pointer tagging. One `FIOBJ` handle covers `null`, `true`, `false`, integers, floats, strings, arrays, and hash maps — plus user-defined extensions.

See also: [← 200 types-overview.md](./200 types-overview.md)

---

## Setup

```c
#define FIO_FIOBJ
#include "fio-stl.h"
```

No name macro is needed — `FIO_FIOBJ` activates the entire soft-type system in one step.

To expose symbols across multiple translation units add `FIO_EXTERN` before the include everywhere, and `FIO_EXTERN_COMPLETE` in exactly one `.c` file.

---

## How it Works

`FIOBJ` is a tagged pointer (`uintptr_t`). The bottom 3 bits encode the type class; the remaining bits hold the value or a heap pointer. Small integers and many floats require **no heap allocation** at all.

```
 bits[2:0]  type class
 bits[63:3] value or aligned pointer
```

All heap-allocated types are reference-counted. Primitives, small numbers, and many floats are immediate values — `fiobj_dup` / `fiobj_free` are no-ops for them.

The system uses an **ownership** model:
- Placing a value into an Array or Hash Map **transfers ownership** to the container.
- Freeing the container frees its elements (Arrays own members; Hash Maps own keys and values).
- Call `fiobj_dup` before inserting if you need to keep an independent reference.

---

## Configuration Macros

Set these before `#include` to override defaults.

### Nesting / recursion

| Macro | Default | Effect |
|---|---|---|
| `FIOBJ_MAX_NESTING` | `512` | Maximum depth for `fiobj_each2` and JSON serialization/parsing. Does **not** apply to `fiobj_free`. |
| `FIOBJ_JSON_APPEND` | `1` | When `1`, JSON parsing appends to existing containers; when `0`, it replaces them. |

### Type-name prefixes

All generated type names can be renamed by defining the following macros before the include. The defaults produce the API names used throughout this document.

| Macro | Default | Resulting prefix |
|---|---|---|
| `FIOBJ___NAME_NULL` | `null` | `fiobj_null` |
| `FIOBJ___NAME_NUMBER` | `num` | `fiobj_num_*` |
| `FIOBJ___NAME_FLOAT` | `float` | `fiobj_float_*` |
| `FIOBJ___NAME_STRING` | `str` | `fiobj_str_*` |
| `FIOBJ___NAME_ARRAY` | `array` | `fiobj_array_*` |
| `FIOBJ___NAME_HASH` | `hash` | `fiobj_hash_*` |

---

## Type Identification

### Type constants

| Constant | Value | Description |
|---|---|---|
| `FIOBJ_T_INVALID` | `0` | Invalid / uninitialized FIOBJ |
| `FIOBJ_T_PRIMITIVE` | `2` | Raw pointer-tag class for all primitives |
| `FIOBJ_T_NULL` | `2` | `null` primitive |
| `FIOBJ_T_TRUE` | `18` | `true` primitive |
| `FIOBJ_T_FALSE` | `34` | `false` primitive |
| `FIOBJ_T_NUMBER` | `0x01` | Integer (small integers are unboxed) |
| `FIOBJ_T_FLOAT` | `0x06` | Float / double (many values are unboxed) |
| `FIOBJ_T_STRING` | `0x03` | Dynamic string |
| `FIOBJ_T_ARRAY` | `0x04` | Dynamic array |
| `FIOBJ_T_HASH` | `0x05` | Ordered hash map |
| `FIOBJ_T_OTHER` | `0x07` | Extension / user-defined type |
| `FIOBJ_INVALID` | `0` | The invalid FIOBJ sentinel value |

### Type-check macros

```c
size_t         FIOBJ_TYPE(FIOBJ o);              /* full type ID, including extensions */
int            FIOBJ_TYPE_IS(FIOBJ o, type);     /* 1 if type matches */
fiobj_class_en FIOBJ_TYPE_CLASS(FIOBJ o);        /* raw 3-bit class (no extension lookup) */
int            FIOBJ_IS_INVALID(FIOBJ o);        /* true if o == FIOBJ_INVALID (0) */
int            FIOBJ_IS_NULL(FIOBJ o);           /* true if INVALID or null primitive */
```

`FIOBJ_TYPE` is the safe, recommended way to check type. `FIOBJ_TYPE_CLASS` is faster but may return `FIOBJ_T_OTHER` for heap-boxed numbers and floats. It also returns `FIOBJ_T_PRIMITIVE` (`2`) for `null`, `true`, and `false` alike — use `FIOBJ_TYPE` to distinguish primitives.

---

## Lifecycle — Reference Counting

```c
FIOBJ fiobj_dup(FIOBJ o);    /* increment ref count; returns o */
void  fiobj_free(FIOBJ o);   /* decrement ref count; frees when zero */
```

Primitives, unboxed integers, and unboxed floats are immune — `fiobj_dup` / `fiobj_free` are no-ops for them. Always call `fiobj_free` on every FIOBJ you own.

**Warning:** `fiobj_free` is recursive. Deeply nested structures can overflow the stack. `FIOBJ_MAX_NESTING` does **not** protect it. Use the JSON parser (which does protect against nesting attacks) when handling untrusted input.

```c
/* build a small object graph */
FIOBJ arr = fiobj_array_new();
fiobj_array_push(arr, fiobj_num_new(42));      /* array owns the number */
fiobj_array_push(arr, fiobj_str_new_cstr("hi", 2)); /* array owns the string */

FIOBJ ref = fiobj_dup(arr);   /* arr and ref both point to same array */
fiobj_free(ref);              /* decrement; array still alive */
fiobj_free(arr);              /* decrement to zero → frees array + contents */
```

---

## Generic Accessors

These work on **any** FIOBJ type.

```c
fio_str_info_s fiobj2cstr(FIOBJ o);      /* string representation (temporary) */
intptr_t       fiobj2i(FIOBJ o);         /* integer representation */
double         fiobj2f(FIOBJ o);         /* float representation */
uint64_t       fiobj2hash(FIOBJ o);      /* hash value (for use as map key) */
unsigned char  fiobj_is_eq(FIOBJ a, FIOBJ b); /* deep equality */
```

Conversion behaviour by type:

| Object type | `fiobj2i` | `fiobj2f` | `fiobj2cstr` |
|---|---|---|---|
| `null` / invalid | `0` | `0.0` | `"null"` / `""` |
| `true` | `1` | `1.0` | `"true"` |
| `false` | `0` | `0.0` | `"false"` |
| Number | the integer | cast to double | base-10 string |
| Float | floor to integer | the double | decimal string |
| String | `fio_atol` of content | `fio_atof` of content | raw content |
| Array | element count | element count as double | `"[...]"` |
| Hash | key-value pair count | count as double | `"{...}"` |

`fiobj_is_eq` performs deep structural comparison for Arrays and Hash Maps.

`fiobj2cstr` returns a temporary view. For numbers/floats the buffer is thread-local (safe for up to 128 concurrent threads). For heap strings it points directly into the string's buffer.

---

## Primitives — null, true, false

The three primitives are immediate constants. They never allocate memory and never need `fiobj_free`.

```c
FIOBJ fiobj_null(void);    /* returns the null primitive */
FIOBJ fiobj_true(void);    /* returns the true primitive */
FIOBJ fiobj_false(void);   /* returns the false primitive */
```

```c
FIOBJ v = fiobj_null();
if (FIOBJ_IS_NULL(v))        /* also true for FIOBJ_INVALID (0) */
    printf("got null\n");

if (FIOBJ_TYPE_IS(v, FIOBJ_T_NULL))   /* strictly the null primitive */
    printf("strict null\n");
```

---

## Numbers — `fiobj_num_*`

Small integers are stored in the tagged pointer itself — no allocation. Large values that don't fit are heap-boxed transparently.

```c
FIOBJ         fiobj_num_new(intptr_t i);     /* create */
intptr_t      fiobj_num2i(FIOBJ i);          /* read as integer */
double        fiobj_num2f(FIOBJ i);          /* read as double */
fio_str_info_s fiobj_num2cstr(FIOBJ i);      /* base-10 string (temp) */
void          fiobj_num_free(FIOBJ i);       /* type-specific free */
```

`fiobj_num_free` is a fast alternative to `fiobj_free` when the type has already been validated.

---

## Floats — `fiobj_float_*`

Many double values fit in the tagged pointer (those whose lower 3 bits of the IEEE 754 representation are zero). Others are heap-boxed.

```c
FIOBJ          fiobj_float_new(double i);    /* create */
intptr_t       fiobj_float2i(FIOBJ i);       /* floor to integer */
double         fiobj_float2f(FIOBJ i);       /* read as double */
fio_str_info_s fiobj_float2cstr(FIOBJ i);    /* decimal string (temp) */
void           fiobj_float_free(FIOBJ i);    /* type-specific free */
```

---

## Strings — `fiobj_str_*`

FIOBJ strings wrap the [Dynamic String module (201)](./201 string.md). The full `fiobj_str_*` API mirrors the `STR_*` API — write, concat, printf, base64, JSON-escape, UTF-8 helpers, freeze, etc.

### Constructors

```c
FIOBJ fiobj_str_new(void);                          /* empty string */
FIOBJ fiobj_str_new_cstr(const char *ptr, size_t len); /* copy from C string */
FIOBJ fiobj_str_new_buf(size_t capa);               /* empty with reserved capacity */
FIOBJ fiobj_str_new_copy(FIOBJ original);           /* copy from any FIOBJ (via fiobj2cstr) */
```

### Common operations

```c
fio_str_info_s fiobj_str2cstr(FIOBJ s);             /* same as fiobj_str_info() */
void           fiobj_str_write(FIOBJ s, const char *buf, size_t len);
void           fiobj_str_printf(FIOBJ s, const char *fmt, ...);
void           fiobj_str_concat(FIOBJ dest, FIOBJ src); /* append src string */
void           fiobj_str_freeze(FIOBJ s);            /* mark immutable */
void           fiobj_str_free(FIOBJ s);              /* type-specific free */
```

### Stack-allocated temporary strings

```c
/* Empty temporary string on the stack */
FIOBJ_STR_TEMP_VAR(name);

/* Temporary string wrapping a static buffer (read-only view) */
FIOBJ_STR_TEMP_VAR_STATIC(name, buf, len);

/* Temporary string wrapping an existing read/write buffer */
FIOBJ_STR_TEMP_VAR_EXISTING(name, buf, len, capa);

/* Always call when done — releases any dynamic allocation */
FIOBJ_STR_TEMP_DESTROY(name);
```

Temporary strings live on the stack and are valid FIOBJ values. They must **not** be passed to containers or freed with `fiobj_free`.

```c
/* example: use a stack string as a hash key without heap allocation */
FIOBJ_STR_TEMP_VAR_STATIC(key, "name", 4);
FIOBJ val = fiobj_hash_get(hash, key);
FIOBJ_STR_TEMP_DESTROY(key);
```

---

## Arrays — `fiobj_array_*`

FIOBJ arrays wrap the [Dynamic Array module (202)](./202 array.md). The element type is `FIOBJ`; elements are owned by the array.

### Common operations

```c
FIOBJ  fiobj_array_new(void);
void   fiobj_array_free(FIOBJ a);
FIOBJ *fiobj_array_push(FIOBJ a, FIOBJ value);  /* append; transfers ownership; NULL on error */
int    fiobj_array_pop(FIOBJ a, FIOBJ *old);    /* remove last; stores in *old or frees if NULL */
FIOBJ  fiobj_array_get(FIOBJ a, int64_t index); /* access; NO ownership transfer */
FIOBJ *fiobj_array_set(FIOBJ a, int64_t index, FIOBJ value, FIOBJ *old); /* old in *old or freed */
int    fiobj_array_remove(FIOBJ a, int64_t index, FIOBJ *old); /* remove; optional ownership transfer */
uint32_t fiobj_array_count(FIOBJ a);
void   fiobj_array_concat(FIOBJ dest, FIOBJ src); /* appends copies of src elements */
```

Negative indices count from the end (`-1` = last element).

```c
FIOBJ a = fiobj_array_new();
fiobj_array_push(a, fiobj_num_new(1));
fiobj_array_push(a, fiobj_num_new(2));
fiobj_array_push(a, fiobj_str_new_cstr("three", 5));

printf("count: %u\n", fiobj_array_count(a));    /* 3 */
printf("first: %ld\n", fiobj2i(fiobj_array_get(a, 0))); /* 1 */

fiobj_free(a);   /* frees array and all three elements */
```

---

## Hash Maps — `fiobj_hash_*`

FIOBJ hash maps are **ordered** maps keyed and valued by FIOBJ. They wrap the [Hash Map module (210)](./210 map.md). The map owns stored keys and values. The low-level `fiobj_hash_set` duplicates FIOBJ keys, so the caller still owns its original key reference; C-string helpers copy the key string internally.

### Core operations

```c
FIOBJ fiobj_hash_new(void);
void  fiobj_hash_free(FIOBJ h);
uint32_t fiobj_hash_count(FIOBJ h);

/* FIOBJ-key variants (low level) */
FIOBJ fiobj_hash_set(FIOBJ h, FIOBJ key, FIOBJ value, FIOBJ *old);
FIOBJ fiobj_hash_get(FIOBJ h, FIOBJ key);
int   fiobj_hash_remove(FIOBJ h, FIOBJ key, FIOBJ *old);
```

### C-string key helpers (recommended)

```c
/* set: copies key string, transfers value ownership */
FIOBJ fiobj_hash_set2(FIOBJ h, const char *key, size_t len, FIOBJ value);

/* get: no allocation, no ownership transfer */
FIOBJ fiobj_hash_get2(FIOBJ h, const char *key, size_t len);

/* remove: returns 0 on success; puts old value in *old (or frees it if NULL) */
int   fiobj_hash_remove2(FIOBJ h, const char *key, size_t len, FIOBJ *old);
```

### Merge

```c
/* deep merge: arrays concatenate, nested hashes recurse, scalars overwrite;
   null/invalid values in src remove the matching key from dest. */
void fiobj_hash_update(FIOBJ dest, FIOBJ src);
```

```c
FIOBJ h = fiobj_hash_new();
fiobj_hash_set2(h, "name", 4, fiobj_str_new_cstr("Alice", 5));
fiobj_hash_set2(h, "age",  3, fiobj_num_new(30));

FIOBJ name = fiobj_hash_get2(h, "name", 4);
printf("%s\n", fiobj2cstr(name).buf);   /* Alice */

fiobj_free(h);   /* frees map, values, and key strings */
```

---

## Iteration

### Shallow — `fiobj_each1`

Iterates direct children of an Array or Hash Map.

```c
typedef struct fiobj_each_s {
  FIOBJ const parent;   /* the container being iterated */
  uint64_t    index;    /* current element index */
  int (*task)(struct fiobj_each_s *info);  /* callback (may be swapped mid-loop) */
  void       *udata;    /* caller-supplied data */
  FIOBJ       value;    /* current element value */
  FIOBJ       key;      /* current key (Hash Maps only) */
} fiobj_each_s;

uint32_t fiobj_each1(FIOBJ o,
                     int (*task)(fiobj_each_s *info),
                     void *udata,
                     int32_t start_at);
```

Return `-1` from the callback to stop early. Returns the stop position (`elements processed + start_at`).

```c
int print_item(fiobj_each_s *e) {
    printf("[%llu] %s\n", (unsigned long long)e->index,
           fiobj2cstr(e->value).buf);
    return 0;
}

fiobj_each1(arr, print_item, NULL, 0);
```

### Deep — `fiobj_each2`

Recursively walks the object and all nested containers, as if flattened.

```c
uint32_t fiobj_each2(FIOBJ o,
                     int (*task)(fiobj_each_s *info),
                     void *udata);
```

Respects `FIOBJ_MAX_NESTING`. The root object itself is also passed to the callback (`index == 0`, `parent == FIOBJ_INVALID`).

---

## JSON

### Serialize to JSON

```c
FIOBJ fiobj2json(FIOBJ dest, FIOBJ o, uint8_t beautify);
```

Returns a `FIOBJ` string containing the JSON representation of `o`. If `dest` is an existing FIOBJ string, the JSON is **appended** to it; otherwise a new string is created. Pass `beautify != 0` for indented output.

```c
FIOBJ obj = fiobj_json_parse2("{\"x\":1,\"y\":[2,3]}", 18, NULL);
FIOBJ json = fiobj2json(FIOBJ_INVALID, obj, 1);   /* beautified */
printf("%s\n", fiobj2cstr(json).buf);
fiobj_free(json);
fiobj_free(obj);
```

### Parse JSON

```c
/* parse a full JSON value */
FIOBJ fiobj_json_parse(fio_str_info_s str, size_t *consumed);

/* convenience macro */
#define fiobj_json_parse2(data, len, consumed) \
  fiobj_json_parse(FIO_STR_INFO2(data, len), consumed)
```

Returns `FIOBJ_INVALID` on parse error. If `consumed` is non-NULL it is set to the number of bytes read.

### Merge JSON into a Hash Map

```c
size_t fiobj_hash_update_json(FIOBJ hash, fio_str_info_s str);
size_t fiobj_hash_update_json2(FIOBJ hash, char *ptr, size_t len);
```

Parses JSON and merges the resulting key-value pairs into an existing hash map. Silently skips non-object JSON data. Returns bytes consumed (0 on error).

### JavaScript-style path lookup

```c
FIOBJ fiobj_json_find(FIOBJ object, fio_str_info_s notation);
#define fiobj_json_find2(object, str, length) \
  fiobj_json_find(object, FIO_STR_INFO2(str, length))
```

Finds a nested value using dot/bracket notation. For example `"[0].name"` returns the `name` field of the first array element. Returns a **temporary reference** — call `fiobj_dup` if you need to keep it.

```c
FIOBJ data = fiobj_json_parse2("[{\"name\":\"Bob\"}]", 16, NULL);
FIOBJ name = fiobj_json_find2(data, "[0].name", 8);
printf("%s\n", fiobj2cstr(name).buf);   /* Bob */
fiobj_free(data);
/* name was a temporary reference into data; do not free separately */
```

---

## Mustache Templates

Renders a compiled Mustache template using FIOBJ data (typically a Hash Map) as context.

```c
/* create a new FIOBJ string with the rendered output */
FIOBJ fiobj_mustache_build(fio_mustache_s *m, FIOBJ ctx);

/* append rendered output to an existing FIOBJ string */
FIOBJ fiobj_mustache_build2(fio_mustache_s *m, FIOBJ dest, FIOBJ ctx);
```

Both may return `FIOBJ_INVALID` if nothing was written and `dest` was empty/invalid. See the Mustache module for how to compile a template (`fio_mustache_s *`).

---

## Extending FIOBJ

Custom types integrate via a virtual function table tagged as `FIOBJ_T_OTHER`.

### Requirements

1. Choose a **unique** type ID ≥ 100. Values below 100 are reserved; values below 40 are illegal.
2. Populate a `FIOBJ_class_vtable_s` — all function pointers must be non-NULL.
3. Wrap the type with `FIO_REF_NAME` (see [249 reference counter.md](./249 reference counter.md)), setting `FIO_REF_METADATA` to `const FIOBJ_class_vtable_s *` and initialising it in `FIO_REF_METADATA_INIT`.
4. Apply pointer tagging so all pointers carry `FIOBJ_T_OTHER` in the low 3 bits.

```c
#define FIO_PTR_TAG(p)          FIOBJ_PTR_TAG(p, FIOBJ_T_OTHER)
#define FIO_PTR_UNTAG(p)        FIOBJ_PTR_UNTAG(p)
#define FIO_PTR_TAG_TYPE        FIOBJ
```

### Virtual function table

```c
typedef struct {
  size_t type_id;                          /* unique ID ≥ 100 */
  unsigned char (*is_eq)(FIOBJ a, FIOBJ b);
  fio_str_info_s (*to_s)(FIOBJ o);
  intptr_t       (*to_i)(FIOBJ o);
  double         (*to_f)(FIOBJ o);
  uint32_t       (*count)(FIOBJ o);        /* number of child elements */
  uint32_t       (*each1)(FIOBJ o, int (*task)(fiobj_each_s *), void *udata, int32_t start_at);
  void           (*free2)(FIOBJ o);        /* free when ref count reaches zero */
} FIOBJ_class_vtable_s;
```

All slots must be filled. If the type has no children, `count` should return `0` and `each1` should be a no-op that returns `0`.

### Minimal example

```c
#define FIOBJ_T_POINT 100UL

typedef struct { double x, y; } point_data_s;

static unsigned char point_eq(FIOBJ a, FIOBJ b) { /* ... */ return 0; }
static fio_str_info_s point_to_s(FIOBJ o)        { /* ... */ return (fio_str_info_s){0}; }
static intptr_t point_to_i(FIOBJ o)              { return 0; }
static double   point_to_f(FIOBJ o)              { return 0.0; }
static uint32_t point_count(FIOBJ o)             { return 0; (void)o; }
static uint32_t point_each1(FIOBJ o, int (*t)(fiobj_each_s *), void *u, int32_t s)
                                                 { return 0; (void)o;(void)t;(void)u;(void)s; }
static void     point_free2(FIOBJ o);            /* forward declaration */

static const FIOBJ_class_vtable_s FIOBJ___POINT_VTBL = {
    .type_id = FIOBJ_T_POINT,
    .is_eq   = point_eq,   .to_s  = point_to_s,
    .to_i    = point_to_i, .to_f  = point_to_f,
    .count   = point_count, .each1 = point_each1,
    .free2   = point_free2,
};

#define FIO_REF_NAME             fiobj_point
#define FIO_REF_TYPE             point_data_s
#define FIO_REF_CONSTRUCTOR_ONLY
#define FIO_REF_METADATA         const FIOBJ_class_vtable_s *
#define FIO_REF_METADATA_INIT(m) (m = &FIOBJ___POINT_VTBL)
#define FIO_PTR_TAG(p)           FIOBJ_PTR_TAG(p, FIOBJ_T_OTHER)
#define FIO_PTR_UNTAG(p)         FIOBJ_PTR_UNTAG(p)
#define FIO_PTR_TAG_TYPE         FIOBJ
#include "fio-stl.h"

static void point_free2(FIOBJ o) { fiobj_point_free(o); }

FIOBJ fiobj_point_new_xy(double x, double y) {
    FIOBJ o = fiobj_point_new();
    point_data_s *p = (point_data_s *)FIOBJ_PTR_UNTAG(o);
    p->x = x; p->y = y;
    return o;
}
```

---

## Debugging / Leak Detection

When compiled with `TEST`, `DEBUG`, or `FIO_LEAK_COUNTER` defined, two global counters track allocations:

```c
size_t FIOBJ_MARK_MEMORY_ALLOC_COUNTER;
size_t FIOBJ_MARK_MEMORY_FREE_COUNTER;
```

Call `FIOBJ_MARK_MEMORY_PRINT()` after all objects should be freed to log any leaks.
# IO and HTTP — Group Overview (400-range)

```c
#define FIO_IO      /* IO Reactor */
#define FIO_IPC     /* Inter-Process Communication */
#define FIO_PUBSUB  /* Pub/Sub */
#define FIO_REDIS   /* Redis engine */
#define FIO_HTTP    /* HTTP server / client */
#include "fio-stl.h"
```

The 400-range is where facil.io becomes a networked server. A single-threaded
evented IO reactor sits at the foundation, optional TLS plugs into it as a
transport hook, IPC ferries work between worker processes, PubSub builds
broadcast semantics on top of IPC, Redis extends PubSub across machines, and
HTTP closes the loop with a production-grade server that speaks HTTP/1.x,
WebSocket, and SSE.

---

## Stack Diagram

```
 ┌──────────────────────────────────────────────────────────────────┐
 │                      HTTP  (439 http.h)                           │
 │          fio_http_listen · fio_http_connect                        │
 │          HTTP/1.x · WebSocket upgrades · SSE (EventSource)        │
 └──────┬─────────────────────────────────────────┬─────────────────┘
        │ uses (internal)                          │ optional
        ▼                                          ▼
 ┌──────────────────────────────┐    ┌─────────────────────────────┐
 │  HTTP parsers  (431)          │    │     Pub/Sub  (420)           │
 │  · HTTP/1.1 parser            │    │  subscribe · publish         │
 │  · WebSocket parser (RFC6455) │    │  pattern matching · replay   │
 │  · HTTP Handle  (internal)    │    │  pluggable engine interface  │
 └──────────────────────────────┘    └──────────────┬──────────────┘
                                                     │ engine
                                      ┌──────────────▼──────────────┐
                                      │     Redis  (422 redis.h)     │
                                      │  connects via IO (master)    │
                                      │  workers route through IPC   │
                                      └──────────────┬──────────────┘
                                                     │
                                      ┌──────────────▼──────────────┐
                                      │      IPC  (404 ipc.h)        │
                                      │  worker ↔ master messages    │
                                      │  cluster RPC (multi-machine) │
                                      │  encrypted: ChaCha20-Poly1305│
                                      └──────────────┬──────────────┘
                                                     │ built on
 ┌───────────────────────────────────────────────────▼──────────────┐
 │                     IO Reactor  (401–403)                          │
 │  fio_io_s · fio_io_protocol_s    epoll / kqueue / poll            │
 │  fio_io_listen · fio_io_connect  fio_io_defer · fio_io_run_every  │
 │  fio_io_async_s (background thread pools for non-IO tasks)        │
 └──────────────────────────────────┬───────────────────────────────┘
                                    │ transport hooks (fio_io_functions_s)
                                    ▼
                         ┌─────────────────────────┐
                         │       TLS  (405)          │
                         │  OpenSSL 3.x  (preferred) │
                         │  or native TLS 1.3        │
                         │  auto-selected at build   │
                         └─────────────────────────┘
```

---

## Layer-by-Layer

### IO Reactor — 401 · 402 · 403

**Enable with:** `#define FIO_IO`  
**Docs:** [./401 io api.md](./401 io api.md)

The event loop. `fio_io_start(workers)` blocks and runs forever (or until
`fio_io_stop()`). With `workers > 0` it forks child processes; the master stays
alive to manage IPC and listeners while workers handle connections.

Key types and functions:

- `fio_io_s` — opaque IO handle (connection). Never store raw; use
  `fio_io_dup` / `fio_io_free` for lifetime management.
- `fio_io_protocol_s` — struct of callbacks (`on_attach`, `on_data`,
  `on_ready`, `on_shutdown`, `on_timeout`, `on_close`) shared by all
  connections of the same protocol family.
- `fio_io_listen(...)` — attach a listening socket with a protocol.
- `fio_io_connect(...)` — connect as a client.
- `fio_io_attach_fd(fd, protocol, udata, tls)` — adopt a raw fd.
- `fio_io_read / fio_io_write / fio_io_close` — IO operations (safe only
  inside callbacks or deferred tasks).
- `fio_io_defer(task, u1, u2)` — schedule a task on the IO thread (thread-safe).
- `fio_io_run_every(...)` — schedule a recurring timer.

`fio_io_async_s` / `fio_io_async_attach(q, threads)` spins up a background
thread pool for CPU-heavy or blocking work that must not block the IO loop.

---

### TLS — 405 openssl.h · 405 tls13.h

**Enable with:** `#define FIO_IO` (TLS backends auto-include when available)  
**Docs:** [./405 openssl.md](./405 openssl.md) · [./405 tls13.md](./405 tls13.md) *(planned)*

TLS is plugged into the reactor through `fio_io_functions_s` — a set of
transport callbacks (`build_context`, `free_context`, `start`, `read`, `write`,
`flush`, `finish`, `cleanup`) that replace the default socket calls.

The `fio_io_tls_s` context carries certificates, ALPN negotiation entries, and
trusted CA chains. It is built from `fio_io_tls_new()` / `fio_io_tls_cert_add()`
/ `fio_io_tls_alpn_add()` / `fio_io_tls_trust_add()`, or parsed directly from a
URL query string:

```c
fio_io_listen(.url = "0.0.0.0:443/?tls=./certs/", .protocol = &my_proto);
```

Two backends are available — OpenSSL 3.x (`fio_openssl_io_functions()`) and
the native TLS 1.3 engine (`fio_tls13_io_functions()`). The default is
selected automatically at build time; override with
`fio_io_tls_default_functions(&my_io_functions)` before `fio_io_start`.

> **Disambiguation:** `405` documents IO-layer TLS integration — how TLS wraps
> network connections in the reactor. The standalone TLS 1.3 crypto library
> (key schedule, record layer, handshake state machines, certificate helpers)
> lives separately at [./190 tls13.md](./190 tls13.md) and is what `405` builds on.

---

### IPC — 404 ipc.h

**Enable with:** `#define FIO_IPC`  
**Docs:** [./404 ipc.md](./404 ipc.md) *(planned)*

Workers need to talk to the master. IPC handles this over a Unix socket (or
optionally TCP for multi-machine clusters). Messages are reference-counted,
encrypted in transit (ChaCha20-Poly1305, keyed from the process secret), and
routed by flags:

| Macro | Delivery |
|-------|----------|
| `fio_ipc_call(...)` | master only |
| `fio_ipc_local(...)` | master + all local workers |
| `fio_ipc_cluster(...)` | master on every machine |
| `fio_ipc_broadcast(...)` | master + workers on every machine |

For cross-machine RPC, register a non-zero op-code with
`fio_ipc_opcode_register(...)` before `fio_io_start`. Function-pointer messages
("fast path") work only within a single machine.

---

### Pub/Sub — 420 pubsub.h

**Enable with:** `#define FIO_PUBSUB` (requires `FIO_IPC`)  
**Docs:** [./420 pubsub.md](./420 pubsub.md) *(planned)*

Pub/Sub delivers messages to named channels across all workers and, via
registered engines, across machines. The core API:

```c
fio_pubsub_subscribe(.channel = FIO_BUF_INFO1("news"),
                     .on_message = my_callback,
                     .io = client_io);

fio_pubsub_publish(.channel = FIO_BUF_INFO1("news"),
                   .message = FIO_BUF_INFO1("breaking!"));
```

Subscriptions can be owned by an `fio_io_s` (auto-cancelled on close) or by
the global environment. Pattern subscriptions and numeric namespace `filter`
values are supported. An `engine` field selects an external backend (Redis,
custom). Replay from a timestamp is supported when history engines are attached.

---

### Redis — 422 redis.h

**Enable with:** `#define FIO_REDIS` (requires `FIO_PUBSUB`)  
**Docs:** [./422 redis.md](./422 redis.md) *(planned)*

Redis integration as a PubSub engine *and* as a standalone command client.

Only the master process connects to the Redis server — workers route all
commands and publishes through IPC. This avoids multiplying Redis connections
and preserves correct subscribe/unsubscribe semantics.

```c
/* attach as a pub/sub engine */
fio_pubsub_engine_s *r = fio_redis_new(.url = "redis://localhost:6379");
/* send arbitrary Redis commands (not SUBSCRIBE/UNSUBSCRIBE) */
fio_redis_send(r, my_fiobj_command_array, my_reply_cb, NULL);
```

The return type is `fio_pubsub_engine_s *` — attach it to a pub/sub
subscription via the `engine` field. Reference counting
(`fio_redis_dup` / `fio_redis_free`) governs the engine lifetime.
All mutation runs on the IO thread via `fio_io_defer`.

---

### HTTP Parsers — 431 http1 parser.h · 431 websocket parser.h

**Enable with:** `#define FIO_HTTP1_PARSER` / `#define FIO_WEBSOCKET_PARSER`  
**Docs:** [./431 http1 parser.md](./431 http1 parser.md) · [./431 websocket parser.md](./431 websocket parser.md) *(planned)*

Zero-allocation, event-driven parsers — no internal buffering, no heap.

- **HTTP/1.1 parser** (`fio_http1_parse`): parses request/response lines,
  headers, and bodies (including chunked). Fires user-implemented callbacks
  (`fio_http1_on_method`, `fio_http1_on_url`, `fio_http1_on_header`,
  `fio_http1_on_body_chunk`, `fio_http1_on_complete`, …).

- **WebSocket parser** (`fio_websocket_parse`): RFC 6455 frame parser.
  Zero-allocation, cache-sized. Produces typed events
  (`FIO_WEBSOCKET_EV_DATA_CHUNK`, `FIO_WEBSOCKET_EV_CONTROL`,
  `FIO_WEBSOCKET_EV_MESSAGE_END`, `FIO_WEBSOCKET_EV_ERROR`).

These are the building blocks used internally by the HTTP layer. Direct use
is for protocol-level work or embedding the parsers in a custom IO protocol.

**HTTP Handle** (`431 http handle.h`, `#define FIO_HTTP_HANDLE`) is an
internal module — the `fio_http_s` request/response state object with header
cache, body (RAM or file), and logging support. It is fully covered by the
HTTP documentation; there is no separate public handle doc.

---

### HTTP — 439 http.h

**Enable with:** `#define FIO_HTTP`  
**Docs:** [./439 http.md](./439 http.md) *(planned)*

The top-level server and client. `fio_http_listen` registers an HTTP service
on the IO reactor; `fio_http_connect` opens an HTTP client connection.

```c
fio_http_listen("0.0.0.0:3000", .on_http = handle_request);
fio_io_start(0); /* or fio_io_start(4) for 4 worker processes */
```

The `fio_http_settings_s` struct wires up:
- `on_http` — HTTP request/response callback
- `on_open` / `on_message` / `on_close` — WebSocket lifecycle
- `on_eventsource` — SSE event callback
- `on_authenticate_websocket` / `on_authenticate_sse` — upgrade guards
- `tls` / `tls_io_func` — optional TLS context
- `queue` — optional `fio_io_async_s` for off-thread response work
- `public_folder` — static file serving with `gz` pre-compressed support

HTTP handles (`fio_http_s`) are created per request and carry headers, body,
and state. The `fio_http_s` lifetime is managed by the library; callbacks
receive a pointer valid only for the duration of the call (or while an
explicit reference is held).

---

## Threading and Reactor Model

The IO reactor is **single-threaded per process**. All protocol callbacks
(`on_data`, `on_ready`, etc.) run in the reactor thread of the process that
owns the connection. There is no locking required for IO operations.

**Process model** (when `fio_io_start(workers)` is called with `workers > 0`):

```
 master process
  ├── owns IPC listener socket
  ├── runs PRE_START state callbacks
  ├── forwards cluster / pub/sub to workers
  └── worker process × N
       ├── each gets its own IO reactor loop
       ├── accepts connections (listeners are dup'd per worker)
       └── communicates back via IPC socket
```

Workers are forked; the master sentinel thread monitors each child and
respawns crashed workers automatically (configurable via
`FIO_ASSERT_DEBUG` in debug builds to prevent respawn masking crashes).

**Deferred work** (`fio_io_defer`) posts tasks onto the reactor queue —
safe to call from any thread. The reactor processes the queue after each
poll cycle.

**Timers** (`fio_io_run_every`) schedule repeating or one-shot callbacks
aligned to the reactor tick (millisecond resolution). Timer precision depends
on the poll timeout and reactor load.

**Background threads** (`fio_io_async_s`): for blocking or CPU-intensive
work that must not stall the IO loop, attach a thread pool with
`fio_io_async_attach(q, thread_count)`. The pool starts and stops with the
reactor automatically.

**Connection lifecycle:**

```
fd created (accept / connect)
    │
    └─► fio_io_attach_fd → on_attach
            │
            ├─ on_data       (data available to read)
            ├─ on_ready      (outgoing buffer drained)
            ├─ on_timeout    (idle too long; default: close)
            │
            ├─ on_shutdown   (reactor shutting down)
            └─ on_close      (connection closed — cleanup here)
```

Outgoing writes are buffered. When the buffer exceeds
`FIO_IO_THROTTLE_LIMIT` (default 2 MiB), `on_data` events are suspended
until the buffer drains ("throttling"). The shutdown sequence gives
connections up to `FIO_IO_SHUTDOWN_TIMEOUT` (default 15 s) to flush before
force-closing.

**`fio_io_env_set` / `fio_io_env_get`**: attach named objects to the
connection's lifetime. The `on_close` callback fires automatically when the
connection closes, enabling RAII-style per-connection resource management.

---

## TLS Disambiguation

There are **two separate TLS documents** in this library:

| Document | What it covers |
|----------|----------------|
| [`./405 tls13.md`](./405 tls13.md) *(planned)* | IO-layer TLS integration — how TLS is plugged into the IO reactor via `fio_io_functions_s`. Use this when setting up TLS listeners or clients. |
| [`./190 tls13.md`](./190 tls13.md) | Standalone TLS 1.3 crypto library — key schedule, record layer, handshake state machines, alerts, KeyUpdate, certificate helpers. The raw machinery. |

If you are adding TLS to a server or client, start with the `405` docs.  
If you are implementing a custom TLS backend or need direct access to the TLS 1.3 internals, start with `./190 tls13.md`.

---

## Documentation Index

| Header | Module | Doc |
|--------|--------|-----|
| `401 io api.h` | IO Reactor API | [./401 io api.md](./401 io api.md) |
| `402 io types.h` | IO Types (internal) | covered by IO docs |
| `403 io reactor.h` | IO Reactor cycle (internal) | covered by IO docs |
| `404 ipc.h` | IPC | [./404 ipc.md](./404 ipc.md) |
| `405 openssl.h` | OpenSSL TLS backend | [./405 openssl.md](./405 openssl.md) |
| `405 tls13.h` | Native TLS 1.3 IO backend | [./405 tls13.md](./405 tls13.md) |
| `420 pubsub.h` | Pub/Sub | [./420 pubsub.md](./420 pubsub.md) |
| `422 redis.h` | Redis engine | [./422 redis.md](./422 redis.md) |
| `431 http1 parser.h` | HTTP/1.1 parser | [./431 http1 parser.md](./431 http1 parser.md) |
| `431 websocket parser.h` | WebSocket parser | [./431 websocket parser.md](./431 websocket parser.md) |
| `431 http handle.h` | HTTP Handle (internal) | covered by HTTP docs |
| `439 http.h` | HTTP server / client | [./439 http.md](./439 http.md) |
# IO Reactor API (401 io api.h)

```c
#define FIO_IO
#include FIO_INCLUDE_FILE
```

`FIO_IO` adds the facil.io evented IO reactor, connection handles, protocol
callbacks, listener/client helpers, TLS transport hooks, per-connection
lifetime storage, timers, and optional async worker queues.

See [./400 io-overview.md](./400 io-overview.md) for the full IO / IPC /
PubSub / HTTP stack. TLS backends are covered by
[./405 openssl.md](./405 openssl.md) and [./405 tls13.md](./405 tls13.md).
Socket helpers are documented in [./004 sock.md](./004 sock.md), timers and
queues in [./102 queue.md](./102 queue.md), and lifecycle callbacks in
[./004 state callbacks.md](./004 state callbacks.md).

---

## Configuration Macros

Define before including the header to override defaults.

| Macro | Default | Meaning |
|-------|---------|---------|
| `FIO_IO_BUFFER_PER_WRITE` | `65536U` | Stack buffer size used during write events. |
| `FIO_IO_THROTTLE_LIMIT` | `2097152U` | `on_data` is throttled while outgoing backlog is large. |
| `FIO_IO_TIMEOUT_MAX` | `300000` | Maximum and default connection timeout, in milliseconds. |
| `FIO_IO_SHUTDOWN_TIMEOUT` | `15000` | Hard timeout for the reactor shutdown loop, in milliseconds. |
| `FIO_IO_COUNT_STORAGE` | `1` in `DEBUG`, else `0` | Enables IO byte-count storage when compiled in. |

---

## Core Types

| Type | Role |
|------|------|
| `fio_io_s` | Opaque connection handle. Use it instead of raw file descriptors inside protocol callbacks. |
| `fio_io_protocol_s` | Shared protocol callback table for a family of connections. Usually static / global and zero-initialized. |
| `fio_io_functions_s` | Optional transport vtable used to override socket IO, mainly for TLS. |
| `fio_io_tls_s` | Reference-counted TLS settings object consumed by transport backends. |
| `fio_io_listener_s` | Listener handle returned by `fio_io_listen`. |
| `fio_pubsub_msg_s` | Message delivered to `on_pubsub` callbacks; defined by the Pub/Sub module. |
| `fio_io_async_s` | Async worker queue attached to the reactor lifecycle. |

---

## Reactor Lifecycle and State

### Start / stop

```c
void fio_io_start(int workers);
void fio_io_stop(void);
void fio_io_add_workers(int workers);
void fio_io_restart(int workers);
void fio_io_restart_on_signal(int signal);
size_t fio_io_shutdown_timeout(void);
size_t fio_io_shutdown_timeout_set(size_t milliseconds);
```

`fio_io_start(workers)` starts the reactor and blocks until shutdown. A positive
`workers` value forks worker processes. `fio_io_stop()` asks the reactor to
stop. `fio_io_add_workers` and `fio_io_restart` are cluster/process helpers;
`fio_io_restart_on_signal` binds hot restart to a signal.

`fio_io_shutdown_timeout_set` changes the hard shutdown grace period and returns
the value that will be used.

### State queries

```c
int fio_io_is_running(void);
int fio_io_is_master(void);
int fio_io_is_worker(void);
uint16_t fio_io_workers(int workers_requested);
int fio_io_pid(void);
int fio_io_root_pid(void);
int64_t fio_io_last_tick(void);
int64_t fio_io_last_tick_time(void);
```

`fio_io_last_tick()` is the cached millisecond value from the last reactor poll.
`fio_io_last_tick_time()` is the cached wall-clock millisecond timestamp,
useful for approximate log and HTTP date values.

---

## Protocols

```c
typedef struct fio_io_protocol_s fio_io_protocol_s;

struct fio_io_protocol_s {
  struct { /* reserved; initialize to zero */ } reserved;

  void (*on_attach)(fio_io_s *io);
  void (*on_data)(fio_io_s *io);
  void (*on_ready)(fio_io_s *io);
  void (*on_shutdown)(fio_io_s *io);
  void (*on_timeout)(fio_io_s *io);
  void (*on_close)(void *iobuf, void *udata);
  void (*on_pubsub)(fio_pubsub_msg_s *msg);

  void (*on_user1)(fio_io_s *io, void *user_data);
  void (*on_user2)(fio_io_s *io, void *user_data);
  void (*on_user3)(fio_io_s *io, void *user_data);
  void (*on_reserved)(fio_io_s *io, void *user_data);

  fio_io_functions_s io_functions;
  uint32_t timeout;
  uint32_t buffer_size;
};
```

Protocol structs define connection behavior. They are normally static/global and
shared by many IO handles. Initialize the full struct to zero and never mutate
`reserved`.

Callback flow:

1. `on_attach` after an IO is attached to the protocol.
2. `on_data` when incoming data can be read.
3. `on_ready` after all pending writes drain.
4. `on_timeout` when the connection timeout is reached.
5. `on_shutdown` immediately before reactor shutdown closes the connection.
6. `on_close(iobuf, udata)` after the connection is closed.

All protocol callbacks return `void`. Use `fio_io_close`,
`fio_io_close_now`, or protocol state to control what happens next. Set
`on_timeout = fio_io_touch` for connections where idle timeout should be
ignored.

`timeout` is in milliseconds, capped by `FIO_IO_TIMEOUT_MAX`; `0` means the
maximum/default. `buffer_size` controls the per-connection protocol buffer
returned by `fio_io_buffer`.

### Iterating protocol IOs

```c
size_t fio_io_protocol_each(fio_io_protocol_s *protocol,
                            void (*task)(fio_io_s *, void *udata2),
                            void *udata2);
```

Runs `task` for each IO using `protocol`. Call only from the main IO thread;
use `fio_io_defer` when scheduling from another thread.

---

## Transport Functions / TLS Hooks

```c
typedef struct fio_io_functions_s fio_io_functions_s;

struct fio_io_functions_s {
  void *(*build_context)(fio_io_tls_s *tls, uint8_t is_client);
  void (*free_context)(void *context);
  void (*start)(fio_io_s *io);
  ssize_t (*read)(fio_socket_i fd, void *buf, size_t len, void *context);
  ssize_t (*write)(fio_socket_i fd, const void *buf, size_t len, void *context);
  int (*flush)(fio_socket_i fd, void *context);
  void (*finish)(fio_socket_i fd, void *context);
  void (*cleanup)(void *context);
};
```

The vtable lets a protocol replace plain socket IO with a transport layer such
as TLS. Functions receive the file descriptor but must not keep it or defer fd
operations; the `fio_io_s` handle is the long-lived identity.

Return conventions:

- `read` behaves like non-blocking `read(2)`.
- `write` returns plaintext bytes accepted (`N > 0`), `0` for zero-length
  input, or `-1` with `errno` set. If data was transformed (for example,
  encrypted), return success even if the underlying socket later blocks; use
  `flush` for pending transformed bytes.
- `flush` returns `0` only when all internal output is empty. Non-zero (`N > 0`
  or `-1`) means pending data remains and the reactor should keep watching for
  writability.
- `finish` runs before closing after output is sent; `cleanup` releases the
  per-connection transport context after close.

Set default TLS transport functions before the reactor starts:

```c
fio_io_functions_s fio_io_tls_default_functions(fio_io_functions_s *funcs);
```

Passing `NULL` returns the current default. Passing a pointer sets a new
default and returns the selected functions.

---

## Listening and Connecting

### `fio_io_listen`

```c
typedef struct fio_io_listen_args_s {
  const char *url;
  fio_io_protocol_s *protocol;
  void *udata;
  fio_io_tls_s *tls;
  void (*on_start)(fio_io_protocol_s *protocol, void *udata);
  void (*on_stop)(fio_io_protocol_s *protocol, void *udata);
  fio_io_async_s *queue_for_accept;
  uint8_t on_root;
  uint8_t hide_from_log;
} fio_io_listen_args_s;

fio_io_listener_s *fio_io_listen(fio_io_listen_args_s args);
#define fio_io_listen(...) fio_io_listen((fio_io_listen_args_s){__VA_ARGS__})
```

Creates a network listener and returns a self-destructible listener handle, or
`NULL` on error. The default URL is `tcp://0.0.0.0:3000`.

Call it before `fio_io_start()` for normal server setup. The header notes that
this schedules a task and should not be called from `PRE_START` or `ON_START`
state callbacks.

TLS can be supplied directly with `.tls` or inferred from the URL query:

```c
fio_io_listen(.url = "0.0.0.0:3000/?tls", .protocol = &MY_PROTOCOL);
fio_io_listen(.url = "0.0.0.0:3000/?tls=./certs/", .protocol = &MY_PROTOCOL);
fio_io_listen(.url = "0.0.0.0:3000/?key=./key.pem&cert=./cert.pem",
              .protocol = &MY_PROTOCOL);
```

`.tls` ownership is moved to the listener. If you need to share a TLS settings
object, duplicate it first with `fio_io_tls_dup` and free your own references
with `fio_io_tls_free`.

Listener helpers:

```c
void fio_io_listen_stop(fio_io_listener_s *l);
fio_io_protocol_s *fio_io_listener_protocol(fio_io_listener_s *l);
void *fio_io_listener_udata(fio_io_listener_s *l);
void *fio_io_listener_udata_set(fio_io_listener_s *l, void *new_udata);
fio_buf_info_s fio_io_listener_url(fio_io_listener_s *l);
int fio_io_listener_is_tls(fio_io_listener_s *l);
```

### `fio_io_connect`

```c
typedef struct {
  const char *url;
  fio_io_protocol_s *protocol;
  void (*on_failed)(fio_io_protocol_s *protocol, void *udata);
  void *udata;
  fio_io_tls_s *tls;
  uint32_t timeout;
} fio_io_connect_args_s;

fio_io_s *fio_io_connect(fio_io_connect_args_s args);
#define fio_io_connect(url_, ...) \
  fio_io_connect((fio_io_connect_args_s){.url = url_, __VA_ARGS__})
```

Connects to `url` as a client and returns the IO handle or `NULL`. The URL may
contain TLS hints. `timeout` defaults to 30 seconds. `on_failed` is the cleanup
hook for failed connection attempts; established connections use the protocol
callbacks.

---

## IO Handles and Operations

```c
fio_io_s *fio_io_attach_fd(fio_socket_i fd,
                           fio_io_protocol_s *protocol,
                           void *udata,
                           void *tls);
fio_io_protocol_s *fio_io_protocol_set(fio_io_s *io,
                                       fio_io_protocol_s *protocol);
fio_io_protocol_s *fio_io_protocol(fio_io_s *io);
void *fio_io_buffer(fio_io_s *io);
size_t fio_io_buffer_len(fio_io_s *io);
void *fio_io_udata_set(fio_io_s *io, void *udata);
void *fio_io_udata(fio_io_s *io);
void *fio_io_tls_set(fio_io_s *io, void *tls);
void *fio_io_tls(fio_io_s *io);
fio_socket_i fio_io_fd(fio_io_s *io);
void fio_io_touch(fio_io_s *io);
size_t fio_io_read(fio_io_s *io, void *buf, size_t len);
void fio_io_close(fio_io_s *io);
void fio_io_close_now(fio_io_s *io);
fio_io_s *fio_io_dup(fio_io_s *io);
void fio_io_free(fio_io_s *io);
void fio_io_suspend(fio_io_s *io);
void fio_io_unsuspend(fio_io_s *io);
int fio_io_is_suspended(fio_io_s *io);
int fio_io_is_open(fio_io_s *io);
size_t fio_io_backlog(fio_io_s *io);
void fio_io_noop(fio_io_s *io);
```

`fio_io_attach_fd` adopts a valid socket into the reactor. It returns `NULL` on
error. The returned pointer must not be used arbitrarily; IO handles are valid
inside proper callbacks and scheduled tasks. If code must keep a handle outside
that context, call `fio_io_dup` and later `fio_io_free`. These two functions are
thread-safe.

`fio_io_protocol_set` installs a new protocol. `NULL` is a valid "only-write"
protocol. The accessor may temporarily return the old protocol while the change
is being attached.

`fio_io_read` returns bytes read. `0` is not EOF by itself; it can mean no data
was available on the non-blocking socket. Use close callbacks for final cleanup.

`fio_io_close` closes after scheduled data is sent. `fio_io_close_now` closes as
soon as possible. `fio_io_suspend` / `fio_io_unsuspend` control future
`on_data` delivery, and `fio_io_backlog` reports the approximate outgoing byte
count.

### Writing

```c
typedef struct {
  void *buf;
  intptr_t fd;
  size_t len;
  size_t offset;
  void (*dealloc)(void *);
  uint8_t copy;
} fio_io_write_args_s;

void fio_io_write2(fio_io_s *io, fio_io_write_args_s args);
#define fio_io_write2(io, ...) \
  fio_io_write2(io, (fio_io_write_args_s){__VA_ARGS__})
#define fio_io_write(io, buf_, len_) \
  fio_io_write2(io, .buf = (buf_), .len = (len_), .copy = 1)
#define fio_io_sendfile(io, source_fd, offset_, bytes) \
  fio_io_write2((io), .fd = (source_fd), .offset = (size_t)(offset_), .len = (bytes))
```

`fio_io_write2` schedules buffered output. If `.buf` is used and `.copy` is
non-zero, the data is copied immediately. If `.copy == 0` and `.dealloc` is set,
the IO layer takes ownership of the buffer and calls `dealloc` later. If
`.dealloc == NULL`, the buffer is not freed by the IO layer.

For file output, pass `.fd`; `.len == 0` means send the whole file. The
`fio_io_sendfile` helper closes `source_fd` after sending or on error.

---

## Scheduling Tasks and Timers

```c
void fio_io_defer(void (*task)(void *, void *), void *udata1, void *udata2);
void fio_io_run_every(fio_timer_schedule_args_s args);
#define fio_io_run_every(...) \
  fio_io_run_every((fio_timer_schedule_args_s){__VA_ARGS__})
fio_queue_s *fio_io_queue(void);
```

`fio_io_defer` schedules a task on the IO reactor queue and is thread-safe.
Use it to move work from other threads back to the IO thread.

`fio_io_run_every` schedules a timer. It uses `fio_timer_schedule_args_s` from
the queue/timer API:

- `fn` returns non-zero to stop the timer.
- `udata1` and `udata2` are passed to callbacks.
- `on_stop` runs when the timer ends.
- `every` is the interval in milliseconds.
- `repetitions` is the number of runs; `-1` means indefinitely.

`fio_io_queue()` returns the reactor queue.

---

## Connection Environment

The IO environment links named objects to a connection lifetime. When the IO is
closed, stored `on_close` callbacks run automatically.

```c
typedef struct {
  intptr_t type;
  fio_buf_info_s name;
  void *udata;
  void (*on_close)(void *data);
  uint8_t const_name;
} fio_io_env_set_args_s;

typedef struct {
  intptr_t type;
  fio_buf_info_s name;
} fio_io_env_get_args_s;

void *fio_io_env_get(fio_io_s *io, fio_io_env_get_args_s args);
void fio_io_env_set(fio_io_s *io, fio_io_env_set_args_s args);
int fio_io_env_unset(fio_io_s *io, fio_io_env_get_args_s args);
int fio_io_env_remove(fio_io_s *io, fio_io_env_get_args_s args);

#define fio_io_env_get(io, ...) \
  fio_io_env_get(io, (fio_io_env_get_args_s){__VA_ARGS__})
#define fio_io_env_set(io, ...) \
  fio_io_env_set(io, (fio_io_env_set_args_s){__VA_ARGS__})
#define fio_io_env_unset(io, ...) \
  fio_io_env_unset(io, (fio_io_env_get_args_s){__VA_ARGS__})
#define fio_io_env_remove(io, ...) \
  fio_io_env_remove(io, (fio_io_env_get_args_s){__VA_ARGS__})
```

`type` and `name` together identify an entry. Negative `type` values are
reserved. If `const_name` is set, the name string must outlive the environment.

If `io == NULL`, entries are stored in the global environment; their `on_close`
callbacks run when the process exits. `unset` detaches without calling
`on_close`; `remove` detaches and calls `on_close` as if the connection closed.

---

## TLS Settings Helpers

```c
fio_io_tls_s *fio_io_tls_new(void);
fio_io_tls_s *fio_io_tls_from_url(fio_io_tls_s *target_or_null, fio_url_s url);
fio_io_tls_s *fio_io_tls_dup(fio_io_tls_s *tls);
void fio_io_tls_free(fio_io_tls_s *tls);

fio_io_tls_s *fio_io_tls_cert_add(fio_io_tls_s *tls,
                                  const char *server_name,
                                  const char *public_cert_file,
                                  const char *private_key_file,
                                  const char *pk_password);
fio_io_tls_s *fio_io_tls_alpn_add(fio_io_tls_s *tls,
                                  const char *protocol_name,
                                  void (*on_selected)(fio_io_s *));
int fio_io_tls_alpn_select(fio_io_tls_s *tls,
                           const char *protocol_name,
                           size_t name_length,
                           fio_io_s *io);
fio_io_tls_s *fio_io_tls_trust_add(fio_io_tls_s *tls,
                                   const char *public_cert_file);
uintptr_t fio_io_tls_cert_count(fio_io_tls_s *tls);
uintptr_t fio_io_tls_alpn_count(fio_io_tls_s *tls);
uintptr_t fio_io_tls_trust_count(fio_io_tls_s *tls);
```

`fio_io_tls_s` stores backend-neutral TLS instructions. Add certificates for
SNI, ALPN protocol callbacks, and trusted certificates / CA stores. Passing
`NULL` certificate paths can request a backend-provided self-signed certificate;
passing `NULL` to `fio_io_tls_trust_add` asks the backend to use system trust.
Backend behavior is documented in the TLS backend docs.

`fio_io_tls_alpn_add` silently ignores a `NULL` protocol name and replaces a
`NULL` callback with a no-op. The first ALPN protocol added is the default.

### Iterating TLS settings

```c
typedef struct fio_io_tls_each_s {
  fio_io_tls_s *tls;
  void *udata;
  void *udata2;
  int (*each_cert)(struct fio_io_tls_each_s *,
                   const char *server_name,
                   const char *public_cert_file,
                   const char *private_key_file,
                   const char *pk_password);
  int (*each_alpn)(struct fio_io_tls_each_s *,
                   const char *protocol_name,
                   void (*on_selected)(fio_io_s *));
  int (*each_trust)(struct fio_io_tls_each_s *, const char *public_cert_file);
} fio_io_tls_each_s;

int fio_io_tls_each(fio_io_tls_each_s args);
#define fio_io_tls_each(tls_, ...) \
  fio_io_tls_each(((fio_io_tls_each_s){.tls = tls_, __VA_ARGS__}))
```

Transport backends use `fio_io_tls_each` to consume the stored instructions.
Iterator callbacks return `int`; check the backend using them for any additional
meaning assigned to non-zero return values.

### Peer certificate inspection (mTLS)

```c
int fio_io_peer_info_next(fio_io_s *io, fio_x509_cert_s *dest);
```

Trust configured with `fio_io_tls_trust_add` always refers to **peer**
verification: a server context verifies (and requires) client certificates,
a client context verifies the server certificate. An empty trust list means
no peer verification; passing `NULL` to `fio_io_tls_trust_add` selects the
system trust store.

Once the handshake completed, iterate the peer's certificate chain (leaf
first) to implement application level client certificate authentication and
authorization:

```c
fio_x509_cert_s cert = {0}; /* zeroed = new loop */
while (fio_io_peer_info_next(io, &cert) == 0) {
  if (!cert.verified)
    continue; /* chain failed TLS-level verification (or was skipped) */
  /* authorize by identity, e.g.: cert.cn,
   * cert.subject, or pin cert.fingerprint (32-byte SHA-256) */
}
```

The iterator is stateless — the position is identified from `dest` alone: a
zeroed `dest` (`der.buf == NULL`) starts a new loop; otherwise iteration
continues at `dest->chain_index + 1`. To restart a loop, zero the struct.
Multiple loops may iterate the same connection concurrently.
Iteration is capped at 128 certificates (`chain_index` 0..127) as a
deep-nesting / DoS guard — longer chains end the loop with -1.

Passing a `NULL` `dest` returns -1. The function returns
-1 when the iteration is done or when peer information is unavailable (no
TLS, handshake incomplete, peer sent no certificate, or the X509 module
missing).

Each call parses the next certificate into a `fio_x509_cert_s` (defined in
the X509 module, `156 x509.h`). All certificate fields point directly into
memory owned by the TLS backend — nothing is copied or allocated. The views
stay valid until the next `fio_io_peer_info_next` call (on ANY connection)
or connection close, whichever comes first; copy anything that must outlive
the loop.

The `verified` flag reflects the TLS backend's verification of the whole
chain and is identical for every certificate in the chain. Connections that
skipped verification (empty trust list) report `verified == 0`.

---

## Async Worker Queues

```c
typedef struct fio_io_async_s fio_io_async_s;
#define FIO_IO_ASYN_INIT ((fio_io_async_s){0})

fio_queue_s *fio_io_async_queue(fio_io_async_s *q);
void fio_io_async_attach(fio_io_async_s *q, uint32_t threads);
#define fio_io_async(q_, ...) fio_queue_push((q_)->q, __VA_ARGS__)
void fio_io_async_every(fio_io_async_s *q, fio_timer_schedule_args_s args);
#define fio_io_async_every(async, ...) \
  fio_io_async_every(async, (fio_timer_schedule_args_s){__VA_ARGS__})
```

Async queues are for non-IO work that must not block the reactor. Allocate the
`fio_io_async_s` object with static or otherwise reactor-long lifetime and
initialize it with `FIO_IO_ASYN_INIT` or zeroes before attaching it.

```c
static fio_io_async_s SLOW_TASKS = FIO_IO_ASYN_INIT;

int main(void) {
  fio_io_async_attach(&SLOW_TASKS, 32);
  fio_io_start(0);
}
```

The queue starts and stops with the IO reactor. Use `fio_io_async_queue` when a
raw `fio_queue_s *` is needed, `fio_io_async` to push tasks, and
`fio_io_async_every` for timers on that async queue.

---

## Minimal Echo Server

```c
#define FIO_IO
#include "fio-stl/include.h"

static void echo_on_data(fio_io_s *io) {
  char buf[4096];
  size_t len;

  while ((len = fio_io_read(io, buf, sizeof(buf))))
    fio_io_write(io, buf, len); /* copies stack buffer */
}

static void echo_on_timeout(fio_io_s *io) { fio_io_close(io); }

static fio_io_protocol_s ECHO = {
    .on_data = echo_on_data,
    .on_timeout = echo_on_timeout,
    .timeout = 30000,
};

int main(void) {
  fio_io_listen(.url = "tcp://0.0.0.0:3000", .protocol = &ECHO);
  fio_io_start(0);
}
```

---

## Common Lifetime Rules

- Treat `fio_io_s *` as callback/task scoped unless you hold a reference with
  `fio_io_dup`; release it with `fio_io_free`.
- Do not keep raw file descriptors received by transport callbacks.
- Protocol objects should be zero-initialized and live at least as long as any
  IO that uses them.
- Listener `.tls` is moved to the listener; duplicate TLS settings before
  sharing them elsewhere.
- `fio_io_write` copies the buffer; `fio_io_write2` can copy, borrow, or take
  ownership depending on `.copy` and `.dealloc`.
- Environment `on_close` callbacks are the preferred cleanup point for objects
  tied to a connection lifetime.
# IPC — Inter-Process Communication (404 ipc.h)

```c
#define FIO_IPC
#include FIO_INCLUDE_FILE
```

IPC is the messaging backbone of the facil.io worker/master model. Workers
fork from a master process. The master keeps listeners, manages state, and
handles anything that needs a single authoritative view of the world. Workers
handle client connections. IPC bridges the two: a worker sends a message to
the master (or to every process in the cluster), the master executes it and
optionally streams replies back.

All messages are encrypted in transit with ChaCha20-Poly1305 AEAD, keyed from
the process secret. Local IPC runs over a Unix socket (or auto-generated TCP
socket). Multi-machine RPC extends the same protocol over TCP with optional
UDP peer discovery.

See [./400 io-overview.md](./400 io-overview.md) for where IPC sits in the
full IO stack.

---

## Worker/Master Model

```
master process
 ├── owns IPC listener socket
 ├── executes messages sent by workers
 ├── forwards cluster/broadcast messages to other machines
 └── worker process × N
      ├── each has its own IO reactor loop
      ├── connects back to master via IPC socket on startup
      └── sends fio_ipc_* messages to reach master or peers
```

The master is always the hub. Workers can only talk *through* the master —
there is no direct worker-to-worker channel. On shutdown the master sends a
local broadcast instructing all workers to stop.

---

## Configuration Macros

Define before including the header to override defaults.

| Macro | Default | Meaning |
|-------|---------|---------|
| `FIO_IPC_URL_MAX_LENGTH` | `1024` | Max IPC socket URL length |
| `FIO_IPC_MAX_LENGTH` | `128 MiB` | Max message payload size |

---

## Types

### `fio_ipc_s` — the message

```c
typedef struct fio_ipc_s {
  fio_io_s *from;               /* originating IO (set by receiver; not transmitted) */
  /* -- wire format starts here -- */
  uint32_t len;                 /* data[] byte count            (AAD - authenticated) */
  uint16_t flags;               /* user-settable flags           (AAD - authenticated) */
  uint16_t routing_flags;       /* internal routing              (AAD - authenticated) */
  uint64_t timestamp;           /* millisecond tick — part of nonce (unencrypted)    */
  uint64_t id;                  /* random 64-bit value — part of nonce (unencrypted) */
  union {
    void (*call)(struct fio_ipc_s *); /* function to call (local IPC; encrypted)  */
    uint32_t opcode;                  /* registered op-code (cluster RPC; encrypted)*/
  };
  void (*on_reply)(struct fio_ipc_s *); /* run on caller when reply arrives (encrypted) */
  void (*on_done)(struct fio_ipc_s *);  /* run on caller when last reply arrives (encrypted) */
  void *udata;                          /* caller-side opaque pointer (encrypted) */
  char data[];                          /* payload + 16-byte Poly1305 MAC at tail (encrypted) */
} fio_ipc_s;
```

Messages are reference-counted. Everything from `call` onward (including the
payload) is encrypted on the wire. `from`, `timestamp`, and `id` are unencrypted
and unauthenticated (they form the nonce). `len`, `flags`, and `routing_flags`
are unencrypted but authenticated via AEAD.

### `fio_ipc_args_s` — construction arguments

```c
typedef struct {
  void (*call)(fio_ipc_s *);     /* function to run on target (required for local IPC) */
  void (*on_reply)(fio_ipc_s *); /* called on caller when a reply arrives */
  void (*on_done)(fio_ipc_s *);  /* called on caller when final reply arrives */
  fio_io_s *exclude;             /* IO to exclude from delivery (or FIO_IPC_EXCLUDE_SELF) */
  uint64_t timestamp;            /* force timestamp; 0 = use reactor tick */
  uint64_t id;                   /* force message ID; 0 = random */
  uint32_t opcode;               /* registered op-code; replaces call if non-zero */
  uint16_t flags;                /* user-settable flags */
  bool cluster;                  /* deliver to remote machines in cluster */
  bool workers;                  /* deliver to master + all local workers */
  void *udata;                   /* opaque pointer for reply callbacks */
  fio_buf_info_s *data;          /* payload (use FIO_IPC_DATA macro) */
} fio_ipc_args_s;
```

### `fio_ipc_opcode_s` — op-code registration

```c
typedef struct fio_ipc_opcode_s {
  uint32_t opcode;                      /* unique non-zero value */
  void (*call)(struct fio_ipc_s *);     /* handler */
  void (*on_reply)(struct fio_ipc_s *); /* reply callback */
  void (*on_done)(struct fio_ipc_s *);  /* final reply callback */
  void *udata;                          /* provided to handler via ipc->udata */
} fio_ipc_opcode_s;
```

### `fio_ipc_reply_args_s` — reply arguments

```c
typedef struct {
  fio_ipc_s *ipc;       /* original request (required) */
  fio_buf_info_s *data; /* reply payload */
  uint64_t timestamp;   /* 0 = current tick */
  uint64_t id;          /* 0 = use original request ID */
  uint16_t flags;       /* 0 = use original request flags */
  uint8_t done;         /* set to 1 on last reply (triggers on_done on caller) */
  uint8_t flags_set;    /* set to 1 to allow flags=0 override */
} fio_ipc_reply_args_s;
```

---

## Helper Macro: `FIO_IPC_DATA`

```c
#define FIO_IPC_DATA(...)  \
  (fio_buf_info_s[]){ __VA_ARGS__, { .len = ((size_t)-1) } }
```

Composes multiple buffers into a single message without intermediate
allocation. Iteration stops at the first entry with `len == (size_t)-1`.

```c
uint32_t seq = 42;
const char *text = "hello";

fio_ipc_call(.call = my_handler,
             .data = FIO_IPC_DATA(
                 FIO_BUF_INFO2(&seq, sizeof(seq)),
                 FIO_BUF_INFO1((char *)text)));
/* seq and text are copied immediately — stack safe */
```

---

## Delivery Macros

All four macros call `fio_ipc_new(...)` then `fio_ipc_send(...)`. They accept
named fields from `fio_ipc_args_s`.

| Macro | Delivered to |
|-------|-------------|
| `fio_ipc_call(...)` | master only |
| `fio_ipc_local(...)` | master + all local workers |
| `fio_ipc_cluster(...)` | master process on every machine (local and remote) |
| `fio_ipc_broadcast(...)` | master + workers on every machine (local and remote) |

```c
/* worker → master */
#define fio_ipc_call(...)      fio_ipc_send(fio_ipc_new(__VA_ARGS__))
/* all local processes */
#define fio_ipc_local(...)     fio_ipc_send(fio_ipc_new(.workers = 1, __VA_ARGS__))
/* remote masters only */
#define fio_ipc_cluster(...)   fio_ipc_send(fio_ipc_new(.cluster = 1, __VA_ARGS__))
/* everywhere */
#define fio_ipc_broadcast(...) fio_ipc_send(fio_ipc_new(.workers = 1, .cluster = 1, __VA_ARGS__))
```

`fio_ipc_cluster` and `fio_ipc_broadcast` require an `opcode` (not a function
pointer) because function pointers are meaningless across machine boundaries.

Use `.exclude = FIO_IPC_EXCLUDE_SELF` to skip the calling process when
broadcasting locally.

**`FIO_IPC_EXCLUDE_SELF`**

```c
#define FIO_IPC_EXCLUDE_SELF ((fio_io_s *)((char *)-1LL))
```

Pass as `.exclude` in `fio_ipc_args_s`, or set `ipc->from = FIO_IPC_EXCLUDE_SELF`
before calling `fio_ipc_send`.

---

## Quick Examples

### Worker calls master, master replies

```c
/* runs on master */
void on_master(fio_ipc_s *msg) {
  printf("master got: %.*s\n", (int)msg->len, msg->data);
  fio_ipc_reply(msg,
                .data = FIO_IPC_DATA(FIO_BUF_INFO1((char *)"pong")),
                .done = 1);
}

/* runs on calling worker when reply arrives */
void on_reply(fio_ipc_s *msg) {
  printf("worker got reply: %.*s\n", (int)msg->len, msg->data);
}

/* somewhere in a worker callback */
fio_ipc_call(.call    = on_master,
             .on_done = on_reply,
             .data    = FIO_IPC_DATA(FIO_BUF_INFO1((char *)"ping")));
```

### Local broadcast (skip self)

```c
void on_notify(fio_ipc_s *msg) {
  printf("[%d] notified: %.*s\n", fio_io_pid(), (int)msg->len, msg->data);
}

fio_ipc_local(.call    = on_notify,
              .exclude = FIO_IPC_EXCLUDE_SELF,
              .data    = FIO_IPC_DATA(FIO_BUF_INFO1((char *)"config-reload")));
```

### Cluster-wide op-code broadcast

```c
#define OP_CACHE_BUST 1

void on_cache_bust(fio_ipc_s *msg) {
  cache_invalidate(msg->data, msg->len);
}

/* register before fio_io_start */
fio_ipc_opcode_register(.opcode = OP_CACHE_BUST, .call = on_cache_bust);
fio_ipc_cluster_listen(9000); /* requires SECRET env var */
fio_io_start(4);

/* later, from any process */
fio_ipc_broadcast(.opcode = OP_CACHE_BUST,
                  .data   = FIO_IPC_DATA(FIO_BUF_INFO1((char *)"key123")));
```

---

## Reply: `fio_ipc_reply`

```c
SFUNC void fio_ipc_reply(fio_ipc_reply_args_s args);
/* shadowing macro — first arg is the original request */
#define fio_ipc_reply(r, ...) \
  fio_ipc_reply((fio_ipc_reply_args_s){ .ipc = (r), __VA_ARGS__ })
```

Send one or more replies from the master back to the caller. Set `.done = 1`
on the final reply to trigger the caller's `on_done` callback. Replies inherit
`call`, `on_reply`, `on_done`, and `udata` from the original request.

Streaming replies:

```c
void stream_handler(fio_ipc_s *msg) {
  for (int i = 0; i < 5; ++i) {
    char chunk[32];
    int n = snprintf(chunk, sizeof(chunk), "chunk%d", i);
    fio_ipc_reply(msg,
                  .data = FIO_IPC_DATA(FIO_BUF_INFO2(chunk, (size_t)n)),
                  .done = (i == 4));
  }
}
```

---

## Op-Code Registration

Op-codes let you perform cluster-wide RPC without transmitting function
pointers (which differ per binary). Register before `fio_io_start`.

```c
SFUNC int fio_ipc_opcode_register(fio_ipc_opcode_s opcode);
/* named-arg macro */
#define fio_ipc_opcode_register(...) \
  fio_ipc_opcode_register((fio_ipc_opcode_s){ __VA_ARGS__ })

/** Returns registered op-code or NULL. */
SFUNC const fio_ipc_opcode_s *fio_ipc_opcode(uint32_t opcode);
```

- Op-codes must be non-zero `uint32_t` values.
- Values `>= 0xFF000000` are reserved for internal use.
- Pass `call = NULL` to unregister.
- Must be called before `fio_io_start()` (not thread-safe after that).
- Returns 0 on success, -1 on error.

```c
fio_ipc_opcode_register(.opcode = OP_SYNC, .call = on_sync, .udata = ctx);

/* look up */
const fio_ipc_opcode_s *op = fio_ipc_opcode(OP_SYNC);

/* remove */
fio_ipc_opcode_register(.opcode = OP_SYNC, .call = NULL);
```

---

## IPC URL

The IPC socket URL is auto-generated at startup (`unix://fio_tmp_<rand>.sock`
in `$TMPDIR`). Override before `fio_io_start` on the master:

```c
/** Returns current IPC socket URL. */
SFUNC const char *fio_ipc_url(void);

/**
 * Sets IPC socket URL. Master only, before IO reactor starts.
 * Pass NULL to regenerate. 'X' / '#' characters in the URL are replaced with
 * random hex digits. Returns 0 on success, -1 on error.
 */
SFUNC int fio_ipc_url_set(const char *url);
```

```c
fio_ipc_url_set("unix:///var/run/myapp-XXXX.sock"); /* X replaced randomly */
fio_ipc_url_set(NULL);  /* auto-generate */
printf("%s\n", fio_ipc_url());
```

---

## Multi-Machine Cluster (RPC)

```c
/**
 * Listen for cluster peers on port. Auto-discovers peers via UDP broadcast.
 * Requires a shared SECRET environment variable (non-random secret).
 * Returns listener handle or NULL if disabled.
 */
SFUNC fio_io_listener_s *fio_ipc_cluster_listen(uint16_t port);

/** Manually connect to a cluster peer (usually unnecessary). */
SFUNC void fio_ipc_cluster_connect(const char *url);

/** Returns last port passed to fio_ipc_cluster_listen / connect, or 0. */
SFUNC uint16_t fio_ipc_cluster_port(void);
```

- Call `fio_ipc_cluster_listen` from the master before or after
  `fio_io_start`. It opens a TCP listener and a UDP broadcast socket on the
  same port for automatic peer discovery.
- Uses the environment's shared secret for encryption (ChaCha20-Poly1305,
  no forward secrecy). Set `SECRET=<shared-key>` across all instances.
- All instances receive all `cluster` / `broadcast` messages.
- `fio_ipc_cluster_connect` lets you manually reach peers on other subnets
  where UDP broadcast doesn't reach.

```c
/* Typical setup */
fio_ipc_opcode_register(.opcode = OP_SYNC, .call = on_sync);
fio_ipc_cluster_listen(9000); /* returns NULL/no-op if SECRET is unset or random */
fio_io_start(4);
```

---

## Core Lifetime API

These are used internally by the delivery macros and are available for
advanced use (e.g., constructing messages for local execution, or async
message handling).

```c
/** Create a message without sending it. */
SFUNC fio_ipc_s *fio_ipc_new(fio_ipc_args_s args);
#define fio_ipc_new(...) fio_ipc_new((fio_ipc_args_s){ __VA_ARGS__ })

/** Encrypt, route, and free the message. Takes ownership. */
SFUNC void fio_ipc_send(fio_ipc_s *ipc);

/** Encrypt and send directly to a specific IO. Takes ownership. */
SFUNC void fio_ipc_send_to(fio_io_s *to, fio_ipc_s *ipc);

/** Increment reference count. */
SFUNC fio_ipc_s *fio_ipc_dup(fio_ipc_s *msg);

/** Decrement reference count; destroy when zero. */
SFUNC void fio_ipc_free(fio_ipc_s *msg);

/** Release the msg->from IO reference (call if storing msg beyond callback). */
SFUNC void fio_ipc_detach(fio_ipc_s *msg);

/** Set a one-shot callback invoked when sending is complete.
    The callback receives ownership of the decrypted message and must free it. */
SFUNC void fio_ipc_after_send(fio_ipc_s *ipc,
                               void (*fn)(fio_ipc_s *, void *),
                               void *udata);
```

Typical async pattern:

```c
void my_handler(fio_ipc_s *msg) {
  fio_ipc_s *ref = fio_ipc_dup(msg); /* keep alive past callback return */
  fio_ipc_detach(ref);               /* release the from-IO ref if unneeded */
  do_async_work(ref);                /* stored elsewhere */
}

void async_done(fio_ipc_s *msg) {
  process(msg->data, msg->len);
  fio_ipc_free(msg);
}
```

---

## Encryption

```c
/** Encrypt a message in-place (idempotent). Called automatically by fio_ipc_send. */
SFUNC void fio_ipc_encrypt(fio_ipc_s *m);

/** Decrypt a message in-place. Returns 0 on success, non-zero on MAC failure. */
FIO_IFUNC int fio_ipc_decrypt(fio_ipc_s *m);
```

ChaCha20-Poly1305 AEAD. Key derived from the shared process secret plus the
message's `id` and `timestamp` fields (which form the nonce). `len`, `flags`,
and `routing_flags` are authenticated but unencrypted. Everything else
(`call`/`opcode`, `on_reply`, `on_done`, `udata`, `data`) is encrypted.

Decryption failure logs a security warning and the connection is closed.
Only processes sharing the same secret can communicate.

---

## Routing Flags

`routing_flags` is internal — do not edit manually. Available for inspection:

```c
#define FIO_IPC_FLAG_ENCRYPTED  ((uint16_t)1 << 0) /* message is encrypted */
#define FIO_IPC_FLAG_DONE       ((uint16_t)1 << 1) /* final reply */
#define FIO_IPC_FLAG_OPCODE     ((uint16_t)1 << 2) /* route via op-code */
#define FIO_IPC_FLAG_WORKERS    ((uint16_t)1 << 3) /* deliver to workers too */
#define FIO_IPC_FLAG_CLUSTER    ((uint16_t)1 << 4) /* deliver to remote machines */
#define FIO_IPC_FLAG_REPLY      ((uint16_t)1 << 5) /* this is a reply */
#define FIO_IPC_FLAG_PING       ((uint16_t)1 << 6) /* internal keepalive */

/** Test a flag: returns flag value if set, 0 otherwise. */
#define FIO_IPC_FLAG_TEST(msg, flag) (((msg)->routing_flags & (flag)) == (flag))
```

---

## Keepalive

IPC and cluster connections use an automatic ping/pong protocol. On the first
timeout the system sends a ping; if no pong arrives before the next timeout
the connection is closed. Ping frames are never delivered to user callbacks.

Default timeouts (set in the protocol initializer):

| Connection type | Timeout |
|-----------------|---------|
| Local IPC (master ↔ worker) | ~6 minutes |
| Cluster RPC (machine ↔ machine) | ~55 seconds |

---

## Memory Management

- Messages are reference-counted (`fio_ipc_dup` / `fio_ipc_free`).
- Callbacks receive a message pointer valid for the duration of the callback.
  The system frees it automatically after the callback returns.
- Call `fio_ipc_dup` to keep a message alive beyond the callback, then
  `fio_ipc_free` when done.
- Data passed via `FIO_IPC_DATA` is copied into the message immediately —
  source buffers may be stack-allocated or freed right after the call.

---

## Thread Safety

- `fio_ipc_call`, `fio_ipc_local`, `fio_ipc_cluster`, `fio_ipc_broadcast`,
  and `fio_ipc_reply` are safe to call from any thread.
- `fio_ipc_dup` / `fio_ipc_free` are thread-safe.
- `fio_ipc_opcode_register` is **not** thread-safe after `fio_io_start`.
- `fio_ipc_url_set` must be called on the master before `fio_io_start`.
# OpenSSL TLS Backend

```c
#define FIO_IO
#include "fio-stl/include.h"
```

OpenSSL 3.x IO-layer TLS backend for the facil.io reactor. When OpenSSL is
available at build time this module auto-registers itself as the default TLS
transport. You don't have to call anything — just define `FIO_IO` and link
against OpenSSL 3.x.

For IO-layer TLS context overview see [./400 io-overview.md](./400 io-overview.md).  
For the sibling native TLS 1.3 backend see [./405 tls13.md](./405 tls13.md) *(planned)*.  
For standalone TLS 1.3 crypto (key schedule, record layer) see [./190 tls13.md](./190 tls13.md).

---

## Activation

The module compiles automatically when all of the following are true:

| Condition | Notes |
|-----------|-------|
| `FIO_IO` is defined | pulls in the IO reactor |
| `FIO_NO_TLS` is **not** defined | opt-out guard |
| OpenSSL 3.x headers are present | `HAVE_OPENSSL` or `__has_include("openssl/ssl.h")` |

If OpenSSL headers are found but the version is older than 3.x
(`OPENSSL_VERSION_MAJOR < 3`), the module emits a compiler warning and
makes `fio_openssl_io_functions()` return `fio_io_tls_default_functions(NULL)`
(the current default, usually a no-op). Everything compiles; TLS simply does nothing.

A module-level constructor runs before `main()`:

- Initialises the custom BIO methods.
- Calls `fio_io_tls_default_functions(&openssl_io_funcs)` so every subsequent
  `fio_io_listen` / `fio_io_connect` call with a `tls` argument uses OpenSSL.
- Registers a `SIGPIPE` monitor (OpenSSL can trigger SIGPIPE on broken sockets).
- Registers a `FIO_CALL_AT_EXIT` cleanup for the custom BIO method objects.

---

## Public API

### `fio_openssl_io_functions`

```c
fio_io_functions_s fio_openssl_io_functions(void);
```

Returns the `fio_io_functions_s` vtable that wires OpenSSL into the IO reactor:

| Field | Role |
|-------|------|
| `build_context` | Converts `fio_io_tls_s` into an `SSL_CTX` wrapper |
| `free_context` | Deferred free of `SSL_CTX` and the `fio_io_tls_s` reference |
| `start` | Per-connection: `SSL_new`, custom BIO setup, initial handshake |
| `read` | Non-blocking decrypt: advances handshake if needed, then `SSL_read_ex` |
| `write` | Non-blocking encrypt: `SSL_write_ex` → encrypt → socket |
| `flush` | Sends any pending encrypted bytes from the internal buffer |
| `finish` | Sends TLS `close_notify` before the TCP close |
| `cleanup` | Frees the per-connection `SSL` object |

Normally you never call this directly — the constructor handles registration.
Use it explicitly only when overriding the default or building a custom setup:

```c
/* Override: switch back to OpenSSL after something else changed the default */
fio_io_functions_s openssl_funcs = fio_openssl_io_functions();
fio_io_tls_default_functions(&openssl_funcs);
```

Or to set it on a specific protocol without touching the global default
(the `io_functions` field lives on `fio_io_protocol_s`):

```c
fio_io_functions_s openssl_funcs = fio_openssl_io_functions();
MY_PROTOCOL.io_functions = openssl_funcs; /* per-protocol override */

fio_io_listen(.url = "0.0.0.0:8443",
              .protocol = &MY_PROTOCOL,
              .tls = tls);
```

---

## Configuring TLS — `fio_io_tls_s`

TLS parameters are held in a `fio_io_tls_s` object defined in `401 io api.h`.
Build one before calling `fio_io_listen` or `fio_io_connect`:

```c
/* Allocate (reference counted) */
fio_io_tls_s *tls = fio_io_tls_new();

/* Certificate — PEM files */
fio_io_tls_cert_add(tls,
    "www.example.com",   /* server_name (SNI) */
    "cert.pem",          /* public certificate or chain */
    "key.pem",           /* private key */
    NULL);               /* PEM password, or NULL */

/* ALPN protocol negotiation */
fio_io_tls_alpn_add(tls, "h2",       on_http2_selected);
fio_io_tls_alpn_add(tls, "http/1.1", on_http1_selected);

/* Peer certificate verification */
fio_io_tls_trust_add(tls, NULL);      /* use system trust store */
fio_io_tls_trust_add(tls, "ca.pem"); /* or a specific CA bundle */

/* Listen */
fio_io_listen(.url = "0.0.0.0:443",
              .protocol = &MY_PROTOCOL,
              .tls = tls);

fio_io_tls_free(tls); /* release your reference; the listener holds its own */
fio_io_start(0);
```

`fio_io_tls_s` is reference-counted (`fio_io_tls_dup` / `fio_io_tls_free`).
The backend duplicates the reference during `build_context`; freeing yours
after `fio_io_listen` is always safe.

### URL shorthand

The URL query string can also configure TLS without building the object
manually:

```c
fio_io_listen(.url = "0.0.0.0:443/?tls=./certs/", .protocol = &MY_PROTOCOL);
```

See `fio_io_tls_from_url` in `401 io api.h` for the query syntax.

---

## Certificates

### Loading from PEM files

When `public_cert_file` and `private_key_file` are both provided to
`fio_io_tls_cert_add`, the backend:

1. Sets a PEM password callback if `pk_password` is non-`NULL`.
2. Loads the certificate chain via `SSL_CTX_use_certificate_chain_file`.
3. Loads the private key via `SSL_CTX_use_PrivateKey_file` (PEM format).
4. Verifies key-cert consistency with `SSL_CTX_check_private_key`.

Errors are logged with `FIO_LOG_ERROR` and the context build fails.

### Self-signed fallback

When a **server** has no certificates configured (or `fio_io_tls_cert_add` is
called with both file arguments as `NULL`), the backend generates a self-signed
ECDSA P-256 certificate on the fly:

| Property | Value |
|----------|-------|
| Key algorithm | ECDSA P-256 (128-bit security ≈ RSA-3072) |
| Key generation time | ~10 ms |
| Signature | SHA-256 |
| Validity | 180 days |
| Serial number | 128-bit cryptographically random |
| X.509 extensions | Basic Constraints (`CA:FALSE`), Key Usage, Extended Key Usage (`serverAuth`), SAN |

The private key is generated once per process (thread-safe, lock-protected) and
freed at exit via a `FIO_CALL_AT_EXIT` callback.

Self-signed certificates are fine for development. Browsers will warn. Use a
CA-issued certificate (e.g. Let's Encrypt) in production.

---

## ALPN

Register protocols with `fio_io_tls_alpn_add` before listen/connect. The first
registered protocol is the preferred default.

When a client sends an ALPN extension, the backend walks the offered names in
order and picks the first that matches a registered protocol.  On match it calls
`fio_io_tls_alpn_select`, which fires the `on_selected` callback for that
connection.  If nothing matches, the handshake fails with a fatal alert.

Protocol names must be 1–255 bytes. The internal wire-format list is capped at
1 023 bytes total; more protocols than that will log an overflow error.

---

## Trust and Peer Verification

Trust configured with `fio_io_tls_trust_add` always refers to **peer**
verification: a client context verifies the server certificate, a server
context requests, requires, and verifies the client certificate (mutual TLS).
An empty trust list means **no peer verification**.

| Scenario | Behaviour |
|----------|-----------|
| `fio_io_tls_trust_add` called (any argument) | `SSL_VERIFY_PEER` enabled |
| `NULL` passed to `fio_io_tls_trust_add` | system trust store is loaded (`X509_STORE_set_default_paths`) |
| Trust configured, server mode | `SSL_VERIFY_PEER \| SSL_VERIFY_FAIL_IF_NO_PEER_CERT` (client certificate required) |
| No trust certs added, client mode | `SSL_VERIFY_NONE` + `FIO_LOG_SECURITY` warning |
| No trust certs added, server mode | `SSL_VERIFY_NONE` (no client certificate requested) |

After the handshake, inspect the peer's certificate chain with
`fio_io_peer_info_next(io, &info)` (see the IO API docs). Each call
re-encodes the next chain certificate into a fixed per-connection staging
buffer (no allocation) and parses it into a `fio_x509_cert_s`, exposing the
subject, issuer, SAN entries, public key, validity, and a SHA-256
fingerprint. `info.verified` reflects OpenSSL's chain verification result
(`SSL_get_verify_result`).

---

## SSL Context Modes

The `SSL_CTX` is configured for non-blocking use:

```
SSL_MODE_ENABLE_PARTIAL_WRITE      — SSL_write may return before all data encrypted
SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER — buffer pointer may differ between retries
SSL_MODE_RELEASE_BUFFERS           — free idle 34 KB per-connection OpenSSL buffers
SSL_MODE_AUTO_RETRY                — CLEARED (return immediately on WANT_READ/WRITE)
```

TLS 1.3 session tickets: 2 tickets are configured for session resumption
(saves ~30–50 % of handshake CPU on reconnects).

---

## Internal Design — Custom BIOs

Instead of `BIO_s_mem()` (which bounces data through a private `BUF_MEM`),
the backend uses custom `BIO_METHOD` objects that give OpenSSL direct access to
the per-connection buffers:

- **rbio** — OpenSSL reads raw socket bytes directly from a 64 KB receive
  buffer that was filled by a single `fio_sock_read` call.
- **wbio** — OpenSSL appends encrypted output directly into a ~66 KB
  encrypted-output buffer (4 × max TLS record).

This eliminates all intermediate copies while retaining full control over
buffering and partial writes.

Handshake responses (ServerHello, etc.) are flushed to the socket from inside
the `read` callback — the IO layer's `on_ready` loop doesn't run until the
handshake completes, so the backend must send them itself.

---

## Lifecycle and Error Behavior

```
fio_io_listen / fio_io_connect
    │
    └─► build_context (SSL_CTX built once per listener/connector)
            │
            └─► per-connection start (SSL_new + BIO setup + SSL_accept/SSL_connect)
                    │
                    ├─ read  (advances handshake until SSL_is_init_finished,
                    │         then SSL_read_ex)
                    ├─ write (SSL_write_ex, encrypt into wbio buffer, flush socket)
                    ├─ flush (send remaining enc_buf bytes)
                    │
                    ├─ finish  (SSL_shutdown + best-effort send of close_notify)
                    └─ cleanup (SSL_free — also frees both BIOs)
```

**Error codes** surfaced to the IO layer:

- `return 0` — peer closed cleanly (`SSL_ERROR_ZERO_RETURN`) or fatal SSL error.
- `return -1` with `errno = EWOULDBLOCK` — not enough data yet (normal event-loop
  signalling; the reactor will retry on next readable event).

All non-fatal internal issues are logged via `FIO_LOG_ERROR` /
`FIO_LOG_WARNING` / `FIO_LOG_SECURITY`. Debug-level detail is at
`FIO_LOG_DDEBUG2`.

---

## Minimal Server Example

```c
#define FIO_LOG
#define FIO_IO
#include "fio-stl/include.h"

static void on_data(fio_io_s *io) {
  char buf[4096];
  size_t n = fio_io_read(io, buf, sizeof(buf));
  if (n)
    fio_io_write(io, buf, n); /* echo */
}

static fio_io_protocol_s ECHO_PROTO = {
    .on_data    = on_data,
    .on_timeout = fio_io_touch,
};

int main(void) {
  /* No certificate configured → self-signed ECDSA P-256 generated automatically */
  fio_io_tls_s *tls = fio_io_tls_new();

  fio_io_listen(.url      = "0.0.0.0:8443",
                .protocol = &ECHO_PROTO,
                .tls      = tls);
  fio_io_tls_free(tls);

  FIO_LOG_INFO("TLS echo server on :8443  (test: openssl s_client -connect localhost:8443)");
  fio_io_start(0);
}
```

For a production server, load real certificates:

```c
fio_io_tls_cert_add(tls, "example.com", "cert.pem", "key.pem", NULL);
```

---

## Disambiguation

| Document | Scope |
|----------|-------|
| **This document** | OpenSSL 3.x IO-layer backend — plugs TLS into the reactor |
| [./405 tls13.md](./405 tls13.md) *(planned)* | Native TLS 1.3 IO backend (same `fio_io_functions_s` interface, no OpenSSL dependency) |
| [./190 tls13.md](./190 tls13.md) | Standalone TLS 1.3 crypto library (key schedule, record layer, raw machinery) |
| [./400 io-overview.md](./400 io-overview.md) | IO + TLS stack overview |
# Native TLS 1.3 IO Backend

```c
#define FIO_IO
#include "fio-stl/include.h"
```

IO-layer TLS 1.3 integration — for standalone crypto library see [`190 tls13.md`](./190 tls13.md).

This module wires the native TLS 1.3 state machines into the facil.io IO reactor
as a drop-in transport backend. When OpenSSL is unavailable at build time it
registers itself automatically; otherwise it is available on demand via
`fio_tls13_io_functions()`. No external dependencies required.

For the IO/TLS stack overview see [./400 io-overview.md](./400 io-overview.md).  
For the OpenSSL backend (preferred when available) see [./405 openssl.md](./405 openssl.md).

---

## Activation

The module compiles when all of the following are true:

| Condition | Notes |
|-----------|-------|
| `FIO_IO` is defined | pulls in the IO reactor |
| `H___FIO_TLS13___H` guard is satisfied | `190 tls13.h` was included first |
| `FIO_NO_TLS` is **not** defined | opt-out guard checked by `include.h` before this backend is included |

A module-level constructor (`FIO_CONSTRUCTOR`) runs automatically before
`main()` **only when OpenSSL is absent** (`HAVE_OPENSSL` not defined and
`405 openssl.h` not included). It calls:

```c
fio_io_tls_default_functions(&FIO___TLS13_IO_FUNCS);
```

This makes every subsequent `fio_io_listen` / `fio_io_connect` call with a
`tls` argument use the native TLS 1.3 engine without any extra configuration.

When OpenSSL *is* present, the native backend compiles but does **not**
auto-register. Use `fio_tls13_io_functions()` to switch explicitly if needed.

---

## Public API

### `fio_tls13_io_functions`

```c
fio_io_functions_s fio_tls13_io_functions(void);
```

Returns the `fio_io_functions_s` vtable that wires the native TLS 1.3 engine
into the IO reactor:

| Field | Role |
|-------|------|
| `build_context` | Converts `fio_io_tls_s` into a per-listener/connector context |
| `free_context` | Deferred free of the context (via `fio_io_defer`) |
| `start` | Per-connection: allocate state, run ClientHello or await ServerHello |
| `read` | Non-blocking decrypt: advances handshake if needed, then decrypts records |
| `write` | Non-blocking encrypt: batches up to 4 TLS records (64 KB) per syscall |
| `flush` | Drains any pending handshake or encrypted bytes from internal buffers |
| `finish` | Sends a TLS `close_notify` alert before the TCP close |
| `cleanup` | Frees per-connection TLS state |

Normally you never call this directly — the constructor handles registration.
Use it to override the global default or set a per-protocol backend:

```c
/* Override: force native TLS 1.3 even when OpenSSL is present */
fio_io_functions_s tls13_funcs = fio_tls13_io_functions();
fio_io_tls_default_functions(&tls13_funcs);
```

Or set it on a specific protocol without changing the global default:

```c
fio_io_functions_s tls13_funcs = fio_tls13_io_functions();
MY_PROTOCOL.io_functions = tls13_funcs; /* per-protocol override */

fio_io_listen(.url      = "0.0.0.0:8443",
              .protocol = &MY_PROTOCOL,
              .tls      = tls);
```

---

## Configuring TLS — `fio_io_tls_s`

TLS parameters are carried in a `fio_io_tls_s` object (defined in
`401 io api.h`). Build one before calling `fio_io_listen` or `fio_io_connect`:

```c
/* Allocate (reference counted) */
fio_io_tls_s *tls = fio_io_tls_new();

/* Certificate — PEM files */
fio_io_tls_cert_add(tls,
    "www.example.com", /* server_name (SNI) */
    "cert.pem",        /* public certificate or chain */
    "key.pem",         /* private key */
    NULL);             /* PEM password, or NULL */

/* ALPN protocol negotiation */
fio_io_tls_alpn_add(tls, "h2",       on_http2_selected);
fio_io_tls_alpn_add(tls, "http/1.1", on_http1_selected);

/* Peer certificate verification */
fio_io_tls_trust_add(tls, NULL);      /* use system trust store */
fio_io_tls_trust_add(tls, "ca.pem"); /* or a specific CA bundle */

/* Listen */
fio_io_listen(.url      = "0.0.0.0:443",
              .protocol = &MY_PROTOCOL,
              .tls      = tls);

fio_io_tls_free(tls); /* release your reference; the listener holds its own */
fio_io_start(0);
```

`fio_io_tls_s` is reference-counted. The backend duplicates the reference
during `build_context`; freeing yours after `fio_io_listen` is always safe.

---

## Certificates

### Loading from PEM files

When both `public_cert_file` and `private_key_file` are provided to
`fio_io_tls_cert_add`, the backend reads and parses the PEM files, loading
every `CERTIFICATE` block in the file as a DER chain entry. Three private key
types are supported:

| Key type | Constant | Notes |
|----------|----------|-------|
| ECDSA P-256 | `FIO_TLS13_SIGNATURE_ECDSA_SECP256R1_SHA256` | Recommended; small and fast |
| Ed25519 | `FIO_TLS13_SIGNATURE_ED25519` | Fastest signatures |
| RSA (any size) | `FIO_TLS13_SIGNATURE_RSA_PSS_RSAE_SHA256` | Requires `H___FIO_RSA___H` |

If PEM parsing fails, the backend logs `FIO_LOG_WARNING` and falls back to a
self-signed certificate. If the fallback also fails the context build fails
and `build_context` returns `NULL`.

### Self-signed fallback

When a **server** has no certificates configured, the backend generates a
self-signed P-256 ECDSA certificate on the fly using `fio_x509_self_signed_cert`
(requires `H___FIO_X509___H`):

| Property | Value |
|----------|-------|
| Key algorithm | ECDSA P-256 (128-bit security ≈ RSA-3072) |
| Signature | SHA-256 |
| SAN | set to `server_name` (defaults to `"localhost"`) |

Self-signed certificates are fine for development. Browsers will warn. Use a
CA-issued certificate (e.g. Let's Encrypt) in production.

If `H___FIO_X509___H` is not available when no PEM files are configured, the
context build fails with `FIO_LOG_ERROR`.

---

## ALPN

Register protocols with `fio_io_tls_alpn_add` before listen/connect. The
first registered protocol is the preferred default.

The backend collects all registered names into a comma-separated string (up
to 255 bytes per name; 255 characters total plus the NUL terminator). On handshake the server matches the
client's offered list against the registered names in registration order and
calls the corresponding `on_selected` callback on the `fio_io_s *` when
negotiation succeeds.

Protocol names must be 1–255 bytes. The internal list overflows when the
comma-separated list would exceed 255 characters — excess protocols are dropped with `FIO_LOG_ERROR`.

---

## Trust and Peer Verification

Trust configured with `fio_io_tls_trust_add` always refers to **peer**
verification: on **client** connections the backend verifies the server
certificate, on **server** connections it requests, requires, and verifies
the client certificate (mutual TLS). An empty trust list means **no peer
verification** — matching the OpenSSL backend.

| Scenario | Behaviour |
|----------|-----------|
| `fio_io_tls_trust_add(tls, NULL)` | system CA bundle used (loaded once, global) |
| `fio_io_tls_trust_add(tls, "ca.pem")` | user-supplied CA bundle (per-context) |
| No `fio_io_tls_trust_add` call, client mode | no server verification + `FIO_LOG_SECURITY` warning |
| No `fio_io_tls_trust_add` call, server mode | no client certificate requested |
| Trust configured, server mode | client certificate required and verified |

For mTLS, client certificate chains are verified against the trust store
(`fio_x509_verify_chain`) and the client's `CertificateVerify` signature is
validated against the leaf certificate's public key (Ed25519, ECDSA P-256 /
P-384, and RSA-PSS / PKCS#1 SHA-256 / SHA-384 schemes). A client that fails
verification is rejected with a TLS alert during the handshake.

After the handshake, inspect the peer's chain with
`fio_io_peer_info_next(io, &info)` (see the IO API docs) — each call parses
the next certificate in place (zero-copy) into a `fio_x509_cert_s`, exposing
the subject CN/DN, issuer, SAN entries, public key, validity, and a SHA-256
fingerprint for pinning, plus a `verified` flag for authorization decisions.

The system CA bundle is loaded **once** into a process-wide singleton
(`fio___tls13_sys_trust`) and shared read-only across all connections —
loading ~128 certs per outgoing connection is avoided. It is freed at process
exit via a `FIO_CALL_AT_EXIT` callback.

Platform CA bundle lookup order (POSIX):

```
/etc/ssl/cert.pem                                       (macOS, FreeBSD)
/etc/ssl/certs/ca-certificates.crt                      (Debian/Ubuntu)
/etc/pki/tls/certs/ca-bundle.crt                        (RHEL/CentOS)
/etc/ssl/ca-bundle.pem                                   (openSUSE)
/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem       (RHEL newer)
/usr/local/etc/ssl/cert.pem                              (FreeBSD ports)
```

On **Windows** the `ROOT` system certificate store is enumerated via the
CryptoAPI (`CertOpenSystemStoreA` / `CertEnumCertificatesInStore`). On MSVC,
`Crypt32.lib` is linked automatically via `#pragma comment`; other Windows
toolchains may need an explicit `Crypt32` link flag.

---

## Lifecycle and Error Behavior

```
fio_io_listen / fio_io_connect
    │
    └─► build_context (fio_io_tls_s → fio___tls13_context_s, once per listener)
            │
            └─► per-connection start
                    │
                    ├─ client: ClientHello sent immediately
                    ├─ server: awaits ClientHello
                    │
                    ├─ read   (advances handshake records; decrypts app data after)
                    ├─ write  (encrypts; up to 4 records batched per syscall)
                    ├─ flush  (drains handshake + enc_buf to socket)
                    │
                    ├─ finish  (sends encrypted close_notify alert, best-effort)
                    └─ cleanup (frees fio___tls13_connection_s)
```

**Internal buffers per connection** (flexible array member, single allocation):

| Region | Purpose | Size |
|--------|---------|------|
| `recv_buf` | Incoming encrypted data | `FIO_IO_BUFFER_PER_WRITE` (~64 KB) |
| `app_buf` | Decrypted plaintext ready to deliver | `FIO_IO_BUFFER_PER_WRITE` |
| `send_buf` | Outgoing handshake bytes | `FIO_IO_BUFFER_PER_WRITE` |
| `enc_buf` | Pre-allocated encryption output (4 max records) | ~66 KB |

**Error codes surfaced to the IO layer:**

- `return 0` from `read` — peer closed cleanly (EOF).
- `return -1` with `errno = EWOULDBLOCK` — no data yet; reactor retries on next readable event.
- `return -1` with `errno = ECONNRESET` — TLS handshake or decryption error; connection will be closed.

All internal errors are logged: `FIO_LOG_ERROR` for hard failures,
`FIO_LOG_WARNING` for soft failures (e.g. PEM fallback), `FIO_LOG_DEBUG2` /
`FIO_LOG_DDEBUG2` for per-connection detail.

**KeyUpdate** (RFC 8446 §4.6.3): when the peer requests a key update, a
`KeyUpdate` response is prepended to the next `write` syscall alongside
application data so they go out in a single call.

---

## Minimal Server Example

```c
#define FIO_LOG
#define FIO_IO
#include "fio-stl/include.h"

static void on_data(fio_io_s *io) {
  char buf[4096];
  size_t n = fio_io_read(io, buf, sizeof(buf));
  if (n)
    fio_io_write(io, buf, n); /* echo */
}

static fio_io_protocol_s ECHO_PROTO = {
    .on_data    = on_data,
    .on_timeout = fio_io_touch,
};

int main(void) {
  /* No certificate → self-signed P-256 generated automatically */
  fio_io_tls_s *tls = fio_io_tls_new();

  fio_io_listen(.url      = "0.0.0.0:8443",
                .protocol = &ECHO_PROTO,
                .tls      = tls);
  fio_io_tls_free(tls);

  FIO_LOG_INFO("TLS echo server on :8443");
  fio_io_start(0);
}
```

For a production server, load real certificates:

```c
fio_io_tls_cert_add(tls, "example.com", "cert.pem", "key.pem", NULL);
```

---

## TLS Client Example

```c
#define FIO_LOG
#define FIO_IO
#include "fio-stl/include.h"

static void on_attach(fio_io_s *io) {
  /* Handshake happens transparently through the TLS transport hooks. */
  const char req[] = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
  fio_io_write(io, req, sizeof(req) - 1);
}

static void on_data(fio_io_s *io) {
  char buf[4096];
  size_t n = fio_io_read(io, buf, sizeof(buf) - 1);
  if (n) {
    buf[n] = '\0';
    FIO_LOG_INFO("response:\n%s", buf);
    fio_io_close(io);
  }
}

static fio_io_protocol_s CLIENT_PROTO = {
    .on_attach  = on_attach,
    .on_data    = on_data,
    .on_timeout = fio_io_touch,
};

int main(void) {
  fio_io_tls_s *tls = fio_io_tls_new();
  /* SNI hostname — system CA store is used automatically for verification */
  fio_io_tls_cert_add(tls, "example.com", NULL, NULL, NULL);

  fio_io_connect(.url      = "example.com:443",
                 .protocol = &CLIENT_PROTO,
                 .tls      = tls);
  fio_io_tls_free(tls);

  fio_io_start(0);
}
```


---

## Comparison with OpenSSL Backend

| Feature | Native TLS 1.3 | OpenSSL 3.x |
|---------|---------------|-------------|
| External dependency | None | OpenSSL 3.x |
| TLS versions | 1.3 only | 1.0–1.3 |
| Certificate key types | P-256, Ed25519, RSA | All |
| ALPN | Yes | Yes |
| Self-signed auto-cert | Yes (P-256) | Yes (P-256) |
| System trust store | Yes (multi-platform) | Yes |
| Session resumption (0-RTT) | No | Yes |
| OCSP stapling | No | Yes |
| Binary size impact | Smaller | Larger |
| Auto-registered when | OpenSSL absent | OpenSSL present |

The native backend is suitable for environments where TLS 1.3-only is
acceptable and minimising dependencies matters. If you need legacy TLS
versions, session resumption, OCSP stapling, or full certificate type
coverage, use the OpenSSL backend.

---

## Disambiguation

| Document | Scope |
|----------|-------|
| **This document** | Native TLS 1.3 IO backend — plugs TLS into the reactor via `fio_io_functions_s` |
| [./405 openssl.md](./405 openssl.md) | OpenSSL 3.x IO backend (same interface, preferred when available) |
| [./190 tls13.md](./190 tls13.md) | Standalone TLS 1.3 crypto library — key schedule, record layer, handshake state machines |
| [./400 io-overview.md](./400 io-overview.md) | IO + TLS stack overview |
# Pub/Sub — Publish/Subscribe Messaging (420 pubsub.h)

```c
#define FIO_PUBSUB
#include FIO_INCLUDE_FILE
```

> **Requires:** `FIO_IPC`. When using `include.h` the dependency is resolved automatically. When using the combined `fio-stl.h`, define `FIO_IPC` before `FIO_PUBSUB`.

Pub/Sub delivers messages to named channels across all worker processes and,
through pluggable engines, across machines or external brokers. It is the
broadcast layer that sits directly on top of IPC.

See [./400 io-overview.md](./400 io-overview.md) for where Pub/Sub fits in the
full IO stack, and [./404 ipc.md](./404 ipc.md) for the IPC transport it relies on.

---

## How it fits together

```
 publisher (any thread/process)
         │
         │  fio_pubsub_publish(...)
         ▼
   engine->publish()          ← default: fio_pubsub_engine_ipc()
         │
         │  IPC message (fio_ipc_local / fio_ipc_broadcast)
         ▼
  every process receives it
         │
         ├─ matches channel  → subscriber callbacks scheduled on queue
         ├─ matches patterns → pattern subscriber callbacks scheduled
         └─ history manager  → message stored for replay
```

Workers do not talk to each other directly. Every publish goes through the
master (via IPC), which fans the message out to all processes. Subscriptions
follow the same path in reverse: when a new channel is first subscribed in a
worker, the worker notifies the master so the master can track routing.

---

## Configuration Macros

Define these **before** including the header to override the defaults.

| Macro | Default | Meaning |
|-------|---------|---------|
| `FIO_PUBSUB_FUTURE_LIMIT_MS` | `60000` | Messages timestamped more than this many ms in the future are not delivered to subscribers immediately; they are handed to history managers instead. |
| `FIO_PUBSUB_HISTORY_DEFAULT_CACHE_SIZE_LIMIT` | `1 << 28` (256 MiB) | Default byte ceiling for the built-in in-memory history cache. |

---

## Types

### `fio_pubsub_msg_s` — the message

```c
typedef struct fio_pubsub_msg_s {
  fio_io_s      *io;        /* IO connection owning the subscription (may be NULL) */
  void          *udata;     /* opaque value set on subscribe                        */
  uint64_t       timestamp; /* ms since epoch                                       */
  uint64_t       id;        /* unique message ID                                    */
  fio_buf_info_s channel;   /* channel name  — shared, do NOT mutate               */
  fio_buf_info_s message;   /* message payload — shared, do NOT mutate             */
  int16_t        filter;    /* numerical namespace; negative values are reserved    */
} fio_pubsub_msg_s;
```

Delivered to `on_message` callbacks. The `channel` and `message` buffers are
shared references into the underlying IPC buffer — do not store pointers to
them beyond the callback unless you call `fio_pubsub_msg2ipc` / `fio_ipc_dup`
first.

### `fio_pubsub_subscribe_args_s` — subscribe / unsubscribe arguments

```c
typedef struct {
  fio_io_s      *io;                     /* subscription owner (NULL = global)      */
  fio_buf_info_s channel;                /* channel name or pattern                 */
  void         (*on_message)(fio_pubsub_msg_s *msg); /* required callback          */
  void         (*on_unsubscribe)(void *udata);       /* optional cleanup callback   */
  void          *udata;                  /* opaque user data for callbacks           */
  fio_queue_s   *queue;                  /* callback queue; NULL = IO queue          */
  uintptr_t     *subscription_handle_ptr; /* out: handle for manual management      */
  uint64_t       replay_since;           /* replay history since this ms timestamp   */
  int16_t        filter;                 /* numerical namespace (must match publish) */
  uint8_t        is_pattern;             /* non-zero = channel is a glob pattern     */
  uint8_t        master_only;            /* non-zero = subscription lives in master  */
} fio_pubsub_subscribe_args_s;
```

Both `fio_pubsub_subscribe` and `fio_pubsub_unsubscribe` use this struct. The
macro wrappers let you pass named arguments directly.

**Ownership rules:**

- When `io` is set, the subscription is stored in the IO object's environment
  and cancelled automatically when the connection closes. Only one subscription
  per `(channel, filter, is_pattern)` triple is allowed per IO.
- When `io` is NULL, the subscription is global — one per
  `(channel, filter, is_pattern)` triple for the whole process.
- When `subscription_handle_ptr` is set, the `io` field is ignored and the
  handle is written to the pointer. The caller is responsible for calling
  `fio_pubsub_unsubscribe(.subscription_handle_ptr = &handle)` explicitly.
  Forgetting to do so leaks the subscription.

**If `on_message` is NULL and `io` is set**, the subscription calls
`io->protocol->on_pubsub(msg)` automatically (protocol-level dispatch).

### `fio_pubsub_publish_args_s` — publish arguments

```c
typedef struct {
  fio_pubsub_engine_s const *engine;  /* NULL = default engine                    */
  fio_io_s                  *from;    /* exclude this IO from receiving the message */
  uint64_t                   id;      /* 0 = auto-generate random ID               */
  uint64_t                   timestamp; /* 0 = current reactor tick (ms)           */
  fio_buf_info_s             channel; /* target channel name                       */
  fio_buf_info_s             message; /* message payload                           */
  int16_t                    filter;  /* numerical namespace                        */
} fio_pubsub_publish_args_s;
```

---

## Subscribe / Unsubscribe

### `fio_pubsub_subscribe`

```c
void fio_pubsub_subscribe(fio_pubsub_subscribe_args_s args);
/* macro wrapper — enables named arguments: */
#define fio_pubsub_subscribe(...) \
  fio_pubsub_subscribe((fio_pubsub_subscribe_args_s){__VA_ARGS__})
```

Subscribe to a channel. The actual subscription is queued as a deferred task
on the IO thread, so it is safe to call from any thread.

```c
/* Global subscription */
fio_pubsub_subscribe(.channel    = FIO_BUF_INFO1("events"),
                     .on_message = handle_event);

/* Per-connection subscription — auto-cancelled on close */
fio_pubsub_subscribe(.io         = client_io,
                     .channel    = FIO_BUF_INFO1("user:123"),
                     .on_message = handle_user_msg,
                     .udata      = user_ctx);

/* Pattern subscription (glob) */
fio_pubsub_subscribe(.channel    = FIO_BUF_INFO1("chat:*"),
                     .on_message = handle_chat,
                     .is_pattern = 1);

/* Replay history from the last 5 minutes */
fio_pubsub_subscribe(.channel      = FIO_BUF_INFO1("news"),
                     .on_message   = handle_news,
                     .replay_since = fio_io_last_tick() - 5*60*1000);

/* Manual handle for explicit control */
uintptr_t sub_handle = 0;
fio_pubsub_subscribe(.channel                = FIO_BUF_INFO1("ticker"),
                     .on_message             = handle_tick,
                     .subscription_handle_ptr = &sub_handle);
/* … later … */
fio_pubsub_unsubscribe(.subscription_handle_ptr = &sub_handle);
```

> **Note:** pattern subscriptions do not support history replay (`replay_since`
> is silently ignored for patterns).

> **Note:** `master_only = 1` creates the subscription only in the master
> process and is automatically removed from forked workers after `fork()`.

### `fio_pubsub_unsubscribe`

```c
int fio_pubsub_unsubscribe(fio_pubsub_subscribe_args_s args);
/* macro wrapper: */
#define fio_pubsub_unsubscribe(...) \
  fio_pubsub_unsubscribe((fio_pubsub_subscribe_args_s){__VA_ARGS__})
```

Remove a subscription. Returns `0` on success, `-1` if not found.

Only `io`, `channel`, `filter`, `is_pattern`, and `subscription_handle_ptr`
are used for matching — `on_message` and `udata` are ignored.

```c
fio_pubsub_unsubscribe(.io      = client_io,
                       .channel = FIO_BUF_INFO1("user:123"));

fio_pubsub_unsubscribe(.channel    = FIO_BUF_INFO1("chat:*"),
                       .is_pattern = 1);
```

---

## Publish

### `fio_pubsub_publish`

```c
void fio_pubsub_publish(fio_pubsub_publish_args_s args);
/* macro wrapper: */
#define fio_pubsub_publish(...) \
  fio_pubsub_publish((fio_pubsub_publish_args_s){__VA_ARGS__})
```

Publish a message. Thread-safe, callable from any process or thread. In
workers the default engine routes the message through IPC to the master, which
fans it out to all processes.

```c
/* Simple publish */
fio_pubsub_publish(.channel = FIO_BUF_INFO1("events"),
                   .message = FIO_BUF_INFO1("something happened"));

/* With namespace filter */
fio_pubsub_publish(.channel = FIO_BUF_INFO1("updates"),
                   .message = FIO_BUF_INFO2(data, data_len),
                   .filter  = 42);

/* Exclude the sender (echo suppression) */
fio_pubsub_publish(.channel = FIO_BUF_INFO1("chat:room1"),
                   .message = FIO_BUF_INFO1("hello"),
                   .from    = sender_io);

/* Cluster-wide (all machines) */
fio_pubsub_publish(.engine  = fio_pubsub_engine_cluster(),
                   .channel = FIO_BUF_INFO1("global"),
                   .message = FIO_BUF_INFO1("cluster alert"));
```

### `fio_pubsub_defer`

```c
void fio_pubsub_defer(fio_pubsub_msg_s *msg);
```

Call from inside `on_message` to push the *same* callback invocation to the
end of the queue. The message and subscription references are managed
automatically.

```c
void on_message(fio_pubsub_msg_s *msg) {
  if (not_ready_yet()) {
    fio_pubsub_defer(msg); /* try again later */
    return;
  }
  /* process msg… */
}
```

---

## Namespace Filters

The `filter` field (`int16_t`) is a numerical namespace. Publishers and
subscribers must use the same `filter` value to match. Negative values are
reserved for internal use.

Filters are useful when two unrelated subsystems share channel names. Using
`filter = 1` for one and `filter = 2` for the other keeps messages isolated
without any naming conventions.

```c
/* System 1 uses filter 1 */
fio_pubsub_subscribe(.channel = FIO_BUF_INFO1("status"), .filter = 1, …);
fio_pubsub_publish  (.channel = FIO_BUF_INFO1("status"), .filter = 1, …);

/* System 2 uses filter 2 — completely separate, same channel name */
fio_pubsub_subscribe(.channel = FIO_BUF_INFO1("status"), .filter = 2, …);
fio_pubsub_publish  (.channel = FIO_BUF_INFO1("status"), .filter = 2, …);
```

---

## IO Subscription Helper

### `FIO_ON_MESSAGE_SEND_MESSAGE`

```c
void FIO_ON_MESSAGE_SEND_MESSAGE(fio_pubsub_msg_s *msg);
```

A ready-made `on_message` callback that writes `msg->message` directly to
`msg->io`. Zero-copy: it duplicates the underlying IPC buffer reference and
hands it to the IO write queue.

```c
/* Wire clients to a channel with one line */
fio_pubsub_subscribe(.io         = client_io,
                     .channel    = FIO_BUF_INFO1("feed"),
                     .on_message = FIO_ON_MESSAGE_SEND_MESSAGE);
```

It can also serve as the protocol-level `on_pubsub` callback:

```c
fio_io_protocol_s MY_PROTOCOL = {
    .on_attach = my_on_attach,
    .on_data   = my_on_data,
    .on_pubsub = FIO_ON_MESSAGE_SEND_MESSAGE,
};
```

---

## Pattern Matching

### `fio_pubsub_match_fn_set`

```c
void fio_pubsub_match_fn_set(uint8_t (*match_cb)(fio_str_info_s pattern,
                                                  fio_str_info_s name));
```

Override the function used to test pattern subscriptions. Pass `NULL` to
restore the default (`fio_glob_match`), which supports `*`, `?`, and `[sets]`.

```c
uint8_t my_regex_match(fio_str_info_s pattern, fio_str_info_s name) {
  /* … custom logic … */
  return matched ? 1 : 0;
}
fio_pubsub_match_fn_set(my_regex_match);

fio_pubsub_match_fn_set(NULL); /* restore glob */
```

---

## Engines

Engines decide *how* messages are distributed. The default engine sends
messages to the local process group (master + all workers on the current
machine). Swap the engine to reach external brokers or remote machines.

### Built-in engines

| Function | Scope |
|----------|-------|
| `fio_pubsub_engine_ipc()` | master + all workers on **this machine** only |
| `fio_pubsub_engine_cluster()` | master + workers on **all machines** in the cluster |

```c
fio_pubsub_engine_s const *fio_pubsub_engine_ipc(void);
fio_pubsub_engine_s const *fio_pubsub_engine_cluster(void);
```

The default engine starts as `fio_pubsub_engine_ipc()` and is automatically
upgraded to `fio_pubsub_engine_cluster()` at `PRE_START` time when
`fio_ipc_cluster_port()` returns a non-zero value (i.e., when cluster IPC is
configured before `fio_io_start`).

### Default engine accessors

```c
fio_pubsub_engine_s const *fio_pubsub_engine_default(void);
fio_pubsub_engine_s const *fio_pubsub_engine_default_set(
    fio_pubsub_engine_s const *engine);
```

`fio_pubsub_engine_default_set(NULL)` restores the auto-selected default
(cluster engine if cluster port is set, IPC engine otherwise).

### Custom engine interface

```c
typedef struct fio_pubsub_engine_s {
  void (*detached)   (const struct fio_pubsub_engine_s *eng);
  void (*subscribe)  (const struct fio_pubsub_engine_s *eng,
                      const fio_buf_info_s channel, int16_t filter);
  void (*psubscribe) (const struct fio_pubsub_engine_s *eng,
                      const fio_buf_info_s channel, int16_t filter);
  void (*unsubscribe)(const struct fio_pubsub_engine_s *eng,
                      const fio_buf_info_s channel, int16_t filter);
  void (*punsubscribe)(const struct fio_pubsub_engine_s *eng,
                       const fio_buf_info_s channel, int16_t filter);
  void (*publish)    (const struct fio_pubsub_engine_s *eng,
                      const fio_pubsub_msg_s *msg);
} fio_pubsub_engine_s;
```

**Execution context:**
- `publish` — called from any thread or process
- all other callbacks — called from the **master process**, **IO thread** only

Callbacks must not block. Defer slow operations with `fio_io_defer`.

Missing callbacks are filled with safe defaults on `fio_pubsub_engine_attach`: subscription callbacks become no-ops; a NULL `publish` falls back to local IPC delivery.

### `fio_pubsub_engine_attach` / `fio_pubsub_engine_detach`

```c
void fio_pubsub_engine_attach(fio_pubsub_engine_s *engine);
void fio_pubsub_engine_detach(fio_pubsub_engine_s *engine);
```

`attach` registers the engine and immediately notifies it of all existing
channel and pattern subscriptions (via `subscribe` / `psubscribe`).

`detach` removes the engine and calls its `detached` callback. If the detached
engine was the current default, the default is automatically reset.

```c
/* Example: a custom engine that bridges to NATS */
static fio_pubsub_engine_s nats_engine = {
    .subscribe   = nats_subscribe,
    .psubscribe  = nats_subscribe,   /* reuse for patterns if needed */
    .unsubscribe = nats_unsubscribe,
    .punsubscribe= nats_unsubscribe,
    .publish     = nats_publish,
    .detached    = nats_cleanup,
};

/* attach before or after fio_io_start — both work */
fio_pubsub_engine_attach(&nats_engine);
fio_pubsub_engine_default_set(&nats_engine);
```

See [./422 redis.md](./422 redis.md) *(planned)* for the built-in Redis engine,
which is a production example of this interface.

---

## History / Replay

The history system caches messages for late-joining subscribers. History lives
in the **master process only** and is separate from the engine interface.

When `replay_since` is set on a subscription, the subscriber receives all
cached messages with `timestamp >= replay_since` before live messages begin.

### Built-in in-memory cache

```c
fio_pubsub_history_s const *fio_pubsub_history_cache(size_t size_limit);
```

Returns the built-in cache manager and sets its byte ceiling. The built-in
cache is initialized by default but **not attached automatically**; call
`fio_pubsub_history_attach(fio_pubsub_history_cache(size), priority)` to begin
storing messages for replay.

`size_limit = 0` inherits from environment variables in order:

1. `WEBSITE_MEMORY_LIMIT_MB` (× 1 MiB)
2. `WEBSITE_MEMORY_LIMIT_KB` (× 1 KiB)
3. `WEBSITE_MEMORY_LIMIT` (bytes)
4. `FIO_PUBSUB_HISTORY_DEFAULT_CACHE_SIZE_LIMIT` (256 MiB)

When the cache overflows its limit, the oldest messages across all channels
are evicted first.

```c
/* Attach the built-in cache with a 64 MiB ceiling */
fio_pubsub_history_attach(fio_pubsub_history_cache(64 * 1024 * 1024), 100);
```

### Attach / detach history managers

```c
int  fio_pubsub_history_attach(const fio_pubsub_history_s *manager,
                               uint8_t priority);
void fio_pubsub_history_detach(const fio_pubsub_history_s *manager);
```

Multiple managers can be active simultaneously. All receive `push()` on every
published message. For replay, managers are consulted in **descending priority
order** via `oldest()` to find a manager with enough history coverage; `replay()`
is called on the selected manager once.

### Custom history manager interface

```c
typedef struct fio_pubsub_history_s {
  void     (*detached)(const struct fio_pubsub_history_s *hist);
  int      (*push)    (const struct fio_pubsub_history_s *hist,
                       fio_pubsub_msg_s *msg);
  int      (*replay)  (const struct fio_pubsub_history_s *hist,
                       fio_buf_info_s channel,
                       int16_t filter,
                       uint64_t since,
                       void (*on_message)(fio_pubsub_msg_s *msg, void *udata),
                       void (*on_done)(void *udata),
                       void *udata);
  uint64_t (*oldest)  (const struct fio_pubsub_history_s *hist,
                       fio_buf_info_s channel,
                       int16_t filter);
} fio_pubsub_history_s;
```

**Execution context:** all callbacks — **master process**, **IO thread** only.
Do not block; use `fio_io_defer` for slow operations.

`replay` **must** call `on_done(udata)` when finished (success or failure) —
the IPC reply mechanism depends on it.

`replay` returns `0` if it handled the request. Returning `-1` does **not**
trigger fallback; use `oldest()` to return `UINT64_MAX` when the manager cannot
cover that channel/filter.

`oldest` returns `UINT64_MAX` if no history is available for the channel.

Missing callbacks are replaced with stubs on `fio_pubsub_history_attach`.

### `fio_pubsub_history_push_all`

```c
void fio_pubsub_history_push_all(fio_pubsub_msg_s *msg);
```

Push a message to all attached history managers without delivering it locally.
Use this from a custom engine when the engine receives a message that should be
stored for replay but is not routed through the normal publish path.

---

## Advanced: IPC Buffer Access

### `fio_pubsub_msg2ipc` / `fio_pubsub_ipc2msg`

```c
fio_ipc_s      *fio_pubsub_msg2ipc(fio_pubsub_msg_s *msg);
fio_pubsub_msg_s fio_pubsub_ipc2msg(fio_ipc_s *ipc);
```

`fio_pubsub_msg2ipc` returns (and detaches) the underlying `fio_ipc_s` for
the message. Use with `fio_ipc_dup` to keep the buffer alive beyond the
callback, enabling true zero-copy deferral.

`fio_pubsub_ipc2msg` is the inverse: reconstruct a `fio_pubsub_msg_s` from a
raw IPC buffer.

> **Fragile.** These functions bypass the normal lifecycle. Incorrect use will
> cause use-after-free bugs. Prefer `fio_pubsub_defer` for simple deferral.

---

## Execution Context Summary

| Operation | Where it runs |
|-----------|---------------|
| `fio_pubsub_publish` | any thread, any process |
| `fio_pubsub_subscribe` / `fio_pubsub_unsubscribe` | any thread, any process |
| `on_message` callback | IO thread, any process (per `queue`) |
| `on_unsubscribe` callback | IO thread, any process |
| Engine `subscribe` / `unsubscribe` / `psubscribe` / `punsubscribe` | **master**, IO thread |
| Engine `publish` | any thread, any process |
| History `push` / `replay` / `oldest` / `detached` | **master**, IO thread |

Callbacks must **not block** — offload slow work with `fio_io_defer` or
route to a background `fio_io_async_s` pool.

---

## Examples

### Chat server — per-connection subscription

```c
#define FIO_PUBSUB
#include FIO_INCLUDE_FILE

static void on_data(fio_io_s *io) {
  char buf[256];
  size_t len = fio_io_read(io, buf, sizeof(buf) - 1);
  if (!len) return;
  /* Publish whatever the client sends to the "chat" channel */
  fio_pubsub_publish(.channel = FIO_BUF_INFO1("chat"),
                     .message = FIO_BUF_INFO2(buf, len),
                     .from    = io); /* suppress echo */
}

static void on_attach(fio_io_s *io) {
  /* Every new client gets live chat messages forwarded to it */
  fio_pubsub_subscribe(.io         = io,
                       .channel    = FIO_BUF_INFO1("chat"),
                       .on_message = FIO_ON_MESSAGE_SEND_MESSAGE);
}

static fio_io_protocol_s CHAT_PROTOCOL = {
    .on_attach  = on_attach,
    .on_data    = on_data,
    .on_timeout = fio_io_touch,
};

int main(void) {
  fio_io_listen(.url = "0.0.0.0:3000", .protocol = &CHAT_PROTOCOL);
  fio_io_start(4); /* 4 worker processes */
  return 0;
}
```

### Pattern subscription

```c
static void on_any_chat(fio_pubsub_msg_s *msg) {
  printf("[%.*s]: %.*s\n",
         (int)msg->channel.len, msg->channel.buf,
         (int)msg->message.len, msg->message.buf);
}

fio_pubsub_subscribe(.channel    = FIO_BUF_INFO1("chat:*"),
                     .on_message = on_any_chat,
                     .is_pattern = 1);

/* Matches "chat:room1", "chat:lobby", "chat:anything" */
fio_pubsub_publish(.channel = FIO_BUF_INFO1("chat:room1"),
                   .message = FIO_BUF_INFO1("hello room"));
```

### History replay (late subscriber)

```c
static void on_news(fio_pubsub_msg_s *msg) {
  printf("[%llu] %.*s\n",
         (unsigned long long)msg->timestamp,
         (int)msg->message.len, msg->message.buf);
}

/* Subscribe and receive all messages from the last 10 minutes */
fio_pubsub_subscribe(.channel      = FIO_BUF_INFO1("news"),
                     .on_message   = on_news,
                     .replay_since = fio_io_last_tick() - 10*60*1000);
```

Attach the built-in cache, or add a custom persistence layer:

```c
/* Attach built-in cache with a 32 MiB ceiling */
fio_pubsub_history_attach(fio_pubsub_history_cache(32 * 1024 * 1024), 50);

/* Attach a custom database-backed manager at higher priority */
fio_pubsub_history_attach(&my_db_history_manager, 100);
```

### Cluster-wide publish

```c
int main(void) {
  /* Enable cluster RPC on port 9999 — must be set before fio_io_start */
  fio_ipc_cluster_listen(9999);
  /* Default engine auto-upgrades to cluster engine at PRE_START */

  fio_io_start(4);
  return 0;
}

/* Anywhere, after startup — goes to every machine in the cluster */
fio_pubsub_publish(.channel = FIO_BUF_INFO1("invalidate"),
                   .message = FIO_BUF_INFO1("cache_key_42"));

/* Or explicitly select the engine */
fio_pubsub_publish(.engine  = fio_pubsub_engine_cluster(),
                   .channel = FIO_BUF_INFO1("invalidate"),
                   .message = FIO_BUF_INFO1("cache_key_42"));
```

### Custom engine (skeleton)

```c
static void my_engine_subscribe(const fio_pubsub_engine_s *eng,
                                const fio_buf_info_s channel,
                                int16_t filter) {
  /* tell external broker to subscribe */
  (void)eng; (void)filter;
  external_broker_subscribe(channel.buf, channel.len);
}

static void my_engine_publish(const fio_pubsub_engine_s *eng,
                              const fio_pubsub_msg_s *msg) {
  /* forward to external broker */
  (void)eng;
  external_broker_publish(msg->channel.buf, msg->channel.len,
                          msg->message.buf, msg->message.len);
}

static void my_engine_cleanup(const fio_pubsub_engine_s *eng) {
  (void)eng;
  external_broker_disconnect();
}

static fio_pubsub_engine_s my_engine = {
    .subscribe    = my_engine_subscribe,
    .psubscribe   = my_engine_subscribe, /* same logic for patterns, or differentiate */
    .unsubscribe  = NULL,                /* NULL → filled with no-op on attach         */
    .punsubscribe = NULL,
    .publish      = my_engine_publish,
    .detached     = my_engine_cleanup,
};

/* Register before or after fio_io_start */
fio_pubsub_engine_attach(&my_engine);
fio_pubsub_engine_default_set(&my_engine);
```

---

## Related

- [./400 io-overview.md](./400 io-overview.md) — full IO/IPC/PubSub stack diagram
- [./404 ipc.md](./404 ipc.md) — IPC transport, `fio_ipc_cluster_listen`, `fio_ipc_s` lifetime
- [./422 redis.md](./422 redis.md) *(planned)* — Redis as a pub/sub engine and command client
# Redis — Pub/Sub Engine and Command Client (422 redis.h)

```c
#define FIO_REDIS
#include FIO_INCLUDE_FILE
```

> **Requires:** `FIO_IO`, `FIO_PUBSUB`, `FIO_FIOBJ`, `FIO_RESP3`. When using `include.h` all dependencies are resolved automatically.

The Redis module does two things: it acts as a **Pub/Sub engine** that connects facil.io's Pub/Sub system to a Redis server, enabling cross-machine message distribution; and it acts as a **Redis command client** for arbitrary commands (`GET`, `SET`, `INCR`, etc.).

See [./400 io-overview.md](./400 io-overview.md) for where Redis fits in the full IO stack, [./420 pubsub.md](./420 pubsub.md) for the Pub/Sub engine interface, [./404 ipc.md](./404 ipc.md) for the IPC transport workers use to reach the master, and [./250 fiobj.md](./250 fiobj.md) for how FIOBJ types map to RESP replies.

---

## Architecture

Only the **master** process opens TCP connections to Redis. Workers never connect directly. Instead:

- Workers forward `fio_redis_send()` calls to the master via IPC; the master executes the command and replies back.
- Workers forward `fio_pubsub_publish()` calls to the master via IPC; the master sends `PUBLISH` to Redis.
- Incoming subscription messages arrive on the master's subscription connection and are fanned out to all workers via the normal Pub/Sub IPC infrastructure.

The master maintains two separate TCP connections:

| Connection | Purpose |
|---|---|
| **pub_conn** | Commands (`fio_redis_send`), `PUBLISH`, `PING`, `AUTH` |
| **sub_conn** | `SUBSCRIBE` / `PSUBSCRIBE` push messages only |

Redis protocol requires this split: a connection in subscription mode cannot execute regular commands.

```
              Master Process
 ┌──────────────────────────────────────────┐
 │  pub_conn ──────────────────────────┐    │
 │  (commands / PUBLISH / PING / AUTH) │    │
 │                                     ▼    │
 │  sub_conn ──────── Redis Server          │
 │  (SUBSCRIBE / PSUBSCRIBE push)      ▲    │
 └──────────────────────────────────────────┘
         ▲                   │
         │ IPC               │ pub/sub IPC fan-out
         │                   ▼
 ┌────────────┐   ┌────────────┐   ┌────────────┐
 │  Worker 1  │   │  Worker 2  │   │  Worker N  │
 └────────────┘   └────────────┘   └────────────┘
```

In single-process mode (`fio_io_start(0)`) the process is both master and worker; all operations go directly to Redis with no IPC overhead.

---

## Setup

```c
#define FIO_LOG
#define FIO_REDIS
#include FIO_INCLUDE_FILE

int main(void) {
  /* Create the engine BEFORE fio_io_start() (before fork). */
  fio_pubsub_engine_s *redis = fio_redis_new(
      .url  = "redis://localhost:6379",
      .auth = "my_password"          /* optional */
  );

  /* Dup before attach — attach transfers your reference to the system. */
  fio_redis_dup(redis);              /* ref: 1 → 2 */
  fio_pubsub_engine_attach(redis);   /* system takes the original ref */

  fio_io_start(4);                   /* master connects; workers use IPC */

  fio_pubsub_engine_detach(redis);   /* system releases its ref: 2 → 1 */
  fio_redis_free(redis);             /* caller releases dup:  1 → 0 → destroy */
}
```

`fio_redis_new()` **must** be called before `fio_io_start()`. Creating an engine from a worker process is not supported.

---

## Configuration

### `FIO_REDIS_READ_BUFFER`

```c
#define FIO_REDIS_READ_BUFFER 32768   /* default: 32 KiB */
```

Read buffer size per connection. The master allocates `FIO_REDIS_READ_BUFFER × 2` bytes of buffer space inside each engine (one slice per connection). Increase this for workloads with very large individual Redis replies.

---

## Types

### `fio_redis_args_s`

```c
typedef struct {
  const char *url;          /* Redis server URL; NULL → "localhost:6379" */
  const char *auth;         /* AUTH password; NULL = no auth */
  size_t      auth_len;     /* Length of auth; 0 = strlen(auth) */
  uint8_t     ping_interval;/* Keepalive interval in seconds; 0 → 30 s */
} fio_redis_args_s;
```

**`url`** accepted formats:

| Format | Example |
|---|---|
| Scheme with port | `"redis://host:6380"` |
| Scheme, default port | `"redis://host"` |
| Host and port | `"host:6380"` |
| Host only | `"myredis"` |
| NULL or `""` | → `localhost:6379` |

**`ping_interval`** — the IO reactor sends a `PING` on each connection if it has been idle this many seconds. Default: **30 seconds**. The protocol error timeout (detecting a hung connection) is also governed by this value.

---

## Reference Counting and Ownership

The engine uses reference counting (`FIO_REF`). Three things independently hold or transfer refs:

1. **Caller**: `fio_redis_new()` returns `ref = 1`. The caller owns this ref and must release it with `fio_redis_free()`.

2. **Pub/Sub system**: `fio_pubsub_engine_attach()` prepares the engine for Pub/Sub use. The Redis engine keeps its Pub/Sub reference on first `SUBSCRIBE` / `PSUBSCRIBE` and releases it from `on_detached`; call `fio_redis_dup()` before attach if you need to keep using the pointer after attaching.

3. **Internal deferred tasks**: The engine dups before scheduling an IO-deferred task and frees at the end. These are invisible to the caller.

**Usage patterns:**

```c
/* Pattern A — with Pub/Sub */
fio_pubsub_engine_s *redis = fio_redis_new(...); /* ref = 1 */
fio_redis_dup(redis);                            /* ref = 2 (keep a copy) */
fio_pubsub_engine_attach(redis);                 /* system takes original ref */
/* ...use pub/sub... */
fio_pubsub_engine_detach(redis);                 /* system releases → ref = 1 */
fio_redis_free(redis);                           /* caller releases → ref = 0 → destroy */

/* Pattern B — command client only, no pub/sub */
fio_pubsub_engine_s *redis = fio_redis_new(...); /* ref = 1 */
/* ...send commands... */
fio_redis_free(redis);                           /* ref = 0 → destroy */
```

You do **not** have to detach before freeing. If you free while the pub/sub system still holds its ref, the engine remains alive until detach (or shutdown) fires the `on_detached` callback.

---

## API

### `fio_redis_new`

```c
fio_pubsub_engine_s *fio_redis_new(fio_redis_args_s args);
#define fio_redis_new(...)  fio_redis_new((fio_redis_args_s){__VA_ARGS__})
```

Creates a Redis engine with `ref = 1`. The macro overload enables named arguments.

Returns a pointer to `fio_pubsub_engine_s` on success, `NULL` on allocation failure.

The engine is typed as `fio_pubsub_engine_s *` so it can be passed directly to `fio_pubsub_engine_attach()`. Pass the same pointer to `fio_redis_dup()`, `fio_redis_free()`, and `fio_redis_send()`.

The connection is deferred to the IO reactor; the engine does nothing until `fio_io_start()` is called.

```c
fio_pubsub_engine_s *redis = fio_redis_new(
    .url           = "redis://10.0.0.5:6379",
    .auth          = "s3cr3t",
    .ping_interval = 60
);
```

### `fio_redis_dup`

```c
fio_pubsub_engine_s *fio_redis_dup(fio_pubsub_engine_s *engine);
```

Atomically increments the reference count and returns the engine. Returns `NULL` if `engine` is `NULL`.

Each `fio_redis_dup()` must be balanced with a `fio_redis_free()`.

### `fio_redis_free`

```c
void fio_redis_free(fio_pubsub_engine_s *engine);
```

Releases the caller's reference. When the count reaches zero, destroys the engine immediately: sets `running = 0`, closes both connections, drains the command queue (invoking any pending callbacks with `FIOBJ_INVALID`), and frees memory.

Safe to call with `NULL` (no-op).

### `fio_redis_send`

```c
int fio_redis_send(fio_pubsub_engine_s *engine,
                   FIOBJ command,
                   void (*callback)(fio_pubsub_engine_s *e,
                                    FIOBJ reply,
                                    void *udata),
                   void *udata);
```

Sends a Redis command. `command` must be a `FIOBJ_T_ARRAY` whose elements are the command verb and arguments as `FIOBJ_T_STRING` (or numbers). `callback` is optional (pass `NULL` to fire-and-forget).

Returns `0` on success, `-1` if `engine` is `NULL` or `command` is not a FIOBJ array.

**On the master**: the command is serialized to RESP and queued on `pub_conn`. Commands are sent one at a time (pipelined within the queue); the next command is sent after the reply for the previous one arrives.

**On a worker**: the RESP bytes are forwarded to the master via IPC. The master executes the command, serializes the reply to RESP, and sends it back. The worker deserializes and calls the callback on its IO thread. This is transparent to the caller.

**Never** pass `SUBSCRIBE`, `PSUBSCRIBE`, `UNSUBSCRIBE`, or `PUNSUBSCRIBE` to `fio_redis_send()`. These are managed internally on the subscription connection; sending them via the publishing connection violates the Redis protocol.

**Callback signature:**

```c
void my_callback(fio_pubsub_engine_s *e, FIOBJ reply, void *udata);
```

`reply` is `FIOBJ_INVALID` if the engine was destroyed or the connection was lost before the reply arrived. Otherwise it is a FIOBJ object:

| Redis response | FIOBJ type |
|---|---|
| Bulk string / simple string | `FIOBJ_T_STRING` |
| Integer | `FIOBJ_T_NUMBER` |
| Array | `FIOBJ_T_ARRAY` |
| Map (RESP3) | `FIOBJ_T_HASH` |
| Null | `fiobj_null()` |
| Boolean (RESP3) | `fiobj_true()` / `fiobj_false()` |
| Double (RESP3) | `FIOBJ_T_FLOAT` |
| Bignum (RESP3) | `FIOBJ_T_STRING` |
| Error | `FIOBJ_T_STRING` (warning logged) |

The callback owns nothing — `reply` is freed by the engine after the callback returns. Copy any data you need to keep.

---

## Building Commands

Commands are FIOBJ arrays of strings (and optionally numbers). Build them, pass to `fio_redis_send()`, then free them — the engine serializes to RESP before returning.

```c
/* SET mykey "hello" */
FIOBJ cmd = fiobj_array_new();
fiobj_array_push(cmd, fiobj_str_new_cstr("SET", 3));
fiobj_array_push(cmd, fiobj_str_new_cstr("mykey", 5));
fiobj_array_push(cmd, fiobj_str_new_cstr("hello", 5));
fio_redis_send(redis, cmd, NULL, NULL);   /* fire-and-forget */
fiobj_free(cmd);

/* GET mykey */
static void on_get(fio_pubsub_engine_s *e, FIOBJ reply, void *udata) {
  fio_str_info_s s = fiobj2cstr(reply);
  printf("value: %.*s\n", (int)s.len, s.buf);
  (void)e; (void)udata;
}

cmd = fiobj_array_new();
fiobj_array_push(cmd, fiobj_str_new_cstr("GET", 3));
fiobj_array_push(cmd, fiobj_str_new_cstr("mykey", 5));
fio_redis_send(redis, cmd, on_get, NULL);
fiobj_free(cmd);

/* INCR counter */
static void on_incr(fio_pubsub_engine_s *e, FIOBJ reply, void *udata) {
  printf("counter now: %ld\n", (long)fiobj2i(reply));
  (void)e; (void)udata;
}

cmd = fiobj_array_new();
fiobj_array_push(cmd, fiobj_str_new_cstr("INCR", 4));
fiobj_array_push(cmd, fiobj_str_new_cstr("counter", 7));
fio_redis_send(redis, cmd, on_incr, NULL);
fiobj_free(cmd);
```

Array replies (e.g., `LRANGE`, `HGETALL`):

```c
static void on_hgetall(fio_pubsub_engine_s *e, FIOBJ reply, void *udata) {
  size_t n = fiobj_array_count(reply);
  for (size_t i = 0; i + 1 < n; i += 2) {
    fio_str_info_s k = fiobj2cstr(fiobj_array_get(reply, (int32_t)i));
    fio_str_info_s v = fiobj2cstr(fiobj_array_get(reply, (int32_t)(i + 1)));
    printf("  %.*s = %.*s\n", (int)k.len, k.buf, (int)v.len, v.buf);
  }
  (void)e; (void)udata;
}

cmd = fiobj_array_new();
fiobj_array_push(cmd, fiobj_str_new_cstr("HGETALL", 7));
fiobj_array_push(cmd, fiobj_str_new_cstr("myhash", 6));
fio_redis_send(redis, cmd, on_hgetall, NULL);
fiobj_free(cmd);
```

See [./250 fiobj.md](./250 fiobj.md) for FIOBJ construction and inspection helpers.

---

## Pub/Sub Integration

When attached via `fio_pubsub_engine_attach()`, the Redis engine implements the `fio_pubsub_engine_s` interface:

| Pub/Sub event | Redis action |
|---|---|
| New channel subscription | `SUBSCRIBE channel` on `sub_conn` |
| New pattern subscription | `PSUBSCRIBE pattern` on `sub_conn` |
| Channel unsubscribe | `UNSUBSCRIBE channel` on `sub_conn` |
| Pattern unsubscribe | `PUNSUBSCRIBE pattern` on `sub_conn` |
| `fio_pubsub_publish(...)` | `PUBLISH channel message` on `pub_conn` (workers route via IPC) |
| Incoming Redis push message | Re-published locally with `fio_pubsub_engine_ipc()` to fan out to all processes |

The `filter` parameter from facil.io's Pub/Sub system is ignored by the Redis engine — Redis does not support numeric filter namespaces.

Example: subscribing to Redis channels through facil.io's Pub/Sub API:

```c
static fio_pubsub_engine_s *redis_engine; /* global, set before fork */

static void on_message(fio_pubsub_msg_s *msg) {
  printf("channel: %.*s  message: %.*s\n",
         (int)msg->channel.len, msg->channel.buf,
         (int)msg->message.len, msg->message.buf);
}

/* Called after IO reactor starts (FIO_CALL_ON_START) */
static void on_start(void *udata) {
  /* Regular subscription — Redis engine sends SUBSCRIBE */
  fio_pubsub_subscribe(.channel    = FIO_BUF_INFO1("alerts"),
                       .on_message = on_message);

  /* Pattern subscription — Redis engine sends PSUBSCRIBE */
  fio_pubsub_subscribe(.channel    = FIO_BUF_INFO1("events:*"),
                       .on_message = on_message,
                       .is_pattern = 1);

  /* Publish via Redis — specify the engine to route through Redis PUBLISH.
   * Without .engine the default IPC engine is used (local cluster only). */
  fio_pubsub_publish(.channel = FIO_BUF_INFO1("alerts"),
                     .message = FIO_BUF_INFO1("server started"),
                     .engine  = redis_engine);
  (void)udata;
}

int main(void) {
  redis_engine = fio_redis_new(.url = "localhost:6379");
  fio_redis_dup(redis_engine);            /* keep a copy before attaching */
  fio_pubsub_engine_attach(redis_engine); /* system takes the original ref */
  fio_state_callback_add(FIO_CALL_ON_START, on_start, NULL);
  fio_io_start(4);
  fio_pubsub_engine_detach(redis_engine);
  fio_redis_free(redis_engine);
}
```

See [./420 pubsub.md](./420 pubsub.md) for the full Pub/Sub API and engine interface.

---

## Multi-Process Behavior

The IPC routing is automatic and transparent:

| Call | On master | On worker |
|---|---|---|
| `fio_redis_send()` | Queued on `pub_conn` | Serialized → IPC → master → Redis → IPC reply → worker callback |
| `fio_pubsub_publish()` | `PUBLISH` on `pub_conn` | Forwarded via IPC; master sends `PUBLISH` |
| Subscription messages | Received on `sub_conn`, fanned out via Pub/Sub IPC | Delivered by Pub/Sub IPC from master |

Detection uses `fio_io_is_master()`. In single-process mode `fio_io_start(0)` everything uses the master path.

Worker processes inherit the engine pointer after fork. A `FIO_CALL_IN_CHILD` hook swaps the engine's `publish` vtable pointer to the IPC-forwarding implementation, so workers never accidentally write directly to a Redis socket.

See [./404 ipc.md](./404 ipc.md) for IPC internals.

---

## Reconnect and Failure Behavior

- **Connection loss**: The `on_close` callback logs a warning and schedules a reconnect via `fio_io_defer()`. Reconnection is attempted after a brief delay; on failure, it retries again.
- **Queued commands**: Commands in the queue on the master stay queued and are flushed once the publishing connection is re-established.
- **Resubscription**: When the subscription connection reconnects, the engine iterates the current Pub/Sub channel maps and re-sends all active `SUBSCRIBE` / `PSUBSCRIBE` commands directly. This restores subscription state without touching ref counts or re-attaching the engine.
- **Keepalive**: A `PING` is sent on each idle connection after `ping_interval` seconds. If the publishing connection has unacknowledged commands when the timeout fires, the connection is forcibly closed and reconnected.
- **Engine destroyed while commands are pending**: All queued commands have their callbacks invoked immediately with `reply = FIOBJ_INVALID`.

---

## Thread Safety

All public API functions are thread-safe:

| Function | Mechanism |
|---|---|
| `fio_redis_new()` | Defers connection to IO thread |
| `fio_redis_dup()` | Atomic reference count increment |
| `fio_redis_free()` | Atomic reference count decrement; destroy runs on caller thread |
| `fio_redis_send()` | Defers queue insertion to IO thread (master); IPC on workers |

Internal state (command queue, connection pointers) is only accessed from the IO thread. No locks are needed internally.

FIOBJ objects passed to callbacks are **not** thread-safe. Copy any data you need to keep or use outside the callback.

---

## Limitations

- `fio_redis_new()` must be called before `fio_io_start()` (i.e., before fork). Creating engines from worker processes is not supported.
- Redis's numeric filter namespaces (`filter` field in Pub/Sub) are not supported. All Redis pub/sub operates with `filter = 0`.
- Single-node Redis only. Redis Cluster requires connecting to the correct shard or using a proxy.
- Very large individual replies must fit within `FIO_REDIS_READ_BUFFER`. Increase the macro if needed.
# HTTP/1.x Parser

```c
#define FIO_HTTP1_PARSER
#include FIO_INCLUDE_FILE
```

Small parser, sharp teeth. Define `FIO_HTTP1_PARSER` to add the static
HTTP/1.x request / response parser used by the HTTP layer. It performs no heap
allocations, stores only parser state, and reports parsed data through callbacks
implemented by the including translation unit.

Nearby context: [IO and HTTP overview](./400 io-overview.md), the higher-level
[HTTP module header](./439 http.h), and the neighboring
[WebSocket parser header](./431 websocket parser.h).

---

## What Gets Added

`FIO_HTTP1_PARSER` exposes:

- `fio_http1_parser_s` — parser state.
- `FIO_HTTP1_PARSER_INIT` — zero-initializer / reset value.
- `fio_http1_parse` — incremental parser entry point.
- parser state helpers:
  - `fio_http1_parser_is_empty`
  - `fio_http1_parser_is_on_header`
  - `fio_http1_parser_is_on_body`
  - `fio_http1_expected`
- parse result / expected-body constants:
  - `FIO_HTTP1_PARSER_ERROR`
  - `FIO_HTTP1_EXPECTED_CHUNKED`
- required user callbacks named `fio_http1_on_*`.

The implementation also declares internal parsing stages named with
`fio_http1___...`; these are private implementation details.

---

## Parser State

### `fio_http1_parser_s`

```c
typedef struct fio_http1_parser_s fio_http1_parser_s;

struct fio_http1_parser_s {
  int (*fn)(fio_http1_parser_s *, fio_buf_info_s *, void *);
  size_t expected;
};
```

The parser state is intentionally tiny: one function pointer for the current
state-machine stage and one `expected` byte counter / sentinel.

Treat both fields as opaque. Allocate the struct wherever it fits your lifetime
(stack, connection object, arena, etc.), initialize it with
`FIO_HTTP1_PARSER_INIT`, and use the helper functions to inspect state.

### `FIO_HTTP1_PARSER_INIT`

```c
#define FIO_HTTP1_PARSER_INIT ((fio_http1_parser_s){0})
```

Zero-initializes a parser:

```c
fio_http1_parser_s parser = FIO_HTTP1_PARSER_INIT;
```

The parser also resets itself to this empty state after a complete message is
reported with `fio_http1_on_complete`.

---

## Parsing API

### `fio_http1_parse`

```c
FIO_SFUNC size_t fio_http1_parse(fio_http1_parser_s *p,
                                 fio_buf_info_s buf,
                                 void *udata);
```

Parses as much HTTP/1.x data as currently possible and invokes callbacks as
fields are discovered.

- `p` is the parser state.
- `buf` is the current readable bytes.
- `udata` is passed unchanged to every callback.

Returns the number of bytes consumed from `buf`, or
`FIO_HTTP1_PARSER_ERROR` (`(size_t)-1`) on parse / callback error.

A successful return may consume fewer bytes than supplied. Any unconsumed bytes
belong to a later parse step or to the next HTTP message on a keep-alive
connection.

If the parser needs more bytes before it can make progress, it returns
successfully with the bytes consumed so far, which can be `0`.

### State Helpers

```c
FIO_IFUNC size_t fio_http1_parser_is_empty(fio_http1_parser_s *p);
FIO_IFUNC size_t fio_http1_parser_is_on_header(fio_http1_parser_s *p);
FIO_IFUNC size_t fio_http1_parser_is_on_body(fio_http1_parser_s *p);
FIO_IFUNC size_t fio_http1_expected(fio_http1_parser_s *p);
```

- `fio_http1_parser_is_empty` returns non-zero when the parser is waiting for a
  new request / response line.
- `fio_http1_parser_is_on_header` returns non-zero while reading regular
  headers or chunked trailer headers.
- `fio_http1_parser_is_on_body` returns non-zero while reading a known-length
  body or while the chunked parser is ready to read the next chunk frame. During
  a split chunk payload, the internal chunk-read stage may report false even
  though body bytes are still being drained.
- `fio_http1_expected` returns the parser's current expected byte count, returns
  `FIO_HTTP1_EXPECTED_CHUNKED` after a chunked body is detected and before the
  next chunk-size line is parsed, and returns `0` when the parser's internal
  state marks the message as having no body. During chunked payload delivery,
  it may expose the current chunk size / remaining chunk bytes.

### Constants

```c
#define FIO_HTTP1_PARSER_ERROR ((size_t)-1)
#define FIO_HTTP1_EXPECTED_CHUNKED ((size_t)(-2))
```

`FIO_HTTP1_PARSER_ERROR` is the error return value from `fio_http1_parse`.
After a parse error, close / discard the stream state rather than attempting to
recover the same parser instance.

`FIO_HTTP1_EXPECTED_CHUNKED` is the parser's sentinel after
`transfer-encoding: chunked` is accepted and before a chunk-size line is parsed.
Once chunk parsing starts, `fio_http1_expected` may instead report the current
chunk size or remaining chunk bytes.

The header also defines `FIO___HTTP1_BODY_NOT_ALLOWED` as an internal sentinel
for methods / states where a body should not be read. User code should rely on
`fio_http1_expected(p) == 0` instead of using that internal macro.

---

## Callback Contract

The parser declares these callbacks as `static` prototypes. The including
translation unit must define them.

For portable user code, every `int` callback should return only `0` to continue
or `-1` to reject the parse.

The parser's internal checks are not identical for every callback:

- `fio_http1_on_method`, `fio_http1_on_url`, `fio_http1_on_version`,
  `fio_http1_on_status`, and `fio_http1_on_body_chunk` treat any non-zero return
  as a parse error.
- `fio_http1_on_header` during normal headers and
  `fio_http1_on_header_content_length` reject only an exact `-1` return.
- `fio_http1_on_header` during chunked trailers is passed through; negative
  values become parse errors and positive values stop the current parse as
  incomplete.
- `fio_http1_on_expect` is special: any non-zero return rejects the expectation,
  resets the parser, and stops the current parse without calling
  `fio_http1_on_complete`.

All `fio_buf_info_s` values point into the `buf` memory passed to
`fio_http1_parse`. They are not NUL-terminated unless the input happened to be.
Copy or retain the data before the callback returns if it must outlive the input
buffer.

> Important: the parser lowercases header names in-place. Feed it writable
> memory, not a string literal or read-only mapping.

### Completion

```c
static void fio_http1_on_complete(void *udata);
```

Called after the request / response line, headers, and any body have been fully
parsed. The parser is reset before this callback is invoked, so it is ready for
the next message on the same connection.

### Request Line Callbacks

```c
static int fio_http1_on_method(fio_buf_info_s method, void *udata);
static int fio_http1_on_url(fio_buf_info_s path, void *udata);
static int fio_http1_on_version(fio_buf_info_s version, void *udata);
```

For request lines, the parser calls the callbacks in this order:

1. `fio_http1_on_method`
2. `fio_http1_on_url`
3. `fio_http1_on_version`

The version slice is clamped to at most 14 bytes. `GET`, `HEAD`, and `OPTIONS`
are recognized case-insensitively and mark the parser as not expecting a body;
body-bearing headers for these methods conflict with that marker and are
rejected.

### Response Line Callbacks

```c
static int fio_http1_on_version(fio_buf_info_s version, void *udata);
static int fio_http1_on_status(size_t istatus,
                               fio_buf_info_s status,
                               void *udata);
```

For response lines, the parser calls `fio_http1_on_version` first and then
`fio_http1_on_status`.

`istatus` is parsed from the numeric status token. `status` is the remaining
status text slice after the numeric token. The version slice is clamped to at
most 14 bytes.

The parser decides whether the first line is a response by checking whether the
second token starts with a decimal digit.

### Headers

```c
static int fio_http1_on_header(fio_buf_info_s name,
                               fio_buf_info_s value,
                               void *udata);

static int fio_http1_on_header_content_length(fio_buf_info_s name,
                                              fio_buf_info_s value,
                                              size_t content_length,
                                              void *udata);
```

For ordinary headers, `fio_http1_on_header` receives:

- `name` lowercased in-place.
- `value` trimmed of leading and trailing spaces / tabs.
- an empty value as `{ .buf = NULL, .len = 0 }`.

Header names must contain a valid `:` separator and may not contain the
forbidden characters encoded by the parser. NUL bytes in header values are
rejected.

`content-length` is special:

- empty values are rejected;
- non-decimal / overflowing values are rejected;
- values colliding with internal sentinels are rejected;
- duplicate `content-length` headers must agree;
- conflicting `content-length` and final `transfer-encoding: chunked` are
  rejected;
- the first non-zero accepted value calls `fio_http1_on_header_content_length`
  instead of the generic header callback; a repeated matching value is accepted
  without calling the content-length callback again.

A `content-length: 0` value marks the message as having no body and does not
call `fio_http1_on_header_content_length`.

`transfer-encoding` is also special when its final token is `chunked`
(case-insensitive):

- the parser switches to chunked body decoding;
- if the value is exactly `chunked`, no generic header callback is made;
- if other transfer-coding text appears before the final `chunked` token, the
  final `chunked` token and adjacent separators are stripped before the
  remaining value is passed to `fio_http1_on_header`;
- malformed separators before the final `chunked` token are rejected.

`expect` is special when its value is exactly `100-continue`. Any other
`Expect` value is rejected.

### Expect: 100-continue

```c
static int fio_http1_on_expect(void *udata);
```

Called after headers when an accepted `Expect: 100-continue` header requires a
post-header decision and the parser has a non-zero body expectation marker.
Return `0` to continue into the body / completion flow. Return non-zero to reset
the parser and stop the current parse without calling `fio_http1_on_complete`.

### Body Chunks

```c
static int fio_http1_on_body_chunk(fio_buf_info_s chunk, void *udata);
```

Called with decoded body bytes.

For `Content-Length` bodies, callback chunks follow the supplied input chunks
and sum to the accepted content length.

For chunked bodies, framing bytes are removed before callback delivery. Large or
split HTTP chunks may be delivered through more than one callback if the input
arrives in smaller pieces.

---

## Parser Flow

```text
start line
  ├─ request  -> method -> url -> version
  └─ response -> version -> status
headers
  ├─ no body / body not allowed -> complete
  ├─ content-length body        -> body chunks -> complete
  └─ chunked body               -> chunk chunks -> trailers -> complete
```

Details worth keeping in mind:

- Leading spaces, `\r`, and `\n` before the first line are skipped.
- First lines shorter than the parser's minimum accepted shape are rejected.
- NUL bytes in the first line are rejected.
- Header and first-line parsing waits for a newline before making progress.
- The parser accepts `\n` line endings and handles an optional preceding `\r`.
- Chunk size lines are hexadecimal, capped by the implementation, and do not
  support chunk extensions.
- A zero-size chunk either completes immediately when followed by an empty line
  or enters trailer parsing.

---

## Chunked Trailers

Allowed trailer headers are reported through `fio_http1_on_header` after the
body's terminating zero-size chunk.

The parser rejects the following trailer names:

- `authorization`
- `cache-control`
- `content-encoding`
- `content-length`
- `content-range`
- `content-type`
- `expect`
- `host`
- `max-forwards`
- `set-cookie`
- `te`
- `trailer`
- `transfer-encoding`

---

## Ownership and Lifetime

- The parser allocates no memory.
- The parser does not copy callback data.
- The parser mutates header names in the input buffer to lowercase.
- Callback slices are valid only while the input buffer remains valid and
  unchanged.
- `udata` is never owned by the parser; it is simply forwarded.
- The parser state may live inside a connection object and be reused for
  keep-alive messages. It resets automatically on complete messages.
- After `FIO_HTTP1_PARSER_ERROR`, discard the parser / connection state.

---

## Minimal Skeleton

```c
#define FIO_HTTP1_PARSER
#include FIO_INCLUDE_FILE

static int fio_http1_on_method(fio_buf_info_s method, void *udata) {
  (void)method;
  (void)udata;
  return 0;
}

static int fio_http1_on_url(fio_buf_info_s path, void *udata) {
  (void)path;
  (void)udata;
  return 0;
}

static int fio_http1_on_version(fio_buf_info_s version, void *udata) {
  (void)version;
  (void)udata;
  return 0;
}

static int fio_http1_on_status(size_t status_code,
                               fio_buf_info_s status,
                               void *udata) {
  (void)status_code;
  (void)status;
  (void)udata;
  return 0;
}

static int fio_http1_on_header(fio_buf_info_s name,
                               fio_buf_info_s value,
                               void *udata) {
  (void)name;
  (void)value;
  (void)udata;
  return 0;
}

static int fio_http1_on_header_content_length(fio_buf_info_s name,
                                              fio_buf_info_s value,
                                              size_t content_length,
                                              void *udata) {
  (void)name;
  (void)value;
  (void)udata;
  return content_length > (1UL << 20) ? -1 : 0;
}

static int fio_http1_on_expect(void *udata) {
  (void)udata;
  return 0;
}

static int fio_http1_on_body_chunk(fio_buf_info_s chunk, void *udata) {
  (void)chunk;
  (void)udata;
  return 0;
}

static void fio_http1_on_complete(void *udata) {
  (void)udata;
}

size_t parse_some_http(char *data, size_t len, void *udata) {
  fio_http1_parser_s parser = FIO_HTTP1_PARSER_INIT;
  return fio_http1_parse(&parser, FIO_BUF_INFO2(data, len), udata);
}
```

For real incremental parsing, keep `fio_http1_parser_s` with the connection and
preserve / retry unconsumed bytes when `fio_http1_parse` returns less than the
available buffer length.
# WebSocket Parser

```c
#define FIO_WEBSOCKET_PARSER
#include FIO_INCLUDE_FILE
```

Small RFC 6455 frame parser and writer. It keeps 24 bytes of parser state,
allocates nothing, unmasks incoming payload bytes in-place, and reports one
parse event at a time.

Nearby context: [IO and HTTP overview](./400 io-overview.md), the neighboring
[HTTP/1.x parser](./431 http1 parser.md), the higher-level
[HTTP module](./439 http.md), and WebSocket compression support in
[DEFLATE / Gzip](./162 deflate.md).

---

## What Gets Added

`FIO_WEBSOCKET_PARSER` exposes:

- `fio_websocket_s` — incremental parser state.
- `fio_websocket_event_s` — one parse event returned by
  `fio_websocket_parse`.
- `fio_websocket_init` / `fio_websocket_reset` — parser lifecycle helpers.
- `fio_websocket_parse` — incremental parser entry point.
- frame writer helpers for server and client data, ping, pong, and close
  frames.
- WebSocket close code, event type, opcode, RSV, parser state, flag, and error
  constants.

The implementation also defines helpers named with `fio___websocket...`; those
are private implementation details.

---

## Parser State

### `fio_websocket_s`

```c
typedef struct fio_websocket_s {
  uint64_t frame_remaining;
  uint32_t mask;
  uint32_t frame_consumed;
  uint16_t close_code;
  uint8_t state;
  uint8_t flags;
  uint8_t flags2;
  uint8_t reserved;
} fio_websocket_s;
```

The parser state is intentionally small and is asserted to fit in 24 bytes.
Allocate it wherever the connection state lives, initialize it before use, and
prefer the public helper macros below instead of editing fields by hand.

```c
FIO_WEBSOCKET_GET_FIN(p)
FIO_WEBSOCKET_GET_MASKED(p)
FIO_WEBSOCKET_GET_OPCODE(p)
FIO_WEBSOCKET_GET_MSG_OPCODE(p)
FIO_WEBSOCKET_GET_PAUSED(p)
FIO_WEBSOCKET_GET_MSG_RSV(p)
```

`GET_OPCODE` reports the current frame opcode. `GET_MSG_OPCODE` reports the
open message opcode (`FIO_WEBSOCKET_OP_TEXT`, `FIO_WEBSOCKET_OP_BINARY`, or
`0` when no message is open). `GET_MSG_RSV` reports the RSV bits from the
opening data frame in the same 3-bit format used by the writer API.

### Lifecycle

```c
FIO_IFUNC void fio_websocket_init(fio_websocket_s *p);
FIO_IFUNC void fio_websocket_reset(fio_websocket_s *p);
```

Both helpers clear the struct and set the parser to the header-reading state.
After a protocol error or after receiving a close frame, discard or reset the
parser before reusing it.

---

## Events

### `fio_websocket_event_s`

```c
typedef struct {
  uint8_t type;
  uint8_t opcode;
  uint8_t is_text;
  uint8_t rsv;
  uint8_t is_first;
  uint8_t is_last;
  fio_buf_info_s payload;
  uint16_t close_code;
} fio_websocket_event_s;
```

`payload` points into the input buffer passed to `fio_websocket_parse`. If the
incoming frame is masked, that memory is unmasked in-place before the event is
returned. Keep the input bytes writable and alive until the event payload is no
longer needed.

### Event Types

```c
FIO_WEBSOCKET_EV_NONE
FIO_WEBSOCKET_EV_DATA_CHUNK
FIO_WEBSOCKET_EV_CONTROL
FIO_WEBSOCKET_EV_MESSAGE_END
FIO_WEBSOCKET_EV_ERROR
```

The current parser reports message data with `FIO_WEBSOCKET_EV_DATA_CHUNK` and
marks message boundaries with `is_first` and `is_last`. Control frames are
reported whole with `FIO_WEBSOCKET_EV_CONTROL`. Protocol errors are reported as
`FIO_WEBSOCKET_EV_ERROR` when an event pointer is supplied.

`FIO_WEBSOCKET_EV_MESSAGE_END` is defined for API completeness, but the current
parse loop uses the `is_last` flag on data events instead of emitting a separate
message-end event.

### Event Fields

For data events:

- `opcode` is the current frame opcode. Continuation frames report
  `FIO_WEBSOCKET_OP_CONT`.
- `is_text` is true when the open message started as a text message.
- `rsv` is copied from the opening data frame.
- `is_first` is true for the first chunk of the opening data frame.
- `is_last` is true for the final chunk of the message.
- `payload` is the available payload chunk, possibly length `0`.

For control events:

- `opcode` is `FIO_WEBSOCKET_OP_CLOSE`, `FIO_WEBSOCKET_OP_PING`, or
  `FIO_WEBSOCKET_OP_PONG`.
- `payload` is the complete control payload, never a partial control frame.
- close frames set `close_code` to the on-wire close code, or
  `FIO_WEBSOCKET_CLOSE_NO_STATUS` for an empty close payload.

---

## Parsing API

```c
FIO_SFUNC size_t fio_websocket_parse(fio_websocket_s *p,
                                     fio_buf_info_s buf,
                                     fio_websocket_event_s *ev);
```

Parses WebSocket bytes from `buf` and returns the number of bytes consumed, or
`FIO_WEBSOCKET_PARSE_ERROR` (`(size_t)-1`) on protocol error.

A successful call may consume fewer bytes than supplied. Feed the remaining
bytes to the next call, usually in a loop. If more bytes are needed before an
event can be produced, the function returns the bytes consumed so far, which can
be `0`, and leaves `ev->type` as `FIO_WEBSOCKET_EV_NONE`.

The parser is pure state plus the caller-provided buffer:

- no heap allocation;
- no callbacks;
- no retained payload pointers;
- no internal message accumulator;
- no control-frame buffering beyond waiting until a complete control payload is
  available in the supplied input.

`ev` may be `NULL` if the caller only wants to advance or validate input, but
then event details and close/error codes must be read from parser state where
available.

### Parser Flow

```text
header
  ├─ incomplete header       -> wait for more bytes
  ├─ control frame           -> wait for full payload -> CONTROL event
  └─ data / continuation     -> unmask available bytes -> DATA_CHUNK event
                                  └─ is_last marks message completion
```

The parser returns after at most one event. For fragmented messages, each data
frame may produce one or more data chunks depending on the input buffer splits.
Control frames may appear between fragmented data frames and are reported as
control events without closing the open message.

### Protocol Checks Performed

The parser rejects:

- unknown opcodes;
- fragmented control frames;
- continuation frames without an open message;
- nested text/binary messages while another data message is open;
- control payloads larger than 125 bytes;
- 64-bit lengths with the high bit set;
- frames larger than `FIO_WEBSOCKET_DEFAULT_MAX_FRAME`;
- close frames with a 1-byte payload;
- close frames carrying invalid on-wire close codes.

On rejection, the parser state becomes `FIO_WEBSOCKET_STATE_ERROR`,
`p->close_code` is set, and `fio_websocket_parse` returns
`FIO_WEBSOCKET_PARSE_ERROR`.

Caller policy still includes:

- enforcing client/server masking rules;
- interpreting RSV bits and applying extension transforms such as
  `permessage-deflate`;
- enforcing total message size limits;
- validating UTF-8 for text messages;
- accumulating message chunks if whole-message delivery is desired.

---

## Frame Writers

Server writers produce unmasked frames. Client writers produce masked frames;
passing `mask == 0` asks the writer to generate a non-zero PRNG mask.

### Sizing

```c
FIO_IFUNC uint64_t fio_websocket_write_len(uint64_t payload_len, _Bool masked);
```

Returns the number of bytes required for one complete frame with the requested
payload length and masking mode. Allocate at least this many bytes before
calling a writer.

### Data Messages

```c
FIO_IFUNC uint64_t fio_websocket_write_message_server(void *target,
                                                      fio_buf_info_s msg,
                                                      _Bool is_text,
                                                      uint8_t rsv);
FIO_IFUNC uint64_t fio_websocket_write_message_client(void *target,
                                                      fio_buf_info_s msg,
                                                      _Bool is_text,
                                                      uint32_t mask,
                                                      uint8_t rsv);
```

Writes one complete FIN data message to `target` and returns the bytes written.
`is_text` selects text (`1`) or binary (`0`). `rsv` is the 3-bit RSV value;
usually pass `0`, or `FIO_WEBSOCKET_RSV1` for a compressed
`permessage-deflate` message after applying the extension transform.

### Ping / Pong

```c
FIO_IFUNC uint64_t fio_websocket_write_ping_server(void *target,
                                                   fio_buf_info_s payload);
FIO_IFUNC uint64_t fio_websocket_write_ping_client(void *target,
                                                   fio_buf_info_s payload,
                                                   uint32_t mask);
FIO_IFUNC uint64_t fio_websocket_write_pong_server(void *target,
                                                   fio_buf_info_s payload);
FIO_IFUNC uint64_t fio_websocket_write_pong_client(void *target,
                                                   fio_buf_info_s payload,
                                                   uint32_t mask);
```

Writes one FIN control frame. Keep ping and pong payloads at 125 bytes or less;
the writer does not add a separate policy check for that RFC limit.

### Close

```c
FIO_IFUNC uint64_t fio_websocket_write_close_server(void *target,
                                                    uint16_t code,
                                                    fio_buf_info_s reason);
FIO_IFUNC uint64_t fio_websocket_write_close_client(void *target,
                                                    uint16_t code,
                                                    fio_buf_info_s reason,
                                                    uint32_t mask);
```

Writes a close frame containing the 2-byte close code followed by the optional
reason. The reason is truncated so the whole close payload fits in the 125-byte
control-frame limit. Choose a close code that is valid to send on the wire.

---

## Constants

### Opcodes

```c
FIO_WEBSOCKET_OP_CONT
FIO_WEBSOCKET_OP_TEXT
FIO_WEBSOCKET_OP_BINARY
FIO_WEBSOCKET_OP_CLOSE
FIO_WEBSOCKET_OP_PING
FIO_WEBSOCKET_OP_PONG
```

### Close Codes

```c
FIO_WEBSOCKET_CLOSE_OK
FIO_WEBSOCKET_CLOSE_GOING_AWAY
FIO_WEBSOCKET_CLOSE_PROTOCOL_ERROR
FIO_WEBSOCKET_CLOSE_UNSUPPORTED_DATA
FIO_WEBSOCKET_CLOSE_NO_STATUS
FIO_WEBSOCKET_CLOSE_INVALID_PAYLOAD
FIO_WEBSOCKET_CLOSE_POLICY_VIOLATION
FIO_WEBSOCKET_CLOSE_MESSAGE_TOO_BIG
FIO_WEBSOCKET_CLOSE_MANDATORY_EXT
FIO_WEBSOCKET_CLOSE_INTERNAL_ERROR
```

`FIO_WEBSOCKET_CLOSE_NO_STATUS` is synthesized for an empty received close
payload and must not be sent as an on-wire status code. The parser also accepts
valid registered wire codes such as 1012-1014 and application/library codes in
the 3000-4999 range.

### RSV Bits

```c
FIO_WEBSOCKET_RSV1
FIO_WEBSOCKET_RSV2
FIO_WEBSOCKET_RSV3
```

These are 3-bit values for the writer API and event `rsv` field. The writer
shifts them into byte-0 bits 4..6 on the wire.

### Parser States and Limits

```c
FIO_WEBSOCKET_STATE_HEADER
FIO_WEBSOCKET_STATE_PAYLOAD
FIO_WEBSOCKET_STATE_CLOSED
FIO_WEBSOCKET_STATE_ERROR

FIO_WEBSOCKET_DEFAULT_MAX_FRAME
FIO_WEBSOCKET_PARSE_ERROR
```

`FIO_WEBSOCKET_DEFAULT_MAX_FRAME` defaults to 1 GiB and may be overridden before
including the header. `FIO_WEBSOCKET_PARSE_ERROR` is the parse error sentinel.

The header also exposes flag bit masks used by the accessor macros:
`FIO_WEBSOCKET_FLAG_FIN`, `FIO_WEBSOCKET_FLAG_MASKED`,
`FIO_WEBSOCKET_FLAG_OPCODE_MASK`, `FIO_WEBSOCKET_FLAG_OPCODE_SHIFT`,
`FIO_WEBSOCKET_FLAG_MSG_OPCODE_MASK`, `FIO_WEBSOCKET_FLAG2_PAUSED`,
`FIO_WEBSOCKET_FLAG2_MSG_RSV_MASK`, and
`FIO_WEBSOCKET_FLAG2_MSG_RSV_SHIFT`.

---

## Minimal Parse Loop

```c
#define FIO_WEBSOCKET_PARSER
#include FIO_INCLUDE_FILE

typedef struct {
  fio_websocket_s ws;
} connection_s;

void websocket_consume(connection_s *c, char *data, size_t len) {
  fio_buf_info_s buf = FIO_BUF_INFO2(data, len);

  while (buf.len) {
    fio_websocket_event_s ev;
    size_t n = fio_websocket_parse(&c->ws, buf, &ev);

    if (n == FIO_WEBSOCKET_PARSE_ERROR) {
      /* Send/record c->ws.close_code and close the connection. */
      return;
    }

    buf.buf += n;
    buf.len -= n;

    if (!n && ev.type == FIO_WEBSOCKET_EV_NONE)
      break; /* wait for more network bytes */

    switch (ev.type) {
    case FIO_WEBSOCKET_EV_DATA_CHUNK:
      /* ev.payload is already unmasked and points into data. */
      if (ev.is_first) {
        /* start a message accumulator, if needed */
      }
      if (ev.payload.len) {
        /* copy/process this chunk before reusing data */
      }
      if (ev.is_last) {
        /* finish the message */
      }
      break;

    case FIO_WEBSOCKET_EV_CONTROL:
      if (ev.opcode == FIO_WEBSOCKET_OP_PING) {
        char out[128 + 14];
        uint64_t written = fio_websocket_write_pong_server(out, ev.payload);
        (void)written; /* write out to the socket */
      } else if (ev.opcode == FIO_WEBSOCKET_OP_CLOSE) {
        /* Echo/close according to local policy. */
        return;
      }
      break;

    default:
      break;
    }
  }
}

void websocket_open(connection_s *c) {
  fio_websocket_init(&c->ws);
}
```

For real IO, preserve any unconsumed bytes and retry when more data arrives.
Copy payload bytes before recycling the read buffer or applying asynchronous
message handling.

---

## Minimal Write Example

```c
char out[14 + 1024];
fio_buf_info_s msg = FIO_BUF_INFO2("hello", 5);

uint64_t len = fio_websocket_write_message_server(out, msg, 1, 0);
/* write `len` bytes from out */
```

For client frames:

```c
uint64_t len = fio_websocket_write_message_client(out, msg, 1, 0, 0);
```

The fourth argument is an explicit mask; `0` lets the writer choose one.

---

## Ownership and Lifetime

- The parser owns only `fio_websocket_s` state supplied by the caller.
- The parser never allocates, frees, stores callbacks, or retains payload
  pointers after returning.
- Input buffers passed to `fio_websocket_parse` must be writable because masked
  payloads are modified in-place.
- Event payload slices are valid only while the input buffer remains valid and
  unchanged.
- Writer targets must be large enough for `fio_websocket_write_len(payload.len,
  masked)` bytes.
- Independent parser instances can be used concurrently by different threads;
  synchronize shared buffers and connection state in the caller.
# HTTP Module

```c
#define FIO_HTTP
#include FIO_INCLUDE_FILE
```

`FIO_HTTP` adds the higher-level HTTP service built on the facil.io IO layer,
the HTTP handle, the HTTP/1.x parser, the WebSocket parser, and the SSE /
WebSocket glue code.

Nearby context: [IO and HTTP overview](./400 io-overview.md), the underlying
[HTTP handle header](./431 http handle.h), the [HTTP/1.x parser](./431 http1 parser.md),
the [WebSocket parser](./431 websocket parser.md), and optional compression
support in [DEFLATE / Gzip](./162 deflate.md).

---

## What Gets Added

`FIO_HTTP` exposes:

- `fio_http_settings_s` and `fio_http_listener_s`.
- server APIs: `fio_http_listen`, `fio_http_listener_settings`,
  `fio_http_route`, and `fio_http_route_settings`.
- client API: `fio_http_connect`.
- request routing helper: `fio_http_resource_action` and
  `fio_http_resource_action_e`.
- upgrade helpers for WebSocket and SSE connections.
- pub/sub helpers for upgraded connections.
- handle access helpers: `fio_http_io` and `fio_http_settings`.
- the HTTP handle API from [`431 http handle.h`](./431 http handle.h),
  including request / response fields, headers, cookies, body storage, writing,
  static-file responses, logging, MIME lookup, and controller hooks.

Implementation helpers named `fio___...` are private implementation details.

---

## Connection Model

HTTP/1.x requests and client responses are parsed into `fio_http_s` handles.
The HTTP layer suspends the IO object while the user callback is running and
resumes it after the response is finished or the upgrade is installed.

Plain HTTP cycles use:

1. request / response parsing;
2. optional `pre_http_body` before an `Expect: 100-continue` body is read;
3. `on_http` for application logic;
4. `fio_http_write` / `fio_http_finish` to send the response;
5. `on_finish` when the non-upgraded cycle is complete.

WebSocket and SSE cycles use the authentication callbacks before upgrade, then
`on_open`, message / event callbacks, `on_ready`, `on_shutdown`, `on_close`, and
finally `on_finish` when the upgraded connection closes.

User callbacks are scheduled through the selected HTTP task queue. If
`fio_http_settings_s.queue` is not supplied, the current IO queue is used.

---

## Settings and Listener API

### `fio_http_settings_s`

```c
typedef struct fio_http_settings_s fio_http_settings_s;
```

`fio_http_settings_s` is used by server listeners, routes, and client
connections. The function-style APIs are shadowed by named-argument macros, so
settings are usually passed as designated initializers.

Callback fields:

| Field | Used for |
| --- | --- |
| `pre_http_body` | Called before body upload when the request used `Expect`. |
| `on_http` | Server request callback, or client response callback. |
| `on_finish` | Called when a plain request / response cycle completes, and after upgraded close cleanup. |
| `on_authenticate_sse` | Return non-zero to reject an SSE upgrade. |
| `on_authenticate_websocket` | Return non-zero to reject a WebSocket upgrade. |
| `on_open` | Called once a WebSocket or SSE upgrade is complete. |
| `on_message` | Called with complete WebSocket messages. |
| `on_eventsource` | Called with complete SSE events. |
| `on_eventsource_reconnect` | Called for SSE requests with `Last-Event-ID`. |
| `on_ready` | Called when an upgraded connection's outgoing buffer is empty. |
| `on_shutdown` | Called for open upgraded connections during IO shutdown. |
| `on_close` | Called after an upgraded connection is closed. |
| `on_stop` | Called when HTTP service settings are being closed. |

Other settings:

| Field | Used for |
| --- | --- |
| `udata` | Default `fio_http_udata(h)` value. |
| `tls_io_func`, `tls` | Optional TLS support. |
| `queue` | Optional HTTP task queue. |
| `public_folder` | Static-file root; can serve pre-compressed `.gz` alternatives. |
| `max_age` | Static-file `Cache-Control` max-age value, in seconds. |
| `max_header_size` | Maximum combined request line and header bytes. |
| `max_line_len` | Maximum bytes per request / header line. |
| `max_body_size` | Maximum request body size. |
| `ws_max_msg_size` | Maximum buffered WebSocket message size. |
| `reserved1`, `reserved2` | Reserved for future use. |
| `timeout` | HTTP/1.x connection timeout, in seconds. |
| `ws_timeout` | WebSocket timeout; timeout pings are sent. |
| `sse_timeout` | SSE timeout; timeout pings are sent. |
| `connect_timeout` | Client connection timeout. |
| `log` | Enables HTTP request logging. |
| `compress_static` | Opt-in static-file compression. |
| `compress_dynamic` | Opt-in dynamic response compression. |
| `compress_ws` | Opt-in WebSocket `permessage-deflate`. |

Unset callbacks and limits are normalized when the listener, route, or client is
created. Server `on_http` defaults to a 404 response; client `on_http` defaults
to a no-op. Server upgrade authentication defaults to denial. Client upgrade
authentication defaults to allow.

### `fio_http_listen`

```c
fio_http_listener_s *fio_http_listen(const char *url,
                                     fio_http_settings_s settings);
#define fio_http_listen(url, ...) \
  fio_http_listen(url, (fio_http_settings_s){__VA_ARGS__})
```

Starts an HTTP / WebSocket / SSE listener on `url`.

```c
fio_http_listener_s *l = fio_http_listen("0.0.0.0:3000",
                                         .on_http = on_request,
                                         .log = 1);
```

The listener is rooted at `/`; a path component in the listen URL is ignored.
Use `fio_http_route` for per-prefix behavior.

### `fio_http_listener_settings`

```c
fio_http_settings_s *fio_http_listener_settings(fio_http_listener_s *l);
```

Returns the listener's root settings object. Prefer `fio_http_route` for
changing root routing behavior after the listener is created.

### `FIO_HTTP_AUTHENTICATE_ALLOW`

```c
int FIO_HTTP_AUTHENTICATE_ALLOW(fio_http_s *h);
```

Authentication callback that always allows the upgrade. Use it for
`.on_authenticate_sse` or `.on_authenticate_websocket` when no gate is needed.

### `fio_http_io` and `fio_http_settings`

```c
fio_io_s *fio_http_io(fio_http_s *h);
fio_http_settings_s *fio_http_settings(fio_http_s *h);
```

`fio_http_io` returns the IO object attached to the handle, when one exists.
`fio_http_settings` returns the route settings matching the handle's original
path, when connection data is available.

---

## Routing

### Prefix routes

```c
int fio_http_route(fio_http_listener_s *listener,
                   const char *url,
                   fio_http_settings_s settings);
#define fio_http_route(listener, url, ...) \
  fio_http_route(listener, url, (fio_http_settings_s){__VA_ARGS__})

fio_http_settings_s *fio_http_route_settings(fio_http_listener_s *l,
                                             const char *url);
```

Routes are best-prefix matches:

- `/` matches everything.
- `/user` matches `/user` and `/user/...`, but not `/user...`.
- More specific prefixes such as `/user/new` win over `/user`.

Route declaration order is not significant unless an existing route is replaced.
Route settings inherit missing listener callbacks and limits, including `udata`,
`on_finish`, `on_stop`, SSE / WebSocket authentication callbacks,
header/body/message limits, timeouts, `public_folder`, and `log`. TLS settings
are ignored on routes.

### Resource action helper

```c
typedef enum {
  FIO_HTTP_RESOURCE_NONE,
  FIO_HTTP_RESOURCE_INDEX,
  FIO_HTTP_RESOURCE_SHOW,
  FIO_HTTP_RESOURCE_NEW,
  FIO_HTTP_RESOURCE_EDIT,
  FIO_HTTP_RESOURCE_CREATE,
  FIO_HTTP_RESOURCE_UPDATE,
  FIO_HTTP_RESOURCE_DELETE,
  FIO_HTTP_RESOURCE_QUERY,
} fio_http_resource_action_e;

fio_http_resource_action_e fio_http_resource_action(fio_http_s *h);
```

Maps the current method and routed path to a small REST-style action:

| Action | Request shape |
| --- | --- |
| `FIO_HTTP_RESOURCE_INDEX` | `GET /` |
| `FIO_HTTP_RESOURCE_SHOW` | `GET /:id` |
| `FIO_HTTP_RESOURCE_NEW` | `GET /new` |
| `FIO_HTTP_RESOURCE_EDIT` | `GET /:id/edit` |
| `FIO_HTTP_RESOURCE_CREATE` | `PUT`, `POST`, or `PATCH /` |
| `FIO_HTTP_RESOURCE_UPDATE` | `PUT`, `POST`, or `PATCH /:id` |
| `FIO_HTTP_RESOURCE_DELETE` | `DELETE /:id` |

`FIO_HTTP_RESOURCE_NONE` is returned for unsupported shapes or invalid input.

---

## HTTP Client Connections

```c
fio_io_s *fio_http_connect(const char *url,
                           fio_http_s *h,
                           fio_http_settings_s settings);
#define fio_http_connect(url, h, ...) \
  fio_http_connect(url, h, (fio_http_settings_s){__VA_ARGS__})
```

Connects as an HTTP / WebSocket / SSE client. If `h` is `NULL`, a new handle is
created. Missing method, path, query, and `Host` are filled from `url`.

Recognized upgrade schemes:

- `ws://` and `wss://` call `fio_http_websocket_set_request`.
- `sse://` and `sses://` call `fio_http_sse_set_request`.

Client `on_http` receives the response handle. If the response accepts a
WebSocket or SSE upgrade, the connection switches to the upgraded protocol and
uses the upgraded callbacks instead.

---

## WebSocket and SSE Upgrades

### Upgrade tests and request / response setup

These helpers are declared by the HTTP handle and used by the HTTP module:

```c
int  fio_http_websocket_requested(fio_http_s *h);
int  fio_http_websocket_accepted(fio_http_s *h);
void fio_http_upgrade_websocket(fio_http_s *h);
void fio_http_websocket_set_request(fio_http_s *h);

int  fio_http_sse_requested(fio_http_s *h);
int  fio_http_sse_accepted(fio_http_s *h);
void fio_http_upgrade_sse(fio_http_s *h);
void fio_http_sse_set_request(fio_http_s *h);
```

`*_requested` inspects request headers. `*_accepted` inspects client-side
response state and marks the handle as upgraded on success. `fio_http_upgrade_*`
sets the server response state for the upgrade. `*_set_request` prepares a
client request handle.

Server listeners normally call the authentication callbacks and then upgrade
automatically when a request asks for WebSocket or SSE.

### WebSocket write and callback override

```c
int fio_http_websocket_write(fio_http_s *h,
                             const void *buf,
                             size_t len,
                             uint8_t is_text);

int fio_http_on_message_set(fio_http_s *h,
                            void (*on_message)(fio_http_s *,
                                               fio_buf_info_s,
                                               uint8_t));
```

`fio_http_websocket_write` writes one WebSocket message and fails if the handle
is not an established WebSocket. `is_text` selects text vs. binary.

`fio_http_on_message_set` overrides the `on_message` callback for the current
WebSocket connection. Passing `NULL` restores the settings callback. It returns
`-1` when the handle is not ready for this change.

Incoming WebSocket messages are accumulated up to `ws_max_msg_size` and then
scheduled on the HTTP queue. Control frames are handled by the module.

### SSE write

```c
typedef struct {
  fio_buf_info_s id;
  fio_buf_info_s event;
  fio_buf_info_s data;
} fio_http_sse_write_args_s;

int fio_http_sse_write(fio_http_s *h, fio_http_sse_write_args_s args);
#define fio_http_sse_write(h, ...) \
  fio_http_sse_write((h), ((fio_http_sse_write_args_s){__VA_ARGS__}))
```

Writes one UTF-8 SSE event. `data` is required. `id` and `event` are optional.
Multiline data is emitted as repeated `data:` lines. The helper fails if the
handle is not an established SSE connection.

### Pub/sub helpers

```c
#define fio_http_subscribe(h, ...) \
  fio_pubsub_subscribe(.io = fio_http_io(h), __VA_ARGS__)

void FIO_HTTP_WEBSOCKET_SUBSCRIBE_DIRECT(fio_pubsub_msg_s *msg);
void FIO_HTTP_WEBSOCKET_SUBSCRIBE_DIRECT_TEXT(fio_pubsub_msg_s *msg);
void FIO_HTTP_WEBSOCKET_SUBSCRIBE_DIRECT_BINARY(fio_pubsub_msg_s *msg);
void FIO_HTTP_SSE_SUBSCRIBE_DIRECT(fio_pubsub_msg_s *msg);
```

`fio_http_subscribe` subscribes using the handle's IO object.

The direct WebSocket callbacks write published messages to the WebSocket. The
plain `FIO_HTTP_WEBSOCKET_SUBSCRIBE_DIRECT` helper checks short payloads for
UTF-8 and selects text or binary; the explicit `_TEXT` and `_BINARY` variants
skip that guess.

`FIO_HTTP_SSE_SUBSCRIBE_DIRECT` writes an SSE event whose event name is the
channel and whose data is the published message.

---

## HTTP Handle API Pulled In by `FIO_HTTP`

The HTTP handle can also be included directly with `FIO_HTTP_HANDLE`. The full
HTTP module uses it as the request / response object exposed to callbacks.

### Lifetime

```c
typedef struct fio_http_s fio_http_s;

fio_http_s *fio_http_new(void);
fio_http_s *fio_http_new_copy_request(fio_http_s *old);
void        fio_http_free(fio_http_s *h);
fio_http_s *fio_http_dup(fio_http_s *h);
fio_http_s *fio_http_destroy(fio_http_s *h);
void        fio_http_start_time_set(fio_http_s *h);
fio_http_s *fio_http_clear_response(fio_http_s *h, bool clear_body);
void        fio_http_close(fio_http_s *h);
```

`fio_http_s` is reference-counted. It is not designed as a thread-safe mutable
object. `fio_http_close` closes the persistent connection associated with an
upgraded handle.

### User data and controller data

```c
void *fio_http_udata(fio_http_s *h);
void *fio_http_udata_set(fio_http_s *h, void *udata);
void *fio_http_udata2(fio_http_s *h);
void *fio_http_udata2_set(fio_http_s *h, void *udata);
void *fio_http_cdata(fio_http_s *h);
void *fio_http_cdata_set(fio_http_s *h, void *cdata);

fio_http_controller_s *fio_http_controller(fio_http_s *h);
fio_http_controller_s *fio_http_controller_set(fio_http_s *h,
                                               fio_http_controller_s *c);
```

`udata` and `udata2` are for application state. `cdata` and the controller are
used by the HTTP layer and custom controllers.

### Request / response properties

```c
size_t         fio_http_status(fio_http_s *h);
size_t         fio_http_status_set(fio_http_s *h, size_t status);
fio_str_info_s fio_http_method(fio_http_s *h);
fio_str_info_s fio_http_method_set(fio_http_s *h, fio_str_info_s value);
fio_str_info_s fio_http_opath(fio_http_s *h);
fio_str_info_s fio_http_opath_set(fio_http_s *h, fio_str_info_s value);
fio_str_info_s fio_http_path(fio_http_s *h);
fio_str_info_s fio_http_path_set(fio_http_s *h, fio_str_info_s value);
fio_str_info_s fio_http_query(fio_http_s *h);
fio_str_info_s fio_http_query_set(fio_http_s *h, fio_str_info_s value);
fio_str_info_s fio_http_version(fio_http_s *h);
fio_str_info_s fio_http_version_set(fio_http_s *h, fio_str_info_s value);
int64_t        fio_http_received_at(fio_http_s *h);
int64_t        fio_http_received_at_set(fio_http_s *h, int64_t value);
```

`opath` is the original path before route prefix trimming. `path` is the routed
path visible to the current callback.

`fio_http_status_set(h, 0)` normalizes to `200`; values above the internal range
normalize to `500`.

### State checks and flags

```c
int fio_http_is_clean(fio_http_s *h);
int fio_http_is_finished(fio_http_s *h);
int fio_http_is_streaming(fio_http_s *h);
int fio_http_is_upgraded(fio_http_s *h);
int fio_http_is_websocket(fio_http_s *h);
int fio_http_is_sse(fio_http_s *h);
int fio_http_is_freeing(fio_http_s *h);
```

The handle also exposes inline flag helpers generated for `cflags` and
`uflags`:

```c
uint16_t fio_http_cflags(fio_http_s *h);
uint16_t fio_http_cflags_set(fio_http_s *h, uint16_t flags);
uint16_t fio_http_cflags_unset(fio_http_s *h, uint16_t flags);
uint16_t fio_http_cflags_flip(fio_http_s *h, uint16_t flags);
uint16_t fio_http_cflags_is_set(fio_http_s *h, uint16_t flags);

uint16_t fio_http_uflags(fio_http_s *h);
uint16_t fio_http_uflags_set(fio_http_s *h, uint16_t flags);
uint16_t fio_http_uflags_unset(fio_http_s *h, uint16_t flags);
uint16_t fio_http_uflags_flip(fio_http_s *h, uint16_t flags);
uint16_t fio_http_uflags_is_set(fio_http_s *h, uint16_t flags);
```

Prefer the state helpers unless implementing protocol or controller glue.

### Headers

```c
fio_str_info_s fio_http_request_header(fio_http_s *h,
                                       fio_str_info_s name,
                                       size_t index);
size_t fio_http_request_header_count(fio_http_s *h, fio_str_info_s name);
fio_str_info_s fio_http_request_header_set(fio_http_s *h,
                                           fio_str_info_s name,
                                           fio_str_info_s value);
fio_str_info_s fio_http_request_header_set_if_missing(fio_http_s *h,
                                                      fio_str_info_s name,
                                                      fio_str_info_s value);
fio_str_info_s fio_http_request_header_add(fio_http_s *h,
                                           fio_str_info_s name,
                                           fio_str_info_s value);
size_t fio_http_request_header_each(fio_http_s *h,
                                    int (*callback)(fio_http_s *,
                                                    fio_str_info_s name,
                                                    fio_str_info_s value,
                                                    void *udata),
                                    void *udata);
```

Response headers use the same shape with `response` in the function name:

```c
fio_http_response_header;
fio_http_response_header_count;
fio_http_response_header_set;
fio_http_response_header_set_if_missing;
fio_http_response_header_add;
fio_http_response_header_each;
```

`*_header` returns an empty value if the requested value is missing. Use
`index` for repeated values. If `name.buf == NULL`, `*_header_count` returns
the number of unique header names.

Response headers cannot be changed after the response headers are sent.

### Header parsing helpers

```c
int fio_http_request_header_parse(fio_http_s *h,
                                  fio_str_info_s *buf_parsed,
                                  fio_str_info_s header_name);
int fio_http_response_header_parse(fio_http_s *h,
                                   fio_str_info_s *buf_parsed,
                                   fio_str_info_s header_name);

#define FIO_HTTP_HEADER_EACH_VALUE(http_handle, is_request, header_name, value)
#define FIO_HTTP_HEADER_VALUE_EACH_PROPERTY(value, property)
#define FIO_HTTP_PARSED_HEADER_EACH(buf_parsed, value)
```

The parse helpers copy repeated comma-separated header values into a compact
parsed buffer. The macros iterate values and `name=value` style properties. The
macro form uses a 2048-byte stack buffer for the parse.

### Body storage and reads

```c
size_t         fio_http_body_length(fio_http_s *h);
size_t         fio_http_body_pos(fio_http_s *h);
size_t         fio_http_body_seek(fio_http_s *h, ssize_t pos);
fio_str_info_s fio_http_body_read(fio_http_s *h, size_t length);
fio_str_info_s fio_http_body_read_until(fio_http_s *h,
                                        char token,
                                        size_t limit);
void           fio_http_body_expect(fio_http_s *h, size_t expected_length);
void           fio_http_body_write(fio_http_s *h, const void *data, size_t len);
int            fio_http_body_fd(fio_http_s *h);
```

The handle stores body data in RAM until the configured threshold is crossed,
then uses a temporary file. `fio_http_body_fd` returns that file descriptor or
`-1`.

### Body parsing

```c
typedef struct fio_http_body_parse_callbacks_s fio_http_body_parse_callbacks_s;
typedef struct fio_http_body_parse_result_s fio_http_body_parse_result_s;

fio_http_body_parse_result_s fio_http_body_parse(
    fio_http_s *h,
    const fio_http_body_parse_callbacks_s *callbacks,
    void *udata);
```

The body parser auto-detects JSON, URL-encoded, and multipart/form-data input.
Callbacks build application objects for primitives, arrays, maps, and file
uploads.

`fio_http_body_parse_callbacks_s` contains primitive callbacks (`on_null`,
`on_true`, `on_false`, `on_number`, `on_float`, `on_string`), container
callbacks (`on_array`, `on_map`, `array_push`, `map_set`, `array_done`,
`map_done`), multipart callbacks (`on_file`, `on_file_data`, `on_file_done`),
and error / cleanup callbacks (`on_error`, `free_unused`).

`fio_http_body_parse_result_s` reports the top-level result, consumed bytes,
and error code.

### Cookies

```c
typedef enum fio_http_cookie_same_site_e {
  FIO_HTTP_COOKIE_SAME_SITE_BROWSER_DEFAULT = 0,
  FIO_HTTP_COOKIE_SAME_SITE_NONE,
  FIO_HTTP_COOKIE_SAME_SITE_LAX,
  FIO_HTTP_COOKIE_SAME_SITE_STRICT,
} fio_http_cookie_same_site_e;

typedef struct fio_http_cookie_args_s fio_http_cookie_args_s;

int fio_http_cookie_set(fio_http_s *h, fio_http_cookie_args_s args);
#define fio_http_cookie_set(h, ...) \
  fio_http_cookie_set((h), (fio_http_cookie_args_s){__VA_ARGS__})

fio_str_info_s fio_http_cookie(fio_http_s *h,
                               const char *name,
                               size_t name_len);
size_t fio_http_cookie_each(fio_http_s *h,
                            int (*callback)(fio_http_s *,
                                            fio_str_info_s name,
                                            fio_str_info_s value,
                                            void *udata),
                            void *udata);
size_t fio_http_set_cookie_each(fio_http_s *h,
                                int (*callback)(fio_http_s *,
                                                fio_str_info_s set_cookie,
                                                fio_str_info_s value,
                                                void *udata),
                                void *udata);
```

`fio_http_cookie_args_s` contains `name`, `value`, optional `domain` and `path`,
`max_age`, `same_site`, and the `secure`, `http_only`, and `partitioned` flags.
An empty / missing value deletes the cookie by setting a negative max-age.

### Writing responses

```c
typedef struct fio_http_write_args_s {
  const void *buf;
  size_t len;
  size_t offset;
  int fd;
  void (*dealloc)(void *);
  int copy;
  int finish;
} fio_http_write_args_s;

void fio_http_write(fio_http_s *h, fio_http_write_args_s args);
#define fio_http_write(h, ...) fio_http_write(h, (fio_http_write_args_s){__VA_ARGS__})
#define fio_http_finish(h)    fio_http_write(h, .finish = 1)
```

`fio_http_write` sends headers on the first write and then writes body data. If
`finish` is set, the response is complete. Without `finish`, the response is a
stream. File writes use `fd` and `offset`; the file descriptor is always closed
by the write path.

On upgraded handles, `fio_http_write` routes through the WebSocket or SSE
controller. For WebSocket text/binary choice, call `fio_http_websocket_write`
directly when the payload type matters.

### General helpers

```c
int            fio_http_send_error_response(fio_http_s *h, size_t status);
int            fio_http_etag_is_match(fio_http_s *h);
int            fio_http_static_file_response(fio_http_s *h,
                                             fio_str_info_s root_folder,
                                             fio_str_info_s file_name,
                                             size_t max_age);
fio_str_info_s fio_http_status2str(size_t status);
void           fio_http_write_log(fio_http_s *h);
int            fio_http_from(fio_str_info_s *dest, const fio_http_s *h);
fio_str_info_s fio_http_date(uint64_t now_in_seconds);
fio_str_info_s fio_http_log_time(uint64_t now_in_seconds);
int64_t        fio_http_get_timestump(void);
```

`fio_http_get_timestump` is the inline timestamp helper used by the handle for
`received_at` and log timing. User code usually reads timestamps with
`fio_http_received_at` or formats them with `fio_http_date` /
`fio_http_log_time`.

`fio_http_from` writes a best-effort peer address starting with the `Forwarded`
header, then socket peer address, then `"[unknown]"`.

`fio_http_static_file_response` sends a file from a root folder and finishes the
response on success.

### Path and MIME helpers

```c
#define FIO_HTTP_PATH_EACH(path, pos)

int fio_http_mimetype_register(char *file_ext,
                               size_t file_ext_len,
                               fio_str_info_s mime_type);
fio_str_info_s fio_http_mimetype(char *file_ext, size_t file_ext_len);
```

`FIO_HTTP_PATH_EACH` iterates decoded path sections using a 4096-byte stack
buffer. The MIME registry is not thread-safe.

### Controller

```c
typedef struct fio_http_controller_s fio_http_controller_s;

struct fio_http_controller_s {
  uintptr_t private_flags;
  void (*on_destroyed)(fio_http_s *h);
  void (*send_headers)(fio_http_s *h);
  void (*write_body)(fio_http_s *h, fio_http_write_args_s args);
  void (*on_finish)(fio_http_s *h);
  void (*close_io)(fio_http_s *h);
  int  (*get_fd)(fio_http_s *h);
};
```

Controllers allow the handle to write through different protocols. Initialize
custom controllers to zero before use. If controller callbacks are not
thread-safe, call `fio_http_write` only from the controller's expected thread.

---

## Compile-Time Knobs

HTTP module defaults:

```c
FIO_HTTP_DEFAULT_MAX_HEADER_SIZE      /* 32768 */
FIO_HTTP_DEFAULT_MAX_LINE_LEN         /* 8192 */
FIO_HTTP_DEFAULT_MAX_BODY_SIZE        /* 33554432 */
FIO_HTTP_DEFAULT_WS_MAX_MSG_SIZE      /* 262144 */
FIO_HTTP_DEFAULT_TIMEOUT              /* 50 */
FIO_HTTP_DEFAULT_TIMEOUT_LONG         /* 50 */
FIO_HTTP_SHOW_CONTENT_LENGTH_HEADER   /* 0 */
FIO_HTTP_WEBSOCKET_WRITE_VALIDITY_TEST_LIMIT
FIO_WEBSOCKET_STATS                   /* 0 */
FIO_HTTP_WEBSOCKET_DEFLATE_MIN        /* 1024 */
```

HTTP handle defaults:

```c
FIO_HTTP_EXACT_LOGGING                /* fuzzy timestamps by default with IO */
FIO_HTTP_BODY_RAM_LIMIT               /* 1 << 17 */
FIO_HTTP_CACHE_LIMIT                  /* 0 */
FIO_HTTP_CACHE_STR_MAX_LEN            /* 1 << 12 */
FIO_HTTP_CACHE_USES_MUTEX             /* 1 */
FIO_HTTP_PRE_CACHE_KNOWN_HEADERS      /* 1 */
FIO_HTTP_DEFAULT_INDEX_FILENAME       /* "index" */
FIO_HTTP_STATIC_FILE_COMPLETION       /* 1 */
FIO_HTTP_STATIC_FILE_COMPRESS_LIMIT   /* 1 << 21 */
FIO_HTTP_LOG_X_REQUEST_START          /* 1 */
FIO_HTTP_ENFORCE_LOWERCASE_HEADERS    /* 0 */
```

State and controller flag macros are exposed for protocol glue:

```c
FIO_HTTP_STATE_STREAMING
FIO_HTTP_STATE_FINISHED
FIO_HTTP_STATE_UPGRADED
FIO_HTTP_STATE_WEBSOCKET
FIO_HTTP_STATE_SSE
FIO_HTTP_STATE_COOKIES_PARSED
FIO_HTTP_STATE_FREEING

FIO_HTTP_CFLAG_COMPRESS_DYNAMIC
FIO_HTTP_CFLAG_COMPRESS_WS
FIO_HTTP_CFLAG_COMPRESS_STATIC
```

Prefer public helper functions over reading state bits directly in application
code.

---

## Minimal Server

```c
#define FIO_HTTP
#include FIO_INCLUDE_FILE

static void on_http(fio_http_s *h) {
  fio_http_response_header_set(h,
                               FIO_STR_INFO1("content-type"),
                               FIO_STR_INFO1("text/plain"));
  fio_http_write(h, .buf = "hello\n", .len = 6, .finish = 1);
}

void start_http(void) {
  fio_http_listen("0.0.0.0:3000", .on_http = on_http, .log = 1);
}
```

For WebSocket or SSE routes, provide the relevant authentication and upgraded
connection callbacks in the route settings.
## Hash Function Testing

During development I tested the Hash functions using [the SMHasher testing suite (@rurban's fork)](https://github.com/rurban/smhasher). The testing suite is often growing with more tests, so I do not know what the future may bring... but attached are the test results for Risky Hash, Stable Hash (64 and 128 bit variation) as they were at the time of (re)development (March, 2023).
### Risky Hash SMHasher Results

The following results were achieved on my personal computer when testing the facil.io Risky Hash (`fio_risky_hash`).

```txt
------------------------------------------------------------
--- Testing Risky "facil.io Risky Hash" GOOD

[[[ Sanity Tests ]]]

Verification value 0x407D2C05 ....... PASS
Running sanity check 1     .......... PASS
Running AppendedZeroesTest .......... PASS

[[[ Speed Tests ]]]

Bulk speed test - 262144-byte keys
Alignment  7 - 17.861 bytes/cycle - 51100.77 MiB/sec @ 3 ghz
Alignment  6 - 17.833 bytes/cycle - 51019.28 MiB/sec @ 3 ghz
Alignment  5 - 17.832 bytes/cycle - 51018.87 MiB/sec @ 3 ghz
Alignment  4 - 17.853 bytes/cycle - 51077.60 MiB/sec @ 3 ghz
Alignment  3 - 17.844 bytes/cycle - 51051.63 MiB/sec @ 3 ghz
Alignment  2 - 17.854 bytes/cycle - 51080.37 MiB/sec @ 3 ghz
Alignment  1 - 17.912 bytes/cycle - 51245.99 MiB/sec @ 3 ghz
Alignment  0 - 19.064 bytes/cycle - 54541.55 MiB/sec @ 3 ghz
Average      - 18.006 bytes/cycle - 51517.01 MiB/sec @ 3 ghz

Small key speed test -    1-byte keys -    13.90 cycles/hash
Small key speed test -    2-byte keys -    14.59 cycles/hash
Small key speed test -    3-byte keys -    14.27 cycles/hash
Small key speed test -    4-byte keys -    14.32 cycles/hash
Small key speed test -    5-byte keys -    14.81 cycles/hash
Small key speed test -    6-byte keys -    14.99 cycles/hash
Small key speed test -    7-byte keys -    15.00 cycles/hash
Small key speed test -    8-byte keys -    14.00 cycles/hash
Small key speed test -    9-byte keys -    14.15 cycles/hash
Small key speed test -   10-byte keys -    14.00 cycles/hash
Small key speed test -   11-byte keys -    14.00 cycles/hash
Small key speed test -   12-byte keys -    14.00 cycles/hash
Small key speed test -   13-byte keys -    14.00 cycles/hash
Small key speed test -   14-byte keys -    14.11 cycles/hash
Small key speed test -   15-byte keys -    14.00 cycles/hash
Small key speed test -   16-byte keys -    14.72 cycles/hash
Small key speed test -   17-byte keys -    15.74 cycles/hash
Small key speed test -   18-byte keys -    15.62 cycles/hash
Small key speed test -   19-byte keys -    15.60 cycles/hash
Small key speed test -   20-byte keys -    15.66 cycles/hash
Small key speed test -   21-byte keys -    15.83 cycles/hash
Small key speed test -   22-byte keys -    15.69 cycles/hash
Small key speed test -   23-byte keys -    15.73 cycles/hash
Small key speed test -   24-byte keys -    15.63 cycles/hash
Small key speed test -   25-byte keys -    15.67 cycles/hash
Small key speed test -   26-byte keys -    15.66 cycles/hash
Small key speed test -   27-byte keys -    15.76 cycles/hash
Small key speed test -   28-byte keys -    15.67 cycles/hash
Small key speed test -   29-byte keys -    15.68 cycles/hash
Small key speed test -   30-byte keys -    15.65 cycles/hash
Small key speed test -   31-byte keys -    15.64 cycles/hash
Average                                    14.971 cycles/hash

[[[ 'Hashmap' Speed Tests ]]]

std::unordered_map
Init std HashMapTest:     148.866 cycles/op (466569 inserts, 1% deletions)
Running std HashMapTest:  85.492 cycles/op (1.0 stdv, found 461903)

greg7mdp/parallel-hashmap
Init fast HashMapTest:    158.746 cycles/op (466569 inserts, 1% deletions)
Running fast HashMapTest: 74.728 cycles/op (2.5 stdv, found 461903)

facil.io HashMap
Init fast fio_map_s Test:    62.909 cycles/op (466569 inserts, 1% deletions)
Running fast fio_map_s Test: 32.332 cycles/op (0.2 stdv, found 461903)
 ....... PASS

[[[ Avalanche Tests ]]]

Testing   24-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.585333%
Testing   32-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.695333%
Testing   40-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.619333%
Testing   48-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.662667%
Testing   56-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.706000%
Testing   64-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.693333%
Testing   72-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.730667%
Testing   80-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.706667%
Testing   96-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.672000%
Testing  112-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.786667%
Testing  128-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.621333%
Testing  160-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.710000%
Testing  512-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.835333%
Testing 1024-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.800000%

[[[ Keyset 'Sparse' Tests ]]]

Keyset 'Sparse' - 16-bit keys with up to 9 bits set - 50643 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          0.3, actual      0 (0.00x)
Testing collisions (high 19-25 bits) - Worst is 19 bits: 2358/2368 (1.00x)
Testing collisions (low  32-bit) - Expected          0.3, actual      0 (0.00x)
Testing collisions (low  19-25 bits) - Worst is 24 bits: 90/76 (1.18x)
Testing distribution - Worst bias is the 13-bit window at bit 41 - 0.600%

Keyset 'Sparse' - 24-bit keys with up to 8 bits set - 1271626 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        188.2, actual    179 (0.95x)
Testing collisions (high 24-35 bits) - Worst is 25 bits: 23889/23794 (1.00x)
Testing collisions (low  32-bit) - Expected        188.2, actual    215 (1.14x) (27)
Testing collisions (low  24-35 bits) - Worst is 33 bits: 108/94 (1.15x)
Testing distribution - Worst bias is the 17-bit window at bit 16 - 0.075%

Keyset 'Sparse' - 32-bit keys with up to 7 bits set - 4514873 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2372.2, actual   2390 (1.01x) (18)
Testing collisions (high 25-38 bits) - Worst is 34 bits: 602/593 (1.01x)
Testing collisions (low  32-bit) - Expected       2372.2, actual   2313 (0.98x)
Testing collisions (low  25-38 bits) - Worst is 35 bits: 301/296 (1.01x)
Testing distribution - Worst bias is the 19-bit window at bit 15 - 0.051%

Keyset 'Sparse' - 40-bit keys with up to 6 bits set - 4598479 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2460.8, actual   2509 (1.02x) (49)
Testing collisions (high 25-38 bits) - Worst is 38 bits: 42/38 (1.09x)
Testing collisions (low  32-bit) - Expected       2460.8, actual   2449 (1.00x) (-11)
Testing collisions (low  25-38 bits) - Worst is 35 bits: 316/307 (1.03x)
Testing distribution - Worst bias is the 19-bit window at bit 12 - 0.058%

Keyset 'Sparse' - 48-bit keys with up to 6 bits set - 14196869 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      23437.8, actual  23532 (1.00x) (95)
Testing collisions (high 27-42 bits) - Worst is 42 bits: 28/22 (1.22x)
Testing collisions (low  32-bit) - Expected      23437.8, actual  23333 (1.00x) (-104)
Testing collisions (low  27-42 bits) - Worst is 40 bits: 96/91 (1.05x)
Testing distribution - Worst bias is the 20-bit window at bit 16 - 0.020%

Keyset 'Sparse' - 56-bit keys with up to 5 bits set - 4216423 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2069.0, actual   2026 (0.98x)
Testing collisions (high 25-38 bits) - Worst is 34 bits: 518/517 (1.00x)
Testing collisions (low  32-bit) - Expected       2069.0, actual   2139 (1.03x) (71)
Testing collisions (low  25-38 bits) - Worst is 36 bits: 142/129 (1.10x)
Testing distribution - Worst bias is the 19-bit window at bit  8 - 0.043%

Keyset 'Sparse' - 64-bit keys with up to 5 bits set - 8303633 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8021.7, actual   8072 (1.01x) (51)
Testing collisions (high 26-40 bits) - Worst is 40 bits: 37/31 (1.18x)
Testing collisions (low  32-bit) - Expected       8021.7, actual   8035 (1.00x) (14)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 32/31 (1.02x)
Testing distribution - Worst bias is the 20-bit window at bit 37 - 0.039%

Keyset 'Sparse' - 72-bit keys with up to 5 bits set - 15082603 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      26451.8, actual  26268 (0.99x) (-183)
Testing collisions (high 27-42 bits) - Worst is 39 bits: 225/206 (1.09x)
Testing collisions (low  32-bit) - Expected      26451.8, actual  26552 (1.00x) (101)
Testing collisions (low  27-42 bits) - Worst is 35 bits: 3381/3309 (1.02x)
Testing distribution - Worst bias is the 20-bit window at bit 36 - 0.027%

Keyset 'Sparse' - 96-bit keys with up to 4 bits set - 3469497 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1401.0, actual   1392 (0.99x) (-8)
Testing collisions (high 25-38 bits) - Worst is 36 bits: 93/87 (1.06x)
Testing collisions (low  32-bit) - Expected       1401.0, actual   1402 (1.00x) (2)
Testing collisions (low  25-38 bits) - Worst is 37 bits: 48/43 (1.10x)
Testing distribution - Worst bias is the 19-bit window at bit 11 - 0.051%

Keyset 'Sparse' - 160-bit keys with up to 4 bits set - 26977161 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      84546.1, actual  84403 (1.00x) (-143)
Testing collisions (high 28-44 bits) - Worst is 34 bits: 21303/21169 (1.01x)
Testing collisions (low  32-bit) - Expected      84546.1, actual  84679 (1.00x) (133)
Testing collisions (low  28-44 bits) - Worst is 43 bits: 45/41 (1.09x)
Testing distribution - Worst bias is the 20-bit window at bit 49 - 0.014%

Keyset 'Sparse' - 256-bit keys with up to 3 bits set - 2796417 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        910.2, actual    930 (1.02x) (20)
Testing collisions (high 25-37 bits) - Worst is 36 bits: 71/56 (1.25x)
Testing collisions (low  32-bit) - Expected        910.2, actual    922 (1.01x) (12)
Testing collisions (low  25-37 bits) - Worst is 35 bits: 121/113 (1.06x)
Testing distribution - Worst bias is the 19-bit window at bit 52 - 0.094%

Keyset 'Sparse' - 512-bit keys with up to 3 bits set - 22370049 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      58155.4, actual  58001 (1.00x) (-154)
Testing collisions (high 28-43 bits) - Worst is 42 bits: 69/56 (1.21x)
Testing collisions (low  32-bit) - Expected      58155.4, actual  58151 (1.00x) (-4)
Testing collisions (low  28-43 bits) - Worst is 41 bits: 121/113 (1.06x)
Testing distribution - Worst bias is the 20-bit window at bit  3 - 0.015%

Keyset 'Sparse' - 1024-bit keys with up to 2 bits set - 524801 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected         32.1, actual     37 (1.15x) (5)
Testing collisions (high 22-32 bits) - Worst is 31 bits: 75/64 (1.17x)
Testing collisions (low  32-bit) - Expected         32.1, actual     40 (1.25x) (8)
Testing collisions (low  22-32 bits) - Worst is 32 bits: 40/32 (1.25x)
Testing distribution - Worst bias is the 16-bit window at bit 17 - 0.136%

Keyset 'Sparse' - 2048-bit keys with up to 2 bits set - 2098177 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        512.4, actual    551 (1.08x) (39)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1105/1024 (1.08x)
Testing collisions (low  32-bit) - Expected        512.4, actual    526 (1.03x) (14)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 76/64 (1.19x)
Testing distribution - Worst bias is the 18-bit window at bit 52 - 0.103%


[[[ Keyset 'Permutation' Tests ]]]

Combination Lowbits Tests:
Keyset 'Combination' - up to 7 blocks from a set of 8 - 2396744 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        668.6, actual    670 (1.00x) (2)
Testing collisions (high 24-37 bits) - Worst is 37 bits: 28/20 (1.34x)
Testing collisions (low  32-bit) - Expected        668.6, actual    675 (1.01x) (7)
Testing collisions (low  24-37 bits) - Worst is 33 bits: 350/334 (1.05x)
Testing distribution - Worst bias is the 18-bit window at bit  1 - 0.086%


Combination Highbits Tests
Keyset 'Combination' - up to 7 blocks from a set of 8 - 2396744 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        668.6, actual    620 (0.93x)
Testing collisions (high 24-37 bits) - Worst is 36 bits: 45/41 (1.08x)
Testing collisions (low  32-bit) - Expected        668.6, actual    634 (0.95x)
Testing collisions (low  24-37 bits) - Worst is 28 bits: 10778/10667 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 35 - 0.053%


Combination Hi-Lo Tests:
Keyset 'Combination' - up to 6 blocks from a set of 15 - 12204240 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      17322.9, actual  17547 (1.01x) (225)
Testing collisions (high 27-41 bits) - Worst is 38 bits: 283/270 (1.04x)
Testing collisions (low  32-bit) - Expected      17322.9, actual  17096 (0.99x) (-226)
Testing collisions (low  27-41 bits) - Worst is 41 bits: 37/33 (1.09x)
Testing distribution - Worst bias is the 20-bit window at bit 46 - 0.035%


Combination 0x8000000 Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8170 (1.00x) (-16)
Testing collisions (high 26-40 bits) - Worst is 33 bits: 4129/4094 (1.01x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8208 (1.00x) (22)
Testing collisions (low  26-40 bits) - Worst is 33 bits: 4128/4094 (1.01x)
Testing distribution - Worst bias is the 20-bit window at bit 21 - 0.041%


Combination 0x0000001 Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8179 (1.00x) (-7)
Testing collisions (high 26-40 bits) - Worst is 31 bits: 16584/16362 (1.01x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8243 (1.01x) (57)
Testing collisions (low  26-40 bits) - Worst is 37 bits: 270/255 (1.05x)
Testing distribution - Worst bias is the 20-bit window at bit  5 - 0.042%


Combination 0x800000000000000 Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8296 (1.01x) (110)
Testing collisions (high 26-40 bits) - Worst is 39 bits: 74/63 (1.16x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8168 (1.00x) (-18)
Testing collisions (low  26-40 bits) - Worst is 38 bits: 144/127 (1.13x)
Testing distribution - Worst bias is the 20-bit window at bit 55 - 0.035%


Combination 0x000000000000001 Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8186 (1.00x)
Testing collisions (high 26-40 bits) - Worst is 39 bits: 72/63 (1.13x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8238 (1.01x) (52)
Testing collisions (low  26-40 bits) - Worst is 35 bits: 1063/1023 (1.04x)
Testing distribution - Worst bias is the 20-bit window at bit 63 - 0.031%


Combination 16-bytes [0-1] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8212 (1.00x) (26)
Testing collisions (high 26-40 bits) - Worst is 34 bits: 2056/2047 (1.00x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8083 (0.99x) (-103)
Testing collisions (low  26-40 bits) - Worst is 37 bits: 259/255 (1.01x)
Testing distribution - Worst bias is the 20-bit window at bit 46 - 0.037%


Combination 16-bytes [0-last] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8052 (0.98x) (-134)
Testing collisions (high 26-40 bits) - Worst is 39 bits: 66/63 (1.03x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8267 (1.01x) (81)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 39/31 (1.22x)
Testing distribution - Worst bias is the 20-bit window at bit  2 - 0.045%


Combination 32-bytes [0-1] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8136 (0.99x) (-50)
Testing collisions (high 26-40 bits) - Worst is 38 bits: 130/127 (1.02x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8138 (0.99x) (-48)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 20-bit window at bit 58 - 0.044%


Combination 32-bytes [0-last] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   7961 (0.97x)
Testing collisions (high 26-40 bits) - Worst is 40 bits: 40/31 (1.25x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8088 (0.99x) (-98)
Testing collisions (low  26-40 bits) - Worst is 35 bits: 1038/1023 (1.01x)
Testing distribution - Worst bias is the 20-bit window at bit 24 - 0.047%


Combination 64-bytes [0-1] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8054 (0.98x) (-132)
Testing collisions (high 26-40 bits) - Worst is 26 bits: 502359/503108 (1.00x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8309 (1.01x) (123)
Testing collisions (low  26-40 bits) - Worst is 34 bits: 2101/2047 (1.03x)
Testing distribution - Worst bias is the 20-bit window at bit 14 - 0.052%


Combination 64-bytes [0-last] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8247 (1.01x) (61)
Testing collisions (high 26-40 bits) - Worst is 33 bits: 4159/4094 (1.02x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8217 (1.00x) (31)
Testing collisions (low  26-40 bits) - Worst is 39 bits: 71/63 (1.11x)
Testing distribution - Worst bias is the 20-bit window at bit 31 - 0.052%


Combination 128-bytes [0-1] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8076 (0.99x) (-110)
Testing collisions (high 26-40 bits) - Worst is 38 bits: 150/127 (1.17x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8278 (1.01x) (92)
Testing collisions (low  26-40 bits) - Worst is 35 bits: 1069/1023 (1.04x)
Testing distribution - Worst bias is the 20-bit window at bit 36 - 0.041%


Combination 128-bytes [0-last] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8108 (0.99x) (-78)
Testing collisions (high 26-40 bits) - Worst is 40 bits: 39/31 (1.22x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8100 (0.99x) (-86)
Testing collisions (low  26-40 bits) - Worst is 37 bits: 295/255 (1.15x)
Testing distribution - Worst bias is the 20-bit window at bit 55 - 0.049%


[[[ Keyset 'Window' Tests ]]]

Keyset 'Window' -  32-bit key,  25-bit window - 32 tests, 33554432 keys per test
Window at   0 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   1 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   2 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   3 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   4 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   5 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   6 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   7 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   8 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   9 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  10 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  11 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  12 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  13 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  14 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  15 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  16 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  17 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  18 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  19 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  20 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  21 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  22 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  23 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  24 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  25 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  26 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  27 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  28 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  29 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  30 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  31 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  32 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)

[[[ Keyset 'Cyclic' Tests ]]]

Keyset 'Cyclic' - 8 cycles of 8 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    116 (1.00x)
Testing collisions (high 23-34 bits) - Worst is 30 bits: 499/465 (1.07x)
Testing collisions (low  32-bit) - Expected        116.4, actual    102 (0.88x)
Testing collisions (low  23-34 bits) - Worst is 25 bits: 14836/14754 (1.01x)
Testing distribution - Worst bias is the 17-bit window at bit 24 - 0.118%

Keyset 'Cyclic' - 8 cycles of 9 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual     96 (0.82x)
Testing collisions (high 23-34 bits) - Worst is 26 bits: 7398/7413 (1.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    116 (1.00x)
Testing collisions (low  23-34 bits) - Worst is 25 bits: 14917/14754 (1.01x)
Testing distribution - Worst bias is the 17-bit window at bit 46 - 0.104%

Keyset 'Cyclic' - 8 cycles of 10 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    102 (0.88x)
Testing collisions (high 23-34 bits) - Worst is 29 bits: 934/930 (1.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    115 (0.99x) (-1)
Testing collisions (low  23-34 bits) - Worst is 34 bits: 35/29 (1.20x)
Testing distribution - Worst bias is the 17-bit window at bit 39 - 0.070%

Keyset 'Cyclic' - 8 cycles of 11 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    112 (0.96x)
Testing collisions (high 23-34 bits) - Worst is 27 bits: 3790/3716 (1.02x)
Testing collisions (low  32-bit) - Expected        116.4, actual    140 (1.20x) (24)
Testing collisions (low  23-34 bits) - Worst is 34 bits: 36/29 (1.24x)
Testing distribution - Worst bias is the 17-bit window at bit 39 - 0.112%

Keyset 'Cyclic' - 8 cycles of 12 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    115 (0.99x) (-1)
Testing collisions (high 23-34 bits) - Worst is 34 bits: 34/29 (1.17x)
Testing collisions (low  32-bit) - Expected        116.4, actual    115 (0.99x) (-1)
Testing collisions (low  23-34 bits) - Worst is 30 bits: 483/465 (1.04x)
Testing distribution - Worst bias is the 17-bit window at bit 52 - 0.132%

Keyset 'Cyclic' - 8 cycles of 16 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    132 (1.13x) (16)
Testing collisions (high 23-34 bits) - Worst is 34 bits: 36/29 (1.24x)
Testing collisions (low  32-bit) - Expected        116.4, actual    100 (0.86x)
Testing collisions (low  23-34 bits) - Worst is 29 bits: 947/930 (1.02x)
Testing distribution - Worst bias is the 17-bit window at bit 15 - 0.109%


[[[ Keyset 'TwoBytes' Tests ]]]

Keyset 'TwoBytes' - up-to-4-byte keys, 652545 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected         49.6, actual     38 (0.77x)
Testing collisions (high 23-33 bits) - Worst is 23 bits: 24681/24735 (1.00x)
Testing collisions (low  32-bit) - Expected         49.6, actual     44 (0.89x)
Testing collisions (low  23-33 bits) - Worst is 31 bits: 108/99 (1.09x)
Testing distribution - Worst bias is the 16-bit window at bit 22 - 0.120%

Keyset 'TwoBytes' - up-to-8-byte keys, 5471025 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       3483.1, actual   3447 (0.99x) (-36)
Testing collisions (high 26-39 bits) - Worst is 26 bits: 217954/217072 (1.00x)
Testing collisions (low  32-bit) - Expected       3483.1, actual   3344 (0.96x)
Testing collisions (low  26-39 bits) - Worst is 38 bits: 57/54 (1.05x)
Testing distribution - Worst bias is the 20-bit window at bit 10 - 0.087%

Keyset 'TwoBytes' - up-to-12-byte keys, 18616785 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      40289.5, actual  40269 (1.00x) (-20)
Testing collisions (high 27-42 bits) - Worst is 38 bits: 644/630 (1.02x)
Testing collisions (low  32-bit) - Expected      40289.5, actual  40110 (1.00x) (-179)
Testing collisions (low  27-42 bits) - Worst is 42 bits: 41/39 (1.04x)
Testing distribution - Worst bias is the 20-bit window at bit 49 - 0.024%

Keyset 'TwoBytes' - up-to-16-byte keys, 44251425 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected     227182.3, actual 227638 (1.00x) (456)
Testing collisions (high 29-45 bits) - Worst is 39 bits: 1794/1780 (1.01x)
Testing collisions (low  32-bit) - Expected     227182.3, actual 226966 (1.00x) (-216)
Testing collisions (low  29-45 bits) - Worst is 42 bits: 253/222 (1.14x)
Testing distribution - Worst bias is the 20-bit window at bit 46 - 0.009%

Keyset 'TwoBytes' - up-to-20-byte keys, 86536545 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected     865959.1, actual 866176 (1.00x) (217)
Testing collisions (high 30-47 bits) - Worst is 43 bits: 437/425 (1.03x)
Testing collisions (low  32-bit) - Expected     865959.1, actual 865959 (1.00x)
Testing collisions (low  30-47 bits) - Worst is 45 bits: 121/106 (1.14x)
Testing distribution - Worst bias is the 20-bit window at bit 45 - 0.004%


[[[ Keyset 'Text' Tests ]]]

Keyset 'Text' - keys of form "FooXXXXBar" - 14776336 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      25389.0, actual  25652 (1.01x) (263)
Testing collisions (high 27-42 bits) - Worst is 42 bits: 31/24 (1.25x)
Testing collisions (low  32-bit) - Expected      25389.0, actual  25380 (1.00x) (-9)
Testing collisions (low  27-42 bits) - Worst is 38 bits: 412/397 (1.04x)
Testing distribution - Worst bias is the 20-bit window at bit 12 - 0.022%

Keyset 'Text' - keys of form "FooBarXXXX" - 14776336 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      25389.0, actual  25507 (1.00x) (118)
Testing collisions (high 27-42 bits) - Worst is 42 bits: 29/24 (1.17x)
Testing collisions (low  32-bit) - Expected      25389.0, actual  25496 (1.00x) (107)
Testing collisions (low  27-42 bits) - Worst is 39 bits: 204/198 (1.03x)
Testing distribution - Worst bias is the 20-bit window at bit 61 - 0.021%

Keyset 'Text' - keys of form "XXXXFooBar" - 14776336 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      25389.0, actual  25559 (1.01x) (170)
Testing collisions (high 27-42 bits) - Worst is 41 bits: 60/49 (1.21x)
Testing collisions (low  32-bit) - Expected      25389.0, actual  25471 (1.00x) (82)
Testing collisions (low  27-42 bits) - Worst is 42 bits: 35/24 (1.41x)
Testing distribution - Worst bias is the 20-bit window at bit 63 - 0.033%

Keyset 'Words' - 4000000 random keys of len 6-16 from alnum charset
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1862.1, actual   1822 (0.98x)
Testing collisions (high 25-38 bits) - Worst is 38 bits: 30/29 (1.03x)
Testing collisions (low  32-bit) - Expected       1862.1, actual   1837 (0.99x) (-25)
Testing collisions (low  25-38 bits) - Worst is 25 bits: 229258/229220 (1.00x)
Testing distribution - Worst bias is the 19-bit window at bit 42 - 0.071%

Keyset 'Words' - 4000000 random keys of len 6-16 from password charset
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1862.1, actual   1827 (0.98x) (-35)
Testing collisions (high 25-38 bits) - Worst is 36 bits: 131/116 (1.13x)
Testing collisions (low  32-bit) - Expected       1862.1, actual   1832 (0.98x) (-30)
Testing collisions (low  25-38 bits) - Worst is 38 bits: 32/29 (1.10x)
Testing distribution - Worst bias is the 19-bit window at bit 24 - 0.069%

Keyset 'Words' - 466569 dict words
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected         25.3, actual     27 (1.07x) (2)
Testing collisions (high 22-32 bits) - Worst is 32 bits: 27/25 (1.07x)
Testing collisions (low  32-bit) - Expected         25.3, actual     26 (1.03x) (1)
Testing collisions (low  22-32 bits) - Worst is 30 bits: 120/101 (1.18x)
Testing distribution - Worst bias is the 16-bit window at bit 48 - 0.155%


[[[ Keyset 'Zeroes' Tests ]]]

Keyset 'Zeroes' - 204800 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          4.9, actual      4 (0.82x)
Testing collisions (high 21-29 bits) - Worst is 28 bits: 83/78 (1.06x)
Testing collisions (low  32-bit) - Expected          4.9, actual      2 (0.41x)
Testing collisions (low  21-29 bits) - Worst is 27 bits: 169/156 (1.08x)
Testing distribution - Worst bias is the 15-bit window at bit 31 - 0.261%


[[[ Keyset 'Seed' Tests ]]]

Keyset 'Seed' - 5000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2909.3, actual   2808 (0.97x)
Testing collisions (high 26-39 bits) - Worst is 37 bits: 93/90 (1.02x)
Testing collisions (low  32-bit) - Expected       2909.3, actual   3032 (1.04x) (123)
Testing collisions (low  26-39 bits) - Worst is 38 bits: 64/45 (1.41x)
Testing distribution - Worst bias is the 19-bit window at bit 63 - 0.044%


[[[ Keyset 'PerlinNoise' Tests ]]]

Testing 16777216 coordinates (L2) : 
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      32725.4, actual  32777 (1.00x) (52)
Testing collisions (high 27-42 bits) - Worst is 42 bits: 37/31 (1.16x)
Testing collisions (low  32-bit) - Expected      32725.4, actual  32511 (0.99x) (-214)
Testing collisions (low  27-42 bits) - Worst is 42 bits: 36/31 (1.13x)

Testing AV variant, 128 count with 4 spacing, 4-12:
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1116.2, actual   1087 (0.97x)
Testing collisions (high 25-37 bits) - Worst is 27 bits: 35400/35452 (1.00x)
Testing collisions (low  32-bit) - Expected       1116.2, actual   1067 (0.96x)
Testing collisions (low  25-37 bits) - Worst is 34 bits: 280/279 (1.00x)


[[[ Diff 'Differential' Tests ]]]

Testing 8303632 up-to-5-bit differentials in 64-bit keys -> 64 bit hashes.
1000 reps, 8303632000 total tests, expecting 0.00 random collisions..........
0 total collisions, of which 0 single collisions were ignored

Testing 11017632 up-to-4-bit differentials in 128-bit keys -> 64 bit hashes.
1000 reps, 11017632000 total tests, expecting 0.00 random collisions..........
0 total collisions, of which 0 single collisions were ignored

Testing 2796416 up-to-3-bit differentials in 256-bit keys -> 64 bit hashes.
1000 reps, 2796416000 total tests, expecting 0.00 random collisions..........
0 total collisions, of which 0 single collisions were ignored


[[[ DiffDist 'Differential Distribution' Tests ]]]

Testing bit 0
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    526 (1.03x) (15)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 38/31 (1.19x)
Testing collisions (low  32-bit) - Expected        511.9, actual    495 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit  3 - 0.118%

Testing bit 1
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    514 (1.00x) (3)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 132/127 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    496 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 25 bits: 64220/64191 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 60 - 0.077%

Testing bit 2
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    520 (1.02x) (9)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 35/31 (1.09x)
Testing collisions (low  32-bit) - Expected        511.9, actual    494 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 15 - 0.078%

Testing bit 3
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    538 (1.05x) (27)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1083/1023 (1.06x)
Testing collisions (low  32-bit) - Expected        511.9, actual    470 (0.92x)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2093/2046 (1.02x)
Testing distribution - Worst bias is the 18-bit window at bit 42 - 0.069%

Testing bit 4
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    514 (1.00x) (3)
Testing collisions (high 24-36 bits) - Worst is 28 bits: 8315/8170 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    549 (1.07x) (38)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 78/63 (1.22x)
Testing distribution - Worst bias is the 18-bit window at bit 52 - 0.081%

Testing bit 5
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    499 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing collisions (low  32-bit) - Expected        511.9, actual    528 (1.03x) (17)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 44/31 (1.38x)
Testing distribution - Worst bias is the 18-bit window at bit 40 - 0.116%

Testing bit 6
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    510 (1.00x) (-1)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 266/255 (1.04x)
Testing collisions (low  32-bit) - Expected        511.9, actual    513 (1.00x) (2)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 73/63 (1.14x)
Testing distribution - Worst bias is the 18-bit window at bit 14 - 0.106%

Testing bit 7
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    528 (1.03x) (17)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing collisions (low  32-bit) - Expected        511.9, actual    553 (1.08x) (42)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 38/31 (1.19x)
Testing distribution - Worst bias is the 18-bit window at bit 42 - 0.095%

Testing bit 8
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    526 (1.03x) (15)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing collisions (low  32-bit) - Expected        511.9, actual    513 (1.00x) (2)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 513/511 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 27 - 0.075%

Testing bit 9
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 76/63 (1.19x)
Testing collisions (low  32-bit) - Expected        511.9, actual    519 (1.01x) (8)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 73/63 (1.14x)
Testing distribution - Worst bias is the 18-bit window at bit 15 - 0.113%

Testing bit 10
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    494 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 71/63 (1.11x)
Testing collisions (low  32-bit) - Expected        511.9, actual    483 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 26 bits: 32540/32429 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 40 - 0.099%

Testing bit 11
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    497 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 132/127 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    504 (0.98x) (-7)
Testing collisions (low  24-36 bits) - Worst is 26 bits: 32534/32429 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 19 - 0.059%

Testing bit 12
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    495 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 136/127 (1.06x)
Testing collisions (low  32-bit) - Expected        511.9, actual    537 (1.05x) (26)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 46/31 (1.44x)
Testing distribution - Worst bias is the 18-bit window at bit 10 - 0.058%

Testing bit 13
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    501 (0.98x)
Testing collisions (high 24-36 bits) - Worst is 28 bits: 8316/8170 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    553 (1.08x) (42)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 72/63 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 41 - 0.054%

Testing bit 14
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    525 (1.03x) (14)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing collisions (low  32-bit) - Expected        511.9, actual    488 (0.95x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 69/63 (1.08x)
Testing distribution - Worst bias is the 18-bit window at bit 43 - 0.088%

Testing bit 15
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    532 (1.04x) (21)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 281/255 (1.10x)
Testing collisions (low  32-bit) - Expected        511.9, actual    555 (1.08x) (44)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 74/63 (1.16x)
Testing distribution - Worst bias is the 18-bit window at bit 50 - 0.079%

Testing bit 16
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2084/2046 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    520 (1.02x) (9)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 520/511 (1.02x)
Testing distribution - Worst bias is the 18-bit window at bit 58 - 0.076%

Testing bit 17
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    518 (1.01x) (7)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 260/255 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 138/127 (1.08x)
Testing distribution - Worst bias is the 17-bit window at bit 54 - 0.068%

Testing bit 18
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    514 (1.00x) (3)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2114/2046 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    513 (1.00x) (2)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 132/127 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 62 - 0.083%

Testing bit 19
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 529/511 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    501 (0.98x)
Testing collisions (low  24-36 bits) - Worst is 27 bits: 16419/16298 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 29 - 0.080%

Testing bit 20
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    504 (0.98x) (-7)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1040/1023 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    507 (0.99x) (-4)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 18-bit window at bit 50 - 0.092%

Testing bit 21
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    510 (1.00x) (-1)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (low  24-36 bits) - Worst is 31 bits: 1062/1023 (1.04x)
Testing distribution - Worst bias is the 18-bit window at bit 34 - 0.079%

Testing bit 22
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    505 (0.99x) (-6)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 260/255 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    466 (0.91x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 47 - 0.077%

Testing bit 23
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    515 (1.01x) (4)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 44/31 (1.38x)
Testing collisions (low  32-bit) - Expected        511.9, actual    500 (0.98x)
Testing collisions (low  24-36 bits) - Worst is 26 bits: 32370/32429 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 57 - 0.064%

Testing bit 24
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    507 (0.99x) (-4)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 272/255 (1.06x)
Testing collisions (low  32-bit) - Expected        511.9, actual    495 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 27 bits: 16412/16298 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 46 - 0.057%

Testing bit 25
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    512 (1.00x) (1)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 269/255 (1.05x)
Testing collisions (low  32-bit) - Expected        511.9, actual    549 (1.07x) (38)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 549/511 (1.07x)
Testing distribution - Worst bias is the 18-bit window at bit 39 - 0.089%

Testing bit 26
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    508 (0.99x) (-3)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1036/1023 (1.01x)
Testing collisions (low  32-bit) - Expected        511.9, actual    481 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit  1 - 0.065%

Testing bit 27
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    527 (1.03x) (16)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 69/63 (1.08x)
Testing collisions (low  32-bit) - Expected        511.9, actual    465 (0.91x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing distribution - Worst bias is the 18-bit window at bit 13 - 0.101%

Testing bit 28
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    484 (0.95x)
Testing collisions (high 24-36 bits) - Worst is 25 bits: 64180/64191 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    536 (1.05x) (25)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 78/63 (1.22x)
Testing distribution - Worst bias is the 18-bit window at bit  8 - 0.130%

Testing bit 29
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    511 (1.00x)
Testing collisions (high 24-36 bits) - Worst is 24 bits: 126329/125777 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    498 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 31 bits: 1036/1023 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit  3 - 0.059%

Testing bit 30
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    519 (1.01x) (8)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2107/2046 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    478 (0.93x)
Testing collisions (low  24-36 bits) - Worst is 25 bits: 64338/64191 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit  4 - 0.078%

Testing bit 31
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    506 (0.99x) (-5)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 74/63 (1.16x)
Testing collisions (low  32-bit) - Expected        511.9, actual    493 (0.96x)
Testing collisions (low  24-36 bits) - Worst is 24 bits: 125635/125777 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 50 - 0.072%

Testing bit 32
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    508 (0.99x) (-3)
Testing collisions (high 24-36 bits) - Worst is 24 bits: 125444/125777 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    514 (1.00x) (3)
Testing collisions (low  24-36 bits) - Worst is 29 bits: 4149/4090 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 12 - 0.094%

Testing bit 33
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    546 (1.07x) (35)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 546/511 (1.07x)
Testing collisions (low  32-bit) - Expected        511.9, actual    546 (1.07x) (35)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 139/127 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 45 - 0.069%

Testing bit 34
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    505 (0.99x) (-6)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    503 (0.98x) (-8)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 142/127 (1.11x)
Testing distribution - Worst bias is the 18-bit window at bit 52 - 0.133%

Testing bit 35
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    506 (0.99x) (-5)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 71/63 (1.11x)
Testing collisions (low  32-bit) - Expected        511.9, actual    483 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 72/63 (1.13x)
Testing distribution - Worst bias is the 17-bit window at bit 31 - 0.062%

Testing bit 36
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    519 (1.01x) (8)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 519/511 (1.01x)
Testing collisions (low  32-bit) - Expected        511.9, actual    496 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2121/2046 (1.04x)
Testing distribution - Worst bias is the 18-bit window at bit 37 - 0.119%

Testing bit 37
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    525 (1.03x) (14)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 77/63 (1.20x)
Testing collisions (low  32-bit) - Expected        511.9, actual    465 (0.91x)
Testing collisions (low  24-36 bits) - Worst is 29 bits: 4123/4090 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 23 - 0.082%

Testing bit 38
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    542 (1.06x) (31)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 70/63 (1.09x)
Testing collisions (low  32-bit) - Expected        511.9, actual    507 (0.99x) (-4)
Testing collisions (low  24-36 bits) - Worst is 31 bits: 1065/1023 (1.04x)
Testing distribution - Worst bias is the 18-bit window at bit 23 - 0.058%

Testing bit 39
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    523 (1.02x) (12)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1056/1023 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    479 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 24 bits: 125933/125777 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 11 - 0.066%

Testing bit 40
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    528 (1.03x) (17)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 47/31 (1.47x)
Testing collisions (low  32-bit) - Expected        511.9, actual    532 (1.04x) (21)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 134/127 (1.05x)
Testing distribution - Worst bias is the 18-bit window at bit 39 - 0.109%

Testing bit 41
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    474 (0.93x)
Testing collisions (high 24-36 bits) - Worst is 27 bits: 16336/16298 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (low  24-36 bits) - Worst is 31 bits: 1038/1023 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 62 - 0.067%

Testing bit 42
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    511 (1.00x)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 137/127 (1.07x)
Testing collisions (low  32-bit) - Expected        511.9, actual    510 (1.00x) (-1)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 72/63 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit  5 - 0.079%

Testing bit 43
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    503 (0.98x) (-8)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing collisions (low  32-bit) - Expected        511.9, actual    528 (1.03x) (17)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 74/63 (1.16x)
Testing distribution - Worst bias is the 18-bit window at bit 38 - 0.089%

Testing bit 44
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    497 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 43/31 (1.34x)
Testing collisions (low  32-bit) - Expected        511.9, actual    511 (1.00x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 71/63 (1.11x)
Testing distribution - Worst bias is the 18-bit window at bit 51 - 0.073%

Testing bit 45
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 529/511 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    535 (1.05x) (24)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 276/255 (1.08x)
Testing distribution - Worst bias is the 18-bit window at bit 47 - 0.092%

Testing bit 46
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    534 (1.04x) (23)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 143/127 (1.12x)
Testing collisions (low  32-bit) - Expected        511.9, actual    507 (0.99x) (-4)
Testing collisions (low  24-36 bits) - Worst is 29 bits: 4172/4090 (1.02x)
Testing distribution - Worst bias is the 18-bit window at bit  9 - 0.073%

Testing bit 47
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 263/255 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    560 (1.09x) (49)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 18-bit window at bit 24 - 0.073%

Testing bit 48
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    514 (1.00x) (3)
Testing collisions (high 24-36 bits) - Worst is 29 bits: 4230/4090 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    512 (1.00x) (1)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 18-bit window at bit 15 - 0.069%

Testing bit 49
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    527 (1.03x) (16)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 265/255 (1.04x)
Testing collisions (low  32-bit) - Expected        511.9, actual    491 (0.96x)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 132/127 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 49 - 0.077%

Testing bit 50
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing collisions (low  32-bit) - Expected        511.9, actual    516 (1.01x) (5)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 39/31 (1.22x)
Testing distribution - Worst bias is the 18-bit window at bit 53 - 0.088%

Testing bit 51
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    461 (0.90x)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2065/2046 (1.01x)
Testing collisions (low  32-bit) - Expected        511.9, actual    502 (0.98x) (-9)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2045/2046 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit  2 - 0.073%

Testing bit 52
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    527 (1.03x) (16)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 527/511 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    532 (1.04x) (21)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 140/127 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 20 - 0.083%

Testing bit 53
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    491 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 41/31 (1.28x)
Testing collisions (low  32-bit) - Expected        511.9, actual    498 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 24 bits: 126008/125777 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 18 - 0.083%

Testing bit 54
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    534 (1.04x) (23)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 146/127 (1.14x)
Testing collisions (low  32-bit) - Expected        511.9, actual    544 (1.06x) (33)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 544/511 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 49 - 0.052%

Testing bit 55
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    454 (0.89x)
Testing collisions (high 24-36 bits) - Worst is 24 bits: 125990/125777 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    504 (0.98x) (-7)
Testing collisions (low  24-36 bits) - Worst is 28 bits: 8220/8170 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 21 - 0.108%

Testing bit 56
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    561 (1.10x) (50)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 148/127 (1.16x)
Testing collisions (low  32-bit) - Expected        511.9, actual    526 (1.03x) (15)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 526/511 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 15 - 0.067%

Testing bit 57
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    468 (0.91x)
Testing collisions (high 24-36 bits) - Worst is 26 bits: 32528/32429 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 43/31 (1.34x)
Testing distribution - Worst bias is the 18-bit window at bit 46 - 0.111%

Testing bit 58
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    534 (1.04x) (23)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 48/31 (1.50x)
Testing collisions (low  32-bit) - Expected        511.9, actual    509 (0.99x) (-2)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 264/255 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 39 - 0.080%

Testing bit 59
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    516 (1.01x) (5)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2078/2046 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    492 (0.96x)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2113/2046 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 57 - 0.088%

Testing bit 60
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    465 (0.91x)
Testing collisions (high 24-36 bits) - Worst is 28 bits: 8183/8170 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    516 (1.01x) (5)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 69/63 (1.08x)
Testing distribution - Worst bias is the 18-bit window at bit 38 - 0.067%

Testing bit 61
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    537 (1.05x) (26)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 142/127 (1.11x)
Testing collisions (low  32-bit) - Expected        511.9, actual    502 (0.98x) (-9)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 68/63 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 19 - 0.075%

Testing bit 62
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    572 (1.12x) (61)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 304/255 (1.19x)
Testing collisions (low  32-bit) - Expected        511.9, actual    514 (1.00x) (3)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 38/31 (1.19x)
Testing distribution - Worst bias is the 18-bit window at bit 19 - 0.068%

Testing bit 63
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    495 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing collisions (low  32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 529/511 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 39 - 0.089%


[[[ MomentChi2 Tests ]]]

Analyze hashes produced from a serie of linearly increasing numbers of 32-bit, using a step of 2 ... 
Target values to approximate : 38918200.000000 - 273633.333333 
Popcount 1 stats : 38919570.005124 - 273658.703091
Popcount 0 stats : 38918319.622388 - 273636.624134
MomentChi2 for bits 1 :   3.42946 
MomentChi2 for bits 0 :  0.0261471 

Derivative stats (transition from 2 consecutive values) : 
Popcount 1 stats : 38919286.374871 - 273651.807467
Popcount 0 stats : 38918700.222160 - 273641.544934
MomentChi2 for deriv b1 :   2.15648 
MomentChi2 for deriv b0 :  0.457215 

  Great 


[[[ Prng Tests ]]]

Generating 33554432 random numbers : 
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected     130731.3, actual 130706 (1.00x) (-25)
Testing collisions (high 28-44 bits) - Worst is 44 bits: 38/31 (1.19x)
Testing collisions (low  32-bit) - Expected     130731.3, actual 130394 (1.00x) (-337)
Testing collisions (low  28-44 bits) - Worst is 41 bits: 261/255 (1.02x)

[[[ BadSeeds Tests ]]]

0x0 PASS


Input vcode 0x00000001, Output vcode 0x00000001, Result vcode 0x00000001
Verification value is 0x00000001 - Testing took 681.305909 seconds
------------------------------------------------------------
```

------------------------------------------------------------
### Stable Hash 64bit SMHasher Results

The following results were achieved on my personal computer when testing the 64bit variant of the facil.io Stable Hash (`fio_stable_hash`).

```txt
------------------------------------------------------------
--- Testing Stable "facil.io Stable Hash 64bit" GOOD

[[[ Sanity Tests ]]]

Verification value 0x6993BB92 ....... PASS
Running sanity check 1     .......... PASS
Running AppendedZeroesTest .......... PASS

[[[ Speed Tests ]]]

Bulk speed test - 262144-byte keys
Alignment  7 - 12.548 bytes/cycle - 35899.15 MiB/sec @ 3 ghz
Alignment  6 - 12.560 bytes/cycle - 35934.05 MiB/sec @ 3 ghz
Alignment  5 - 12.554 bytes/cycle - 35917.07 MiB/sec @ 3 ghz
Alignment  4 - 12.560 bytes/cycle - 35935.44 MiB/sec @ 3 ghz
Alignment  3 - 12.559 bytes/cycle - 35931.79 MiB/sec @ 3 ghz
Alignment  2 - 12.557 bytes/cycle - 35926.78 MiB/sec @ 3 ghz
Alignment  1 - 12.561 bytes/cycle - 35936.39 MiB/sec @ 3 ghz
Alignment  0 - 12.678 bytes/cycle - 36273.41 MiB/sec @ 3 ghz
Average      - 12.572 bytes/cycle - 35969.26 MiB/sec @ 3 ghz

Small key speed test -    1-byte keys -    20.57 cycles/hash
Small key speed test -    2-byte keys -    21.00 cycles/hash
Small key speed test -    3-byte keys -    21.00 cycles/hash
Small key speed test -    4-byte keys -    20.99 cycles/hash
Small key speed test -    5-byte keys -    21.00 cycles/hash
Small key speed test -    6-byte keys -    21.00 cycles/hash
Small key speed test -    7-byte keys -    22.00 cycles/hash
Small key speed test -    8-byte keys -    21.00 cycles/hash
Small key speed test -    9-byte keys -    21.00 cycles/hash
Small key speed test -   10-byte keys -    21.00 cycles/hash
Small key speed test -   11-byte keys -    21.00 cycles/hash
Small key speed test -   12-byte keys -    21.00 cycles/hash
Small key speed test -   13-byte keys -    21.00 cycles/hash
Small key speed test -   14-byte keys -    21.00 cycles/hash
Small key speed test -   15-byte keys -    21.00 cycles/hash
Small key speed test -   16-byte keys -    21.00 cycles/hash
Small key speed test -   17-byte keys -    22.88 cycles/hash
Small key speed test -   18-byte keys -    22.92 cycles/hash
Small key speed test -   19-byte keys -    22.94 cycles/hash
Small key speed test -   20-byte keys -    22.93 cycles/hash
Small key speed test -   21-byte keys -    22.94 cycles/hash
Small key speed test -   22-byte keys -    22.92 cycles/hash
Small key speed test -   23-byte keys -    22.89 cycles/hash
Small key speed test -   24-byte keys -    22.89 cycles/hash
Small key speed test -   25-byte keys -    22.93 cycles/hash
Small key speed test -   26-byte keys -    22.90 cycles/hash
Small key speed test -   27-byte keys -    23.03 cycles/hash
Small key speed test -   28-byte keys -    22.81 cycles/hash
Small key speed test -   29-byte keys -    22.89 cycles/hash
Small key speed test -   30-byte keys -    23.13 cycles/hash
Small key speed test -   31-byte keys -    22.99 cycles/hash
Average                                    21.953 cycles/hash

[[[ 'Hashmap' Speed Tests ]]]

std::unordered_map
Init std HashMapTest:     155.249 cycles/op (466569 inserts, 1% deletions)
Running std HashMapTest:  93.391 cycles/op (1.6 stdv, found 461903)

greg7mdp/parallel-hashmap
Init fast HashMapTest:    143.332 cycles/op (466569 inserts, 1% deletions)
Running fast HashMapTest: 93.590 cycles/op (1.3 stdv, found 461903)

facil.io HashMap
Init fast fio_map_s Test:    87.173 cycles/op (466569 inserts, 1% deletions)
Running fast fio_map_s Test: 49.115 cycles/op (0.5 stdv, found 461903)
 ....... PASS

[[[ Avalanche Tests ]]]

Testing   24-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.632667%
Testing   32-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.712000%
Testing   40-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.704000%
Testing   48-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.696667%
Testing   56-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.686000%
Testing   64-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.712000%
Testing   72-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.636000%
Testing   80-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.630667%
Testing   96-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.690667%
Testing  112-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.670667%
Testing  128-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.760667%
Testing  160-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.773333%
Testing  512-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.825333%
Testing 1024-bit keys ->  64-bit hashes, 300000 reps.......... worst bias is 0.753333%

[[[ Keyset 'Sparse' Tests ]]]

Keyset 'Sparse' - 16-bit keys with up to 9 bits set - 50643 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          0.3, actual      0 (0.00x)
Testing collisions (high 19-25 bits) - Worst is 23 bits: 171/152 (1.12x)
Testing collisions (low  32-bit) - Expected          0.3, actual      0 (0.00x)
Testing collisions (low  19-25 bits) - Worst is 21 bits: 602/606 (0.99x)
Testing distribution - Worst bias is the 13-bit window at bit 31 - 0.586%

Keyset 'Sparse' - 24-bit keys with up to 8 bits set - 1271626 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        188.2, actual    197 (1.05x) (9)
Testing collisions (high 24-35 bits) - Worst is 33 bits: 100/94 (1.06x)
Testing collisions (low  32-bit) - Expected        188.2, actual    170 (0.90x)
Testing collisions (low  24-35 bits) - Worst is 26 bits: 12071/11972 (1.01x)
Testing distribution - Worst bias is the 17-bit window at bit 48 - 0.087%

Keyset 'Sparse' - 32-bit keys with up to 7 bits set - 4514873 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2372.2, actual   2345 (0.99x) (-27)
Testing collisions (high 25-38 bits) - Worst is 38 bits: 47/37 (1.27x)
Testing collisions (low  32-bit) - Expected       2372.2, actual   2374 (1.00x) (2)
Testing collisions (low  25-38 bits) - Worst is 30 bits: 9492/9478 (1.00x)
Testing distribution - Worst bias is the 19-bit window at bit 35 - 0.057%

Keyset 'Sparse' - 40-bit keys with up to 6 bits set - 4598479 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2460.8, actual   2380 (0.97x)
Testing collisions (high 25-38 bits) - Worst is 36 bits: 163/153 (1.06x)
Testing collisions (low  32-bit) - Expected       2460.8, actual   2490 (1.01x) (30)
Testing collisions (low  25-38 bits) - Worst is 38 bits: 45/38 (1.17x)
Testing distribution - Worst bias is the 19-bit window at bit 50 - 0.061%

Keyset 'Sparse' - 48-bit keys with up to 6 bits set - 14196869 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      23437.8, actual  23277 (0.99x) (-160)
Testing collisions (high 27-42 bits) - Worst is 42 bits: 28/22 (1.22x)
Testing collisions (low  32-bit) - Expected      23437.8, actual  23272 (0.99x) (-165)
Testing collisions (low  27-42 bits) - Worst is 30 bits: 93648/93442 (1.00x)
Testing distribution - Worst bias is the 20-bit window at bit 29 - 0.022%

Keyset 'Sparse' - 56-bit keys with up to 5 bits set - 4216423 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2069.0, actual   2065 (1.00x) (-3)
Testing collisions (high 25-38 bits) - Worst is 38 bits: 37/32 (1.14x)
Testing collisions (low  32-bit) - Expected       2069.0, actual   2042 (0.99x) (-26)
Testing collisions (low  25-38 bits) - Worst is 38 bits: 40/32 (1.24x)
Testing distribution - Worst bias is the 19-bit window at bit 37 - 0.050%

Keyset 'Sparse' - 64-bit keys with up to 5 bits set - 8303633 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8021.7, actual   8020 (1.00x) (-1)
Testing collisions (high 26-40 bits) - Worst is 40 bits: 36/31 (1.15x)
Testing collisions (low  32-bit) - Expected       8021.7, actual   8038 (1.00x) (17)
Testing collisions (low  26-40 bits) - Worst is 31 bits: 16101/16033 (1.00x)
Testing distribution - Worst bias is the 20-bit window at bit 43 - 0.043%

Keyset 'Sparse' - 72-bit keys with up to 5 bits set - 15082603 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      26451.8, actual  26679 (1.01x) (228)
Testing collisions (high 27-42 bits) - Worst is 39 bits: 218/206 (1.05x)
Testing collisions (low  32-bit) - Expected      26451.8, actual  26434 (1.00x) (-17)
Testing collisions (low  27-42 bits) - Worst is 35 bits: 3324/3309 (1.00x)
Testing distribution - Worst bias is the 20-bit window at bit 26 - 0.028%

Keyset 'Sparse' - 96-bit keys with up to 4 bits set - 3469497 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1401.0, actual   1462 (1.04x) (62)
Testing collisions (high 25-38 bits) - Worst is 37 bits: 53/43 (1.21x)
Testing collisions (low  32-bit) - Expected       1401.0, actual   1406 (1.00x) (6)
Testing collisions (low  25-38 bits) - Worst is 37 bits: 60/43 (1.37x)
Testing distribution - Worst bias is the 19-bit window at bit 12 - 0.088%

Keyset 'Sparse' - 160-bit keys with up to 4 bits set - 26977161 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      84546.1, actual  84527 (1.00x) (-19)
Testing collisions (high 28-44 bits) - Worst is 44 bits: 29/20 (1.40x)
Testing collisions (low  32-bit) - Expected      84546.1, actual  84586 (1.00x) (40)
Testing collisions (low  28-44 bits) - Worst is 41 bits: 172/165 (1.04x)
Testing distribution - Worst bias is the 20-bit window at bit 39 - 0.017%

Keyset 'Sparse' - 256-bit keys with up to 3 bits set - 2796417 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        910.2, actual    947 (1.04x) (37)
Testing collisions (high 25-37 bits) - Worst is 36 bits: 63/56 (1.11x)
Testing collisions (low  32-bit) - Expected        910.2, actual    925 (1.02x) (15)
Testing collisions (low  25-37 bits) - Worst is 33 bits: 473/455 (1.04x)
Testing distribution - Worst bias is the 19-bit window at bit 50 - 0.082%

Keyset 'Sparse' - 512-bit keys with up to 3 bits set - 22370049 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      58155.4, actual  57664 (0.99x) (-491)
Testing collisions (high 28-43 bits) - Worst is 38 bits: 924/910 (1.02x)
Testing collisions (low  32-bit) - Expected      58155.4, actual  58193 (1.00x) (38)
Testing collisions (low  28-43 bits) - Worst is 39 bits: 498/455 (1.09x)
Testing distribution - Worst bias is the 20-bit window at bit 23 - 0.017%

Keyset 'Sparse' - 1024-bit keys with up to 2 bits set - 524801 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected         32.1, actual     30 (0.94x)
Testing collisions (high 22-32 bits) - Worst is 28 bits: 518/512 (1.01x)
Testing collisions (low  32-bit) - Expected         32.1, actual     23 (0.72x)
Testing collisions (low  22-32 bits) - Worst is 29 bits: 294/256 (1.15x)
Testing distribution - Worst bias is the 16-bit window at bit 48 - 0.155%

Keyset 'Sparse' - 2048-bit keys with up to 2 bits set - 2098177 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        512.4, actual    499 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 29 bits: 4139/4094 (1.01x)
Testing collisions (low  32-bit) - Expected        512.4, actual    529 (1.03x) (17)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 143/128 (1.12x)
Testing distribution - Worst bias is the 18-bit window at bit 30 - 0.057%


[[[ Keyset 'Permutation' Tests ]]]

Combination Lowbits Tests:
Keyset 'Combination' - up to 7 blocks from a set of 8 - 2396744 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        668.6, actual    670 (1.00x) (2)
Testing collisions (high 24-37 bits) - Worst is 36 bits: 44/41 (1.05x)
Testing collisions (low  32-bit) - Expected        668.6, actual    655 (0.98x)
Testing collisions (low  24-37 bits) - Worst is 34 bits: 173/167 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit  9 - 0.080%


Combination Highbits Tests
Keyset 'Combination' - up to 7 blocks from a set of 8 - 2396744 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        668.6, actual    657 (0.98x) (-11)
Testing collisions (high 24-37 bits) - Worst is 37 bits: 24/20 (1.15x)
Testing collisions (low  32-bit) - Expected        668.6, actual    691 (1.03x) (23)
Testing collisions (low  24-37 bits) - Worst is 33 bits: 362/334 (1.08x)
Testing distribution - Worst bias is the 18-bit window at bit  9 - 0.047%


Combination Hi-Lo Tests:
Keyset 'Combination' - up to 6 blocks from a set of 15 - 12204240 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      17322.9, actual  17106 (0.99x) (-216)
Testing collisions (high 27-41 bits) - Worst is 41 bits: 37/33 (1.09x)
Testing collisions (low  32-bit) - Expected      17322.9, actual  17214 (0.99x) (-108)
Testing collisions (low  27-41 bits) - Worst is 28 bits: 273805/273271 (1.00x)
Testing distribution - Worst bias is the 20-bit window at bit  3 - 0.030%


Combination 0x8000000 Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8288 (1.01x) (102)
Testing collisions (high 26-40 bits) - Worst is 40 bits: 41/31 (1.28x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8135 (0.99x) (-51)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 39/31 (1.22x)
Testing distribution - Worst bias is the 20-bit window at bit 52 - 0.038%


Combination 0x0000001 Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8207 (1.00x) (21)
Testing collisions (high 26-40 bits) - Worst is 40 bits: 36/31 (1.13x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8100 (0.99x) (-86)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 20-bit window at bit 43 - 0.036%


Combination 0x800000000000000 Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   7983 (0.98x)
Testing collisions (high 26-40 bits) - Worst is 40 bits: 34/31 (1.06x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8152 (1.00x) (-34)
Testing collisions (low  26-40 bits) - Worst is 36 bits: 525/511 (1.03x)
Testing distribution - Worst bias is the 20-bit window at bit  4 - 0.054%


Combination 0x000000000000001 Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8201 (1.00x) (15)
Testing collisions (high 26-40 bits) - Worst is 39 bits: 66/63 (1.03x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8064 (0.99x) (-122)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 39/31 (1.22x)
Testing distribution - Worst bias is the 20-bit window at bit 49 - 0.040%


Combination 16-bytes [0-1] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8156 (1.00x) (-30)
Testing collisions (high 26-40 bits) - Worst is 28 bits: 130239/129717 (1.00x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8355 (1.02x) (169)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 39/31 (1.22x)
Testing distribution - Worst bias is the 20-bit window at bit 44 - 0.040%


Combination 16-bytes [0-last] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8046 (0.98x) (-140)
Testing collisions (high 26-40 bits) - Worst is 36 bits: 550/511 (1.07x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8034 (0.98x) (-152)
Testing collisions (low  26-40 bits) - Worst is 37 bits: 265/255 (1.04x)
Testing distribution - Worst bias is the 20-bit window at bit 24 - 0.042%


Combination 32-bytes [0-1] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8155 (1.00x) (-31)
Testing collisions (high 26-40 bits) - Worst is 39 bits: 84/63 (1.31x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8295 (1.01x) (109)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 41/31 (1.28x)
Testing distribution - Worst bias is the 20-bit window at bit  4 - 0.036%


Combination 32-bytes [0-last] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8248 (1.01x) (62)
Testing collisions (high 26-40 bits) - Worst is 37 bits: 279/255 (1.09x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8246 (1.01x) (60)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 20-bit window at bit 51 - 0.040%


Combination 64-bytes [0-1] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8174 (1.00x) (-12)
Testing collisions (high 26-40 bits) - Worst is 38 bits: 136/127 (1.06x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8053 (0.98x) (-133)
Testing collisions (low  26-40 bits) - Worst is 37 bits: 269/255 (1.05x)
Testing distribution - Worst bias is the 20-bit window at bit 46 - 0.043%


Combination 64-bytes [0-last] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8175 (1.00x) (-11)
Testing collisions (high 26-40 bits) - Worst is 39 bits: 68/63 (1.06x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8116 (0.99x) (-70)
Testing collisions (low  26-40 bits) - Worst is 40 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 20-bit window at bit 10 - 0.042%


Combination 128-bytes [0-1] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8223 (1.00x) (37)
Testing collisions (high 26-40 bits) - Worst is 38 bits: 130/127 (1.02x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8294 (1.01x) (108)
Testing collisions (low  26-40 bits) - Worst is 38 bits: 149/127 (1.16x)
Testing distribution - Worst bias is the 20-bit window at bit 55 - 0.045%


Combination 128-bytes [0-last] Tests:
Keyset 'Combination' - up to 22 blocks from a set of 2 - 8388606 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8186.7, actual   8163 (1.00x) (-23)
Testing collisions (high 26-40 bits) - Worst is 40 bits: 38/31 (1.19x)
Testing collisions (low  32-bit) - Expected       8186.7, actual   8198 (1.00x) (12)
Testing collisions (low  26-40 bits) - Worst is 35 bits: 1055/1023 (1.03x)
Testing distribution - Worst bias is the 20-bit window at bit 45 - 0.041%


[[[ Keyset 'Window' Tests ]]]

Keyset 'Window' -  32-bit key,  25-bit window - 32 tests, 33554432 keys per test
Window at   0 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   1 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   2 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   3 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   4 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   5 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   6 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   7 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   8 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at   9 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  10 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  11 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  12 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  13 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  14 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  15 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  16 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  17 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  18 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  19 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  20 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  21 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  22 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  23 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  24 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  25 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  26 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  27 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  28 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  29 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  30 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  31 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Window at  32 - Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)

[[[ Keyset 'Cyclic' Tests ]]]

Keyset 'Cyclic' - 8 cycles of 8 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    104 (0.89x)
Testing collisions (high 23-34 bits) - Worst is 24 bits: 29355/29218 (1.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    107 (0.92x)
Testing collisions (low  23-34 bits) - Worst is 34 bits: 34/29 (1.17x)
Testing distribution - Worst bias is the 17-bit window at bit 20 - 0.156%

Keyset 'Cyclic' - 8 cycles of 9 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    117 (1.01x) (1)
Testing collisions (high 23-34 bits) - Worst is 32 bits: 117/116 (1.01x)
Testing collisions (low  32-bit) - Expected        116.4, actual    111 (0.95x)
Testing collisions (low  23-34 bits) - Worst is 34 bits: 36/29 (1.24x)
Testing distribution - Worst bias is the 17-bit window at bit 40 - 0.129%

Keyset 'Cyclic' - 8 cycles of 10 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual     95 (0.82x)
Testing collisions (high 23-34 bits) - Worst is 24 bits: 29375/29218 (1.01x)
Testing collisions (low  32-bit) - Expected        116.4, actual    136 (1.17x) (20)
Testing collisions (low  23-34 bits) - Worst is 34 bits: 36/29 (1.24x)
Testing distribution - Worst bias is the 17-bit window at bit 12 - 0.166%

Keyset 'Cyclic' - 8 cycles of 11 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    110 (0.94x)
Testing collisions (high 23-34 bits) - Worst is 23 bits: 57228/57305 (1.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    128 (1.10x) (12)
Testing collisions (low  23-34 bits) - Worst is 34 bits: 34/29 (1.17x)
Testing distribution - Worst bias is the 17-bit window at bit 40 - 0.096%

Keyset 'Cyclic' - 8 cycles of 12 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    102 (0.88x)
Testing collisions (high 23-34 bits) - Worst is 34 bits: 30/29 (1.03x)
Testing collisions (low  32-bit) - Expected        116.4, actual    111 (0.95x)
Testing collisions (low  23-34 bits) - Worst is 24 bits: 29234/29218 (1.00x)
Testing distribution - Worst bias is the 17-bit window at bit 27 - 0.099%

Keyset 'Cyclic' - 8 cycles of 16 bytes - 1000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    128 (1.10x) (12)
Testing collisions (high 23-34 bits) - Worst is 34 bits: 36/29 (1.24x)
Testing collisions (low  32-bit) - Expected        116.4, actual    115 (0.99x) (-1)
Testing collisions (low  23-34 bits) - Worst is 29 bits: 930/930 (1.00x)
Testing distribution - Worst bias is the 17-bit window at bit 30 - 0.148%


[[[ Keyset 'TwoBytes' Tests ]]]

Keyset 'TwoBytes' - up-to-4-byte keys, 652545 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected         49.6, actual     47 (0.95x)
Testing collisions (high 23-33 bits) - Worst is 33 bits: 29/24 (1.17x)
Testing collisions (low  32-bit) - Expected         49.6, actual     47 (0.95x)
Testing collisions (low  23-33 bits) - Worst is 33 bits: 27/24 (1.09x)
Testing distribution - Worst bias is the 16-bit window at bit 16 - 0.189%

Keyset 'TwoBytes' - up-to-8-byte keys, 5471025 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       3483.1, actual   3504 (1.01x) (21)
Testing collisions (high 26-39 bits) - Worst is 39 bits: 31/27 (1.14x)
Testing collisions (low  32-bit) - Expected       3483.1, actual   3493 (1.00x) (10)
Testing collisions (low  26-39 bits) - Worst is 39 bits: 38/27 (1.40x)
Testing distribution - Worst bias is the 20-bit window at bit 52 - 0.096%

Keyset 'TwoBytes' - up-to-12-byte keys, 18616785 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      40289.5, actual  40041 (0.99x) (-248)
Testing collisions (high 27-42 bits) - Worst is 42 bits: 45/39 (1.14x)
Testing collisions (low  32-bit) - Expected      40289.5, actual  40866 (1.01x) (577)
Testing collisions (low  27-42 bits) - Worst is 40 bits: 177/157 (1.12x)
Testing distribution - Worst bias is the 20-bit window at bit 18 - 0.021%

Keyset 'TwoBytes' - up-to-16-byte keys, 44251425 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected     227182.3, actual 227276 (1.00x) (94)
Testing collisions (high 29-45 bits) - Worst is 40 bits: 938/890 (1.05x)
Testing collisions (low  32-bit) - Expected     227182.3, actual 227236 (1.00x) (54)
Testing collisions (low  29-45 bits) - Worst is 45 bits: 37/27 (1.33x)
Testing distribution - Worst bias is the 20-bit window at bit 18 - 0.006%

Keyset 'TwoBytes' - up-to-20-byte keys, 86536545 total keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected     865959.1, actual 866307 (1.00x) (348)
Testing collisions (high 30-47 bits) - Worst is 37 bits: 27622/27237 (1.01x)
Testing collisions (low  32-bit) - Expected     865959.1, actual 865094 (1.00x) (-865)
Testing collisions (low  30-47 bits) - Worst is 46 bits: 68/53 (1.28x)
Testing distribution - Worst bias is the 20-bit window at bit 37 - 0.004%


[[[ Keyset 'Text' Tests ]]]

Keyset 'Text' - keys of form "FooXXXXBar" - 14776336 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      25389.0, actual  25376 (1.00x) (-13)
Testing collisions (high 27-42 bits) - Worst is 42 bits: 26/24 (1.05x)
Testing collisions (low  32-bit) - Expected      25389.0, actual  25263 (1.00x) (-126)
Testing collisions (low  27-42 bits) - Worst is 38 bits: 425/397 (1.07x)
Testing distribution - Worst bias is the 19-bit window at bit 27 - 0.018%

Keyset 'Text' - keys of form "FooBarXXXX" - 14776336 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      25389.0, actual  25364 (1.00x) (-25)
Testing collisions (high 27-42 bits) - Worst is 42 bits: 27/24 (1.09x)
Testing collisions (low  32-bit) - Expected      25389.0, actual  25570 (1.01x) (181)
Testing collisions (low  27-42 bits) - Worst is 42 bits: 30/24 (1.21x)
Testing distribution - Worst bias is the 20-bit window at bit 13 - 0.031%

Keyset 'Text' - keys of form "XXXXFooBar" - 14776336 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      25389.0, actual  25556 (1.01x) (167)
Testing collisions (high 27-42 bits) - Worst is 41 bits: 63/49 (1.27x)
Testing collisions (low  32-bit) - Expected      25389.0, actual  25622 (1.01x) (233)
Testing collisions (low  27-42 bits) - Worst is 39 bits: 212/198 (1.07x)
Testing distribution - Worst bias is the 20-bit window at bit 54 - 0.031%

Keyset 'Words' - 4000000 random keys of len 6-16 from alnum charset
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1862.1, actual   1859 (1.00x) (-3)
Testing collisions (high 25-38 bits) - Worst is 37 bits: 70/58 (1.20x)
Testing collisions (low  32-bit) - Expected       1862.1, actual   1926 (1.03x) (64)
Testing collisions (low  25-38 bits) - Worst is 38 bits: 36/29 (1.24x)
Testing distribution - Worst bias is the 19-bit window at bit 30 - 0.067%

Keyset 'Words' - 4000000 random keys of len 6-16 from password charset
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1862.1, actual   1860 (1.00x) (-2)
Testing collisions (high 25-38 bits) - Worst is 27 bits: 59197/59016 (1.00x)
Testing collisions (low  32-bit) - Expected       1862.1, actual   1829 (0.98x) (-33)
Testing collisions (low  25-38 bits) - Worst is 25 bits: 228540/229220 (1.00x)
Testing distribution - Worst bias is the 19-bit window at bit  7 - 0.042%

Keyset 'Words' - 466569 dict words
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected         25.3, actual     31 (1.22x) (6)
Testing collisions (high 22-32 bits) - Worst is 32 bits: 31/25 (1.22x)
Testing collisions (low  32-bit) - Expected         25.3, actual     20 (0.79x)
Testing collisions (low  22-32 bits) - Worst is 26 bits: 1650/1618 (1.02x)
Testing distribution - Worst bias is the 16-bit window at bit  1 - 0.220%


[[[ Keyset 'Zeroes' Tests ]]]

Keyset 'Zeroes' - 204800 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          4.9, actual      5 (1.02x) (1)
Testing collisions (high 21-29 bits) - Worst is 29 bits: 44/39 (1.13x)
Testing collisions (low  32-bit) - Expected          4.9, actual      4 (0.82x)
Testing collisions (low  21-29 bits) - Worst is 29 bits: 49/39 (1.25x)
Testing distribution - Worst bias is the 15-bit window at bit 29 - 0.313%


[[[ Keyset 'Seed' Tests ]]]

Keyset 'Seed' - 5000000 keys
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2909.3, actual   2994 (1.03x) (85)
Testing collisions (high 26-39 bits) - Worst is 37 bits: 103/90 (1.13x)
Testing collisions (low  32-bit) - Expected       2909.3, actual   2973 (1.02x) (64)
Testing collisions (low  26-39 bits) - Worst is 39 bits: 36/22 (1.58x)
Testing distribution - Worst bias is the 19-bit window at bit 10 - 0.042%


[[[ Keyset 'PerlinNoise' Tests ]]]

Testing 16777216 coordinates (L2) : 
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      32725.4, actual  32957 (1.01x) (232)
Testing collisions (high 27-42 bits) - Worst is 41 bits: 71/63 (1.11x)
Testing collisions (low  32-bit) - Expected      32725.4, actual  32857 (1.00x) (132)
Testing collisions (low  27-42 bits) - Worst is 41 bits: 69/63 (1.08x)

Testing AV variant, 128 count with 4 spacing, 4-12:
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1116.2, actual   1110 (0.99x) (-6)
Testing collisions (high 25-37 bits) - Worst is 31 bits: 2271/2231 (1.02x)
Testing collisions (low  32-bit) - Expected       1116.2, actual   1068 (0.96x)
Testing collisions (low  25-37 bits) - Worst is 37 bits: 42/34 (1.20x)


[[[ Diff 'Differential' Tests ]]]

Testing 8303632 up-to-5-bit differentials in 64-bit keys -> 64 bit hashes.
1000 reps, 8303632000 total tests, expecting 0.00 random collisions..........
0 total collisions, of which 0 single collisions were ignored

Testing 11017632 up-to-4-bit differentials in 128-bit keys -> 64 bit hashes.
1000 reps, 11017632000 total tests, expecting 0.00 random collisions..........
0 total collisions, of which 0 single collisions were ignored

Testing 2796416 up-to-3-bit differentials in 256-bit keys -> 64 bit hashes.
1000 reps, 2796416000 total tests, expecting 0.00 random collisions..........
0 total collisions, of which 0 single collisions were ignored


[[[ DiffDist 'Differential Distribution' Tests ]]]

Testing bit 0
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    473 (0.92x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 38/31 (1.19x)
Testing collisions (low  32-bit) - Expected        511.9, actual    515 (1.01x) (4)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 145/127 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 23 - 0.095%

Testing bit 1
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    503 (0.98x) (-8)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 42/31 (1.31x)
Testing collisions (low  32-bit) - Expected        511.9, actual    499 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 17-bit window at bit 23 - 0.077%

Testing bit 2
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    479 (0.94x)
Testing collisions (high 24-36 bits) - Worst is 28 bits: 8185/8170 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    538 (1.05x) (27)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 136/127 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 26 - 0.062%

Testing bit 3
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    490 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2047/2046 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    500 (0.98x)
Testing collisions (low  24-36 bits) - Worst is 28 bits: 8326/8170 (1.02x)
Testing distribution - Worst bias is the 18-bit window at bit 10 - 0.061%

Testing bit 4
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    515 (1.01x) (4)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 142/127 (1.11x)
Testing collisions (low  32-bit) - Expected        511.9, actual    463 (0.90x)
Testing collisions (low  24-36 bits) - Worst is 25 bits: 64159/64191 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 26 - 0.076%

Testing bit 5
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    501 (0.98x)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 135/127 (1.05x)
Testing collisions (low  32-bit) - Expected        511.9, actual    534 (1.04x) (23)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 70/63 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 58 - 0.071%

Testing bit 6
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    503 (0.98x) (-8)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 39/31 (1.22x)
Testing collisions (low  32-bit) - Expected        511.9, actual    537 (1.05x) (26)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 140/127 (1.09x)
Testing distribution - Worst bias is the 17-bit window at bit 28 - 0.054%

Testing bit 7
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    553 (1.08x) (42)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 278/255 (1.09x)
Testing collisions (low  32-bit) - Expected        511.9, actual    533 (1.04x) (22)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 142/127 (1.11x)
Testing distribution - Worst bias is the 17-bit window at bit 60 - 0.058%

Testing bit 8
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    519 (1.01x) (8)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 73/63 (1.14x)
Testing collisions (low  32-bit) - Expected        511.9, actual    519 (1.01x) (8)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 519/511 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 41 - 0.088%

Testing bit 9
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    527 (1.03x) (16)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 269/255 (1.05x)
Testing collisions (low  32-bit) - Expected        511.9, actual    546 (1.07x) (35)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 147/127 (1.15x)
Testing distribution - Worst bias is the 18-bit window at bit 29 - 0.063%

Testing bit 10
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 131/127 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    533 (1.04x) (22)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 50 - 0.081%

Testing bit 11
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing collisions (low  32-bit) - Expected        511.9, actual    492 (0.96x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 67/63 (1.05x)
Testing distribution - Worst bias is the 17-bit window at bit 45 - 0.064%

Testing bit 12
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    539 (1.05x) (28)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing collisions (low  32-bit) - Expected        511.9, actual    565 (1.10x) (54)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 79/63 (1.23x)
Testing distribution - Worst bias is the 18-bit window at bit 16 - 0.086%

Testing bit 13
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 517/511 (1.01x)
Testing collisions (low  32-bit) - Expected        511.9, actual    471 (0.92x)
Testing collisions (low  24-36 bits) - Worst is 24 bits: 125649/125777 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 29 - 0.071%

Testing bit 14
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    494 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 66/63 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    502 (0.98x) (-9)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 76/63 (1.19x)
Testing distribution - Worst bias is the 18-bit window at bit 54 - 0.104%

Testing bit 15
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    513 (1.00x) (2)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 513/511 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    513 (1.00x) (2)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2182/2046 (1.07x)
Testing distribution - Worst bias is the 18-bit window at bit  7 - 0.062%

Testing bit 16
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    521 (1.02x) (10)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2094/2046 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    497 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 132/127 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 48 - 0.103%

Testing bit 17
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    480 (0.94x)
Testing collisions (high 24-36 bits) - Worst is 25 bits: 64414/64191 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    480 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 69/63 (1.08x)
Testing distribution - Worst bias is the 18-bit window at bit 30 - 0.090%

Testing bit 18
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    513 (1.00x) (2)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 142/127 (1.11x)
Testing collisions (low  32-bit) - Expected        511.9, actual    535 (1.05x) (24)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 44/31 (1.38x)
Testing distribution - Worst bias is the 18-bit window at bit 41 - 0.072%

Testing bit 19
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    556 (1.09x) (45)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 144/127 (1.13x)
Testing collisions (low  32-bit) - Expected        511.9, actual    488 (0.95x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 72/63 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 45 - 0.055%

Testing bit 20
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    502 (0.98x) (-9)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 283/255 (1.11x)
Testing collisions (low  32-bit) - Expected        511.9, actual    548 (1.07x) (37)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 38/31 (1.19x)
Testing distribution - Worst bias is the 18-bit window at bit 35 - 0.061%

Testing bit 21
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    489 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 29 bits: 4170/4090 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    498 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 26 - 0.063%

Testing bit 22
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    494 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    535 (1.05x) (24)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 135/127 (1.05x)
Testing distribution - Worst bias is the 18-bit window at bit 35 - 0.111%

Testing bit 23
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    486 (0.95x)
Testing collisions (high 24-36 bits) - Worst is 28 bits: 8215/8170 (1.01x)
Testing collisions (low  32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 266/255 (1.04x)
Testing distribution - Worst bias is the 18-bit window at bit 42 - 0.096%

Testing bit 24
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    511 (1.00x)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 139/127 (1.09x)
Testing collisions (low  32-bit) - Expected        511.9, actual    488 (0.95x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 48 - 0.085%

Testing bit 25
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    484 (0.95x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 35/31 (1.09x)
Testing collisions (low  32-bit) - Expected        511.9, actual    532 (1.04x) (21)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing distribution - Worst bias is the 18-bit window at bit  2 - 0.072%

Testing bit 26
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    521 (1.02x) (10)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1046/1023 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 144/127 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 44 - 0.060%

Testing bit 27
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    521 (1.02x) (10)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing collisions (low  32-bit) - Expected        511.9, actual    513 (1.00x) (2)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 18-bit window at bit 12 - 0.067%

Testing bit 28
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    491 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    492 (0.96x)
Testing collisions (low  24-36 bits) - Worst is 31 bits: 1027/1023 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 44 - 0.072%

Testing bit 29
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    560 (1.09x) (49)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 153/127 (1.20x)
Testing collisions (low  32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 132/127 (1.03x)
Testing distribution - Worst bias is the 17-bit window at bit 59 - 0.067%

Testing bit 30
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    484 (0.95x)
Testing collisions (high 24-36 bits) - Worst is 29 bits: 4102/4090 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    516 (1.01x) (5)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 516/511 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 16 - 0.084%

Testing bit 31
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    549 (1.07x) (38)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 549/511 (1.07x)
Testing collisions (low  32-bit) - Expected        511.9, actual    509 (0.99x) (-2)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 144/127 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit  6 - 0.070%

Testing bit 32
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 138/127 (1.08x)
Testing collisions (low  32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 31 - 0.086%

Testing bit 33
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    536 (1.05x) (25)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 271/255 (1.06x)
Testing collisions (low  32-bit) - Expected        511.9, actual    568 (1.11x) (57)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 79/63 (1.23x)
Testing distribution - Worst bias is the 18-bit window at bit  9 - 0.075%

Testing bit 34
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    510 (1.00x) (-1)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing collisions (low  32-bit) - Expected        511.9, actual    512 (1.00x) (1)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 257/255 (1.00x)
Testing distribution - Worst bias is the 17-bit window at bit 24 - 0.047%

Testing bit 35
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 38/31 (1.19x)
Testing collisions (low  32-bit) - Expected        511.9, actual    530 (1.04x) (19)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 17-bit window at bit 28 - 0.061%

Testing bit 36
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    506 (0.99x) (-5)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 43/31 (1.34x)
Testing collisions (low  32-bit) - Expected        511.9, actual    501 (0.98x)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2062/2046 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 16 - 0.092%

Testing bit 37
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    509 (0.99x) (-2)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing collisions (low  32-bit) - Expected        511.9, actual    527 (1.03x) (16)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 140/127 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 42 - 0.085%

Testing bit 38
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    489 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    556 (1.09x) (45)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 556/511 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 50 - 0.045%

Testing bit 39
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    535 (1.05x) (24)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 73/63 (1.14x)
Testing collisions (low  32-bit) - Expected        511.9, actual    539 (1.05x) (28)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 282/255 (1.10x)
Testing distribution - Worst bias is the 18-bit window at bit 27 - 0.081%

Testing bit 40
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    512 (1.00x) (1)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing collisions (low  32-bit) - Expected        511.9, actual    459 (0.90x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 26 - 0.095%

Testing bit 41
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 135/127 (1.05x)
Testing collisions (low  32-bit) - Expected        511.9, actual    537 (1.05x) (26)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 270/255 (1.05x)
Testing distribution - Worst bias is the 18-bit window at bit 31 - 0.100%

Testing bit 42
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    515 (1.01x) (4)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1041/1023 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    477 (0.93x)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2094/2046 (1.02x)
Testing distribution - Worst bias is the 18-bit window at bit 50 - 0.080%

Testing bit 43
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    525 (1.03x) (14)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 41/31 (1.28x)
Testing collisions (low  32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 276/255 (1.08x)
Testing distribution - Worst bias is the 18-bit window at bit 20 - 0.091%

Testing bit 44
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 39/31 (1.22x)
Testing collisions (low  32-bit) - Expected        511.9, actual    483 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2095/2046 (1.02x)
Testing distribution - Worst bias is the 18-bit window at bit 48 - 0.088%

Testing bit 45
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    559 (1.09x) (48)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 39/31 (1.22x)
Testing collisions (low  32-bit) - Expected        511.9, actual    558 (1.09x) (47)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 38/31 (1.19x)
Testing distribution - Worst bias is the 18-bit window at bit  4 - 0.073%

Testing bit 46
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    528 (1.03x) (17)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 39/31 (1.22x)
Testing collisions (low  32-bit) - Expected        511.9, actual    533 (1.04x) (22)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 271/255 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 50 - 0.073%

Testing bit 47
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    472 (0.92x)
Testing collisions (high 24-36 bits) - Worst is 24 bits: 125700/125777 (1.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    519 (1.01x) (8)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing distribution - Worst bias is the 18-bit window at bit 11 - 0.084%

Testing bit 48
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    492 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 26 bits: 32601/32429 (1.01x)
Testing collisions (low  32-bit) - Expected        511.9, actual    512 (1.00x) (1)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 139/127 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 46 - 0.053%

Testing bit 49
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    502 (0.98x) (-9)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1046/1023 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    544 (1.06x) (33)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 74/63 (1.16x)
Testing distribution - Worst bias is the 18-bit window at bit  5 - 0.074%

Testing bit 50
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    558 (1.09x) (47)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 71/63 (1.11x)
Testing collisions (low  32-bit) - Expected        511.9, actual    483 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 66/63 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 56 - 0.061%

Testing bit 51
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    496 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 132/127 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    500 (0.98x)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 266/255 (1.04x)
Testing distribution - Worst bias is the 18-bit window at bit 62 - 0.079%

Testing bit 52
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    499 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 28 bits: 8213/8170 (1.01x)
Testing collisions (low  32-bit) - Expected        511.9, actual    477 (0.93x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 28 - 0.110%

Testing bit 53
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 35/31 (1.09x)
Testing collisions (low  32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 39/31 (1.22x)
Testing distribution - Worst bias is the 18-bit window at bit  7 - 0.073%

Testing bit 54
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    546 (1.07x) (35)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 283/255 (1.11x)
Testing collisions (low  32-bit) - Expected        511.9, actual    518 (1.01x) (7)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit  5 - 0.079%

Testing bit 55
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    497 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 270/255 (1.05x)
Testing collisions (low  32-bit) - Expected        511.9, actual    527 (1.03x) (16)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 42/31 (1.31x)
Testing distribution - Worst bias is the 18-bit window at bit 54 - 0.066%

Testing bit 56
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    495 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 29 bits: 4143/4090 (1.01x)
Testing collisions (low  32-bit) - Expected        511.9, actual    520 (1.02x) (9)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 41/31 (1.28x)
Testing distribution - Worst bias is the 18-bit window at bit 17 - 0.112%

Testing bit 57
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    515 (1.01x) (4)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 71/63 (1.11x)
Testing collisions (low  32-bit) - Expected        511.9, actual    539 (1.05x) (28)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 140/127 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 57 - 0.142%

Testing bit 58
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    496 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 131/127 (1.02x)
Testing collisions (low  32-bit) - Expected        511.9, actual    528 (1.03x) (17)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 269/255 (1.05x)
Testing distribution - Worst bias is the 18-bit window at bit 20 - 0.091%

Testing bit 59
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 41/31 (1.28x)
Testing collisions (low  32-bit) - Expected        511.9, actual    541 (1.06x) (30)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 146/127 (1.14x)
Testing distribution - Worst bias is the 18-bit window at bit  4 - 0.056%

Testing bit 60
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    497 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 82/63 (1.28x)
Testing collisions (low  32-bit) - Expected        511.9, actual    488 (0.95x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 15 - 0.083%

Testing bit 61
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    545 (1.06x) (34)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 545/511 (1.06x)
Testing collisions (low  32-bit) - Expected        511.9, actual    526 (1.03x) (15)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 526/511 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 12 - 0.083%

Testing bit 62
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    509 (0.99x) (-2)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 75/63 (1.17x)
Testing collisions (low  32-bit) - Expected        511.9, actual    530 (1.04x) (19)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 35/31 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 37 - 0.095%

Testing bit 63
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    525 (1.03x) (14)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 264/255 (1.03x)
Testing collisions (low  32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 68/63 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit  8 - 0.077%


[[[ MomentChi2 Tests ]]]

Analyze hashes produced from a serie of linearly increasing numbers of 32-bit, using a step of 2 ... 
Target values to approximate : 38918200.000000 - 273633.333333 
Popcount 1 stats : 38918998.920034 - 273638.558723
Popcount 0 stats : 38918770.531038 - 273645.996303
MomentChi2 for bits 1 :   1.16628 
MomentChi2 for bits 0 :  0.594771 

Derivative stats (transition from 2 consecutive values) : 
Popcount 1 stats : 38919257.619283 - 273628.012809
Popcount 0 stats : 38918472.697479 - 273650.071385
MomentChi2 for deriv b1 :   2.04392 
MomentChi2 for deriv b0 :  0.135878 

  Great 


[[[ Prng Tests ]]]

Generating 33554432 random numbers : 
Testing collisions ( 64-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected     130731.3, actual 130884 (1.00x) (153)
Testing collisions (high 28-44 bits) - Worst is 43 bits: 75/63 (1.17x)
Testing collisions (low  32-bit) - Expected     130731.3, actual 131211 (1.00x) (480)
Testing collisions (low  28-44 bits) - Worst is 43 bits: 68/63 (1.06x)

[[[ BadSeeds Tests ]]]

0x0 PASS


Input vcode 0x00000001, Output vcode 0x00000001, Result vcode 0x00000001
Verification value is 0x00000001 - Testing took 755.424002 seconds
------------------------------------------------------------
```

------------------------------------------------------------
### Stable Hash 128bit SMHasher Results

The following results were achieved on my personal computer when testing the 128bit variant of the facil.io Stable Hash (`fio_stable_hash128`).

```txt
------------------------------------------------------------
--- Testing Stable128 "facil.io Stable Hash 128bit" GOOD

[[[ Sanity Tests ]]]

Verification value 0x97D1AB52 ....... PASS
Running sanity check 1     .......... PASS
Running AppendedZeroesTest .......... PASS

[[[ Speed Tests ]]]

Bulk speed test - 262144-byte keys
Alignment  7 - 12.633 bytes/cycle - 36143.93 MiB/sec @ 3 ghz
Alignment  6 - 12.651 bytes/cycle - 36193.49 MiB/sec @ 3 ghz
Alignment  5 - 12.646 bytes/cycle - 36179.39 MiB/sec @ 3 ghz
Alignment  4 - 12.644 bytes/cycle - 36174.76 MiB/sec @ 3 ghz
Alignment  3 - 12.656 bytes/cycle - 36208.15 MiB/sec @ 3 ghz
Alignment  2 - 12.650 bytes/cycle - 36192.29 MiB/sec @ 3 ghz
Alignment  1 - 12.644 bytes/cycle - 36175.29 MiB/sec @ 3 ghz
Alignment  0 - 12.797 bytes/cycle - 36612.89 MiB/sec @ 3 ghz
Average      - 12.665 bytes/cycle - 36235.02 MiB/sec @ 3 ghz

Small key speed test -    1-byte keys -    21.00 cycles/hash
Small key speed test -    2-byte keys -    21.81 cycles/hash
Small key speed test -    3-byte keys -    22.97 cycles/hash
Small key speed test -    4-byte keys -    21.99 cycles/hash
Small key speed test -    5-byte keys -    22.69 cycles/hash
Small key speed test -    6-byte keys -    22.00 cycles/hash
Small key speed test -    7-byte keys -    23.92 cycles/hash
Small key speed test -    8-byte keys -    21.98 cycles/hash
Small key speed test -    9-byte keys -    21.99 cycles/hash
Small key speed test -   10-byte keys -    21.99 cycles/hash
Small key speed test -   11-byte keys -    21.99 cycles/hash
Small key speed test -   12-byte keys -    21.99 cycles/hash
Small key speed test -   13-byte keys -    21.99 cycles/hash
Small key speed test -   14-byte keys -    21.99 cycles/hash
Small key speed test -   15-byte keys -    21.99 cycles/hash
Small key speed test -   16-byte keys -    22.00 cycles/hash
Small key speed test -   17-byte keys -    23.98 cycles/hash
Small key speed test -   18-byte keys -    23.98 cycles/hash
Small key speed test -   19-byte keys -    23.98 cycles/hash
Small key speed test -   20-byte keys -    23.98 cycles/hash
Small key speed test -   21-byte keys -    23.97 cycles/hash
Small key speed test -   22-byte keys -    23.98 cycles/hash
Small key speed test -   23-byte keys -    23.98 cycles/hash
Small key speed test -   24-byte keys -    23.97 cycles/hash
Small key speed test -   25-byte keys -    23.97 cycles/hash
Small key speed test -   26-byte keys -    23.97 cycles/hash
Small key speed test -   27-byte keys -    23.97 cycles/hash
Small key speed test -   28-byte keys -    23.97 cycles/hash
Small key speed test -   29-byte keys -    23.97 cycles/hash
Small key speed test -   30-byte keys -    23.98 cycles/hash
Small key speed test -   31-byte keys -    24.18 cycles/hash
Average                                    23.037 cycles/hash

[[[ 'Hashmap' Speed Tests ]]]

std::unordered_map
Init std HashMapTest:     151.297 cycles/op (466569 inserts, 1% deletions)
Running std HashMapTest:  93.079 cycles/op (1.2 stdv, found 461903)

greg7mdp/parallel-hashmap
Init fast HashMapTest:    145.520 cycles/op (466569 inserts, 1% deletions)
Running fast HashMapTest: 91.867 cycles/op (0.7 stdv, found 461903)

facil.io HashMap
Init fast fio_map_s Test:    84.812 cycles/op (466569 inserts, 1% deletions)
Running fast fio_map_s Test: 48.436 cycles/op (0.4 stdv, found 461903)
 ....... PASS

[[[ Avalanche Tests ]]]

Testing   24-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.659333%
Testing   32-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.712000%
Testing   40-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.723333%
Testing   48-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.696667%
Testing   56-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.694000%
Testing   64-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.712000%
Testing   72-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.676000%
Testing   80-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.704667%
Testing   96-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.690667%
Testing  112-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.721333%
Testing  128-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.760667%
Testing  160-bit keys -> 128-bit hashes, 300000 reps.......... worst bias is 0.773333%

[[[ Keyset 'Sparse' Tests ]]]

Keyset 'Sparse' - 16-bit keys with up to 9 bits set - 50643 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          0.3, actual      0 (0.00x)
Testing collisions (high 19-25 bits) - Worst is 23 bits: 161/152 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          0.3, actual      0 (0.00x)
Testing collisions (low  19-25 bits) - Worst is 21 bits: 602/606 (0.99x)
Testing distribution - Worst bias is the 13-bit window at bit 31 - 0.586%

Keyset 'Sparse' - 24-bit keys with up to 8 bits set - 1271626 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        188.2, actual    190 (1.01x) (2)
Testing collisions (high 24-35 bits) - Worst is 32 bits: 190/188 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        188.2, actual    170 (0.90x)
Testing collisions (low  24-35 bits) - Worst is 26 bits: 12071/11972 (1.01x)
Testing distribution - Worst bias is the 17-bit window at bit 35 - 0.080%

Keyset 'Sparse' - 32-bit keys with up to 7 bits set - 4514873 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2372.2, actual   2355 (0.99x) (-17)
Testing collisions (high 25-38 bits) - Worst is 38 bits: 39/37 (1.05x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       2372.2, actual   2374 (1.00x) (2)
Testing collisions (low  25-38 bits) - Worst is 30 bits: 9492/9478 (1.00x)
Testing distribution - Worst bias is the 19-bit window at bit 74 - 0.065%

Keyset 'Sparse' - 40-bit keys with up to 6 bits set - 4598479 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2460.8, actual   2429 (0.99x) (-31)
Testing collisions (high 25-38 bits) - Worst is 29 bits: 19779/19637 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       2460.8, actual   2490 (1.01x) (30)
Testing collisions (low  25-38 bits) - Worst is 38 bits: 45/38 (1.17x)
Testing distribution - Worst bias is the 19-bit window at bit 82 - 0.051%

Keyset 'Sparse' - 48-bit keys with up to 6 bits set - 14196869 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      23437.8, actual  23636 (1.01x) (199)
Testing collisions (high 27-42 bits) - Worst is 42 bits: 24/22 (1.05x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected      23437.8, actual  23272 (0.99x) (-165)
Testing collisions (low  27-42 bits) - Worst is 30 bits: 93648/93442 (1.00x)
Testing distribution - Worst bias is the 20-bit window at bit 74 - 0.028%

Keyset 'Sparse' - 56-bit keys with up to 5 bits set - 4216423 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2069.0, actual   2045 (0.99x) (-23)
Testing collisions (high 25-38 bits) - Worst is 38 bits: 33/32 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       2069.0, actual   2042 (0.99x) (-26)
Testing collisions (low  25-38 bits) - Worst is 38 bits: 40/32 (1.24x)
Testing distribution - Worst bias is the 19-bit window at bit 96 - 0.061%

Keyset 'Sparse' - 64-bit keys with up to 5 bits set - 8303633 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       8021.7, actual   8104 (1.01x) (83)
Testing collisions (high 26-40 bits) - Worst is 40 bits: 39/31 (1.24x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       8021.7, actual   8038 (1.00x) (17)
Testing collisions (low  26-40 bits) - Worst is 31 bits: 16101/16033 (1.00x)
Testing distribution - Worst bias is the 20-bit window at bit 43 - 0.043%

Keyset 'Sparse' - 72-bit keys with up to 5 bits set - 15082603 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      26451.8, actual  26433 (1.00x) (-18)
Testing collisions (high 27-42 bits) - Worst is 41 bits: 55/51 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected      26451.8, actual  26434 (1.00x) (-17)
Testing collisions (low  27-42 bits) - Worst is 35 bits: 3324/3309 (1.00x)
Testing distribution - Worst bias is the 20-bit window at bit 26 - 0.028%

Keyset 'Sparse' - 96-bit keys with up to 4 bits set - 3469497 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1401.0, actual   1326 (0.95x)
Testing collisions (high 25-38 bits) - Worst is 38 bits: 22/21 (1.00x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       1401.0, actual   1406 (1.00x) (6)
Testing collisions (low  25-38 bits) - Worst is 37 bits: 60/43 (1.37x)
Testing distribution - Worst bias is the 19-bit window at bit 12 - 0.088%

Keyset 'Sparse' - 160-bit keys with up to 4 bits set - 26977161 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      84546.1, actual  84181 (1.00x) (-365)
Testing collisions (high 28-44 bits) - Worst is 43 bits: 54/41 (1.31x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected      84546.1, actual  84586 (1.00x) (40)
Testing collisions (low  28-44 bits) - Worst is 41 bits: 172/165 (1.04x)
Testing distribution - Worst bias is the 20-bit window at bit 39 - 0.017%

Keyset 'Sparse' - 256-bit keys with up to 3 bits set - 2796417 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        910.2, actual    920 (1.01x) (10)
Testing collisions (high 25-37 bits) - Worst is 36 bits: 65/56 (1.14x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        910.2, actual    925 (1.02x) (15)
Testing collisions (low  25-37 bits) - Worst is 33 bits: 473/455 (1.04x)
Testing distribution - Worst bias is the 19-bit window at bit 73 - 0.096%


[[[ Keyset 'Permutation' Tests ]]]

Combination Lowbits Tests:
Keyset 'Combination' - up to 7 blocks from a set of 8 - 2396744 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        668.6, actual    735 (1.10x) (67)
Testing collisions (high 24-37 bits) - Worst is 34 bits: 207/167 (1.24x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        668.6, actual    655 (0.98x)
Testing collisions (low  24-37 bits) - Worst is 34 bits: 173/167 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit  9 - 0.080%


Combination Highbits Tests
Keyset 'Combination' - up to 7 blocks from a set of 8 - 2396744 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        668.6, actual    681 (1.02x) (13)
Testing collisions (high 24-37 bits) - Worst is 37 bits: 26/20 (1.24x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        668.6, actual    691 (1.03x) (23)
Testing collisions (low  24-37 bits) - Worst is 33 bits: 362/334 (1.08x)
Testing distribution - Worst bias is the 18-bit window at bit 68 - 0.091%


Combination Hi-Lo Tests:
Keyset 'Combination' - up to 6 blocks from a set of 15 - 12204240 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      17322.9, actual  17433 (1.01x) (111)
Testing collisions (high 27-41 bits) - Worst is 34 bits: 4416/4333 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected      17322.9, actual  17214 (0.99x) (-108)
Testing collisions (low  27-41 bits) - Worst is 28 bits: 273805/273271 (1.00x)
Testing distribution - Worst bias is the 20-bit window at bit  3 - 0.030%


Combination 0x8000000 Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      9 (1.13x) (2)
Testing collisions (high 21-30 bits) - Worst is 28 bits: 135/127 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual     12 (1.50x) (5)
Testing collisions (low  21-30 bits) - Worst is 30 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 15-bit window at bit 48 - 0.268%


Combination 0x0000001 Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      9 (1.13x) (2)
Testing collisions (high 21-30 bits) - Worst is 26 bits: 537/511 (1.05x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual      8 (1.00x) (1)
Testing collisions (low  21-30 bits) - Worst is 29 bits: 73/63 (1.14x)
Testing distribution - Worst bias is the 15-bit window at bit 119 - 0.328%


Combination 0x800000000000000 Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      1 (0.13x)
Testing collisions (high 21-30 bits) - Worst is 24 bits: 2103/2037 (1.03x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual      8 (1.00x) (1)
Testing collisions (low  21-30 bits) - Worst is 30 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 15-bit window at bit 15 - 0.279%


Combination 0x000000000000001 Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      9 (1.13x) (2)
Testing collisions (high 21-30 bits) - Worst is 29 bits: 72/63 (1.13x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual     10 (1.25x) (3)
Testing collisions (low  21-30 bits) - Worst is 24 bits: 2041/2037 (1.00x)
Testing distribution - Worst bias is the 15-bit window at bit  2 - 0.203%


Combination 16-bytes [0-1] Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      3 (0.38x)
Testing collisions (high 21-30 bits) - Worst is 30 bits: 33/31 (1.03x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual     15 (1.88x) (8)
Testing collisions (low  21-30 bits) - Worst is 27 bits: 262/255 (1.02x)
Testing distribution - Worst bias is the 15-bit window at bit 21 - 0.203%


Combination 16-bytes [0-last] Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      7 (0.88x)
Testing collisions (high 21-30 bits) - Worst is 28 bits: 132/127 (1.03x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual      7 (0.88x)
Testing collisions (low  21-30 bits) - Worst is 25 bits: 1037/1021 (1.02x)
Testing distribution - Worst bias is the 15-bit window at bit 47 - 0.252%


Combination 32-bytes [0-1] Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual     13 (1.63x) (6)
Testing collisions (high 21-30 bits) - Worst is 30 bits: 41/31 (1.28x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual      6 (0.75x)
Testing collisions (low  21-30 bits) - Worst is 25 bits: 1034/1021 (1.01x)
Testing distribution - Worst bias is the 15-bit window at bit 43 - 0.259%


Combination 32-bytes [0-last] Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      5 (0.63x)
Testing collisions (high 21-30 bits) - Worst is 29 bits: 67/63 (1.05x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual      4 (0.50x)
Testing collisions (low  21-30 bits) - Worst is 29 bits: 70/63 (1.09x)
Testing distribution - Worst bias is the 15-bit window at bit 24 - 0.293%


Combination 64-bytes [0-1] Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      8 (1.00x) (1)
Testing collisions (high 21-30 bits) - Worst is 22 bits: 8144/8023 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual      3 (0.38x)
Testing collisions (low  21-30 bits) - Worst is 28 bits: 151/127 (1.18x)
Testing distribution - Worst bias is the 15-bit window at bit 74 - 0.329%


Combination 64-bytes [0-last] Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      8 (1.00x) (1)
Testing collisions (high 21-30 bits) - Worst is 27 bits: 286/255 (1.12x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual      4 (0.50x)
Testing collisions (low  21-30 bits) - Worst is 24 bits: 2098/2037 (1.03x)
Testing distribution - Worst bias is the 15-bit window at bit 118 - 0.211%


Combination 128-bytes [0-1] Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual      7 (0.88x)
Testing collisions (high 21-30 bits) - Worst is 26 bits: 553/511 (1.08x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual      7 (0.88x)
Testing collisions (low  21-30 bits) - Worst is 29 bits: 78/63 (1.22x)
Testing distribution - Worst bias is the 15-bit window at bit 57 - 0.323%


Combination 128-bytes [0-last] Tests:
Keyset 'Combination' - up to 17 blocks from a set of 2 - 262142 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          8.0, actual     11 (1.38x) (4)
Testing collisions (high 21-30 bits) - Worst is 28 bits: 136/127 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          8.0, actual     13 (1.63x) (6)
Testing collisions (low  21-30 bits) - Worst is 30 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 15-bit window at bit 125 - 0.223%


[[[ Keyset 'Window' Tests ]]]

Keyset 'Window' -  32-bit key,  25-bit window - 32 tests, 33554432 keys per test
Window at   0 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at   1 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at   2 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at   3 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at   4 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at   5 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at   6 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at   7 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at   8 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at   9 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  10 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  11 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  12 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  13 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  14 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  15 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  16 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  17 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  18 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  19 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  20 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  21 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  22 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  23 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  24 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  25 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  26 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  27 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  28 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  29 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  30 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  31 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Window at  32 - Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)

[[[ Keyset 'Cyclic' Tests ]]]

Keyset 'Cyclic' - 8 cycles of 16 bytes - 1000000 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    115 (0.99x) (-1)
Testing collisions (high 23-34 bits) - Worst is 33 bits: 63/58 (1.08x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    115 (0.99x) (-1)
Testing collisions (low  23-34 bits) - Worst is 29 bits: 930/930 (1.00x)
Testing distribution - Worst bias is the 17-bit window at bit 30 - 0.148%

Keyset 'Cyclic' - 8 cycles of 17 bytes - 1000000 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    115 (0.99x) (-1)
Testing collisions (high 23-34 bits) - Worst is 28 bits: 1937/1860 (1.04x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    123 (1.06x) (7)
Testing collisions (low  23-34 bits) - Worst is 31 bits: 264/232 (1.13x)
Testing distribution - Worst bias is the 17-bit window at bit 38 - 0.131%

Keyset 'Cyclic' - 8 cycles of 18 bytes - 1000000 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    104 (0.89x)
Testing collisions (high 23-34 bits) - Worst is 28 bits: 1911/1860 (1.03x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    113 (0.97x)
Testing collisions (low  23-34 bits) - Worst is 31 bits: 240/232 (1.03x)
Testing distribution - Worst bias is the 17-bit window at bit 58 - 0.123%

Keyset 'Cyclic' - 8 cycles of 19 bytes - 1000000 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    116 (1.00x)
Testing collisions (high 23-34 bits) - Worst is 34 bits: 33/29 (1.13x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    100 (0.86x)
Testing collisions (low  23-34 bits) - Worst is 28 bits: 1891/1860 (1.02x)
Testing distribution - Worst bias is the 17-bit window at bit  4 - 0.103%

Keyset 'Cyclic' - 8 cycles of 20 bytes - 1000000 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual     97 (0.83x)
Testing collisions (high 23-34 bits) - Worst is 28 bits: 1903/1860 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    125 (1.07x) (9)
Testing collisions (low  23-34 bits) - Worst is 34 bits: 36/29 (1.24x)
Testing distribution - Worst bias is the 17-bit window at bit 32 - 0.172%

Keyset 'Cyclic' - 8 cycles of 24 bytes - 1000000 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        116.4, actual    116 (1.00x)
Testing collisions (high 23-34 bits) - Worst is 34 bits: 32/29 (1.10x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        116.4, actual    131 (1.13x) (15)
Testing collisions (low  23-34 bits) - Worst is 34 bits: 38/29 (1.31x)
Testing distribution - Worst bias is the 17-bit window at bit 11 - 0.148%


[[[ Keyset 'TwoBytes' Tests ]]]

Keyset 'TwoBytes' - up-to-4-byte keys, 652545 total keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected         49.6, actual     50 (1.01x) (1)
Testing collisions (high 23-33 bits) - Worst is 33 bits: 27/24 (1.09x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected         49.6, actual     47 (0.95x)
Testing collisions (low  23-33 bits) - Worst is 33 bits: 27/24 (1.09x)
Testing distribution - Worst bias is the 16-bit window at bit 16 - 0.189%

Keyset 'TwoBytes' - up-to-8-byte keys, 5471025 total keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       3483.1, actual   3479 (1.00x) (-4)
Testing collisions (high 26-39 bits) - Worst is 35 bits: 484/435 (1.11x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       3483.1, actual   3493 (1.00x) (10)
Testing collisions (low  26-39 bits) - Worst is 39 bits: 38/27 (1.40x)
Testing distribution - Worst bias is the 20-bit window at bit 11 - 0.064%

Keyset 'TwoBytes' - up-to-12-byte keys, 18616785 total keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      40289.5, actual  39913 (0.99x) (-376)
Testing collisions (high 27-42 bits) - Worst is 41 bits: 81/78 (1.03x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected      40289.5, actual  40866 (1.01x) (577)
Testing collisions (low  27-42 bits) - Worst is 40 bits: 177/157 (1.12x)
Testing distribution - Worst bias is the 20-bit window at bit 18 - 0.021%


[[[ Keyset 'Text' Tests ]]]

Keyset 'Text' - keys of form "FooXXXXBar" - 14776336 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      25389.0, actual  25586 (1.01x) (197)
Testing collisions (high 27-42 bits) - Worst is 38 bits: 422/397 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected      25389.0, actual  25263 (1.00x) (-126)
Testing collisions (low  27-42 bits) - Worst is 38 bits: 425/397 (1.07x)
Testing distribution - Worst bias is the 20-bit window at bit 57 - 0.021%

Keyset 'Text' - keys of form "FooBarXXXX" - 14776336 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      25389.0, actual  25629 (1.01x) (240)
Testing collisions (high 27-42 bits) - Worst is 34 bits: 6431/6352 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected      25389.0, actual  25570 (1.01x) (181)
Testing collisions (low  27-42 bits) - Worst is 42 bits: 30/24 (1.21x)
Testing distribution - Worst bias is the 20-bit window at bit 13 - 0.031%

Keyset 'Text' - keys of form "XXXXFooBar" - 14776336 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      25389.0, actual  25481 (1.00x) (92)
Testing collisions (high 27-42 bits) - Worst is 40 bits: 111/99 (1.12x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected      25389.0, actual  25622 (1.01x) (233)
Testing collisions (low  27-42 bits) - Worst is 39 bits: 212/198 (1.07x)
Testing distribution - Worst bias is the 20-bit window at bit 110 - 0.026%

Keyset 'Words' - 4000000 random keys of len 6-16 from alnum charset
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1862.1, actual   1878 (1.01x) (16)
Testing collisions (high 25-38 bits) - Worst is 34 bits: 490/465 (1.05x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       1862.1, actual   1926 (1.03x) (64)
Testing collisions (low  25-38 bits) - Worst is 38 bits: 36/29 (1.24x)
Testing distribution - Worst bias is the 19-bit window at bit 30 - 0.067%

Keyset 'Words' - 4000000 random keys of len 6-16 from password charset
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1862.1, actual   1898 (1.02x) (36)
Testing collisions (high 25-38 bits) - Worst is 36 bits: 138/116 (1.19x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       1862.1, actual   1829 (0.98x) (-33)
Testing collisions (low  25-38 bits) - Worst is 25 bits: 228540/229220 (1.00x)
Testing distribution - Worst bias is the 19-bit window at bit 110 - 0.058%

Keyset 'Words' - 466569 dict words
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected         25.3, actual     34 (1.34x) (9)
Testing collisions (high 22-32 bits) - Worst is 32 bits: 34/25 (1.34x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected         25.3, actual     20 (0.79x)
Testing collisions (low  22-32 bits) - Worst is 26 bits: 1650/1618 (1.02x)
Testing distribution - Worst bias is the 16-bit window at bit  1 - 0.220%


[[[ Keyset 'Zeroes' Tests ]]]

Keyset 'Zeroes' - 204800 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected          4.9, actual      7 (1.43x) (3)
Testing collisions (high 21-29 bits) - Worst is 29 bits: 48/39 (1.23x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected          4.9, actual      4 (0.82x)
Testing collisions (low  21-29 bits) - Worst is 29 bits: 49/39 (1.25x)
Testing distribution - Worst bias is the 15-bit window at bit 29 - 0.313%


[[[ Keyset 'Seed' Tests ]]]

Keyset 'Seed' - 5000000 keys
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       2909.3, actual   2778 (0.95x)
Testing collisions (high 26-39 bits) - Worst is 37 bits: 96/90 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       2909.3, actual   2973 (1.02x) (64)
Testing collisions (low  26-39 bits) - Worst is 39 bits: 36/22 (1.58x)
Testing distribution - Worst bias is the 19-bit window at bit 79 - 0.055%


[[[ Keyset 'PerlinNoise' Tests ]]]

Testing 16777216 coordinates (L2) : 
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected      32725.4, actual  32905 (1.01x) (180)
Testing collisions (high 27-42 bits) - Worst is 40 bits: 142/127 (1.11x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected      32725.4, actual  32857 (1.00x) (132)
Testing collisions (low  27-42 bits) - Worst is 41 bits: 69/63 (1.08x)

Testing AV variant, 128 count with 4 spacing, 4-12:
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected       1116.2, actual   1102 (0.99x) (-14)
Testing collisions (high 25-37 bits) - Worst is 36 bits: 83/69 (1.19x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected       1116.2, actual   1068 (0.96x)
Testing collisions (low  25-37 bits) - Worst is 37 bits: 42/34 (1.20x)


[[[ Diff 'Differential' Tests ]]]

Testing 8303632 up-to-5-bit differentials in 64-bit keys -> 128 bit hashes.
1000 reps, 8303632000 total tests, expecting 0.00 random collisions..........
0 total collisions, of which 0 single collisions were ignored

Testing 11017632 up-to-4-bit differentials in 128-bit keys -> 128 bit hashes.
1000 reps, 11017632000 total tests, expecting 0.00 random collisions..........
0 total collisions, of which 0 single collisions were ignored

Testing 2796416 up-to-3-bit differentials in 256-bit keys -> 128 bit hashes.
1000 reps, 2796416000 total tests, expecting 0.00 random collisions..........
0 total collisions, of which 0 single collisions were ignored


[[[ DiffDist 'Differential Distribution' Tests ]]]

Testing bit 0
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    544 (1.06x) (33)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 71/63 (1.11x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    515 (1.01x) (4)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 145/127 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 79 - 0.123%

Testing bit 1
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    515 (1.01x) (4)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2065/2046 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    499 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 18-bit window at bit 99 - 0.092%

Testing bit 2
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    442 (0.86x)
Testing collisions (high 24-36 bits) - Worst is 29 bits: 4169/4090 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    538 (1.05x) (27)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 136/127 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 107 - 0.080%

Testing bit 3
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    515 (1.01x) (4)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 266/255 (1.04x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    500 (0.98x)
Testing collisions (low  24-36 bits) - Worst is 28 bits: 8326/8170 (1.02x)
Testing distribution - Worst bias is the 18-bit window at bit 113 - 0.077%

Testing bit 4
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    501 (0.98x)
Testing collisions (high 24-36 bits) - Worst is 29 bits: 4107/4090 (1.00x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    463 (0.90x)
Testing collisions (low  24-36 bits) - Worst is 25 bits: 64159/64191 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 26 - 0.076%

Testing bit 5
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    482 (0.94x)
Testing collisions (high 24-36 bits) - Worst is 28 bits: 8318/8170 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    534 (1.04x) (23)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 70/63 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 82 - 0.070%

Testing bit 6
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    518 (1.01x) (7)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 73/63 (1.14x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    537 (1.05x) (26)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 140/127 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 127 - 0.104%

Testing bit 7
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    483 (0.94x)
Testing collisions (high 24-36 bits) - Worst is 27 bits: 16399/16298 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    533 (1.04x) (22)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 142/127 (1.11x)
Testing distribution - Worst bias is the 18-bit window at bit 106 - 0.081%

Testing bit 8
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 276/255 (1.08x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    519 (1.01x) (8)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 519/511 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 41 - 0.088%

Testing bit 9
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    565 (1.10x) (54)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 148/127 (1.16x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    546 (1.07x) (35)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 147/127 (1.15x)
Testing distribution - Worst bias is the 18-bit window at bit 123 - 0.110%

Testing bit 10
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    533 (1.04x) (22)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 89 - 0.083%

Testing bit 11
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    502 (0.98x) (-9)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 75/63 (1.17x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    492 (0.96x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 67/63 (1.05x)
Testing distribution - Worst bias is the 18-bit window at bit 68 - 0.087%

Testing bit 12
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    499 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 79/63 (1.23x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    565 (1.10x) (54)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 79/63 (1.23x)
Testing distribution - Worst bias is the 18-bit window at bit 126 - 0.095%

Testing bit 13
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    498 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 68/63 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    471 (0.92x)
Testing collisions (low  24-36 bits) - Worst is 24 bits: 125649/125777 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 119 - 0.082%

Testing bit 14
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    532 (1.04x) (21)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 83/63 (1.30x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    502 (0.98x) (-9)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 76/63 (1.19x)
Testing distribution - Worst bias is the 18-bit window at bit 53 - 0.073%

Testing bit 15
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    501 (0.98x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    513 (1.00x) (2)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2182/2046 (1.07x)
Testing distribution - Worst bias is the 18-bit window at bit 126 - 0.104%

Testing bit 16
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 277/255 (1.08x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    497 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 132/127 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 108 - 0.101%

Testing bit 17
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    520 (1.02x) (9)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    480 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 69/63 (1.08x)
Testing distribution - Worst bias is the 18-bit window at bit 30 - 0.090%

Testing bit 18
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 73/63 (1.14x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    535 (1.05x) (24)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 44/31 (1.38x)
Testing distribution - Worst bias is the 18-bit window at bit 81 - 0.101%

Testing bit 19
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    563 (1.10x) (52)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    488 (0.95x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 72/63 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 54 - 0.106%

Testing bit 20
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    507 (0.99x) (-4)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1041/1023 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    548 (1.07x) (37)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 38/31 (1.19x)
Testing distribution - Worst bias is the 18-bit window at bit 61 - 0.071%

Testing bit 21
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    511 (1.00x)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 271/255 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    498 (0.97x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 121 - 0.092%

Testing bit 22
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    507 (0.99x) (-4)
Testing collisions (high 24-36 bits) - Worst is 25 bits: 63947/64191 (1.00x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    535 (1.05x) (24)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 135/127 (1.05x)
Testing distribution - Worst bias is the 18-bit window at bit 35 - 0.111%

Testing bit 23
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    555 (1.08x) (44)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 555/511 (1.08x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 266/255 (1.04x)
Testing distribution - Worst bias is the 18-bit window at bit 42 - 0.096%

Testing bit 24
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    486 (0.95x)
Testing collisions (high 24-36 bits) - Worst is 25 bits: 64468/64191 (1.00x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    488 (0.95x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 79 - 0.109%

Testing bit 25
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    493 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    532 (1.04x) (21)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing distribution - Worst bias is the 18-bit window at bit 53 - 0.081%

Testing bit 26
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    485 (0.95x)
Testing collisions (high 24-36 bits) - Worst is 27 bits: 16451/16298 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    524 (1.02x) (13)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 144/127 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 75 - 0.093%

Testing bit 27
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    516 (1.01x) (5)
Testing collisions (high 24-36 bits) - Worst is 27 bits: 16466/16298 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    513 (1.00x) (2)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 40/31 (1.25x)
Testing distribution - Worst bias is the 18-bit window at bit 107 - 0.098%

Testing bit 28
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    514 (1.00x) (3)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1060/1023 (1.04x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    492 (0.96x)
Testing collisions (low  24-36 bits) - Worst is 31 bits: 1027/1023 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 80 - 0.107%

Testing bit 29
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    507 (0.99x) (-4)
Testing collisions (high 24-36 bits) - Worst is 24 bits: 126291/125777 (1.00x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 132/127 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 126 - 0.088%

Testing bit 30
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    477 (0.93x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 35/31 (1.09x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    516 (1.01x) (5)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 516/511 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 16 - 0.084%

Testing bit 31
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    525 (1.03x) (14)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 140/127 (1.09x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    509 (0.99x) (-2)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 144/127 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit  6 - 0.070%

Testing bit 32
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    499 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2072/2046 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 31 - 0.086%

Testing bit 33
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    521 (1.02x) (10)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 147/127 (1.15x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    568 (1.11x) (57)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 79/63 (1.23x)
Testing distribution - Worst bias is the 18-bit window at bit 99 - 0.097%

Testing bit 34
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    495 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 41/31 (1.28x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    512 (1.00x) (1)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 257/255 (1.00x)
Testing distribution - Worst bias is the 18-bit window at bit 79 - 0.087%

Testing bit 35
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    508 (0.99x) (-3)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 269/255 (1.05x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    530 (1.04x) (19)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 96 - 0.066%

Testing bit 36
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    501 (0.98x)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 77/63 (1.20x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    501 (0.98x)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2062/2046 (1.01x)
Testing distribution - Worst bias is the 18-bit window at bit 53 - 0.121%

Testing bit 37
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    502 (0.98x) (-9)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    527 (1.03x) (16)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 140/127 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 121 - 0.093%

Testing bit 38
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    508 (0.99x) (-3)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1045/1023 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    556 (1.09x) (45)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 556/511 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 123 - 0.077%

Testing bit 39
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    470 (0.92x)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 83/63 (1.30x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    539 (1.05x) (28)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 282/255 (1.10x)
Testing distribution - Worst bias is the 18-bit window at bit 27 - 0.081%

Testing bit 40
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    493 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 27 bits: 16404/16298 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    459 (0.90x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 26 - 0.095%

Testing bit 41
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    505 (0.99x) (-6)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 136/127 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    537 (1.05x) (26)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 270/255 (1.05x)
Testing distribution - Worst bias is the 18-bit window at bit 31 - 0.100%

Testing bit 42
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    529 (1.03x) (18)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 42/31 (1.31x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    477 (0.93x)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2094/2046 (1.02x)
Testing distribution - Worst bias is the 18-bit window at bit 99 - 0.076%

Testing bit 43
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    475 (0.93x)
Testing collisions (high 24-36 bits) - Worst is 24 bits: 125780/125777 (1.00x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 276/255 (1.08x)
Testing distribution - Worst bias is the 18-bit window at bit 20 - 0.091%

Testing bit 44
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    508 (0.99x) (-3)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    483 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 30 bits: 2095/2046 (1.02x)
Testing distribution - Worst bias is the 18-bit window at bit 19 - 0.068%

Testing bit 45
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 271/255 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    558 (1.09x) (47)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 38/31 (1.19x)
Testing distribution - Worst bias is the 18-bit window at bit 117 - 0.078%

Testing bit 46
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1087/1023 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    533 (1.04x) (22)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 271/255 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 51 - 0.104%

Testing bit 47
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    491 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2085/2046 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    519 (1.01x) (8)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing distribution - Worst bias is the 18-bit window at bit 82 - 0.085%

Testing bit 48
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    523 (1.02x) (12)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 78/63 (1.22x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    512 (1.00x) (1)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 139/127 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 73 - 0.081%

Testing bit 49
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    527 (1.03x) (16)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2143/2046 (1.05x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    544 (1.06x) (33)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 74/63 (1.16x)
Testing distribution - Worst bias is the 18-bit window at bit 66 - 0.099%

Testing bit 50
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    494 (0.97x)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 41/31 (1.28x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    483 (0.94x)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 66/63 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 119 - 0.103%

Testing bit 51
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    502 (0.98x) (-9)
Testing collisions (high 24-36 bits) - Worst is 35 bits: 75/63 (1.17x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    500 (0.98x)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 266/255 (1.04x)
Testing distribution - Worst bias is the 18-bit window at bit 78 - 0.090%

Testing bit 52
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    504 (0.98x) (-7)
Testing collisions (high 24-36 bits) - Worst is 30 bits: 2076/2046 (1.01x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    477 (0.93x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 36/31 (1.13x)
Testing distribution - Worst bias is the 18-bit window at bit 28 - 0.110%

Testing bit 53
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    517 (1.01x) (6)
Testing collisions (high 24-36 bits) - Worst is 34 bits: 138/127 (1.08x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 39/31 (1.22x)
Testing distribution - Worst bias is the 18-bit window at bit  7 - 0.073%

Testing bit 54
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    518 (1.01x) (7)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    518 (1.01x) (7)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 33/31 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 71 - 0.098%

Testing bit 55
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    527 (1.03x) (16)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 42/31 (1.31x)
Testing distribution - Worst bias is the 18-bit window at bit 87 - 0.087%

Testing bit 56
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    546 (1.07x) (35)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 45/31 (1.41x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    520 (1.02x) (9)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 41/31 (1.28x)
Testing distribution - Worst bias is the 18-bit window at bit 17 - 0.112%

Testing bit 57
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    475 (0.93x)
Testing collisions (high 24-36 bits) - Worst is 24 bits: 126113/125777 (1.00x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    539 (1.05x) (28)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 140/127 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 49 - 0.089%

Testing bit 58
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    557 (1.09x) (46)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 37/31 (1.16x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    528 (1.03x) (17)
Testing collisions (low  24-36 bits) - Worst is 33 bits: 269/255 (1.05x)
Testing distribution - Worst bias is the 18-bit window at bit 20 - 0.091%

Testing bit 59
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    518 (1.01x) (7)
Testing collisions (high 24-36 bits) - Worst is 31 bits: 1082/1023 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    541 (1.06x) (30)
Testing collisions (low  24-36 bits) - Worst is 34 bits: 146/127 (1.14x)
Testing distribution - Worst bias is the 18-bit window at bit 55 - 0.065%

Testing bit 60
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    508 (0.99x) (-3)
Testing collisions (high 24-36 bits) - Worst is 33 bits: 260/255 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    488 (0.95x)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 34/31 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit 15 - 0.083%

Testing bit 61
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    521 (1.02x) (10)
Testing collisions (high 24-36 bits) - Worst is 36 bits: 48/31 (1.50x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    526 (1.03x) (15)
Testing collisions (low  24-36 bits) - Worst is 32 bits: 526/511 (1.03x)
Testing distribution - Worst bias is the 18-bit window at bit 12 - 0.083%

Testing bit 62
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    492 (0.96x)
Testing collisions (high 24-36 bits) - Worst is 24 bits: 126044/125777 (1.00x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    530 (1.04x) (19)
Testing collisions (low  24-36 bits) - Worst is 36 bits: 35/31 (1.09x)
Testing distribution - Worst bias is the 18-bit window at bit 37 - 0.095%

Testing bit 63
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected        511.9, actual    543 (1.06x) (32)
Testing collisions (high 24-36 bits) - Worst is 32 bits: 543/511 (1.06x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected        511.9, actual    522 (1.02x) (11)
Testing collisions (low  24-36 bits) - Worst is 35 bits: 68/63 (1.06x)
Testing distribution - Worst bias is the 18-bit window at bit  8 - 0.077%


[[[ MomentChi2 Tests ]]]

Analyze hashes produced from a serie of linearly increasing numbers of 32-bit, using a step of 2 ... 
Target values to approximate : 38918200.000000 - 273633.333333 
Popcount 1 stats : 38918998.920034 - 273638.558723
Popcount 0 stats : 38918770.531038 - 273645.996303
MomentChi2 for bits 1 :   1.16628 
MomentChi2 for bits 0 :  0.594771 

Derivative stats (transition from 2 consecutive values) : 
Popcount 1 stats : 38919257.619283 - 273628.012809
Popcount 0 stats : 38918472.697479 - 273650.071385
MomentChi2 for deriv b1 :   2.04392 
MomentChi2 for deriv b0 :  0.135878 

  Great 


[[[ Prng Tests ]]]

Generating 33554432 random numbers : 
Testing collisions (128-bit) - Expected    0.0, actual      0 (0.00x)
Testing collisions (high 64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (high 32-bit) - Expected     130731.3, actual 131029 (1.00x) (298)
Testing collisions (high 28-44 bits) - Worst is 37 bits: 4158/4095 (1.02x)
Testing collisions (low  64-bit) - Expected          0.0, actual      0 (0.00x)
Testing collisions (low  32-bit) - Expected     130731.3, actual 130672 (1.00x) (-59)
Testing collisions (low  28-44 bits) - Worst is 43 bits: 85/63 (1.33x)

[[[ BadSeeds Tests ]]]

0x0 PASS


Input vcode 0x00000001, Output vcode 0x00000001, Result vcode 0x00000001
Verification value is 0x00000001 - Testing took 1063.513065 seconds
------------------------------------------------------------
```

------------------------------------------------------------
## Pseudo-Random Function Testing

Testing the Pseudo-Random Number Generator (PRNG) Functions isn't somewhat of a chore, as a complete test could take a week or so to complete and my laptop wasn't designed for such long running intensive tasks.

Tests were conducted by utilizing [PractRand](https://pracrand.sourceforge.net) as well as some tests adopted from the [xoshiro](http://xoshiro.di.unimi.it/hwd.php) testing code, running both up to 2TB of random data (which took about 1 night for PractRand).

Results are as follows:

------------------------------------------------------------
### `fio_rand`

The following are the tests for the core  `fio_rand64` and `fio_rand_bytes` functions provided when using `FIO_RAND`.

**The `PractRand` results**:

```txt
# ./tmp/rnd -p fio | RNG_test stdin
RNG_test using PractRand version 0.95
RNG = RNG_stdin, seed = unknown
test set = core, folding = standard(unknown format)

rng=RNG_stdin, seed=unknown
length= 256 megabytes (2^28 bytes), time= 3.0 seconds
  no anomalies in 217 test result(s)

rng=RNG_stdin, seed=unknown
length= 512 megabytes (2^29 bytes), time= 6.5 seconds
  no anomalies in 232 test result(s)

rng=RNG_stdin, seed=unknown
length= 1 gigabyte (2^30 bytes), time= 13.0 seconds
  no anomalies in 251 test result(s)

rng=RNG_stdin, seed=unknown
length= 2 gigabytes (2^31 bytes), time= 25.2 seconds
  no anomalies in 269 test result(s)

rng=RNG_stdin, seed=unknown
length= 4 gigabytes (2^32 bytes), time= 48.2 seconds
  no anomalies in 283 test result(s)

rng=RNG_stdin, seed=unknown
length= 8 gigabytes (2^33 bytes), time= 96.5 seconds
  Test Name                         Raw       Processed     Evaluation
  BCFN(2+7,13-2,T)                  R=  -9.1  p =1-3.9e-5   unusual          
  ...and 299 test result(s) without anomalies

rng=RNG_stdin, seed=unknown
length= 16 gigabytes (2^34 bytes), time= 190 seconds
  no anomalies in 315 test result(s)

rng=RNG_stdin, seed=unknown
length= 32 gigabytes (2^35 bytes), time= 376 seconds
  no anomalies in 328 test result(s)

rng=RNG_stdin, seed=unknown
length= 64 gigabytes (2^36 bytes), time= 752 seconds
  no anomalies in 344 test result(s)

rng=RNG_stdin, seed=unknown
length= 128 gigabytes (2^37 bytes), time= 1498 seconds
  no anomalies in 359 test result(s)

rng=RNG_stdin, seed=unknown
length= 256 gigabytes (2^38 bytes), time= 2978 seconds
  no anomalies in 372 test result(s)

rng=RNG_stdin, seed=unknown
length= 512 gigabytes (2^39 bytes), time= 9897 seconds
  no anomalies in 387 test result(s)

rng=RNG_stdin, seed=unknown
length= 1 terabyte (2^40 bytes), time= 21004 seconds
  no anomalies in 401 test result(s)

rng=RNG_stdin, seed=unknown
length= 2 terabytes (2^41 bytes), time= 43108 seconds
  no anomalies in 413 test result(s)
```

**The tests adopted from the `xoshiro` code base**:


```txt
# ./tmp/random fio
mix3 extreme = 1.80533 (sig = 00100000) weight 1 (16), p-value = 0.692
mix3 extreme = 2.35650 (sig = 00000102) weight 2 (112), p-value = 0.876
mix3 extreme = 3.33991 (sig = 20002100) weight 3 (448), p-value = 0.313
mix3 extreme = 4.30088 (sig = 02210200) weight 4 (1120), p-value = 0.0189
mix3 extreme = 4.03228 (sig = 01002212) weight >=5 (4864), p-value = 0.236
bits per word = 64 (analyzing bits); min category p-value = 0.0189

processed 1.11e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:21 2025

p = 0.0909
------

mix3 extreme = 1.84047 (sig = 20000000) weight 1 (16), p-value = 0.663
mix3 extreme = 3.12258 (sig = 00000102) weight 2 (112), p-value = 0.182
mix3 extreme = 3.47031 (sig = 20002100) weight 3 (448), p-value = 0.208
mix3 extreme = 3.61471 (sig = 12000101) weight 4 (1120), p-value = 0.286
mix3 extreme = 3.76213 (sig = 10111202) weight >=5 (4864), p-value = 0.559
bits per word = 64 (analyzing bits); min category p-value = 0.182

processed 1.29e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:21 2025

p = 0.634
------

mix3 extreme = 1.65805 (sig = 00000002) weight 1 (16), p-value = 0.806
mix3 extreme = 3.09464 (sig = 00000102) weight 2 (112), p-value = 0.198
mix3 extreme = 3.53210 (sig = 00102010) weight 3 (448), p-value = 0.169
mix3 extreme = 3.35900 (sig = 02210200) weight 4 (1120), p-value = 0.584
mix3 extreme = 3.58826 (sig = 02011101) weight >=5 (4864), p-value = 0.802
bits per word = 64 (analyzing bits); min category p-value = 0.169

processed 1.66e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:21 2025

p = 0.603
------

mix3 extreme = 1.75332 (sig = 00000001) weight 1 (16), p-value = 0.735
mix3 extreme = 2.97345 (sig = 00000102) weight 2 (112), p-value = 0.281
mix3 extreme = 3.21121 (sig = 20002100) weight 3 (448), p-value = 0.447
mix3 extreme = 2.99207 (sig = 10102020) weight 4 (1120), p-value = 0.955
mix3 extreme = 3.53616 (sig = 21120101) weight >=5 (4864), p-value = 0.861
bits per word = 64 (analyzing bits); min category p-value = 0.281

processed 1.85e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:21 2025

p = 0.808
------

mix3 extreme = 1.83225 (sig = 00000100) weight 1 (16), p-value = 0.67
mix3 extreme = 3.10258 (sig = 00000102) weight 2 (112), p-value = 0.194
mix3 extreme = 3.03291 (sig = 20002100) weight 3 (448), p-value = 0.663
mix3 extreme = 3.09549 (sig = 10200022) weight 4 (1120), p-value = 0.89
mix3 extreme = 3.76428 (sig = 02011101) weight >=5 (4864), p-value = 0.556
bits per word = 64 (analyzing bits); min category p-value = 0.194

processed 2.03e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:21 2025

p = 0.659
------

mix3 extreme = 1.84289 (sig = 00000100) weight 1 (16), p-value = 0.661
mix3 extreme = 3.57529 (sig = 00000102) weight 2 (112), p-value = 0.0384
mix3 extreme = 3.46502 (sig = 20002010) weight 3 (448), p-value = 0.211
mix3 extreme = 3.25764 (sig = 00220201) weight 4 (1120), p-value = 0.716
mix3 extreme = 4.19914 (sig = 11222212) weight >=5 (4864), p-value = 0.122
bits per word = 64 (analyzing bits); min category p-value = 0.0384

processed 2.59e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:21 2025

p = 0.178
------

mix3 extreme = 2.19203 (sig = 00000002) weight 1 (16), p-value = 0.369
mix3 extreme = 3.04970 (sig = 00000102) weight 2 (112), p-value = 0.227
mix3 extreme = 3.44778 (sig = 20002010) weight 3 (448), p-value = 0.224
mix3 extreme = 3.66866 (sig = 22110000) weight 4 (1120), p-value = 0.239
mix3 extreme = 3.58667 (sig = 02021021) weight >=5 (4864), p-value = 0.804
bits per word = 64 (analyzing bits); min category p-value = 0.224

processed 3.14e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:21 2025

p = 0.718
------

mix3 extreme = 2.51064 (sig = 00000002) weight 1 (16), p-value = 0.176
mix3 extreme = 2.57871 (sig = 20010000) weight 2 (112), p-value = 0.672
mix3 extreme = 3.85273 (sig = 20002010) weight 3 (448), p-value = 0.051
mix3 extreme = 3.18862 (sig = 01101010) weight 4 (1120), p-value = 0.799
mix3 extreme = 3.60146 (sig = 22121010) weight >=5 (4864), p-value = 0.785
bits per word = 64 (analyzing bits); min category p-value = 0.051

processed 4.07e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:21 2025

p = 0.23
------

mix3 extreme = 1.89834 (sig = 00000002) weight 1 (16), p-value = 0.613
mix3 extreme = 2.27677 (sig = 00100200) weight 2 (112), p-value = 0.924
mix3 extreme = 3.52766 (sig = 20000022) weight 3 (448), p-value = 0.171
mix3 extreme = 3.50133 (sig = 11000102) weight 4 (1120), p-value = 0.405
mix3 extreme = 3.47778 (sig = 11121011) weight >=5 (4864), p-value = 0.915
bits per word = 64 (analyzing bits); min category p-value = 0.171

processed 5.18e+08 bytes in 1 seconds (0.5177 GB/s, 1.864 TB/h). Fri Feb 21 23:22:22 2025

p = 0.609
------

mix3 extreme = 1.99814 (sig = 00000001) weight 1 (16), p-value = 0.527
mix3 extreme = 2.43362 (sig = 00100200) weight 2 (112), p-value = 0.815
mix3 extreme = 3.46309 (sig = 20002010) weight 3 (448), p-value = 0.213
mix3 extreme = 3.67295 (sig = 20110001) weight 4 (1120), p-value = 0.236
mix3 extreme = 3.50848 (sig = 00222210) weight >=5 (4864), p-value = 0.888
bits per word = 64 (analyzing bits); min category p-value = 0.213

processed 6.1e+08 bytes in 1 seconds (0.6101 GB/s, 2.196 TB/h). Fri Feb 21 23:22:22 2025

p = 0.698
------

mix3 extreme = 1.78022 (sig = 00000002) weight 1 (16), p-value = 0.713
mix3 extreme = 2.64570 (sig = 10000001) weight 2 (112), p-value = 0.6
mix3 extreme = 3.55344 (sig = 20002010) weight 3 (448), p-value = 0.157
mix3 extreme = 3.15383 (sig = 11000102) weight 4 (1120), p-value = 0.836
mix3 extreme = 4.03858 (sig = 11121011) weight >=5 (4864), p-value = 0.23
bits per word = 64 (analyzing bits); min category p-value = 0.157

processed 7.03e+08 bytes in 1 seconds (0.7026 GB/s, 2.529 TB/h). Fri Feb 21 23:22:22 2025

p = 0.573
------

mix3 extreme = 1.83987 (sig = 00000002) weight 1 (16), p-value = 0.663
mix3 extreme = 2.43194 (sig = 00000110) weight 2 (112), p-value = 0.816
mix3 extreme = 3.40962 (sig = 20000022) weight 3 (448), p-value = 0.253
mix3 extreme = 3.02964 (sig = 02012200) weight 4 (1120), p-value = 0.936
mix3 extreme = 3.75207 (sig = 20222201) weight >=5 (4864), p-value = 0.574
bits per word = 64 (analyzing bits); min category p-value = 0.253

processed 8.5e+08 bytes in 1 seconds (0.8505 GB/s, 3.062 TB/h). Fri Feb 21 23:22:22 2025

p = 0.767
------

mix3 extreme = 2.03491 (sig = 00000002) weight 1 (16), p-value = 0.496
mix3 extreme = 2.48334 (sig = 00001002) weight 2 (112), p-value = 0.769
mix3 extreme = 3.15042 (sig = 10200002) weight 3 (448), p-value = 0.519
mix3 extreme = 3.19215 (sig = 21200001) weight 4 (1120), p-value = 0.795
mix3 extreme = 3.68390 (sig = 20222201) weight >=5 (4864), p-value = 0.673
bits per word = 64 (analyzing bits); min category p-value = 0.496

processed 1.02e+09 bytes in 1 seconds (1.017 GB/s, 3.661 TB/h). Fri Feb 21 23:22:22 2025

p = 0.967
------

mix3 extreme = 1.98184 (sig = 00000001) weight 1 (16), p-value = 0.541
mix3 extreme = 2.49397 (sig = 01001000) weight 2 (112), p-value = 0.759
mix3 extreme = 2.98386 (sig = 02000011) weight 3 (448), p-value = 0.721
mix3 extreme = 3.29022 (sig = 20200220) weight 4 (1120), p-value = 0.674
mix3 extreme = 4.30382 (sig = 11122011) weight >=5 (4864), p-value = 0.0784
bits per word = 64 (analyzing bits); min category p-value = 0.0784

processed 1.26e+09 bytes in 1 seconds (1.257 GB/s, 4.526 TB/h). Fri Feb 21 23:22:22 2025

p = 0.335
------

mix3 extreme = 2.33599 (sig = 00000001) weight 1 (16), p-value = 0.27
mix3 extreme = 2.45643 (sig = 00000110) weight 2 (112), p-value = 0.795
mix3 extreme = 2.84156 (sig = 00022100) weight 3 (448), p-value = 0.867
mix3 extreme = 3.61210 (sig = 01100202) weight 4 (1120), p-value = 0.288
mix3 extreme = 3.51573 (sig = 21101210) weight >=5 (4864), p-value = 0.882
bits per word = 64 (analyzing bits); min category p-value = 0.27

processed 1.52e+09 bytes in 1 seconds (1.516 GB/s, 5.458 TB/h). Fri Feb 21 23:22:22 2025

p = 0.793
------

mix3 extreme = 2.57590 (sig = 00000001) weight 1 (16), p-value = 0.149
mix3 extreme = 2.18056 (sig = 10000002) weight 2 (112), p-value = 0.964
mix3 extreme = 2.94943 (sig = 20102000) weight 3 (448), p-value = 0.76
mix3 extreme = 3.39387 (sig = 11010002) weight 4 (1120), p-value = 0.538
mix3 extreme = 3.85744 (sig = 22210212) weight >=5 (4864), p-value = 0.427
bits per word = 64 (analyzing bits); min category p-value = 0.149

processed 1.76e+09 bytes in 1 seconds (1.756 GB/s, 6.323 TB/h). Fri Feb 21 23:22:22 2025

p = 0.552
------

mix3 extreme = 2.64879 (sig = 00000001) weight 1 (16), p-value = 0.122
mix3 extreme = 2.69778 (sig = 10000002) weight 2 (112), p-value = 0.544
mix3 extreme = 3.15759 (sig = 20102000) weight 3 (448), p-value = 0.51
mix3 extreme = 3.41194 (sig = 11010002) weight 4 (1120), p-value = 0.515
mix3 extreme = 4.19052 (sig = 11122011) weight >=5 (4864), p-value = 0.127
bits per word = 64 (analyzing bits); min category p-value = 0.122

processed 2.02e+09 bytes in 1 seconds (2.015 GB/s, 7.255 TB/h). Fri Feb 21 23:22:22 2025

p = 0.477
------

mix3 extreme = 2.10150 (sig = 00000001) weight 1 (16), p-value = 0.44
mix3 extreme = 2.31937 (sig = 00100100) weight 2 (112), p-value = 0.9
mix3 extreme = 2.98427 (sig = 20102000) weight 3 (448), p-value = 0.721
mix3 extreme = 3.62790 (sig = 21200001) weight 4 (1120), p-value = 0.274
mix3 extreme = 3.47379 (sig = 22011100) weight >=5 (4864), p-value = 0.918
bits per word = 64 (analyzing bits); min category p-value = 0.274

processed 2.51e+09 bytes in 1 seconds (2.514 GB/s, 9.052 TB/h). Fri Feb 21 23:22:22 2025

p = 0.798
------

mix3 extreme = 2.20184 (sig = 00100000) weight 1 (16), p-value = 0.362
mix3 extreme = 2.44576 (sig = 00100100) weight 2 (112), p-value = 0.804
mix3 extreme = 2.66267 (sig = 10200002) weight 3 (448), p-value = 0.969
mix3 extreme = 3.76139 (sig = 21200001) weight 4 (1120), p-value = 0.172
mix3 extreme = 3.82048 (sig = 20222201) weight >=5 (4864), p-value = 0.477
bits per word = 64 (analyzing bits); min category p-value = 0.172

processed 3.01e+09 bytes in 2 seconds (1.507 GB/s, 5.425 TB/h). Fri Feb 21 23:22:23 2025

p = 0.612
------

mix3 extreme = 2.18426 (sig = 00100000) weight 1 (16), p-value = 0.375
mix3 extreme = 2.65062 (sig = 20002000) weight 2 (112), p-value = 0.595
mix3 extreme = 2.92917 (sig = 01100200) weight 3 (448), p-value = 0.782
mix3 extreme = 3.89526 (sig = 21200001) weight 4 (1120), p-value = 0.104
mix3 extreme = 3.78679 (sig = 20220022) weight >=5 (4864), p-value = 0.524
bits per word = 64 (analyzing bits); min category p-value = 0.104

processed 4.01e+09 bytes in 2 seconds (2.006 GB/s, 7.222 TB/h). Fri Feb 21 23:22:23 2025

p = 0.423
------

mix3 extreme = 2.46216 (sig = 20000000) weight 1 (16), p-value = 0.199
mix3 extreme = 2.30564 (sig = 20002000) weight 2 (112), p-value = 0.909
mix3 extreme = 2.97563 (sig = 00100220) weight 3 (448), p-value = 0.731
mix3 extreme = 3.70435 (sig = 20110001) weight 4 (1120), p-value = 0.211
mix3 extreme = 3.47865 (sig = 22100011) weight >=5 (4864), p-value = 0.914
bits per word = 64 (analyzing bits); min category p-value = 0.199

processed 5.01e+09 bytes in 2 seconds (2.505 GB/s, 9.019 TB/h). Fri Feb 21 23:22:23 2025

p = 0.671
------

mix3 extreme = 2.48402 (sig = 00100000) weight 1 (16), p-value = 0.189
mix3 extreme = 2.32620 (sig = 00000012) weight 2 (112), p-value = 0.896
mix3 extreme = 3.13986 (sig = 00120200) weight 3 (448), p-value = 0.531
mix3 extreme = 3.56935 (sig = 20110001) weight 4 (1120), p-value = 0.33
mix3 extreme = 3.62670 (sig = 01210201) weight >=5 (4864), p-value = 0.753
bits per word = 64 (analyzing bits); min category p-value = 0.189

processed 6.01e+09 bytes in 3 seconds (2.003 GB/s, 7.211 TB/h). Fri Feb 21 23:22:24 2025

p = 0.649
------

mix3 extreme = 2.07995 (sig = 00100000) weight 1 (16), p-value = 0.458
mix3 extreme = 2.14438 (sig = 00002010) weight 2 (112), p-value = 0.974
mix3 extreme = 3.12213 (sig = 00120200) weight 3 (448), p-value = 0.553
mix3 extreme = 3.59330 (sig = 20110001) weight 4 (1120), p-value = 0.306
mix3 extreme = 3.93490 (sig = 01210201) weight >=5 (4864), p-value = 0.333
bits per word = 64 (analyzing bits); min category p-value = 0.306

processed 7.01e+09 bytes in 3 seconds (2.336 GB/s, 8.409 TB/h). Fri Feb 21 23:22:24 2025

p = 0.839
------

mix3 extreme = 1.82939 (sig = 20000000) weight 1 (16), p-value = 0.672
mix3 extreme = 2.04212 (sig = 00002010) weight 2 (112), p-value = 0.991
mix3 extreme = 3.40400 (sig = 00120200) weight 3 (448), p-value = 0.257
mix3 extreme = 3.20336 (sig = 12020002) weight 4 (1120), p-value = 0.782
mix3 extreme = 4.26991 (sig = 01210201) weight >=5 (4864), p-value = 0.0907
bits per word = 64 (analyzing bits); min category p-value = 0.0907

processed 8.5e+09 bytes in 4 seconds (2.126 GB/s, 7.654 TB/h). Fri Feb 21 23:22:25 2025

p = 0.378
------

mix3 extreme = 1.82510 (sig = 00100000) weight 1 (16), p-value = 0.676
mix3 extreme = 2.69933 (sig = 00002020) weight 2 (112), p-value = 0.542
mix3 extreme = 2.78787 (sig = 02020200) weight 3 (448), p-value = 0.908
mix3 extreme = 3.06049 (sig = 00001122) weight 4 (1120), p-value = 0.916
mix3 extreme = 4.40242 (sig = 01210201) weight >=5 (4864), p-value = 0.0507
bits per word = 64 (analyzing bits); min category p-value = 0.0507

processed 1e+10 bytes in 4 seconds (2.501 GB/s, 9.002 TB/h). Fri Feb 21 23:22:25 2025

p = 0.229
------

mix3 extreme = 2.20665 (sig = 00100000) weight 1 (16), p-value = 0.358
mix3 extreme = 2.66682 (sig = 00002020) weight 2 (112), p-value = 0.577
mix3 extreme = 3.18173 (sig = 02200010) weight 3 (448), p-value = 0.481
mix3 extreme = 3.39020 (sig = 01202100) weight 4 (1120), p-value = 0.543
mix3 extreme = 3.99569 (sig = 01210201) weight >=5 (4864), p-value = 0.269
bits per word = 64 (analyzing bits); min category p-value = 0.269

processed 1.25e+10 bytes in 5 seconds (2.503 GB/s, 9.012 TB/h). Fri Feb 21 23:22:26 2025

p = 0.792
------

mix3 extreme = 1.60923 (sig = 00000002) weight 1 (16), p-value = 0.838
mix3 extreme = 2.73755 (sig = 02000001) weight 2 (112), p-value = 0.501
mix3 extreme = 3.23640 (sig = 00101200) weight 3 (448), p-value = 0.419
mix3 extreme = 3.31439 (sig = 21220000) weight 4 (1120), p-value = 0.643
mix3 extreme = 3.57996 (sig = 21001101) weight >=5 (4864), p-value = 0.812
bits per word = 64 (analyzing bits); min category p-value = 0.419

processed 1.5e+10 bytes in 6 seconds (2.502 GB/s, 9.008 TB/h). Fri Feb 21 23:22:27 2025

p = 0.934
------

mix3 extreme = 1.91785 (sig = 00000002) weight 1 (16), p-value = 0.596
mix3 extreme = 2.60874 (sig = 02000001) weight 2 (112), p-value = 0.64
mix3 extreme = 3.38156 (sig = 00101200) weight 3 (448), p-value = 0.276
mix3 extreme = 3.24044 (sig = 21220000) weight 4 (1120), p-value = 0.737
mix3 extreme = 3.69590 (sig = 12211201) weight >=5 (4864), p-value = 0.656
bits per word = 64 (analyzing bits); min category p-value = 0.276

processed 1.75e+10 bytes in 7 seconds (2.501 GB/s, 9.004 TB/h). Fri Feb 21 23:22:28 2025

p = 0.801
------

mix3 extreme = 1.80532 (sig = 00000001) weight 1 (16), p-value = 0.692
mix3 extreme = 2.54416 (sig = 00020010) weight 2 (112), p-value = 0.709
mix3 extreme = 3.15400 (sig = 00101200) weight 3 (448), p-value = 0.514
mix3 extreme = 3.33906 (sig = 21220000) weight 4 (1120), p-value = 0.61
mix3 extreme = 3.56547 (sig = 00012112) weight >=5 (4864), p-value = 0.829
bits per word = 64 (analyzing bits); min category p-value = 0.514

processed 2e+10 bytes in 8 seconds (2.501 GB/s, 9.002 TB/h). Fri Feb 21 23:22:29 2025

p = 0.973
------

mix3 extreme = 1.94039 (sig = 00000002) weight 1 (16), p-value = 0.577
mix3 extreme = 2.27077 (sig = 01010000) weight 2 (112), p-value = 0.928
mix3 extreme = 3.19342 (sig = 00120200) weight 3 (448), p-value = 0.468
mix3 extreme = 3.51655 (sig = 21220000) weight 4 (1120), p-value = 0.387
mix3 extreme = 3.56607 (sig = 20122012) weight >=5 (4864), p-value = 0.828
bits per word = 64 (analyzing bits); min category p-value = 0.387

processed 2.5e+10 bytes in 10 seconds (2.502 GB/s, 9.005 TB/h). Fri Feb 21 23:22:31 2025

p = 0.914
------

mix3 extreme = 1.89200 (sig = 01000000) weight 1 (16), p-value = 0.619
mix3 extreme = 2.24801 (sig = 01010000) weight 2 (112), p-value = 0.938
mix3 extreme = 2.88784 (sig = 02000210) weight 3 (448), p-value = 0.825
mix3 extreme = 3.34758 (sig = 21220000) weight 4 (1120), p-value = 0.599
mix3 extreme = 3.45248 (sig = 11200111) weight >=5 (4864), p-value = 0.933
bits per word = 64 (analyzing bits); min category p-value = 0.599

processed 3e+10 bytes in 12 seconds (2.501 GB/s, 9.002 TB/h). Fri Feb 21 23:22:33 2025

p = 0.99
------

mix3 extreme = 1.56396 (sig = 00000200) weight 1 (16), p-value = 0.865
mix3 extreme = 1.97760 (sig = 00000022) weight 2 (112), p-value = 0.996
mix3 extreme = 3.23027 (sig = 20021000) weight 3 (448), p-value = 0.426
mix3 extreme = 3.36856 (sig = 21021000) weight 4 (1120), p-value = 0.571
mix3 extreme = 4.27622 (sig = 20122012) weight >=5 (4864), p-value = 0.0883
bits per word = 64 (analyzing bits); min category p-value = 0.0883

processed 4e+10 bytes in 16 seconds (2.501 GB/s, 9.002 TB/h). Fri Feb 21 23:22:37 2025

p = 0.37
------

mix3 extreme = 1.89666 (sig = 00000100) weight 1 (16), p-value = 0.615
mix3 extreme = 2.70819 (sig = 20000010) weight 2 (112), p-value = 0.532
mix3 extreme = 3.11747 (sig = 20021000) weight 3 (448), p-value = 0.559
mix3 extreme = 2.83492 (sig = 21021000) weight 4 (1120), p-value = 0.994
mix3 extreme = 3.73280 (sig = 20122012) weight >=5 (4864), p-value = 0.602
bits per word = 64 (analyzing bits); min category p-value = 0.532

processed 5e+10 bytes in 20 seconds (2.501 GB/s, 9.002 TB/h). Fri Feb 21 23:22:41 2025

p = 0.978
------

mix3 extreme = 1.60871 (sig = 00000001) weight 1 (16), p-value = 0.838
mix3 extreme = 2.92820 (sig = 20000010) weight 2 (112), p-value = 0.318
mix3 extreme = 3.30722 (sig = 20021000) weight 3 (448), p-value = 0.344
mix3 extreme = 3.12534 (sig = 12100002) weight 4 (1120), p-value = 0.863
mix3 extreme = 3.90592 (sig = 21101011) weight >=5 (4864), p-value = 0.367
bits per word = 64 (analyzing bits); min category p-value = 0.318

processed 6e+10 bytes in 24 seconds (2.501 GB/s, 9.002 TB/h). Fri Feb 21 23:22:45 2025

p = 0.852
------

mix3 extreme = 1.48010 (sig = 02000000) weight 1 (16), p-value = 0.909
mix3 extreme = 2.48996 (sig = 20000010) weight 2 (112), p-value = 0.763
mix3 extreme = 3.17803 (sig = 20021000) weight 3 (448), p-value = 0.486
mix3 extreme = 3.17420 (sig = 20101010) weight 4 (1120), p-value = 0.814
mix3 extreme = 3.79968 (sig = 21101011) weight >=5 (4864), p-value = 0.506
bits per word = 64 (analyzing bits); min category p-value = 0.486

processed 7e+10 bytes in 28 seconds (2.501 GB/s, 9.002 TB/h). Fri Feb 21 23:22:49 2025

p = 0.964
------

mix3 extreme = 2.31418 (sig = 02000000) weight 1 (16), p-value = 0.284
mix3 extreme = 2.75073 (sig = 02000001) weight 2 (112), p-value = 0.487
mix3 extreme = 3.22184 (sig = 20021000) weight 3 (448), p-value = 0.435
mix3 extreme = 2.99913 (sig = 12100002) weight 4 (1120), p-value = 0.952
mix3 extreme = 4.17272 (sig = 21101011) weight >=5 (4864), p-value = 0.136
bits per word = 64 (analyzing bits); min category p-value = 0.136

processed 8.5e+10 bytes in 35 seconds (2.429 GB/s, 8.744 TB/h). Fri Feb 21 23:22:56 2025

p = 0.519
------

mix3 extreme = 2.96167 (sig = 02000000) weight 1 (16), p-value = 0.0478
mix3 extreme = 2.15955 (sig = 02000001) weight 2 (112), p-value = 0.97
mix3 extreme = 3.37730 (sig = 20021000) weight 3 (448), p-value = 0.28
mix3 extreme = 3.52700 (sig = 12100002) weight 4 (1120), p-value = 0.376
mix3 extreme = 4.04580 (sig = 21101011) weight >=5 (4864), p-value = 0.224
bits per word = 64 (analyzing bits); min category p-value = 0.0478

processed 1e+11 bytes in 41 seconds (2.439 GB/s, 8.781 TB/h). Fri Feb 21 23:23:02 2025

p = 0.217
------

mix3 extreme = 2.47196 (sig = 02000000) weight 1 (16), p-value = 0.195
mix3 extreme = 2.51373 (sig = 02000001) weight 2 (112), p-value = 0.74
mix3 extreme = 3.39366 (sig = 20021000) weight 3 (448), p-value = 0.266
mix3 extreme = 3.21456 (sig = 12100002) weight 4 (1120), p-value = 0.769
mix3 extreme = 3.77086 (sig = 12221021) weight >=5 (4864), p-value = 0.547
bits per word = 64 (analyzing bits); min category p-value = 0.195

processed 1.25e+11 bytes in 51 seconds (2.451 GB/s, 8.824 TB/h). Fri Feb 21 23:23:12 2025

p = 0.661
------

mix3 extreme = 2.48207 (sig = 02000000) weight 1 (16), p-value = 0.19
mix3 extreme = 2.29109 (sig = 20020000) weight 2 (112), p-value = 0.917
mix3 extreme = 3.01591 (sig = 20021000) weight 3 (448), p-value = 0.683
mix3 extreme = 3.36633 (sig = 20200101) weight 4 (1120), p-value = 0.574
mix3 extreme = 3.40749 (sig = 02222012) weight >=5 (4864), p-value = 0.959
bits per word = 64 (analyzing bits); min category p-value = 0.19

processed 1.5e+11 bytes in 61 seconds (2.459 GB/s, 8.853 TB/h). Fri Feb 21 23:23:22 2025

p = 0.651
------

mix3 extreme = 2.14788 (sig = 02000000) weight 1 (16), p-value = 0.403
mix3 extreme = 2.23188 (sig = 01000002) weight 2 (112), p-value = 0.945
mix3 extreme = 3.01619 (sig = 20100002) weight 3 (448), p-value = 0.683
mix3 extreme = 3.35531 (sig = 22020010) weight 4 (1120), p-value = 0.589
mix3 extreme = 3.90091 (sig = 20120120) weight >=5 (4864), p-value = 0.373
bits per word = 64 (analyzing bits); min category p-value = 0.373

processed 1.75e+11 bytes in 71 seconds (2.465 GB/s, 8.874 TB/h). Fri Feb 21 23:23:32 2025

p = 0.903
------

mix3 extreme = 2.56990 (sig = 00010000) weight 1 (16), p-value = 0.151
mix3 extreme = 2.15879 (sig = 20020000) weight 2 (112), p-value = 0.97
mix3 extreme = 3.11637 (sig = 20100002) weight 3 (448), p-value = 0.56
mix3 extreme = 3.75031 (sig = 20200101) weight 4 (1120), p-value = 0.179
mix3 extreme = 3.82089 (sig = 11022021) weight >=5 (4864), p-value = 0.476
bits per word = 64 (analyzing bits); min category p-value = 0.151

processed 2e+11 bytes in 81 seconds (2.469 GB/s, 8.889 TB/h). Fri Feb 21 23:23:42 2025

p = 0.559
------

mix3 extreme = 2.68286 (sig = 00010000) weight 1 (16), p-value = 0.111
mix3 extreme = 2.03612 (sig = 01000002) weight 2 (112), p-value = 0.992
mix3 extreme = 2.72508 (sig = 01110000) weight 3 (448), p-value = 0.944
mix3 extreme = 3.29533 (sig = 20200101) weight 4 (1120), p-value = 0.668
mix3 extreme = 3.94062 (sig = 11101011) weight >=5 (4864), p-value = 0.327
bits per word = 64 (analyzing bits); min category p-value = 0.111

processed 2.5e+11 bytes in 102 seconds (2.451 GB/s, 8.824 TB/h). Fri Feb 21 23:24:03 2025

p = 0.444
------

mix3 extreme = 3.02806 (sig = 00010000) weight 1 (16), p-value = 0.0387
mix3 extreme = 2.64241 (sig = 12000000) weight 2 (112), p-value = 0.604
mix3 extreme = 3.14107 (sig = 12001000) weight 3 (448), p-value = 0.53
mix3 extreme = 3.10292 (sig = 10202002) weight 4 (1120), p-value = 0.883
mix3 extreme = 3.78800 (sig = 11122221) weight >=5 (4864), p-value = 0.522
bits per word = 64 (analyzing bits); min category p-value = 0.0387

processed 3e+11 bytes in 122 seconds (2.459 GB/s, 8.853 TB/h). Fri Feb 21 23:24:23 2025

p = 0.179
------

mix3 extreme = 2.11373 (sig = 00010000) weight 1 (16), p-value = 0.43
mix3 extreme = 2.53673 (sig = 00010020) weight 2 (112), p-value = 0.716
mix3 extreme = 3.82566 (sig = 01000220) weight 3 (448), p-value = 0.0568
mix3 extreme = 3.94266 (sig = 22020010) weight 4 (1120), p-value = 0.0863
mix3 extreme = 3.38864 (sig = 02212120) weight >=5 (4864), p-value = 0.967
bits per word = 64 (analyzing bits); min category p-value = 0.0568

processed 4e+11 bytes in 163 seconds (2.454 GB/s, 8.834 TB/h). Fri Feb 21 23:25:04 2025

p = 0.253
------

mix3 extreme = 2.02087 (sig = 02000000) weight 1 (16), p-value = 0.507
mix3 extreme = 3.38282 (sig = 00010020) weight 2 (112), p-value = 0.0772
mix3 extreme = 3.77275 (sig = 01000220) weight 3 (448), p-value = 0.0698
mix3 extreme = 3.96045 (sig = 22020010) weight 4 (1120), p-value = 0.0804
mix3 extreme = 3.96698 (sig = 10212100) weight >=5 (4864), p-value = 0.298
bits per word = 64 (analyzing bits); min category p-value = 0.0698

processed 5e+11 bytes in 204 seconds (2.451 GB/s, 8.824 TB/h). Fri Feb 21 23:25:45 2025

p = 0.304
------

mix3 extreme = 2.11460 (sig = 02000000) weight 1 (16), p-value = 0.429
mix3 extreme = 3.45081 (sig = 00010020) weight 2 (112), p-value = 0.0607
mix3 extreme = 4.11176 (sig = 01000220) weight 3 (448), p-value = 0.0174
mix3 extreme = 3.90641 (sig = 22020010) weight 4 (1120), p-value = 0.0996
mix3 extreme = 3.86051 (sig = 10110212) weight >=5 (4864), p-value = 0.423
bits per word = 64 (analyzing bits); min category p-value = 0.0174

processed 6e+11 bytes in 244 seconds (2.459 GB/s, 8.853 TB/h). Fri Feb 21 23:26:25 2025

p = 0.0842
------

mix3 extreme = 2.19471 (sig = 02000000) weight 1 (16), p-value = 0.367
mix3 extreme = 3.42206 (sig = 00010020) weight 2 (112), p-value = 0.0673
mix3 extreme = 3.76615 (sig = 01000220) weight 3 (448), p-value = 0.0716
mix3 extreme = 3.59051 (sig = 22020010) weight 4 (1120), p-value = 0.309
mix3 extreme = 3.55018 (sig = 20012221) weight >=5 (4864), p-value = 0.846
bits per word = 64 (analyzing bits); min category p-value = 0.0673

processed 7e+11 bytes in 285 seconds (2.456 GB/s, 8.842 TB/h). Fri Feb 21 23:27:06 2025

p = 0.294
------

mix3 extreme = 2.08568 (sig = 02000000) weight 1 (16), p-value = 0.453
mix3 extreme = 2.70372 (sig = 00010020) weight 2 (112), p-value = 0.537
mix3 extreme = 3.67441 (sig = 20021000) weight 3 (448), p-value = 0.101
mix3 extreme = 3.40600 (sig = 22020010) weight 4 (1120), p-value = 0.522
mix3 extreme = 3.74169 (sig = 10110212) weight >=5 (4864), p-value = 0.589
bits per word = 64 (analyzing bits); min category p-value = 0.101

processed 8.5e+11 bytes in 346 seconds (2.457 GB/s, 8.844 TB/h). Fri Feb 21 23:28:07 2025

p = 0.414
------

mix3 extreme = 1.70943 (sig = 00000100) weight 1 (16), p-value = 0.768
mix3 extreme = 3.16917 (sig = 00010020) weight 2 (112), p-value = 0.157
mix3 extreme = 3.64671 (sig = 02000201) weight 3 (448), p-value = 0.112
mix3 extreme = 3.82262 (sig = 11010200) weight 4 (1120), p-value = 0.137
mix3 extreme = 3.64758 (sig = 00222021) weight >=5 (4864), p-value = 0.724
bits per word = 64 (analyzing bits); min category p-value = 0.112

processed 1e+12 bytes in 407 seconds (2.457 GB/s, 8.845 TB/h). Fri Feb 21 23:29:08 2025

p = 0.448
------

mix3 extreme = 1.76773 (sig = 20000000) weight 1 (16), p-value = 0.723
mix3 extreme = 3.14942 (sig = 00002020) weight 2 (112), p-value = 0.168
mix3 extreme = 3.45782 (sig = 02000201) weight 3 (448), p-value = 0.217
mix3 extreme = 3.68367 (sig = 11010200) weight 4 (1120), p-value = 0.227
mix3 extreme = 4.05225 (sig = 00222021) weight >=5 (4864), p-value = 0.219
bits per word = 64 (analyzing bits); min category p-value = 0.168

processed 1.25e+12 bytes in 509 seconds (2.456 GB/s, 8.841 TB/h). Fri Feb 21 23:30:50 2025

p = 0.6
------

mix3 extreme = 1.78446 (sig = 00000001) weight 1 (16), p-value = 0.709
mix3 extreme = 3.00030 (sig = 00002020) weight 2 (112), p-value = 0.261
mix3 extreme = 3.68239 (sig = 02000201) weight 3 (448), p-value = 0.0983
mix3 extreme = 3.38984 (sig = 11010020) weight 4 (1120), p-value = 0.543
mix3 extreme = 4.00734 (sig = 02221002) weight >=5 (4864), p-value = 0.258
bits per word = 64 (analyzing bits); min category p-value = 0.0983

processed 1.5e+12 bytes in 611 seconds (2.455 GB/s, 8.838 TB/h). Fri Feb 21 23:32:32 2025

p = 0.404
------

mix3 extreme = 1.49970 (sig = 02000000) weight 1 (16), p-value = 0.899
mix3 extreme = 2.64430 (sig = 20000020) weight 2 (112), p-value = 0.602
mix3 extreme = 3.85954 (sig = 02000201) weight 3 (448), p-value = 0.0496
mix3 extreme = 3.76326 (sig = 11010020) weight 4 (1120), p-value = 0.171
mix3 extreme = 3.91001 (sig = 00222021) weight >=5 (4864), p-value = 0.362
bits per word = 64 (analyzing bits); min category p-value = 0.0496

processed 1.75e+12 bytes in 713 seconds (2.454 GB/s, 8.836 TB/h). Fri Feb 21 23:34:14 2025

p = 0.225
------

mix3 extreme = 1.64313 (sig = 02000000) weight 1 (16), p-value = 0.816
mix3 extreme = 3.02382 (sig = 02100000) weight 2 (112), p-value = 0.244
mix3 extreme = 3.21441 (sig = 02000201) weight 3 (448), p-value = 0.443
mix3 extreme = 4.07811 (sig = 11010020) weight 4 (1120), p-value = 0.0496
mix3 extreme = 4.50737 (sig = 00222021) weight >=5 (4864), p-value = 0.0314
bits per word = 64 (analyzing bits); min category p-value = 0.0314

processed 2e+12 bytes in 815 seconds (2.454 GB/s, 8.834 TB/h). Fri Feb 21 23:35:56 2025

p = 0.148
------

mix3 extreme = 1.57884 (sig = 20000000) weight 1 (16), p-value = 0.857
mix3 extreme = 2.68908 (sig = 00001002) weight 2 (112), p-value = 0.553
mix3 extreme = 3.22275 (sig = 01000220) weight 3 (448), p-value = 0.434
mix3 extreme = 3.84110 (sig = 11010020) weight 4 (1120), p-value = 0.128
mix3 extreme = 3.89955 (sig = 10020112) weight >=5 (4864), p-value = 0.374
bits per word = 64 (analyzing bits); min category p-value = 0.128

processed 2.5e+12 bytes in 1.02e+03 seconds (2.456 GB/s, 8.841 TB/h). Fri Feb 21 23:39:19 2025

p = 0.496
------

mix3 extreme = 1.67736 (sig = 00001000) weight 1 (16), p-value = 0.792
mix3 extreme = 2.62488 (sig = 00001002) weight 2 (112), p-value = 0.623
mix3 extreme = 3.38419 (sig = 02002002) weight 3 (448), p-value = 0.274
mix3 extreme = 3.80757 (sig = 11010020) weight 4 (1120), p-value = 0.145
mix3 extreme = 3.95015 (sig = 00211221) weight >=5 (4864), p-value = 0.316
bits per word = 64 (analyzing bits); min category p-value = 0.145

processed 3e+12 bytes in 1.22e+03 seconds (2.451 GB/s, 8.824 TB/h). Fri Feb 21 23:42:45 2025

p = 0.544
------

mix3 extreme = 1.79534 (sig = 00002000) weight 1 (16), p-value = 0.701
mix3 extreme = 2.76965 (sig = 00001002) weight 2 (112), p-value = 0.468
mix3 extreme = 3.58785 (sig = 01000220) weight 3 (448), p-value = 0.139
mix3 extreme = 3.25341 (sig = 20200012) weight 4 (1120), p-value = 0.721
mix3 extreme = 3.65576 (sig = 12111122) weight >=5 (4864), p-value = 0.713
bits per word = 64 (analyzing bits); min category p-value = 0.139

processed 4e+12 bytes in 1.63e+03 seconds (2.448 GB/s, 8.813 TB/h). Fri Feb 21 23:49:35 2025

p = 0.526
------
```

------------------------------------------------------------
### `FIO_DEFINE_RANDOM128_FN`

The following are the tests for the built-in `FIO_DEFINE_RANDOM128_FN` macro using the deterministic PRNG (where the auto-reseeding `reseed_log` is set to `0`).

**Note**: setting `reseed_log` to any practical value (such as `31`) would improve randomness make the PRNG non-deterministic, improving the security of the result.

**The `PractRand` results**:

```txt
# ./tmp/rnd -p M128 | RNG_test stdin
RNG_test using PractRand version 0.95
RNG = RNG_stdin, seed = unknown
test set = core, folding = standard(unknown format)

rng=RNG_stdin, seed=unknown
length= 256 megabytes (2^28 bytes), time= 2.4 seconds
  no anomalies in 217 test result(s)

rng=RNG_stdin, seed=unknown
length= 512 megabytes (2^29 bytes), time= 5.0 seconds
  no anomalies in 232 test result(s)

rng=RNG_stdin, seed=unknown
length= 1 gigabyte (2^30 bytes), time= 10.1 seconds
  no anomalies in 251 test result(s)

rng=RNG_stdin, seed=unknown
length= 2 gigabytes (2^31 bytes), time= 20.6 seconds
  no anomalies in 269 test result(s)

rng=RNG_stdin, seed=unknown
length= 4 gigabytes (2^32 bytes), time= 41.2 seconds
  no anomalies in 283 test result(s)

rng=RNG_stdin, seed=unknown
length= 8 gigabytes (2^33 bytes), time= 83.9 seconds
  no anomalies in 300 test result(s)

rng=RNG_stdin, seed=unknown
length= 16 gigabytes (2^34 bytes), time= 167 seconds
  no anomalies in 315 test result(s)

rng=RNG_stdin, seed=unknown
length= 32 gigabytes (2^35 bytes), time= 330 seconds
  no anomalies in 328 test result(s)

rng=RNG_stdin, seed=unknown
length= 64 gigabytes (2^36 bytes), time= 665 seconds
  no anomalies in 344 test result(s)

rng=RNG_stdin, seed=unknown
length= 128 gigabytes (2^37 bytes), time= 1329 seconds
  no anomalies in 359 test result(s)

rng=RNG_stdin, seed=unknown
length= 256 gigabytes (2^38 bytes), time= 2630 seconds
  no anomalies in 372 test result(s)

rng=RNG_stdin, seed=unknown
length= 512 gigabytes (2^39 bytes), time= 6846 seconds
  no anomalies in 387 test result(s)

rng=RNG_stdin, seed=unknown
length= 1 terabyte (2^40 bytes), time= 18633 seconds
  no anomalies in 401 test result(s)

rng=RNG_stdin, seed=unknown
length= 2 terabytes (2^41 bytes), time= 39061 seconds
  no anomalies in 413 test result(s)
```

**The tests adopted from the `xoshiro` code base**:


```txt
# ./tmp/random m128
mix3 extreme = 1.90403 (sig = 00020000) weight 1 (16), p-value = 0.608
mix3 extreme = 2.78422 (sig = 01000001) weight 2 (112), p-value = 0.453
mix3 extreme = 2.98802 (sig = 00001220) weight 3 (448), p-value = 0.716
mix3 extreme = 3.51263 (sig = 22110000) weight 4 (1120), p-value = 0.392
mix3 extreme = 3.86128 (sig = 22110200) weight >=5 (4864), p-value = 0.422
bits per word = 64 (analyzing bits); min category p-value = 0.392

processed 1.11e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.917
------

mix3 extreme = 2.33512 (sig = 00020000) weight 1 (16), p-value = 0.271
mix3 extreme = 2.60631 (sig = 00000011) weight 2 (112), p-value = 0.643
mix3 extreme = 3.20941 (sig = 00001220) weight 3 (448), p-value = 0.449
mix3 extreme = 3.23491 (sig = 10000221) weight 4 (1120), p-value = 0.744
mix3 extreme = 3.98181 (sig = 12101200) weight >=5 (4864), p-value = 0.283
bits per word = 64 (analyzing bits); min category p-value = 0.271

processed 1.29e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.794
------

mix3 extreme = 2.09795 (sig = 00020000) weight 1 (16), p-value = 0.443
mix3 extreme = 2.59859 (sig = 00000011) weight 2 (112), p-value = 0.651
mix3 extreme = 3.16919 (sig = 00020012) weight 3 (448), p-value = 0.496
mix3 extreme = 3.32522 (sig = 01002210) weight 4 (1120), p-value = 0.628
mix3 extreme = 3.87995 (sig = 20211102) weight >=5 (4864), p-value = 0.398
bits per word = 64 (analyzing bits); min category p-value = 0.398

processed 1.66e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.921
------

mix3 extreme = 1.97959 (sig = 00020000) weight 1 (16), p-value = 0.543
mix3 extreme = 2.47936 (sig = 00000011) weight 2 (112), p-value = 0.773
mix3 extreme = 3.25629 (sig = 02000022) weight 3 (448), p-value = 0.397
mix3 extreme = 3.20819 (sig = 20001012) weight 4 (1120), p-value = 0.776
mix3 extreme = 3.85604 (sig = 21111101) weight >=5 (4864), p-value = 0.429
bits per word = 64 (analyzing bits); min category p-value = 0.397

processed 1.85e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.92
------

mix3 extreme = 2.14920 (sig = 00020000) weight 1 (16), p-value = 0.402
mix3 extreme = 2.58549 (sig = 00000011) weight 2 (112), p-value = 0.665
mix3 extreme = 2.97421 (sig = 02000022) weight 3 (448), p-value = 0.732
mix3 extreme = 2.92783 (sig = 00112001) weight 4 (1120), p-value = 0.978
mix3 extreme = 3.95789 (sig = 22110200) weight >=5 (4864), p-value = 0.308
bits per word = 64 (analyzing bits); min category p-value = 0.308

processed 2.03e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.841
------

mix3 extreme = 1.47529 (sig = 00001000) weight 1 (16), p-value = 0.911
mix3 extreme = 2.73432 (sig = 10000100) weight 2 (112), p-value = 0.505
mix3 extreme = 2.85412 (sig = 00220100) weight 3 (448), p-value = 0.856
mix3 extreme = 3.71866 (sig = 20200011) weight 4 (1120), p-value = 0.201
mix3 extreme = 3.82703 (sig = 12212021) weight >=5 (4864), p-value = 0.468
bits per word = 64 (analyzing bits); min category p-value = 0.201

processed 2.59e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.674
------

mix3 extreme = 1.48360 (sig = 20000000) weight 1 (16), p-value = 0.907
mix3 extreme = 2.46121 (sig = 00002002) weight 2 (112), p-value = 0.79
mix3 extreme = 2.87026 (sig = 20000201) weight 3 (448), p-value = 0.841
mix3 extreme = 3.86069 (sig = 20200011) weight 4 (1120), p-value = 0.119
mix3 extreme = 3.76155 (sig = 21201200) weight >=5 (4864), p-value = 0.56
bits per word = 64 (analyzing bits); min category p-value = 0.119

processed 3.14e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.469
------

mix3 extreme = 1.84311 (sig = 20000000) weight 1 (16), p-value = 0.661
mix3 extreme = 2.57296 (sig = 10001000) weight 2 (112), p-value = 0.679
mix3 extreme = 3.19014 (sig = 20000201) weight 3 (448), p-value = 0.471
mix3 extreme = 3.98985 (sig = 20200011) weight 4 (1120), p-value = 0.0714
mix3 extreme = 3.85549 (sig = 22121010) weight >=5 (4864), p-value = 0.43
bits per word = 64 (analyzing bits); min category p-value = 0.0714

processed 4.07e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.309
------

mix3 extreme = 1.41079 (sig = 20000000) weight 1 (16), p-value = 0.937
mix3 extreme = 3.04738 (sig = 10001000) weight 2 (112), p-value = 0.228
mix3 extreme = 3.74043 (sig = 00220100) weight 3 (448), p-value = 0.079
mix3 extreme = 3.82980 (sig = 00101210) weight 4 (1120), p-value = 0.134
mix3 extreme = 3.95613 (sig = 12012120) weight >=5 (4864), p-value = 0.31
bits per word = 64 (analyzing bits); min category p-value = 0.079

processed 5.18e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.337
------

mix3 extreme = 1.46023 (sig = 00020000) weight 1 (16), p-value = 0.917
mix3 extreme = 2.75344 (sig = 00200100) weight 2 (112), p-value = 0.484
mix3 extreme = 3.35270 (sig = 20000201) weight 3 (448), p-value = 0.301
mix3 extreme = 3.74339 (sig = 02102100) weight 4 (1120), p-value = 0.184
mix3 extreme = 3.75002 (sig = 12012120) weight >=5 (4864), p-value = 0.577
bits per word = 64 (analyzing bits); min category p-value = 0.184

processed 6.1e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.638
------

mix3 extreme = 1.50237 (sig = 00000020) weight 1 (16), p-value = 0.898
mix3 extreme = 2.42220 (sig = 00200100) weight 2 (112), p-value = 0.825
mix3 extreme = 2.96916 (sig = 20000201) weight 3 (448), p-value = 0.738
mix3 extreme = 3.78954 (sig = 02102100) weight 4 (1120), p-value = 0.156
mix3 extreme = 3.66917 (sig = 10010121) weight >=5 (4864), p-value = 0.694
bits per word = 64 (analyzing bits); min category p-value = 0.156

processed 7.03e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.571
------

mix3 extreme = 1.78774 (sig = 00000020) weight 1 (16), p-value = 0.707
mix3 extreme = 2.70149 (sig = 00010100) weight 2 (112), p-value = 0.54
mix3 extreme = 3.27303 (sig = 01120000) weight 3 (448), p-value = 0.379
mix3 extreme = 4.21573 (sig = 02102100) weight 4 (1120), p-value = 0.0275
mix3 extreme = 3.70578 (sig = 12100210) weight >=5 (4864), p-value = 0.641
bits per word = 64 (analyzing bits); min category p-value = 0.0275

processed 8.5e+08 bytes in 0 seconds (inf GB/s, inf TB/h). Fri Feb 21 23:22:45 2025

p = 0.13
------

mix3 extreme = 2.31948 (sig = 10000000) weight 1 (16), p-value = 0.281
mix3 extreme = 2.44934 (sig = 00010100) weight 2 (112), p-value = 0.801
mix3 extreme = 3.53130 (sig = 01120000) weight 3 (448), p-value = 0.169
mix3 extreme = 4.28717 (sig = 02102100) weight 4 (1120), p-value = 0.0201
mix3 extreme = 3.66277 (sig = 12100210) weight >=5 (4864), p-value = 0.703
bits per word = 64 (analyzing bits); min category p-value = 0.0201

processed 1.02e+09 bytes in 1 seconds (1.017 GB/s, 3.661 TB/h). Fri Feb 21 23:22:46 2025

p = 0.0964
------

mix3 extreme = 2.60876 (sig = 00000020) weight 1 (16), p-value = 0.136
mix3 extreme = 2.67150 (sig = 22000000) weight 2 (112), p-value = 0.572
mix3 extreme = 3.39462 (sig = 01120000) weight 3 (448), p-value = 0.265
mix3 extreme = 3.62549 (sig = 02200120) weight 4 (1120), p-value = 0.276
mix3 extreme = 3.63707 (sig = 12100021) weight >=5 (4864), p-value = 0.739
bits per word = 64 (analyzing bits); min category p-value = 0.136

processed 1.26e+09 bytes in 1 seconds (1.257 GB/s, 4.526 TB/h). Fri Feb 21 23:22:46 2025

p = 0.518
------

mix3 extreme = 2.15590 (sig = 00000020) weight 1 (16), p-value = 0.397
mix3 extreme = 2.80570 (sig = 00001010) weight 2 (112), p-value = 0.431
mix3 extreme = 3.40481 (sig = 02020001) weight 3 (448), p-value = 0.257
mix3 extreme = 3.36693 (sig = 01100012) weight 4 (1120), p-value = 0.573
mix3 extreme = 3.51384 (sig = 12012120) weight >=5 (4864), p-value = 0.883
bits per word = 64 (analyzing bits); min category p-value = 0.257

processed 1.52e+09 bytes in 1 seconds (1.516 GB/s, 5.458 TB/h). Fri Feb 21 23:22:46 2025

p = 0.773
------

mix3 extreme = 1.90047 (sig = 00000020) weight 1 (16), p-value = 0.611
mix3 extreme = 3.23402 (sig = 00001010) weight 2 (112), p-value = 0.128
mix3 extreme = 2.96881 (sig = 20001001) weight 3 (448), p-value = 0.739
mix3 extreme = 3.52470 (sig = 01012002) weight 4 (1120), p-value = 0.378
mix3 extreme = 3.72569 (sig = 22111101) weight >=5 (4864), p-value = 0.612
bits per word = 64 (analyzing bits); min category p-value = 0.128

processed 1.76e+09 bytes in 1 seconds (1.756 GB/s, 6.323 TB/h). Fri Feb 21 23:22:46 2025

p = 0.495
------

mix3 extreme = 1.57279 (sig = 00000010) weight 1 (16), p-value = 0.86
mix3 extreme = 2.81387 (sig = 00001010) weight 2 (112), p-value = 0.423
mix3 extreme = 3.18715 (sig = 02002010) weight 3 (448), p-value = 0.475
mix3 extreme = 3.33070 (sig = 00101120) weight 4 (1120), p-value = 0.621
mix3 extreme = 4.16820 (sig = 20122202) weight >=5 (4864), p-value = 0.139
bits per word = 64 (analyzing bits); min category p-value = 0.139

processed 2.02e+09 bytes in 1 seconds (2.015 GB/s, 7.255 TB/h). Fri Feb 21 23:22:46 2025

p = 0.526
------

mix3 extreme = 1.53157 (sig = 00000020) weight 1 (16), p-value = 0.883
mix3 extreme = 2.71599 (sig = 00001010) weight 2 (112), p-value = 0.524
mix3 extreme = 3.16021 (sig = 02002010) weight 3 (448), p-value = 0.507
mix3 extreme = 3.26824 (sig = 00101120) weight 4 (1120), p-value = 0.703
mix3 extreme = 4.01251 (sig = 22021002) weight >=5 (4864), p-value = 0.253
bits per word = 64 (analyzing bits); min category p-value = 0.253

processed 2.51e+09 bytes in 1 seconds (2.514 GB/s, 9.052 TB/h). Fri Feb 21 23:22:46 2025

p = 0.768
------

mix3 extreme = 1.74122 (sig = 00000020) weight 1 (16), p-value = 0.744
mix3 extreme = 2.86200 (sig = 00010100) weight 2 (112), p-value = 0.377
mix3 extreme = 3.41797 (sig = 02002010) weight 3 (448), p-value = 0.246
mix3 extreme = 3.48775 (sig = 00101120) weight 4 (1120), p-value = 0.421
mix3 extreme = 4.32378 (sig = 22021002) weight >=5 (4864), p-value = 0.0719
bits per word = 64 (analyzing bits); min category p-value = 0.0719

processed 3.01e+09 bytes in 1 seconds (3.014 GB/s, 10.85 TB/h). Fri Feb 21 23:22:46 2025

p = 0.311
------

mix3 extreme = 1.61472 (sig = 00000020) weight 1 (16), p-value = 0.835
mix3 extreme = 3.22143 (sig = 00010100) weight 2 (112), p-value = 0.133
mix3 extreme = 3.34281 (sig = 02002010) weight 3 (448), p-value = 0.31
mix3 extreme = 3.41424 (sig = 10011100) weight 4 (1120), p-value = 0.512
mix3 extreme = 3.68273 (sig = 00122011) weight >=5 (4864), p-value = 0.675
bits per word = 64 (analyzing bits); min category p-value = 0.133

processed 4.01e+09 bytes in 1 seconds (4.012 GB/s, 14.44 TB/h). Fri Feb 21 23:22:46 2025

p = 0.511
------

mix3 extreme = 2.08971 (sig = 00000020) weight 1 (16), p-value = 0.45
mix3 extreme = 3.03226 (sig = 00010100) weight 2 (112), p-value = 0.238
mix3 extreme = 3.22814 (sig = 00010102) weight 3 (448), p-value = 0.428
mix3 extreme = 3.27577 (sig = 10011100) weight 4 (1120), p-value = 0.693
mix3 extreme = 3.67641 (sig = 12012120) weight >=5 (4864), p-value = 0.684
bits per word = 64 (analyzing bits); min category p-value = 0.238

processed 5.01e+09 bytes in 2 seconds (2.505 GB/s, 9.019 TB/h). Fri Feb 21 23:22:47 2025

p = 0.744
------

mix3 extreme = 1.91894 (sig = 10000000) weight 1 (16), p-value = 0.595
mix3 extreme = 2.77625 (sig = 00010100) weight 2 (112), p-value = 0.461
mix3 extreme = 3.06382 (sig = 00011001) weight 3 (448), p-value = 0.625
mix3 extreme = 3.14390 (sig = 21020200) weight 4 (1120), p-value = 0.846
mix3 extreme = 3.59615 (sig = 00122011) weight >=5 (4864), p-value = 0.792
bits per word = 64 (analyzing bits); min category p-value = 0.461

processed 6.01e+09 bytes in 2 seconds (3.004 GB/s, 10.82 TB/h). Fri Feb 21 23:22:47 2025

p = 0.954
------

mix3 extreme = 1.86597 (sig = 00000020) weight 1 (16), p-value = 0.641
mix3 extreme = 2.61078 (sig = 00010100) weight 2 (112), p-value = 0.638
mix3 extreme = 3.42404 (sig = 00011001) weight 3 (448), p-value = 0.242
mix3 extreme = 3.40741 (sig = 00020122) weight 4 (1120), p-value = 0.52
mix3 extreme = 3.89941 (sig = 00122011) weight >=5 (4864), p-value = 0.374
bits per word = 64 (analyzing bits); min category p-value = 0.242

processed 7.01e+09 bytes in 2 seconds (3.504 GB/s, 12.61 TB/h). Fri Feb 21 23:22:47 2025

p = 0.749
------

mix3 extreme = 1.73399 (sig = 00000020) weight 1 (16), p-value = 0.75
mix3 extreme = 2.96016 (sig = 20000200) weight 2 (112), p-value = 0.292
mix3 extreme = 3.09543 (sig = 02000022) weight 3 (448), p-value = 0.586
mix3 extreme = 3.42588 (sig = 10101001) weight 4 (1120), p-value = 0.497
mix3 extreme = 3.66808 (sig = 10110201) weight >=5 (4864), p-value = 0.695
bits per word = 64 (analyzing bits); min category p-value = 0.292

processed 8.5e+09 bytes in 3 seconds (2.835 GB/s, 10.21 TB/h). Fri Feb 21 23:22:48 2025

p = 0.822
------

mix3 extreme = 1.52914 (sig = 00200000) weight 1 (16), p-value = 0.885
mix3 extreme = 3.81757 (sig = 20000200) weight 2 (112), p-value = 0.015
mix3 extreme = 3.07953 (sig = 02201000) weight 3 (448), p-value = 0.605
mix3 extreme = 3.26619 (sig = 01102001) weight 4 (1120), p-value = 0.705
mix3 extreme = 3.73963 (sig = 02122020) weight >=5 (4864), p-value = 0.592
bits per word = 64 (analyzing bits); min category p-value = 0.015

processed 1e+10 bytes in 3 seconds (3.334 GB/s, 12 TB/h). Fri Feb 21 23:22:48 2025

p = 0.0727
------

mix3 extreme = 2.00898 (sig = 00020000) weight 1 (16), p-value = 0.518
mix3 extreme = 2.73872 (sig = 20000200) weight 2 (112), p-value = 0.5
mix3 extreme = 2.89865 (sig = 22000200) weight 3 (448), p-value = 0.814
mix3 extreme = 3.51139 (sig = 10002220) weight 4 (1120), p-value = 0.393
mix3 extreme = 3.46747 (sig = 21020120) weight >=5 (4864), p-value = 0.922
bits per word = 64 (analyzing bits); min category p-value = 0.393

processed 1.25e+10 bytes in 4 seconds (3.129 GB/s, 11.27 TB/h). Fri Feb 21 23:22:49 2025

p = 0.918
------

mix3 extreme = 2.54743 (sig = 00020000) weight 1 (16), p-value = 0.16
mix3 extreme = 2.66976 (sig = 00020200) weight 2 (112), p-value = 0.574
mix3 extreme = 3.21302 (sig = 02020010) weight 3 (448), p-value = 0.445
mix3 extreme = 3.17616 (sig = 10002220) weight 4 (1120), p-value = 0.812
mix3 extreme = 4.08852 (sig = 00122011) weight >=5 (4864), p-value = 0.19
bits per word = 64 (analyzing bits); min category p-value = 0.16

processed 1.5e+10 bytes in 5 seconds (3.003 GB/s, 10.81 TB/h). Fri Feb 21 23:22:50 2025

p = 0.582
------

mix3 extreme = 2.32339 (sig = 00020000) weight 1 (16), p-value = 0.278
mix3 extreme = 2.41362 (sig = 00001010) weight 2 (112), p-value = 0.832
mix3 extreme = 3.50336 (sig = 02020010) weight 3 (448), p-value = 0.186
mix3 extreme = 3.13576 (sig = 20221000) weight 4 (1120), p-value = 0.854
mix3 extreme = 4.28795 (sig = 00122011) weight >=5 (4864), p-value = 0.084
bits per word = 64 (analyzing bits); min category p-value = 0.084

processed 1.75e+10 bytes in 5 seconds (3.502 GB/s, 12.61 TB/h). Fri Feb 21 23:22:50 2025

p = 0.355
------

mix3 extreme = 2.55481 (sig = 00020000) weight 1 (16), p-value = 0.157
mix3 extreme = 2.72892 (sig = 00001010) weight 2 (112), p-value = 0.51
mix3 extreme = 3.28466 (sig = 11000001) weight 3 (448), p-value = 0.367
mix3 extreme = 3.42088 (sig = 01020102) weight 4 (1120), p-value = 0.503
mix3 extreme = 4.05525 (sig = 00122011) weight >=5 (4864), p-value = 0.216
bits per word = 64 (analyzing bits); min category p-value = 0.157

processed 2e+10 bytes in 6 seconds (3.334 GB/s, 12 TB/h). Fri Feb 21 23:22:51 2025

p = 0.575
------

mix3 extreme = 2.46488 (sig = 00020000) weight 1 (16), p-value = 0.198
mix3 extreme = 2.44365 (sig = 20000200) weight 2 (112), p-value = 0.806
mix3 extreme = 3.31277 (sig = 02020010) weight 3 (448), p-value = 0.339
mix3 extreme = 3.81631 (sig = 01020102) weight 4 (1120), p-value = 0.141
mix3 extreme = 4.12544 (sig = 00122011) weight >=5 (4864), p-value = 0.165
bits per word = 64 (analyzing bits); min category p-value = 0.141

processed 2.5e+10 bytes in 8 seconds (3.127 GB/s, 11.26 TB/h). Fri Feb 21 23:22:53 2025

p = 0.532
------

mix3 extreme = 2.18715 (sig = 00020000) weight 1 (16), p-value = 0.373
mix3 extreme = 2.10006 (sig = 20000020) weight 2 (112), p-value = 0.983
mix3 extreme = 3.08063 (sig = 00210002) weight 3 (448), p-value = 0.604
mix3 extreme = 3.66924 (sig = 02110010) weight 4 (1120), p-value = 0.239
mix3 extreme = 4.35154 (sig = 00122011) weight >=5 (4864), p-value = 0.0636
bits per word = 64 (analyzing bits); min category p-value = 0.0636

processed 3e+10 bytes in 9 seconds (3.334 GB/s, 12 TB/h). Fri Feb 21 23:22:54 2025

p = 0.28
------

mix3 extreme = 1.43572 (sig = 00020000) weight 1 (16), p-value = 0.927
mix3 extreme = 2.41760 (sig = 02000020) weight 2 (112), p-value = 0.829
mix3 extreme = 3.16026 (sig = 02100020) weight 3 (448), p-value = 0.507
mix3 extreme = 3.44650 (sig = 02220002) weight 4 (1120), p-value = 0.471
mix3 extreme = 4.35459 (sig = 00221220) weight >=5 (4864), p-value = 0.0628
bits per word = 64 (analyzing bits); min category p-value = 0.0628

processed 4e+10 bytes in 12 seconds (3.334 GB/s, 12 TB/h). Fri Feb 21 23:22:57 2025

p = 0.277
------

mix3 extreme = 1.73103 (sig = 02000000) weight 1 (16), p-value = 0.752
mix3 extreme = 2.59948 (sig = 02000020) weight 2 (112), p-value = 0.65
mix3 extreme = 3.24793 (sig = 02020010) weight 3 (448), p-value = 0.406
mix3 extreme = 3.18550 (sig = 00022021) weight 4 (1120), p-value = 0.802
mix3 extreme = 4.36307 (sig = 00122011) weight >=5 (4864), p-value = 0.0605
bits per word = 64 (analyzing bits); min category p-value = 0.0605

processed 5e+10 bytes in 15 seconds (3.334 GB/s, 12 TB/h). Fri Feb 21 23:23:00 2025

p = 0.268
------

mix3 extreme = 1.69245 (sig = 02000000) weight 1 (16), p-value = 0.781
mix3 extreme = 2.90012 (sig = 02000020) weight 2 (112), p-value = 0.342
mix3 extreme = 3.04010 (sig = 02100020) weight 3 (448), p-value = 0.654
mix3 extreme = 3.18726 (sig = 11012000) weight 4 (1120), p-value = 0.8
mix3 extreme = 4.13605 (sig = 00122011) weight >=5 (4864), p-value = 0.158
bits per word = 64 (analyzing bits); min category p-value = 0.158

processed 6e+10 bytes in 18 seconds (3.334 GB/s, 12 TB/h). Fri Feb 21 23:23:03 2025

p = 0.577
------

mix3 extreme = 2.02538 (sig = 02000000) weight 1 (16), p-value = 0.504
mix3 extreme = 2.69836 (sig = 20000020) weight 2 (112), p-value = 0.543
mix3 extreme = 2.74898 (sig = 00200110) weight 3 (448), p-value = 0.932
mix3 extreme = 3.09844 (sig = 11012000) weight 4 (1120), p-value = 0.887
mix3 extreme = 4.04079 (sig = 20022201) weight >=5 (4864), p-value = 0.228
bits per word = 64 (analyzing bits); min category p-value = 0.228

processed 7e+10 bytes in 21 seconds (3.334 GB/s, 12 TB/h). Fri Feb 21 23:23:06 2025

p = 0.726
------

mix3 extreme = 2.00762 (sig = 00020000) weight 1 (16), p-value = 0.519
mix3 extreme = 2.67485 (sig = 20000020) weight 2 (112), p-value = 0.569
mix3 extreme = 2.98340 (sig = 00110100) weight 3 (448), p-value = 0.722
mix3 extreme = 3.30873 (sig = 12210000) weight 4 (1120), p-value = 0.65
mix3 extreme = 4.04171 (sig = 20022201) weight >=5 (4864), p-value = 0.227
bits per word = 64 (analyzing bits); min category p-value = 0.227

processed 8.5e+10 bytes in 25 seconds (3.4 GB/s, 12.24 TB/h). Fri Feb 21 23:23:10 2025

p = 0.725
------

mix3 extreme = 1.98503 (sig = 00100000) weight 1 (16), p-value = 0.538
mix3 extreme = 2.83929 (sig = 20000020) weight 2 (112), p-value = 0.398
mix3 extreme = 3.24294 (sig = 00110100) weight 3 (448), p-value = 0.412
mix3 extreme = 3.26307 (sig = 00022012) weight 4 (1120), p-value = 0.709
mix3 extreme = 3.82851 (sig = 20212120) weight >=5 (4864), p-value = 0.466
bits per word = 64 (analyzing bits); min category p-value = 0.398

processed 1e+11 bytes in 30 seconds (3.333 GB/s, 12 TB/h). Fri Feb 21 23:23:15 2025

p = 0.921
------

mix3 extreme = 1.80847 (sig = 00000001) weight 1 (16), p-value = 0.69
mix3 extreme = 2.36679 (sig = 00001020) weight 2 (112), p-value = 0.868
mix3 extreme = 3.12962 (sig = 00110100) weight 3 (448), p-value = 0.544
mix3 extreme = 3.41207 (sig = 00022012) weight 4 (1120), p-value = 0.514
mix3 extreme = 3.50238 (sig = 01120021) weight >=5 (4864), p-value = 0.894
bits per word = 64 (analyzing bits); min category p-value = 0.514

processed 1.25e+11 bytes in 37 seconds (3.378 GB/s, 12.16 TB/h). Fri Feb 21 23:23:22 2025

p = 0.973
------

mix3 extreme = 1.61617 (sig = 01000000) weight 1 (16), p-value = 0.834
mix3 extreme = 2.75953 (sig = 10002000) weight 2 (112), p-value = 0.478
mix3 extreme = 3.01768 (sig = 22000200) weight 3 (448), p-value = 0.681
mix3 extreme = 3.12351 (sig = 20120020) weight 4 (1120), p-value = 0.865
mix3 extreme = 3.88895 (sig = 01201202) weight >=5 (4864), p-value = 0.387
bits per word = 64 (analyzing bits); min category p-value = 0.387

processed 1.5e+11 bytes in 45 seconds (3.334 GB/s, 12 TB/h). Fri Feb 21 23:23:30 2025

p = 0.914
------

mix3 extreme = 1.77082 (sig = 10000000) weight 1 (16), p-value = 0.721
mix3 extreme = 3.16452 (sig = 20000020) weight 2 (112), p-value = 0.16
mix3 extreme = 3.33010 (sig = 22000200) weight 3 (448), p-value = 0.322
mix3 extreme = 3.30032 (sig = 10202001) weight 4 (1120), p-value = 0.661
mix3 extreme = 3.71439 (sig = 01201202) weight >=5 (4864), p-value = 0.629
bits per word = 64 (analyzing bits); min category p-value = 0.16

processed 1.75e+11 bytes in 52 seconds (3.366 GB/s, 12.12 TB/h). Fri Feb 21 23:23:37 2025

p = 0.581
------

mix3 extreme = 1.63286 (sig = 10000000) weight 1 (16), p-value = 0.823
mix3 extreme = 3.04031 (sig = 20000020) weight 2 (112), p-value = 0.233
mix3 extreme = 3.07386 (sig = 00020120) weight 3 (448), p-value = 0.612
mix3 extreme = 3.25670 (sig = 20200101) weight 4 (1120), p-value = 0.717
mix3 extreme = 3.86559 (sig = 02121212) weight >=5 (4864), p-value = 0.417
bits per word = 64 (analyzing bits); min category p-value = 0.233

processed 2e+11 bytes in 59 seconds (3.39 GB/s, 12.2 TB/h). Fri Feb 21 23:23:44 2025

p = 0.734
------

mix3 extreme = 1.16364 (sig = 00000010) weight 1 (16), p-value = 0.989
mix3 extreme = 3.24185 (sig = 20000020) weight 2 (112), p-value = 0.125
mix3 extreme = 3.33043 (sig = 00020120) weight 3 (448), p-value = 0.322
mix3 extreme = 3.08521 (sig = 01100011) weight 4 (1120), p-value = 0.898
mix3 extreme = 3.38515 (sig = 20222110) weight >=5 (4864), p-value = 0.969
bits per word = 64 (analyzing bits); min category p-value = 0.125

processed 2.5e+11 bytes in 74 seconds (3.378 GB/s, 12.16 TB/h). Fri Feb 21 23:23:59 2025

p = 0.486
------

mix3 extreme = 1.51793 (sig = 02000000) weight 1 (16), p-value = 0.89
mix3 extreme = 3.13632 (sig = 02000020) weight 2 (112), p-value = 0.175
mix3 extreme = 4.30962 (sig = 00020120) weight 3 (448), p-value = 0.0073
mix3 extreme = 3.03490 (sig = 01000212) weight 4 (1120), p-value = 0.933
mix3 extreme = 3.69122 (sig = 12111112) weight >=5 (4864), p-value = 0.662
bits per word = 64 (analyzing bits); min category p-value = 0.0073

processed 3e+11 bytes in 89 seconds (3.371 GB/s, 12.14 TB/h). Fri Feb 21 23:24:14 2025

p = 0.036
------

mix3 extreme = 1.90308 (sig = 00002000) weight 1 (16), p-value = 0.609
mix3 extreme = 2.65558 (sig = 00022000) weight 2 (112), p-value = 0.589
mix3 extreme = 3.65368 (sig = 00020120) weight 3 (448), p-value = 0.109
mix3 extreme = 3.49934 (sig = 01000212) weight 4 (1120), p-value = 0.407
mix3 extreme = 3.96134 (sig = 12111112) weight >=5 (4864), p-value = 0.304
bits per word = 64 (analyzing bits); min category p-value = 0.109

processed 4e+11 bytes in 119 seconds (3.361 GB/s, 12.1 TB/h). Fri Feb 21 23:24:44 2025

p = 0.44
------

mix3 extreme = 2.01103 (sig = 00000010) weight 1 (16), p-value = 0.516
mix3 extreme = 2.75092 (sig = 20000020) weight 2 (112), p-value = 0.487
mix3 extreme = 3.58533 (sig = 00020120) weight 3 (448), p-value = 0.14
mix3 extreme = 3.00752 (sig = 00101101) weight 4 (1120), p-value = 0.948
mix3 extreme = 3.78108 (sig = 20222110) weight >=5 (4864), p-value = 0.532
bits per word = 64 (analyzing bits); min category p-value = 0.14

processed 5e+11 bytes in 148 seconds (3.378 GB/s, 12.16 TB/h). Fri Feb 21 23:25:13 2025

p = 0.53
------

mix3 extreme = 2.46075 (sig = 00000010) weight 1 (16), p-value = 0.2
mix3 extreme = 3.05595 (sig = 10001000) weight 2 (112), p-value = 0.222
mix3 extreme = 3.57444 (sig = 00020120) weight 3 (448), p-value = 0.146
mix3 extreme = 3.08460 (sig = 02100120) weight 4 (1120), p-value = 0.898
mix3 extreme = 3.57242 (sig = 21020202) weight >=5 (4864), p-value = 0.821
bits per word = 64 (analyzing bits); min category p-value = 0.146

processed 6e+11 bytes in 178 seconds (3.371 GB/s, 12.14 TB/h). Fri Feb 21 23:25:43 2025

p = 0.544
------

mix3 extreme = 2.39889 (sig = 00010000) weight 1 (16), p-value = 0.233
mix3 extreme = 3.05936 (sig = 20000020) weight 2 (112), p-value = 0.22
mix3 extreme = 3.35611 (sig = 00020120) weight 3 (448), p-value = 0.298
mix3 extreme = 3.06385 (sig = 12002002) weight 4 (1120), p-value = 0.914
mix3 extreme = 3.77731 (sig = 21020202) weight >=5 (4864), p-value = 0.538
bits per word = 64 (analyzing bits); min category p-value = 0.22

processed 7e+11 bytes in 207 seconds (3.382 GB/s, 12.17 TB/h). Fri Feb 21 23:26:12 2025

p = 0.712
------

mix3 extreme = 2.44670 (sig = 00010000) weight 1 (16), p-value = 0.207
mix3 extreme = 3.17448 (sig = 00101000) weight 2 (112), p-value = 0.155
mix3 extreme = 3.94606 (sig = 00020120) weight 3 (448), p-value = 0.035
mix3 extreme = 3.39578 (sig = 12002002) weight 4 (1120), p-value = 0.535
mix3 extreme = 3.46995 (sig = 22000212) weight >=5 (4864), p-value = 0.921
bits per word = 64 (analyzing bits); min category p-value = 0.035

processed 8.5e+11 bytes in 252 seconds (3.373 GB/s, 12.14 TB/h). Fri Feb 21 23:26:57 2025

p = 0.163
------

mix3 extreme = 2.60423 (sig = 00000200) weight 1 (16), p-value = 0.138
mix3 extreme = 4.14356 (sig = 02000020) weight 2 (112), p-value = 0.00382
mix3 extreme = 3.30244 (sig = 00210010) weight 3 (448), p-value = 0.349
mix3 extreme = 2.92058 (sig = 00111100) weight 4 (1120), p-value = 0.98
mix3 extreme = 3.64298 (sig = 21221022) weight >=5 (4864), p-value = 0.73
bits per word = 64 (analyzing bits); min category p-value = 0.00382

processed 1e+12 bytes in 296 seconds (3.378 GB/s, 12.16 TB/h). Fri Feb 21 23:27:41 2025

p = 0.019
------

mix3 extreme = 2.39411 (sig = 00000200) weight 1 (16), p-value = 0.236
mix3 extreme = 3.86207 (sig = 02000020) weight 2 (112), p-value = 0.0125
mix3 extreme = 3.43732 (sig = 00210010) weight 3 (448), p-value = 0.231
mix3 extreme = 3.30612 (sig = 10010102) weight 4 (1120), p-value = 0.654
mix3 extreme = 3.69570 (sig = 02122102) weight >=5 (4864), p-value = 0.656
bits per word = 64 (analyzing bits); min category p-value = 0.0125

processed 1.25e+12 bytes in 370 seconds (3.378 GB/s, 12.16 TB/h). Fri Feb 21 23:28:55 2025

p = 0.061
------

mix3 extreme = 1.91917 (sig = 00000200) weight 1 (16), p-value = 0.595
mix3 extreme = 2.88512 (sig = 10001000) weight 2 (112), p-value = 0.355
mix3 extreme = 3.79690 (sig = 00210010) weight 3 (448), p-value = 0.0635
mix3 extreme = 3.81192 (sig = 10010102) weight 4 (1120), p-value = 0.143
mix3 extreme = 3.94746 (sig = 20221222) weight >=5 (4864), p-value = 0.319
bits per word = 64 (analyzing bits); min category p-value = 0.0635

processed 1.5e+12 bytes in 444 seconds (3.378 GB/s, 12.16 TB/h). Fri Feb 21 23:30:09 2025

p = 0.28
------

mix3 extreme = 1.91653 (sig = 00000200) weight 1 (16), p-value = 0.598
mix3 extreme = 3.17062 (sig = 02000020) weight 2 (112), p-value = 0.157
mix3 extreme = 3.50267 (sig = 00210010) weight 3 (448), p-value = 0.186
mix3 extreme = 4.12447 (sig = 10010102) weight 4 (1120), p-value = 0.0408
mix3 extreme = 4.27098 (sig = 20221222) weight >=5 (4864), p-value = 0.0903
bits per word = 64 (analyzing bits); min category p-value = 0.0408

processed 1.75e+12 bytes in 518 seconds (3.378 GB/s, 12.16 TB/h). Fri Feb 21 23:31:23 2025

p = 0.188
------

mix3 extreme = 1.81906 (sig = 00000200) weight 1 (16), p-value = 0.681
mix3 extreme = 3.06798 (sig = 10100000) weight 2 (112), p-value = 0.215
mix3 extreme = 3.71332 (sig = 02020200) weight 3 (448), p-value = 0.0876
mix3 extreme = 4.23944 (sig = 10010102) weight 4 (1120), p-value = 0.0248
mix3 extreme = 4.44624 (sig = 20221222) weight >=5 (4864), p-value = 0.0416
bits per word = 64 (analyzing bits); min category p-value = 0.0248

processed 2e+12 bytes in 592 seconds (3.378 GB/s, 12.16 TB/h). Fri Feb 21 23:32:37 2025

p = 0.118
------

mix3 extreme = 1.53717 (sig = 01000000) weight 1 (16), p-value = 0.88
mix3 extreme = 3.32377 (sig = 10100000) weight 2 (112), p-value = 0.0947
mix3 extreme = 3.12416 (sig = 02200001) weight 3 (448), p-value = 0.55
mix3 extreme = 3.46863 (sig = 02110100) weight 4 (1120), p-value = 0.443
mix3 extreme = 4.84658 (sig = 20221222) weight >=5 (4864), p-value = 0.00609
bits per word = 64 (analyzing bits); min category p-value = 0.00609

processed 2.5e+12 bytes in 740 seconds (3.378 GB/s, 12.16 TB/h). Fri Feb 21 23:35:05 2025

p = 0.0301
------

mix3 extreme = 1.61173 (sig = 00000200) weight 1 (16), p-value = 0.837
mix3 extreme = 3.37008 (sig = 10100000) weight 2 (112), p-value = 0.0807
mix3 extreme = 3.93348 (sig = 00202100) weight 3 (448), p-value = 0.0368
mix3 extreme = 3.29910 (sig = 02200021) weight 4 (1120), p-value = 0.663
mix3 extreme = 4.29826 (sig = 20221222) weight >=5 (4864), p-value = 0.0803
bits per word = 64 (analyzing bits); min category p-value = 0.0368

processed 3e+12 bytes in 887 seconds (3.382 GB/s, 12.18 TB/h). Fri Feb 21 23:37:32 2025

p = 0.171
------

mix3 extreme = 2.09163 (sig = 00000200) weight 1 (16), p-value = 0.448
mix3 extreme = 2.61382 (sig = 01100000) weight 2 (112), p-value = 0.635
mix3 extreme = 3.98155 (sig = 00202100) weight 3 (448), p-value = 0.0302
mix3 extreme = 3.38573 (sig = 00002211) weight 4 (1120), p-value = 0.549
mix3 extreme = 4.15291 (sig = 20211110) weight >=5 (4864), p-value = 0.148
bits per word = 64 (analyzing bits); min category p-value = 0.0302

processed 4e+12 bytes in 1.18e+03 seconds (3.381 GB/s, 12.17 TB/h). Fri Feb 21 23:42:28 2025

p = 0.142
------
```

------------------------------------------------------------

------------------------------------------------------------
