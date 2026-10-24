"""Create a shareable source ZIP, optionally with a portable Windows executable.

Only the listed project folders/files are included. Local compilers, build caches,
Python caches, and earlier packages are excluded.
"""

import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_FOLDERS = ("src", "include", "demos", "docs", "scripts", "tests", "tools", ".github")
SOURCE_FILES = (
    "CMakeLists.txt",
    "README.md",
    "START_HERE.txt",
    ".gitignore",
    ".clang-format",
    ".cmake-format.json",
    ".editorconfig",
    "pyproject.toml",
    "requirements-format.txt",
)


def source_files():
    for name in SOURCE_FILES:
        yield ROOT / name

    # Preserve a project license/notice if one is added later.
    for name in ("LICENSE", "LICENSE.md", "LICENSE.txt", "NOTICE"):
        path = ROOT / name
        if path.is_file():
            yield path

    for folder in SOURCE_FOLDERS:
        for path in sorted((ROOT / folder).rglob("*")):
            if not path.is_file() or "__pycache__" in path.parts:
                continue
            if path.suffix in (".pyc", ".pyo"):
                continue
            yield path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "out/rvsim-source.zip")
    parser.add_argument("--windows-exe", type=Path, help="include a built Windows x64 rvsim.exe")
    args = parser.parse_args()

    files = [(path, path.relative_to(ROOT).as_posix()) for path in source_files()]
    if args.windows_exe:
        executable = args.windows_exe.resolve()
        if not executable.is_file():
            parser.error(f"executable not found: {executable}")
        files.append((executable, "rvsim.exe"))

    output = args.output.resolve()
    if output in {path.resolve() for path, _ in files}:
        parser.error("output ZIP must not replace a packaged source file or executable")

    output.parent.mkdir(parents=True, exist_ok=True)
    hashes = {}
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, relative in files:
            data = path.read_bytes()
            archive.writestr(f"rvsim/{relative}", data)
            hashes[relative] = hashlib.sha256(data).hexdigest()

        info = {
            "package": "source-and-windows-x64" if args.windows_exe else "source",
            "start_here": "START_HERE.txt",
            "sha256": hashes,
        }
        archive.writestr("rvsim/PACKAGE_INFO.json", json.dumps(info, indent=2) + "\n")

    with zipfile.ZipFile(output) as archive:
        bad_file = archive.testzip()
        if bad_file:
            raise RuntimeError(f"ZIP integrity check failed: {bad_file}")

    print(f"Created {output} ({output.stat().st_size:,} bytes; {len(files)} project files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
