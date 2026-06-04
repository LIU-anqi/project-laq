#!/usr/bin/env python3
"""
Phase G same-mesh A/B comparison.

Compares two VTU results where geometry should be identical and the intended
only material change is nuclei labels 1..6 being merged back to brain label 10.
"""

from __future__ import annotations

import argparse
from array import array
from collections import Counter
import json
import math
from pathlib import Path
import sys
from typing import Any
from xml.etree import ElementTree as ET

from phase_g_vtu_material_audit import (
    NUCLEI_LABELS,
    array_from_bytes,
    audit_vtu,
    decode_fem_diagnostics,
    decode_vtk_binary,
    percentile,
    read_array,
)


def vtk_type_code(data_array: ET.Element) -> str:
    vtk_type = data_array.attrib.get("type", "")
    if vtk_type == "Float64":
        return "d"
    if vtk_type == "Float32":
        return "f"
    if vtk_type == "Int32":
        return "i"
    if vtk_type == "Int64":
        return "q"
    if vtk_type == "UInt32":
        return "I"
    if vtk_type == "UInt64":
        return "Q"
    raise RuntimeError(f"unsupported VTK DataArray type: {vtk_type!r}")


def read_named_array(root: ET.Element, name: str) -> tuple[ET.Element, array, bytes]:
    el = root.find(f'.//DataArray[@Name="{name}"]')
    if el is None:
        raise RuntimeError(f"missing DataArray Name={name!r}")
    raw = decode_vtk_binary(el)
    return el, array_from_bytes(raw, vtk_type_code(el)), raw


def load_vtu(path: Path) -> dict[str, Any]:
    root = ET.fromstring(path.read_text(encoding="utf-8"))
    piece = root.find(".//Piece")
    if piece is None:
        raise RuntimeError(f"{path}: VTU Piece element not found")

    points_el, raw_points = read_array(root, "Points")
    conn_el, raw_connectivity = read_array(root, "connectivity")
    _, labels, raw_labels = read_named_array(root, "MaterialLabel")
    _, e_mag, _ = read_named_array(root, "E_mag_Vmm")
    _, potential, _ = read_named_array(root, "Potential_V")

    return {
        "path": str(path),
        "root": root,
        "points": int(piece.attrib["NumberOfPoints"]),
        "cells": int(piece.attrib["NumberOfCells"]),
        "raw_points": raw_points,
        "raw_connectivity": raw_connectivity,
        "raw_labels": raw_labels,
        "point_type": points_el.attrib.get("type"),
        "connectivity_type": conn_el.attrib.get("type"),
        "labels": [int(v) for v in labels],
        "e_mag": [float(v) for v in e_mag],
        "potential": [float(v) for v in potential],
        "diagnostics": decode_fem_diagnostics(root),
    }


def diff_stats(a: list[float], b: list[float]) -> dict[str, Any]:
    if len(a) != len(b):
        raise RuntimeError("array length mismatch")
    abs_diffs = [abs(x - y) for x, y in zip(a, b)]
    signed = [x - y for x, y in zip(a, b)]
    return {
        "count": len(a),
        "nonzero_gt_1e-12": sum(1 for v in abs_diffs if v > 1e-12),
        "mean_abs": round(sum(abs_diffs) / len(abs_diffs), 12) if abs_diffs else 0.0,
        "p50_abs": round(percentile(abs_diffs, 50.0), 12),
        "p95_abs": round(percentile(abs_diffs, 95.0), 12),
        "p99_abs": round(percentile(abs_diffs, 99.0), 12),
        "max_abs": round(max(abs_diffs), 12) if abs_diffs else 0.0,
        "mean_signed_a_minus_b": round(sum(signed) / len(signed), 12) if signed else 0.0,
    }


def rel_delta(a: float, b: float) -> float:
    if abs(b) < 1e-15:
        return math.inf if abs(a) > 1e-15 else 0.0
    return (a - b) / b


def selected_diag(diag: dict[str, Any] | None) -> dict[str, Any]:
    if not diag:
        return {}
    named = diag.get("named", {})
    out = {
        "selectedContact": int(round(named.get("selectedContact", -1))),
        "selectedVoltage": named.get("selectedVoltage"),
        "amplitude": named.get("amplitude"),
        "pulseWidth": int(round(named.get("pulseWidth", 0))),
        "activeThresholdVolume": named.get("activeThresholdVolume"),
        "activeThresholdCells": int(round(named.get("activeThresholdCells", 0))),
        "p95": named.get("p95"),
        "p99": named.get("p99"),
        "p999": named.get("p999"),
        "robustMaxE": named.get("robustMaxE"),
        "electrodeSurfaceNodesNoBrain": int(round(named.get("electrodeSurfaceNodesNoBrain", 0))),
        "hollowedSurfaceNodes": int(round(named.get("hollowedSurfaceNodes", 0))),
        "contactBrainFacingRatio": named.get("contactBrainFacingRatio", []),
        "contactEffectiveRatio": named.get("contactEffectiveRatio", []),
        "contactFluxMA": named.get("contactFluxMA", []),
        "contactFluxFaces": named.get("contactFluxFaces", []),
        "contactFluxMissingFaces": named.get("contactFluxMissingFaces", []),
    }
    out.update(named.get("selectedContactDerived", {}))
    return out


def compare(a_path: Path, b_path: Path, threshold: float) -> dict[str, Any]:
    a = load_vtu(a_path)
    b = load_vtu(b_path)
    a_audit = audit_vtu(a_path, threshold)
    b_audit = audit_vtu(b_path, threshold)

    transitions = Counter(zip(a["labels"], b["labels"]))
    changed = {f"{x}->{y}": n for (x, y), n in sorted(transitions.items()) if x != y}
    unexpected = {
        f"{x}->{y}": n
        for (x, y), n in sorted(transitions.items())
        if x != y and not (x in NUCLEI_LABELS and y == 10)
    }

    a_counts = {int(k): int(v) for k, v in a_audit["label_counts"].items()}
    b_counts = {int(k): int(v) for k, v in b_audit["label_counts"].items()}
    preserved_labels = [50, 51, 101, 102, 103, 104]

    a_diag = selected_diag(a["diagnostics"])
    b_diag = selected_diag(b["diagnostics"])
    stable_diag_keys = [
        "selectedContact",
        "selectedVoltage",
        "amplitude",
        "pulseWidth",
        "electrodeSurfaceNodesNoBrain",
        "hollowedSurfaceNodes",
        "contactBrainFacingRatio",
        "contactEffectiveRatio",
        "contactFluxFaces",
        "contactFluxMissingFaces",
    ]
    stable_diag_equal = {key: a_diag.get(key) == b_diag.get(key) for key in stable_diag_keys}

    high_key = f"high_E_Ege{threshold:g}"
    a_high = a_audit[high_key]["volume_mm3"]
    b_high = b_audit[high_key]["volume_mm3"]
    a_flux = float(a_diag.get("fluxMA", 0.0))
    b_flux = float(b_diag.get("fluxMA", 0.0))

    checks = {
        "same_point_count": a["points"] == b["points"],
        "same_cell_count": a["cells"] == b["cells"],
        "same_point_coordinates_bytes": a["raw_points"] == b["raw_points"],
        "same_connectivity_bytes": a["raw_connectivity"] == b["raw_connectivity"],
        "nuclei_on_has_nuclei": a_audit["nuclei_cells"] > 0,
        "homogeneous_has_no_nuclei": b_audit["nuclei_cells"] == 0,
        "only_nuclei_to_brain_labels_changed": not unexpected,
        "brain_count_accounting": b_counts.get(10, 0) == a_counts.get(10, 0) + a_audit["nuclei_cells"],
        "preserved_non_nuclei_labels": all(a_counts.get(label, 0) == b_counts.get(label, 0) for label in preserved_labels),
        "diagnostics_stable_for_bc_geometry": all(stable_diag_equal.values()),
        "selected_effective_ratio_is_one": a_diag.get("effectiveRatio") == 1.0 and b_diag.get("effectiveRatio") == 1.0,
        "selected_flux_faces_not_missing": a_diag.get("fluxMissingFaces") == 0 and b_diag.get("fluxMissingFaces") == 0,
        "field_difference_present": diff_stats(a["e_mag"], b["e_mag"])["mean_abs"] > 1e-12,
    }

    return {
        "passed": all(checks.values()),
        "a_vtu": str(a_path),
        "b_vtu": str(b_path),
        "threshold": threshold,
        "checks": checks,
        "stable_diagnostic_checks": stable_diag_equal,
        "material": {
            "changed_label_transitions": changed,
            "unexpected_label_transitions": unexpected,
            "a_label_counts": a_counts,
            "b_label_counts": b_counts,
            "a_nuclei_cells": a_audit["nuclei_cells"],
            "a_nuclei_volume_mm3": a_audit["nuclei_volume_mm3"],
        },
        "geometry": {
            "a_points": a["points"],
            "b_points": b["points"],
            "a_cells": a["cells"],
            "b_cells": b["cells"],
            "point_type": a["point_type"],
            "connectivity_type": a["connectivity_type"],
        },
        "field_difference": {
            "E_mag_Vmm": diff_stats(a["e_mag"], b["e_mag"]),
            "Potential_V": diff_stats(a["potential"], b["potential"]),
        },
        "physical_summary": {
            "a_high_E_volume_mm3": a_high,
            "b_high_E_volume_mm3": b_high,
            "high_E_volume_delta_mm3": round(a_high - b_high, 6),
            "high_E_volume_relative_delta_a_minus_b": round(rel_delta(a_high, b_high), 9),
            "a_activeThresholdVolume_diag": a_diag.get("activeThresholdVolume"),
            "b_activeThresholdVolume_diag": b_diag.get("activeThresholdVolume"),
            "a_activeThresholdCells_diag": a_diag.get("activeThresholdCells"),
            "b_activeThresholdCells_diag": b_diag.get("activeThresholdCells"),
            "a_selected_flux_mA": round(a_flux, 12),
            "b_selected_flux_mA": round(b_flux, 12),
            "selected_flux_delta_mA": round(a_flux - b_flux, 12),
            "selected_flux_relative_delta_a_minus_b": round(rel_delta(a_flux, b_flux), 9),
        },
        "diagnostics": {
            "a": a_diag,
            "b": b_diag,
        },
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    phys = report["physical_summary"]
    lines = [
        "# Phase G Same-Mesh A/B Compare",
        "",
        f"Passed: `{report['passed']}`",
        "",
        "## Checks",
        "",
        "| Check | Passed |",
        "|---|---:|",
    ]
    for key, value in report["checks"].items():
        lines.append(f"| {key} | `{value}` |")
    lines.extend([
        "",
        "## Material Change",
        "",
        f"A nuclei cells: `{report['material']['a_nuclei_cells']}`",
        f"A nuclei volume: `{report['material']['a_nuclei_volume_mm3']:.3f} mm3`",
        f"Changed transitions: `{json.dumps(report['material']['changed_label_transitions'], ensure_ascii=False)}`",
        f"Unexpected transitions: `{json.dumps(report['material']['unexpected_label_transitions'], ensure_ascii=False)}`",
        "",
        "## Physical Difference",
        "",
        f"High-E volume A: `{phys['a_high_E_volume_mm3']:.3f} mm3`",
        f"High-E volume B: `{phys['b_high_E_volume_mm3']:.3f} mm3`",
        f"High-E delta A-B: `{phys['high_E_volume_delta_mm3']:.3f} mm3`",
        f"High-E relative delta A-B: `{phys['high_E_volume_relative_delta_a_minus_b']:.6f}`",
        f"Selected flux A: `{phys['a_selected_flux_mA']:.9f} mA`",
        f"Selected flux B: `{phys['b_selected_flux_mA']:.9f} mA`",
        f"Selected flux delta A-B: `{phys['selected_flux_delta_mA']:.9f} mA`",
        "",
        "## Field Difference",
        "",
        f"E mean abs diff: `{report['field_difference']['E_mag_Vmm']['mean_abs']:.9f} V/mm`",
        f"E p95 abs diff: `{report['field_difference']['E_mag_Vmm']['p95_abs']:.9f} V/mm`",
        f"E max abs diff: `{report['field_difference']['E_mag_Vmm']['max_abs']:.9f} V/mm`",
        f"Potential mean abs diff: `{report['field_difference']['Potential_V']['mean_abs']:.9f} V`",
        f"Potential max abs diff: `{report['field_difference']['Potential_V']['max_abs']:.9f} V`",
        "",
        "## BC Diagnostics",
        "",
        f"Selected effective ratio A/B: `{report['diagnostics']['a'].get('effectiveRatio')}` / `{report['diagnostics']['b'].get('effectiveRatio')}`",
        f"Selected brain-facing ratio A/B: `{report['diagnostics']['a'].get('brainFacingRatio')}` / `{report['diagnostics']['b'].get('brainFacingRatio')}`",
        f"Flux missing faces A/B: `{report['diagnostics']['a'].get('fluxMissingFaces')}` / `{report['diagnostics']['b'].get('fluxMissingFaces')}`",
    ])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare same-mesh Phase G nuclei-on vs homogeneous VTUs.")
    parser.add_argument("--a-vtu", required=True, type=Path, help="Nuclei-on VTU")
    parser.add_argument("--b-vtu", required=True, type=Path, help="Homogeneous VTU")
    parser.add_argument("--threshold", type=float, default=0.2, help="High-E threshold in V/mm")
    parser.add_argument("--json", type=Path, help="Optional JSON report output")
    parser.add_argument("--md", type=Path, help="Optional Markdown report output")
    args = parser.parse_args()

    report = compare(args.a_vtu, args.b_vtu, args.threshold)
    text = json.dumps(report, ensure_ascii=False, indent=2)
    print(text)
    if args.json:
        args.json.write_text(text + "\n", encoding="utf-8")
    if args.md:
        write_markdown(report, args.md)
    return 0 if report["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
