# Iterative F(10)=55. Input a0; valid 0..46 (signed 32-bit result).
# Invalid input returns -1. Entry selects the checked input 10.
.option norvc
.option norelax
.section .text.start
.globl _start
_start:
    addi a0, zero, 10
    jal ra, fibonacci
    ebreak
fibonacci:
    blt a0, zero, invalid
    addi t0, zero, 46
    blt t0, a0, invalid
    addi t0, zero, 0             # previous F(0)
    addi t1, zero, 1             # current F(1)
loop:
    beq a0, zero, done
    add t2, t0, t1
    addi t0, t1, 0
    addi t1, t2, 0
    addi a0, a0, -1
    jal zero, loop
done:
    addi a0, t0, 0
    jalr zero, 0(ra)
invalid:
    addi a0, zero, -1
    jalr zero, 0(ra)
