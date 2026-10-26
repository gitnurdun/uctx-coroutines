# Context switch for x86-64 Windows (Microsoft x64 calling convention)
# void coro_switch(coro_context *from (rcx), coro_context *to (rdx))
#
# Saves callee-saved registers + rsp + return-address to [from],
# loads them from [to], then jumps to the saved rip.
#
# We save RSP+8 (skipping past the return-address slot that the "call"
# pushed) so that after the jmp the stack is correctly positioned.

    .text
    .globl coro_switch

coro_switch:
    # Save callee-saved registers to [from] (rcx)
    movq    %rbx,   0(%rcx)
    movq    %rbp,   8(%rcx)
    movq    %rdi,  16(%rcx)
    movq    %rsi,  24(%rcx)
    movq    %r12,  32(%rcx)
    movq    %r13,  40(%rcx)
    movq    %r14,  48(%rcx)
    movq    %r15,  56(%rcx)
    # Save RSP+8  (past the return-address slot pushed by "call")
    leaq    8(%rsp), %rax
    movq    %rax,  64(%rcx)
    # Save return address
    movq    (%rsp), %rax
    movq    %rax,  72(%rcx)

    # Load callee-saved registers from [to] (rdx)
    movq    0(%rdx), %rbx
    movq    8(%rdx), %rbp
    movq   16(%rdx), %rdi
    movq   24(%rdx), %rsi
    movq   32(%rdx), %r12
    movq   40(%rdx), %r13
    movq   48(%rdx), %r14
    movq   56(%rdx), %r15
    movq   64(%rdx), %rsp
    # Jump to saved instruction pointer
    movq   72(%rdx), %rax
    jmp     *%rax
