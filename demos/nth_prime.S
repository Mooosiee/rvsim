# n=1 means 2; nth_prime(10)=29. Valid n=1..25; otherwise return -1.
# Trial division uses repeated subtraction: RV32I, no DIV/REM or M extension.
.option norvc
.option norelax
.section .text.start
.globl _start
_start:
    addi a0, zero, 10
    addi t4, a0, 0
    bge zero, a0, invalid
    addi t0, zero, 25
    blt t0, a0, invalid
    addi t0, zero, 2             # candidate
    addi t1, zero, 0             # primes found
candidate:
    addi t2, zero, 2             # divisor
divide:
    beq t2, t0, prime
    addi t3, t0, 0               # remainder
subtract:
    bltu t3, t2, remainder
    sub t3, t3, t2
    jal zero, subtract
remainder:
    beq t3, zero, composite
    addi t2, t2, 1
    jal zero, divide
prime:
    addi t1, t1, 1
    beq t1, t4, done
composite:
    addi t0, t0, 1
    jal zero, candidate
done:
    addi a0, t0, 0
    ebreak
invalid:
    addi a0, zero, -1
    ebreak
