# Benchmarks and timing interpretation

## Model and metrics

The pipeline issues at most one instruction per cycle, with in-order retirement, one-cycle ALU and memory, independent IF/data ports, no caches, not-taken sequential prediction, EX redirects, and WB architectural writes/stores. Same-cycle WB precedes MEM/ID. Forwarding bypasses EX/MEM and MEM/WB into every EX consumer, with a one-cycle load-use interlock. Forwarding-off waits in ID for register availability. See [Architecture](ARCHITECTURE.md) for the exact rules.

Count cycles from first IF attempt through the terminal instruction's WB cycle, including fill/drain, stalls, redirects, and terminal-trap WB. A normal retirement increments retired/instruction mix; EBREAK/ECALL/faults do not. CPI=cycles/normal retired instructions, null at zero retirements. `load_use` (on) and `raw` (off) count disjoint stalled ID cycles. No structural stall counter is needed. Redirects count taken branches/all jumps that actually resolve without an older fault. Flushed counts valid younger packets discarded, not unused fetch opportunities. A normal redirect loses two opportunities but usually flushes one actual packet.

Limits stop before a tick without draining; debugger pauses cost no cycles. Reset restarts counters. Host elapsed time and host instructions/second are not recorded here. These modeled cycles do not predict a real CPU's speed or frequency.

## Collected results

Actually measured on Windows x64, GCC/WinLibs 16.2.0 build, CMake 4.3.1, Python 3.14.2. Program artifacts were built with Zig 0.14.1. Configuration: 64 KiB zeroed RAM, entry/load 0x1000, max 1,000,000 retirements and 10,000,000 cycles, supplied ELF images, EBREAK completion. [measurements.json](measurements.json) records program source/artifact hashes, compiler commands/version, simulator binary/source hashes, host, configuration, and individual records. Functional runs in that file have null cycles/CPI and serve architectural verification.

| Demo | Retired | Cycles on / off | CPI on / off | Stalls on / off | Redirects |
|---|---:|---:|---:|---:|---:|
| fibonacci | 70 | 101 / 114 | 1.443 / 1.629 | 0 / 13 | 13 |
| array_sum | 54 | 88 / 117 | 1.630 / 2.167 | 7 / 36 | 11 |
| bubble_sort | 177 | 267 / 362 | 1.508 / 2.045 | 21 / 116 | 32 |
| nth_prime | 2223 | 3730 / 4078 | 1.678 / 1.834 | 0 / 348 | 751 |
| alu_chain | 4 | 9 / 15 | 2.250 / 3.750 | 0 / 6 | 0 |
| load_use | 4 | 10 / 15 | 2.500 / 3.750 | 1 / 6 | 0 |
| branch_flush | 6 | 15 / 17 | 2.500 / 2.833 | 0 / 2 | 2 |
| freestanding | 44 | 63 / 82 | 1.432 / 1.864 | 0 / 19 | 7 |

Stalls on means load-use stalls; off means RAW stalls. These counts include instruction expansion and actual C compiler output. Forwarding does not remove redirect penalties.

## Regenerate and explain

```sh
python scripts/run_demos.py --sim build/rvsim --bench docs/measurements.json
python scripts/benchmark_table.py docs/measurements.json
./build/rvsim --program demos/prebuilt/load_use.elf --format elf --model pipeline --forwarding on --trace docs/load_use_trace.txt
./build/rvsim --program demos/prebuilt/alu_chain.elf --format elf --model pipeline --forwarding off --trace chain-off.txt
```

Windows uses `build/rvsim.exe`. `run_demos.py` validates architectural equivalence before saving any table data. Changing code, compiler, binary, memory map, or timing contract requires regeneration. `benchmark_table.py` prints a table from saved measurements; it never invents missing results.

The four-instruction ALU chain needs 4+5=9 cycles with bypasses. With bypasses off, three adjacent RAW dependencies each add two cycles: 15. The load-use demo also has four normal instructions (`la` expands to two), but load data is unavailable from EX/MEM, adding one bubble even with forwarding: 10. Its off mode has three two-cycle RAW waits: 15. See the [collected traces](DEMOS.md#pipeline-trace-walkthrough).

Array sum reads a word and immediately adds it, producing seven load-use stalls with forwarding. Bubble sort compares each newly loaded second word, producing 21 such stalls for the supplied seven-item input. Nth prime avoids memory dependencies but spends many cycles in subtraction loops: 751 redirects cost 1502 cycles in either mode. Fibonacci forwards its ALU recurrence without load stalls, but its loop/control flow still redirects 13 times.

No Linux/WSL, GNU-cross-generated replacement, independent Spike, hosted-CI, sanitizer, or real-CPU performance measurements were collected here. No speedup beyond the supplied modeled timings is claimed.
