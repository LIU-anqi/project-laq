# Phase G 核团 σ 分区 — Codex 基准实现计划 v1

**日期**：2026-06-03
**状态**：审计后基准计划，用于后续代码实现
**前置状态**：Phase F 已通过 OSS-DBS 验收，0.865mm offset patch 和 `inferSurfaceLabel` 优先级化已完成

---

## 1. 目标

把 FEM 从“均匀脑组织 σ=0.115 S/m”升级为“核团区域 σ=0.333 S/m”，但不改变 Phase F 已验证通过的电极/包膜 conforming mesh、BC、floating、hollow 逻辑。

核心方法：**post-mesh centroid backfill**。

CGAL 仍只生成 Phase F 那套 subdomain：

```text
brain 10 / encap 51 / insulator 50 / contact 101..104
```

mesh 完成后，在写 `.mesh` 前遍历 `label==10` 的 brain tet，按 tet centroid 采样原始 `m_labelImage`，命中 `1..6` 就把 tet label 改成核团 label。

---

## 2. 必须保持不变

- 不让核团 STL / polyhedron 参与 CGAL。
- 不改 `inferSurfaceLabel` 主逻辑。
- 不改 `DBSSimWorker` 的 floating aliasing。
- 不改 hollow / `nodeUsedByBrain`。
- 不改电极 STL 几何。
- backfill OFF 时 Phase F F1-F4 结果必须保持一致。

---

## 3. 对旧计划的修正

`(1..6, contact)`、`(1..6, encap)` 这些 pair 在当前 Phase G 不会真实出现，因为核团 label 是 mesh 后处理写进 tet 的，不是 CGAL subdomain。现有 priority 规则可以保留，作为未来保险；但它不是 Phase G 的必要依赖。

---

## 4. 代码改动计划

### 4.1 `include/dbs_fem_types.h`

新增字段，不重命名旧的 `stageA_ignoreNuclei`：

```cpp
bool useNucleiBackfill = false;
double sigmaNuclei = 0.333;
bool logNucleiBackfill = true;
```

默认 `false`，保证 Phase F 回归不受影响。

### 4.2 `src/dbs_mesh_worker.cpp`

在 `runConformingMeshing()` 内部实现局部 backfill lambda，而不是先在 `.h` 暴露函数。原因是当前 `Tet` 是局部 struct，强行放到头文件会扩大改动面。

接入点：收集完 `tets` / `verts` 后，写 `.mesh` 前。最终必须在 `Tetrahedra` 输出前完成。

逻辑：

```cpp
if (m_spec.useNucleiBackfill && m_labelImage) {
    backfill label==10 tets by centroid sampling m_labelImage;
}
```

只改 `label==10` 的 tet，不碰：

- `50` insulator
- `51` encap
- `101..104` contacts
- 已经不是 brain 的任何 label

### 4.3 Backfill 采样规则

对每个 brain tet：

- 取 4 个 vertex 的平均坐标作为 centroid。
- 使用 `m_labelImage->GetOrigin / GetSpacing / GetExtent` 做 world -> voxel index。
- nearest-neighbor：`round((coord-origin)/spacing)`。
- 越界则保持 `10`。
- `label in [1,6]` 才回填，否则保持 `10`。

输出诊断：

- candidate brain tet 数
- relabeled 总数
- relabeled / candidate 比例
- label 1-6 各自 tet 数

### 4.4 `src/dbs_sim_worker.cpp`

如果新增 `sigmaNuclei` 字段，就必须接进求解器：

```cpp
for (int i = 1; i <= 6; ++i) {
    sigma_map[i] = m_spec.sigmaNuclei;
}
```

并加永久 sanity 日志：

```text
[FEM] conductivity sigma_brain=..., sigma_nuclei=..., sigma_encap=...
```

不用重构 `sigmaForLabel()`。

### 4.5 UI 接入策略

第一版只加轻量开关，方便手动验证：

- `LeadSimulatorWidget` 增加“启用核团 σ 分区” checkbox。
- 默认不勾选，保持 Phase F。
- `dicomviewer_3d::slot_computeRealVTA()` 把 checkbox 状态写入 `spec.useNucleiBackfill`。

---

## 5. 验证计划

1. 编译通过。
2. `useNucleiBackfill=false` 跑 Phase F F1，结果应接近既有 baseline。
3. `useNucleiBackfill=true` 跑标准 STN case，日志中应出现 label 1-6 的 relabeled tet，至少 label 3 应非零。
4. VTU 的 `MaterialLabel` 应包含 `1..6`。
5. FEM 日志确认 `sigma_nuclei=0.333`。
6. backfill ON/OFF 对比 VTA。变化幅度作为 sanity，不作为硬门槛。
7. 若 ON 后完全无变化，优先查：`m_labelImage` 是否为空、坐标是否对齐、`sigma_map[1..6]` 是否真的生效。
8. 若 OFF 回归变化明显，停止推进，查是否误改了 Phase F 路径。

---

## 6. 完成标准

Phase G 初版完成只要求：

- backfill OFF 完全兼容 Phase F。
- backfill ON 能稳定产生核团 tet label。
- 求解器对 1-6 使用 nuclei σ。
- VTU 可看到 `MaterialLabel=1..6`。
- 没有新增 `Unknown surface pair` warning。
- 没有触碰 Phase F 已验证的 BC/floating/hollow 逻辑。
