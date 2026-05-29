#ifndef DBS_MESH_WORKER_H
#define DBS_MESH_WORKER_H
#pragma execution_character_set("utf-8")

#include <QObject>
#include <QString>
#include <vtkSmartPointer.h>
#include <vtkImageData.h>

#include "dbs_fem_types.h"
#include "dbs_electrode_stl.h"

// ============================================================
// DBS 网格化工作器
// 输入: 脑部影像 + 核团标签 + 电极 STL → 输出: .mesh 文件
// 在 QThread 中运行，通过信号报告进度
// ============================================================

class DBSMeshWorker : public QObject
{
    Q_OBJECT

public:
    explicit DBSMeshWorker(QObject* parent = nullptr);

    // 设置输入
    void setSpec(const dbs_fem::DBSSimSpec& spec)    { m_spec = spec; }
    void setBrainImage(vtkSmartPointer<vtkImageData> img) { m_brainImage = img; }
    void setLabelImage(vtkSmartPointer<vtkImageData> img) { m_labelImage = img; }
    void setOutputPath(const QString& path)           { m_outputMeshPath = path; }

public slots:
    void process();

signals:
    void progressUpdated(int percent, const QString& message);
    void finished(const QString& meshFilePath);
    void errorOccurred(const QString& errorMsg);

private:
    // Step 1: 以 target 为中心裁剪 ROI
    vtkSmartPointer<vtkImageData> extractROI();

    // Step 2: 构建合并标签图 (核团 + 脑组织 + 电极)
    vtkSmartPointer<vtkImageData> buildMergedLabelMap(vtkSmartPointer<vtkImageData> roiImage);

    // Step 3: 将 vtkPolyData 光栅化写入标签图
    void rasterizePolyToLabel(vtkSmartPointer<vtkImageData> labelMap,
                              vtkSmartPointer<vtkPolyData> poly,
                              int labelValue);

    // Step 4a: CGAL 网格化 — image-based (P0/P1/方案 0, 旧 pipeline)
    bool runCGALMeshing(vtkSmartPointer<vtkImageData> labelMap);

    // Step 4b: CGAL 网格化 — conforming (路线 C, 新 pipeline)
    //   把脑组织(+核团)从体素图提为闭合 STL，与电极 STL 合并送入
    //   Polyhedral_complex_mesh_domain_3，让 CGAL 在所有 polyhedron
    //   表面自动加密节点，电极内部声明为外部 (subdomain 0) 自动挖空。
    bool runConformingMeshing(vtkSmartPointer<vtkImageData> labelMap);

    dbs_fem::DBSSimSpec m_spec;
    vtkSmartPointer<vtkImageData> m_brainImage;
    vtkSmartPointer<vtkImageData> m_labelImage;
    QString m_outputMeshPath;
};

#endif // DBS_MESH_WORKER_H
