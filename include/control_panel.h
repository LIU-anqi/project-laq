#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H
#pragma execution_character_set("utf-8")

#include <QWidget>
#include <QToolBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QStackedWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>

class ControlPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPanel(QWidget* parent = nullptr);

    // 提供接口：允许外部设置 3D 参数显示的数值 (比如重置时更新 UI)
    void set3DParamsUI(double shell, double internal, double edge);

    // 【新增阶段A】提供接口：更新临床分析面板的 UI
    void updateAnalysisUI(const std::vector<double>& coverages, const std::vector<double>& distances, int bestContactIndex);

    // 更新 AC-PC 标定状态显示
    void updateAcPcStatusUI(bool hasAC, bool hasPC, bool hasMSP, bool acpcValid);

    // 更新靶点/进针点的双坐标显示
    void updateCoordinateDisplay(
        bool hasTarget, const double targetWorld[3], const double targetAcPc[3],
        bool hasEntry,  const double entryWorld[3],  const double entryAcPc[3],
        bool acpcValid);

    // 设置当前激活的标定按钮样式 (0=none, 1=AC, 2=PC, 3=MSP)
    void setAcPcActiveMode(int mode);

    // 更新单个解剖点的 World 坐标显示 (idx: 0=AC,1=PC,2=MSP; isPreview=悬停预览)
    void updateAcPcPointWorld(int idx, bool has, const double world[3], bool isPreview);

    // 更新单个解剖点的 AC-PC 坐标显示
    void updateAcPcPointAcpc(int idx, bool has, const double acpc[3]);

    // 更新十字线位置显示
    void updateCrosshairDisplay(bool visible, const double world[3], const double* acpc);

signals:
    // --- 文件操作信号 ---
    void sigOpenDicom();
    void sigImportLabel();

    // --- 视图控制信号 ---
    void sigToggleLabel();
    void sigToggleVolume();
    void sigFocusROI();

    // --- 导航跳转信号 ---
    void sigJumpTarget();
    void sigJumpEntry();

    // --- 3D 参数调整信号 ---
    void sig3DParamsChanged(double shell, double internal, double edge);
    void sigReset3DParams();

    // --- 插针信号 ---
    void sigToggleNeedle();

    void sigSetSolidMode(bool isSolid);
    void sigEnableROI(bool enable);
    void sigResetROI();
    void sigHideRoiBox(bool hide);
    void sigShowPlane(int axis, bool show);
    void sigResetPlanes();
    void sigShowPlaneArrows(bool show);
    void sigShowOrientationBox(bool show);

    void sigToggleTrajectoryMode();
    void sigSnapToNeedle();

    // 【新增阶段A】分析请求信号 (已废弃手动触发，但保留以备后用)
    void sigRunAnalysis();

    // --- AC-PC 坐标系标定信号 ---
    void sigStartCalibrateAC();
    void sigStartCalibratePC();
    void sigStartCalibrateMSP();
    void sigResetAcPc();
    // 键盘输入确认信号 (idx: 0=AC,1=PC,2=MSP)
    void sigConfirmAcPcInput(int idx, double x, double y, double z);

    // 十字线相关信号
    void sigCrosshairSetTarget();
    void sigCrosshairSetEntry();
    void sigCrosshairLinkToggled(bool linked);

    // 重置 2D 视图窗宽窗位
    void sigResetWindowLevel();

    // 【调试辅助】一键填入默认 AC-PC 坐标
    void sigSetDefaultAcPc();

    // 【调试辅助】一键填入 FEM 测试参数 (target + 触点极性 + 刺激参数)
    void sigFillTestParams();

private slots:
    // 内部槽函数：当滑块变动时，收集所有值并发射 sig3DParamsChanged
    void onSliderChanged();

private:
    void setupUi();

    // === 已有 UI 控件 ===
    QToolBox* toolBox;

    // 3D 参数控件
    QSlider* sliderShell;
    QSlider* sliderInternal;
    QSlider* sliderEdge;
    QLabel* lblShellVal;
    QLabel* lblInternalVal;
    QLabel* lblEdgeVal;

public:
    // 公开按钮指针，方便主窗口改文字
    QPushButton* btnToggleNeedle;
    QPushButton* btnToggleTrajectoryMode;
    QPushButton* btnSnapToNeedle;

private:
    // 【新增阶段A】临床分析 UI 控件
    QProgressBar* barCoverages[3];
    QLabel* lblDistances[4];
    QLabel* lblRecommendation;

    // --- AC-PC 标定 UI 控件 ---
    QPushButton* btnCalibrateAC;   // 0=AC
    QPushButton* btnCalibratePC;   // 1=PC
    QPushButton* btnCalibrateMSP;  // 2=MSP
    QPushButton* btnInputAC;
    QPushButton* btnInputPC;
    QPushButton* btnInputMSP;
    QLabel*       lblAcPcSystemStatus;

    // 每个解剖点行内的 QStackedWidget (page0=文本显示, page1=输入编辑)
    QStackedWidget* m_pointStack[3];  // 0=AC,1=PC,2=MSP
    QLabel*         m_lblPointWorld[3];
    QLabel*         m_lblPointAcpc[3];
    QDoubleSpinBox* m_edPoint[3][3];  // [pointIdx][0=x,1=y,2=z]

    // --- 靶点/进针点坐标显示 ---
    QLabel* lblTargetWorld;
    QLabel* lblTargetAcPc;
    QLabel* lblEntryWorld;
    QLabel* lblEntryAcPc;

    // --- 十字线位置显示 ---
    QLabel* m_lblCrosshairWorld;
    QLabel* m_lblCrosshairAcPc;
    QPushButton* m_btnCrosshairSetTarget;
    QPushButton* m_btnCrosshairSetEntry;
    QCheckBox*   m_chkCrosshairLink;
    QWidget*     m_crosshairGroup;
};

#endif // CONTROL_PANEL_H
