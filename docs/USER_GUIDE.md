# User guide

## Build and run

Follow the platform commands in [README](../README.md). Python is used by test/demo scripts; the simulator executable itself has no Python dependency. GCC builds use strict warnings, and MinGW executable runtimes are statically linked. Visual Studio is supported through CMake's VS2022 generator. For a fresh compiler installation, restart old terminals so they read the persistent PATH.

```sh
./build/rvsim --help
./build/rvsim --program demos/prebuilt/array_sum.elf --format elf --model pipeline --forwarding on --stats sum.json
./build/rvsim --program demos/prebuilt/fibonacci.bin --format bin --load-address 0x1000 --entry 0x1000
```

Normal output names status, architectural PC, retired count, pipeline cycles where applicable, terminal trap, and a0 in hex/unsigned/signed decimal. Use `registers` in the debugger for all registers; the JSON stats snapshot also contains all 32 registers and sparse nonzero bytes of final RAM (missing bytes mean zero).

## Complete command line

Numbers are unsigned decimal or lowercase-prefix `0x` hexadecimal, with no whitespace/sign. Duplicate options, unknown options, bad values, and missing values are rejected. Addresses must fit 32 bits.

| Option | Default and meaning |
|---|---|
| `--help` | Print help, exit 0; takes precedence over other arguments |
| `--program PATH` | Required image path |
| `--format elf\|bin` | `elf`; format is explicit, not inferred from extension |
| `--model functional\|pipeline` | `functional` |
| `--memory-size BYTES` | 65536, allowed 1..2^32 subject to host memory availability |
| `--load-address ADDRESS` | Raw only; 0x1000 |
| `--entry ADDRESS` | Raw only; defaults to load address |
| `--max-instructions N` | 1000000 normal retirements |
| `--max-cycles N` | Pipeline only; 10000000 ticks |
| `--forwarding on\|off` | Pipeline only; on |
| `--trace PATH` | Optional output file, replaced on run |
| `--trace-format text\|jsonl` | text; specifying it requires `--trace` |
| `--stats PATH` | Optional JSON statistics/final-state file, replaced on run |
| `--debug` | Enter interactive debugger before execution |

ELF addresses cannot be overridden by raw options. Explicit forwarding or max-cycles in functional mode is an error. Program, stats, and trace paths must differ. Create output directories first. Large memory sizes allocate real host RAM and are unnecessary for the demos.

## Image/environment contract

ELF: little-endian ELF32, RISC-V machine 243, static ET_EXEC, System V ABI identification, flags zero (no compressed instructions, floating ABI, or RVE), normal 52-byte ELF and 32-byte program headers. PT_LOAD physical and virtual addresses must agree; segments must fit RAM, have filesz<=memsz, obey alignment, and not overlap. BSS is zeroed. Entry must identify a complete aligned instruction in an executable file-backed segment. Dynamic/interpreter/TLS segments are rejected. Section tables/symbols are not needed. Runtime memory permissions are not enforced.

Raw: nonempty bytes copied at load address; entry must point to a complete aligned instruction inside those bytes. RAM outside the image starts zero. There are no symbols or startup metadata. Instruction encodings are little-endian 32-bit words in both formats. Unsupported opcodes fault at commit.

All registers start zero. There is no implicit a0 argument, initialized stack, global pointer, argc/argv, syscall service, or runtime. The supplied C startup sets its own stack and BSS. EBREAK (`0x00100073`) at the oldest commit position completes normally, irrespective of a0; it does not retire. ECALL (`0x00000073`) is a terminal fault. Wrong-path EBREAK/ECALL/faults never stop execution.

## Limits and exit codes

| Code | Meaning |
|---:|---|
| 0 | Committed EBREAK completion, `--help`, debugger quit/EOF |
| 1 | Terminal simulation fault (illegal instruction, memory/alignment fault, ECALL) |
| 2 | Execution limit reached |
| 64 | Usage, input image, output file, or host setup error |

Limits are checked **before** each tick. N=0 stops immediately. Once max-instructions normal instructions have retired, the run stops before the next tick, even if that tick would commit EBREAK. Set the instruction budget above the expected retirement count to observe completion. Max-cycles counts pipeline ticks, including fill/drain/trap WB. A limit stops without draining and publishes only already-committed state. No terminal trap is fabricated for a limit. Host elapsed time is not a simulated cycle.

## Debugger

```sh
./build/rvsim --program demos/prebuilt/alu_chain.elf --format elf --model pipeline --debug
```

| Command | Behavior |
|---|---|
| `help` | List commands |
| `continue` | Run until breakpoint, terminal trap, or limit |
| `registers` | All x0..x31 in hex, unsigned, signed |
| `memory ADDRESS LENGTH` | Print that many bytes; full range checked |
| `breakpoint ADDRESS` | Add aligned pre-commit breakpoint |
| `delete ADDRESS` | Remove breakpoint |
| `step` | Functional: one instruction/trap; pipeline: next retirement/trap boundary |
| `tick` | Pipeline: one cycle |
| `stages` | Pipeline: inspect four current latch occupancies |
| `reset` | Restore initial loaded RAM/entry, zero registers, clear counters/latches/trap; retain breakpoints |
| `quit` | Exit debugger |

Breakpoints observe the **oldest instruction just before commit**, before its register/memory effects or trap. In pipeline mode younger speculative fetches do not trigger them. For example, `breakpoint 0x100c` in the ALU chain stops with three instructions retired and a0 still zero. `continue` from a breakpoint skips that one stop and allows its commit; a later visit to the address stops again. `step` and `tick` honor breakpoints too. A `stages` inspection refers to current latches, not architectural completion.

Only the `commit()` function emits normal retirement/trap callbacks. Debugger checks happen before that tick enters commit; traces therefore cannot observe a partially applied instruction. On reset, output trace files remain open and new event sequences/cycle counts restart at 1; use separate runs/files for comparisons across resets. Debugger quit has no terminal trap and returns 0, so automation should check `status==completed` if completion matters.

## Trace schema

Text traces are for reading; JSONL has one JSON object per line. Retirement and terminal traps are separate from speculative cycles:

```json
{"type":"retire","sequence":1,"pc":4096,"word":1049235,"op":"addi","register_write":{"register":5,"value":1},"memory_write":null,"next_pc":4100}
{"type":"trap","cause":"ebreak","pc":4112,"value":0}
```

Retirement fields: dynamic committed sequence, instruction PC/word/op, optional register write (x0 omitted), optional store `{address,width,value}` with value masked to written bytes, and next architectural PC. A trap reports cause, faulting PC, and related value (bad address/illegal word; zero for ECALL/EBREAK). The trap instruction does not retire.

Pipeline cycle records contain `cycle`, five stage occupancies `{tag,pc}` or null, `stall`, forwarding choices such as `rs2:x6<-MEM/WB`, `redirect`, target, and count of actual younger packets flushed. IF is the fetch attempted during that cycle; ID/EX/MEM/WB represent latches at its start. Retirement/trap callbacks occur before that cycle's final cycle record; join by reading event types, not by assuming a retirement is always the last line of a tick. Functional traces contain no cycle records.

## Statistics

`--stats` writes retired count and instruction mix for either model; pipeline adds cycles, CPI, disjoint load-use/RAW stall counts, redirects, and actual flushed packets. Functional cycles/CPI are JSON null. Pipeline CPI is null for zero retired instructions. Otherwise CPI=all simulated ticks/normal retired instructions, including terminal-trap tick, fill, drain, and any redirects. Trap instructions are excluded from instruction mix. Counters reset at reset and stop at completion/fault/limit; user inspection/pause time is excluded. There are no structural stalls or overlapping stall categories in this model.

The snapshot also includes status, architectural PC, registers, sparse RAM bytes, and terminal trap or null. It intentionally records guest state, not host instructions/second. See [Benchmarks](BENCHMARKS.md).

## Troubleshooting

- `g++` not found after installation: open a new terminal/VS Code window. On this host GCC is installed by WinGet for the Windows user, not inside the project.
- ELF rejected: check `-march=rv32i -mabi=ilp32 -nostdlib`, the supplied linker script, and no dynamic/PIE/compressed ABI flags. Linux executables are not bare-metal demos.
- Illegal instruction: inspect the trap PC/word; M extension DIV/REM and CSR instructions are unsupported.
- Load/store alignment: use word-aligned data and increments of four for 32-bit arrays.
- Unexpected limit: raise the appropriate budget; a loop without EBREAK does not finish by itself.
- Linker/stack fault: C startup assumes 64 KiB RAM and stack top 0x10000. Rebuild startup/linker layout for smaller RAM.
- Trace output cannot open: create its parent directory and ensure write access. Hosted/sandbox temporary directories can differ from project permissions.
- Spike adapter unavailable: validation returns 77 and reports NOT RUN; follow [Testing](TESTING.md), not Spike's default proxy-kernel workflow.
