# Phase G Surface/BC Log Audit

Passed: `True`

## Checks

| Check | OK | Detail |
|---|---:|---|
| off disabled | True | `nuclei-off log should say disabled` |
| on enabled | True | `nuclei-on log should say enabled` |
| on relabeled > 0 | True | `{'candidates': 262627, 'relabeled': 28696, 'ratio': 0.109265, 'outOfExtent': 0, 'nonNucleiSamples': 233931}` |
| on outOfExtent == 0 | True | `{'candidates': 262627, 'relabeled': 28696, 'ratio': 0.109265, 'outOfExtent': 0, 'nonNucleiSamples': 233931}` |
| triangle labels unchanged | True | `off={-1: 23918, 50: 1294, 101: 272, 102: 270, 103: 272, 104: 270}, on={-1: 23918, 50: 1294, 101: 272, 102: 270, 103: 272, 104: 270}` |
| triangle labels allowed | True | `on tri labels=[-1, 50, 101, 102, 103, 104]` |
| off has no nuclei tet labels | True | `off tet labels=[10, 50, 101, 102, 103, 104]` |
| on has nuclei tet labels | True | `on tet labels={1: 563, 2: 25999, 3: 416, 4: 526, 5: 963, 6: 229, 10: 233931, 50: 2622, 101: 493, 102: 503, 103: 505, 104: 482}` |
| non-nuclei tets preserved | True | `off={10: 262627, 50: 2622, 101: 493, 102: 503, 103: 505, 104: 482}, on={1: 563, 2: 25999, 3: 416, 4: 526, 5: 963, 6: 229, 10: 233931, 50: 2622, 101: 493, 102: 503, 103: 505, 104: 482}` |
| brain tet accounting | True | `off brain=262627, on brain=233931, nuclei=28696` |
| selected effectiveRatio stable | True | `off=1.0, on=1.0` |
| selected brainFacingRatio stable | True | `off=0.855072, on=0.855072` |
| inactive contact flux near zero | True | `C0=2.73917e-14, C1=2.07627e-14, C3=-7.44717e-15` |
| no unknown/fallback warnings | True | `off warnings=0, on warnings=0` |

## Key Counts

Off FEM tri labels: `{-1: 23918, 50: 1294, 101: 272, 102: 270, 103: 272, 104: 270}`
On FEM tri labels: `{-1: 23918, 50: 1294, 101: 272, 102: 270, 103: 272, 104: 270}`
Off FEM tet labels: `{10: 262627, 50: 2622, 101: 493, 102: 503, 103: 505, 104: 482}`
On FEM tet labels: `{1: 563, 2: 25999, 3: 416, 4: 526, 5: 963, 6: 229, 10: 233931, 50: 2622, 101: 493, 102: 503, 103: 505, 104: 482}`
