"""Format project sources, or check them with --check.

Requires clang-format 19 and the packages in requirements-format.txt.
Visual Studio's bundled clang-format is detected automatically on Windows.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def find_clang_format():
    installed = shutil.which("clang-format")
    if installed:
        return installed

    program_files = Path(os.environ.get("ProgramFiles", "C:/Program Files"))
    visual_studio = program_files / "Microsoft Visual Studio"
    candidates = sorted(visual_studio.glob("*/*/VC/Tools/Llvm/x64/bin/clang-format.exe"))
    return str(candidates[-1]) if candidates else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report changes without writing files")
    parser.add_argument("--clang-format", default=find_clang_format())
    args = parser.parse_args()
    if not args.clang_format:
        parser.error("clang-format not found; install version 19 or pass --clang-format PATH")

    cpp_files = sorted(
        path
        for directory in ("src", "include", "tests", "tools", "demos")
        for path in (ROOT / directory).rglob("*")
        if path.suffix in (".cpp", ".hpp", ".c")
    )
    cpp_flags = ["--dry-run", "--Werror"] if args.check else ["-i"]
    commands = [[args.clang_format, "--style=file", *cpp_flags, *map(str, cpp_files)]]

    # One file per invocation avoids multiprocessing and works in restricted terminals.
    for directory in ("scripts", "tests"):
        for path in sorted((ROOT / directory).glob("*.py")):
            commands.append(
                [sys.executable, "-m", "black", *(["--check"] if args.check else []), str(path)]
            )
    commands.append(
        [
            sys.executable,
            "-m",
            "cmakelang.format",
            "-c",
            str(ROOT / ".cmake-format.json"),
            "--check" if args.check else "-i",
            str(ROOT / "CMakeLists.txt"),
        ]
    )

    failed = False
    env = os.environ.copy()
    local_packages = ROOT / ".tools" / "formatters"
    if local_packages.is_dir():
        env["PYTHONPATH"] = str(local_packages) + os.pathsep + env.get("PYTHONPATH", "")
    for command in commands:
        if subprocess.run(command, cwd=ROOT, env=env).returncode:
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
