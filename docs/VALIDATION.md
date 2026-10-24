# Delivery and validation record

Delivery date: 2026-10-24, Asia/Calcutta. The starting workspace was empty and was not a Git repository. All listed project files were newly created; there is no base revision or diff to attribute. No upstream implementation was reused. Attribution and licensing findings are in [UPSTREAM.md](UPSTREAM.md).

## Installed native compiler

At the user's explicit request, WinGet installed `BrechtSanders.WinLibs.POSIX.UCRT` version `16.2.0-14.0.0-r3`, with GCC/G++ 16.2.0, GDB, Ninja, and supporting tools, on the persistent Windows **user PATH**. This makes the compiler available across projects for this user; it is not an all-users administrator installation. Existing terminals/app processes must restart or refresh their environment.

Installation command:

```powershell
winget install --id BrechtSanders.WinLibs.POSIX.UCRT --exact --source winget --accept-package-agreements --accept-source-agreements --silent --disable-interactivity
```

WinGet verified the archive hash and reported success. `g++ --version` and `ninja --version` were run after refreshing the process PATH. A C++17 iostream program was compiled with `-Wall -Wextra -Werror` and printed `C++ compiler works`. Visual Studio's existing MSVC installation was also found; it was not installed by this work.

## Commands actually run and results

In this session's old terminal environment, the installed user PATH was refreshed using:

```powershell
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
```

| Command/check | Observed result |
|---|---|
| `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++` | Configured with GCC 16.2.0 |
| `cmake --build build` | Core library, rvsim, core_tests built; warnings treated as errors |
| `ctest --test-dir build --output-on-failure` | 4/4 suites passed |
| `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64` | Configured with MSVC 19.44.35225, Windows SDK 10.0.26100.0 |
| `cmake --build build-msvc --config Debug --parallel 2 -- /nodeReuse:false` | Built with `/W4 /WX /permissive-` |
| `ctest --test-dir build-msvc -C Debug --output-on-failure` | 4/4 suites passed |
| `cmake -S . -B build-clean -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release` | Fresh build directory configured |
| `cmake --build build-clean` then `ctest --test-dir build-clean --output-on-failure` | Fresh Release build; 4/4 suites passed |
| `python scripts/build_demos.py --zig .tools/zig-x86_64-windows-0.14.1/zig.exe` | Eight real ELF and raw demo pairs built with recorded provenance |
| Same command with `--check` | Every ELF and raw image matched a fresh build byte-for-byte |
| `python scripts/verify_binaries.py` | Source/artifact hashes and ELF/raw pairs verified |
| `python scripts/run_demos.py --sim build/rvsim.exe --bench docs/measurements.json` | 48 runs passed; checked expected results, commit streams, all final registers/RAM |
| `python scripts/benchmark_table.py docs/measurements.json` | Table reproduced from collected data |
| `python -m compileall -q scripts tests` | Python sources compiled successfully |
| Git Bash `-n scripts/setup_spike.sh` | Shell syntax check passed; setup/build itself not run |
| `python scripts/validate_spike.py --sim build/rvsim.exe` | Exit 77, explicitly NOT RUN because adapter unavailable |
| Documented Fibonacci / ALU-chain / load-use CLI commands | 55 / 7 / 42; ALU 9 cycles, load use 10 cycles with forwarding |

Some Windows tool subprocesses could not run in the restricted sandbox (MSBuild, Git Bash, and Zig's compiler subprocesses). Those checks were rerun outside that sandbox with tool approval and then passed. Initial setup failures were not counted as successful checks. Test scripts place temporary files in their current working directory to avoid sandbox-specific redirected temp permissions.

## New file inventory

- Build/configuration: [CMakeLists.txt](../CMakeLists.txt), [.gitignore](../.gitignore), [.github/workflows/ci.yml](../.github/workflows/ci.yml).
- Public core interfaces: `include/rv/types.hpp`, `memory.hpp`, `decode.hpp`, `execute.hpp`, `loader.hpp`, `machine.hpp`, `trace.hpp`.
- Implementation: `src/memory.cpp`, `decode.cpp`, `execute.cpp`, `loader.cpp`, `machine.cpp`, `trace.cpp`, `main.cpp`.
- Native/integration tests: `tests/core_tests.cpp`, `tests/cli_tests.py`.
- Demonstrations: `demos/fibonacci.S`, `array_sum.S`, `bubble_sort.S`, `nth_prime.S`, `alu_chain.S`, `load_use.S`, `branch_flush.S`, `start.S`, `freestanding.c`, `link.ld`, `expected.json`; 16 supplied `.elf`/`.bin` files and `demos/prebuilt/provenance.json`.
- Scripts: `scripts/build_demos.py`, `run_demos.py`, `verify_binaries.py`, `benchmark_table.py`, `setup_spike.sh`, `validate_spike.py`; independent `tools/spike_adapter.cpp`.
- Documentation: `README.md`; `docs/ARCHITECTURE.md`, `USER_GUIDE.md`, `WRITING_PROGRAMS.md`, `DEMOS.md`, `TESTING.md`, `BENCHMARKS.md`, `ADDING_INSTRUCTIONS.md`, `UPSTREAM.md`, `PROJECT_SUMMARY.md`, this record; actual `measurements.json`, `alu_chain_trace.txt`, `load_use_trace.txt`.

Generated local build directories and `.tools` are ignored; they are not required source or part of a clean checkout. Supplied prebuilt demo artifacts **are** required repository files. No Git repository/commit or remote/PR was created without a request.

## Explicit limitations and checks not run

- Spike: pinned adapter/harness supplied and API inspected, but no local compile/run. It covers successful aligned EBREAK-terminated programs in a matched flat environment; trap equivalence remains outside the independent overlap.
- Linux/WSL: workflows documented; not executed here. WSL is unavailable on this host.
- Address/undefined sanitizers: Linux GCC/Clang CMake option and CI job supplied, not locally run.
- GNU RISC-V toolchain: primary setup/build commands supplied, not run; actual artifact provenance is Zig 0.14.1.
- Hosted CI: workflow written, no hosted execution or success claimed.
- Scope: base integer teaching environment only; no privileged OS, caches, device model, M/C extensions, or commercial CPU performance fidelity/certification.
- No upstream URL/revision supplied; no project distribution license selected. Resolve these before presenting this as an upstream fork or distributing under a claimed license.

## Formatting follow-up

The user's readability review identified compressed statements, inline control flow, and inconsistent
spacing. All project C/C++ files in `src`, `include`, `tests`, `tools`, and the C demo were reformatted
with clang-format 19.1.5: four-space indentation, explicit control-flow braces, expanded function
bodies, and a 100-column limit. Python helpers/tests were formatted with Black 25.1.0; CMake with
cmakelang 0.6.13 and two-space indentation. Assembly already used label/instruction indentation and
was preserved.

New formatting support files: `.clang-format`, `.editorconfig`, `.cmake-format.json`,
`pyproject.toml`, `requirements-format.txt`, and `scripts/format_code.py`. README documents the
commands. A formatting CI job is supplied; hosted execution remains unrun.

Checks actually rerun after formatting:

- `python scripts/format_code.py --check`: all supported source files match the configured style.
- GCC Debug and MSVC Debug builds: warnings-as-errors builds succeeded; each passed all four suites.
- The demo runner: all 48 runs passed with the same expected results and timing values.
- Zig cross-build with `--check`: every reformatted-source build matched the previous ELF/raw bytes.
- Rebuilt artifacts/provenance and regenerated measurements: source hashes reflect the formatted
  files, and benchmark timings are unchanged.

The optional Python formatter packages are local to ignored `.tools/formatters`; Visual Studio's
existing clang-format was used. They are not additional simulator runtime dependencies.

A further readability pass added blank lines between validation, processing, state updates, and
results inside functions. Short argument lists can stay together, and empty constructors can use
`{}`. This avoids both dense blocks and unnecessarily vertical calls. The pass changed only
whitespace in 17 C/C++ files; the seven Python files retained identical syntax trees. The GCC build,
all four suites, and 48 demo runs passed again. A fresh cross-build matched every previous demo
binary, and provenance/measurement source hashes were refreshed.

The documentation/comment follow-up adds `docs/INSTRUCTION_REFERENCE.md` and links it from README,
Architecture, and Writing Programs. It explains the register/ABI names, instruction operands,
all 40 supported instructions, signed values, memory addressing, and a debugger walkthrough.
Comments in the core headers and implementation explain bit extraction, sign extension, effects,
commit, forwarding, hazards, trap ordering, loaders, and event observers. C/C++ tokens excluding
comments/whitespace remain unchanged in all 17 compared files. The GCC build and all four suites
passed; all 48 demo runs passed with refreshed measurement source hashes. The documented ALU
debugger steps were also executed and produced the listed register values and architectural PCs.

The sharing follow-up adds `START_HERE.txt`, `docs/SHARING.md`, and
`scripts/package_project.py`. Source-only and source-plus-Windows-executable ZIPs are generated in
ignored `out/`, excluding local build folders, `.tools`, and Python caches. A fresh GCC Release
build in `build-share` passed all four suites. The Windows ZIP was extracted, its file hashes
verified, and all 48 demos passed with compiler tools removed from PATH. The extracted source
also configured and built in a fresh directory and passed all four suites. The executable imports
Windows system/CRT DLLs, with no MinGW compiler-runtime DLL imports.
