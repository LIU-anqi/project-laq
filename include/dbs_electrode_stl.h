#ifndef DBS_ELECTRODE_STL_H
#define DBS_ELECTRODE_STL_H
#pragma execution_character_set("utf-8")

#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include "dbs_fem_types.h"

// ============================================================
// DBS 电极 STL 生成器
// 根据轨迹和电极型号参数，生成闭合三角面片：
//   - 绝缘体杆体 (label = LABEL_ELECTRODE_BODY)
//   - 各触点圆柱 (label = LABEL_CONTACT_BASE + i)
// ============================================================

class DBSElectrodeSTL
{
public:
    DBSElectrodeSTL();

    // 设置参数
    void setSpec(const dbs_fem::DBSSimSpec& spec);

    // 生成几何 (必须先 setSpec)
    bool generate();

    // 获取结果
    vtkSmartPointer<vtkPolyData> getInsulatorPoly() const { return m_insulatorPoly; }
    vtkSmartPointer<vtkPolyData> getEncapsulationPoly() const { return m_encapsulationPoly; }
    vtkSmartPointer<vtkPolyData> getContactPoly(int contactIndex) const;
    int getNumContacts() const { return m_spec.numContacts; }

    // 获取触点中心世界坐标 (用于边界条件定位)
    bool getContactCenter(int contactIndex, double center[3]) const;

private:
    // 沿轨迹方向生成圆柱并旋转对齐
    vtkSmartPointer<vtkPolyData> makeAlignedCylinder(
        const double center[3],
        double radius, double height,
        int resolution = 36,
        int axialSegments = 1) const;
    vtkSmartPointer<vtkPolyData> makeRoundedLeadBody(
        const double distalTip[3],
        double radius, double totalLength,
        int resolution = 36,
        int hemisphereSegments = 8,
        int axialSegments = 1) const;

    // 轨迹方向单位向量
    void getTrajectoryDir(double dir[3]) const;

    // 计算各触点中心坐标
    void computeContactCenters();

    dbs_fem::DBSSimSpec m_spec;
    bool m_generated = false;

    vtkSmartPointer<vtkPolyData> m_insulatorPoly;
    vtkSmartPointer<vtkPolyData> m_encapsulationPoly;
    vtkSmartPointer<vtkPolyData> m_contactPolys[4];
    double m_contactCenters[4][3];
};

#endif // DBS_ELECTRODE_STL_H
