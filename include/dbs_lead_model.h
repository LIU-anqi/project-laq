#ifndef DBS_LEAD_MODEL_H
#define DBS_LEAD_MODEL_H
#pragma execution_character_set("utf-8")

#include <vtkSmartPointer.h>
#include <vtkAssembly.h>
#include <vtkActor.h>
#include <vtkPolyDataMapper.h>
#include <vtkAlgorithmOutput.h>
#include <vtkPlane.h>
#include <vtkCylinderSource.h>
#include <vector>
#include <vtkSphereSource.h>
#include <vtkPolyDataNormals.h>
#include <vtkPolyData.h>

class vtkProperty;

// 触点极性枚举
enum ContactPolarity {
    POLARITY_OFF     =  0,
    POLARITY_CATHODE = -1,
    POLARITY_ANODE   =  1
};

class DBSLeadModel
{
public:
    DBSLeadModel();
    ~DBSLeadModel();

    vtkSmartPointer<vtkAssembly> GetAssembly3D() const { return m_assembly3D; }
    vtkSmartPointer<vtkAssembly> GetAssembly2DAbove(int viewIndex) const { return m_assembly2DAbove[viewIndex]; }
    vtkSmartPointer<vtkAssembly> GetAssembly2DBelow(int viewIndex) const { return m_assembly2DBelow[viewIndex]; }

    void UpdateTrajectory(const double entry[3], const double target[3]);
    void SetVisibility(bool visible);
    void ApplyClippingPlanes(int viewIndex, vtkPlane* planeAbove, vtkPlane* planeBelow);

    // ===== Sprint 1: 触点极性控制 =====
    void setContactPolarity(int contactIndex, ContactPolarity polarity);
    ContactPolarity getContactPolarity(int contactIndex) const;
    int getNumContacts() const { return m_numContacts; }

    // ===== Sprint 2: VTA 放电球控制 =====
    void setAmplitude(double mA);
    void setPulseWidth(int us);
    void setFrequency(int hz);
    void setShowVTA(bool show);
    void setVTADisplayPreset(int presetIndex);
    double getAmplitude() const { return m_amplitude; }
    int getPulseWidth() const { return m_pulseWidth; }
    int getFrequency() const { return m_frequency; }
    int getVTADisplayPreset() const { return m_vtaDisplayPreset; }
    double getLeadRadius() const { return m_leadRadius; }
    double getContactLength() const { return m_contactLength; }
    double getContactSpacing() const { return m_contactSpacing; }

    // ===== Sprint 3: 深度微调 + 型号切换 =====
    void setDepthOffset(double mm);
    double getDepthOffset() const { return m_depthOffset; }
    void setLeadType(int presetIndex);
    int getLeadType() const { return m_currentPreset; }

    // 【Sprint 4】反向查询触点索引
    int GetContactIndexFromActor(vtkActor* actor);

    // ==========================================
    // 阶段 A 分析所需接口
    // ==========================================
    double getVTARadius() const;
    bool getVTACenterWorld(double outCenter[3]);
    bool getContactCenterWorld(int index, double outCenter[3]);

    // ==========================================
    // 真实 VTA (FEM 计算结果) 接口
    // ==========================================
    void setRealVTA(vtkSmartPointer<vtkPolyData> vtaPoly);
    void clearRealVTA();
    bool hasRealVTA() const { return m_hasRealVTA; }
    vtkSmartPointer<vtkPolyData> getRealVTAPoly() const { return m_realVTAPoly; }

private:
    void BuildGeometry();
    void BuildAssembly(vtkSmartPointer<vtkAssembly> assembly,
        std::vector<vtkSmartPointer<vtkPolyDataMapper>>* mapperList,
        int renderType);

    void getPolarityColor(ContactPolarity polarity, double color[3]) const;
    void applyVTAMaterial(vtkProperty* prop, int renderType) const;
    void applyVTADisplayPreset();
    void updateVTA();
    vtkSmartPointer<vtkPolyData> buildLeadDBSDisplayVTA(vtkPolyData* vtaPoly) const;

    // Sprint 3: 重建所有几何体 (型号切换时调用)
    void rebuildGeometry();
    // Sprint 3: 重建静态几何源 (触点/间隔/针尖)
    void rebuildStaticSources();

private:
    // ===== 动态几何源 =====
    vtkSmartPointer<vtkCylinderSource> m_shaftSource;
    vtkSmartPointer<vtkPolyDataNormals> m_shaftNormals;
    vtkSmartPointer<vtkCylinderSource> m_innerCoreSource;
    vtkSmartPointer<vtkPolyDataNormals> m_innerCoreNormals;

    // ===== 静态几何源的输出端口及算法实例 =====
    // 必须保存 source 和 normals 的实例，否则 GetOutputPort() 的生产者会被销毁，引发崩溃
    vtkSmartPointer<vtkCylinderSource> m_contactSource;
    vtkSmartPointer<vtkPolyDataNormals> m_contactNormals;
    vtkSmartPointer<vtkAlgorithmOutput> m_contactPort;

    vtkSmartPointer<vtkCylinderSource> m_gapSource;
    vtkSmartPointer<vtkPolyDataNormals> m_gapNormals;
    vtkSmartPointer<vtkAlgorithmOutput> m_gapPort;

    vtkSmartPointer<vtkSphereSource> m_tipSource;
    vtkSmartPointer<vtkPolyDataNormals> m_tipNormals;
    vtkSmartPointer<vtkAlgorithmOutput> m_tipPort;

    // ===== VTA 几何源 =====
    vtkSmartPointer<vtkSphereSource> m_vtaSource;
    vtkSmartPointer<vtkPolyDataNormals> m_vtaNormals;

    // ===== 装配体 =====
    vtkSmartPointer<vtkAssembly> m_assembly3D;
    vtkSmartPointer<vtkAssembly> m_assembly2DAbove[3];
    vtkSmartPointer<vtkAssembly> m_assembly2DBelow[3];

    // ===== Actor 引用 =====
    std::vector<vtkSmartPointer<vtkActor>> m_shaftActors;
    std::vector<vtkSmartPointer<vtkActor>> m_innerCoreActors;
    std::vector<vtkSmartPointer<vtkActor>> m_contactActors[4];
    ContactPolarity m_contactPolarity[4] = { POLARITY_OFF, POLARITY_OFF, POLARITY_OFF, POLARITY_OFF };
    std::vector<vtkSmartPointer<vtkActor>> m_vtaActors;

    // ===== VTA 参数 =====
    double m_amplitude = 0.0;
    int m_pulseWidth = 60;
    int m_frequency = 130;
    bool m_showVTA = false;
    int m_vtaDisplayPreset = 0;

    // ===== 2D 裁剪 Mapper =====
    std::vector<vtkSmartPointer<vtkPolyDataMapper>> m_mappersAbove[3];
    std::vector<vtkSmartPointer<vtkPolyDataMapper>> m_mappersBelow[3];

    // ===== 物理参数 =====
    double m_leadRadius = 0.635;
    double m_contactLength = 1.5;
    double m_contactSpacing = 0.5;
    int m_numContacts = 4;

    // ===== Sprint 3: 深度偏移 + 型号 =====
    double m_depthOffset = 0.0;      // 沿轨迹方向的偏移量 (mm), +深入 -回退
    int m_currentPreset = 0;         // 当前型号: 0=3389, 1=3387, 2=Cartesia

    // Sprint 3: 存储原始轨迹，供深度偏移重算
    double m_originalEntry[3] = {0,0,0};
    double m_originalTarget[3] = {0,0,0};
    bool m_hasTrajectory = false;

    // ===== 真实 VTA 数据 =====
    bool m_hasRealVTA = false;
    vtkSmartPointer<vtkPolyData> m_realVTAPoly;
    vtkSmartPointer<vtkPolyData> m_realVTADisplayPoly;
};

#endif // DBS_LEAD_MODEL_H