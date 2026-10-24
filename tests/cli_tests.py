import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--sim", required=True)

    a = p.parse_args()
    sim = str(Path(a.sim).resolve())
    root = Path(__file__).resolve().parents[1]
    image = str(root / "demos/prebuilt/alu_chain.elf")

    def run(args, code=0, stdin=None):
        r = subprocess.run([sim] + args, input=stdin, capture_output=True, text=True)
        assert r.returncode == code, (args, code, r.returncode, r.stdout, r.stderr)
        return r.stdout

    assert "--forwarding" in run(["--help"])
    run([], 64)
    base = ["--program", image, "--format", "elf"]
    for extra in (
        ["--forwarding", "on"],
        ["--max-cycles", "5"],
        ["--entry", "0x1000"],
        ["--memory-size", "0"],
        ["--memory-size", "-1"],
        ["--model", "no"],
        ["--format", "bin"],
        ["--trace-format", "jsonl"],
        ["--max-instructions", "1x"],
        ["--forwarding", "yes"],
        ["--unknown", "x"],
        ["--trace", image],
    ):
        run(base + extra, 64)

    with tempfile.TemporaryDirectory(dir=Path.cwd()) as td:
        t = Path(td)
        stats = t / "stats.json"
        trace = t / "trace.jsonl"
        args = base + [
            "--model",
            "pipeline",
            "--stats",
            str(stats),
            "--trace",
            str(trace),
            "--trace-format",
            "jsonl",
        ]

        run(args + ["--max-cycles", "1"], 2)
        s = json.loads(stats.read_text())
        assert s["cycles"] == 1 and s["retired"] == 0 and s["cpi"] is None
        run(base + ["--max-instructions", "0"], 2)

        run(args + ["--debug"], stdin="breakpoint 0x100c\ncontinue\nregisters\nquit\n")
        s = json.loads(stats.read_text())
        assert s["retired"] == 3 and s["registers"][10] == 0

        run(args + ["--debug"], stdin="breakpoint 0x100c\ncontinue\ncontinue\nquit\n")
        s = json.loads(stats.read_text())
        assert s["registers"][10] == 7 and s["retired"] == 4

        run(args + ["--debug"], stdin="tick\nstages\nstep\nquit\n")
        s = json.loads(stats.read_text())
        assert s["retired"] == 1 and s["cycles"] == 5

        run(base + ["--debug", "--stats", str(stats)], stdin="step\nreset\nregisters\nquit\n")
        s = json.loads(stats.read_text())
        assert s["retired"] == 0 and all(v == 0 for v in s["registers"])

        bad = t / "bad.bin"
        bad.write_bytes(bytes.fromhex("ffffffff"))
        run(["--program", str(bad), "--format", "bin", "--stats", str(stats)], 1)
        s = json.loads(stats.read_text())
        assert s["trap"]["cause"] == "illegal_instruction" and s["pc"] == 4096

        bp_store = t / "store.bin"
        # addi x1,x0,64; addi x2,x0,9; sw x2,0(x1); ebreak
        bp_store.write_bytes(bytes.fromhex("930000041301900023a0200073001000"))
        run(
            [
                "--program",
                str(bp_store),
                "--format",
                "bin",
                "--model",
                "pipeline",
                "--debug",
                "--stats",
                str(stats),
            ],
            stdin="breakpoint 0x1008\ncontinue\nmemory 64 4\nquit\n",
        )
        s = json.loads(stats.read_text())
        assert s["retired"] == 2 and "64" not in s["memory_nonzero_bytes"]

        # A wrong-path breakpoint is never reached at WB.
        run(
            [
                "--program",
                str(root / "demos/prebuilt/branch_flush.elf"),
                "--format",
                "elf",
                "--model",
                "pipeline",
                "--debug",
                "--stats",
                str(stats),
            ],
            stdin="breakpoint 0x1010\ncontinue\nquit\n",
        )
        assert json.loads(stats.read_text())["registers"][10] == 42

    print("CLI, exits, limits, debugger/reset and pre-commit breakpoints passed")


if __name__ == "__main__":
    main()
