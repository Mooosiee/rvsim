# Sharing the project with friends

## What goes in the ZIP?

Share source and examples, not a copy of the entire workspace. Local build folders contain compiler
outputs, caches, and absolute paths from your computer. A friend should create a fresh build folder
when compiling the source. The one executable can be copied separately into a convenient location.

| Include | Purpose |
|---|---|
| `src/`, `include/` | Simulator C++ implementation and interfaces |
| `demos/`, including `prebuilt/` | Assembly/C source and runnable guest programs |
| `docs/`, `README.md`, `START_HERE.txt` | Learning, building, and running instructions |
| `scripts/`, `tests/`, `tools/` | Development helpers, tests, optional independent adapter |
| `CMakeLists.txt` | Portable build definition |
| `.github/` and formatter/configuration files | CI definition and consistent editing style |
| A fresh Release `rvsim.exe`, optionally | Run directly on Windows x64 without building |

Exclude `build/`, `build-clean/`, `build-msvc/`, `build-setup/`, any other `build*` folder, `.tools/`,
`__pycache__/`, `.pyc` files, and `out/` packages. The hidden `.tools/` folder contains local
compiler/formatter downloads; it is different from `tools/`, which contains project source.

Keep the `demos/prebuilt/*.elf` and `*.bin` files: those are guest programs required for toolchain-free
demo runs. They are different from host build intermediates. You do not need to send a compiler
installer or your whole Visual Studio/GCC installation.

## Create a source-only ZIP

From the project root:

```powershell
python scripts/package_project.py --output out/rvsim-source.zip
```

The helper includes only the project files above and puts them inside one `rvsim/` directory in
the ZIP. `PACKAGE_INFO.json` records a SHA-256 hash for every included project file. The ZIP is
checked for archive integrity after writing.

## Include a Windows executable

Build a fresh Release executable with GCC/Ninja, then package it:

```powershell
cmake -S . -B build-share -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build-share
ctest --test-dir build-share --output-on-failure
python scripts/package_project.py --windows-exe build-share/rvsim.exe --output out/rvsim-share-windows-x64.zip
```

The resulting archive includes the source plus a root-level `rvsim.exe`. MinGW runtime libraries
are statically linked by this project's CMake configuration, so compiler DLLs do not need to be
copied from your installation. This executable is for Windows x64; other platforms require a
native source build. Windows is the platform tested in this delivery.

## How your friends run it

Extract the ZIP, enter its `rvsim` directory, and open PowerShell there:

```powershell
.\rvsim.exe --program demos/prebuilt/fibonacci.elf --format elf
```

Expected a0 result: `0x00000037`, decimal **55**. Running this supplied executable does not require
Python, CMake, or any compiler. It is a terminal application, so use the command with arguments
rather than double-clicking the EXE.

To explore pipeline mode:

```powershell
.\rvsim.exe --program demos/prebuilt/alu_chain.elf --format elf --model pipeline --debug
```

Use `help`, `step`, `tick`, `stages`, `registers`, `continue`, and `quit`. Start learning with the
[instruction reference](INSTRUCTION_REFERENCE.md), then `functional_tick()` in
[machine.cpp](../src/machine.cpp), before reading the pipeline scheduler.

Friends who want to edit/build need a C++17 compiler, CMake 3.20+, and Python 3.10+ for this project's
configure/test workflow. Follow the platform commands in [README](../README.md). They do not need a
RISC-V cross-toolchain to use prebuilt guest demos. Rebuilding those demos and running independent
Spike validation are separate optional workflows, documented in [Testing](TESTING.md).
