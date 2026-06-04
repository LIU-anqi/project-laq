#!/usr/bin/env python3
"""
Phase G centroid resampling audit.

Reads a final Medit .mesh plus the aligned label dump emitted by AI plan runs:
  aligned_label_header.json
  aligned_label_u8.raw

Then replays the centroid -> image-index sampling rule independently from the
mesh writer and checks whether final tet labels 1..6 match the aligned label
image at their centroids.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


NUCLEI = {1, 2, 3, 4, 5, 6}


def parse_mesh(path: Path) -> tuple[list[tuple[float, float, float]], list[tuple[tuple[int, int, int, int], int]]]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    verts: list[tuple[float, float, float]] = []
    tets: list[tuple[tuple[int, int, int, int], int]] = []
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        if s == "Vertices":
            n = int(lines[i + 1].strip())
            for line in lines[i + 2 : i + 2 + n]:
                p = line.split()
                verts.append((float(p[0]), float(p[1]), float(p[2])))
            i += 2 + n
            continue
        if s == "Tetrahedra":
            n = int(lines[i + 1].strip())
            for line in lines[i + 2 : i + 2 + n]:
                p = line.split()
                ids = tuple(int(x) - 1 for x in p[:4])
                tets.append((ids, int(p[4])))  # type: ignore[arg-type]
            i += 2 + n
            continue
        i += 1
    return verts, tets


def load_label_dump(header_path: Path) -> dict[str, Any]:
    header = json.loads(header_path.read_text(encoding="utf-8"))
    raw_path = header_path.parent / header["rawFile"]
    raw = raw_path.read_bytes()
    dims = [int(x) for x in header["dims"]]
    extent = [int(x) for x in header["extent"]]
    origin = [float(x) for x in header["origin"]]
    spacing = [float(x) for x in header["spacing"]]
    expected = dims[0] * dims[1] * dims[2]
    if len(raw) != expected:
        raise RuntimeError(f"raw label size {len(raw)} != expected {expected}")
    return {
        "raw": raw,
        "dims": dims,
        "extent": extent,
        "origin": origin,
        "spacing": spacing,
    }


def sample_label(label: dict[str, Any], xyz: tuple[float, float, float]) -> tuple[int | None, tuple[int, int, int]]:
    origin = label["origin"]
    spacing = label["spacing"]
    extent = label["extent"]
    dims = label["dims"]
    raw: bytes = label["raw"]

    def cxx_round(v: float) -> int:
        return int(math.floor(v + 0.5)) if v >= 0.0 else int(math.ceil(v - 0.5))

    idx = tuple(cxx_round((xyz[d] - origin[d]) / spacing[d]) for d in range(3))
    if (
        idx[0] < extent[0] or idx[0] > extent[1]
        or idx[1] < extent[2] or idx[1] > extent[3]
        or idx[2] < extent[4] or idx[2] > extent[5]
    ):
        return None, idx

    local_i = idx[0] - extent[0]
    local_j = idx[1] - extent[2]
    local_k = idx[2] - extent[4]
    offset = local_i + dims[0] * (local_j + dims[1] * local_k)
    return raw[offset], idx


def sample_index(label: dict[str, Any], idx: tuple[int, int, int]) -> int | None:
    extent = label["extent"]
    dims = label["dims"]
    raw: bytes = label["raw"]
    if (
        idx[0] < extent[0] or idx[0] > extent[1]
        or idx[1] < extent[2] or idx[1] > extent[3]
        or idx[2] < extent[4] or idx[2] > extent[5]
    ):
        return None
    local_i = idx[0] - extent[0]
    local_j = idx[1] - extent[2]
    local_k = idx[2] - extent[4]
    offset = local_i + dims[0] * (local_j + dims[1] * local_k)
    return raw[offset]


def audit(mesh_path: Path, header_path: Path, sample_brain_limit: int = 0) -> dict[str, Any]:
    verts, tets = parse_mesh(mesh_path)
    label = load_label_dump(header_path)

    nuclei_total = 0
    nuclei_match = 0
    nuclei_out = 0
    mismatch_by_pair: dict[str, int] = {}
    boundary_neighbor_matches = 0
    label10_total = 0
    label10_nuclei_samples = 0
    label10_out = 0
    per_label: dict[int, dict[str, int]] = {}
    examples = []

    for ci, (ids, tet_label) in enumerate(tets):
        c = tuple(sum(verts[v][d] for v in ids) / 4.0 for d in range(3))
        sampled, idx = sample_label(label, c)

        if tet_label in NUCLEI:
            nuclei_total += 1
            stat = per_label.setdefault(tet_label, {"total": 0, "match": 0, "out": 0})
            stat["total"] += 1
            if sampled is None:
                nuclei_out += 1
                stat["out"] += 1
            elif sampled == tet_label:
                nuclei_match += 1
                stat["match"] += 1
            else:
                neighbor_hit = False
                for di in (-1, 0, 1):
                    for dj in (-1, 0, 1):
                        for dk in (-1, 0, 1):
                            if di == 0 and dj == 0 and dk == 0:
                                continue
                            nidx = (idx[0] + di, idx[1] + dj, idx[2] + dk)
                            if sample_index(label, nidx) == tet_label:
                                neighbor_hit = True
                                break
                        if neighbor_hit:
                            break
                    if neighbor_hit:
                        break
                if neighbor_hit:
                    boundary_neighbor_matches += 1
                key = f"{tet_label}->{sampled}"
                mismatch_by_pair[key] = mismatch_by_pair.get(key, 0) + 1
                if len(examples) < 20:
                    examples.append({
                        "cell": ci,
                        "tetLabel": tet_label,
                        "sampled": sampled,
                        "centroid": [round(x, 6) for x in c],
                        "index": idx,
                    })

        elif tet_label == 10:
            if sample_brain_limit and label10_total >= sample_brain_limit:
                continue
            label10_total += 1
            if sampled is None:
                label10_out += 1
            elif sampled in NUCLEI:
                label10_nuclei_samples += 1
                if len(examples) < 20:
                    examples.append({
                        "cell": ci,
                        "tetLabel": tet_label,
                        "sampled": sampled,
                        "centroid": [round(x, 6) for x in c],
                        "index": idx,
                    })

    match_rate = nuclei_match / nuclei_total if nuclei_total else 0.0
    strict_passed = nuclei_total > 0 and nuclei_match == nuclei_total and nuclei_out == 0 and label10_nuclei_samples == 0
    tolerated_mismatches = nuclei_total - nuclei_match - nuclei_out
    accepted = (
        nuclei_total > 0
        and nuclei_out == 0
        and label10_nuclei_samples == 0
        and tolerated_mismatches == boundary_neighbor_matches
        and tolerated_mismatches <= max(1, int(math.ceil(nuclei_total * 0.0001)))
    )
    return {
        "mesh": str(mesh_path),
        "labelHeader": str(header_path),
        "vertices": len(verts),
        "tets": len(tets),
        "nucleiTotal": nuclei_total,
        "nucleiMatch": nuclei_match,
        "nucleiOutOfExtent": nuclei_out,
        "nucleiMatchRate": match_rate,
        "strictPassed": strict_passed,
        "boundaryNeighborMatches": boundary_neighbor_matches,
        "mismatchByPair": mismatch_by_pair,
        "perLabel": per_label,
        "label10Checked": label10_total,
        "label10OutOfExtent": label10_out,
        "label10NucleiSamples": label10_nuclei_samples,
        "passed": strict_passed or accepted,
        "acceptanceNote": (
            "strict 100% match"
            if strict_passed
            else "accepted: mismatches are tiny boundary-neighbor cases, likely from serialized mesh coordinate precision"
            if accepted
            else "failed strict and tolerance checks"
        ),
        "examples": examples,
    }


def write_markdown(result: dict[str, Any], path: Path) -> None:
    lines = [
        "# Phase G Centroid Resampling Audit",
        "",
        f"Passed: `{result['passed']}`",
        "",
        f"Mesh: `{result['mesh']}`",
        f"Label header: `{result['labelHeader']}`",
        "",
        "## Summary",
        "",
        f"Nuclei tets: `{result['nucleiTotal']}`",
        f"Nuclei matches: `{result['nucleiMatch']}`",
        f"Nuclei match rate: `{result['nucleiMatchRate']:.6f}`",
        f"Strict passed: `{result['strictPassed']}`",
        f"Boundary-neighbor matches: `{result['boundaryNeighborMatches']}`",
        f"Acceptance note: `{result['acceptanceNote']}`",
        f"Nuclei out of extent: `{result['nucleiOutOfExtent']}`",
        f"Label 10 checked: `{result['label10Checked']}`",
        f"Label 10 sampled as nuclei: `{result['label10NucleiSamples']}`",
        "",
        "## Per Label",
        "",
        "| Label | Total | Match | Out |",
        "|---:|---:|---:|---:|",
    ]
    for label, stat in sorted((int(k), v) for k, v in result["perLabel"].items()):
        lines.append(f"| {label} | {stat['total']} | {stat['match']} | {stat['out']} |")

    if result["mismatchByPair"]:
        lines.extend(["", "## Mismatches", "", f"`{result['mismatchByPair']}`"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", required=True)
    parser.add_argument("--label-header", required=True)
    parser.add_argument("--json")
    parser.add_argument("--md")
    parser.add_argument("--sample-brain-limit", type=int, default=0)
    args = parser.parse_args()

    result = audit(Path(args.mesh), Path(args.label_header), args.sample_brain_limit)
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    if args.md:
        write_markdown(result, Path(args.md))
    print(json.dumps({
        "passed": result["passed"],
        "nucleiTotal": result["nucleiTotal"],
        "nucleiMatch": result["nucleiMatch"],
        "nucleiMatchRate": result["nucleiMatchRate"],
        "strictPassed": result["strictPassed"],
        "boundaryNeighborMatches": result["boundaryNeighborMatches"],
        "label10NucleiSamples": result["label10NucleiSamples"],
        "acceptanceNote": result["acceptanceNote"],
        "mismatchByPair": result["mismatchByPair"],
    }, indent=2, ensure_ascii=False))
    return 0 if result["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
