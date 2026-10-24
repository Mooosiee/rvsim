# Adding instructions

An instruction has three separate concerns: recognizing its bits, computing its meaning, and scheduling its operands/results. Keep them separate so both models retain architectural equivalence.

1. **Choose an environment and legal encoding.** Check the official RISC-V specification and extension dependencies. Define alignment/trap behavior and whether the instruction can have side effects. Do not silently reinterpret unsupported words.
2. **Add decoding.** Add an `Op` and printable name in [decode.hpp](../include/rv/decode.hpp) / [decode.cpp](../src/decode.cpp). Validate opcode/funct fields/reserved bits. Extract signed immediates with `sext`; never depend on host signed shifts for bit reconstruction.
3. **Specify dependencies.** Set `uses_rs1`, `uses_rs2`, `writes_rd`, `load`, `store`, and access width accurately. A register number present in unused encoding bits is not automatically a source. x0 remains excluded from producer matching, but x0 destinations do not suppress memory faults.
4. **Implement meaning.** Extend the pure [execute.cpp](../src/execute.cpp) function to return an `Effect`. Arithmetic should use unsigned fixed-width operations and explicit signed interpretation. Do not write registers or RAM here. If needed, extend `Effect` and `memory_access()` for validation/data acquisition.
5. **Preserve commit.** Architectural effects and terminal traps go through [Machine::commit](../src/machine.cpp). Stores validate before commit and update only at commit. Faults carry their own instruction PC. Older instructions must finish before a younger trap takes effect.
6. **Review scheduling.** A one-cycle ALU instruction using existing metadata automatically participates in RAW checks/bypasses. A multi-cycle instruction requires explicit occupancy/backpressure and result-availability rules; merely adding an enum is insufficient. Think through EX/MEM versus MEM/WB availability, store data, branch/JALR consumers, speculative squashes, and forwarding-off operation.
7. **Test independently.** Add literal encoding examples, boundary semantic values, illegal neighboring encodings, functional/pipeline comparisons on/off, newest-writer and x0 cases, precise exceptions, wrong-path suppression, and hand-derived timing changes in [core_tests.cpp](../tests/core_tests.cpp). Avoid deriving expected results with the simulator's own decoder/execute function. Extend the independent adapter's overlap deliberately if appropriate.
8. **Update the user contract.** Document ISA/environment support, instruction mappings, assumptions/counters, examples, and any new trap. Rebuild all demos if sources/compiler flags change; regenerate provenance and benchmark data and rerun essential tests.

For example, a future M-extension MUL must define low-32-bit behavior and exact latency before scheduling it. DIV additionally needs divide-by-zero and signed-overflow rules from the specification. Adding host `/` without those checks could invoke undefined behavior and produce a simulator that agrees with itself but is architecturally wrong.

## Existing instruction map

| Family | Decoder/meaning | Focused tests |
|---|---|---|
| LUI/AUIPC | U-type cases; `execute` PC/immediate arithmetic | `decoding`, `arithmetic`, JALR/link control cases |
| ADD/SUB and immediate ADDI | R/I cases; wrapping addition/subtraction | `decoding`, `arithmetic`, generated oracle, ALU demo |
| SLT/SLTU/SLTI/SLTIU | R/I cases; signed_value or unsigned compare | literal words and boundary values in `arithmetic` |
| XOR/OR/AND and immediate variants | R/I cases; bitwise operators | literal words, bit masks in `arithmetic`, generated oracle |
| SLL/SRL/SRA and immediate variants | R/I funct checks; masked shifts/explicit arithmetic shift | literal words, sign/count boundaries in `arithmetic` |
| LB/LH/LW/LBU/LHU | I cases; EX address, MEM read/sign extension, WB result | `loads_stores`, `hazards_timing`, load-use/array/C demos |
| SB/SH/SW | S cases; EX address/data, MEM validation, WB bytes | `loads_stores`, `control_precision`, sort/flush demos |
| BEQ/BNE/BLT/BGE/BLTU/BGEU | B immediate; EX comparisons/redirect | `decoding`, `control_precision`, loop programs |
| JAL/JALR | J/I cases; target/link/alignment in EX | `control_precision`, fibonacci/startup demos |
| FENCE | Base FENCE decoding; no effect in selected memory model | literal decode; single-hart environment documented |
| ECALL/EBREAK/illegal | Exact system encodings or decoder rejection; tagged trap through WB | `decoding`, `control_precision`, all demos, CLI fault tests |

All source paths above are real implementation links. The test names are C++ functions in the single deliberately small native test executable.
