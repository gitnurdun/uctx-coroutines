# uctx - Minimal Coroutines in C + Assembly

A lightweight asymmetric (stackful) coroutine library for x86-64 Windows,
using the C standard library and a single assembly function for context
switching.

## Overview

Coroutines are functions that can pause execution mid-way and resume later,
without blocking the main program. Unlike threads, only one coroutine runs
at a time — they *cooperate* by explicitly yielding control.

This library implements **stackful** coroutines: each coroutine gets its
own stack (64 KB by default), so it can yield from anywhere in the call
chain, not just from the top-level function.

## Files

| File | Role |
|------|------|
| `coro.h` | Public API and context structure |
| `coro.c` | Coroutine lifecycle (create, resume, yield, destroy) |
| `coro_switch.S` | Assembly context switch (29 instructions) |
| `test.c` | Two demo tests |
| `build.ps1` | PowerShell build & run script |

## API

```c
coro_t *coro_create(coro_func func, void *arg, size_t stack_size);
void    coro_resume(coro_t *co);
void    coro_yield(void);
int     coro_alive(coro_t *co);
void    coro_destroy(coro_t *co);
```

### coro_create

Allocates a coroutine with its own stack. Pass `0` for the default
64 KB stack.

```c
coro_t *co = coro_create(my_func, &some_arg, 0);
```

### coro_resume

Switches execution to the coroutine. If this is the first resume, the
coroutine's function starts running. If the coroutine previously yielded,
execution picks up right after that `coro_yield()` call.

```c
coro_resume(co);   // start or continue the coroutine
```

### coro_yield

Pauses the current coroutine and returns control to whoever resumed it.
Must be called from inside a coroutine (not from `main`).

```c
void my_func(void *arg) {
    for (int i = 0; i < 5; i++) {
        printf("step %d\n", i);
        coro_yield();           // <-- pause here, resume later
    }
}
```

### coro_alive

Returns 1 if the coroutine is still runnable, 0 if it has finished.

### coro_destroy

Frees the coroutine's stack and handle. Call after the coroutine is dead.

## How it works

### Context structure (`coro.h:6-16`)

```c
typedef struct coro_context {
    void *rbx, *rbp, *rdi, *rsi;   // callee-saved GPRs
    void *r12, *r13, *r14, *r15;
    void *rsp;                      // stack pointer
    void *rip;                      // instruction pointer
} coro_context;                     // 10 x 8 = 80 bytes
```

This matches the Microsoft x64 calling convention. We save every register
the *callee* must preserve, plus the stack and instruction pointers.

### Assembly context switch (`coro_switch.S`)

```
coro_switch(coro_context *from, coro_context *to)
```

The function:
1. Saves all callee-saved registers + RSP + return-address into `*from`
2. Loads the same set from `*to`
3. Jumps to `to->rip`

The critical detail is **line 23**: we save `RSP + 8` (not `RSP`).
The `call` instruction that got us into `coro_switch` pushed an 8-byte
return address onto the stack. Saving `RSP + 8` skips past that slot,
so after the jump the stack is at the correct position. Without this
adjustment, each context switch leaks 8 bytes of stack space.

```
coro_switch:
    ; ── save ──────────────────────────────
    movq  %rbx,   0(%rcx)
    ...
    leaq  8(%rsp), %rax          ; RSP+8 → skip return-address slot
    movq  %rax,  64(%rcx)
    movq  (%rsp), %rax           ; return address
    movq  %rax,  72(%rcx)
    ; ── load ──────────────────────────────
    movq   0(%rdx), %rbx
    ...
    movq  64(%rdx), %rsp
    movq  72(%rdx), %rax
    jmp   *%rax
```

### Coroutine lifecycle

1. `coro_create` allocates a stack and sets the initial context:
   - `ctx.rsp` = top of stack (16-byte aligned, minus 8 for call-entry convention)
   - `ctx.rip` = address of `coro_trampoline`

2. `coro_resume` saves the caller's context into the coroutine's `->caller`
   field, then calls `coro_switch` to enter the coroutine.

3. The **trampoline** calls the user's function. When the function returns,
   the trampoline marks the coroutine dead and switches back to the caller.

4. `coro_yield` saves the coroutine's context back into its `ctx` field
   and switches to the caller's context.

```
main                          coroutine
  │                              │
  ├─ coro_resume(co) ───────────►│
  │  (saves main ctx)            ├─ trampoline
  │                              ├─ user_func()
  │                              │    coro_yield()
  │◄─────────────────────────────┤    (saves co ctx, restores main)
  │                              │
  ├─ coro_resume(co) ───────────►│
  │                              ├─ user_func()  (continues)
  │                              │    return
  │                              ├─ trampoline: co->state = DEAD
  │◄─────────────────────────────┤    switches back to main
  │                              │
  ├─ coro_alive(co) → 0         ✗
```

### Stack layout

```
 high   ┌──────────────────┐  stack + size
        │                  │
        │   usable stack   │  ← coroutine's own 64 KB
        │   (grows down)   │
        │                  │
        ├──────────────────┤  sp = (stack + size - 8) & ~15
        │ trampoline addr  │  (stored in ctx.rip, not on stack)
 low    └──────────────────┘  stack
```

The initial RSP is set to 8 mod 16, mimicking a normal function entry
after `call` pushes the return address. The trampoline's prologue
(`push rbp`) then restores 16-byte alignment.

## Requirements

- Windows x86-64
- GCC (MinGW-W64) — the assembly uses GNU assembler syntax
- Or MSVC with the included MASM reference (see `coro_switch.S` comments)

## Build & Run

```cmd
build.bat
```

Or manually:

```powershell
gcc -Wall -O2 -o test.exe test.c coro.c coro_switch.S
.\test.exe
```

Clean build artifacts:

```cmd
build.bat clean
```

## Test output

```
=== Round-robin coroutine test ===
  coroutine 1: step 1
  coroutine 2: step 1
  coroutine 1: step 2
  coroutine 2: step 2
  coroutine 1: step 3
  coroutine 2: step 3
  coroutine 1: finished
  coroutine 2: finished

=== Producer-consumer coroutine test ===
  producer: sent 1
  consumer: received 1
  producer: sent 2
  consumer: received 2
  ...
  producer: done
  consumer: done

All tests passed.
```

## Porting to Linux / System V ABI

For Linux x86-64, `coro_switch.S` needs these changes:

- First argument moves from `RCX` → `RDI`, second from `RDX` → `RSI`
- `RDI` and `RSI` are **not** callee-saved on System V — drop them
  from the context struct and the save/restore in assembly
- The trampoline entry alignment may differ (check your compiler)

## License

Public domain / CC0. Use however you like.
