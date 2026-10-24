# Understanding the registers and instructions

Start here if the names in `decode.cpp` and `execute.cpp` are unfamiliar. This guide explains what
the guest instructions mean; the C++ comments explain how the simulator implements those meanings.
For assembling your own source, follow [Writing programs](WRITING_PROGRAMS.md).

## What is an instruction set?

An instruction set defines the operations a CPU understands and their effects on registers, memory,
and control flow. **RV32I** is the 32-bit base integer instruction set of RISC-V. `32` describes the
register width; `I` describes the integer base. It does not mean there are only 32 instructions.

The simulator is an ordinary C++ executable running on your Windows/Linux computer. Its guest
program is a separate collection of RISC-V machine instructions. Your host runs the C++ code; that
code interprets guest instruction bits. The guest cannot call C++ functions or Windows services
simply by executing a RISC-V instruction.

Assembly is a readable spelling of those instructions. An assembler converts `addi a0,zero,5` to
the 32-bit word `0x00500513`. The loader places its bytes in guest RAM. The decoder recognizes its
fields, execution computes the result, and commit writes 5 to register x10.

## Registers, RAM, and PC

There are **32 integer registers**, x0 through x31, each containing a 32-bit value. Registers are
small named storage locations used directly by arithmetic instructions. RAM is a larger array of
bytes; loads copy data from RAM into registers, and stores copy register bits into RAM.

The **program counter**, PC, is a separate address identifying an instruction. It is not x32 and
cannot be used as an ordinary register operand. Most instructions advance it by four bytes. A
taken branch or jump chooses a different next address. The debugger's architectural PC tracks
committed execution; pipeline fetch can be further ahead speculatively.

ABI names such as `a0` are aliases for register numbers, not extra registers:

| Number | ABI name | Conventional use |
|---|---|---|
| x0 | zero | Always zero; destination writes are discarded |
| x1 | ra | Return address written by a conventional function call |
| x2 | sp | Stack pointer; startup must initialize it |
| x3 | gp | Global pointer; not initialized by this simulator |
| x4 | tp | Thread pointer; not initialized by this simulator |
| x5, x6, x7 | t0, t1, t2 | Temporary registers; a called function may overwrite them |
| x8, x9 | s0/fp, s1 | Saved registers; a called function preserves them if it uses them |
| x10, x11 | a0, a1 | First two arguments; also return values |
| x12..x17 | a2..a7 | More argument registers |
| x18..x27 | s2..s11 | More saved registers |
| x28..x31 | t3..t6 | More temporary registers |

Except for x0, these are software conventions. The CPU does not automatically save s0 or allocate
a stack frame. Your function's instructions do that. `a0` is the result location in our demos by
convention; arithmetic instructions can write any register other than changing x0.

Reset starts all integer registers at zero, including sp. The supplied C startup sets sp to 0x10000
and decrements it before stack stores. Calling conventions and 16-byte stack alignment are covered
in [Writing programs](WRITING_PROGRAMS.md#functions-stack-and-freestanding-c).

## Reading instruction syntax

```asm
add  a0, t0, t1     # a0 receives t0 + t1; the two sources stay unchanged
addi t0, t0, -1     # t0 is both source and destination: replace it with its old value - 1
lw   a0, 4(t0)      # read four RAM bytes starting at byte address t0 + 4
sw   a0, 4(t0)      # write a0's four bytes there; a0 and t0 stay unchanged
```

The C++ decoder calls the destination **rd** and the source registers **rs1/rs2**. These fields hold
register *numbers*. The values read from those numbers are supplied to `execute()` as `a` and `b`.
An **immediate** is a constant carried inside the instruction, such as -1 or the address offset 4.

For example, `add a0,t0,t1` has rd=10, rs1=5, rs2=6. With t0=2 and t1=3, execution proposes the
value 5; commit writes it to x10. The register indices themselves are not added.

Arithmetic uses 32-bit wraparound: `0xffffffff + 1` becomes zero. The bits `0xffffffff` can mean
unsigned 4294967295 or signed -1. Signed/unsigned operations interpret the bits differently; they
do not switch a register into a permanent signed or unsigned mode.

Most I/S-type constants are signed 12-bit values, -2048 through 2047. The decoder **sign-extends**
them to 32 bits: a 12-bit -1 becomes `0xffffffff`. Shift-immediate counts instead range from 0 to
31. A larger constant or address usually needs multiple instructions, which an assembler can
generate using pseudoinstructions such as `li` and `la`.

## Arithmetic and comparisons

Each instruction in this table writes its first operand, rd, and otherwise advances PC by four.
Writing rd=x0 discards the result. All examples use real instruction mnemonics.

| Instruction | Meaning | Example |
|---|---|---|
| ADD | Add two register values, modulo 2^32 | `add a0,t0,t1` |
| SUB | Subtract rs2 from rs1, modulo 2^32 | `sub a0,t0,t1` |
| ADDI | Add a sign-extended immediate | `addi t0,t0,-1` |
| SLT | Write 1 if signed rs1 < signed rs2, otherwise 0 | `slt a0,t0,t1` |
| SLTU | Same test using unsigned values | `sltu a0,t0,t1` |
| SLTI | Signed less-than test against an immediate | `slti a0,t0,10` |
| SLTIU | Unsigned less-than test against a sign-extended immediate | `sltiu a0,t0,10` |

SLT and branches are different: SLT writes a 0/1 result and continues sequentially; BLT chooses
whether to jump and writes no result register. SLTIU still sign-extends its immediate first, so
`sltiu a0,t0,-1` compares t0 with unsigned `0xffffffff`, not with 4095.

## Bit operations and shifts

These also write rd. Each bitwise operation acts independently on all 32 bits.

| Instruction | Meaning | Example |
|---|---|---|
| AND | A bit is 1 only where both source bits are 1 | `and a0,t0,t1` |
| OR | A bit is 1 where either source bit is 1 | `or a0,t0,t1` |
| XOR | A bit is 1 where the source bits differ | `xor a0,t0,t1` |
| ANDI | AND with a sign-extended immediate; useful for masking | `andi a0,t0,255` |
| ORI | OR with a sign-extended immediate | `ori a0,t0,1` |
| XORI | XOR with a sign-extended immediate; -1 flips every bit | `xori a0,t0,-1` |
| SLL | Shift left by rs2's low five bits, filling with zeros | `sll a0,t0,t1` |
| SRL | Shift right by rs2's low five bits, filling with zeros | `srl a0,t0,t1` |
| SRA | Shift right by rs2's low five bits, copying the sign bit | `sra a0,t0,t1` |
| SLLI | Shift left by a constant count | `slli a0,t0,2` |
| SRLI | Logical right shift by a constant count | `srli a0,t0,2` |
| SRAI | Arithmetic right shift by a constant count | `srai a0,t0,2` |

For t0=`0xfffffff8` (-8), SRLI by 1 gives `0x7ffffffc`; SRAI by 1 gives `0xfffffffc` (-4).
For register shifts, a count of 33 acts as 1, because only the count's low five bits are used.

## Loads and stores

Every address is **base register + signed byte offset**. There is no automatic scaling by word
size. Advancing to the next four-byte array element requires adding four to its address.

| Instruction | Meaning | Example |
|---|---|---|
| LB | Read one byte and sign-extend it into rd | `lb a0,0(t0)` |
| LBU | Read one byte and zero-extend it into rd | `lbu a0,0(t0)` |
| LH | Read two bytes and sign-extend them into rd | `lh a0,2(t0)` |
| LHU | Read two bytes and zero-extend them into rd | `lhu a0,2(t0)` |
| LW | Read four bytes into rd | `lw a0,4(t0)` |
| SB | Write the low eight bits of rs2 to one byte | `sb a0,0(t0)` |
| SH | Write the low sixteen bits of rs2 to two bytes | `sh a0,2(t0)` |
| SW | Write all 32 bits of rs2 to four bytes | `sw a0,4(t0)` |

If memory contains byte `0x80`, LB gives `0xffffff80` (-128), while LBU gives `0x00000080` (128).
Little-endian RAM stores word `0x12345678` as bytes `78 56 34 12` at increasing addresses.

Halfwords must have addresses divisible by two; words must have addresses divisible by four.
The full access must fit RAM. Failed stores change no bytes. Stores do not modify their source
registers. A load targeting x0 still performs its access and can fault even though its result is
discarded. The pipeline validates accesses in MEM and commits stores in WB.

## Branches, jumps, and address construction

A label is a source-level name for an address. The assembler computes its encoded displacement.
Branch/JAL displacements are relative to the address of the branch/jump itself, not PC+4.

| Instruction | Meaning | Example |
|---|---|---|
| BEQ | Jump to label if rs1 == rs2 | `beq t0,t1,done` |
| BNE | Jump if rs1 != rs2 | `bne t0,zero,loop` |
| BLT | Jump if signed rs1 < signed rs2 | `blt t0,t1,smaller` |
| BGE | Jump if signed rs1 >= signed rs2 | `bge t0,t1,done` |
| BLTU | Jump if unsigned rs1 < unsigned rs2 | `bltu t0,t1,smaller` |
| BGEU | Jump if unsigned rs1 >= unsigned rs2 | `bgeu t0,t1,done` |
| JAL | Write PC+4 to rd, then jump to a PC-relative target | `jal ra,function` |
| JALR | Write PC+4 to rd, jump to (old rs1 + offset) with bit 0 cleared | `jalr zero,0(ra)` |
| LUI | Write a 20-bit constant into rd's upper bits; zero the lower twelve | `lui t0,0x2` gives 0x2000 |
| AUIPC | Add the shifted 20-bit constant to this instruction's PC | `auipc t0,0x1` adds 0x1000 |

Branches have no rd and leave all general registers unchanged. A not-taken branch continues at
PC+4. `jal zero,loop` jumps without saving a link. `jalr zero,0(ra)` is a conventional function
return, because a previous call stored its return address in ra. When rd and rs1 are the same
register in JALR, the target still uses the old source value.

JAL's signed displacement range is -1048576 through 1048574 bytes; conditional branches use -4096
through 4094 bytes, both encoded in multiples of two. This RV32I environment requires four-byte
instruction alignment: a taken target two bytes off alignment faults. A not-taken branch does not
fault on the unused target. JALR clears bit zero but must still pass the four-byte alignment check.

## Ordering, completion, and unsupported words

| Instruction/condition | Behavior in this simulator |
|---|---|
| FENCE, e.g. `fence rw,rw` | Ordinary memory-ordering instruction; no additional work in this single-hart uncached model |
| EBREAK | Normal demo completion **when it commits**; the instruction itself does not retire |
| ECALL | Terminal environment-call fault; no Linux/Windows syscall service is supplied |
| Unsupported/malformed encoding | Illegal-instruction terminal fault at commit |

FENCE.I/TSO, CSR/privileged instructions, compressed instructions, multiplication/division/remainder
extensions, floating point, atomics, interrupts, and OS services are outside the selected environment.
For example, `rem` is not available; the prime demo uses subtraction instead.

These terminal behaviors are environment conventions, not promises about every RISC-V system.
Speculative wrong-path EBREAK/ECALL/illegal instructions are discarded by an older redirect and
have no architectural effect. On a real terminal fault, older effects remain committed, the faulting
instruction applies no effects, and the report identifies its PC.

## Walk through a supplied program

[alu_chain.S](../demos/alu_chain.S) is small enough to follow without a cross-toolchain:

```asm
addi t0, zero, 1    # x5 = 1
addi t1, t0, 2      # x6 = 3
add  t2, t1, t0     # x7 = 4
add  a0, t2, t1     # x10 = 7
ebreak             # Complete after the four normal instructions commit
```

Run from the project root in PowerShell:

```powershell
.\build\rvsim.exe --program demos/prebuilt/alu_chain.elf --format elf --debug
```

Enter `step`, then `registers`; repeat to inspect each result. Register output uses x numbers,
so look for x5, x6, x7, and x10. `continue` executes the remaining instructions through completion.

| Boundary | PC shown after commit | New register value |
|---|---|---|
| Before the first instruction | 0x1000 | All registers are zero |
| First `step` | 0x1004 | x5/t0 = 1 |
| Second `step` | 0x1008 | x6/t1 = 3 |
| Third `step` | 0x100c | x7/t2 = 4 |
| Fourth `step` | 0x1010 | x10/a0 = 7 |
| EBREAK commits | 0x1010 | Completion; no new register write or normal retirement |

Pipeline mode has the same committed values. Add `--model pipeline --debug`, then use `tick` and
`stages` to see overlap, or `step` to move to the next architectural boundary. Forwarding supplies
the newest result to a dependent instruction before it has been written to the register file. The
supplied chain takes 9 total cycles with forwarding and 15 without, including completion/fill/drain.

## Read the C++ in this order

1. [types.hpp](../include/rv/types.hpp): `Word`, signed interpretation, sign extension, and registers.
2. [decode.hpp](../include/rv/decode.hpp): every operation's short meaning and decoded fields.
3. [decode.cpp](../src/decode.cpp): extracting bits without reading/changing any registers.
4. [execute.cpp](../src/execute.cpp): calculating an instruction's proposed `Effect`.
5. [machine.cpp](../src/machine.cpp): `functional_tick()` first, then `memory_access()` and `commit()`.
6. The same file's `pipeline_tick()`: scheduling those effects through current/next latches.
7. [main.cpp](../src/main.cpp): loading, tick/debug loop, breakpoint timing, and event observers.

For the example word `0x00500513`, the decoder finds opcode 0x13, funct3=0 (ADDI), rd=10,
rs1=0, and immediate=5. Execution computes 0+5. The effect is only a proposal until commit writes
x10 and sets the next architectural PC to old PC+4. That separation explains why the pipeline can
discard an instruction after decoding/executing it without having to undo a register write.

Tests for these rules are in [core_tests.cpp](../tests/core_tests.cpp). The specification/reference
scope is credited in [Upstream](UPSTREAM.md); this guide describes the implemented environment,
not a certified complete privileged RISC-V machine.
