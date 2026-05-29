#ifndef DBS_SIM_WORKER_H
#define DBS_SIM_WORKER_H
#pragma execution_character_set("utf-8")

#include <QObject>
#include <QString>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <array>

#include "dbs_fem_types.h"

Q_DECLARE_METATYPE(vtkSmartPointer<vtkUnstructuredGrid>)

// ============================================================
// DBS FEM 求解工作器
// 输入: .mesh 文件 + 刺激参数 → 输出: 含电场的 vtkUnstructuredGrid
// 在 QThread 中运行
// ============================================================

class DBSSimWorker : public QObject
{
    Q_OBJECT

public:
    explicit DBSSimWorker(QObject* parent = nullptr);

    void setSpec(const dbs_fem::DBSSimSpec& spec) { m_spec = spec; }
    void setMeshPath(const QString& path)          { m_meshPath = path; }

public slots:
    void process();

signals:
    void progressUpdated(int percent, const QString& message);
    void finished(vtkSmartPointer<vtkUnstructuredGrid> result);
    void errorOccurred(const QString& errorMsg);

private:
    struct FEMDiagnostics {
        int numPoints = 0;
        int numTets = 0;
        int numTris = 0;
        int selectedContact = -1;
        double selectedVoltage = 0.0;
        double amplitude = 0.0;
        int pulseWidth = 0;
        int dirNodes = 0;
        int contactDirNodes = 0;
        int boundaryDirNodes = 0;
        double dirPct = 0.0;
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
        double p995 = 0.0;
        double p999 = 0.0;
        double maxE = 0.0;
        double robustMaxE = 0.0;
        int maxELabel = 0;
        double maxEVol = 0.0;
        double tinyVolP1 = 0.0;
        double activeThresholdVolume = 0.0;
        int activeThresholdCells = 0;
        int electrodeSurfaceNodesNoBrain = 0;
        int hollowedSurfaceNodes = 0;
        std::array<int, 4> contactSurfaceNodes = {{0, 0, 0, 0}};
        std::array<int, 4> contactVolumeNodes = {{0, 0, 0, 0}};
        std::array<int, 4> contactAllSurfaceNodes = {{0, 0, 0, 0}};
        std::array<int, 4> contactAllSurfaceBrainShared = {{0, 0, 0, 0}};
        std::array<int, 4> contactAllSurfaceNoBrain = {{0, 0, 0, 0}};
        std::array<int, 4> contactSurfaceBrainShared = {{0, 0, 0, 0}};
        std::array<int, 4> contactSurfaceNoBrain = {{0, 0, 0, 0}};
        std::array<int, 4> contactSurfaceEffective = {{0, 0, 0, 0}};
        std::array<double, 4> contactBrainFacingRatio = {{0.0, 0.0, 0.0, 0.0}};
        std::array<double, 4> contactEffectiveRatio = {{0.0, 0.0, 0.0, 0.0}};
        std::array<double, 4> contactFluxMA = {{0.0, 0.0, 0.0, 0.0}};
        std::array<int, 4> contactFluxFaces = {{0, 0, 0, 0}};
        std::array<int, 4> contactFluxMissingFaces = {{0, 0, 0, 0}};
    };

    // Step 1: 读取 .mesh 文件
    bool readMeditMesh(const QString& path, dbs_fem::DBSMeshData& mesh);

    // Step 2: 组装刚度矩阵并求解
    bool solveElectricField(const dbs_fem::DBSMeshData& mesh,
                            std::vector<double>& phi,
                            std::vector<double>& E_mag);

    // Step 3: 构建 VTK 结果
    vtkSmartPointer<vtkUnstructuredGrid> buildResultGrid(
        const dbs_fem::DBSMeshData& mesh,
        const std::vector<double>& phi,
        const std::vector<double>& E_mag);

    dbs_fem::DBSSimSpec m_spec;
    QString m_meshPath;
    FEMDiagnostics m_diag;
};

#endif // DBS_SIM_WORKER_H
