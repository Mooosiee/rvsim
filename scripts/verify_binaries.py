"""Check source/artifact SHA-256 manifest and ELF-to-raw correspondence without a toolchain.
Use build_demos.py --check as the stronger source-to-machine-code rebuild check.
"""

import hashlib
import json
from pathlib import Path
from build_demos import raw_image

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = json.loads((ROOT / "demos/prebuilt/provenance.json").read_text())
    for group in ("sources", "artifacts"):
        for path, expected in p[group].items():
            actual = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
            if actual != expected:
                raise SystemExit(f"provenance mismatch: {path}; rebuild demos")
    for name in p["commands"]:
        base, raw = raw_image(ROOT / f"demos/prebuilt/{name}.elf")
        assert base == 4096 and raw == (ROOT / f"demos/prebuilt/{name}.bin").read_bytes(), name
    print("Source/artifact hashes and all ELF/raw pairs verified (not an independent rebuild)")


if __name__ == "__main__":
    main()
