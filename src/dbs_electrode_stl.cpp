#include "dbs_electrode_stl.h"

#include <vtkCylinderSource.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTriangleFilter.h>
#include <vtkPolyDataNormals.h>
#include <vtkAppendPolyData.h>
#include <vtkCleanPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkMath.h>
#include <vtkNew.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <QDebug>

// ============================================================
// 构造 / 配置
// ============================================================

DBSElectrodeSTL::DBSElectrodeSTL()
{
    for (int i = 0; i < 4; ++i)
        m_contactCenters[i][0] = m_contactCenters[i][1] = m_contactCenters[i][2] = 0.0;
}

void DBSElectrodeSTL::setSpec(const dbs_fem::DBSSimSpec& spec)
{
    m_spec = spec;
    m_generated = false;
}

// ============================================================
// 轨迹方向计算
// ============================================================

void DBSElectrodeSTL::getTrajectoryDir(double dir[3]) const
{
    dir[0] = m_spec.target[0] - m_spec.entry[0];
    dir[1] = m_spec.target[1] - m_spec.entry[1];
    dir[2] = m_spec.target[2] - m_spec.entry[2];
    double len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    if (len > 1e-9) {
        dir[0] /= len;
        dir[1] /= len;
        dir[2] /= len;
    }
}

// ============================================================
// 计算各触点沿轨迹的中心坐标
// ============================================================

void DBSElectrodeSTL::computeContactCenters()
{
    double dir[3];
    getTrajectoryDir(dir);

    // 电极尖端 = target + depthOffset * dir (沿 entry→target 方向)
    double tip[3];
    tip[0] = m_spec.target[0] + m_spec.depthOffset * dir[0];
    tip[1] = m_spec.target[1] + m_spec.depthOffset * dir[1];
    tip[2] = m_spec.target[2] + m_spec.depthOffset * dir[2];

    // 触点从尖端开始向 entry 方向排列 (即沿 -dir 方向)
    // 第 0 个触点的中心距尖端: tipLength + contactLength/2
    // 3389 型号尖端长度约 1.5mm (半球)
    double tipLength = m_spec.leadRadius;  // 球形尖端半径≈杆体半径

    for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
        double offset = tipLength
                       + m_spec.contactLength * 0.5
                       + i * (m_spec.contactLength + m_spec.contactSpacing);
        // 从 tip 沿 -dir (朝 entry) 偏移
        m_contactCenters[i][0] = tip[0] - offset * dir[0];
        m_contactCenters[i][1] = tip[1] - offset * dir[1];
        m_contactCenters[i][2] = tip[2] - offset * dir[2];
    }
}

bool DBSElectrodeSTL::getContactCenter(int contactIndex, double center[3]) const
{
    if (contactIndex < 0 || contactIndex >= m_spec.numContacts || !m_generated) return false;
    center[0] = m_contactCenters[contactIndex][0];
    center[1] = m_contactCenters[contactIndex][1];
    center[2] = m_contactCenters[contactIndex][2];
    return true;
}

// ============================================================
// 生成沿轨迹对齐的闭合圆柱
// ============================================================

vtkSmartPointer<vtkPolyData> DBSElectrodeSTL::makeAlignedCylinder(
    const double center[3], double radius, double height, int resolution, int axialSegments) const
{
    resolution = std::max(3, resolution);
    axialSegments = std::max(1, axialSegments);

    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> polys;

    double y0 = -0.5 * height;
    double dy = height / static_cast<double>(axialSegments);
    for (int j = 0; j <= axialSegments; ++j) {
        double y = y0 + j * dy;
        for (int i = 0; i < resolution; ++i) {
            double a = 2.0 * vtkMath::Pi() * static_cast<double>(i) / static_cast<double>(resolution);
            points->InsertNextPoint(radius * std::cos(a), y, radius * std::sin(a));
        }
    }

    vtkIdType bottomCenter = points->InsertNextPoint(0.0, y0, 0.0);
    vtkIdType topCenter = points->InsertNextPoint(0.0, 0.5 * height, 0.0);

    auto ringId = [resolution](int j, int i) -> vtkIdType {
        int ii = (i + resolution) % resolution;
        return static_cast<vtkIdType>(j * resolution + ii);
    };

    for (int j = 0; j < axialSegments; ++j) {
        for (int i = 0; i < resolution; ++i) {
            vtkIdType p0 = ringId(j, i);
            vtkIdType p1 = ringId(j, i + 1);
            vtkIdType p2 = ringId(j + 1, i + 1);
            vtkIdType p3 = ringId(j + 1, i);
            vtkIdType tri1[3] = {p0, p1, p2};
            vtkIdType tri2[3] = {p0, p2, p3};
            polys->InsertNextCell(3, tri1);
            polys->InsertNextCell(3, tri2);
        }
    }

    for (int i = 0; i < resolution; ++i) {
        vtkIdType p0 = ringId(0, i);
        vtkIdType p1 = ringId(0, i + 1);
        vtkIdType p2 = ringId(axialSegments, i);
        vtkIdType p3 = ringId(axialSegments, i + 1);
        vtkIdType bottomTri[3] = {bottomCenter, p1, p0};
        vtkIdType topTri[3] = {topCenter, p2, p3};
        polys->InsertNextCell(3, bottomTri);
        polys->InsertNextCell(3, topTri);
    }

    vtkNew<vtkPolyData> cyl;
    cyl->SetPoints(points);
    cyl->SetPolys(polys);

    // 2. 计算旋转: 将 Y 轴对齐到轨迹方向
    double dir[3];
    getTrajectoryDir(dir);

    // vtkCylinderSource 默认沿 Y 轴
    // 需要将 (0,1,0) 旋转到 (-dir) (因为触点从 target 往 entry 排列)
    double negDir[3] = { -dir[0], -dir[1], -dir[2] };
    double yAxis[3] = { 0.0, 1.0, 0.0 };

    // 旋转轴 = cross(yAxis, negDir)
    double rotAxis[3];
    vtkMath::Cross(yAxis, negDir, rotAxis);
    double rotAxisLen = vtkMath::Norm(rotAxis);

    // 旋转角度
    double dotVal = vtkMath::Dot(yAxis, negDir);
    double angle = std::acos(std::max(-1.0, std::min(1.0, dotVal))) * 180.0 / vtkMath::Pi();

    vtkNew<vtkTransform> transform;
    transform->PostMultiply();

    // 旋转 (如果旋转轴长度为 0 说明方向平行或反向)
    if (rotAxisLen > 1e-9) {
        rotAxis[0] /= rotAxisLen;
        rotAxis[1] /= rotAxisLen;
        rotAxis[2] /= rotAxisLen;
        transform->RotateWXYZ(angle, rotAxis[0], rotAxis[1], rotAxis[2]);
    }
    else if (dotVal < 0) {
        // 反向: 绕任意垂直轴旋转 180°
        transform->RotateX(180.0);
    }

    // 平移到目标中心
    transform->Translate(center[0], center[1], center[2]);

    vtkNew<vtkTransformPolyDataFilter> tf;
    tf->SetInputData(cyl);
    tf->SetTransform(transform);
    tf->Update();

    // 3. 三角化 + 法线
    vtkNew<vtkTriangleFilter> tri;
    tri->SetInputConnection(tf->GetOutputPort());
    tri->Update();

    vtkNew<vtkPolyDataNormals> normals;
    normals->SetInputConnection(tri->GetOutputPort());
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();
    normals->Update();

    vtkSmartPointer<vtkPolyData> result = vtkSmartPointer<vtkPolyData>::New();
    result->DeepCopy(normals->GetOutput());
    return result;
}

vtkSmartPointer<vtkPolyData> DBSElectrodeSTL::makeRoundedLeadBody(
    const double distalTip[3], double radius, double totalLength,
    int resolution, int hemisphereSegments, int axialSegments) const
{
    resolution = std::max(8, resolution);
    hemisphereSegments = std::max(2, hemisphereSegments);
    axialSegments = std::max(1, axialSegments);
    if (radius <= 0.0 || totalLength <= radius) return nullptr;

    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> polys;

    vtkIdType tipId = points->InsertNextPoint(0.0, 0.0, 0.0);
    std::vector<std::vector<vtkIdType>> rings;
    rings.reserve(static_cast<size_t>(hemisphereSegments + axialSegments));

    auto addRing = [&](double y, double r) {
        std::vector<vtkIdType> ring;
        ring.reserve(static_cast<size_t>(resolution));
        for (int i = 0; i < resolution; ++i) {
            double a = 2.0 * vtkMath::Pi() * static_cast<double>(i) / static_cast<double>(resolution);
            ring.push_back(points->InsertNextPoint(r * std::cos(a), y, r * std::sin(a)));
        }
        rings.push_back(ring);
    };

    for (int j = 1; j <= hemisphereSegments; ++j) {
        double alpha = 0.5 * vtkMath::Pi() * static_cast<double>(j) / static_cast<double>(hemisphereSegments);
        double y = radius * (1.0 - std::cos(alpha));
        double r = radius * std::sin(alpha);
        addRing(y, r);
    }

    double cylinderLength = totalLength - radius;
    for (int j = 1; j <= axialSegments; ++j) {
        double y = radius + cylinderLength * static_cast<double>(j) / static_cast<double>(axialSegments);
        addRing(y, radius);
    }

    for (int i = 0; i < resolution; ++i) {
        vtkIdType tri[3] = {
            tipId,
            rings.front()[static_cast<size_t>((i + 1) % resolution)],
            rings.front()[static_cast<size_t>(i)]
        };
        polys->InsertNextCell(3, tri);
    }

    for (size_t j = 0; j + 1 < rings.size(); ++j) {
        for (int i = 0; i < resolution; ++i) {
            vtkIdType p0 = rings[j][static_cast<size_t>(i)];
            vtkIdType p1 = rings[j][static_cast<size_t>((i + 1) % resolution)];
            vtkIdType p2 = rings[j + 1][static_cast<size_t>((i + 1) % resolution)];
            vtkIdType p3 = rings[j + 1][static_cast<size_t>(i)];
            vtkIdType tri1[3] = {p0, p1, p2};
            vtkIdType tri2[3] = {p0, p2, p3};
            polys->InsertNextCell(3, tri1);
            polys->InsertNextCell(3, tri2);
        }
    }

    vtkIdType topCenter = points->InsertNextPoint(0.0, totalLength, 0.0);
    const auto& topRing = rings.back();
    for (int i = 0; i < resolution; ++i) {
        vtkIdType tri[3] = {
            topCenter,
            topRing[static_cast<size_t>(i)],
            topRing[static_cast<size_t>((i + 1) % resolution)]
        };
        polys->InsertNextCell(3, tri);
    }

    vtkNew<vtkPolyData> body;
    body->SetPoints(points);
    body->SetPolys(polys);

    double dir[3];
    getTrajectoryDir(dir);
    double negDir[3] = { -dir[0], -dir[1], -dir[2] };
    double yAxis[3] = { 0.0, 1.0, 0.0 };
    double rotAxis[3];
    vtkMath::Cross(yAxis, negDir, rotAxis);
    double rotAxisLen = vtkMath::Norm(rotAxis);
    double dotVal = vtkMath::Dot(yAxis, negDir);
    double angle = std::acos(std::max(-1.0, std::min(1.0, dotVal))) * 180.0 / vtkMath::Pi();

    vtkNew<vtkTransform> transform;
    transform->PostMultiply();
    if (rotAxisLen > 1e-9) {
        rotAxis[0] /= rotAxisLen;
        rotAxis[1] /= rotAxisLen;
        rotAxis[2] /= rotAxisLen;
        transform->RotateWXYZ(angle, rotAxis[0], rotAxis[1], rotAxis[2]);
    }
    else if (dotVal < 0) {
        transform->RotateX(180.0);
    }
    transform->Translate(distalTip[0], distalTip[1], distalTip[2]);

    vtkNew<vtkTransformPolyDataFilter> tf;
    tf->SetInputData(body);
    tf->SetTransform(transform);
    tf->Update();

    vtkNew<vtkTriangleFilter> tri;
    tri->SetInputConnection(tf->GetOutputPort());
    tri->Update();

    vtkNew<vtkPolyDataNormals> normals;
    normals->SetInputConnection(tri->GetOutputPort());
    normals->ConsistencyOn();
    normals->AutoOrientNormalsOn();
    normals->Update();

    vtkSmartPointer<vtkPolyData> result = vtkSmartPointer<vtkPolyData>::New();
    result->DeepCopy(normals->GetOutput());
    return result;
}

// ============================================================
// 主生成函数
// ============================================================

bool DBSElectrodeSTL::generate()
{
    m_generated = false;

    double dir[3];
    getTrajectoryDir(dir);
    double trajLen = std::sqrt(
        std::pow(m_spec.target[0] - m_spec.entry[0], 2) +
        std::pow(m_spec.target[1] - m_spec.entry[1], 2) +
        std::pow(m_spec.target[2] - m_spec.entry[2], 2));

    if (trajLen < 1.0) {
        qWarning() << "[DBSElectrodeSTL] 轨迹长度过短:" << trajLen << "mm";
        return false;
    }

    // 1. 计算触点中心
    computeContactCenters();

    // 2. 计算绝缘体杆体的范围
    double tipLength = m_spec.leadRadius;
    double tip[3];
    tip[0] = m_spec.target[0] + m_spec.depthOffset * dir[0];
    tip[1] = m_spec.target[1] + m_spec.depthOffset * dir[1];
    tip[2] = m_spec.target[2] + m_spec.depthOffset * dir[2];

    // 杆体长度: 从尖端到最后一个触点顶部再多延伸一段
    double lastContactTop = tipLength
        + m_spec.numContacts * m_spec.contactLength
        + (m_spec.numContacts - 1) * m_spec.contactSpacing;
    double shaftExtra = 5.0;  // 触点上方多延伸 5mm
    double shaftLength = lastContactTop + shaftExtra;

    // 3. 生成绝缘体杆体
    m_insulatorPoly = makeRoundedLeadBody(tip, m_spec.leadRadius, shaftLength, 72, 12, 8);
    if (!m_insulatorPoly) {
        qWarning() << "[DBSElectrodeSTL] 绝缘体杆体生成失败";
        return false;
    }
    m_encapsulationPoly = nullptr;
    if (m_spec.useEncapsulationLayer && m_spec.encapsulationThickness > 0.0) {
        double encapTip[3] = {
            tip[0] + m_spec.encapsulationThickness * dir[0],
            tip[1] + m_spec.encapsulationThickness * dir[1],
            tip[2] + m_spec.encapsulationThickness * dir[2]
        };
        m_encapsulationPoly = makeRoundedLeadBody(
            encapTip,
            m_spec.leadRadius + m_spec.encapsulationThickness,
            shaftLength + 2.0 * m_spec.encapsulationThickness,
            72,
            12,
            8);
        if (!m_encapsulationPoly) {
            qWarning() << "[DBSElectrodeSTL] 包膜外壳生成失败";
            return false;
        }
    }

    qDebug() << "[DBSElectrodeSTL] 圆头绝缘体杆体:"
             << m_insulatorPoly->GetNumberOfPoints() << "pts,"
             << m_insulatorPoly->GetNumberOfCells() << "cells";
    if (m_encapsulationPoly) {
        qDebug() << "[DBSElectrodeSTL] 包膜外壳:"
                 << m_encapsulationPoly->GetNumberOfPoints() << "pts,"
                 << m_encapsulationPoly->GetNumberOfCells() << "cells,"
                 << "thickness=" << m_spec.encapsulationThickness << "mm";
    }

    // 4. 生成各触点
    //    conforming meshing 要求触点严格嵌套在绝缘体 polyhedron 内部。
    double contactRadius = m_spec.leadRadius - 0.01;
    if (contactRadius <= 0.0) contactRadius = m_spec.leadRadius;
    qDebug() << "[DBSElectrodeSTL] FEM 触点半径 =" << contactRadius
             << "mm (leadRadius=" << m_spec.leadRadius << ", strict nested)";

    for (int i = 0; i < m_spec.numContacts && i < 4; ++i) {
        m_contactPolys[i] = makeAlignedCylinder(
            m_contactCenters[i], contactRadius, m_spec.contactLength, 72, 6);

        if (!m_contactPolys[i]) {
            qWarning() << "[DBSElectrodeSTL] 触点" << i << "生成失败";
            return false;
        }

        qDebug() << "[DBSElectrodeSTL] 触点" << i << ":"
                 << m_contactPolys[i]->GetNumberOfPoints() << "pts,"
                 << m_contactPolys[i]->GetNumberOfCells() << "cells,"
                 << "center=(" << m_contactCenters[i][0]
                 << "," << m_contactCenters[i][1]
                 << "," << m_contactCenters[i][2] << ")";
    }

    m_generated = true;
    qDebug() << "[DBSElectrodeSTL] 电极 STL 生成完成";
    return true;
}

vtkSmartPointer<vtkPolyData> DBSElectrodeSTL::getContactPoly(int contactIndex) const
{
    if (contactIndex < 0 || contactIndex >= m_spec.numContacts || contactIndex >= 4)
        return nullptr;
    return m_contactPolys[contactIndex];
}
