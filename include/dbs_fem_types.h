#ifndef DBS_FEM_TYPES_H
#define DBS_FEM_TYPES_H
#pragma execution_character_set("utf-8")

#include <array>
#include <vector>
#include <map>
#include <cmath>
#include <string>

// ============================================================
// DBS FEM 仿真数据结构与物理参数
// ============================================================

namespace dbs_fem {

// ----- 标签定义 -----
// 核团标签 1-6 直接来自 NIfTI 分割文件
// 1 = Left Red Nucleus,     4 = Right Red Nucleus
// 2 = Left Substantia Nigra,5 = Right Substantia Nigra
// 3 = Left STN,             6 = Right STN
constexpr int LABEL_BACKGROUND       = 0;
constexpr int LABEL_BRAIN_TISSUE     = 10;   // 非核团脑组织 (白质)
constexpr int LABEL_ELECTRODE_BODY   = 50;   // 电极绝缘体 (聚氨酯)
constexpr int LABEL_ENCAPSULATION    = 51;
constexpr int LABEL_CONTACT_BASE     = 101;  // 触点标签: 101, 102, 103, 104

// ----- 电导率表 (S/m) — Lead-DBS 经验值 -----
inline std::map<int, double> getDefaultConductivityMap()
{
    std::map<int, double> m;
    // 核团 (灰质)
    for (int i = 1; i <= 6; ++i)
        m[i] = 0.333;              // Gray Matter

    m[LABEL_BRAIN_TISSUE]     = 0.115;   // Gabriel 1996 @10kHz baseline
    m[LABEL_ELECTRODE_BODY]   = 1e-16;   // Polyurethane (绝缘)
    m[LABEL_ENCAPSULATION]    = 0.115;

    // 触点 (Pt/Ir 合金, 极高导电)
    for (int c = 0; c < 4; ++c)
        m[LABEL_CONTACT_BASE + c] = 1e7;

    return m;
}

// ----- VTA 阈值 (V/mm) — Hemm/Åström 经验公式 -----
// 基于脉宽的非线性插值
inline double getVTAThreshold(int pulseWidth_us)
{
    // 经验数据点
    // 60 μs  → 0.200 V/mm
    // 90 μs  → 0.165 V/mm
    // 120 μs → 0.130 V/mm
    if (pulseWidth_us <= 60)  return 0.200;
    if (pulseWidth_us >= 120) return 0.130;

    // 60-120 μs 之间线性插值
    double t = (pulseWidth_us - 60.0) / 60.0;  // 0.0 ~ 1.0
    return 0.200 + t * (0.130 - 0.200);         // 0.200 → 0.130
}

// ----- DBS 仿真输入规格 -----
struct DBSSimSpec
{
    // ----- 网格生成模式 (路线 C) -----
    enum MeshingMode {
        IMAGE_BASED = 0,    // P0/P1/方案 0: Labeled_mesh_domain_3 (image-based)
        CONFORMING  = 1     // 路线 C: Polyhedral_complex_mesh_domain_3 (符合边界)
    };
    int meshingMode = CONFORMING;

    // 阶段 A: 暂时忽略核团 (整个脑组织视作均匀介质，避开电极穿核团的拓扑冲突)
    // 阶段 B: 设为 false 后启用核团 STL 提取
    bool stageA_ignoreNuclei = true;

    // 轨迹
    double entry[3]  = {0, 0, 0};
    double target[3] = {0, 0, 0};

    // 电极型号参数
    int    leadType       = 0;      // 0=3389, 1=3387, 2=Cartesia
    double leadRadius     = 0.635;  // mm
    double contactLength  = 1.5;    // mm
    double contactSpacing = 0.5;    // mm
    int    numContacts    = 4;
    double depthOffset    = 0.0;    // mm, 沿轨迹偏移

    // 触点极性: -1=阴极, 0=关, +1=阳极
    int contactPolarity[4] = {0, 0, 0, 0};

    // 刺激参数
    double amplitude      = 3.0;    // mA (电流控制) 或 V (电压控制)
    int    pulseWidth      = 60;    // μs
    bool   isVoltageControl = true; // true=电压控制, false=电流控制
    bool   useFloatingContacts = true;
    bool   useEncapsulationLayer = true;
    double sigmaBrain = 0.115;
    double sigmaEncapsulation = 0.115;
    double encapsulationThickness = 0.2;

    // 网格参数 (P2 方案 0：image-based + STL 后处理打标签)
    // 不再让标签图包含电极，因此电极相关的网格密度由 facet_size 控制
    double roiHalfSize    = 25.0;   // mm, 局部 ROI 半边长（P1.2 验证过的稳定值）
    double meshContactSize = 0.3;   // mm, 触点区域网格尺寸（暂未使用，预留）
    double meshNucleusSize = 0.8;   // mm, 核团区域网格尺寸（暂未使用，预留）
    double meshBrainSize   = 1.5;   // mm, 脑组织区域网格尺寸（CGAL cell_size）

    // ----- 已弃用 (P2 方案 0 不再光栅化电极) -----
    // 旧版本曾用径向膨胀让电极在 1mm 标签图中保留足够体素。
    // 方案 0 改为"网格只含脑组织 + 核团，电极标签靠 STL 后处理打"，
    // 不再需要膨胀。保留字段仅为二进制兼容，固定为 0。
    double femContactInflateMM = 0.0;
};

// ----- 四面体网格数据 -----
struct DBSMeshData
{
    // 节点坐标 (Np × 3)
    std::vector<std::array<double, 3>> points;

    // 四面体 (Nc × 4 节点索引) + 材料标签
    std::vector<std::array<size_t, 4>> tets;
    std::vector<int> tet_labels;

    // 表面三角形 + 标签 (用于标记触点边界)
    std::vector<std::array<size_t, 3>> tris;
    std::vector<int> tri_labels;

    size_t numPoints() const { return points.size(); }
    size_t numTets()   const { return tets.size(); }
    size_t numTris()   const { return tris.size(); }
};

// ----- 四面体梯度 (用于电场计算) -----
struct TetGrad
{
    double g[4][3];  // 4 个节点的形函数梯度 (3D)
    double vol;      // 四面体体积
};

// 计算四面体的形函数梯度和体积
// 输入: 四个顶点坐标 p0-p3
// 输出: TetGrad (梯度 + 体积)
// 返回: false 表示退化四面体 (体积 ≈ 0)
inline bool computeTetGrad(const double p0[3], const double p1[3],
                           const double p2[3], const double p3[3],
                           TetGrad& tg)
{
    double j00 = p1[0] - p0[0], j01 = p2[0] - p0[0], j02 = p3[0] - p0[0];
    double j10 = p1[1] - p0[1], j11 = p2[1] - p0[1], j12 = p3[1] - p0[1];
    double j20 = p1[2] - p0[2], j21 = p2[2] - p0[2], j22 = p3[2] - p0[2];

    double det = j00 * (j11*j22 - j12*j21)
               - j01 * (j10*j22 - j12*j20)
               + j02 * (j10*j21 - j11*j20);

    tg.vol = std::abs(det) / 6.0;
    if (tg.vol < 1e-15) return false;

    double inv_det = 1.0 / det;

    tg.g[1][0] =  (j11*j22 - j12*j21) * inv_det;
    tg.g[1][1] =  (j02*j21 - j01*j22) * inv_det;
    tg.g[1][2] =  (j01*j12 - j02*j11) * inv_det;

    tg.g[2][0] =  (j12*j20 - j10*j22) * inv_det;
    tg.g[2][1] =  (j00*j22 - j02*j20) * inv_det;
    tg.g[2][2] =  (j02*j10 - j00*j12) * inv_det;

    tg.g[3][0] =  (j10*j21 - j11*j20) * inv_det;
    tg.g[3][1] =  (j01*j20 - j00*j21) * inv_det;
    tg.g[3][2] =  (j00*j11 - j01*j10) * inv_det;

    for (int d = 0; d < 3; ++d) {
        tg.g[0][d] = -(tg.g[1][d] + tg.g[2][d] + tg.g[3][d]);
    }

    return true;
}

} // namespace dbs_fem

#endif // DBS_FEM_TYPES_H
