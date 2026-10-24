"""Run every prebuilt ELF and raw image in functional/pipeline on/off, checking all effects."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def word(state, address):
    return sum(state["memory_nonzero_bytes"].get(str(address + i), 0) << (8 * i) for i in range(4))


def effects(path):
    return [
        e for line in path.read_text().splitlines() if (e := json.loads(line))["type"] != "cycle"
    ]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sim", required=True)
    p.add_argument("--bench", type=Path, help="save measured records as JSON")

    args = p.parse_args()
    sim = str(Path(args.sim).resolve())
    expected = json.loads((ROOT / "demos/expected.json").read_text())
    measurements = []

    with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
        tmp = Path(td)

        for name, spec in expected.items():
            reference_state = reference_events = None
            for fmt in ("elf", "bin"):
                for model, forwarding in (
                    ("functional", None),
                    ("pipeline", "on"),
                    ("pipeline", "off"),
                ):
                    stats, trace = tmp / "stats.json", tmp / "trace.jsonl"
                    cmd = [
                        sim,
                        "--program",
                        str(ROOT / f"demos/prebuilt/{name}.{fmt}"),
                        "--format",
                        fmt,
                        "--model",
                        model,
                        "--stats",
                        str(stats),
                        "--trace",
                        str(trace),
                        "--trace-format",
                        "jsonl",
                    ]
                    if fmt == "bin":
                        cmd += ["--load-address", "0x1000", "--entry", "0x1000"]
                    if forwarding:
                        cmd += ["--forwarding", forwarding]

                    result = subprocess.run(cmd, text=True, capture_output=True)
                    if result.returncode:
                        raise AssertionError(f"{cmd}\n{result.stdout}\n{result.stderr}")

                    state, events = json.loads(stats.read_text()), effects(trace)
                    assert state["status"] == "completed" and state["trap"]["cause"] == "ebreak", (
                        name,
                        state,
                    )

                    for register, value in spec["registers"].items():
                        assert state["registers"][int(register)] == value & 0xFFFFFFFF, (
                            name,
                            register,
                            state,
                        )
                    for addr, value in spec.get("words", {}).items():
                        assert word(state, int(addr, 0)) == value & 0xFFFFFFFF, (name, addr, state)
                    for addr, values in spec.get("arrays", {}).items():
                        assert [word(state, int(addr, 0) + 4 * i) for i in range(len(values))] == [
                            v & 0xFFFFFFFF for v in values
                        ], name

                    architectural = {
                        k: state[k]
                        for k in (
                            "registers",
                            "memory_nonzero_bytes",
                            "pc",
                            "retired",
                            "instruction_mix",
                            "trap",
                        )
                    }
                    if reference_state is None:
                        reference_state, reference_events = architectural, events
                    else:
                        assert architectural == reference_state, (
                            name,
                            fmt,
                            model,
                            forwarding,
                            "final state differs",
                        )
                        assert events == reference_events, (
                            name,
                            fmt,
                            model,
                            forwarding,
                            "commit effects differ",
                        )

                    if forwarding and "timing" in spec:
                        assert state["cycles"] == spec["timing"][forwarding], (
                            name,
                            forwarding,
                            state["cycles"],
                            spec["timing"],
                        )

                    if fmt == "elf":
                        measurements.append(
                            {
                                "demo": name,
                                "model": model,
                                "forwarding": forwarding,
                                **{
                                    k: state[k]
                                    for k in (
                                        "retired",
                                        "cycles",
                                        "cpi",
                                        "stalls",
                                        "redirects",
                                        "flushed",
                                    )
                                },
                            }
                        )

            print(f"PASS {name}: ELF/raw x functional/pipeline on/off; all commits, registers, RAM")

    if args.bench:
        args.bench.parent.mkdir(parents=True, exist_ok=True)
        source_paths = sorted(
            list((ROOT / "src").glob("*.cpp")) + list((ROOT / "include/rv").glob("*.hpp"))
        )
        args.bench.write_text(
            json.dumps(
                {
                    "provenance": json.loads((ROOT / "demos/prebuilt/provenance.json").read_text()),
                    "simulator_sha256": hashlib.sha256(Path(sim).read_bytes()).hexdigest(),
                    "source_sha256": {
                        str(p.relative_to(ROOT))
                        .replace("\\", "/"): hashlib.sha256(p.read_bytes())
                        .hexdigest()
                        for p in source_paths
                    },
                    "host": platform.platform(),
                    "python": platform.python_version(),
                    "configuration": {
                        "ram_bytes": 65536,
                        "max_instructions": 1000000,
                        "max_cycles": 10000000,
                        "isa": "RV32I",
                        "entry": 4096,
                    },
                    "records": measurements,
                },
                indent=2,
            )
            + "\n"
        )

    print("48 demo runs passed")


if __name__ == "__main__":
    main()
