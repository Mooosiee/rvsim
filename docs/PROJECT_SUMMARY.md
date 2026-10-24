# Project summary for an interview

## A concise explanation

“I built and studied an AI-assisted C++17 RV32I teaching simulator with two execution models. The functional model completes one instruction at a time. The five-stage pipeline overlaps them, so it needs explicit latches, dependency detection, bypasses, stalls, and branch flushes. Both models share instruction semantics and produce the same committed-event format, which lets me compare their architectural behavior while keeping pipeline timing separate.

“The key design decision is an in-order commit boundary. Memory accesses validate in MEM, while stores and register writes commit in WB. Older WB stores happen before younger MEM loads in the same cycle. Faults and EBREAK remain tagged instructions until they reach WB, so older effects finish and wrong-path instructions cannot change state or terminate execution. Each tick computes next latches entirely from current latches.

“I validated it with native boundary tests, hand-derived cycle cases, reproducible generated programs, and eight real assembly/C demos in both formats and all schedules. Independent Spike integration is supplied but was not executed here. This models a deliberately simple pipeline; it is not a timing model for a commercial CPU or official ISA certification.”

## Evidence you can demonstrate

- Run Fibonacci to obtain 55, array sum/max to obtain 3/9, bubble sort to verify the entire signed array, and nth prime to obtain 29 without DIV/REM.
- Show the ALU chain trace: four normal instructions take 9 cycles with forwarding, 15 without; both return 7.
- Show load use: the adjacent load consumer needs one bubble even with forwarding, giving 10 versus 15 cycles for that supplied program.
- Stop at a pre-commit breakpoint on a store and show memory is unchanged; then continue to commit it.
- Explain why functional/pipeline agreement tests scheduling but cannot independently prove shared instruction semantics correct.
- Run `ctest` and the binary rebuild checker. Know which external checks were not run.

## Resume bullet suggestions

Use only after you understand the linked implementation and can reproduce these checks:

- Developed an AI-assisted C++17 RV32I simulator with functional and five-stage pipeline models, explicit current/next latches, dependency stalls, result forwarding, and precise in-order register/store/trap commit.
- Added ELF/raw loaders, a pre-commit debugger, retirement/cycle traces, and 48 automated demo executions comparing full registers, memory, and committed effects across formats and forwarding configurations.
- Verified hand-derived pipeline timings on Windows GCC and MSVC; demonstrated a four-instruction ALU chain at 9 modeled cycles with forwarding versus 15 without, with identical results.

These bullets do not claim real-CPU performance improvement. The measured counts include fill/drain and terminal EBREAK cycles; see [Benchmarks](BENCHMARKS.md). Do not claim hosted CI, Linux sanitizers, GNU cross builds, or independent Spike passed until those checks actually run.

## Tradeoffs to discuss

Shared semantics keep the project readable but create a common-mode correctness risk. A flat RAM/no-cache environment makes timing deterministic but omits real memory hierarchy effects. EX-resolved branches and not-taken prediction are easy to explain but incur a two-cycle redirect penalty. Store data is forwarded in EX, so load-to-store uses the same bubble as load-to-ALU. No privileged machine/OS/device support keeps the environment small; Linux binaries cannot be treated as supported programs. The external adapter deliberately supplies an equivalent flat environment instead of assuming Spike defaults match.

Read [Architecture](ARCHITECTURE.md), then walk through [machine.cpp](../src/machine.cpp) beside [load_use_trace.txt](load_use_trace.txt). Be able to point to the producer priority, the single latch assignment, and the only architectural store site. Attribution and the empty starting workspace are documented in [Upstream](UPSTREAM.md).
