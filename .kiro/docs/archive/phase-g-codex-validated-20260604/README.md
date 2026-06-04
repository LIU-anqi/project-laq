# Phase G 验证版本归档说明

归档日期：2026-06-04

归档目录：

`E:\LQ\Code_Files\VS_Code\read\.kiro\docs\archive\phase-g-codex-validated-20260604`

## 这份归档是什么

这是 Phase G 核团材料回填版本的轻量归档。它记录的是“这版怎么实现、怎么跑、用了什么参数、结果是否通过验证、以后怎么复核”。

这不是完整源码副本，也不是 Git commit。它适合当前阶段做基准记录；如果后续你确认这版要长期冻结，建议再做一次正式 Git commit 或 tag。

## 为什么不复制整份代码

不建议复制 C++ 源码文件做版本归档，因为那样很快会出现多份代码，不知道哪份才是当前真实版本。更稳妥的方式是：

- 源码版本由 Git 管理。
- 归档目录记录参数、结果、验证报告。
- 当前未提交代码的改动用 patch 文件记录。
- 大型 `.vtu`、`.mesh` 文件不复制，只记录原始输出路径和小型审计结果。

## 归档包含什么

- `parameter-manifest.json`：本版关键参数清单，包括 image、坐标、电导率、刺激参数、AI plan、验证批次路径和关键结果。
- `reports/phase-g-validation-run-report-codex-20260604.md`：中文总验证报告。
- `reports/phase-g-nuclei-backfill-validation-plan-codex-v1.md`：原始 Step 1-5 验证计划。
- `reports/same_mesh_ab_compare.md/json`：Step 5 同网格 A/B 对比结果。
- `reports/centroid_resample_audit.md/json`：Step 3 tet 中心点重采样审计。
- `reports/F1_bc_audit.*` 到 `F4_bc_audit.*`：Step 4 表面 label 和 BC 回归审计。
- `plans/`：用于自动复现实验的 AI plan JSON。
- `tools/`：本次验证使用的 Python 审计脚本。
- `run-configs/`：代表性运行配置，包括 F1 nuclei-on、Step 5 nuclei-on、Step 5 homogeneous。
- `patches/`：当前相关源码改动的 patch。
- `git-status.txt`：归档时工作树状态。

## 没有复制什么

- 没有复制 C++ 源码文件。
- 没有复制大型 `.vtu` 文件。
- 没有复制大型 `.mesh` 文件。
- 没有复制 build 目录。

大型结果文件仍在原始输出目录中，例如：

- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step1_f1_f4_20260604_103818`
- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step3_f1_centroid_20260604_105142`
- `E:\LQ\Code_Files\VS_Code\read\out\ai_runs\phase_g_step5_same_mesh_ab_20260604_110613`

如果以后清理 `out` 目录，大型 `.vtu/.mesh` 原始文件可能会消失；但本归档里的小型审计报告和参数清单仍能保留当时结论。

## 本版核心结论

Phase G 当前实现门槛已通过：

- 核团分区开关有效。
- 核团 label 是 mesh 完成后按 tet 中心点采样回填的。
- 最终 VTU 中存在核团材料标签。
- 回填标签与对齐后的 NIfTI label image 高度一致。
- Phase F 表面 label 和 contact BC 没有被核团标签污染。
- 同网格 A/B 证明核团材料标签进入了 FEM 求解，并改变了物理输出。

## 后续如果要做正式版本冻结

建议流程：

1. 先确认当前运行和报告都符合预期。
2. 清理不需要纳入版本的临时输出。
3. 用 Git 只提交相关源码、计划、脚本、文档。
4. commit message 可以类似：`Archive Phase G nuclei backfill validated baseline`。
5. 如需更强冻结，可以再打 tag，例如：`phase-g-validated-20260604`。

当前我没有直接做 commit，因为工作树里还有不少历史输出和未跟踪文件，直接提交容易把无关内容一起带进去。
