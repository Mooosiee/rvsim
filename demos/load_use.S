# Adjacent LW -> ADD requires one bubble even with forwarding. a0=42.
.option norvc
.option norelax
.section .text.start
.globl _start
_start:
    la t0, value
    lw t1, 0(t0)
    add a0, t1, t1
    ebreak
.section .data
.balign 4
.globl value
value: .word 21
