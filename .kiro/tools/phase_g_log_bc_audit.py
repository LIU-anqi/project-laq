#!/usr/bin/env python3
"""
Phase G log/BC regression audit.

Compares a nuclei-off run.log and a nuclei-on run.log. The goal is to prove
that Phase G changes tetra material labels only, while surface labels and
contact boundary-condition diagnostics remain in the Phase F validated state.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
from typing import Any


ALLOWED_TRI_LABELS = {-1, 50, 51, 101, 102, 103, 104}
NUCLEI_LABELS = {1, 2, 3, 4, 5, 6}


def parse_int_counts(lines: list[str], pattern: str) -> dict[int, int]:
    out: dict[int, int] = {}
    rx = re.compile(pattern)
    for line in lines:
        m = rx.search(line)
        if m:
            out[int(m.group(1))] = int(m.group(2))
    return out


def parse_float(text: str) -> float:
    return float(text.strip().strip('"'))


def parse_log(path: Path) -> dict[str, Any]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

    phase_g = {
        "enabled": any("nuclei centroid backfill enabled" in x for x in lines),
        "disabled": any("nuclei centroid backfill disabled" in x for x in lines),
    }
    backfill = {}
    for line in lines:
        m = re.search(
            r"candidates\(brain tet\)=\s*(\d+)\s+relabeled=\s*(\d+)\s+ratio=\s*\"?([0-9.eE+-]+)\"?\s+outOfExtent=\s*(\d+)\s+nonNucleiSamples=\s*(\d+)",
            line,
        )
        if m:
            backfill = {
                "candidates": int(m.group(1)),
                "relabeled": int(m.group(2)),
                "ratio": float(m.group(3)),
                "outOfExtent": int(m.group(4)),
                "nonNucleiSamples": int(m.group(5)),
            }

    conforming_tri = parse_int_counts(lines, r"\[Conforming\]\s+label\s+(-?\d+)\s*:\s*(\d+)")
    fem_tri = parse_int_counts(lines, r"\[FEM\]\s+tri label\s+(-?\d+)\s*:\s*(\d+)")
    fem_tet = parse_int_counts(lines, r"\[FEM\]\s+tet label\s+(-?\d+)\s*:\s*(\d+)")
    phase_g_tet = parse_int_counts(lines, r"\[Phase G\]\s+sub\s+(-?\d+)\s*:\s*(\d+)")

    brain_share: dict[int, dict[str, Any]] = {}
    effective: dict[int, dict[str, Any]] = {}
    floating_phi: dict[int, dict[str, Any]] = {}
    for line in lines:
        m = re.search(
            r"Contact\s+(\d+)\s+surface brain-share:\s+all=\s*(\d+)\s+shared=\s*(\d+)\s+noBrain=\s*(\d+)\s+brainFacingRatio=\s*\"?([0-9.eE+-]+)\"?",
            line,
        )
        if m:
            c = int(m.group(1))
            brain_share[c] = {
                "all": int(m.group(2)),
                "shared": int(m.group(3)),
                "noBrain": int(m.group(4)),
                "brainFacingRatio": float(m.group(5)),
            }

        m = re.search(
            r"Contact\s+(\d+)\s+effective contact Dirichlet BC nodes:\s+(\d+)\s*/\s*(\d+).*effectiveRatio=\s*\"?([0-9.eE+-]+)\"?",
            line,
        )
        if m:
            c = int(m.group(1))
            effective[c] = {
                "effective": int(m.group(2)),
                "total": int(m.group(3)),
                "effectiveRatio": float(m.group(4)),
            }

        m = re.search(
            r"Contact\s+(\d+)\s+floating phi:\s+nodes=\s*(\d+).*span=\s*([0-9.eE+-]+)",
            line,
        )
        if m:
            c = int(m.group(1))
            floating_phi[c] = {
                "nodes": int(m.group(2)),
                "span": float(m.group(3)),
            }

    summary = {
        "selectedContact": None,
        "effectiveRatio": None,
        "brainFacingRatio": None,
        "contactFluxMA": {},
    }
    for line in lines:
        m = re.search(
            r"selected contact BC.*brainFacingRatio=\s*([0-9.eE+-]+)\s+effectiveRatio=\s*([0-9.eE+-]+)",
            line,
        )
        if m:
            summary["brainFacingRatio"] = float(m.group(1))
            summary["effectiveRatio"] = float(m.group(2))

        if "[VTA SUMMARY] 触点: selected=" in line:
            m = re.search(r"selected=\s*(\d+)", line)
            if m:
                summary["selectedContact"] = int(m.group(1))

        if "[VTA SUMMARY] contact flux:" in line:
            for c, v in re.findall(r"C(\d)=\s*([0-9.eE+-]+)\s*mA", line):
                summary["contactFluxMA"][int(c)] = float(v)

    warnings = [
        line for line in lines
        if re.search(r"unknown|fallback|surface pair", line, re.IGNORECASE)
    ]

    return {
        "path": str(path),
        "phaseG": phase_g,
        "backfill": backfill,
        "conformingTriangleLabels": conforming_tri,
        "femTriangleLabels": fem_tri,
        "femTetLabels": fem_tet,
        "phaseGTetLabels": phase_g_tet,
        "brainShare": brain_share,
        "effectiveContact": effective,
        "floatingPhi": floating_phi,
        "summary": summary,
        "warnings": warnings,
    }


def near(a: float | None, b: float | None, tol: float) -> bool:
    if a is None or b is None:
        return False
    return abs(a - b) <= tol


def audit_pair(off: dict[str, Any], on: dict[str, Any]) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []

    def add(name: str, ok: bool, detail: str) -> None:
        checks.append({"name": name, "ok": bool(ok), "detail": detail})

    off_tri = off["femTriangleLabels"]
    on_tri = on["femTriangleLabels"]
    add("off disabled", off["phaseG"]["disabled"], "nuclei-off log should say disabled")
    add("on enabled", on["phaseG"]["enabled"], "nuclei-on log should say enabled")
    add("on relabeled > 0", on["backfill"].get("relabeled", 0) > 0, str(on["backfill"]))
    add("on outOfExtent == 0", on["backfill"].get("outOfExtent", 0) == 0, str(on["backfill"]))

    add("triangle labels unchanged", off_tri == on_tri, f"off={off_tri}, on={on_tri}")
    tri_labels_ok = set(on_tri) <= ALLOWED_TRI_LABELS and not (set(on_tri) & NUCLEI_LABELS)
    add("triangle labels allowed", tri_labels_ok, f"on tri labels={sorted(on_tri)}")

    off_tet = off["femTetLabels"]
    on_tet = on["femTetLabels"]
    add("off has no nuclei tet labels", not (set(off_tet) & NUCLEI_LABELS), f"off tet labels={sorted(off_tet)}")
    add("on has nuclei tet labels", all(on_tet.get(x, 0) > 0 for x in NUCLEI_LABELS), f"on tet labels={on_tet}")
    add("non-nuclei tets preserved",
        all(off_tet.get(x) == on_tet.get(x) for x in (50, 51, 101, 102, 103, 104)),
        f"off={off_tet}, on={on_tet}")
    if 10 in off_tet and 10 in on_tet:
        relabeled = sum(on_tet.get(x, 0) for x in NUCLEI_LABELS)
        add("brain tet accounting",
            off_tet[10] == on_tet[10] + relabeled,
            f"off brain={off_tet[10]}, on brain={on_tet[10]}, nuclei={relabeled}")

    off_summary = off["summary"]
    on_summary = on["summary"]
    add("selected effectiveRatio stable",
        near(off_summary.get("effectiveRatio"), on_summary.get("effectiveRatio"), 1e-6)
        and on_summary.get("effectiveRatio") == 1.0,
        f"off={off_summary.get('effectiveRatio')}, on={on_summary.get('effectiveRatio')}")
    add("selected brainFacingRatio stable",
        near(off_summary.get("brainFacingRatio"), on_summary.get("brainFacingRatio"), 5e-4),
        f"off={off_summary.get('brainFacingRatio')}, on={on_summary.get('brainFacingRatio')}")

    selected = on_summary.get("selectedContact")
    flux_ok = True
    flux_detail = []
    for c, flux in on_summary.get("contactFluxMA", {}).items():
        if selected is not None and c == selected:
            continue
        ok = abs(flux) < 1e-9
        flux_ok = flux_ok and ok
        flux_detail.append(f"C{c}={flux:g}")
    add("inactive contact flux near zero", flux_ok, ", ".join(flux_detail))

    add("no unknown/fallback warnings", not off["warnings"] and not on["warnings"],
        f"off warnings={len(off['warnings'])}, on warnings={len(on['warnings'])}")

    return {
        "off": off,
        "on": on,
        "checks": checks,
        "passed": all(x["ok"] for x in checks),
    }


def write_markdown(result: dict[str, Any], path: Path) -> None:
    lines = [
        "# Phase G Surface/BC Log Audit",
        "",
        f"Passed: `{result['passed']}`",
        "",
        "## Checks",
        "",
        "| Check | OK | Detail |",
        "|---|---:|---|",
    ]
    for check in result["checks"]:
        detail = str(check["detail"]).replace("\n", " ")
        lines.append(f"| {check['name']} | {check['ok']} | `{detail}` |")

    lines.extend([
        "",
        "## Key Counts",
        "",
        f"Off FEM tri labels: `{result['off']['femTriangleLabels']}`",
        f"On FEM tri labels: `{result['on']['femTriangleLabels']}`",
        f"Off FEM tet labels: `{result['off']['femTetLabels']}`",
        f"On FEM tet labels: `{result['on']['femTetLabels']}`",
    ])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--off-log", required=True)
    parser.add_argument("--on-log", required=True)
    parser.add_argument("--json")
    parser.add_argument("--md")
    args = parser.parse_args()

    result = audit_pair(parse_log(Path(args.off_log)), parse_log(Path(args.on_log)))

    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    if args.md:
        write_markdown(result, Path(args.md))

    print(json.dumps({
        "passed": result["passed"],
        "checks": result["checks"],
    }, indent=2, ensure_ascii=False))
    return 0 if result["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
