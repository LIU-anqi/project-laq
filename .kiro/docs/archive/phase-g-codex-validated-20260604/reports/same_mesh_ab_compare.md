# Phase G Same-Mesh A/B Compare

Passed: `True`

## Checks

| Check | Passed |
|---|---:|
| same_point_count | `True` |
| same_cell_count | `True` |
| same_point_coordinates_bytes | `True` |
| same_connectivity_bytes | `True` |
| nuclei_on_has_nuclei | `True` |
| homogeneous_has_no_nuclei | `True` |
| only_nuclei_to_brain_labels_changed | `True` |
| brain_count_accounting | `True` |
| preserved_non_nuclei_labels | `True` |
| diagnostics_stable_for_bc_geometry | `True` |
| selected_effective_ratio_is_one | `True` |
| selected_flux_faces_not_missing | `True` |
| field_difference_present | `True` |

## Material Change

A nuclei cells: `28422`
A nuclei volume: `2633.024 mm3`
Changed transitions: `{"1->10": 551, "2->10": 25704, "3->10": 439, "4->10": 495, "5->10": 999, "6->10": 234}`
Unexpected transitions: `{}`

## Physical Difference

High-E volume A: `140.826 mm3`
High-E volume B: `162.970 mm3`
High-E delta A-B: `-22.144 mm3`
High-E relative delta A-B: `-0.135881`
Selected flux A: `-6.050209952 mA`
Selected flux B: `-3.526202506 mA`
Selected flux delta A-B: `-2.524007446 mA`

## Field Difference

E mean abs diff: `0.017762541 V/mm`
E p95 abs diff: `0.042839661 V/mm`
E max abs diff: `4.087155508 V/mm`
Potential mean abs diff: `0.037190676 V`
Potential max abs diff: `0.422313466 V`

## BC Diagnostics

Selected effective ratio A/B: `1.0` / `1.0`
Selected brain-facing ratio A/B: `0.86013986` / `0.86013986`
Flux missing faces A/B: `0` / `0`
