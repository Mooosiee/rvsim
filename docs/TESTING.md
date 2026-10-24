# Testing and independent validation

## Essential local checks

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python scripts/run_demos.py --sim build/rvsim
python scripts/verify_binaries.py
```

Use `build/rvsim.exe` with Windows GCC, or `build-msvc/Debug/rvsim.exe` with MSVC. Tests use explicit checks that remain enabled in Release builds, not `assert` compiled away by NDEBUG. Compiler warnings are errors (`-Wall -Wextra -Wpedantic -Werror`, or `/W4 /WX /permissive-`).

Four CTest suites:

| Suite / implementation | Evidence |
|---|---|
| `core` / [core_tests.cpp](../tests/core_tests.cpp) | Register reset/x0; endian/alignment/bounds/atomic stores; literal decoder examples; arithmetic boundaries; all load/store widths; control flow and precise faults; hazards/forwarding; exact short timings; reset; malformed ELF/raw; seeded arithmetic/control flow |
| `demos` / [run_demos.py](../scripts/run_demos.py) | 48 executions; checked scalars/whole sorted array; ELF/raw and all schedulers compare commits plus final registers/RAM |
| `cli` / [cli_tests.py](../tests/cli_tests.py) | Option validation, exit codes, zero-budget limits/CPI, illegal-word fault PC, instruction/cycle stepping/reset, pre-commit breakpoints including store suppression and wrong-path addresses |
| `provenance` / [verify_binaries.py](../scripts/verify_binaries.py) | Source/artifact hashes and ELF-to-raw consistency; explicitly not a compilation proof |

The stronger build check uses the exact recorded Zig 0.14.1 compiler:

```sh
python scripts/build_demos.py --zig /path/to/zig --check
```

It rebuilds every assembly and C program, compares both formats byte-for-byte, and fails on any difference. It was run successfully on Windows in this delivery. An alternative GNU build is available but was not run here.

## What the core cases establish

Literal machine words such as `0x003100b3` (ADD), `0xfff10093` (negative ADDI), `0xfe209ee3` (backward BNE), and `0x403150b3` (SRA) are specified independently of the test encoders. Direct semantic checks cover wraparound, signed/unsigned 0x80000000 boundaries, sign extension, logical/arithmetic shifts, and masking register shift counts. The decoder rejects M/CSR/compressed/unsupported encodings.

Memory tests check complete ranges, partial-end accesses, alignment priority, unchanged bytes after failed stores, all width/sign combinations, and loads to x0 still trapping. Control cases exercise forward/backward and taken/not-taken branches, misaligned targets, JAL link/JALR bit clearing, AUIPC, and dependent links/bases.

Pipeline cases check newest-producer selection, ALU/store-data/store-address/branch/JALR dependencies, load-use bubbles, forwarding-off RAW waits, wrong-path stores/illegal words/EBREAK/fetch faults, an older redirect discarding a fetch fault, a MEM fault outranking a younger redirect, older stores visible to immediate younger loads, and state containing only older effects at terminal faults. Hand-derived cycle counts include EBREAK alone (5), an ALU chain (8/12), load use (9/12), and a taken redirect example (10 with forwarding). Test encoders are helpers for generating programs, not the source of expected decoder words or cycle counts.

## Deterministic generated programs and differential checking

Seeds 1, 42, 2026, and 0xc0ffee each generate 400 straight-line ALU instructions with a separately computed expected register vector. Each also generates a bounded backward loop/memory program with an independently known arithmetic-series result. Functional execution is checked against these expected results, then both pipeline schedules execute the same words and initial state.

`compare()` serializes retirement and trap records and compares them boundary-for-boundary. It also saves all registers, the entire RAM byte vector, and architectural PC after **each** retirement and compares those snapshots, plus the final terminal state. The demo runner extends event/final-state comparisons to ELF/raw startup and actual programs. Speculative PCs and cycle counts are deliberately excluded from architectural comparisons.

Shared decode/semantics mean this strongly tests scheduling, but it cannot independently prove instruction correctness. A semantic bug shared by both models might survive differential checking. Literal words, separately derived values, and the following Spike adapter address different parts of that risk. Random cases are deterministic and limited in number; this is not exhaustive verification or official ISA certification.

## Independent Spike harness: NOT RUN here

Pinned upstream: [Spike v1.1.0](https://github.com/riscv-software-src/riscv-isa-sim/tree/530af85d83781a3dae31a4ace84a573ec255fefa), exact revision `530af85d83781a3dae31a4ace84a573ec255fefa`. The API headers and release hash were inspected when writing [spike_adapter.cpp](../tools/spike_adapter.cpp). The adapter is not linked into the simulator and shares no decoder, instruction semantics, or loader implementation with it.

This Windows host has no Spike or working WSL installation. The adapter has **not been compiled or executed here**, so API/link/platform issues may remain. The supplied runnable setup is the next independent check, not a claimed pass. The actual validation command was invoked and returned 77 with `NOT RUN: pinned Spike adapter unavailable`.

On Linux/WSL, from the project root:

```sh
sudo apt-get update
sudo apt-get install build-essential git autoconf automake libtool device-tree-compiler libboost-regex-dev libboost-system-dev
bash scripts/setup_spike.sh
python scripts/validate_spike.py --sim build/rvsim --adapter build-spike/spike_adapter
```

[setup_spike.sh](../scripts/setup_spike.sh) clones the pinned release, checks its exact commit, builds with commit logging, then compiles the adapter against the same generated config/headers/libraries. It deliberately does not enable Spike's optional misaligned-access emulation. A missing adapter makes [validate_spike.py](../scripts/validate_spike.py) exit **77**; missing dependencies/build failures exit nonzero. No silent fallback to functional/pipeline comparison occurs.

### Agreed overlap and platform adapter

Spike's default platform has a boot ROM, devices, DRAM at a different address, privileged trap handling, and optional proxy-kernel conventions. Running `spike demo.elf` does not match this simulator.

The adapter constructs a single `processor_t` directly, selects RV32I/M-mode with no PMP restrictions, sets all integer registers to zero and PC to 0x1000, and supplies its own zero-filled 64 KiB physical RAM. It loads the identical raw image at 0x1000. All accesses use checked `simif_t` MMIO callbacks, avoiding boot ROM/devices and direct host-pointer access past RAM ends. It never uses pk, SBI, syscalls, virtual memory, interrupts, or a default Spike machine map.

After each Spike `step(1)`, its commit log provides register-write presence (including same-value writes) and store effects; the adapter records PC/word, next PC, and a final full sparse RAM/register snapshot. The Python harness normalizes only operation-name fields and cycle events, then compares all normal retirement effects and terminal completion for all eight demos against functional and both pipeline modes (24 comparisons).

At the agreed EBREAK instruction boundary, the adapter emits completion before Spike's privileged trap-vector processing. This matches our host completion convention, which leaves PC on EBREAK and excludes it from retirement. Other traps cause adapter failure; the independent harness currently covers **successful aligned programs ending in EBREAK**, not trap-cause equivalence, loader rejection, unsupported instructions, or speculative pipeline timing. Those are covered locally, not independently proven by this adapter.

## Sanitizers, CI, and actual status

Linux GCC/Clang sanitizer command:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DRVSIM_SANITIZERS=ON
cmake --build build-asan --parallel 2
ctest --test-dir build-asan --output-on-failure
```

ASan/UBSan are not claimed as locally run on this Windows host. MinGW sanitizer configuration is rejected explicitly. [ci.yml](../.github/workflows/ci.yml) supplies Windows/Linux builds, a Linux sanitizer job, and a pinned Zig rebuild job. No hosted run has been observed because the original workspace was not a Git repository or connected remote.

Actual completed local checks: GCC 16.2 warnings-as-errors build; all four CTest suites; 48 demo runs with expected outputs and complete architectural comparisons; all-source binary rebuild with Zig 0.14.1; compiler smoke test. MSVC verification and final clean-build results are recorded in [VALIDATION.md](VALIDATION.md). Missing runs: Linux/WSL, GNU RISC-V build, hosted CI, and independent Spike. See [Upstream](UPSTREAM.md) for scope/ownership.
