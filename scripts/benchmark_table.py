"""Render only actually collected pipeline measurements as a Markdown table."""

import argparse
import json
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("path", type=Path, nargs="?", default=Path("docs/measurements.json"))
    a = p.parse_args()
    data = json.loads(a.path.read_text())
    rows = {}
    for r in data["records"]:
        if r["model"] == "pipeline":
            rows.setdefault(r["demo"], {})[r["forwarding"]] = r
    print("| Demo | Retired | Cycles on / off | CPI on / off | Stalls on / off | Redirects |")
    print("|---|---:|---:|---:|---:|---:|")
    for name, pair in rows.items():
        x, y = pair["on"], pair["off"]
        cpi = lambda r: "n/a" if r["cpi"] is None else f"{r['cpi']:.3f}"
        print(
            f"| {name} | {x['retired']} | {x['cycles']} / {y['cycles']} | {cpi(x)} / {cpi(y)} | {sum(x['stalls'].values())} / {sum(y['stalls'].values())} | {x['redirects']} |"
        )


if __name__ == "__main__":
    main()
