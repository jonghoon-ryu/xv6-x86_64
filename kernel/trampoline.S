        #
        # low-level code to handle traps from user space into
        # the kernel, and returns from kernel to user.
        #
        # the kernel maps the page holding this code
        # at the same virtual address (TRAMPOLINE)
        # in user and kernel space so that it continues
        # to work when it switches page tables.
        # kernel.ld causes this code to start at
        # a page boundary.
        #

#include "x86.h"
#include "memlayout.h"

.altmacro

        # one entry stub per trap vector. the IDT that trap.c
        # installs while a process runs in user space points here.
        # x86 does not tell the handler which vector it came
        # through, so each stub pushes its vector number.
        # the CPU pushes an error code for some exceptions;
        # for the rest, push a 0 so every trapframe looks alike.
.macro uvec n
uvec\n:
  .if (\n == 8) || (\n == 10) || (\n == 11) || (\n == 12) || (\n == 13) || (\n == 14) || (\n == 17) || (\n == 21) || (\n == 29) || (\n == 30)
  .else
        pushq $0
  .endif
        pushq $\n
        jmp uservec
.endm

.macro uvecaddr n
        .quad uvec\n
.endm

.section trampsec, "ax"
.globl trampoline
.globl usertrap
trampoline:

.set i, 0
.rept NIDT
        uvec %i
        .set i, i+1
.endr

.globl uservec
uservec:
        #
        # traps from user space arrive here, in supervisor mode,
        # but with a user page table.
        #
        # the TSS told the CPU to use the end of TRAPFRAME as
        # the stack, so the CPU and the vector stub have already
        # saved ss, rsp, rflags, cs, rip, err, and trapno in
        # p->trapframe. each process has a separate p->trapframe
        # memory area, but it's mapped to the same virtual
        # address (TRAPFRAME) in every process's user page table.
        #

        # save the user registers in TRAPFRAME
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

        # now %rsp is TRAPFRAME+32, just above the kernel_* fields.

        # the kernel's C code expects the direction flag to be
        # clear, and a user program may have set it.
        cld

        # make GS base hold the current cpu id,
        # from p->trapframe->kernel_hartid
        movq -8(%rsp), %rax
        movq %rax, %rdx
        shrq $32, %rdx
        movl $MSR_GS_BASE, %ecx
        wrmsr

        # load the address of usertrap(), from p->trapframe->kernel_trap
        movq -16(%rsp), %rbx

        # fetch the kernel page table address, from p->trapframe->kernel_cr3.
        movq -32(%rsp), %rcx

        # initialize kernel stack pointer, from p->trapframe->kernel_sp
        movq -24(%rsp), %rsp

        # install the kernel page table.
        # writing cr3 also flushes the TLB.
        movq %rcx, %cr3

        # call usertrap()
        call *%rbx

        # usertrap() returns here, with user cr3 in %rax.
        movq %rax, %rdi

.globl userret
userret:
        # usertrap() and forkret() come here, with user cr3 in %rdi.
        # return from kernel to user.

        # switch to the user page table.
        movq %rdi, %cr3

        # restore all registers from TRAPFRAME
        movabsq $(TRAPFRAME+32), %rsp
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

        # return to user mode and user pc.
        # prepare_return() set up cs, ss, and rflags.
        iretq

        # the stub addresses, for trap.c's IDT. these are kernel
        # addresses; trap.c converts them to TRAMPOLINE addresses.
.data
.globl uvectors
uvectors:
.set i, 0
.rept NIDT
        uvecaddr %i
        .set i, i+1
.endr
