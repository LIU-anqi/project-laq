#include "dbs_lead_model.h"
#include <vtkProperty.h>
#include <vtkTransform.h>
#include <vtkMatrix4x4.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkCleanPolyData.h>
#include <vtkTriangleFilter.h>
#include <vtkWindowedSincPolyDataFilter.h>
#include <vtkImageData.h>
#include <vtkImplicitPolyDataDistance.h>
#include <vtkContourFilter.h>
#include <algorithm>
#include <cmath>
#include <vtkProp3DCollection.h>
#include <QDebug>

DBSLeadModel::DBSLeadModel()
{
    BuildGeometry();
}

DBSLeadModel::~DBSLeadModel() {}

void DBSLeadModel::BuildGeometry()
{
    // ================================================================
    // 1. 动态轴杆 (最后一个触点上方的绝缘管，长度随路径动态变化)
    //    【核心改进】：不再用一整根长管覆盖触点区域，
    //    而是只覆盖触点上方到进针点的部分，彻底消除 Z-fighting
    // ================================================================
    m_shaftSource = vtkSmartPointer<vtkCylinderSource>::New();
    m_shaftSource->SetRadius(m_leadRadius - 0.1);  // 绝缘管略细于触点
    m_shaftSource->SetHeight(100.0);               // 初始占位值，UpdateTrajectory 会更新
    m_shaftSource->SetResolution(200);

    m_shaftNormals = vtkSmartPointer<vtkPolyDataNormals>::New();
    m_shaftNormals->SetInputConnection(m_shaftSource->GetOutputPort());
    m_shaftNormals->SetFeatureAngle(60.0);
    m_shaftNormals->ComputePointNormalsOn();
    m_shaftNormals->Update();

    // ================================================================
    // 2. 动态内芯 (贯穿全长的深色金属丝)
    // ================================================================
    m_innerCoreSource = vtkSmartPointer<vtkCylinderSource>::New();
    m_innerCoreSource->SetRadius(m_leadRadius * 0.3);
    m_innerCoreSource->SetHeight(100.0);
    m_innerCoreSource->SetResolution(100);

    m_innerCoreNormals = vtkSmartPointer<vtkPolyDataNormals>::New();
    m_innerCoreNormals->SetInputConnection(m_innerCoreSource->GetOutputPort());
    m_innerCoreNormals->SetFeatureAngle(60.0);
    m_innerCoreNormals->ComputePointNormalsOn();
    m_innerCoreNormals->Update();

    // ================================================================
    // 3. 静态触点环 (铂铱合金，4个，形状固定)
    // ================================================================
    m_contactSource = vtkSmartPointer<vtkCylinderSource>::New();
    m_contactSource->SetRadius(m_leadRadius);         // 触点是最外层，全径
    m_contactSource->SetHeight(m_contactLength);
    m_contactSource->SetResolution(250);              // 高精度保证高光细腻

    m_contactNormals = vtkSmartPointer<vtkPolyDataNormals>::New();
    m_contactNormals->SetInputConnection(m_contactSource->GetOutputPort());
    m_contactNormals->SetFeatureAngle(60.0);
    m_contactNormals->ComputePointNormalsOn();
    m_contactNormals->Update();
    m_contactPort = m_contactNormals->GetOutputPort();

    // ================================================================
    // 4. 静态绝缘间隔段 (触点之间的短管，高度 = m_contactSpacing)
    //    【核心新增】：每两个相邻触点之间放一小段绝缘管，
    //    与触点完全不重叠，彻底杜绝 Z-fighting
    // ================================================================
    m_gapSource = vtkSmartPointer<vtkCylinderSource>::New();
    m_gapSource->SetRadius(m_leadRadius - 0.1);       // 与轴杆同径
    m_gapSource->SetHeight(m_contactSpacing);          // 精确等于触点间距 0.5mm
    m_gapSource->SetResolution(200);

    m_gapNormals = vtkSmartPointer<vtkPolyDataNormals>::New();
    m_gapNormals->SetInputConnection(m_gapSource->GetOutputPort());
    m_gapNormals->SetFeatureAngle(60.0);
    m_gapNormals->ComputePointNormalsOn();
    m_gapNormals->Update();
    m_gapPort = m_gapNormals->GetOutputPort();

    // ================================================================
    // 5. 半球形子弹头针尖
    //    【核心改进】：
    //    a) 从完整球体改为半球体，消除球体上半部嵌入管体导致的接缝
    //    b) 半径设为触点径(m_leadRadius)，几何上与 Contact 0 无缝衔接
    //    c) 颜色使用绝缘体色(partType=0)，视觉上与触点明确区分
    //    d) VTK 球默认 Z 轴为极方向，RotateX(90) 后北半球朝下 = 子弹头
    // ================================================================
    m_tipSource = vtkSmartPointer<vtkSphereSource>::New();
    m_tipSource->SetRadius(m_leadRadius);              // 与触点同径，几何无缝
    m_tipSource->SetThetaResolution(100);
    m_tipSource->SetPhiResolution(100);
    m_tipSource->SetStartPhi(0);                       // 从北极 (Phi=0)
    m_tipSource->SetEndPhi(90);                        // 到赤道 (Phi=90) = 北半球
    // RotateX(90) 后：北半球(z>0)变为(y<0)，即朝下的子弹头封盖

    m_tipNormals = vtkSmartPointer<vtkPolyDataNormals>::New();
    m_tipNormals->SetInputConnection(m_tipSource->GetOutputPort());
    m_tipNormals->SetFeatureAngle(60.0);
    m_tipNormals->ComputePointNormalsOn();
    m_tipNormals->Update();
    m_tipPort = m_tipNormals->GetOutputPort();

    // ================================================================
    // 6. VTA 放电球 (Volume of Tissue Activated)
    //    用球体近似电流扩散的组织激活体积
    //    半径由 Amplitude 和 PulseWidth 动态驱动
    // ================================================================
    m_vtaSource = vtkSmartPointer<vtkSphereSource>::New();
    m_vtaSource->SetRadius(0.001);           // 初始极小，setAmplitude 会实时更新
    m_vtaSource->SetThetaResolution(60);     // 经纬线精度
    m_vtaSource->SetPhiResolution(60);

    m_vtaNormals = vtkSmartPointer<vtkPolyDataNormals>::New();
    m_vtaNormals->SetInputConnection(m_vtaSource->GetOutputPort());
    m_vtaNormals->SetFeatureAngle(60.0);
    m_vtaNormals->ComputePointNormalsOn();
    m_vtaNormals->Update();

    // ================================================================
    // 构建 7 套装配体 (1个3D + 3个2D_Above + 3个2D_Below)
    // ================================================================
    m_assembly3D = vtkSmartPointer<vtkAssembly>::New();
    BuildAssembly(m_assembly3D, nullptr, 0);

    for (int i = 0; i < 3; i++) {
        m_assembly2DAbove[i] = vtkSmartPointer<vtkAssembly>::New();
        m_assembly2DBelow[i] = vtkSmartPointer<vtkAssembly>::New();
        BuildAssembly(m_assembly2DAbove[i], &m_mappersAbove[i], 1);
        BuildAssembly(m_assembly2DBelow[i], &m_mappersBelow[i], 2);
    }
    applyVTADisplayPreset();
    SetVisibility(false);
}

void DBSLeadModel::BuildAssembly(vtkSmartPointer<vtkAssembly> assembly,
    std::vector<vtkSmartPointer<vtkPolyDataMapper>>* mapperList,
    int renderType)
{
    // ==========================================
    // 核心视觉配方：波士顿科学 (Boston Scientific) 极简写实风格
    // partType: 0=绝缘体(外管/间隔/针尖), 1=触点(银色金属), 2=内芯(暗黑)
    // ==========================================
    auto ApplyMaterial = [](vtkProperty* prop, int type, int partType) {

        // 1. 基础颜色分配
        if (partType == 0)       prop->SetColor(0.88, 0.88, 0.88);   // 绝缘体+针尖：医用级浅灰
        else if (partType == 1)  prop->SetColor(0.92, 0.95, 1.0);    // 触点：铂铱合金亮银(微冷蓝)
        else if (partType == 2)  prop->SetColor(0.15, 0.15, 0.15);   // 内芯：深空灰

        prop->SetInterpolationToPhong();

        // 2. 材质与光照分配
        if (type == 0) { // --- 3D 视图 ---
            if (partType == 0) {
                // 绝缘体：不透明实体，哑光医用硅胶质感
                prop->SetOpacity(1.0);
                prop->SetAmbient(0.3);
                prop->SetDiffuse(0.7);
                prop->SetSpecular(0.15);
                prop->SetSpecularPower(15);
            }
            else if (partType == 1) {
                // 金属触点：极致锐利的冰冷金属高光
                prop->SetOpacity(1.0);
                prop->SetAmbient(0.2);
                prop->SetDiffuse(0.3);
                prop->SetSpecular(1.0);
                prop->SetSpecularPower(120);
                prop->SetSpecularColor(0.95, 0.98, 1.0); // 高光偏冷蓝
            }
            else if (partType == 2) {
                // 内芯：仅在截面切断时可见的深色实心
                prop->SetOpacity(1.0);
                prop->SetAmbient(0.5);
                prop->SetDiffuse(0.5);
                prop->SetSpecular(0.0);
            }
        }
        else if (type == 1) { // --- 2D 视图 Above (切面前方) ---
            prop->SetOpacity(1.0);
            if (partType == 0) {
                prop->SetAmbient(0.4);
                prop->SetDiffuse(0.6);
                prop->SetSpecular(0.1);
            }
            else {
                prop->SetAmbient(0.3);
                prop->SetDiffuse(0.5);
                prop->SetSpecular(0.4);
                prop->SetSpecularPower(30);
            }
        }
        else if (type == 2) { // --- 2D 视图 Below (切面后方，半透明埋入感) ---
            if (partType == 0) prop->SetOpacity(0.15);
            else prop->SetOpacity(0.25);
            prop->SetAmbient(0.1);
            prop->SetDiffuse(0.2);
            prop->SetSpecular(0.0);
            // 【方案E】半透明物体关闭背面剔除：
            // 使远侧管壁也可见，呈现完整的"管状"半透明轮廓
            prop->SetBackfaceCulling(0);
        }
    };

    auto ApplyOffset = [](vtkPolyDataMapper* mapper) {
        mapper->SetResolveCoincidentTopologyToPolygonOffset();
        mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0, -4000);
        mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(0, -4000);
    };

    double stepY = m_contactLength + m_contactSpacing;

    // ==========================================
    // Part A: 轴杆 (最后一个触点上方的动态绝缘管)
    // ==========================================
    {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(m_shaftNormals->GetOutputPort());
        if (renderType == 2) ApplyOffset(mapper);
        if (mapperList) mapperList->push_back(mapper);

        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        ApplyMaterial(actor->GetProperty(), renderType, 0);
        actor->GetProperty()->SetLighting(true);

        m_shaftActors.push_back(actor);
        assembly->AddPart(actor);
    }

    // ==========================================
    // Part B: 绝缘间隔段 (触点之间的短管，共 numContacts-1 段)
    //         与触点零重叠，彻底杜绝 Z-fighting
    // ==========================================
    for (int i = 0; i < m_numContacts - 1; i++) {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(m_gapPort);
        if (renderType == 2) ApplyOffset(mapper);
        if (mapperList) mapperList->push_back(mapper);

        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        ApplyMaterial(actor->GetProperty(), renderType, 0);
        actor->GetProperty()->SetLighting(true);

        // 间隔段中心 = 第i个触点顶部 + 间距/2
        double gapCenterY = m_contactLength + i * stepY + m_contactSpacing / 2.0;
        vtkNew<vtkTransform> trans;
        trans->Translate(0.0, gapCenterY, 0.0);
        actor->SetUserTransform(trans);

        assembly->AddPart(actor);
    }

    // ==========================================
    // Part C: 内芯 (贯穿全长的深色金属丝)
    // ==========================================
    {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(m_innerCoreNormals->GetOutputPort());
        if (renderType == 2) ApplyOffset(mapper);
        if (mapperList) mapperList->push_back(mapper);

        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        ApplyMaterial(actor->GetProperty(), renderType, 2);
        actor->GetProperty()->SetLighting(true);

        m_innerCoreActors.push_back(actor);
        assembly->AddPart(actor);
    }

    // ==========================================
    // Part D: 4 个金属触点环 (铂铱合金银色)
    // ==========================================
    for (int i = 0; i < m_numContacts; i++) {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(m_contactPort);
        if (renderType == 2) ApplyOffset(mapper);
        if (mapperList) mapperList->push_back(mapper);

        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        ApplyMaterial(actor->GetProperty(), renderType, 1);
        actor->GetProperty()->SetLighting(true);

        vtkNew<vtkTransform> trans;
        trans->Translate(0.0, m_contactLength / 2.0 + i * stepY, 0.0);
        actor->SetUserTransform(trans);

        // 【Sprint 1】收集触点 Actor 引用，供极性变色使用
        m_contactActors[i].push_back(actor);

        assembly->AddPart(actor);
    }

    // ==========================================
    // Part E: 半球形子弹头针尖
    //   颜色 = 绝缘体色 (partType=0)，与触点形成明确色差
    //   几何 = 与触点同径，无缝衔接 Contact 0 的底部
    // ==========================================
    {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(m_tipPort);
        if (renderType == 2) ApplyOffset(mapper);
        if (mapperList) mapperList->push_back(mapper);

        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        // 【修正】：针尖使用绝缘体材质 (partType=0)，
        //            与触点的银色金属形成视觉区分
        ApplyMaterial(actor->GetProperty(), renderType, 0);
        actor->GetProperty()->SetLighting(true);

        // RotateX(90)：把 VTK 球体默认的 Z 轴极方向旋转到 Y 轴
        // 北半球(z>0)旋转后变为(y<0)，即朝下的子弹头封盖
        vtkNew<vtkTransform> trans;
        trans->RotateX(90);
        actor->SetUserTransform(trans);

        assembly->AddPart(actor);
    }

    // ==========================================
    // Part F: VTA 放电球 (Sprint 2)
    //   半透明绿色球体，可视化电流扩散范围
    //   默认隐藏，setShowVTA(true) + amplitude>0 时显示
    // ==========================================
    {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(m_vtaNormals->GetOutputPort());
        mapper->ScalarVisibilityOff();
        if (renderType == 2) ApplyOffset(mapper);
        if (mapperList) mapperList->push_back(mapper);

        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);

        applyVTAMaterial(actor->GetProperty(), renderType);

        actor->SetVisibility(0);  // 默认隐藏
        m_vtaActors.push_back(actor);
        assembly->AddPart(actor);
    }
}


void DBSLeadModel::UpdateTrajectory(const double entry[3], const double target[3])
{
    // Sprint 3: 存储原始轨迹，供深度偏移重算使用
    for (int i = 0; i < 3; i++) {
        m_originalEntry[i] = entry[i];
        m_originalTarget[i] = target[i];
    }
    m_hasTrajectory = true;

    // 计算方向向量 (target → entry)
    double dir[3] = { entry[0] - target[0], entry[1] - target[1], entry[2] - target[2] };
    double distance = vtkMath::Norm(dir);
    if (distance < 1e-6) return;
    vtkMath::Normalize(dir);

    // Sprint 3: 应用深度偏移 (+ 深入, - 回退)
    //   【修正】：整根电极刚体平移，不改变电极长度！
    //   有效靶点 = 原始靶点 - dir * offset (沿轨迹方向平移)
    //   距离保持不变！（电极不被拉伸/缩短）
    double effectiveTarget[3];
    for (int i = 0; i < 3; i++) {
        effectiveTarget[i] = target[i] - dir[i] * m_depthOffset;
    }
    // 距离不变，电极几何形状保持原样
    double effectiveDistance = distance;

    // 2. 计算触点区域总高度 (从靶点到最后一个触点顶部)
    double stepY = m_contactLength + m_contactSpacing;
    double contactRegionTop = m_contactLength + (m_numContacts - 1) * stepY;
    // = 1.5 + 3 * 2.0 = 7.5 mm

    // 3. 更新轴杆 (仅覆盖触点上方到进针点)
    double shaftLength = effectiveDistance - contactRegionTop;
    if (shaftLength < 0.1) shaftLength = 0.1;  // 最小保护值

    m_shaftSource->SetHeight(shaftLength);
    m_shaftSource->Update();
    m_shaftNormals->Update();  // 重新计算法线

    // 轴杆中心 = 触点区顶 + 轴杆长度/2
    double shaftCenterY = contactRegionTop + shaftLength / 2.0;
    vtkNew<vtkTransform> shaftTrans;
    shaftTrans->Translate(0.0, shaftCenterY, 0.0);
    for (auto& actor : m_shaftActors) {
        actor->SetUserTransform(shaftTrans);
    }

    // 4. 更新内芯 (贯穿全长)
    m_innerCoreSource->SetHeight(effectiveDistance);
    m_innerCoreSource->Update();
    m_innerCoreNormals->Update();

    vtkNew<vtkTransform> coreTrans;
    coreTrans->Translate(0.0, effectiveDistance / 2.0, 0.0);
    for (auto& actor : m_innerCoreActors) {
        actor->SetUserTransform(coreTrans);
    }

    // 5. 计算旋转矩阵 (从默认Y轴旋转到实际方向)
    double defaultY[3] = { 0.0, 1.0, 0.0 };
    double rotAxis[3];
    vtkMath::Cross(defaultY, dir, rotAxis);

    double angle = 0.0;
    if (vtkMath::Norm(rotAxis) > 1e-6) {
        vtkMath::Normalize(rotAxis);
        angle = vtkMath::DegreesFromRadians(acos(vtkMath::Dot(defaultY, dir)));
    }
    else if (vtkMath::Dot(defaultY, dir) < 0) {
        rotAxis[0] = 1.0; rotAxis[1] = 0.0; rotAxis[2] = 0.0;
        angle = 180.0;
    }

    // 6. 应用总体姿态 (先旋转对齐方向，再平移到有效靶点)
    vtkNew<vtkTransform> transform;
    transform->PostMultiply();
    transform->RotateWXYZ(angle, rotAxis);
    transform->Translate(effectiveTarget[0], effectiveTarget[1], effectiveTarget[2]);

    m_assembly3D->SetUserTransform(transform);
    for (int i = 0; i < 3; i++) {
        m_assembly2DAbove[i]->SetUserTransform(transform);
        m_assembly2DBelow[i]->SetUserTransform(transform);
    }
}

void DBSLeadModel::ApplyClippingPlanes(int viewIndex, vtkPlane* planeAbove, vtkPlane* planeBelow)
{
    if (viewIndex < 0 || viewIndex >= 3) return;

    for (auto& mapper : m_mappersAbove[viewIndex]) {
        mapper->RemoveAllClippingPlanes();
        if (planeAbove) mapper->AddClippingPlane(planeAbove);
    }

    for (auto& mapper : m_mappersBelow[viewIndex]) {
        mapper->RemoveAllClippingPlanes();
        if (planeBelow) mapper->AddClippingPlane(planeBelow);
    }
}

void DBSLeadModel::SetVisibility(bool visible)
{
    m_assembly3D->SetVisibility(visible ? 1 : 0);
    for (int i = 0; i < 3; i++) {
        m_assembly2DAbove[i]->SetVisibility(visible ? 1 : 0);
        m_assembly2DBelow[i]->SetVisibility(visible ? 1 : 0);
    }
}

int DBSLeadModel::GetContactIndexFromActor(vtkActor* actor)
{
    if (!actor) return -1;
    for (int i = 0; i < m_numContacts; ++i) {
        for (const auto& a : m_contactActors[i]) {
            if (a.Get() == actor) {
                return i;
            }
        }
    }
    return -1;
}

// ================================================================
// Sprint 1: 触点极性控制 —— 改变触点颜色实现视觉反馈
// ================================================================
void DBSLeadModel::setContactPolarity(int contactIndex, ContactPolarity polarity)
{
    if (contactIndex < 0 || contactIndex >= m_numContacts) return;
    m_contactPolarity[contactIndex] = polarity;

    double color[3];
    getPolarityColor(polarity, color);

    for (auto& actor : m_contactActors[contactIndex]) {
        actor->GetProperty()->SetColor(color[0], color[1], color[2]);

        if (polarity != POLARITY_OFF) {
            actor->GetProperty()->SetSpecular(1.2);
            actor->GetProperty()->SetSpecularPower(80);
            actor->GetProperty()->SetAmbient(0.35);
        }
        else {
            actor->GetProperty()->SetSpecular(1.0);
            actor->GetProperty()->SetSpecularPower(120);
            actor->GetProperty()->SetAmbient(0.2);
        }
    }

    // Sprint 2: 极性变化时重新计算 VTA 位置（中心跟随阴极）
    updateVTA();
}

ContactPolarity DBSLeadModel::getContactPolarity(int contactIndex) const
{
    if (contactIndex < 0 || contactIndex >= m_numContacts) return POLARITY_OFF;
    return m_contactPolarity[contactIndex];
}

void DBSLeadModel::getPolarityColor(ContactPolarity polarity, double color[3]) const
{
    switch (polarity) {
    case POLARITY_CATHODE:
        // 阴极：冷蓝色
        color[0] = 0.2; color[1] = 0.5; color[2] = 1.0;
        break;
    case POLARITY_ANODE:
        // 阳极：暖红色
        color[0] = 1.0; color[1] = 0.3; color[2] = 0.2;
        break;
    case POLARITY_OFF:
    default:
        color[0] = 0.92; color[1] = 0.95; color[2] = 1.0;
        break;
    }
}

// ================================================================
// Sprint 2: VTA 放电球控制
// ================================================================

void DBSLeadModel::setAmplitude(double mA)
{
    m_amplitude = mA;
    updateVTA();
}

void DBSLeadModel::setPulseWidth(int us)
{
    m_pulseWidth = us;
    updateVTA();
}

void DBSLeadModel::setFrequency(int hz)
{
    m_frequency = hz;
    // 频率不直接影响 VTA 半径，仅记录
}

void DBSLeadModel::setShowVTA(bool show)
{
    m_showVTA = show;
    updateVTA();
}

void DBSLeadModel::setVTADisplayPreset(int presetIndex)
{
    if (presetIndex < 0 || presetIndex > 2) {
        presetIndex = 0;
    }
    m_vtaDisplayPreset = presetIndex;
    applyVTADisplayPreset();
}

void DBSLeadModel::applyVTAMaterial(vtkProperty* prop, int renderType) const
{
    if (!prop) return;

    struct VTAMaterialPreset {
        double r;
        double g;
        double b;
        double opacity3D;
        double opacityAbove;
        double opacityBelow;
        double ambient3D;
        double diffuse3D;
        double specular3D;
        double specularPower3D;
    };

    static const VTAMaterialPreset presets[] = {
        {0.05, 0.72, 0.70, 0.24, 0.18, 0.05, 0.12, 0.50, 0.03, 3.0},
        {1.00, 0.42, 0.08, 0.26, 0.19, 0.05, 0.12, 0.50, 0.03, 3.0},
        {0.95, 0.78, 0.04, 0.22, 0.16, 0.05, 0.12, 0.48, 0.02, 3.0}
    };

    int idx = m_vtaDisplayPreset;
    if (idx < 0 || idx > 2) {
        idx = 0;
    }
    const auto& preset = presets[idx];

    prop->SetColor(preset.r, preset.g, preset.b);
    prop->SetInterpolationToGouraud();
    prop->BackfaceCullingOn();
    prop->SetLighting(true);
    prop->SetEdgeVisibility(0);

    if (renderType == 0) {
        prop->SetOpacity(preset.opacity3D);
        prop->SetAmbient(preset.ambient3D);
        prop->SetDiffuse(preset.diffuse3D);
        prop->SetSpecular(preset.specular3D);
        prop->SetSpecularPower(preset.specularPower3D);
    }
    else if (renderType == 1) {
        prop->SetOpacity(preset.opacityAbove);
        prop->SetAmbient(0.30);
        prop->SetDiffuse(0.35);
        prop->SetSpecular(0.10);
        prop->SetSpecularPower(24.0);
    }
    else {
        prop->SetOpacity(preset.opacityBelow);
        prop->SetAmbient(0.18);
        prop->SetDiffuse(0.12);
        prop->SetSpecular(0.0);
    }
}

void DBSLeadModel::applyVTADisplayPreset()
{
    for (size_t k = 0; k < m_vtaActors.size(); ++k) {
        int renderType = 0;
        if (k > 0) {
            renderType = ((k - 1) % 2 == 0) ? 1 : 2;
        }
        auto& actor = m_vtaActors[k];
        applyVTAMaterial(actor->GetProperty(), renderType);
        actor->SetBackfaceProperty(nullptr);
    }
}

vtkSmartPointer<vtkPolyData> DBSLeadModel::buildLeadDBSDisplayVTA(vtkPolyData* vtaPoly) const
{
    if (!vtaPoly || vtaPoly->GetNumberOfPoints() == 0) {
        return nullptr;
    }

    vtkSmartPointer<vtkPolyData> smoothingInput = vtkSmartPointer<vtkPolyData>::New();
    smoothingInput->DeepCopy(vtaPoly);

    double bounds[6] = {0};
    vtaPoly->GetBounds(bounds);
    double extentX = bounds[1] - bounds[0];
    double extentY = bounds[3] - bounds[2];
    double extentZ = bounds[5] - bounds[4];
    double maxExtent = std::max(extentX, std::max(extentY, extentZ));
    if (maxExtent > 0.0) {
        double padding = 0.8;
        double spacing = 0.20;
        double padded[6] = {
            bounds[0] - padding, bounds[1] + padding,
            bounds[2] - padding, bounds[3] + padding,
            bounds[4] - padding, bounds[5] + padding
        };
        double paddedExtentX = padded[1] - padded[0];
        double paddedExtentY = padded[3] - padded[2];
        double paddedExtentZ = padded[5] - padded[4];
        double paddedMaxExtent = std::max(paddedExtentX, std::max(paddedExtentY, paddedExtentZ));
        if (paddedMaxExtent / spacing > 95.0) {
            spacing = paddedMaxExtent / 95.0;
        }

        int dims[3] = {
            static_cast<int>(std::ceil(paddedExtentX / spacing)) + 1,
            static_cast<int>(std::ceil(paddedExtentY / spacing)) + 1,
            static_cast<int>(std::ceil(paddedExtentZ / spacing)) + 1
        };
        dims[0] = std::max(dims[0], 8);
        dims[1] = std::max(dims[1], 8);
        dims[2] = std::max(dims[2], 8);

        vtkNew<vtkImageData> mask;
        mask->SetDimensions(dims);
        mask->SetOrigin(padded[0], padded[2], padded[4]);
        mask->SetSpacing(spacing, spacing, spacing);
        mask->AllocateScalars(VTK_FLOAT, 1);

        vtkNew<vtkImplicitPolyDataDistance> implicitSurface;
        implicitSurface->SetInput(vtaPoly);

        double p[3] = {0.0, 0.0, 0.0};
        for (int k = 0; k < dims[2]; ++k) {
            p[2] = padded[4] + k * spacing;
            for (int j = 0; j < dims[1]; ++j) {
                p[1] = padded[2] + j * spacing;
                for (int i = 0; i < dims[0]; ++i) {
                    p[0] = padded[0] + i * spacing;
                    float* voxel = static_cast<float*>(mask->GetScalarPointer(i, j, k));
                    voxel[0] = (implicitSurface->EvaluateFunction(p) <= 0.0) ? 1.0f : 0.0f;
                }
            }
        }

        vtkNew<vtkContourFilter> maskContour;
        maskContour->SetInputData(mask);
        maskContour->SetValue(0, 0.5);
        maskContour->Update();
        if (maskContour->GetOutput() && maskContour->GetOutput()->GetNumberOfPoints() > 0) {
            smoothingInput->DeepCopy(maskContour->GetOutput());
        }
    }

    vtkNew<vtkCleanPolyData> clean;
    clean->SetInputData(smoothingInput);
    clean->PointMergingOn();
    clean->Update();

    vtkNew<vtkTriangleFilter> triangle;
    triangle->SetInputConnection(clean->GetOutputPort());
    triangle->Update();

    vtkNew<vtkWindowedSincPolyDataFilter> smoother;
    smoother->SetInputConnection(triangle->GetOutputPort());
    smoother->SetNumberOfIterations(25);
    smoother->SetPassBand(0.12);
    smoother->BoundarySmoothingOff();
    smoother->FeatureEdgeSmoothingOff();
    smoother->NonManifoldSmoothingOn();
    smoother->NormalizeCoordinatesOn();
    smoother->Update();

    vtkNew<vtkPolyDataNormals> normals;
    normals->SetInputConnection(smoother->GetOutputPort());
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOff();
    normals->SplittingOff();
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();
    normals->Update();

    vtkSmartPointer<vtkPolyData> out = vtkSmartPointer<vtkPolyData>::New();
    if (normals->GetOutput() && normals->GetOutput()->GetNumberOfPoints() > 0) {
        out->DeepCopy(normals->GetOutput());
    } else {
        out->DeepCopy(smoothingInput);
    }
    return out;
}

// ----------------------------------------------------------------
// VTA 核心更新逻辑：
//   1. 根据 Amplitude + PulseWidth 计算半径
//   2. 将 VTA 球心定位在所有阴极触点的质心处
//   3. 同步更新所有 7 套装配体中的 VTA Actor
// ----------------------------------------------------------------
void DBSLeadModel::updateVTA()
{
    // 1. 计算 VTA 半径
    //    简化 Miocinovic 模型: R(mm) = sqrt(mA * us / 90)
    //    1.5mA, 60us -> R=1.0mm;  3.0mA, 60us -> R=1.41mm
    double radius = 0.0;
    if (m_amplitude > 0.001 && m_showVTA) {
        radius = std::sqrt(m_amplitude * m_pulseWidth / 90.0);
    }

    // 2. 更新球体几何（所有 VTA Actor 共享同一个 source）
    m_vtaSource->SetRadius(radius > 0.001 ? radius : 0.001);
    m_vtaSource->Update();
    m_vtaNormals->Update();

    // 3. 计算 VTA 中心 = 所有阴极触点 Y 坐标的加权平均
    double stepY = m_contactLength + m_contactSpacing;
    double vtaCenterY = 0.0;
    int cathodeCount = 0;

    for (int i = 0; i < m_numContacts; i++) {
        if (m_contactPolarity[i] == POLARITY_CATHODE) {
            vtaCenterY += m_contactLength / 2.0 + i * stepY;
            cathodeCount++;
        }
    }

    if (cathodeCount > 0) {
        vtaCenterY /= cathodeCount;
    }
    else {
        // 无阴极时，VTA 中心放在电极中部 (Contact 1-2 之间)
        vtaCenterY = m_contactLength / 2.0 + 1.5 * stepY;
    }

    // 4. 更新所有 VTA Actor 的位置和可见性
    bool visible = m_showVTA && m_hasRealVTA && m_amplitude > 0.001 && cathodeCount > 0;
    for (auto& actor : m_vtaActors) {
        if (m_hasRealVTA) {
            // 真实 VTA: 几何已在世界坐标，不设 Transform，只控制可见性
            actor->SetVisibility(visible ? 1 : 0);
        } else {
            // 球形 VTA: 需要位移到阴极中心
            vtkNew<vtkTransform> trans;
            trans->Translate(0.0, vtaCenterY, 0.0);
            actor->SetUserTransform(trans);
            actor->SetVisibility(visible ? 1 : 0);
        }
    }
}

// ================================================================
// 阶段 A：分析接口实现
// ================================================================

double DBSLeadModel::getVTARadius() const
{
    if (!m_hasRealVTA || !m_showVTA || m_amplitude <= 0.001) return 0.0;
    return m_vtaSource->GetRadius();
}

bool DBSLeadModel::getVTACenterWorld(double outCenter[3])
{
    if (m_vtaActors.empty() || !m_hasRealVTA || !m_showVTA || m_amplitude <= 0.001) return false;
    
    // 获取 3D 视图中 VTA 的变换矩阵
    vtkActor* vtaActor = m_vtaActors[0];
    vtkTransform* localTrans = vtkTransform::SafeDownCast(vtaActor->GetUserTransform());
    if (!localTrans) return false;
    
    // VTA 在自身坐标系中的中心是 (0,0,0)（因为 localTrans 已经做了位移）
    // 所以只需要获取 Assembly3D 的世界变换矩阵
    vtkMatrix4x4* assemblyMat = m_assembly3D->GetMatrix();
    if (!assemblyMat) return false;
    
    double localPos[4] = { 0.0, 0.0, 0.0, 1.0 };
    // 先乘以内部 actor 的本地变换
    localTrans->GetMatrix()->MultiplyPoint(localPos, localPos);
    // 再乘以整个装配体的世界变换
    double worldPos[4];
    assemblyMat->MultiplyPoint(localPos, worldPos);
    
    for (int i = 0; i < 3; i++) outCenter[i] = worldPos[i];
    return true;
}

bool DBSLeadModel::getContactCenterWorld(int index, double outCenter[3])
{
    if (index < 0 || index >= m_numContacts || m_contactActors[index].empty()) return false;
    
    vtkActor* contactActor = m_contactActors[index][0]; // 取 3D 视图里的那个
    vtkTransform* localTrans = vtkTransform::SafeDownCast(contactActor->GetUserTransform());
    if (!localTrans) return false;
    
    vtkMatrix4x4* assemblyMat = m_assembly3D->GetMatrix();
    if (!assemblyMat) return false;
    
    double localPos[4] = { 0.0, 0.0, 0.0, 1.0 };
    localTrans->GetMatrix()->MultiplyPoint(localPos, localPos);
    
    double worldPos[4];
    assemblyMat->MultiplyPoint(localPos, worldPos);
    
    for (int i = 0; i < 3; i++) outCenter[i] = worldPos[i];
    return true;
}

// ================================================================
// Sprint 3: 深度微调
// ================================================================
void DBSLeadModel::setDepthOffset(double mm)
{
    m_depthOffset = mm;
    // 重新应用轨迹（UpdateTrajectory 内部会使用新的 m_depthOffset）
    if (m_hasTrajectory) {
        UpdateTrajectory(m_originalEntry, m_originalTarget);
    }
}

// ================================================================
// Sprint 3: 型号切换
// ================================================================
void DBSLeadModel::setLeadType(int presetIndex)
{
    // 预设参数表
    struct Preset {
        double contactLen;
        double spacing;
        double radius;
        int nContacts;
    };
    static const Preset presets[] = {
        {1.5, 0.5, 0.635, 4},   // Medtronic 3389 (窄间距)
        {1.5, 1.5, 0.635, 4},   // Medtronic 3387 (宽间距)
        {1.5, 0.5, 0.65,  4},   // Boston Cartesia (简化版)
    };

    if (presetIndex < 0 || presetIndex > 2) return;
    if (presetIndex == m_currentPreset) return;

    m_currentPreset = presetIndex;
    m_contactLength = presets[presetIndex].contactLen;
    m_contactSpacing = presets[presetIndex].spacing;
    m_leadRadius = presets[presetIndex].radius;
    m_numContacts = presets[presetIndex].nContacts;

    rebuildGeometry();
}

// ----------------------------------------------------------------
// Sprint 3: 重建静态几何源 (触点环/绝缘间隔/针尖)
//   型号切换时调用，用新的物理参数重新创建
// ----------------------------------------------------------------
void DBSLeadModel::rebuildStaticSources()
{
    // 触点环
    m_contactSource = vtkSmartPointer<vtkCylinderSource>::New();
    m_contactSource->SetRadius(m_leadRadius);
    m_contactSource->SetHeight(m_contactLength);
    m_contactSource->SetResolution(250);
    m_contactNormals = vtkSmartPointer<vtkPolyDataNormals>::New();
    m_contactNormals->SetInputConnection(m_contactSource->GetOutputPort());
    m_contactNormals->SetFeatureAngle(60.0);
    m_contactNormals->ComputePointNormalsOn();
    m_contactNormals->Update();
    m_contactPort = m_contactNormals->GetOutputPort();

    // 绝缘间隔段
    m_gapSource = vtkSmartPointer<vtkCylinderSource>::New();
    m_gapSource->SetRadius(m_leadRadius - 0.1);
    m_gapSource->SetHeight(m_contactSpacing);
    m_gapSource->SetResolution(200);
    m_gapNormals = vtkSmartPointer<vtkPolyDataNormals>::New();
    m_gapNormals->SetInputConnection(m_gapSource->GetOutputPort());
    m_gapNormals->SetFeatureAngle(60.0);
    m_gapNormals->ComputePointNormalsOn();
    m_gapNormals->Update();
    m_gapPort = m_gapNormals->GetOutputPort();

    // 半球针尖
    m_tipSource = vtkSmartPointer<vtkSphereSource>::New();
    m_tipSource->SetRadius(m_leadRadius);
    m_tipSource->SetThetaResolution(100);
    m_tipSource->SetPhiResolution(100);
    m_tipSource->SetStartPhi(0);
    m_tipSource->SetEndPhi(90);
    m_tipNormals = vtkSmartPointer<vtkPolyDataNormals>::New();
    m_tipNormals->SetInputConnection(m_tipSource->GetOutputPort());
    m_tipNormals->SetFeatureAngle(60.0);
    m_tipNormals->ComputePointNormalsOn();
    m_tipNormals->Update();
    m_tipPort = m_tipNormals->GetOutputPort();

    // 动态源的半径也要更新
    m_shaftSource->SetRadius(m_leadRadius - 0.1);
    m_innerCoreSource->SetRadius(m_leadRadius * 0.3);
}

// ----------------------------------------------------------------
// Sprint 3: 完整重建流程
//   1. 保存状态 → 2. 清空装配体 → 3. 重建源 →
//   4. 重建装配体 → 5. 恢复轨迹 → 6. 恢复极性 → 7. 恢复 VTA
// ----------------------------------------------------------------
void DBSLeadModel::rebuildGeometry()
{
    // 1. 保存当前极性状态
    ContactPolarity savedPol[4];
    for (int i = 0; i < 4; i++) savedPol[i] = m_contactPolarity[i];
    bool savedVisible = m_assembly3D->GetVisibility();

    // 2. 清空所有 Actor 引用数组
    m_shaftActors.clear();
    m_innerCoreActors.clear();
    for (int i = 0; i < 4; i++) m_contactActors[i].clear();
    m_vtaActors.clear();
    for (int i = 0; i < 3; i++) {
        m_mappersAbove[i].clear();
        m_mappersBelow[i].clear();
    }

    // 3. 清空装配体的所有 Parts (保留装配体本身在渲染器中)
    auto clearParts = [](vtkAssembly* assembly) {
        auto* parts = assembly->GetParts();
        std::vector<vtkProp3D*> toRemove;
        parts->InitTraversal();
        vtkProp3D* p;
        while ((p = parts->GetNextProp3D())) toRemove.push_back(p);
        for (auto* r : toRemove) assembly->RemovePart(r);
    };

    clearParts(m_assembly3D);
    for (int i = 0; i < 3; i++) {
        clearParts(m_assembly2DAbove[i]);
        clearParts(m_assembly2DBelow[i]);
    }

    // 4. 用新参数重建静态几何源
    rebuildStaticSources();

    // 5. 重建 7 套装配体内容
    BuildAssembly(m_assembly3D, nullptr, 0);
    for (int i = 0; i < 3; i++) {
        BuildAssembly(m_assembly2DAbove[i], &m_mappersAbove[i], 1);
        BuildAssembly(m_assembly2DBelow[i], &m_mappersBelow[i], 2);
    }
    applyVTADisplayPreset();

    // 6. 恢复轨迹姿态
    if (m_hasTrajectory) {
        UpdateTrajectory(m_originalEntry, m_originalTarget);
    }

    // 7. 恢复极性状态
    for (int i = 0; i < m_numContacts && i < 4; i++) {
        if (savedPol[i] != POLARITY_OFF) {
            setContactPolarity(i, savedPol[i]);
        }
    }

    // 8. 恢复 VTA 状态
    updateVTA();

    // 9. 恢复可见性
    SetVisibility(savedVisible);
}

// ================================================================
// 真实 VTA (FEM 结果) 替换球形 VTA
// ================================================================
void DBSLeadModel::setRealVTA(vtkSmartPointer<vtkPolyData> vtaPoly)
{
    if (!vtaPoly || vtaPoly->GetNumberOfPoints() == 0) {
        clearRealVTA();
        return;
    }

    m_realVTAPoly = vtkSmartPointer<vtkPolyData>::New();
    m_realVTAPoly->DeepCopy(vtaPoly);
    m_realVTADisplayPoly = buildLeadDBSDisplayVTA(m_realVTAPoly);
    if (!m_realVTADisplayPoly || m_realVTADisplayPoly->GetNumberOfPoints() == 0) {
        m_realVTADisplayPoly = m_realVTAPoly;
    }
    m_hasRealVTA = true;

    double bounds[6] = {0};
    m_realVTAPoly->GetBounds(bounds);
    double cx = 0.5 * (bounds[0] + bounds[1]);
    double cy = 0.5 * (bounds[2] + bounds[3]);
    double cz = 0.5 * (bounds[4] + bounds[5]);
    double dx = bounds[1] - bounds[0];
    double dy = bounds[3] - bounds[2];
    double dz = bounds[5] - bounds[4];
    qDebug() << "[VTA Display] real VTA bounds:" << bounds[0] << bounds[1]
             << bounds[2] << bounds[3] << bounds[4] << bounds[5];
    qDebug() << "[VTA Display] real VTA center:" << cx << cy << cz
             << "extent (mm):" << dx << dy << dz
             << "approx radius:" << 0.5 * std::sqrt(dx*dx + dy*dy + dz*dz);

    // 关键修复:
    // VTA polydata 已是世界坐标 (来自 FEM)，但 actor 挂在带 UserTransform 的
    // assembly 下，会被再次变换。每个 actor 都需要设置自身 UserTransform =
    // 所属 assembly 当前 UserMatrix 的逆，使最终效果为 identity。
    auto applyAssemblyInverse = [](vtkActor* actor, vtkAssembly* assembly) {
        if (!actor || !assembly) return;
        vtkSmartPointer<vtkMatrix4x4> mat = vtkSmartPointer<vtkMatrix4x4>::New();
        mat->DeepCopy(assembly->GetMatrix());
        mat->Invert();
        auto trans = vtkSmartPointer<vtkTransform>::New();
        trans->SetMatrix(mat);
        actor->SetUserTransform(trans);
    };

    // 用真实 VTA 几何替换所有 VTA Actor 的输入
    // m_vtaActors 顺序: [3D, Above0, Below0, Above1, Below1, Above2, Below2]
    for (size_t k = 0; k < m_vtaActors.size(); ++k) {
        auto& actor = m_vtaActors[k];
        auto mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
        if (mapper) {
            mapper->SetInputData(m_realVTADisplayPoly);
            mapper->ScalarVisibilityOff();
            mapper->Update();
        }

        vtkAssembly* parent = nullptr;
        if (k == 0) parent = m_assembly3D;
        else {
            int idx = static_cast<int>((k - 1) / 2);
            bool isAbove = ((k - 1) % 2 == 0);
            if (idx >= 0 && idx < 3)
                parent = isAbove ? m_assembly2DAbove[idx] : m_assembly2DBelow[idx];
        }
        applyAssemblyInverse(actor.GetPointer(), parent);

        actor->SetVisibility(m_showVTA ? 1 : 0);
    }
    applyVTADisplayPreset();

    if (!m_vtaActors.empty()) {
        double worldBounds[6] = {0};
        m_vtaActors[0]->GetBounds(worldBounds);
        qDebug() << "[VTA Display] 3D actor scene bounds:"
                 << worldBounds[0] << worldBounds[1]
                 << worldBounds[2] << worldBounds[3]
                 << worldBounds[4] << worldBounds[5];
    }
}

void DBSLeadModel::clearRealVTA()
{
    m_hasRealVTA = false;
    m_realVTAPoly = nullptr;
    m_realVTADisplayPoly = nullptr;

    // 恢复球形 VTA
    for (auto& actor : m_vtaActors) {
        auto mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
        if (mapper) {
            mapper->SetInputConnection(m_vtaNormals->GetOutputPort());
            mapper->ScalarVisibilityOff();
            mapper->Update();
        }
    }
    applyVTADisplayPreset();
    updateVTA();  // 恢复球形位置
}