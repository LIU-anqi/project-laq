#include "dbs_sim_worker.h"

#ifdef DBS_HAS_ARMADILLO
#define ARMA_DONT_USE_WRAPPER
#define ARMA_DONT_PRINT_ERRORS
#define ARMA_USE_SUPERLU
#include <armadillo>
#endif

#include <vtkNew.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPoints.h>
#include <vtkTetra.h>
#include <vtkDoubleArray.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkIntArray.h>
#include <vtkFieldData.h>

#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QRegularExpression>
#include <cmath>
#include <map>
#include <set>
#include <algorithm>
#include <limits>
#include <array>

// ============================================================
DBSSimWorker::DBSSimWorker(QObject* parent)
    : QObject(parent)
{}

// ============================================================
// 主流程
// ============================================================
void DBSSimWorker::process()
{
    try {
        emit progressUpdated(0, "开始 FEM 求解...");

        // Step 1: 读取网格
        emit progressUpdated(5, "读取 .mesh 文件...");
        dbs_fem::DBSMeshData mesh;
        if (!readMeditMesh(m_meshPath, mesh)) {
            emit errorOccurred("读取 .mesh 文件失败");
            return;
        }
        qDebug() << "[FEM] 网格: " << mesh.numPoints() << "节点,"
                 << mesh.numTets() << "四面体," << mesh.numTris() << "三角形";
        std::map<int, size_t> triCounts, tetCounts;
        for (int label : mesh.tri_labels) triCounts[label]++;
        for (int label : mesh.tet_labels) tetCounts[label]++;
        qDebug() << "[FEM] triangle label 计数:";
        for (const auto& kv : triCounts) {
            qDebug() << "[FEM]   tri label" << kv.first << ":" << kv.second;
        }
        qDebug() << "[FEM] tetra label 计数:";
        for (const auto& kv : tetCounts) {
            qDebug() << "[FEM]   tet label" << kv.first << ":" << kv.second;
        }

        // Step 2: 求解
        emit progressUpdated(20, "组装刚度矩阵并求解...");
        std::vector<double> phi, E_mag;
        if (!solveElectricField(mesh, phi, E_mag)) {
            emit errorOccurred("FEM 求解失败");
            return;
        }

        // Step 3: 构建结果
        emit progressUpdated(90, "构建 VTK 结果...");
        auto result = buildResultGrid(mesh, phi, E_mag);
        if (!result) {
            emit errorOccurred("构建 VTK 结果失败");
            return;
        }

        emit progressUpdated(100, "FEM 求解完成");
        emit finished(result);

    } catch (const std::exception& e) {
        emit errorOccurred(QString("FEM 异常: %1").arg(e.what()));
    } catch (...) {
        emit errorOccurred("FEM 未知异常");
    }
}

// ============================================================
// Step 1: 读取 Medit .mesh 文件
// (复用 TTF readMeditMesh + IRE loadcgalmesh 逻辑)
// ============================================================
bool DBSSimWorker::readMeditMesh(const QString& path, dbs_fem::DBSMeshData& mesh)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[FEM] 无法打开 .mesh:" << path;
        return false;
    }

    QTextStream in(&file);
    QString line;
    QRegularExpression ws("\\s+");

    while (!in.atEnd()) {
        line = in.readLine().trimmed();

        // --- Vertices ---
        if (line.startsWith("Vertices")) {
            int count = in.readLine().trimmed().toInt();
            mesh.points.resize(count);
            for (int i = 0; i < count; ++i) {
                QStringList parts = in.readLine().trimmed().split(ws, Qt::SkipEmptyParts);
                if (parts.size() >= 3) {
                    mesh.points[i] = {
                        parts[0].toDouble(),
                        parts[1].toDouble(),
                        parts[2].toDouble()
                    };
                }
            }
        }

        // --- Triangles ---
        else if (line.startsWith("Triangles")) {
            int count = in.readLine().trimmed().toInt();
            mesh.tris.resize(count);
            mesh.tri_labels.resize(count);
            for (int i = 0; i < count; ++i) {
                QStringList parts = in.readLine().trimmed().split(ws, Qt::SkipEmptyParts);
                if (parts.size() >= 4) {
                    mesh.tris[i] = {
                        static_cast<size_t>(parts[0].toULongLong() - 1),  // 1-based → 0-based
                        static_cast<size_t>(parts[1].toULongLong() - 1),
                        static_cast<size_t>(parts[2].toULongLong() - 1)
                    };
                    mesh.tri_labels[i] = parts[3].toInt();
                }
            }
        }

        // --- Tetrahedra ---
        else if (line.startsWith("Tetrahedra")) {
            int count = in.readLine().trimmed().toInt();
            mesh.tets.resize(count);
            mesh.tet_labels.resize(count);
            for (int i = 0; i < count; ++i) {
                QStringList parts = in.readLine().trimmed().split(ws, Qt::SkipEmptyParts);
                if (parts.size() >= 5) {
                    mesh.tets[i] = {
                        static_cast<size_t>(parts[0].toULongLong() - 1),
                        static_cast<size_t>(parts[1].toULongLong() - 1),
                        static_cast<size_t>(parts[2].toULongLong() - 1),
                        static_cast<size_t>(parts[3].toULongLong() - 1)
                    };
                    mesh.tet_labels[i] = parts[4].toInt();
                }
            }
        }
    }

    return mesh.numPoints() > 0 && mesh.numTets() > 0;
}

// ============================================================
// Step 2: FEM 求解
// ============================================================
bool DBSSimWorker::solveElectricField(const dbs_fem::DBSMeshData& mesh,
                                       std::vector<double>& phi_out,
                                       std::vector<double>& E_mag_out)
{
#ifdef DBS_HAS_ARMADILLO
    const size_t Np = mesh.numPoints();
    const size_t Nc = mesh.numTets();
    m_diag = FEMDiagnostics();
    m_diag.numPoints = static_cast<int>(Np);
    m_diag.numTets = static_cast<int>(Nc);
    m_diag.numTris = static_cast<int>(mesh.numTris());
    m_diag.amplitude = m_spec.amplitude;
    m_diag.pulseWidth = m_spec.pulseWidth;

    auto sigma_map = dbs_fem::getDefaultConductivityMap();
    sigma_map[dbs_fem::LABEL_BRAIN_TISSUE] = m_spec.sigmaBrain;
    sigma_map[dbs_fem::LABEL_ENCAPSULATION] = m_spec.sigmaEncapsulation;
    auto sigmaForLabel = [&sigma_map, this](int label) -> double {
        if (label == dbs_fem::LABEL_BRAIN_TISSUE) return m_spec.sigmaBrain;
        if (label == dbs_fem::LABEL_ENCAPSULATION) return m_spec.sigmaEncapsulation;
        auto it = sigma_map.find(label);
        if (it != sigma_map.end()) return it->second;
        return m_spec.sigmaBrain;
    };

    // ----- 1. 标记 Dirichlet 边界节点 -----
    std::vector<bool> isDir(Np, false);
    std::vector<double> Vdir(Np, 0.0);

    // 【P2 方案 0】判断 label 是否属于电极内部（用于"挖空"+ 内部节点处理）
    auto isElectrodeLabel = [](int lab) {
        return lab == dbs_fem::LABEL_ELECTRODE_BODY ||
               (lab >= dbs_fem::LABEL_CONTACT_BASE &&
                lab < dbs_fem::LABEL_CONTACT_BASE + 4);
    };

    auto voltageForPolarity = [this](int polarity) -> double {
        if (polarity == -1) return -m_spec.amplitude;
        if (polarity == 1) return m_spec.amplitude;
        return 0.0;
    };

    std::set<size_t> contactSurfaceNodes[4];
    std::set<size_t> allContactSurfaceNodes[4];
    std::set<size_t> contactVolumeNodes[4];
    std::set<size_t> electrodeSurfaceNodes;
    std::map<int, size_t> electrodeSurfaceTriCounts;
    std::map<int, std::set<size_t>> electrodeSurfaceNodesByLabel;
    std::set<size_t> floatingContactNodes[4];
    std::vector<int> floatingMasterOfNode(Np, -1);
    std::array<int, 4> floatingMaster = {{-1, -1, -1, -1}};

    // 触点候选节点统计: 基于四面体标签 (label 101-104)
    for (size_t ci = 0; ci < Nc; ++ci) {
        int label = mesh.tet_labels[ci];
        int contactIdx = label - dbs_fem::LABEL_CONTACT_BASE;
        if (contactIdx < 0 || contactIdx >= m_spec.numContacts) continue;

        for (int k = 0; k < 4; ++k) {
            contactVolumeNodes[contactIdx].insert(mesh.tets[ci][k]);
        }
    }

    // 触点边界: 优先基于 CGAL 表面三角形标签 (label 101-104)
    for (size_t fi = 0; fi < mesh.numTris(); ++fi) {
        int label = mesh.tri_labels[fi];
        if (isElectrodeLabel(label)) {
            electrodeSurfaceTriCounts[label]++;
            for (int k = 0; k < 3; ++k) {
                size_t ni = mesh.tris[fi][k];
                electrodeSurfaceNodes.insert(ni);
                electrodeSurfaceNodesByLabel[label].insert(ni);
            }
        }
        int contactIdx = label - dbs_fem::LABEL_CONTACT_BASE;
        if (contactIdx < 0 || contactIdx >= m_spec.numContacts) continue;

        for (int k = 0; k < 3; ++k) {
            allContactSurfaceNodes[contactIdx].insert(mesh.tris[fi][k]);
        }

        int polarity = m_spec.contactPolarity[contactIdx];
        double voltage = voltageForPolarity(polarity);
        for (int k = 0; k < 3; ++k) {
            size_t ni = mesh.tris[fi][k];
            contactSurfaceNodes[contactIdx].insert(ni);
            if (!m_spec.useFloatingContacts || polarity != 0) {
                isDir[ni] = true;
                Vdir[ni] = voltage;
            }
        }
    }

    // 调试: 确认触点参数
    qDebug() << "[FEM] 电极表面节点总数:" << electrodeSurfaceNodes.size();
    for (const auto& kv : electrodeSurfaceTriCounts) {
        qDebug() << "[FEM]   electrode surface tri label" << kv.first << ":" << kv.second;
    }
    for (int i = 0; i < m_spec.numContacts; ++i) {
        m_diag.contactSurfaceNodes[i] = static_cast<int>(contactSurfaceNodes[i].size());
        m_diag.contactVolumeNodes[i] = static_cast<int>(contactVolumeNodes[i].size());
        m_diag.contactAllSurfaceNodes[i] = static_cast<int>(allContactSurfaceNodes[i].size());
        if (m_spec.contactPolarity[i] != 0 && m_diag.selectedContact < 0) {
            m_diag.selectedContact = i;
            m_diag.selectedVoltage = voltageForPolarity(m_spec.contactPolarity[i]);
        }
        qDebug() << "[FEM] Contact" << i
                 << "polarity=" << m_spec.contactPolarity[i]
                 << "voltage=" << voltageForPolarity(m_spec.contactPolarity[i])
                 << "surfaceNodes=" << contactSurfaceNodes[i].size()
                 << "allSurfaceNodes=" << allContactSurfaceNodes[i].size()
                 << "volumeNodes=" << contactVolumeNodes[i].size();
    }
    qDebug() << "[FEM] amplitude=" << m_spec.amplitude
             << "interpreted_as_voltage_V=" << m_spec.isVoltageControl
             << "useFloatingContacts=" << m_spec.useFloatingContacts
             << "useEncapsulationLayer=" << m_spec.useEncapsulationLayer;
    qDebug() << "[FEM] conductivity sigma_brain=" << m_spec.sigmaBrain << "S/m"
             << "sigma_encap=" << m_spec.sigmaEncapsulation << "S/m"
             << "encapThickness=" << m_spec.encapsulationThickness << "mm";
    qDebug() << "[FEM] flux residual unit path: coords=mm, grad=1/mm, vol=mm^3, K=sigma*vol*1e-3 => A/V, residual=K*phi A, report=mA";

    // 外边界接地: 仅 tri_label == -1 (外边界面) 的三角形节点接地
    size_t numContactDir = 0;
    for (auto b : isDir) if (b) numContactDir++;

    size_t numBoundaryDir = 0;
    size_t numBoundaryElectrodeSkipped = 0;
    for (size_t fi = 0; fi < mesh.numTris(); ++fi) {
        if (mesh.tri_labels[fi] != -1) continue;  // 只处理外边界
        for (int k = 0; k < 3; ++k) {
            size_t ni = mesh.tris[fi][k];
            if (electrodeSurfaceNodes.count(ni)) {
                numBoundaryElectrodeSkipped++;
                continue;
            }
            if (!isDir[ni]) {
                isDir[ni] = true;
                Vdir[ni] = 0.0;
                numBoundaryDir++;
            }
        }
    }
    qDebug() << "[FEM] outer boundary grounding skipped electrode-surface node hits:"
             << numBoundaryElectrodeSkipped;

    // ----- 1.5 挖空电极内部 (P2 方案 0 关键步骤) -----
    // 标记每个节点是否被"脑组织 tet"占用。挖空电极后，
    // 仅被电极 tet 占用的"孤立内部节点"必须被强制 Dirichlet 否则矩阵奇异。
    std::vector<bool> nodeUsedByBrain(Np, false);
    for (size_t ci = 0; ci < Nc; ++ci) {
        int lab = mesh.tet_labels[ci];
        if (isElectrodeLabel(lab)) continue;
        for (int k = 0; k < 4; ++k) nodeUsedByBrain[mesh.tets[ci][k]] = true;
    }
    size_t surfaceNodesNoBrain = 0;
    for (size_t ni : electrodeSurfaceNodes) {
        if (!nodeUsedByBrain[ni]) surfaceNodesNoBrain++;
    }
    for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
        int allShared = 0;
        int allNoBrain = 0;
        int activeShared = 0;
        int activeNoBrain = 0;
        for (size_t ni : allContactSurfaceNodes[i]) {
            if (nodeUsedByBrain[ni]) allShared++;
            else allNoBrain++;
        }
        for (size_t ni : contactSurfaceNodes[i]) {
            if (nodeUsedByBrain[ni]) activeShared++;
            else activeNoBrain++;
        }
        m_diag.contactAllSurfaceBrainShared[i] = allShared;
        m_diag.contactAllSurfaceNoBrain[i] = allNoBrain;
        m_diag.contactSurfaceBrainShared[i] = activeShared;
        m_diag.contactSurfaceNoBrain[i] = activeNoBrain;
        if (m_diag.contactAllSurfaceNodes[i] > 0) {
            m_diag.contactBrainFacingRatio[i] =
                static_cast<double>(allShared) / static_cast<double>(m_diag.contactAllSurfaceNodes[i]);
        }
    }
    if (m_spec.useFloatingContacts) {
        for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
            if (m_spec.contactPolarity[i] != 0) continue;
            for (size_t ni : contactSurfaceNodes[i]) {
                if (!nodeUsedByBrain[ni]) continue;
                if (isDir[ni]) continue;
                floatingContactNodes[i].insert(ni);
            }
            if (!floatingContactNodes[i].empty()) {
                floatingMaster[i] = static_cast<int>(*floatingContactNodes[i].begin());
                for (size_t ni : floatingContactNodes[i]) {
                    floatingMasterOfNode[ni] = floatingMaster[i];
                }
            }
            qDebug() << "[FEM] Contact" << i
                     << "floating nodes:"
                     << floatingContactNodes[i].size()
                     << "master=" << floatingMaster[i]
                     << "polarity=" << m_spec.contactPolarity[i];
        }
    }
    m_diag.electrodeSurfaceNodesNoBrain = static_cast<int>(surfaceNodesNoBrain);
    qDebug() << "[FEM] 电极表面节点未被脑组织tet共享:"
             << surfaceNodesNoBrain << "/" << electrodeSurfaceNodes.size();
    for (const auto& kv : electrodeSurfaceNodesByLabel) {
        int noBrain = 0;
        for (size_t ni : kv.second) {
            if (!nodeUsedByBrain[ni]) noBrain++;
        }
        qDebug() << "[FEM]   surfaceNoBrain by label" << kv.first
                 << ":" << noBrain << "/" << kv.second.size();
    }
    for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
        qDebug() << "[FEM] Contact" << i
                 << "surface brain-share: all="
                 << m_diag.contactAllSurfaceNodes[i]
                 << "shared=" << m_diag.contactAllSurfaceBrainShared[i]
                 << "noBrain=" << m_diag.contactAllSurfaceNoBrain[i]
                 << "brainFacingRatio=" << QString::number(m_diag.contactBrainFacingRatio[i], 'f', 3)
                 << "| dirichletBC="
                 << m_diag.contactSurfaceNodes[i]
                 << "shared=" << m_diag.contactSurfaceBrainShared[i]
                 << "noBrain=" << m_diag.contactSurfaceNoBrain[i];
    }

    // 给"电极内部孤立节点"打 Dirichlet：
    //   - 在某触点 tet 内 → V = 该触点电位（如果有极性），否则 V=0
    //   - 在绝缘体 tet 内 → V=0（数值占位，物理上不影响表面外的电场）
    size_t numHollowedNodes = 0;
    size_t numHollowedSurfaceNodes = 0;
    for (size_t ci = 0; ci < Nc; ++ci) {
        int lab = mesh.tet_labels[ci];
        if (!isElectrodeLabel(lab)) continue;

        double Vfill = 0.0;
        int contactIdx = lab - dbs_fem::LABEL_CONTACT_BASE;
        if (contactIdx >= 0 && contactIdx < m_spec.numContacts) {
            Vfill = voltageForPolarity(m_spec.contactPolarity[contactIdx]);
        }
        for (int k = 0; k < 4; ++k) {
            size_t ni = mesh.tets[ci][k];
            if (!nodeUsedByBrain[ni] && !isDir[ni]) {
                isDir[ni] = true;
                Vdir[ni] = Vfill;
                numHollowedNodes++;
                if (electrodeSurfaceNodes.count(ni)) numHollowedSurfaceNodes++;
            }
        }
    }
    m_diag.hollowedSurfaceNodes = static_cast<int>(numHollowedSurfaceNodes);
    qDebug() << "[FEM] 挖空电极: 内部孤立节点强制 Dirichlet =" << numHollowedNodes;
    qDebug() << "[FEM] 挖空电极: 其中电极表面节点 =" << numHollowedSurfaceNodes;

    // 统计 + 验证摘要
    size_t numDir = 0;
    for (auto b : isDir) if (b) numDir++;
    double dirPct = 100.0 * numDir / Np;
    m_diag.dirNodes = static_cast<int>(numDir);
    m_diag.contactDirNodes = static_cast<int>(numContactDir);
    m_diag.boundaryDirNodes = static_cast<int>(numBoundaryDir);
    m_diag.dirPct = dirPct;
    qDebug() << "[FEM] Dirichlet 节点:" << numDir << "/" << Np
             << QString("(%1%)").arg(dirPct, 0, 'f', 1);
    qDebug() << "[FEM]   其中 触点:" << numContactDir << ", 外边界:" << numBoundaryDir;

    // ----- 2. 建立自由节点映射 -----
    size_t nFree = 0;
    std::vector<size_t> fMap(Np, SIZE_MAX);
    for (size_t i = 0; i < Np; ++i) {
        if (isDir[i]) continue;
        int master = floatingMasterOfNode[i];
        if (master >= 0 && static_cast<size_t>(master) != i) continue;
        fMap[i] = nFree++;
    }
    if (m_spec.useFloatingContacts) {
        for (size_t i = 0; i < Np; ++i) {
            int master = floatingMasterOfNode[i];
            if (master >= 0 && static_cast<size_t>(master) != i) {
                fMap[i] = fMap[static_cast<size_t>(master)];
            }
        }
    }
    if (nFree == 0) {
        qWarning() << "[FEM] 无自由节点";
        return false;
    }
    qDebug() << "[FEM] 自由节点:" << nFree;

    emit progressUpdated(30, QString("组装刚度矩阵 (%1 四面体)...").arg(Nc));

    // ----- 3. 组装刚度矩阵 (实数版, 参考 IRE FEMTools::getSparseMatrix) -----
    struct Triplet { arma::uword r, c; double v; };
    std::vector<Triplet> triplets;
    triplets.reserve(Nc * 16);
    arma::vec b_rhs(nFree, arma::fill::zeros);
    std::vector<bool> dirHasFreeCoupling(Np, false);

    for (size_t ci = 0; ci < Nc; ++ci) {
        int label = mesh.tet_labels[ci];

        // 【P2 方案 0】挖空电极: 跳过所有电极 tet (绝缘体 + 触点)
        // 电极内部不参与 FEM 求解，只在表面通过 Dirichlet/Neumann BC 影响脑组织
        if (isElectrodeLabel(label)) continue;

        const double* p0 = mesh.points[mesh.tets[ci][0]].data();
        const double* p1 = mesh.points[mesh.tets[ci][1]].data();
        const double* p2 = mesh.points[mesh.tets[ci][2]].data();
        const double* p3 = mesh.points[mesh.tets[ci][3]].data();

        dbs_fem::TetGrad tg;
        if (!dbs_fem::computeTetGrad(p0, p1, p2, p3, tg)) continue;

        double sigma = sigmaForLabel(label);

        // 局部刚度矩阵: K_local[i][j] = sigma * vol * dot(grad_i, grad_j)
        // 坐标单位: mm → 转换到 m: grad 需要 /1000, vol 需要 *1e-9
        // 但因为最终 E = -grad(phi) 的单位是 V/mm, 所以保持 mm 单位不转换
        // sigma 的单位 S/m = A/(V·m) 在 mm 体系下需要 sigma * 1e-3
        double factor = sigma * tg.vol * 1e-3;  // S/m * mm^3 * 1e-3 = S·mm^2·1e-3

        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                double kij = factor * (tg.g[i][0]*tg.g[j][0] + tg.g[i][1]*tg.g[j][1] + tg.g[i][2]*tg.g[j][2]);

                size_t ni = mesh.tets[ci][i];
                size_t nj = mesh.tets[ci][j];
                size_t niAsm = ni;
                size_t njAsm = nj;
                if (!isDir[ni] && floatingMasterOfNode[ni] >= 0) niAsm = static_cast<size_t>(floatingMasterOfNode[ni]);
                if (!isDir[nj] && floatingMasterOfNode[nj] >= 0) njAsm = static_cast<size_t>(floatingMasterOfNode[nj]);

                if (!isDir[niAsm]) {
                    if (!isDir[njAsm]) {
                        triplets.push_back({static_cast<arma::uword>(fMap[niAsm]),
                                           static_cast<arma::uword>(fMap[njAsm]),
                                           kij});
                    } else {
                        b_rhs(fMap[niAsm]) -= kij * Vdir[njAsm];
                        if (std::abs(kij) > 1e-30) {
                            dirHasFreeCoupling[njAsm] = true;
                        }
                    }
                }
            }
        }
    }
    for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
        int effective = 0;
        for (size_t ni : contactSurfaceNodes[i]) {
            if (dirHasFreeCoupling[ni]) effective++;
        }
        m_diag.contactSurfaceEffective[i] = effective;
        if (m_diag.contactAllSurfaceBrainShared[i] > 0) {
            m_diag.contactEffectiveRatio[i] =
                static_cast<double>(effective) / static_cast<double>(m_diag.contactAllSurfaceBrainShared[i]);
        }
        qDebug() << "[FEM] Contact" << i
                 << "effective contact Dirichlet BC nodes:"
                 << effective << "/" << contactSurfaceNodes[i].size()
                 << "(brain-shared=" << m_diag.contactSurfaceBrainShared[i]
                 << ", noBrain=" << m_diag.contactSurfaceNoBrain[i]
                 << ", effectiveRatio=" << QString::number(m_diag.contactEffectiveRatio[i], 'f', 3)
                 << ")";
    }

    emit progressUpdated(60, "求解线性方程组 (SuperLU)...");

    // ----- 4. 合并三元组并构建稀疏矩阵 -----
    // 排序后合并相同位置
    std::sort(triplets.begin(), triplets.end(), [](const Triplet& a, const Triplet& b) {
        return a.r < b.r || (a.r == b.r && a.c < b.c);
    });

    std::vector<arma::uword> rows, cols;
    std::vector<double> vals;
    if (!triplets.empty()) {
        rows.push_back(triplets[0].r);
        cols.push_back(triplets[0].c);
        vals.push_back(triplets[0].v);
        for (size_t i = 1; i < triplets.size(); ++i) {
            if (triplets[i].r == rows.back() && triplets[i].c == cols.back()) {
                vals.back() += triplets[i].v;
            } else {
                rows.push_back(triplets[i].r);
                cols.push_back(triplets[i].c);
                vals.push_back(triplets[i].v);
            }
        }
    }
    triplets.clear();

    arma::umat locs(2, rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        locs(0, i) = rows[i];
        locs(1, i) = cols[i];
    }
    arma::vec arma_vals = arma::conv_to<arma::vec>::from(vals);
    rows.clear(); cols.clear(); vals.clear();

    arma::sp_mat K(locs, arma_vals, nFree, nFree);
    locs.reset(); arma_vals.reset();

    qDebug() << "[FEM] 刚度矩阵: " << K.n_rows << "x" << K.n_cols
             << ", 非零:" << K.n_nonzero;

    // ----- 5. SuperLU 求解 -----
    arma::vec x;
    if (!arma::spsolve(x, K, b_rhs, "superlu")) {
        qWarning() << "[FEM] SuperLU 失败，尝试默认求解器...";
        if (!arma::spsolve(x, K, b_rhs)) {
            qWarning() << "[FEM] 所有求解器均失败";
            return false;
        }
    }

    emit progressUpdated(80, "计算电场...");

    // ----- 6. 还原电位 -----
    phi_out.resize(Np, 0.0);
    for (size_t i = 0; i < Np; ++i) {
        phi_out[i] = isDir[i] ? Vdir[i] : x(fMap[i]);
    }
    if (m_spec.useFloatingContacts) {
        for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
            double minPhi = std::numeric_limits<double>::max();
            double maxPhi = -std::numeric_limits<double>::max();
            for (size_t ni : floatingContactNodes[i]) {
                minPhi = std::min(minPhi, phi_out[ni]);
                maxPhi = std::max(maxPhi, phi_out[ni]);
            }
            if (!floatingContactNodes[i].empty()) {
                qDebug() << "[FEM] Contact" << i
                         << "floating phi:"
                         << "nodes=" << floatingContactNodes[i].size()
                         << "master=" << floatingMaster[i]
                         << "min=" << minPhi
                         << "max=" << maxPhi
                         << "span=" << (maxPhi - minPhi);
            }
        }
    }

    std::vector<int> contactFluxOwner(Np, -1);
    std::array<int, 4> fluxBrainSharedNodes = {{0, 0, 0, 0}};
    std::array<int, 4> fluxNoBrainSkippedNodes = {{0, 0, 0, 0}};
    for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
        for (size_t ni : contactSurfaceNodes[i]) {
            if (!nodeUsedByBrain[ni]) {
                fluxNoBrainSkippedNodes[i]++;
                continue;
            }
            contactFluxOwner[ni] = i;
            fluxBrainSharedNodes[i]++;
        }
    }
    std::array<int, 4> fluxContribTets = {{0, 0, 0, 0}};
    for (size_t ci = 0; ci < Nc; ++ci) {
        if (isElectrodeLabel(mesh.tet_labels[ci])) continue;
        const auto& tet = mesh.tets[ci];
        const double* p0 = mesh.points[tet[0]].data();
        const double* p1 = mesh.points[tet[1]].data();
        const double* p2 = mesh.points[tet[2]].data();
        const double* p3 = mesh.points[tet[3]].data();
        dbs_fem::TetGrad tgFlux;
        if (!dbs_fem::computeTetGrad(p0, p1, p2, p3, tgFlux)) continue;
        double sigma = sigmaForLabel(mesh.tet_labels[ci]);
        double factor = sigma * tgFlux.vol * 1e-3;
        bool counted[4] = {false, false, false, false};
        for (int a = 0; a < 4; ++a) {
            int owner = contactFluxOwner[tet[a]];
            if (owner < 0 || owner >= 4) continue;
            double currentA = 0.0;
            for (int b = 0; b < 4; ++b) {
                double kab = factor * (tgFlux.g[a][0]*tgFlux.g[b][0] +
                                       tgFlux.g[a][1]*tgFlux.g[b][1] +
                                       tgFlux.g[a][2]*tgFlux.g[b][2]);
                currentA += kab * phi_out[tet[b]];
            }
            m_diag.contactFluxMA[owner] += currentA * 1000.0;
            counted[owner] = true;
        }
        for (int i = 0; i < 4; ++i) {
            if (counted[i]) fluxContribTets[i]++;
        }
    }
    for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
        m_diag.contactFluxFaces[i] = fluxContribTets[i];
        m_diag.contactFluxMissingFaces[i] = 0;
        qDebug() << "[FEM] Contact" << i
                 << "flux residual estimate:"
                 << QString::number(m_diag.contactFluxMA[i], 'g', 8) << "mA"
                 << "brainSharedFluxNodes=" << fluxBrainSharedNodes[i]
                 << "noBrainSkippedNodes=" << fluxNoBrainSkippedNodes[i]
                 << "contribTets=" << fluxContribTets[i]
                 << "polarity=" << m_spec.contactPolarity[i];
    }

    // ----- 7. 计算电场幅值 (每个四面体) -----
    //   【P2 方案 0】电极 tet 已挖空，电场设为 0（不参与 VTA 等值面提取）
    E_mag_out.resize(Nc, 0.0);
    std::vector<double> brainE;
    std::vector<double> brainVol;
    std::vector<std::pair<double, size_t>> topECells;
    brainE.reserve(Nc);
    brainVol.reserve(Nc);
    topECells.reserve(Nc);
    double activeThreshold = dbs_fem::getVTAThreshold(m_spec.pulseWidth);
    double activeThresholdVolume = 0.0;
    int activeThresholdCells = 0;
    for (size_t ci = 0; ci < Nc; ++ci) {
        // 电极内部 tet 跳过（电场无意义）
        if (isElectrodeLabel(mesh.tet_labels[ci])) {
            E_mag_out[ci] = 0.0;
            continue;
        }

        const double* p0 = mesh.points[mesh.tets[ci][0]].data();
        const double* p1 = mesh.points[mesh.tets[ci][1]].data();
        const double* p2 = mesh.points[mesh.tets[ci][2]].data();
        const double* p3 = mesh.points[mesh.tets[ci][3]].data();

        dbs_fem::TetGrad tg;
        if (!dbs_fem::computeTetGrad(p0, p1, p2, p3, tg)) continue;

        // E = -grad(phi) = -sum(phi_i * grad_Ni)
        double Ex = 0, Ey = 0, Ez = 0;
        for (int k = 0; k < 4; ++k) {
            double v = phi_out[mesh.tets[ci][k]];
            Ex -= v * tg.g[k][0];
            Ey -= v * tg.g[k][1];
            Ez -= v * tg.g[k][2];
        }
        E_mag_out[ci] = std::sqrt(Ex*Ex + Ey*Ey + Ez*Ez);
        brainE.push_back(E_mag_out[ci]);
        brainVol.push_back(tg.vol);
        topECells.push_back({E_mag_out[ci], ci});
        if (E_mag_out[ci] >= activeThreshold) {
            activeThresholdCells++;
            activeThresholdVolume += tg.vol;
        }
    }

    auto maxIt = std::max_element(E_mag_out.begin(), E_mag_out.end());
    double maxE = *maxIt;
    size_t maxCi = static_cast<size_t>(std::distance(E_mag_out.begin(), maxIt));
    // 电场统计摘要 (排序副本取百分位)
    std::vector<double> Esorted(E_mag_out);
    std::sort(Esorted.begin(), Esorted.end());
    auto pct = [](const std::vector<double>& v, double q) -> double {
        if (v.empty()) return 0.0;
        size_t idx = static_cast<size_t>(q * static_cast<double>(v.size() - 1));
        return v[idx];
    };
    double p50 = pct(Esorted, 0.50);
    double p95 = pct(Esorted, 0.95);
    double p99 = pct(Esorted, 0.99);
    double p995 = pct(Esorted, 0.995);
    double p999 = pct(Esorted, 0.999);
    std::vector<double> volSorted = brainVol;
    std::sort(volSorted.begin(), volSorted.end());
    double tinyVolP1 = pct(volSorted, 0.01);
    double robustMaxE = 0.0;
    for (size_t i = 0; i < brainE.size(); ++i) {
        if (brainVol[i] >= tinyVolP1 && brainE[i] > robustMaxE) robustMaxE = brainE[i];
    }
    m_diag.maxE = maxE;
    m_diag.robustMaxE = robustMaxE;
    m_diag.p50 = p50;
    m_diag.p95 = p95;
    m_diag.p99 = p99;
    m_diag.p995 = p995;
    m_diag.p999 = p999;
    m_diag.tinyVolP1 = tinyVolP1;
    m_diag.activeThresholdCells = activeThresholdCells;
    m_diag.activeThresholdVolume = activeThresholdVolume;
    qDebug() << "[FEM] 电场范围: 0 ~" << maxE << "V/mm";
    qDebug() << "[FEM] 电场分布: p50=" << p50
             << " p95=" << p95
             << " p99=" << p99
             << " p99.5=" << p995
             << " p99.9=" << p999
             << " robustMax(p1体积过滤)=" << robustMaxE
             << "V/mm";
    qDebug() << "[FEM] tet体积 p1 =" << tinyVolP1
             << "mm^3, E>=threshold(" << activeThreshold
             << ") cells=" << activeThresholdCells
             << " volume=" << activeThresholdVolume << "mm^3";
    for (double thr : {1.0, 5.0, 10.0, 20.0}) {
        int cnt = 0;
        double vol = 0.0;
        for (size_t i = 0; i < brainE.size(); ++i) {
            if (brainE[i] >= thr) {
                cnt++;
                vol += brainVol[i];
            }
        }
        qDebug() << "[FEM] 高场体积 E>=" << thr << "V/mm: cells=" << cnt
                 << "volume=" << vol << "mm^3";
    }
    if (maxCi < Nc) {
        dbs_fem::TetGrad tgMax;
        const auto& tet = mesh.tets[maxCi];
        const double* p0 = mesh.points[tet[0]].data();
        const double* p1 = mesh.points[tet[1]].data();
        const double* p2 = mesh.points[tet[2]].data();
        const double* p3 = mesh.points[tet[3]].data();
        if (dbs_fem::computeTetGrad(p0, p1, p2, p3, tgMax)) {
            qDebug() << "[FEM] maxE cell:" << maxCi
                     << "label=" << mesh.tet_labels[maxCi]
                     << "vol=" << tgMax.vol;
            m_diag.maxELabel = mesh.tet_labels[maxCi];
            m_diag.maxEVol = tgMax.vol;
            qDebug() << "[FEM] maxE phi:"
                     << phi_out[tet[0]] << phi_out[tet[1]]
                     << phi_out[tet[2]] << phi_out[tet[3]];
        }
    }
    std::sort(topECells.begin(), topECells.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    double activeCenter[3] = {0.0, 0.0, 0.0};
    bool haveActiveCenter = false;
    if (m_diag.selectedContact >= 0 && m_diag.selectedContact < m_spec.numContacts) {
        double dir[3] = {
            m_spec.target[0] - m_spec.entry[0],
            m_spec.target[1] - m_spec.entry[1],
            m_spec.target[2] - m_spec.entry[2]
        };
        double len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        if (len > 1e-9) {
            dir[0] /= len; dir[1] /= len; dir[2] /= len;
            double tip[3] = {
                m_spec.target[0] + m_spec.depthOffset * dir[0],
                m_spec.target[1] + m_spec.depthOffset * dir[1],
                m_spec.target[2] + m_spec.depthOffset * dir[2]
            };
            double offset = m_spec.leadRadius + m_spec.contactLength * 0.5
                          + m_diag.selectedContact * (m_spec.contactLength + m_spec.contactSpacing);
            activeCenter[0] = tip[0] - offset * dir[0];
            activeCenter[1] = tip[1] - offset * dir[1];
            activeCenter[2] = tip[2] - offset * dir[2];
            haveActiveCenter = true;
        }
    }
    int topCount = std::min<int>(5, static_cast<int>(topECells.size()));
    for (int rank = 0; rank < topCount; ++rank) {
        size_t ci = topECells[rank].second;
        const auto& tet = mesh.tets[ci];
        dbs_fem::TetGrad tgTop;
        const double* p0 = mesh.points[tet[0]].data();
        const double* p1 = mesh.points[tet[1]].data();
        const double* p2 = mesh.points[tet[2]].data();
        const double* p3 = mesh.points[tet[3]].data();
        if (!dbs_fem::computeTetGrad(p0, p1, p2, p3, tgTop)) continue;
        double cx = 0.25 * (p0[0] + p1[0] + p2[0] + p3[0]);
        double cy = 0.25 * (p0[1] + p1[1] + p2[1] + p3[1]);
        double cz = 0.25 * (p0[2] + p1[2] + p2[2] + p3[2]);
        double dist = -1.0;
        if (haveActiveCenter) {
            double dx = cx - activeCenter[0], dy = cy - activeCenter[1], dz = cz - activeCenter[2];
            dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        }
        qDebug() << "[FEM] topE" << rank
                 << "cell=" << ci
                 << "E=" << topECells[rank].first
                 << "label=" << mesh.tet_labels[ci]
                 << "vol=" << tgTop.vol
                 << "center=" << cx << cy << cz
                 << "distToActiveContact=" << dist;
    }
    qDebug() << "[FEM] ===== 验证摘要 =====";
    qDebug() << "[FEM] Dirichlet 比例:" << QString("%1%").arg(100.0 * numDir / Np, 0, 'f', 1)
             << "| 电场 max:" << maxE << "V/mm"
             << "| robustMax:" << robustMaxE << "V/mm"
             << "| 重点看 surfaceNodes / p99.9 / maxE top cells";

    return true;

#else
    emit errorOccurred("Armadillo/SuperLU 未安装，无法求解。请安装后重新编译并定义 DBS_HAS_ARMADILLO。");
    return false;
#endif
}

// ============================================================
// Step 3: 构建 VTK 结果网格
// ============================================================
vtkSmartPointer<vtkUnstructuredGrid> DBSSimWorker::buildResultGrid(
    const dbs_fem::DBSMeshData& mesh,
    const std::vector<double>& phi,
    const std::vector<double>& E_mag)
{
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();

    // 节点
    vtkNew<vtkPoints> points;
    points->SetNumberOfPoints(static_cast<vtkIdType>(mesh.numPoints()));
    for (size_t i = 0; i < mesh.numPoints(); ++i) {
        points->SetPoint(static_cast<vtkIdType>(i),
                         mesh.points[i][0], mesh.points[i][1], mesh.points[i][2]);
    }
    grid->SetPoints(points);

    // 四面体单元
    for (size_t ci = 0; ci < mesh.numTets(); ++ci) {
        vtkNew<vtkTetra> tetra;
        for (int k = 0; k < 4; ++k) {
            tetra->GetPointIds()->SetId(k, static_cast<vtkIdType>(mesh.tets[ci][k]));
        }
        grid->InsertNextCell(tetra->GetCellType(), tetra->GetPointIds());
    }

    // 节点电位
    vtkNew<vtkDoubleArray> phiArr;
    phiArr->SetName("Potential_V");
    phiArr->SetNumberOfTuples(static_cast<vtkIdType>(mesh.numPoints()));
    for (size_t i = 0; i < mesh.numPoints(); ++i) {
        phiArr->SetValue(static_cast<vtkIdType>(i), phi[i]);
    }
    grid->GetPointData()->AddArray(phiArr);

    // 单元电场幅值
    vtkNew<vtkDoubleArray> eArr;
    eArr->SetName("E_mag_Vmm");
    eArr->SetNumberOfTuples(static_cast<vtkIdType>(mesh.numTets()));
    for (size_t ci = 0; ci < mesh.numTets(); ++ci) {
        eArr->SetValue(static_cast<vtkIdType>(ci), E_mag[ci]);
    }
    grid->GetCellData()->AddArray(eArr);
    grid->GetCellData()->SetActiveScalars("E_mag_Vmm");

    // 单元材料标签
    vtkNew<vtkIntArray> labelArr;
    labelArr->SetName("MaterialLabel");
    labelArr->SetNumberOfTuples(static_cast<vtkIdType>(mesh.numTets()));
    for (size_t ci = 0; ci < mesh.numTets(); ++ci) {
        labelArr->SetValue(static_cast<vtkIdType>(ci), mesh.tet_labels[ci]);
    }
    grid->GetCellData()->AddArray(labelArr);

    vtkNew<vtkDoubleArray> diagArr;
    diagArr->SetName("FEMDiagnostics");
    diagArr->SetNumberOfComponents(1);
    diagArr->SetNumberOfTuples(77);
    diagArr->SetValue(0, m_diag.numPoints);
    diagArr->SetValue(1, m_diag.numTets);
    diagArr->SetValue(2, m_diag.numTris);
    diagArr->SetValue(3, m_diag.selectedContact);
    diagArr->SetValue(4, m_diag.selectedVoltage);
    diagArr->SetValue(5, m_diag.amplitude);
    diagArr->SetValue(6, m_diag.pulseWidth);
    diagArr->SetValue(7, m_diag.dirNodes);
    diagArr->SetValue(8, m_diag.contactDirNodes);
    diagArr->SetValue(9, m_diag.boundaryDirNodes);
    diagArr->SetValue(10, m_diag.dirPct);
    diagArr->SetValue(11, m_diag.p50);
    diagArr->SetValue(12, m_diag.p95);
    diagArr->SetValue(13, m_diag.p99);
    diagArr->SetValue(14, m_diag.maxE);
    diagArr->SetValue(15, m_diag.maxELabel);
    diagArr->SetValue(16, m_diag.maxEVol);
    diagArr->SetValue(17, m_diag.p995);
    diagArr->SetValue(18, m_diag.p999);
    diagArr->SetValue(19, m_diag.robustMaxE);
    diagArr->SetValue(20, m_diag.tinyVolP1);
    diagArr->SetValue(21, m_diag.activeThresholdVolume);
    diagArr->SetValue(22, m_diag.activeThresholdCells);
    diagArr->SetValue(23, m_diag.electrodeSurfaceNodesNoBrain);
    diagArr->SetValue(24, m_diag.hollowedSurfaceNodes);
    for (int i = 0; i < 4; ++i) {
        diagArr->SetValue(25 + i, m_diag.contactSurfaceNodes[i]);
        diagArr->SetValue(29 + i, m_diag.contactVolumeNodes[i]);
        diagArr->SetValue(33 + i, m_diag.contactAllSurfaceNodes[i]);
        diagArr->SetValue(37 + i, m_diag.contactAllSurfaceBrainShared[i]);
        diagArr->SetValue(41 + i, m_diag.contactAllSurfaceNoBrain[i]);
        diagArr->SetValue(45 + i, m_diag.contactSurfaceBrainShared[i]);
        diagArr->SetValue(49 + i, m_diag.contactSurfaceNoBrain[i]);
        diagArr->SetValue(53 + i, m_diag.contactSurfaceEffective[i]);
        diagArr->SetValue(57 + i, m_diag.contactBrainFacingRatio[i]);
        diagArr->SetValue(61 + i, m_diag.contactEffectiveRatio[i]);
        diagArr->SetValue(65 + i, m_diag.contactFluxMA[i]);
        diagArr->SetValue(69 + i, m_diag.contactFluxFaces[i]);
        diagArr->SetValue(73 + i, m_diag.contactFluxMissingFaces[i]);
    }
    grid->GetFieldData()->AddArray(diagArr);

    qDebug() << "[FEM] VTK 结果: " << grid->GetNumberOfPoints() << "pts,"
             << grid->GetNumberOfCells() << "cells";

    return grid;
}
