"""Independent Spike comparison. Exits 77 (NOT RUN) when adapter is absent.
Compares all eight successful prebuilt raw demos, retirement metadata and final RAM/registers.
"""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sim", required=True)
    p.add_argument("--adapter", default=str(ROOT / "build-spike/spike_adapter"))

    a = p.parse_args()
    adapter = Path(a.adapter).resolve()
    if not adapter.is_file():
        print(
            "NOT RUN: pinned Spike adapter unavailable. See docs/TESTING.md and scripts/setup_spike.sh"
        )
        return 77
    sim = str(Path(a.sim).resolve())

    with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
        stats, trace = Path(td) / "state.json", Path(td) / "trace.jsonl"

        for name in json.loads((ROOT / "demos/expected.json").read_text()):
            raw = str(ROOT / f"demos/prebuilt/{name}.bin")
            oracle = subprocess.run(
                [str(adapter), raw, "0x1000", "0x1000", "65536", "1000000"],
                capture_output=True,
                text=True,
                check=True,
                timeout=120,
            )
            spike = [json.loads(line) for line in oracle.stdout.splitlines()]
            oracle_state = spike.pop()
            assert oracle_state.pop("type") == "state"

            for model, forwarding in (
                ("functional", None),
                ("pipeline", "on"),
                ("pipeline", "off"),
            ):
                cmd = [
                    sim,
                    "--program",
                    raw,
                    "--format",
                    "bin",
                    "--load-address",
                    "0x1000",
                    "--entry",
                    "0x1000",
                    "--memory-size",
                    "65536",
                    "--model",
                    model,
                    "--trace",
                    str(trace),
                    "--trace-format",
                    "jsonl",
                    "--stats",
                    str(stats),
                ]
                if forwarding:
                    cmd += ["--forwarding", forwarding]

                subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=120)

                actual = []
                for line in trace.read_text().splitlines():
                    e = json.loads(line)
                    if e["type"] == "cycle":
                        continue
                    e.pop("op", None)
                    actual.append(e)
                if actual != spike:
                    mismatch = next(
                        (i for i, (x, y) in enumerate(zip(actual, spike)) if x != y),
                        min(len(actual), len(spike)),
                    )
                    raise AssertionError(
                        (
                            name,
                            model,
                            forwarding,
                            "first boundary mismatch",
                            mismatch,
                            actual[mismatch : mismatch + 1],
                            spike[mismatch : mismatch + 1],
                        )
                    )

                state = json.loads(stats.read_text())
                assert {k: state[k] for k in oracle_state} == oracle_state, (
                    name,
                    "final RAM/registers/PC mismatch",
                )

            print(f"PASS pinned Spike: {name} x 3 schedules")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
