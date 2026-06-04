#!/usr/bin/env python3
"""
Create a same-geometry homogeneous control mesh for Phase G.

Only Tetrahedra labels 1..6 are changed to 10. Vertices, triangles, contact
labels, encapsulation labels, and all formatting outside Tetrahedra rows are
left as close as possible to the input text.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


NUCLEI = {1, 2, 3, 4, 5, 6}


def rewrite_mesh(input_path: Path, output_path: Path) -> dict:
    lines = input_path.read_text(encoding="utf-8", errors="replace").splitlines()
    out: list[str] = []
    changed = 0
    original_counts: dict[int, int] = {}
    new_counts: dict[int, int] = {}
    i = 0
    while i < len(lines):
        out.append(lines[i])
        if lines[i].strip() == "Tetrahedra":
            i += 1
            out.append(lines[i])
            n = int(lines[i].strip())
            for _ in range(n):
                i += 1
                parts = lines[i].split()
                lab = int(parts[4])
                original_counts[lab] = original_counts.get(lab, 0) + 1
                if lab in NUCLEI:
                    parts[4] = "10"
                    lab = 10
                    changed += 1
                new_counts[lab] = new_counts.get(lab, 0) + 1
                out.append(" ".join(parts))
        i += 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(out) + "\n", encoding="utf-8")
    return {
        "input": str(input_path),
        "output": str(output_path),
        "changedNucleiToBrain": changed,
        "originalTetLabelCounts": {str(k): v for k, v in sorted(original_counts.items())},
        "newTetLabelCounts": {str(k): v for k, v in sorted(new_counts.items())},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--json")
    args = parser.parse_args()
    result = rewrite_mesh(Path(args.input), Path(args.output))
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
