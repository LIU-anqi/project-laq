# Phase G Nuclei Backfill Validation Plan - Codex v1

## 0. 最简执行版（以后优先按这个走）

当前样例数据固定为：

```text
main image:
E:\LQ\Code_Files\VS_Code\read\out\build\release\mni_t2_job1901422.nii.gz

label image:
E:\LQ\Code_Files\VS_Code\read\out\build\release\mni_structures_job1901422.nii.gz

README:
E:\LQ\Code_Files\VS_Code\read\out\build\release\README.pdf
```

当前阶段不需要先上 WSL / OSS-DBS / COMSOL，也不需要把验证流程弄成很大的外部实验。

最小验证闭环：

1. 在程序里跑同一个 F case：
   - 不勾选“启用核团 σ 分区”：作为 Phase F baseline。
   - 勾选“启用核团 σ 分区”：作为 Phase G。

2. 看日志：
   - baseline 应出现 `nuclei centroid backfill disabled`。
   - Phase G 应出现 `nuclei centroid backfill enabled`。
   - Phase G 的 tet label count 应包含 `label 1..6`。
   - triangle label 仍应只承担 ROI / 电极 / 包膜 / contact 边界职责，不应因为核团分区变成新的 BC 来源。

3. 需要导出 VTU 时才勾选 `Save FEM output files (.vtu)`：
   - 用 `.kiro\tools\phase_g_vtu_material_audit.py` 读 VTU。
   - 只看三个核心指标：
     - `MaterialLabel` 是否含 `1..6`。
     - `label 50/51/101..104` 是否仍正常存在。
     - per-label volume 是否比单纯 tet count 更合理。

4. 当前 F1-G 已完成一次 VTU 审计：
   - VTU: `E:\LQ\Code_Files\VS_Code\read\out\phase_f_project\F1_20260603_220838_result.vtu`
   - JSON: `.kiro\docs\phase-g-f1-20260603-220838-vtu-audit.json`
   - Markdown: `.kiro\docs\phase-g-f1-20260603-220838-vtu-audit.md`

5. 现在的暂定结论：
   - Phase G 确实开启了。
   - VTU 中确实存在核团 tet label `1..6`。
   - 电极、包膜、contact 的 label 没看到被核团 backfill 污染。
   - `label 2` 的 tet 数很多，但 volume 与右侧对应结构 `label 5` 接近，主要应解释为电极附近局部网格细化，不是解剖体积异常。

生成时间：2026-06-03
当前目标：验证 Phase G “mesh 后按 tet 中心采样原始 NIfTI label，把 brain tet 回填为 nuclei label 1..6，并使用 nuclei conductivity” 是否真实、稳定、物理上可解释。

## 1. 当前 F1-G 日志与 VTU 结论

本轮 F1-G 输入：

```text
case = F1
target = (173.47, 237.42, 128.00)
entry  = (128.08, 193.89, 201.00)
useNucleiBackfill = true
sigmaBrain = 0.115 S/m
sigmaNuclei = 0.333 S/m
sigmaEncapsulation = 0.115 S/m
VTU = E:\LQ\Code_Files\VS_Code\read\out\phase_f_project\F1_20260603_220838_result.vtu
```

日志与 VTU 文件内 `MaterialLabel` 完全一致：

```text
label 1  = 551
label 2  = 25704
label 3  = 439
label 4  = 495
label 5  = 999
label 6  = 234
label 10 = 233084
label 50 = 2727
label 51 = 8130
label 101 = 518
label 102 = 487
label 103 = 503
label 104 = 516
total = 274387
```

### 1.1 从数量上是否正常

结论：从实现和网格角度看，这组数量是正常的，没有看到爆炸式误标或 surface label 被污染。

理由：

1. 回填守恒正确：

```text
backfill 前 brain tet = 261506
回填到 label 1..6 = 28422
回填后 label 10 = 233084
28422 + 233084 = 261506
```

2. 非 brain 子域没有被改变：

```text
label 50/51/101..104 在 backfill 前后保持一致
```

3. 没有 out-of-extent：

```text
outOfExtent = 0
```

说明 centroid -> label image 的空间映射没有明显跑出图像范围。

4. tet 数量不能直接等同解剖体积。电极附近网格更细，所以靠近电极或高场区域的核团会被切成更多 tet。当前 label 2 数量很大，但按 VTU 解压后的体积统计看：

```text
label 1 volume ~= 375.063 mm3
label 2 volume ~= 764.547 mm3
label 3 volume ~= 202.674 mm3
label 4 volume ~= 367.997 mm3
label 5 volume ~= 754.643 mm3
label 6 volume ~= 168.100 mm3
```

这比单看 tet 数更合理：1/4、2/5、3/6 的左右对应体积大体成对，label 2 与 label 5 体积接近。label 2 的 tet 数远多于 label 5，主要是局部 mesh refinement 导致，不应直接解读为 label 2 解剖体积巨大。

5. 当前高场区域确实大量进入 nuclei：

```text
E >= 0.2 V/mm cells = 14466
其中 nuclei cells = 7601
E >= 0.2 V/mm volume = 140.826 mm3
其中 nuclei volume = 70.076 mm3
```

这说明 nuclei conductivity 对当前 case 很可能会产生明显物理影响。

### 1.2 当前仍不能直接下的结论

不能仅凭这组计数就断言“这是标准 STN case 且物理完全正确”。

原因：

```text
label 1 = Left Red Nucleus
label 2 = Left Substantia Nigra
label 3 = Left STN
label 4 = Right Red Nucleus
label 5 = Right Substantia Nigra
label 6 = Right STN
```

本次 label 2 在高场附近占主导，而 STN 是 label 3/6。若目标是“标准 STN case”，需要单独审计 target、contact center、active contact 周围点到底落在哪个 atlas label 上。

## 2. 验证计划

### Step 1 - 开关与日志激活审计

做什么实验：

运行 F1/F2/F3/F4 中至少 F1 与 F2，各跑一遍 nuclei off 和 nuclei on。nuclei on 时勾选“启用核团 σ 分区”，并勾选 “Save FEM output files (.vtu)” 保存结果文件。

这一步在验证什么：

确认 UI 开关确实进入 `DBSSimSpec.useNucleiBackfill`，并确认所有 preset 都会触发 backfill，而不是 F1 专属。

你需要手动做什么：

1. 选择 preset。
2. nuclei off 跑一次。
3. nuclei on 跑一次。
4. on 的结果勾选保存 VTU。
5. 把日志或 VTU 路径交给我审计。

验证指标与预期目标：

```text
useNucleiBackfill=false 时：
  [Phase G] nuclei centroid backfill disabled
  FEM tet label 只应有 10/50/51/101..104

useNucleiBackfill=true 时：
  [Phase G] nuclei centroid backfill enabled
  relabeled > 0
  outOfExtent = 0
  FEM tet label 应出现 1..6
```

通过标准：

```text
F1/F2/F3/F4 只要勾选 nuclei on，都应出现同类 Phase G 日志。
```

### Step 2 - VTU MaterialLabel 文件级审计

做什么实验：

直接读取保存的 `.vtu`，解压 `MaterialLabel`、`E_mag_Vmm`、points、tet connectivity，独立统计 label 数、体积、bbox、高场区域。

这一步在验证什么：

确认 nuclei label 不是只存在日志里，而是真实写入结果文件，并且结果文件可以被后处理脚本复核。

你需要手动做什么：

只需要确认保存了 VTU，并把路径发给我。当前例子：

```text
E:\LQ\Code_Files\VS_Code\read\out\phase_f_project\F1_20260603_220838_result.vtu
```

验证指标与预期目标：

```text
sum(label counts) == NumberOfCells
VTU MaterialLabel counts == 日志 FEM tetra label counts
label 1..6 cells > 0
label 50/51/101..104 未消失
total tet volume 正常
nuclei volume 在 ROI 内为合理正值
```

通过标准：

```text
文件级统计和日志统计完全一致。
```

### Step 3 - Centroid 回采样正确性审计

做什么实验：

对每个 tet 计算中心点，用同一套 world -> image index 映射重新采样原始 label image。对已回填成 1..6 的 tet，检查采样值是否等于该 tet 的 MaterialLabel。

这一步在验证什么：

验证 backfill 的核心逻辑“按 tet 中心采样原始 NIfTI label”没有坐标系错误、origin/spacing/extent 错误或 off-by-one 错误。

你需要手动做什么：

理想情况下不需要手动操作。我会写一个审计工具或临时调试函数，输入当前 mesh/VTU 与 label image 后自动输出报告。如果需要你配合，只需要重新跑一次指定 case 并保存 VTU。

验证指标与预期目标：

```text
relabeled tet centroid resample match rate = 100%
outOfExtent = 0
label 10 中抽样检查不应大量采到 1..6
```

通过标准：

```text
没有坐标映射错误；误回填率应为 0。
```

### Step 4 - Surface label 与 BC 回归审计

做什么实验：

对 nuclei off/on 的日志比较 surface triangle label、contact surface nodes、effectiveRatio、floating residual、Dirichlet 节点比例。

这一步在验证什么：

确认 nuclei backfill 没有污染原本正常的 ROI 边界、电极表面、包膜表面、绝缘体表面、contact 表面 label。

你需要手动做什么：

跑 nuclei off/on 两组日志即可。若要最强审计，保存两组 VTU。

验证指标与预期目标：

```text
triangle label 只应有：
  -1, 50, 51, 101, 102, 103, 104

不应出现：
  triangle label 1..6
  unknown surface pair warning
  fallback pair warning

active contact:
  effectiveRatio = 1

inactive contacts:
  floating residual 约 e-14 mA 或接近数值零

contact brain-facing ratio:
  维持在 Phase F 同类水平，约 0.85-0.87
```

通过标准：

```text
surface/BC 仍表现为 Phase F 已验收状态。
```

### Step 5 - 同网格 nuclei-on/off 物理 A/B

做什么实验：

使用同一个 mesh 做两次求解：

1. nuclei-on：保留 label 1..6，sigmaNuclei=0.333。
2. homogeneous copy：把同一个 `.mesh` 里的 tet label 1..6 改回 10，其他 label 完全不变。

这一步在验证什么：

排除 CGAL 重新 meshing 的随机/局部差异，只看 conductivity 从 homogeneous brain 到 nuclei σ 分区后对解的影响。

你需要手动做什么：

这一步最好由我写工具跑，不建议你手动改 `.mesh`。你需要做的是保存 nuclei-on 运行产物，或者按我要求跑一次指定 case。

验证指标与预期目标：

```text
两组 mesh 几何完全一致：
  nodes/tets/tris 一致
  triangle labels 一致
  contact surface nodes 一致

两组只允许 tet material 1..6 vs 10 不同。

比较：
  active flux
  VTA direct cell volume
  E p50/p95/p99/p99.9/robustMax/max
  E>=0.2 mm3
  nuclei 内外 E 分布
```

预期目标：

```text
若高场穿过高 σ nuclei，总电流/flux 与 E 分布应有可解释变化。
不要求与 nuclei-off 完全一致；要求变化方向可解释且 solver 稳定。
```

通过标准：

```text
没有 BC 变化，没有几何变化，只有材料变化导致电场变化。
```

### Step 6 - 靶点与触点的解剖 label 审计

做什么实验：

采样以下 world 坐标对应的 label image 值：

```text
target
entry
contact0 center
contact1 center
contact2 center
contact3 center
active contact 周围若干采样点
```

这一步在验证什么：

确认当前所谓“Standard STN”是否真的贴近 STN label，而不是贴近 SN 或其它核团。

你需要手动做什么：

如果你有临床/规划上的目标，请确认你希望 target 落在 label 3/6 还是其它结构。若当前坐标本来就是新测试点，不一定要叫 STN。

验证指标与预期目标：

```text
若目标是 STN：
  target 或 active contact 近邻应采到 label 3 或 label 6，或至少与其 bbox/表面很近。

若采到 label 2：
  说明当前 case 更像 Left Substantia Nigra 邻近 case，不应再称作标准 STN。
```

通过标准：

```text
case 名称、坐标和 atlas label 语义一致。
```

### Step 7 - 外部仿真对比的取舍

做什么实验：

优先不直接上 COMSOL/OSS-DBS。先完成同网格 A/B 与 centroid 审计。外部对比用于趋势确认，而不是第一优先级。

OSS-DBS 可做什么：

```text
构造同样 target/entry/contact、同样 conductivity map 的 dummy MRI / material case。
跑 OSS-DBS 后，把 OSS 和本项目 E-field 都插值到同一个规则采样网格上比较。
比较 global VTA、radial profile、采样网格上的 E RMSE/corr。
```

OSS-DBS 局限：

```text
网格不同，不能逐节点一一比较。
核团材料/包膜/电极几何字段要非常仔细对齐，否则差异来源不干净。
```

COMSOL 可做什么：

```text
更适合构造简化 benchmark：
  均匀脑组织 + 一个高 σ 球/椭球 nuclei + 电极/包膜
比较解析趋势、flux、E 分布和 VTA volume。
```

COMSOL 局限：

```text
真实 atlas nuclei 几何导入、mesh 一致性、contact BC 细节都很重。
不建议作为 Phase G 第一验收门槛。
```

通过标准：

```text
内部同网格 A/B 先通过；外部仿真作为增强证据，而不是阻塞项。
```

## 3. 计划使用或保留的脚本/环境

### 3.1 当前可用环境

```text
项目路径:
E:\LQ\Code_Files\VS_Code\read

Release build dir:
E:\LQ\Code_Files\VS_Code\read\out\build\release

VS developer build command:
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "E:\LQ\Code_Files\VS_Code\read\out\build\release" --config Release'

当前系统 Python:
可用标准库
不可用 vtk
不可用 numpy
```

### 3.2 已验证可行的脚本思路

1. `phase_g_vtu_material_audit.py`

作用：

```text
不用 vtk/numpy，仅用 Python 标准库解析 VTK XML 压缩格式：
  base64
  zlib
  struct
  array
  xml.etree.ElementTree

读取：
  MaterialLabel
  E_mag_Vmm
  Points
  connectivity

输出：
  label counts
  per-label volume
  per-label bbox
  per-label E mean/p95/max
  high-E nuclei overlap
```

价值：

```text
直接从 .vtu 文件审计，不依赖应用日志。
```

2. `phase_g_log_summary.py`

作用：

```text
从粘贴日志中提取：
  useNucleiBackfill
  relabeled count
  tet label counts
  triangle label counts
  selected contact BC
  E-field summary
  VTA summary
```

价值：

```text
快速比较 nuclei off/on，减少人工读日志误差。
```

3. `phase_g_centroid_resample_audit`

形态：

```text
优先做成 C++/Qt/VTK 小工具或临时 app debug function。
```

作用：

```text
对 tet centroid 重新采样原始 label image，检查 MaterialLabel 1..6 是否与原始 NIfTI label 一致。
```

价值：

```text
这是确认“按真实核团分区”最关键的审计。
```

4. `phase_g_make_homogeneous_mesh_copy.py`

作用：

```text
读取 Medit .mesh，将 tetra label 1..6 改为 10，其他 label 不变，生成同网格 homogeneous 对照 mesh。
```

价值：

```text
用于同网格 nuclei-on/off 物理 A/B。
```

5. `phase_g_same_mesh_solver_compare`

形态：

```text
可能需要 C++ 调试入口或小 harness，因为当前 DBSSimWorker 在 Qt app 内。
```

作用：

```text
对同一几何 mesh 的 nuclei-on 与 homogeneous-copy 分别求解，输出可比较的 VTU/JSON。
```

价值：

```text
把 mesh 差异完全排除，只验证材料分区的物理影响。
```

## 4. 当前建议的下一步

下一步优先做两个工具/审计：

```text
1. phase_g_vtu_material_audit.py
   先固化文件级 VTU 审计，作为以后每次跑 G 的标准报告。

2. centroid resample audit
   直接证明每个 label 1..6 tet 的中心点确实采到了原始 NIfTI 的同一 label。
```

完成这两项后，再做同网格 nuclei-on/off A/B。
外部 OSS/COMSOL 对比放在后面，除非内部审计出现解释不了的问题。
