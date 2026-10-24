# Architecture and timing contract

For instruction meanings and register roles before implementation details, read the
[instruction reference and source-reading order](INSTRUCTION_REFERENCE.md).

## Architecture before scheduling

An instruction says how visible machine state changes. For example, `add x3,x1,x2` reads two registers and writes their 32-bit sum into a third. A pipeline changes when that result becomes available, but must preserve the same visible behavior. **Retirement** means an instruction has reached the boundary where its architectural effects become permanent.

[types.hpp](../include/rv/types.hpp) defines `Word` and `Address` as unsigned 32-bit integers, `RegisterFile`, and terminal fault records. `signed_value()` interprets the same bits as a signed value without relying on host signed overflow or implementation-defined conversions. Register x0 always reads zero; writes are ignored. Reset clears all registers, counters, latches, and terminal state. The core reset preserves RAM; the CLI debugger restores its saved initial image as well.

[Memory](../include/rv/memory.hpp) owns a vector of bytes. Address 0x2000 is an index in guest RAM, never a host pointer. A word occupies four bytes, least significant byte first. [memory.cpp](../src/memory.cpp) validates width, natural alignment, and the entire range using a widened calculation before reading/writing any byte. Failed stores cannot partially change RAM. Alignment wins over access bounds when both fail. Instruction and data ports use the same RAM, but have no structural port conflict.

Default RAM spans [0,65536). The demos reserve code at 0x1000, data/BSS at 0x2000, and a downward-growing stack whose initial pointer is 0x10000. Stack top is an address just beyond RAM; decrement it before storing. RAM has no page permissions or protected code region. Programs must not modify instructions they might execute; FENCE.I and coherent self-modification are unsupported.

## Interfaces and ownership

| File | Responsibility |
|---|---|
| [decode.hpp](../include/rv/decode.hpp), [decode.cpp](../src/decode.cpp) | Turn a word into an operation, immediate, registers, source-use flags, destination flag, and access width |
| [execute.hpp](../include/rv/execute.hpp), [execute.cpp](../src/execute.cpp) | Pure ALU/control semantics: compute `Effect` without touching registers/RAM |
| [loader.cpp](../src/loader.cpp) | Validate images before loading; ELF segment copies and BSS zeroing |
| [machine.hpp](../include/rv/machine.hpp) | Own registers, RAM, committed PC, latches, statistics, and event callbacks |
| [machine.cpp](../src/machine.cpp) | Functional/pipeline scheduling, memory accesses, hazards, commit |
| [trace.cpp](../src/trace.cpp) | Serialize retirement, trap, cycle, and final-state records |
| [main.cpp](../src/main.cpp) | CLI, limits, debugger, pre-commit breakpoint checks |

`Decoded` describes what an instruction needs. `Effect` describes its proposed value, data address/store value, next PC, redirect, or trap. `Packet` carries those with PC, word, validity, and a speculative fetch tag. A bubble is an invalid packet; a NOP is a valid instruction that still retires. `Retirement.sequence` numbers committed normal instructions, independently of speculative fetch tags.

`Machine::pc()` is the next **architectural** PC, updated only at commit. `fetch_pc_` is speculative and private. After a terminal trap, architectural PC is the faulting instruction PC. These two PCs must not be compared as if they meant the same thing.

## Functional path

`functional_tick()` fetches, decodes, computes an effect, performs validated memory access, then calls `commit()`. It finishes at most one normal instruction or terminal trap. It has no simulated pipeline cycle count. Instruction limits count only normal retired instructions; EBREAK/ECALL/faulting instructions do not retire.

## Five-stage pipeline

IF fetches a word into IF/ID. ID decodes and reads operands into ID/EX. EX computes ALU results, addresses, branch comparisons, JALR bases, and jump/branch targets into EX/MEM. MEM reads loads or validates stores into MEM/WB. WB commits stores/register results or a terminal trap. ALU and RAM each take one cycle; one instruction can enter each stage per cycle. Completion remains in order.

Each `pipeline_tick()` copies `current_` to immutable `old`, creates empty `next`, then assigns `current_=next` exactly once. No stage consumes a newly created latch. Host call order cannot move one instruction through two stages in one tick.

Within that tick:

1. Record current latch occupancy and increment simulated cycles.
2. Commit old MEM/WB. A terminal trap ends this tick immediately and discards all younger work.
3. Process old EX/MEM in MEM. WB stores are already visible to this load.
4. Process old ID/EX in EX using forwarding from **old** EX/MEM and MEM/WB.
5. Resolve priority: older MEM fault, then EX fault, then EX redirect, then ID hazard/read and IF fetch.
6. Publish next latches and the cycle event.

WB register writes precede ID reads in the same cycle. The bypass from old MEM/WB to EX is still needed because ID/EX values were read in an earlier cycle. IF has no separate persistent latch: its result becomes IF/ID. There are exactly four stage-boundary latch pairs with explicit current/next state.

## Dependencies and forwarding

`producer()` ignores invalid packets, faults, non-writing instructions, and x0. Each actually used source is handled independently. EX/MEM takes priority over MEM/WB, so the newest matching producer wins. A load in EX/MEM has an address, not a loaded value, and is never forwarded from that latch.

The EX consumers include ALU rs1/rs2, branch comparisons, JALR bases, store address rs1, and store data rs2. Store data is resolved in EX and carried to WB. There is no late MEM-stage store-data shortcut in this model: an immediately dependent load-to-store therefore needs the same one bubble as load-to-ALU.

With forwarding **on**, ID stalls when its used source matches a load in old ID/EX. IF/ID and the fetch PC hold, ID/EX gets a bubble, and older instructions advance. One bubble makes the value available from MEM/WB when the consumer reaches EX.

With forwarding **off**, ID waits for any matching writer in old ID/EX or EX/MEM. A matching writer in MEM/WB commits before this ID read, so no stall is needed for it. Adjacent dependent ALU instructions need two bubbles. Stalls alter timing, never architectural results.

Counters `load_use` and `raw` are disjoint: forwarding-on uses only `load_use`; forwarding-off uses only `raw`. Squashed younger instructions do not generate hazard stalls on a redirect/fault cycle.

## Branches, faults, and side effects

Sequential fetch predicts not taken. All taken conditional branches and all jumps resolve in EX. On a redirect, the IF/ID packet is discarded, ID/EX next is empty, fetch PC changes, and no fetch occurs that cycle. Target fetch starts on the next tick: a two-cycle control penalty. A jump to its sequential successor still redirects under this simple rule.

Only already-fetched valid packets count as `flushed`. The unused IF opportunity on a redirect is a timing penalty but is not a flushed instruction. Thus a normal taken branch often flushes **one** valid packet while losing **two** issue opportunities. Cycle traces show IF `-` on that redirect cycle.

Loads and stores validate in MEM; stores change RAM only in WB. Register writes occur only in WB. Older WB effects happen before a younger MEM access. This guarantees an immediately following load sees an older store, while a faulting load/store changes no architectural state. A load to x0 still accesses/validates RAM and can fault.

Fetch/decode faults and ECALL/EBREAK become tagged packets. They stop further front-end fetching but do not terminate the run. An older EX redirect discards a younger tagged fault and reopens fetch. An older MEM fault overrides a younger EX redirect. Once a fault reaches the oldest WB position, all older instructions have committed, the faulting instruction applies no effects, younger effects are suppressed, and a separate terminal record identifies its PC/cause/value. EBREAK uses this same path and means normal demo completion; ECALL is a simulation fault. There is no guest trap handler.

## Four instruction walkthroughs

**ADD:** functional execution reads operands, computes wrapping sum, commits rd. In the pipeline ID reads registers, EX selects newer bypass values if needed and computes the sum, MEM passes it on, WB writes rd. Following ALU instructions can consume it from EX/MEM.

**LW:** functional execution computes base+signed offset, checks alignment/range, reads little-endian bytes, commits rd. The pipeline computes the address in EX and reads in MEM; only MEM/WB contains the loaded value. An immediately following consumer stalls once with forwarding. A failing LW to x0 traps too.

**SW:** functional execution computes address and store data, validates, and commits bytes. The pipeline forwards base and data in EX, validates in MEM, and commits bytes in WB. A younger MEM load in that WB cycle reads the updated bytes. A squashed SW never reaches the only memory-write site in `commit()`.

**Taken BEQ:** functional execution compares operands, checks alignment only for a taken target, and commits the target PC. The pipeline compares forwarded operands in EX, kills younger state, redirects speculative fetch, and eventually commits the branch's target as architectural next PC. A wrong-path store/EBREAK/illegal word has no effect. A not-taken branch with a misaligned encoded target does not fault; a taken one faults on the branch PC. JALR first clears target bit zero, then checks four-byte alignment.

## Exact short timings

Cycles begin with the first IF attempt and end with terminal-trap WB, including fill and drain. With N normal instructions followed by EBREAK, no stalls/redirects: **N+5 cycles**. A normal instruction first fetched in cycle 1 retires in cycle 5. EBREAK alone takes 5 cycles and retires zero instructions.

| Hand-checkable sequence | Forwarding on | Off |
|---|---:|---:|
| EBREAK | 5 | 5 |
| 3 normal dependent ALU instructions, EBREAK | 8 | 12 |
| ADDI base; LW; dependent ADD; EBREAK | 9 | 12 |
| ADDI; taken branch skipping wrong path; ADDI; EBREAK | 10 | Depends on RAW operands |

These exact cases are asserted in [core_tests.cpp](../tests/core_tests.cpp). A finite successful program with no other effects costs N+5+stalls+2*redirects. Do not apply this formula blindly to early faults, limits, debugger pauses, or unsupported environments. See [Benchmarks](BENCHMARKS.md) for the collected demo measurements.
