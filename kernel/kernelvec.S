        #
        # interrupts and exceptions while in supervisor
        # mode come here.
        #
        # the current stack is a kernel stack; the CPU has
        # already pushed ss, rsp, rflags, cs, rip, and perhaps
        # an error code. push the rest, call kerneltrap().
        # when kerneltrap() returns, restore registers, return.
        #

#include "x86.h"

.altmacro

        # one entry stub per trap vector, as in trampoline.S.
.macro kvec n
kvec\n:
  .if (\n == 8) || (\n == 10) || (\n == 11) || (\n == 12) || (\n == 13) || (\n == 14) || (\n == 17) || (\n == 21) || (\n == 29) || (\n == 30)
  .else
        pushq $0
  .endif
        pushq $\n
        jmp kernelvec
.endm

.macro kvecaddr n
        .quad kvec\n
.endm

.text
.set i, 0
.rept NIDT
        kvec %i
        .set i, i+1
.endr

.globl kerneltrap
.globl kernelvec
kernelvec:
        # save registers.
        pushq %rax
        pushq %rbx
        pushq %rcx
        pushq %rdx
        pushq %rbp
        pushq %rsi
        pushq %rdi
        pushq %r8
        pushq %r9
        pushq %r10
        pushq %r11
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15

        # call the C trap handler in trap.c:
        # kerneltrap(trapno, err, rip)
        movq 120(%rsp), %rdi
        movq 128(%rsp), %rsi
        movq 136(%rsp), %rdx
        call kerneltrap

        # restore registers.
        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %r11
        popq %r10
        popq %r9
        popq %r8
        popq %rdi
        popq %rsi
        popq %rbp
        popq %rdx
        popq %rcx
        popq %rbx
        popq %rax

        # skip trapno and err
        addq $16, %rsp

        # return to whatever we were doing in the kernel.
        iretq

        # the stub addresses, for trap.c's IDT.
.data
.globl kvectors
kvectors:
.set i, 0
.rept NIDT
        kvecaddr %i
        .set i, i+1
.endr
