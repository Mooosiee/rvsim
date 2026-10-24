# Signed in-place sort of [-3,7,0,-3,2,-9,7]. a0=0 on success.
# Length 0/1 succeeds; negative or length > allocation (7) returns -1.
.option norvc
.option norelax
.section .text.start
.globl _start
_start:
    la a1, array
    addi a2, zero, 7
    blt a2, zero, invalid
    addi t0, zero, 7
    blt t0, a2, invalid
    addi t0, a2, -1
    bge zero, t0, done
outer:
    addi t1, zero, 0
    addi t2, a1, 0
inner:
    lw t3, 0(t2)
    lw t4, 4(t2)
    bge t4, t3, ordered
    sw t4, 0(t2)
    sw t3, 4(t2)
ordered:
    addi t2, t2, 4
    addi t1, t1, 1
    blt t1, t0, inner
    addi t0, t0, -1
    bne t0, zero, outer
done:
    addi a0, zero, 0
    ebreak
invalid:
    addi a0, zero, -1
    ebreak
.section .data
.balign 4
.globl array
array: .word -3, 7, 0, -3, 2, -9, 7
