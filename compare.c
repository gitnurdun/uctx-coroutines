#include "coro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ======================================================================
 * Case 1 — Protocol parser
 *
 * Simulate a non-blocking stream that delivers bytes in chunks.
 * The protocol: "PRINT:message\n" and "ADD:a,b\n".
 *
 * Without coroutines: explicit state machine with manual buffering.
 * With coroutines:    clean sequential loop that "reads" by yielding.
 * ====================================================================== */

/* ---------- simulated chunked stream ---------- */
static const char *g_fragments[] = {
    "PRINT:hel", "lo\nPRIN", "T:world\n", "ADD:3,5\n", "PRINT:done\n", NULL
};

/* ========== APPROACH A: state machine (no coroutines) ========== */

typedef enum {
    PS_WAIT_CMD,
    PS_WAIT_ARG,
    PS_WAIT_NL,
} parser_state;

static char  sm_buf[64];
static int   sm_pos;

static void sm_reset(void) { sm_pos = 0; sm_buf[0] = '\0'; }

static void sm_parse(void) {
    sm_reset();
    parser_state st = PS_WAIT_CMD;
    char cmd[32] = {0};
    char arg[32] = {0};

    for (int fi = 0; g_fragments[fi]; fi++) {
        const char *chunk = g_fragments[fi];
        for (int ci = 0; chunk[ci]; ci++) {
            char c = chunk[ci];
            sm_buf[sm_pos++] = c;

            if (c == '\n') {
                sm_buf[sm_pos - 1] = '\0';
                /* parse cmd:arg */
                char *colon = strchr(sm_buf, ':');
                if (colon) {
                    *colon = '\0';
                    strcpy(cmd, sm_buf);
                    strcpy(arg, colon + 1);
                } else {
                    cmd[0] = '\0';
                }

                if (strcmp(cmd, "PRINT") == 0) {
                    printf("  SM: PRINT '%s'\n", arg);
                } else if (strcmp(cmd, "ADD") == 0) {
                    int a = 0, b = 0;
                    sscanf(arg, "%d,%d", &a, &b);
                    printf("  SM: ADD %d+%d=%d\n", a, b, a + b);
                }
                sm_reset();
            }
        }
    }
    (void)st;
}

/* ========== APPROACH B: coroutine parser ========== */

typedef struct {
    const char **frags;
    int fi, ci;
    int waiting;   /* 1 when the current chunk is exhausted */
} chunk_stream;

static chunk_stream g_cs;

static int stream_byte(char *out) {
    for (;;) {
        if (!g_cs.frags[g_cs.fi]) return 0;  /* EOF */
        char c = g_cs.frags[g_cs.fi][g_cs.ci];
        if (c) {
            *out = c;
            g_cs.ci++;
            return 1;
        }
        /* chunk exhausted → yield out so caller can resume us later */
        g_cs.fi++;
        g_cs.ci = 0;
        g_cs.waiting = 1;
        coro_yield();
    }
}

static void coro_parser(void *arg) {
    (void)arg;
    char buf[64];
    int pos = 0;

    for (;;) {
        char c;
        if (!stream_byte(&c)) break;  /* EOF */
        buf[pos++] = c;

        if (c == '\n') {
            buf[pos - 1] = '\0';
            char *colon = strchr(buf, ':');
            char cmd[32] = {0}, argument[32] = {0};
            if (colon) {
                *colon = '\0';
                strcpy(cmd, buf);
                strcpy(argument, colon + 1);
            }

            if (strcmp(cmd, "PRINT") == 0) {
                printf("  C: PRINT '%s'\n", argument);
            } else if (strcmp(cmd, "ADD") == 0) {
                int a = 0, b = 0;
                sscanf(argument, "%d,%d", &a, &b);
                printf("  C: ADD %d+%d=%d\n", a, b, a + b);
            }
            pos = 0;
        }
    }
}

static void test_parser(void) {
    printf("--- Protocol parser ---\n");

    printf("State machine approach:\n");
    sm_parse();

    printf("\nCoroutine approach:\n");
    g_cs.frags    = g_fragments;
    g_cs.fi       = 0;
    g_cs.ci       = 0;
    g_cs.waiting  = 0;
    coro_t *co = coro_create(coro_parser, NULL, 0);
    while (coro_alive(co)) coro_resume(co);
    coro_destroy(co);
    printf("\n");
}

/* ======================================================================
 * Case 2 — Tree traversal generator
 *
 * Lazily yield each node of a BST in-order.  Useful when you want to
 * walk a large tree without materialising the whole traversal in memory.
 *
 * Without coroutines: explicit stack-based iterator.
 * With coroutines:    recursive traversal + coro_yield.
 * ====================================================================== */

typedef struct tnode {
    int val;
    struct tnode *left, *right;
} tnode;

static tnode *make_node(int v) {
    tnode *n = calloc(1, sizeof(*n));
    n->val = v;
    return n;
}

static tnode *insert(tnode *r, int v) {
    if (!r) return make_node(v);
    if      (v < r->val) r->left  = insert(r->left,  v);
    else if (v > r->val) r->right = insert(r->right, v);
    return r;
}

static void free_tree(tnode *r) {
    if (!r) return;
    free_tree(r->left);
    free_tree(r->right);
    free(r);
}

/* ========== A: explicit stack iterator ========== */

typedef struct {
    tnode **stack;
    int cap, top;
} tree_iter;

static void ti_init(tree_iter *it, tnode *root, int cap) {
    it->stack = malloc((size_t)cap * sizeof(tnode *));
    it->cap   = cap;
    it->top   = 0;
    tnode *n  = root;
    while (n) { it->stack[it->top++] = n; n = n->left; }
}

static int ti_next(tree_iter *it) {
    if (it->top == 0) return 0;  /* done */
    tnode *n = it->stack[--it->top];
    int v = n->val;
    n = n->right;
    while (n) { it->stack[it->top++] = n; n = n->left; }
    return v;
}

static void ti_free(tree_iter *it) { free(it->stack); }

/* ========== B: coroutine generator ========== */

static tnode *g_root;
static int    g_yielded_val;

static void inorder_walk(tnode *n) {
    if (!n) return;
    inorder_walk(n->left);
    g_yielded_val = n->val;
    coro_yield();
    inorder_walk(n->right);
}

static void coro_traverse(void *arg) {
    (void)arg;
    inorder_walk(g_root);
}

/* ========== driver ========== */
static void test_tree(void) {
    printf("--- Tree traversal generator ---\n");

    int vals[] = {5, 3, 7, 2, 4, 6, 8, 1, 9};
    tnode *root = NULL;
    for (int i = 0; i < 9; i++) root = insert(root, vals[i]);

    printf("Explicit stack iterator:   ");
    tree_iter it;
    ti_init(&it, root, 64);
    for (int v = ti_next(&it); v; v = ti_next(&it))
        printf("%d ", v);
    ti_free(&it);
    printf("\n");

    printf("Coroutine generator:       ");
    g_root = root;
    coro_t *co = coro_create(coro_traverse, NULL, 0);
    while (coro_alive(co)) {
        coro_resume(co);
        if (coro_alive(co)) printf("%d ", g_yielded_val);
    }
    coro_destroy(co);
    printf("\n\n");

    free_tree(root);
}

/* ======================================================================
 * Case 3 — Game actor simulation
 *
 * Three actors in a game loop.  Each actor needs to "wait N frames"
 * between actions.  The main loop runs at 60 fps, calling each actor
 * once per frame.
 *
 * Without coroutines: explicit state enum + frame counter per actor.
 * With coroutines:    sequential code, coro_yield() = "wait one frame".
 * ====================================================================== */

/* ---------- shared frame counter ---------- */
static int g_frame;

/* ========== A: state machine actors ========== */

typedef enum {
    ACT_MOVE_RIGHT,
    ACT_MOVE_UP,
    ACT_SHOOT,
    ACT_IDLE,
    ACT_DONE,
} actor_state;

static actor_state sm_actor_st;
static int          sm_actor_wait;
static int          sm_actor_x, sm_actor_y;

static void sm_actor_init(void) {
    sm_actor_st  = ACT_MOVE_RIGHT;
    sm_actor_wait = 3;
    sm_actor_x    = 0;
    sm_actor_y    = 0;
}

static void sm_actor_tick(void) {
    if (sm_actor_st == ACT_DONE) return;
    if (sm_actor_wait > 0) { sm_actor_wait--; return; }

    switch (sm_actor_st) {
    case ACT_MOVE_RIGHT:
        sm_actor_x += 10;
        printf("  SM actor: move right → (%d,%d) [frame %d]\n",
               sm_actor_x, sm_actor_y, g_frame);
        sm_actor_st   = ACT_MOVE_UP;
        sm_actor_wait = 2;
        break;
    case ACT_MOVE_UP:
        sm_actor_y += 5;
        printf("  SM actor: move up   → (%d,%d) [frame %d]\n",
               sm_actor_x, sm_actor_y, g_frame);
        sm_actor_st   = ACT_SHOOT;
        sm_actor_wait = 1;
        break;
    case ACT_SHOOT:
        printf("  SM actor: shoot!    → (%d,%d) [frame %d]\n",
               sm_actor_x, sm_actor_y, g_frame);
        sm_actor_st   = ACT_IDLE;
        sm_actor_wait = 2;
        break;
    case ACT_IDLE:
        printf("  SM actor: idle            [frame %d]\n", g_frame);
        sm_actor_st = ACT_DONE;
        break;
    default: break;
    }
}

/* ========== B: coroutine actors ========== */

static int g_cx, g_cy;

static void cr_wait(int frames) {
    for (int i = 0; i < frames; i++) coro_yield();
}

static void cr_enemy(void *arg) {
    (void)arg;
    int ex = 100;
    for (int i = 0; i < 2; i++) {
        ex -= 5;
        printf("  enemy: patrol left  → (%d)  [frame %d]\n", ex, g_frame);
        cr_wait(2);
        ex += 5;
        printf("  enemy: patrol right → (%d)  [frame %d]\n", ex, g_frame);
        cr_wait(2);
    }
}

static void cr_collectible(void *arg) {
    (void)arg;
    for (int i = 0; i < 3; i++) {
        printf("  coin: visible              [frame %d]\n", g_frame);
        cr_wait(1);
        printf("  coin: hidden               [frame %d]\n", g_frame);
        cr_wait(1);
    }
    printf("  coin: collected             [frame %d]\n", g_frame);
}

/* ========== driver ========== */

static void test_actors(void) {
    printf("--- Game actor simulation ---\n");

    /* state-machine version */
    printf("State machine actor:\n");
    sm_actor_init();
    for (g_frame = 0; sm_actor_st != ACT_DONE; g_frame++)
        sm_actor_tick();

    /* coroutine version */
    printf("\nCoroutine actors:\n");
    g_cx = 0; g_cy = 0;
    coro_t *enemy = coro_create(cr_enemy, NULL, 0);
    coro_t *coin  = coro_create(cr_collectible, NULL, 0);
    for (g_frame = 0; coro_alive(enemy) || coro_alive(coin); g_frame++) {
        if (coro_alive(enemy)) coro_resume(enemy);
        if (coro_alive(coin))  coro_resume(coin);
    }
    coro_destroy(enemy);
    coro_destroy(coin);
    printf("\n");
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(void) {
    test_parser();
    test_tree();
    test_actors();
    printf("Done.\n");
    return 0;
}
