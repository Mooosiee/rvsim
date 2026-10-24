"""Build real RV32I ELF/raw demos. GNU is the primary workflow; Zig is optional.
Usage: python scripts/build_demos.py [--cc riscv64-unknown-elf-gcc]
       python scripts/build_demos.py --zig /path/to/zig
       python scripts/build_demos.py --check (requires recorded toolchain)
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
NAMES = [
    "fibonacci",
    "array_sum",
    "bubble_sort",
    "nth_prime",
    "alu_chain",
    "load_use",
    "branch_flush",
    "freestanding",
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def raw_image(path):
    """Extract PT_LOAD bytes, preserving address gaps; no external objcopy needed."""
    data = path.read_bytes()
    off = struct.unpack_from("<I", data, 28)[0]
    size, count = struct.unpack_from("<HH", data, 42)
    segments = []
    for i in range(count):
        kind, fileoff, va, pa, filesz, memsz, flags, align = struct.unpack_from(
            "<8I", data, off + i * size
        )
        if kind == 1 and filesz:
            segments.append((pa, data[fileoff : fileoff + filesz]))

    base = min(a for a, _ in segments)
    out = bytearray(max(a + len(b) for a, b in segments) - base)
    for address, chunk in segments:
        out[address - base : address - base + len(chunk)] = chunk

    return base, bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("RISCV_CC", "riscv64-unknown-elf-gcc"))
    parser.add_argument("--zig")
    parser.add_argument(
        "--check",
        action="store_true",
        help="rebuild and compare bytes instead of replacing artifacts",
    )

    args = parser.parse_args()

    compiler = [str(Path(args.zig).resolve()), "cc"] if args.zig else [args.cc]
    if not Path(compiler[0]).is_file() and not shutil.which(compiler[0]):
        parser.error(f"cross compiler unavailable: {compiler[0]}; see docs/WRITING_PROGRAMS.md")

    version = subprocess.check_output(
        [compiler[0], "version"] if args.zig else compiler + ["--version"], text=True
    ).strip()

    common = [
        "-march=rv32i",
        "-mabi=ilp32",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fno-pic",
        "-fno-pie",
        "-msmall-data-limit=0",
        "-nostdlib",
        "-Wl,--no-relax",
        "-Wl,--build-id=none",
        "-Wl,-T,demos/link.ld",
    ]
    if args.zig:
        common = ["-target", "riscv32-freestanding-none", "-mcpu=generic_rv32", "-mno-relax"] + [
            flag for flag in common if flag not in ("-Wl,--no-relax", "-march=rv32i")
        ]

    provenance = {
        "compiler": version,
        "driver": "zig cc" if args.zig else args.cc,
        "flags": common,
        "sources": {},
        "artifacts": {},
        "commands": {},
    }

    with tempfile.TemporaryDirectory(dir=ROOT) as td:
        output = Path(td)
        env = os.environ.copy()
        env.update(TMP=td, TEMP=td, TMPDIR=td)
        if args.zig:
            env["ZIG_GLOBAL_CACHE_DIR"] = str(output / "zig-cache")

        for name in NAMES:
            sources = (
                ["demos/start.S", "demos/freestanding.c"]
                if name == "freestanding"
                else [f"demos/{name}.S"]
            )
            cmd = compiler + common + ["-O1", "-g0"] + sources + ["-o", str(output / f"{name}.elf")]

            print("Building", name, flush=True)
            subprocess.run(cmd, cwd=ROOT, check=True, env=env)

            base, raw = raw_image(output / f"{name}.elf")
            if base != 0x1000:
                raise RuntimeError("unexpected image base")
            (output / f"{name}.bin").write_bytes(raw)

            provenance["commands"][name] = (
                ["zig", "cc"]
                + common
                + ["-O1", "-g0"]
                + sources
                + ["-o", f"demos/prebuilt/{name}.elf"]
                if args.zig
                else [args.cc]
                + common
                + ["-O1", "-g0"]
                + sources
                + ["-o", f"demos/prebuilt/{name}.elf"]
            )

            for s in sources + ["demos/link.ld"]:
                provenance["sources"][s] = digest(ROOT / s)

            for suffix in ("elf", "bin"):
                relative = f"demos/prebuilt/{name}.{suffix}"
                built = output / f"{name}.{suffix}"
                provenance["artifacts"][relative] = digest(built)
                if args.check:
                    if built.read_bytes() != (ROOT / relative).read_bytes():
                        raise RuntimeError(
                            f"rebuild differs: {relative} (use same compiler/version as provenance)"
                        )
                else:
                    (ROOT / relative).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(built, ROOT / relative)

    if not args.check:
        (ROOT / "demos/prebuilt/provenance.json").write_text(
            json.dumps(provenance, indent=2) + "\n", encoding="utf-8"
        )

    print(
        "All demos rebuilt" + ("; byte comparison passed" if args.check else "; provenance saved")
    )


if __name__ == "__main__":
    main()
