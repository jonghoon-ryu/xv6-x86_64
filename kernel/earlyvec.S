        # [platform: real PC] early exception stubs, until trap.cpp
        # brings back the real IDT. without an IDT, any CPU exception
        # (a bad pointer, a page the firmware did not map) turns into
        # a triple fault, and a real PC silently reboots. with these,
        # earlytrap() prints what happened on the screen instead.
        #
        # one 16-byte stub per vector 0-31: push a dummy error code if
        # the CPU did not push one, push the vector number, and go to
        # the common code. earlytrapinit() points the IDT at them.
.text
.altmacro
.macro vec n
        .align 16
        .if (\n == 8 || (\n >= 10 && \n <= 14) || \n == 17 || \n == 21 || \n == 29 || \n == 30)
        .else
        pushq $0
        .endif
        pushq $\n
        jmp earlycommon
.endm

.align 16
.global earlyvectors
earlyvectors:
.set i, 0
.rept 32
        vec %i
        .set i, i+1
.endr

earlycommon:
        # %rsp points at: vector, error code, rip, cs, rflags, rsp, ss
        movq %rsp, %rdi
        andq $-16, %rsp
        call earlytrap
1:
        hlt
        jmp 1b
