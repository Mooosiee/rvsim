# Demonstration programs

## Build, run, verify

All demos terminate by EBREAK reaching commit. They are self-contained fixed-input programs; none reads host command-line input. Scalar results use a0. Data labels are linker-resolved addresses, not guessed host pointers. Default RAM is 64 KiB.

Every row below has readable source, a supplied `demos/prebuilt/NAME.elf`, a matching `demos/prebuilt/NAME.bin`, and checked values in [expected.json](../demos/expected.json). The builder uses a real cross-toolchain; an internal hand assembler is not substituted.

```sh
python scripts/build_demos.py --cc riscv64-unknown-elf-gcc
# Exact artifact reproduction instead uses Zig 0.14.1:
python scripts/build_demos.py --zig /path/to/zig --check
python scripts/run_demos.py --sim build/rvsim
```

The automated runner exits nonzero on any discrepancy. It runs each ELF/raw image in functional mode and pipeline with forwarding on/off (48 executions). It checks the expected scalar/array values, compares complete retirement/trap streams at matching architectural boundaries, and compares final registers and every RAM byte. It does not compare speculative PCs or cycle numbers between models.

For any NAME below:

```sh
./build/rvsim --program demos/prebuilt/NAME.elf --format elf --model pipeline --forwarding on --stats NAME.json
./build/rvsim --program demos/prebuilt/NAME.bin --format bin --load-address 0x1000 --entry 0x1000 --model functional
```

On Windows use `.\build\rvsim.exe`; Python scripts accept `--sim build/rvsim.exe`. Build commands apply to all rows; per-program exact compiler commands are in [provenance.json](../demos/prebuilt/provenance.json). [link.ld](../demos/link.ld) determines the map. Rebuild and regenerate measurements together after source/compiler changes.

## Programs and expected output

| NAME / source | Purpose, input | Checked output |
|---|---|---|
| `fibonacci` / [fibonacci.S](../demos/fibonacci.S) | Iterative F(0)=0, F(1)=1; input a0=10 before call | a0=55; valid n=0..46; outside range returns -1 |
| `array_sum` / [array_sum.S](../demos/array_sum.S) | Signed sum/max of `[-7,4,0,-2,9,9,-10]` at `input=0x2000` | a0=3, a1=9; `sum_result=0x201c`=3, `max_result=0x2020`=9; length<=0 returns a0=-1 |
| `bubble_sort` / [bubble_sort.S](../demos/bubble_sort.S) | In-place signed sort of `[-3,7,0,-3,2,-9,7]`; duplicates and negatives | a0=0; all seven words at `array=0x2000` equal `[-9,-3,-3,0,2,7,7]`; n=0/1 succeeds, negative or n>7 returns -1 |
| `nth_prime` / [nth_prime.S](../demos/nth_prime.S) | Trial division with repeated subtraction, no DIV/REM; n=1 means 2, supplied n=10 | a0=29; valid n=1..25, outside returns -1 |
| `alu_chain` / [alu_chain.S](../demos/alu_chain.S) | Four dependent ALU instructions | a0=7; 9 cycles on, 15 off |
| `load_use` / [load_use.S](../demos/load_use.S) | Load value 21, immediately add it to itself | `value=0x2000` remains 21; a0=42; 10 cycles on, 15 off; one load-use stall on |
| `branch_flush` / [branch_flush.S](../demos/branch_flush.S) | Two taken branches: discard wrong-path store and wrong-path EBREAK | `sentinel=0x2000` remains zero; a0=42; two redirects |
| `freestanding` / [freestanding.c](../demos/freestanding.c), [start.S](../demos/start.S) | Real C compilation, ELF data/BSS initialization, stack/startup, input `[-2,5,12,15]` | a0=30, `result=0x2010`=30; fixed valid allocation/length |

Array-sum and sort length checks apply when adapting their source; the allocations remain fixed, so do not simply increase a length without supplying storage. Sums must fit your chosen intended signed result range. Fibonacci uses signed-result-safe n<=46; overflow of scratch values outside the returned result is still modular RV32I arithmetic. C demo input sums to 30 without signed overflow.

To inspect sorted RAM interactively:

```sh
./build/rvsim --program demos/prebuilt/bubble_sort.elf --format elf --debug
```

Then enter `continue`, `memory 0x2000 28`, `registers`, `quit`. Bytes `f7 ff ff ff` are little-endian -9, for example. The automated runner checks words rather than requiring manual byte interpretation.

## Pipeline trace walkthrough

Actually collected [ALU trace](alu_chain_trace.txt) and [load-use trace](load_use_trace.txt) can be regenerated:

```sh
./build/rvsim --program demos/prebuilt/alu_chain.elf --format elf --model pipeline --trace docs/alu_chain_trace.txt
./build/rvsim --program demos/prebuilt/load_use.elf --format elf --model pipeline --trace docs/load_use_trace.txt
```

In the load-use program, `la t0,value` expands to **two** machine instructions at 0x1000 and 0x1004. LW is at 0x1008, ADD at 0x100c, and EBREAK at 0x1010. The precise timing is:

| Cycle | Key event |
|---:|---|
| 1..3 | Fill: fetch AUIPC/ADDI/LW, first instruction reaches EX |
| 4 | ADDI consumes AUIPC through EX/MEM bypass; ADD fetched |
| 5 | LW computes address using ADDI bypass; ADD in ID stalls; IF holds; first instruction retires |
| 6 | LW reads 21 in MEM; ADD enters ID/EX; EX contains a bubble |
| 7 | ADD uses loaded value from MEM/WB for **both** operands; LW retires |
| 8..9 | ADD drains; a0 becomes 42 at cycle 9 |
| 10 | EBREAK reaches WB and completes without retirement |

Four normal instructions + five fill/terminal overhead cycles + one load-use bubble = ten cycles. Forwarding disabled adds six RAW stall cycles instead, so the total is fifteen. The ALU example needs no load-use bubble, producing nine cycles with forwarding.

For control flow, run `branch_flush` with `--trace branch.txt`. Redirect cycles have IF unused and the younger ID packet flushed; target fetch resumes next cycle. Wrong-path stores never produce `memory_write` retirement events, and wrong-path EBREAKs never produce a terminal record. Only the final EBREAK does.
