# Four ALU instructions, a0=7. Forwarding removes all RAW stalls.
.option norvc
.section .text.start
.globl _start
_start:
    addi t0, zero, 1
    addi t1, t0, 2
    add t2, t1, t0
    add a0, t2, t1
    ebreak
