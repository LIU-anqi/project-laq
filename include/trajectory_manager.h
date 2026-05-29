#pragma once
#ifndef TRAJECTORY_MANAGER_H
#define TRAJECTORY_MANAGER_H
#pragma execution_character_set("utf-8")

#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkResliceImageViewer.h>
#include <vtkCutter.h>
#include <vtkSphereSource.h>
#include <vtkMatrix4x4.h>
#include "dbs_lead_model.h"

class TrajectoryManager {
public:
    TrajectoryManager();
    ~TrajectoryManager();

    //核心数据状态
    bool hasTarget = false;
    bool hasEntry = false;
    double targetPos[3] = { 0.0, 0.0, 0.0 };
    double entryPos[3] = { 0.0, 0.0, 0.0 };

    // =============================================
    // AC-PC 坐标系标定数据
    // =============================================
    bool hasAC  = false;
    bool hasPC  = false;
    bool hasMSP = false;
    double acPos[3]  = { 0.0, 0.0, 0.0 };
    double pcPos[3]  = { 0.0, 0.0, 0.0 };
    double mspPos[3] = { 0.0, 0.0, 0.0 };

    // 标记球 Actor（在 3D 视图中可视化三个解剖点）
    vtkSmartPointer<vtkActor> actorACSphere;
    vtkSmartPointer<vtkActor> actorPCSphere;
    vtkSmartPointer<vtkActor> actorMSPSphere;

    // 2D 十字 Actor（每个解剖点在三个正交视图各一个）
    vtkSmartPointer<vtkActor> acCrossArr[3];
    vtkSmartPointer<vtkActor> pcCrossArr[3];
    vtkSmartPointer<vtkActor> mspCrossArr[3];

    // 变换矩阵（当三点齐备后由 computeAcPcMatrices 计算）
    vtkSmartPointer<vtkMatrix4x4> m_worldToAcPcMatrix;
    vtkSmartPointer<vtkMatrix4x4> m_acPcToWorldMatrix;
    bool m_acpcValid = false;

    // 计算两个 4x4 变换矩阵（三点必须全部就绪，否则返回 false）
    // 数学公式：
    //   Origin = (AC + PC) / 2
    //   Y轴 (Anterior) = Normalize(AC - PC)
    //   临时Z = Normalize(MSP - Origin)
    //   X轴 (Right)    = Normalize(Cross(Y, 临时Z))
    //   Z轴 (Superior) = Normalize(Cross(X, Y))
    bool computeAcPcMatrices();

    // 世界坐标 → AC-PC 坐标（返回 false 表示矩阵无效）
    bool worldToAcPc(const double worldIn[3], double acpcOut[3]) const;

    // AC-PC 坐标 → 世界坐标
    bool acpcToWorld(const double acpcIn[3], double worldOut[3]) const;

    // 判断 AC-PC 矩阵是否已就绪
    bool isAcPcValid() const { return m_acpcValid; }

    // 重置所有 AC-PC 标定数据（不含渲染 Actor 的移除，由外部负责）
    void resetAcPc();

    // 3D 渲染组件
    vtkSmartPointer<vtkActor> actorTargetSphere;
    vtkSmartPointer<vtkActor> actorEntrySphere;
    vtkSmartPointer<vtkActor> targetCrossArr[3];
    vtkSmartPointer<vtkActor> entryCrossArr[3];

    // 手术针渲染组件 (1 个放 3D 窗口，3 个放 2D 窗口)
    //vtkSmartPointer<vtkActor> actorNeedle;
    ////vtkSmartPointer<vtkActor> actorNeedle2D[3];           // 2D视图里的实体针
    //vtkSmartPointer<vtkActor> actorNeedle2D_Above[3];       // 外部实心针
    //vtkSmartPointer<vtkActor> actorNeedle2D_Below[3];       // 内部半透明针
   
    // 替换为全新的数字孪生电极：
    DBSLeadModel* m_dbsLead = nullptr;
    bool m_isLeadAddedToScene = false; // 防止重复AddActor

    // 【替换掉原来的 CrossSection 和 Cutter，改为交点指示器】
    //vtkSmartPointer<vtkSphereSource> intersectionSphere[3]; // 用来生成永远完美的圆
    //vtkSmartPointer<vtkActor> actorIntersectionDot[3];      // 交点小圆点 Actor

    // 核心功能函数
    vtkSmartPointer<vtkActor> createCrossActor(double* pos, double* color);

    // 生成包含头尾圆球的焊接手术针，并加入渲染器
    void updateNeedleActor(vtkRenderer* ren3d, vtkResliceImageViewer* viewers[3]);

    // 彻底清理并移除所有手术针
    void clearNeedleActors(vtkRenderer* ren3d, vtkResliceImageViewer* viewers[3]);

    // 【新增】计算轨迹对齐的三个 4x4 变换矩阵
    // center: 切面中心点 (通常传靶点)
    // matProbe: 探针视角矩阵 (垂直于针)
    // matInline1: 纵切视角1 (平行于针)
    // matInline2: 纵切视角2 (平行于针，与1垂直)
    bool getTrajectoryMatrices(double* center, vtkMatrix4x4* matProbe, vtkMatrix4x4* matInline1, vtkMatrix4x4* matInline2);
};

#endif // TRAJECTORY_MANAGER_H