#include "coro.h"
#include <stdio.h>
#include <stdlib.h>

static void func1(void *arg) {
    int n = *(int *)arg;
    for (int i = 0; i < 3; i++) {
        printf("  coroutine %d: step %d\n", n, i + 1);
        coro_yield();
    }
    printf("  coroutine %d: finished\n", n);
}

static void producer(void *arg) {
    int *shared = (int *)arg;
    for (int i = 1; i <= 5; i++) {
        *shared = i;
        printf("  producer: sent %d\n", i);
        coro_yield();
    }
    *shared = -1;
    printf("  producer: done\n");
}

static void consumer(void *arg) {
    int *shared = (int *)arg;
    for (;;) {
        if (*shared == -1) break;
        if (*shared > 0) {
            printf("  consumer: received %d\n", *shared);
            *shared = 0;
        }
        coro_yield();
    }
    printf("  consumer: done\n");
}

int main(void) {
    printf("=== Round-robin coroutine test ===\n");
    int id1 = 1, id2 = 2;
    coro_t *co1 = coro_create(func1, &id1, 0);
    coro_t *co2 = coro_create(func1, &id2, 0);

    while (coro_alive(co1) || coro_alive(co2)) {
        if (coro_alive(co1)) { coro_resume(co1); }
        if (coro_alive(co2)) { coro_resume(co2); }
    }

    coro_destroy(co1);
    coro_destroy(co2);

    printf("\n=== Producer-consumer coroutine test ===\n");
    int shared = 0;
    coro_t *prod = coro_create(producer, &shared, 0);
    coro_t *cons = coro_create(consumer, &shared, 0);

    while (coro_alive(prod) || coro_alive(cons)) {
        if (coro_alive(prod)) { coro_resume(prod); }
        if (coro_alive(cons)) { coro_resume(cons); }
    }

    coro_destroy(prod);
    coro_destroy(cons);

    printf("\nAll tests passed.\n");
	while(1){}
    return 0;
}
