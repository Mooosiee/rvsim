# Freestanding C entry: 16-byte aligned stack; no OS, libc, constructors, or TLS.
# Loader zeroes .bss, but clear it here too so startup works for raw images.
.option norvc
.option norelax
.section .text.start
.globl _start
_start:
    la sp, __stack_top
    la t0, __bss_start
    la t1, __bss_end
clear:
    bgeu t0, t1, cleared
    sb zero, 0(t0)
    addi t0, t0, 1
    jal zero, clear
cleared:
    jal ra, main
    ebreak
