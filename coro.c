#include "coro.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CORO_DEAD  0
#define CORO_ALIVE 1

struct coro {
    coro_context ctx;
    void        *stack;
    size_t       stack_size;
    coro_func    func;
    void        *arg;
    int          state;
    coro_t      *caller;
};

static coro_t g_main_ctx;
static coro_t *g_current = &g_main_ctx;

static void coro_trampoline(void);

coro_t *coro_create(coro_func func, void *arg, size_t stack_size) {
    if (stack_size == 0) stack_size = 65536;

    coro_t *co = (coro_t *)calloc(1, sizeof(coro_t));
    if (!co) return NULL;

    co->stack = malloc(stack_size);
    if (!co->stack) {
        free(co);
        return NULL;
    }

    co->stack_size = stack_size;
    co->func       = func;
    co->arg        = arg;
    co->state      = CORO_ALIVE;

    uintptr_t sp = (uintptr_t)co->stack + stack_size;
    sp = (sp & ~(uintptr_t)15) - 8;

    co->ctx.rsp = (void *)sp;
    co->ctx.rip = (void *)coro_trampoline;

    return co;
}

void coro_resume(coro_t *co) {
    if (!co || co->state == CORO_DEAD) return;
    co->caller = g_current;
    g_current  = co;
    coro_switch(&co->caller->ctx, &co->ctx);
}

void coro_yield(void) {
    coro_t *curr   = g_current;
    coro_t *caller = curr->caller;
    if (!caller) return;
    g_current = caller;
    coro_switch(&curr->ctx, &caller->ctx);
}

int coro_alive(coro_t *co) {
    return co && co->state == CORO_ALIVE;
}

void coro_destroy(coro_t *co) {
    if (!co) return;
    free(co->stack);
    free(co);
}

static void coro_trampoline(void) {
    coro_t *co = g_current;
    co->func(co->arg);
    co->state  = CORO_DEAD;
    g_current  = co->caller;
    coro_switch(&co->ctx, &co->caller->ctx);
}
