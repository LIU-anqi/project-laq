# Phase G 核团回填验证运行报告

日期：2026-06-04

基准计划文件：

`E:\LQ\Code_Files\VS_Code\read\.kiro\docs\phase-g-nuclei-backfill-validation-plan-codex-v1.md`

本报告记录 Phase G 核团材料回填方案的 Step 1-5 自动验证结果。这里的 Phase G 指的是：网格仍按 Phase F 的方式生成，电极、包膜、ROI 边界继续保持精确；mesh 完成后，再遍历脑组织 tet，把原本 label `10` 的 tet 按中心点采样核团 label image，命中 `1..6` 时改成对应核团材料标签。

## Step 1：开关与日志激活验证

使用的 AI plan：

`E:\LQ\Code_Files\VS_Code\read\.kiro\ai_plans\phase_g_step1_f1_f4.json`

批次输出目录：

`E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818`

验证结果：

- F1/F2/F3/F4 全部运行成功。
- nuclei-off 运行日志显示 Phase G 处于关闭状态。
- nuclei-on 运行日志显示 Phase G 处于开启状态，并且 `relabeled tet count > 0`、`outOfExtent = 0`。
- F1、F2、F4 的核团 tet 数量为：`1:551, 2:25704, 3:439, 4:495, 5:999, 6:234`。
- F3 的核团 tet 数量为：`1:563, 2:25999, 3:416, 4:526, 5:963, 6:229`。

解释：

开关行为是有效的。未启用核团分区时，mesh 保持 Phase F 风格；启用核团分区时，程序会在 mesh 生成后把一部分脑组织 tet 从 label `10` 改成核团 label `1..6`。

## Step 2：VTU MaterialLabel 审计

使用脚本：

`E:\LQ\Code_Files\VS_Code\read\.kiro\tools\phase_g_vtu_material_audit.py`

审计报告：

- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818\002_F1_nuclei_on\vtu_audit.md`
- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818\004_F2_nuclei_on\vtu_audit.md`
- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818\006_F3_nuclei_on\vtu_audit.md`
- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818\008_F4_nuclei_on\vtu_audit.md`

验证结果：

- F1/F2/F4：总 cell 数 274387，核团 cell 数 28422，核团体积 2633.024088 mm3。
- F3：总 cell 数 267232，核团 cell 数 28696，核团体积 2651.034993 mm3。
- 非核团材料标签 `50, 51, 101, 102, 103, 104` 在对应 case 中保持存在且数量正确。
- F3 没有 label `51`，这是预期现象，因为 F3 是无包膜 case。

解释：

VTU 文件中的 `MaterialLabel` 与日志中的标签统计一致。也就是说，核团材料标签不只是出现在中间日志里，而是真的进入了最终求解结果文件。

## Step 3：tet 中心点重采样验证

使用的 AI plan：

`E:\LQ\Code_Files\VS_Code\read\.kiro\ai_plans\phase_g_step3_f1_centroid.json`

批次输出目录：

`E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step3_f1_centroid_20260604_105142`

使用脚本：

`E:\LQ\Code_Files\VS_Code\read\.kiro\tools\phase_g_centroid_resample_audit.py`

审计报告：

`E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step3_f1_centroid_20260604_105142\001_F1_nuclei_on\centroid_resample_audit.md`

验证结果：

- nucleiTotal = 28422。
- nucleiMatch = 28421。
- nucleiMatchRate = 0.9999648159876152。
- nucleiOutOfExtent = 0。
- label10NucleiSamples = 0。
- 有 1 个 mismatch 被接受为边界邻近误差，原因是 `.mesh` 文本坐标序列化精度导致的半格点附近舍入差异。

解释：

mesh 中的核团标签基本严格匹配对齐后的 NIfTI label image 的 tet 中心点采样结果。唯一 1 个 mismatch 发生在核团边界附近，而且 label `10` 的 tet 没有采样到核团，因此这不是坐标系错位或整体采样错误。

## Step 4：Phase F 表面 label 与 BC 回归验证

使用脚本：

`E:\LQ\Code_Files\VS_Code\read\.kiro\tools\phase_g_log_bc_audit.py`

审计报告：

- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818\F1_bc_audit.md`
- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818\F2_bc_audit.md`
- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818\F3_bc_audit.md`
- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818\F4_bc_audit.md`

验证结果：

- F1/F2/F3/F4 全部通过。
- nuclei-off 与 nuclei-on 的 triangle labels 完全一致。
- nuclei-on 的 triangle labels 仍只包含 `-1, 50, 51, 101, 102, 103, 104`；F3 没有 `51`，符合无包膜 case 的预期。
- nuclei-off 没有核团 tet label。
- nuclei-on 有核团 tet label。
- 非核团 tet labels 保持不变。
- 脑组织 tet 数量守恒关系成立：off brain count = on brain count + nuclei count。
- 选中 contact 的 effectiveRatio 稳定，且等于 1.0。
- 选中 contact 的 brainFacingRatio 稳定。
- 非激活 contact 的 flux 仍接近 0。
- 没有发现 unknown/fallback 相关警告。

解释：

核团回填没有污染 Phase F 已经修好的表面 label，也没有破坏 contact BC 的选择逻辑。这一步直接回应了之前的顾虑：即使电极附近穿过核团，核团 label 也不会重新触发旧的 BC bug，因为核团只回填到 tet 材料标签，不参与电极/包膜/ROI 的表面 label 判定。

## Step 5：同网格 nuclei-on vs homogeneous 物理 A/B 对比

homogeneous mesh 生成脚本：

`E:\LQ\Code_Files\VS_Code\read\.kiro\tools\phase_g_make_homogeneous_mesh.py`

A/B 对比脚本：

`E:\LQ\Code_Files\VS_Code\read\.kiro\tools\phase_g_same_mesh_ab_compare.py`

使用的 AI plan：

`E:\LQ\Code_Files\VS_Code\read\.kiro\ai_plans\phase_g_step5_same_mesh_ab.json`

批次输出目录：

`E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step5_same_mesh_ab_20260604_110613`

A/B 对比报告：

`E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step5_same_mesh_ab_20260604_110613\same_mesh_ab_compare.md`

验证结果：

- Step 5 对比通过。
- A 组和 B 组点数完全一致：49272。
- A 组和 B 组 cell 数完全一致：274387。
- A 组和 B 组点坐标字节级一致。
- A 组和 B 组 connectivity 字节级一致。
- 标签变化只包含：`1->10:551, 2->10:25704, 3->10:439, 4->10:495, 5->10:999, 6->10:234`。
- 没有任何非预期材料标签变化。
- 非核团标签 `50, 51, 101, 102, 103, 104` 保持不变。
- 两组的选中 contact effectiveRatio 都保持 1.0。
- 两组的选中 contact brainFacingRatio 都保持 0.86013986。
- 两组 flux missing faces 都保持 0。
- 电场确实发生变化：E mean abs diff = 0.017762540955 V/mm。
- 电位确实发生变化：Potential mean abs diff = 0.037190675802 V。
- homogeneous B 的 High-E volume 为 162.970073 mm3；nuclei-on A 的 High-E volume 为 140.825615 mm3。
- homogeneous B 的 selected contact flux 为 -3.526202506215 mA；nuclei-on A 的 selected contact flux 为 -6.050209951974 mA。

解释：

同网格 A/B 对比把变量隔离得比较干净：几何和边界条件诊断保持稳定，唯一预期变化是材料标签从核团 `1..6` 合并回脑组织 `10`。在这个前提下，电场、电位、High-E volume、selected contact flux 都发生了变化，说明 Phase G 核团标签确实作为材料电导率进入了 FEM 求解，而不是只改变了显示标签。

## 当前结论

Step 1-5 没有发现重大偏差。

当前证据支持：

- Phase G 核团分区由开关控制。
- 核团标签是在 mesh 生成之后，通过 tet 中心点采样回填到脑组织 tet 上。
- 最终 VTU 文件包含核团材料标签。
- 回填后的核团标签与对齐后的 NIfTI label image 在 tet 中心点层面高度一致，仅有 1 个可接受的边界邻近 mismatch。
- Phase F 的表面 labels 和 contact BC 行为保持稳定。
- 同网格 A/B 证明：启用核团材料标签后，FEM 物理输出确实发生相应变化。

剩余说明：

这轮验证基于当前标准样例 image 和 F1-F4 case。对于当前 Phase G 实现门槛，这已经足够作为“基准可用状态”；后续如果要提高鲁棒性，可以再加入更多患者/样本数据做扩展验证。
