#!/usr/bin/env python3
"""
Phase G VTU material audit.

Reads a VTK XML UnstructuredGrid (.vtu) written by this project and audits
MaterialLabel/E_mag_Vmm directly from the result file. This intentionally uses
only Python standard-library modules, because the local Windows Python may not
have vtk or numpy installed.
"""

from __future__ import annotations

import argparse
import base64
from array import array
import json
import math
from pathlib import Path
import re
import struct
import sys
from typing import Any
from xml.etree import ElementTree as ET
import zlib


NUCLEI_LABELS = {1, 2, 3, 4, 5, 6}
BRAINLIKE_LABELS = {1, 2, 3, 4, 5, 6, 10}
LABEL_NAMES = {
    1: "Left Red Nucleus",
    2: "Left Substantia Nigra",
    3: "Left STN",
    4: "Right Red Nucleus",
    5: "Right Substantia Nigra",
    6: "Right STN",
    10: "Brain",
    50: "Lead Insulator",
    51: "Encapsulation",
    101: "Contact 0",
    102: "Contact 1",
    103: "Contact 2",
    104: "Contact 3",
}
DIAGNOSTIC_NAMES = {
    0: "numPoints",
    1: "numTets",
    2: "numTris",
    3: "selectedContact",
    4: "selectedVoltage",
    5: "amplitude",
    6: "pulseWidth",
    7: "dirNodes",
    8: "contactDirNodes",
    9: "boundaryDirNodes",
    10: "dirPct",
    11: "p50",
    12: "p95",
    13: "p99",
    14: "maxE",
    15: "maxELabel",
    16: "maxEVol",
    17: "p995",
    18: "p999",
    19: "robustMaxE",
    20: "tinyVolP1",
    21: "activeThresholdVolume",
    22: "activeThresholdCells",
    23: "electrodeSurfaceNodesNoBrain",
    24: "hollowedSurfaceNodes",
}


def b64_len(nbytes: int) -> int:
    return 4 * ((nbytes + 2) // 3)


def decode_vtk_binary(data_array: ET.Element) -> bytes:
    """Decode VTK XML binary data compressed with vtkZLibDataCompressor."""
    payload = re.sub(r"\s+", "", data_array.text or "")
    if not payload:
        return b""

    first = base64.b64decode(payload[:8])
    nblocks = struct.unpack("<I", first[:4])[0]
    header_nbytes = 4 * (3 + nblocks)
    header_chars = b64_len(header_nbytes)
    header = base64.b64decode(payload[:header_chars])[:header_nbytes]
    header_values = struct.unpack("<" + "I" * (3 + nblocks), header)
    compressed_sizes = header_values[3:]

    # VTK writes the header as one base64 block and then all compressed blocks
    # as one continuous base64 stream.
    raw_compressed = base64.b64decode(payload[header_chars:])
    pos = 0
    chunks = []
    for size in compressed_sizes:
        chunks.append(zlib.decompress(raw_compressed[pos : pos + size]))
        pos += size
    return b"".join(chunks)


def array_from_bytes(raw: bytes, type_code: str) -> array:
    out = array(type_code)
    out.frombytes(raw)
    if sys.byteorder != "little" and out.itemsize > 1:
        out.byteswap()
    return out


def read_array(root: ET.Element, name: str) -> tuple[ET.Element, bytes]:
    el = root.find(f'.//DataArray[@Name="{name}"]')
    if el is None:
        raise RuntimeError(f"missing DataArray Name={name!r}")
    return el, decode_vtk_binary(el)


def read_optional_array(root: ET.Element, name: str) -> tuple[ET.Element, bytes] | None:
    el = root.find(f'.//DataArray[@Name="{name}"]')
    if el is None:
        return None
    return el, decode_vtk_binary(el)


def decode_fem_diagnostics(root: ET.Element) -> dict[str, Any] | None:
    found = read_optional_array(root, "FEMDiagnostics")
    if found is None:
        return None

    _, raw = found
    values = [float(v) for v in array_from_bytes(raw, "d")]
    named: dict[str, Any] = {
        DIAGNOSTIC_NAMES.get(i, f"value{i}"): round(values[i], 9)
        for i in range(min(len(values), 25))
    }

    if len(values) >= 77:
        named["contactSurfaceNodes"] = [int(round(values[25 + i])) for i in range(4)]
        named["contactVolumeNodes"] = [int(round(values[29 + i])) for i in range(4)]
        named["contactAllSurfaceNodes"] = [int(round(values[33 + i])) for i in range(4)]
        named["contactAllSurfaceBrainShared"] = [int(round(values[37 + i])) for i in range(4)]
        named["contactAllSurfaceNoBrain"] = [int(round(values[41 + i])) for i in range(4)]
        named["contactSurfaceBrainShared"] = [int(round(values[45 + i])) for i in range(4)]
        named["contactSurfaceNoBrain"] = [int(round(values[49 + i])) for i in range(4)]
        named["contactSurfaceEffective"] = [int(round(values[53 + i])) for i in range(4)]
        named["contactBrainFacingRatio"] = [round(values[57 + i], 9) for i in range(4)]
        named["contactEffectiveRatio"] = [round(values[61 + i], 9) for i in range(4)]
        named["contactFluxMA"] = [round(values[65 + i], 12) for i in range(4)]
        named["contactFluxFaces"] = [int(round(values[69 + i])) for i in range(4)]
        named["contactFluxMissingFaces"] = [int(round(values[73 + i])) for i in range(4)]

    selected = int(round(values[3])) if len(values) > 3 else -1
    if 0 <= selected < 4 and len(values) >= 77:
        named["selectedContactDerived"] = {
            "contact": selected,
            "brainFacingRatio": round(values[57 + selected], 9),
            "effectiveRatio": round(values[61 + selected], 9),
            "fluxMA": round(values[65 + selected], 12),
            "fluxFaces": int(round(values[69 + selected])),
            "fluxMissingFaces": int(round(values[73 + selected])),
        }

    return {
        "tuple_count": len(values),
        "named": named,
    }


def point(points: array, idx: int) -> tuple[float, float, float]:
    j = idx * 3
    return float(points[j]), float(points[j + 1]), float(points[j + 2])


def sub(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return a[0] - b[0], a[1] - b[1], a[2] - b[2]


def cross(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    vals = sorted(values)
    idx = (len(vals) - 1) * p / 100.0
    lo = int(math.floor(idx))
    hi = int(math.ceil(idx))
    if lo == hi:
        return vals[lo]
    return vals[lo] * (hi - idx) + vals[hi] * (idx - lo)


def empty_stat() -> dict[str, Any]:
    return {
        "cells": 0,
        "volume_mm3": 0.0,
        "xmin": math.inf,
        "xmax": -math.inf,
        "ymin": math.inf,
        "ymax": -math.inf,
        "zmin": math.inf,
        "zmax": -math.inf,
        "E_sum": 0.0,
        "E_max": 0.0,
        "E_values": [],
    }


def audit_vtu(path: Path, threshold: float) -> dict[str, Any]:
    root = ET.fromstring(path.read_text(encoding="utf-8"))
    piece = root.find(".//Piece")
    if piece is None:
        raise RuntimeError("VTU Piece element not found")

    npoints = int(piece.attrib["NumberOfPoints"])
    ncells = int(piece.attrib["NumberOfCells"])

    _, raw_labels = read_array(root, "MaterialLabel")
    _, raw_e = read_array(root, "E_mag_Vmm")
    _, raw_points = read_array(root, "Points")
    _, raw_connectivity = read_array(root, "connectivity")

    labels = array_from_bytes(raw_labels, "i")
    e_mag = array_from_bytes(raw_e, "d")
    points = array_from_bytes(raw_points, "f")
    connectivity = array_from_bytes(raw_connectivity, "q")

    if len(labels) != ncells:
        raise RuntimeError(f"MaterialLabel length {len(labels)} != NumberOfCells {ncells}")
    if len(e_mag) != ncells:
        raise RuntimeError(f"E_mag_Vmm length {len(e_mag)} != NumberOfCells {ncells}")
    if len(connectivity) != ncells * 4:
        raise RuntimeError("This audit expects tetra-only VTU connectivity")
    if len(points) != npoints * 3:
        raise RuntimeError("Point coordinate length mismatch")

    stats: dict[int, dict[str, Any]] = {}
    total_volume = 0.0
    nuclei_cells = 0
    nuclei_volume = 0.0
    brainlike_cells = 0
    brainlike_volume = 0.0
    high_cells = 0
    high_volume = 0.0
    high_nuclei_cells = 0
    high_nuclei_volume = 0.0

    for ci in range(ncells):
        base = ci * 4
        p0 = point(points, connectivity[base])
        p1 = point(points, connectivity[base + 1])
        p2 = point(points, connectivity[base + 2])
        p3 = point(points, connectivity[base + 3])

        volume = abs(dot(sub(p1, p0), cross(sub(p2, p0), sub(p3, p0)))) / 6.0
        cx = (p0[0] + p1[0] + p2[0] + p3[0]) / 4.0
        cy = (p0[1] + p1[1] + p2[1] + p3[1]) / 4.0
        cz = (p0[2] + p1[2] + p2[2] + p3[2]) / 4.0
        label = int(labels[ci])
        e_val = float(e_mag[ci])

        stat = stats.setdefault(label, empty_stat())
        stat["cells"] += 1
        stat["volume_mm3"] += volume
        stat["xmin"] = min(stat["xmin"], cx)
        stat["xmax"] = max(stat["xmax"], cx)
        stat["ymin"] = min(stat["ymin"], cy)
        stat["ymax"] = max(stat["ymax"], cy)
        stat["zmin"] = min(stat["zmin"], cz)
        stat["zmax"] = max(stat["zmax"], cz)
        stat["E_sum"] += e_val
        stat["E_max"] = max(stat["E_max"], e_val)
        stat["E_values"].append(e_val)

        total_volume += volume
        if label in NUCLEI_LABELS:
            nuclei_cells += 1
            nuclei_volume += volume
        if label in BRAINLIKE_LABELS:
            brainlike_cells += 1
            brainlike_volume += volume
        if e_val >= threshold:
            high_cells += 1
            high_volume += volume
            if label in NUCLEI_LABELS:
                high_nuclei_cells += 1
                high_nuclei_volume += volume

    per_label: dict[str, Any] = {}
    for label in sorted(stats):
        stat = stats[label]
        e_values = stat["E_values"]
        per_label[str(label)] = {
            "name": LABEL_NAMES.get(label, ""),
            "cells": stat["cells"],
            "volume_mm3": round(stat["volume_mm3"], 6),
            "bbox_centroid_xyz": [
                round(stat["xmin"], 6),
                round(stat["xmax"], 6),
                round(stat["ymin"], 6),
                round(stat["ymax"], 6),
                round(stat["zmin"], 6),
                round(stat["zmax"], 6),
            ],
            "E_mean": round(stat["E_sum"] / stat["cells"], 9),
            "E_p95": round(percentile(e_values, 95.0), 9),
            "E_max": round(stat["E_max"], 9),
        }

    return {
        "vtu": str(path),
        "points": npoints,
        "cells": ncells,
        "cell_arrays": [el.attrib.get("Name") for el in root.findall(".//CellData/DataArray")],
        "point_arrays": [el.attrib.get("Name") for el in root.findall(".//PointData/DataArray")],
        "field_arrays": [el.attrib.get("Name") for el in root.findall(".//FieldData/DataArray")],
        "diagnostics": decode_fem_diagnostics(root),
        "label_counts": {label: stats[label]["cells"] for label in sorted(stats)},
        "total_tet_volume_mm3": round(total_volume, 6),
        "nuclei_cells": nuclei_cells,
        "nuclei_volume_mm3": round(nuclei_volume, 6),
        "brainlike_cells": brainlike_cells,
        "brainlike_volume_mm3": round(brainlike_volume, 6),
        "nuclei_fraction_of_brainlike_cells": round(nuclei_cells / brainlike_cells, 9) if brainlike_cells else 0.0,
        "nuclei_fraction_of_brainlike_volume": round(nuclei_volume / brainlike_volume, 9) if brainlike_volume else 0.0,
        f"high_E_Ege{threshold:g}": {
            "cells": high_cells,
            "volume_mm3": round(high_volume, 6),
            "nuclei_cells": high_nuclei_cells,
            "nuclei_volume_mm3": round(high_nuclei_volume, 6),
        },
        "per_label_stats": per_label,
    }


def write_markdown(report: dict[str, Any], out_path: Path, threshold: float) -> None:
    high_key = f"high_E_Ege{threshold:g}"
    lines = [
        "# Phase G VTU Material Audit",
        "",
        f"VTU: `{report['vtu']}`",
        "",
        f"Points: `{report['points']}`",
        f"Cells: `{report['cells']}`",
        f"Total tet volume: `{report['total_tet_volume_mm3']:.3f} mm3`",
        "",
        "## Aggregate",
        "",
        f"Nuclei cells: `{report['nuclei_cells']}`",
        f"Nuclei volume: `{report['nuclei_volume_mm3']:.3f} mm3`",
        f"Brain-like cells: `{report['brainlike_cells']}`",
        f"Brain-like volume: `{report['brainlike_volume_mm3']:.3f} mm3`",
        f"Nuclei fraction by cells: `{report['nuclei_fraction_of_brainlike_cells']:.6f}`",
        f"Nuclei fraction by volume: `{report['nuclei_fraction_of_brainlike_volume']:.6f}`",
        "",
        f"High E threshold: `{threshold:g} V/mm`",
        f"High E cells: `{report[high_key]['cells']}`",
        f"High E volume: `{report[high_key]['volume_mm3']:.3f} mm3`",
        f"High E nuclei cells: `{report[high_key]['nuclei_cells']}`",
        f"High E nuclei volume: `{report[high_key]['nuclei_volume_mm3']:.3f} mm3`",
        "",
        "## FEM Diagnostics",
        "",
    ]
    diag = report.get("diagnostics")
    if diag:
        named = diag["named"]
        selected = named.get("selectedContactDerived", {})
        lines.extend([
            f"Selected contact: `{int(named.get('selectedContact', -1))}`",
            f"Selected voltage: `{named.get('selectedVoltage', 0.0):.6f} V`",
            f"Active threshold volume: `{named.get('activeThresholdVolume', 0.0):.3f} mm3`",
            f"Active threshold cells: `{int(named.get('activeThresholdCells', 0))}`",
            f"Robust max E: `{named.get('robustMaxE', 0.0):.6f} V/mm`",
            f"Selected brain-facing ratio: `{selected.get('brainFacingRatio', 0.0):.6f}`",
            f"Selected effective ratio: `{selected.get('effectiveRatio', 0.0):.6f}`",
            f"Selected flux: `{selected.get('fluxMA', 0.0):.9f} mA`",
            "",
        ])
    else:
        lines.extend(["FEMDiagnostics: `(missing)`", ""])
    lines.extend([
        "## Per Label",
        "",
        "| Label | Name | Cells | Volume mm3 | BBox centroid xyz | E mean | E p95 | E max |",
        "|---:|---|---:|---:|---|---:|---:|---:|",
    ])
    for label, stat in report["per_label_stats"].items():
        bbox = ", ".join(f"{v:.3f}" for v in stat["bbox_centroid_xyz"])
        lines.append(
            f"| {label} | {stat['name']} | {stat['cells']} | "
            f"{stat['volume_mm3']:.3f} | {bbox} | "
            f"{stat['E_mean']:.6f} | {stat['E_p95']:.6f} | {stat['E_max']:.6f} |"
        )
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit Phase G VTU MaterialLabel distribution.")
    parser.add_argument("--vtu", required=True, type=Path, help="Path to result .vtu")
    parser.add_argument("--threshold", type=float, default=0.2, help="High-E threshold in V/mm")
    parser.add_argument("--json", type=Path, help="Optional JSON report output")
    parser.add_argument("--md", type=Path, help="Optional Markdown report output")
    args = parser.parse_args()

    report = audit_vtu(args.vtu, args.threshold)
    text = json.dumps(report, ensure_ascii=False, indent=2)
    print(text)
    if args.json:
        args.json.write_text(text + "\n", encoding="utf-8")
    if args.md:
        write_markdown(report, args.md, args.threshold)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
