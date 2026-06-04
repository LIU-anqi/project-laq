# Phase G Surface/BC Log Audit

Passed: `True`

## Checks

| Check | OK | Detail |
|---|---:|---|
| off disabled | True | `nuclei-off log should say disabled` |
| on enabled | True | `nuclei-on log should say enabled` |
| on relabeled > 0 | True | `{'candidates': 261506, 'relabeled': 28422, 'ratio': 0.108686, 'outOfExtent': 0, 'nonNucleiSamples': 233084}` |
| on outOfExtent == 0 | True | `{'candidates': 261506, 'relabeled': 28422, 'ratio': 0.108686, 'outOfExtent': 0, 'nonNucleiSamples': 233084}` |
| triangle labels unchanged | True | `off={-1: 23782, 50: 1356, 51: 3556, 101: 290, 102: 278, 103: 282, 104: 294}, on={-1: 23782, 50: 1356, 51: 3556, 101: 290, 102: 278, 103: 282, 104: 294}` |
| triangle labels allowed | True | `on tri labels=[-1, 50, 51, 101, 102, 103, 104]` |
| off has no nuclei tet labels | True | `off tet labels=[10, 50, 51, 101, 102, 103, 104]` |
| on has nuclei tet labels | True | `on tet labels={1: 551, 2: 25704, 3: 439, 4: 495, 5: 999, 6: 234, 10: 233084, 50: 2727, 51: 8130, 101: 518, 102: 487, 103: 503, 104: 516}` |
| non-nuclei tets preserved | True | `off={10: 261506, 50: 2727, 51: 8130, 101: 518, 102: 487, 103: 503, 104: 516}, on={1: 551, 2: 25704, 3: 439, 4: 495, 5: 999, 6: 234, 10: 233084, 50: 2727, 51: 8130, 101: 518, 102: 487, 103: 503, 104: 516}` |
| brain tet accounting | True | `off brain=261506, on brain=233084, nuclei=28422` |
| selected effectiveRatio stable | True | `off=1.0, on=1.0` |
| selected brainFacingRatio stable | True | `off=0.86014, on=0.86014` |
| inactive contact flux near zero | True | `C0=2.02833e-14, C1=-1.41922e-14, C3=-5.51425e-15` |
| no unknown/fallback warnings | True | `off warnings=0, on warnings=0` |

## Key Counts

Off FEM tri labels: `{-1: 23782, 50: 1356, 51: 3556, 101: 290, 102: 278, 103: 282, 104: 294}`
On FEM tri labels: `{-1: 23782, 50: 1356, 51: 3556, 101: 290, 102: 278, 103: 282, 104: 294}`
Off FEM tet labels: `{10: 261506, 50: 2727, 51: 8130, 101: 518, 102: 487, 103: 503, 104: 516}`
On FEM tet labels: `{1: 551, 2: 25704, 3: 439, 4: 495, 5: 999, 6: 234, 10: 233084, 50: 2727, 51: 8130, 101: 518, 102: 487, 103: 503, 104: 516}`
