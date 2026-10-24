# Writing RV32I programs

New to register names, instruction operands, or the difference between ADD and LW? Begin with the
[instruction and register reference](INSTRUCTION_REFERENCE.md), which includes every supported
instruction and a step-by-step debugger example.

## Registers and values

There are 32 general-purpose registers, each holding 32 bits. An instruction may interpret the same bits as signed or unsigned. `0xffffffff` means unsigned 4294967295 or signed -1. ADD/SUB wrap; they do not trap on overflow. This is different from overflowing a signed `int` expression in C, which should be avoided.

| Register | ABI name | Conventional role |
|---|---|---|
| x0 | zero | Constant zero |
| x1 | ra | Return address |
| x2 | sp | Stack pointer |
| x3, x4 | gp, tp | Global/thread pointer; no runtime sets these here |
| x5..x7 | t0..t2 | Temporary, caller-saved |
| x8, x9 | s0/fp, s1 | Callee-saved |
| x10..x17 | a0..a7 | Arguments; a0/a1 return values |
| x18..x27 | s2..s11 | Callee-saved |
| x28..x31 | t3..t6 | Temporary, caller-saved |

These roles are calling conventions, not special hardware behavior. The simulator only enforces x0. Its demos use a0 for primary scalar results and labeled memory for arrays.

## Syntax, data, and addresses

`add a0,t0,t1` adds two registers. `addi t0,t0,-1` uses a signed 12-bit immediate. `lw t1,0(t0)` reads the word at the byte address held in t0. `sw t1,4(t0)` stores at base+4. `.word` declares four-byte values; `.balign 4` ensures word alignment. A label gives the assembler/linker a symbolic address. RAM is byte-addressable, so moving to the next word requires adding four, not one.

`blt`/`bge` compare signed integers. `bltu`/`bgeu` compare unsigned integers. `slt` versus `sltu`, and `slti` versus `sltiu`, follow the same distinction. Even SLTIU's immediate is sign-extended before the unsigned comparison. `sra`/`srai` preserve the sign bit; `srl`/`srli` shift zeros in. Register shifts use only the low five bits of the shift count.

Branches name labels and have limited PC-relative range. A forward branch skips code; a backward branch forms a loop. Four-byte instruction alignment is required; a taken misaligned target faults on the branch/jump. JAL writes its sequential successor to rd and jumps; JALR adds an offset to rs1, clears bit zero, then jumps. `jal ra,function` calls a function, and `jalr zero,0(ra)` returns.

Pseudoinstructions are assembler conveniences. `li` may expand to one or more instructions, `la` normally expands to AUIPC+ADDI, `mv` is an ADDI with zero immediate, `ret` is JALR, and `j` is JAL to x0. Never count source lines as machine instructions. Demos disable compressed encodings and relaxation to keep layouts/timings reproducible. Inspect ELF disassembly with `riscv64-unknown-elf-objdump -d` when using GNU tools.

## A complete array-sum example

Create `sum.S` at the project root:

```asm
.option norvc
.option norelax
.section .text.start
.globl _start
_start:
    la t0, numbers
    addi t1, zero, 4
    addi a0, zero, 0
loop:
    lw t2, 0(t0)
    add a0, a0, t2
    addi t0, t0, 4
    addi t1, t1, -1
    bne t1, zero, loop
    ebreak
.section .data
.balign 4
numbers: .word -2, 5, 12, 15
```

The loop reads four words, accumulates 30 in a0, and terminates with EBREAK. It needs no stack because it calls no functions and uses only temporary/argument registers. The zero-length case should skip the loop; a reusable routine should check its length before entering. This fixed example intentionally uses a known nonempty allocation. The supplied [array_sum.S](../demos/array_sum.S) also computes a signed maximum and writes labeled results.

## GNU cross-toolchain workflow (Linux/WSL)

Example Debian/Ubuntu setup, not executed on this Windows host:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build python3 gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf
riscv64-unknown-elf-gcc --version
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -msmall-data-limit=0 -nostdlib -Wl,--no-relax -Wl,--build-id=none -T demos/link.ld sum.S -o sum.elf
./build/rvsim --program sum.elf --format elf
riscv64-unknown-elf-objcopy -O binary sum.elf sum.bin
./build/rvsim --program sum.bin --format bin --load-address 0x1000 --entry 0x1000
python scripts/build_demos.py --cc riscv64-unknown-elf-gcc
python scripts/run_demos.py --sim build/rvsim
```

The `riscv64` tool prefix can still generate RV32I with `-march=rv32i -mabi=ilp32`; our programs need no target libraries. The [linker script](../demos/link.ld) puts text at 0x1000 and data at 0x2000 and defines the stack top. ELF records segment addresses; raw bytes do not, so load/entry options are required to reproduce the map. Objcopy preserves any address gap as zero-filled raw bytes. Our builder performs the equivalent PT_LOAD extraction in Python.

Rebuilding with a different compiler can change code, layout, and timing while retaining correct results. `--check` is a byte comparison intended for the exact recorded toolchain. A GNU rebuild updates the manifest and should be followed by all tests and regenerated benchmarks. The tiny assembly timing assertions rely on the documented AUIPC+ADDI expansion.

## Reproducing the supplied artifacts with Zig 0.14.1

The supplied binaries were actually built with this portable Clang-based cross-toolchain. Install Zig 0.14.1 from its official distribution, or use its version-pinned Python packaging:

```sh
python -m pip install ziglang==0.14.1
python scripts/build_demos.py --zig /absolute/path/to/zig
python scripts/build_demos.py --zig /absolute/path/to/zig --check
python scripts/verify_binaries.py
```

On this Windows host, the tested command is:

```powershell
python scripts/build_demos.py --zig .tools/zig-x86_64-windows-0.14.1/zig.exe --check
```

`.tools` is an ignored local tools folder, not part of a clean checkout. Use your installed Zig path elsewhere. The builder selects `riscv32-freestanding-none`, `generic_rv32` (only I; Zig's `baseline_rv32` includes extra extensions), ILP32, no relaxation, no libc, and the supplied linker script. Source hashes, artifact hashes, compiler version, flags, and exact equivalent commands are recorded in [provenance.json](../demos/prebuilt/provenance.json). Hash consistency alone does not prove compilation; the separate byte-for-byte rebuild check was also run here.

## Functions, stack, and freestanding C

Callers must preserve any live t*/a* registers across calls; callees preserve s* registers they modify. A function that calls another must save its incoming ra before the nested call. Keep sp aligned to 16 bytes at call boundaries. For example, allocate a 16-byte frame with `addi sp,sp,-16`, save ra with `sw ra,12(sp)`, restore it before deallocating, then return. Stack accesses are checked like all RAM accesses.

[start.S](../demos/start.S) sets sp to the linker-defined aligned 0x10000, clears BSS bytewise, calls `main`, and EBREAKs with its return value still in a0. ELF loading already zeroes BSS; explicit clearing also supports raw startup. No process loader supplies a stack or arguments.

[freestanding.c](../demos/freestanding.c) uses a fixed volatile array and a BSS result. The builder uses `-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -msmall-data-limit=0 -nostdlib`. Restrict C to functions/data you supply. There is no libc, heap allocator, printf, constructors, exception runtime, TLS, or OS. Avoid compiler-generated helpers by staying within simple 32-bit integer operations. RV32I has no M extension: C multiplication/division may introduce unsupported instructions or unresolved library calls. The nth-prime demo instead implements divisibility with subtraction.

Bare-metal programs target this memory/startup/trap convention. A Linux RISC-V ELF expects syscalls, ABI startup state, and an operating system; loading its bytes does not make those services exist.
