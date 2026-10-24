# Both wrong-path store and EBREAK are discarded. sentinel remains 0; a0=42.
# Two branches ensure each kind of wrong-path instruction occupies IF/ID.
.option norvc
.option norelax
.section .text.start
.globl _start
_start:
    la t0, sentinel
    addi t1, zero, 99
    beq zero, zero, safe
    sw t1, 0(t0)
    ebreak
safe:
    beq zero, zero, done
    ebreak
done:
    addi a0, zero, 42
    ebreak
.section .data
.balign 4
.globl sentinel
sentinel: .word 0
