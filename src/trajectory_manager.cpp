#include "trajectory_manager.h"

#include <vtkCursor3D.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkLineSource.h>
#include <vtkTubeFilter.h>
#include <vtkSphereSource.h>
#include <vtkAppendPolyData.h>
#include <vtkMath.h>
#include <vtkMatrix4x4.h>

#include <vtkArrowSource.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>

TrajectoryManager::TrajectoryManager()
{
    // 在构造函数直接初始化电极
    m_dbsLead = new DBSLeadModel();
}

TrajectoryManager::~TrajectoryManager()
{
    if (m_dbsLead) delete m_dbsLead;
}

vtkSmartPointer<vtkActor> TrajectoryManager::createCrossActor(double* pos, double* color)
{
    vtkNew<vtkCursor3D> cursorSource;
    double s = 1.0; // 大小 2mm
    cursorSource->SetModelBounds(pos[0] - s, pos[0] + s, pos[1] - s, pos[1] + s, pos[2] - s, pos[2] + s);
    cursorSource->SetFocalPoint(pos);
    cursorSource->AllOff();
    cursorSource->AxesOn();

    vtkNew<vtkPolyDataMapper> cursorMapper;
    cursorMapper->SetInputConnection(cursorSource->GetOutputPort());
    cursorMapper->SetResolveCoincidentTopologyToPolygonOffset();

    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(cursorMapper);
    actor->GetProperty()->SetColor(color);
    actor->GetProperty()->SetLineWidth(3.0);
    actor->GetProperty()->LightingOff();
    actor->GetProperty()->SetOpacity(1.0);
    actor->SetVisibility(false); // 默认隐藏

    return actor;
}

void TrajectoryManager::updateNeedleActor(vtkRenderer* ren3d, vtkResliceImageViewer* viewers[3])
{
    if (!hasEntry || !hasTarget) return;

    // 第一次生成时，把所有的 Assemblies 加载到场景中
    if (!m_isLeadAddedToScene) {
        if (ren3d) ren3d->AddActor(m_dbsLead->GetAssembly3D());
        for (int i = 0; i < 3; i++) {
            if (viewers[i]) {
                viewers[i]->GetRenderer()->AddActor(m_dbsLead->GetAssembly2DAbove(i));
                viewers[i]->GetRenderer()->AddActor(m_dbsLead->GetAssembly2DBelow(i));
            }
        }
        m_isLeadAddedToScene = true;
    }

    // 自动对齐并显示
    m_dbsLead->UpdateTrajectory(entryPos, targetPos);
    m_dbsLead->SetVisibility(true);
}

void TrajectoryManager::clearNeedleActors(vtkRenderer* ren3d, vtkResliceImageViewer* viewers[3])
{
    // 我们不再暴力 RemoveActor，而是优雅地隐藏，随时待命
    if (m_dbsLead) {
        m_dbsLead->SetVisibility(false);
    }
}

// =====================================================================
// AC-PC 坐标系实现
// =====================================================================

bool TrajectoryManager::computeAcPcMatrices()
{
    if (!hasAC || !hasPC || !hasMSP) {
        m_acpcValid = false;
        return false;
    }

    // 1. 计算原点：AC 与 PC 的中点
    double origin[3] = {
        (acPos[0] + pcPos[0]) * 0.5,
        (acPos[1] + pcPos[1]) * 0.5,
        (acPos[2] + pcPos[2]) * 0.5
    };

    // 2. Y 轴（Anterior）= Normalize(AC - PC)
    double yAxis[3] = {
        acPos[0] - pcPos[0],
        acPos[1] - pcPos[1],
        acPos[2] - pcPos[2]
    };
    vtkMath::Normalize(yAxis);

    // 3. 临时 Z 轴 = Normalize(MSP - Origin)
    double tempZ[3] = {
        mspPos[0] - origin[0],
        mspPos[1] - origin[1],
        mspPos[2] - origin[2]
    };
    vtkMath::Normalize(tempZ);

    // 4. X 轴（Right）= Normalize(Cross(Y, 临时Z))
    double xAxis[3];
    vtkMath::Cross(yAxis, tempZ, xAxis);
    vtkMath::Normalize(xAxis);

    // 5. 真实 Z 轴（Superior）= Normalize(Cross(X, Y))
    double zAxis[3];
    vtkMath::Cross(xAxis, yAxis, zAxis);
    vtkMath::Normalize(zAxis);

    // 6. 组装 AcPcToWorld 矩阵 (列向量 = 轴方向, 最后一列 = 原点)
    //    形式: M = [X | Y | Z | Origin; 0 0 0 1]
    if (!m_acPcToWorldMatrix) {
        m_acPcToWorldMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    }
    m_acPcToWorldMatrix->Identity();
    for (int i = 0; i < 3; i++) {
        m_acPcToWorldMatrix->SetElement(i, 0, xAxis[i]);
        m_acPcToWorldMatrix->SetElement(i, 1, yAxis[i]);
        m_acPcToWorldMatrix->SetElement(i, 2, zAxis[i]);
        m_acPcToWorldMatrix->SetElement(i, 3, origin[i]);
    }

    // 7. 组装 WorldToAcPc 矩阵 = 旋转部分的转置 + 平移修正
    //    R^T * (-origin) = 平移分量
    if (!m_worldToAcPcMatrix) {
        m_worldToAcPcMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    }
    m_worldToAcPcMatrix->Identity();
    // 旋转行：各轴分量直接填入行
    for (int i = 0; i < 3; i++) {
        m_worldToAcPcMatrix->SetElement(0, i, xAxis[i]);
        m_worldToAcPcMatrix->SetElement(1, i, yAxis[i]);
        m_worldToAcPcMatrix->SetElement(2, i, zAxis[i]);
    }
    // 平移列：R^T * (-origin)
    double tx = -(xAxis[0]*origin[0] + xAxis[1]*origin[1] + xAxis[2]*origin[2]);
    double ty = -(yAxis[0]*origin[0] + yAxis[1]*origin[1] + yAxis[2]*origin[2]);
    double tz = -(zAxis[0]*origin[0] + zAxis[1]*origin[1] + zAxis[2]*origin[2]);
    m_worldToAcPcMatrix->SetElement(0, 3, tx);
    m_worldToAcPcMatrix->SetElement(1, 3, ty);
    m_worldToAcPcMatrix->SetElement(2, 3, tz);

    m_acpcValid = true;
    return true;
}

bool TrajectoryManager::worldToAcPc(const double worldIn[3], double acpcOut[3]) const
{
    if (!m_acpcValid || !m_worldToAcPcMatrix) return false;

    double in4[4]  = { worldIn[0], worldIn[1], worldIn[2], 1.0 };
    double out4[4] = { 0.0, 0.0, 0.0, 0.0 };
    m_worldToAcPcMatrix->MultiplyPoint(in4, out4);
    acpcOut[0] = out4[0];
    acpcOut[1] = out4[1];
    acpcOut[2] = out4[2];
    return true;
}

bool TrajectoryManager::acpcToWorld(const double acpcIn[3], double worldOut[3]) const
{
    if (!m_acpcValid || !m_acPcToWorldMatrix) return false;

    double in4[4]  = { acpcIn[0], acpcIn[1], acpcIn[2], 1.0 };
    double out4[4] = { 0.0, 0.0, 0.0, 0.0 };
    m_acPcToWorldMatrix->MultiplyPoint(in4, out4);
    worldOut[0] = out4[0];
    worldOut[1] = out4[1];
    worldOut[2] = out4[2];
    return true;
}

void TrajectoryManager::resetAcPc()
{
    hasAC  = false;
    hasPC  = false;
    hasMSP = false;
    acPos[0]  = acPos[1]  = acPos[2]  = 0.0;
    pcPos[0]  = pcPos[1]  = pcPos[2]  = 0.0;
    mspPos[0] = mspPos[1] = mspPos[2] = 0.0;
    m_acpcValid = false;
    // 矩阵对象保留，不销毁，下次直接覆盖
}

bool TrajectoryManager::getTrajectoryMatrices(double* center, vtkMatrix4x4* matProbe, vtkMatrix4x4* matInline1, vtkMatrix4x4* matInline2)
{
    if (!hasEntry || !hasTarget) return false;

    // 1. 计算针的方向向量
    double dir[3] = { targetPos[0] - entryPos[0], targetPos[1] - entryPos[1], targetPos[2] - entryPos[2] };
    vtkMath::Normalize(dir);

    // 2. 寻找另外两个互相垂直的轴
    double up[3] = { 0.0, 0.0, 1.0 };
    if (std::abs(vtkMath::Dot(dir, up)) > 0.99) {
        up[0] = 0.0; up[1] = 1.0; up[2] = 0.0;
    }

    double xAxis[3], yAxis[3];
    vtkMath::Cross(up, dir, xAxis);
    vtkMath::Normalize(xAxis);
    vtkMath::Cross(dir, xAxis, yAxis);
    vtkMath::Normalize(yAxis);

    // 3. 组装 Probe's Eye (探针视角) 
    matProbe->Identity();
    for (int i = 0; i < 3; i++) {
        matProbe->SetElement(i, 0, xAxis[i]);
        matProbe->SetElement(i, 1, yAxis[i]);
        matProbe->SetElement(i, 2, dir[i]);
        matProbe->SetElement(i, 3, center[i]);
    }

    // 4. 组装 Inline 1 (纵切视角 1) 
    double negDir[3] = { -dir[0], -dir[1], -dir[2] };
    matInline1->Identity();
    for (int i = 0; i < 3; i++) {
        matInline1->SetElement(i, 0, xAxis[i]);
        matInline1->SetElement(i, 1, negDir[i]);
        matInline1->SetElement(i, 2, yAxis[i]);
        matInline1->SetElement(i, 3, center[i]);
    }

    // 5. 组装 Inline 2 (纵切视角 2) 
    double negXAxis[3] = { -xAxis[0], -xAxis[1], -xAxis[2] };
    matInline2->Identity();
    for (int i = 0; i < 3; i++) {
        matInline2->SetElement(i, 0, yAxis[i]);
        matInline2->SetElement(i, 1, negDir[i]);
        matInline2->SetElement(i, 2, negXAxis[i]);
        matInline2->SetElement(i, 3, center[i]);
    }

    return true;
}