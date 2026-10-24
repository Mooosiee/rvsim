# Upstream, licensing, and authorship

## Starting point

The workspace at `F:\RISC-V Simulator` was inspected before implementation on 2026-10-24. It was empty: no source, Git repository, AGENTS.md, license, upstream URL, or revision was supplied. Consequently there is no base simulator revision to inspect or claim credit for. No existing simulator implementation was copied or rewritten. The core, CLI, demos, tests, scripts, and guides were created in this workspace with Codex assistance under the user's requirements.

If this project was intended to extend a particular upstream repository, that URL/revision still needs to be supplied and evaluated; do not describe this implementation as a fork of an unidentified project. No project distribution license was chosen on the user's behalf. Select an appropriate license before publishing/distributing; this statement is separate from the licenses of development/reference tools.

## References actually consulted

| Work | Inspected version / use | Licensing and reuse |
|---|---|---|
| [RISC-V unprivileged RV32I specification](https://docs.riscv.org/reference/isa/v20260120/unpriv/rv32.html) | RV32I v2.1; instruction behavior, branch/JALR alignment, base ISA environment | Reference specification only; no specification prose or diagrams copied into implementation |
| [Spike / riscv-isa-sim](https://github.com/riscv-software-src/riscv-isa-sim) | v1.1.0, exact tag revision `530af85d83781a3dae31a4ace84a573ec255fefa`; inspected processor/simif/MMU/execute API source | BSD-style upstream license; adapter links externally built Spike; Spike implementation is not vendored/reused in rvcore |
| [Zig](https://ziglang.org/download/) | 0.14.1; portable Clang/LLD RV32I cross compilation and exact rebuild | External build tool, ignored `.tools` install; upstream licenses apply to Zig/LLVM; not shipped as project source |
| [WinLibs](https://www.winlibs.com/) | GCC 16.2.0 / MinGW-w64 UCRT package installed via WinGet | External native compiler/tool distribution; compiler/source licenses remain upstream; not vendored |

The Spike version pin was checked with `git ls-remote ... refs/tags/v1.1.0`. Its adapter source matches the inspected interface, but compilation/execution remains unverified here. Consult the pinned upstream [LICENSE](https://github.com/riscv-software-src/riscv-isa-sim/blob/530af85d83781a3dae31a4ace84a573ec255fefa/LICENSE) for exact terms before distributing linked adapters. This document records findings, not legal advice or relicensing of upstream work.

## Original work and corrections

Original project components include the portable typed register/memory model, RV32I decoder and shared effects, functional and explicit current/next pipeline schedulers, precise WB commit policy, all-consumer forwarding and hazard controls, loaders, CLI/debugger/event format, demonstration assembly/C startup, reproducible builder/manifest, core/differential/timing tests, benchmark collection, CI definition, and the independent Spike platform adapter/harness.

The implementation directly addresses common teaching-simulator mistakes: load addresses are never bypassed as data; x0 is not a producer; disabled forwarding stalls instead of changing results; stores commit in order; terminal instructions and faults remain speculative until WB; younger faults can be discarded by older redirects; branch target alignment is checked only when taken; failed stores are atomic; and breakpoints observe commit rather than fetch. These are original design choices/fixes to the usual problem class, not corrections attributed to an unseen upstream project.

Portfolio ownership should be described accurately: this is an AI-assisted implementation you can study, test, modify, and explain. Do not claim independent manual authorship of every line or upstream ISA certification. [PROJECT_SUMMARY.md](PROJECT_SUMMARY.md) gives factual interview phrasing and scope limits.
