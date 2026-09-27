# Context switch
#
#   void swtch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.	
#
# on x86-64 the return address is on the stack rather than in a
# register (RISC-V ra), so save it and the stack pointer as it
# will be after returning; restoring new then "returns" to new->ra.

.globl swtch
swtch:
        movq (%rsp), %rax
        movq %rax, 0(%rdi)
        leaq 8(%rsp), %rax
        movq %rax, 8(%rdi)
        movq %rbx, 16(%rdi)
        movq %rbp, 24(%rdi)
        movq %r12, 32(%rdi)
        movq %r13, 40(%rdi)
        movq %r14, 48(%rdi)
        movq %r15, 56(%rdi)

        movq 8(%rsi), %rsp
        movq 16(%rsi), %rbx
        movq 24(%rsi), %rbp
        movq 32(%rsi), %r12
        movq 40(%rsi), %r13
        movq 48(%rsi), %r14
        movq 56(%rsi), %r15
        
        jmp *0(%rsi)
