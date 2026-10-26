#ifndef CORO_H
#define CORO_H

#include <stddef.h>

typedef struct coro_context {
    void *rbx;
    void *rbp;
    void *rdi;
    void *rsi;
    void *r12;
    void *r13;
    void *r14;
    void *r15;
    void *rsp;
    void *rip;
} coro_context;

typedef struct coro coro_t;

typedef void (*coro_func)(void *arg);

coro_t  *coro_create(coro_func func, void *arg, size_t stack_size);
void     coro_resume(coro_t *co);
void     coro_yield(void);
int      coro_alive(coro_t *co);
void     coro_destroy(coro_t *co);

void coro_switch(coro_context *from, coro_context *to);

#endif
