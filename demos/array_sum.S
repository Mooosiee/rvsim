# Seven signed words; sum=3 in a0 and sum_result, max=9 in a1/max_result.
# Fixed nonempty input; for reusable routine length <=0 returns a0=-1.
.option norvc
.option norelax
.section .text.start
.globl _start
_start:
    la t0, input
    addi t1, zero, 7
    bge zero, t1, invalid
    addi a0, zero, 0
    lw a1, 0(t0)
loop:
    lw t2, 0(t0)
    add a0, a0, t2
    bge a1, t2, keep
    addi a1, t2, 0
keep:
    addi t0, t0, 4
    addi t1, t1, -1
    bne t1, zero, loop
    la t0, sum_result
    sw a0, 0(t0)
    sw a1, 4(t0)
    ebreak
invalid:
    addi a0, zero, -1
    ebreak
.section .data
.balign 4
.globl input, sum_result, max_result
input: .word -7, 4, 0, -2, 9, 9, -10
sum_result: .word 0
max_result: .word 0
