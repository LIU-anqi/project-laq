#include "control_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDebug>
#include <QCheckBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>


ControlPanel::ControlPanel(QWidget* parent) : QWidget(parent)
{
    setupUi();
}

void ControlPanel::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // 创建工具箱 (折叠面板)
    toolBox = new QToolBox(this);
    // 设置 ToolBox 样式 (深色风)
    toolBox->setStyleSheet(
        "QToolBox::tab { background: #444; color: white; border-radius: 2px; }"
        "QToolBox::tab:selected { background: #666; font-weight: bold; }"
    );
    mainLayout->addWidget(toolBox);

    // ============================================================
    // 页面 1: 数据与视图 (Data & View)
    // ============================================================
    QWidget* pageData = new QWidget();
    QVBoxLayout* layoutData = new QVBoxLayout(pageData);
    layoutData->setSpacing(10);

    QPushButton* btnOpen = new QPushButton("打开图像");
    QPushButton* btnImport = new QPushButton("导入标签");
    QPushButton* btnToggleLabel = new QPushButton("开/关标签");
    QPushButton* btnToggleVol = new QPushButton("显示/隐藏原图");
    QPushButton* btnFocus = new QPushButton("聚焦 ROI");

    // 统一高度
    int h = 32;
    btnOpen->setFixedHeight(h); btnImport->setFixedHeight(h);
    btnToggleLabel->setFixedHeight(h); btnToggleVol->setFixedHeight(h);
    btnFocus->setFixedHeight(h);

    layoutData->addWidget(btnOpen);
    layoutData->addWidget(btnImport);

    QHBoxLayout* h1 = new QHBoxLayout;
    h1->addWidget(btnToggleLabel);
    h1->addWidget(btnToggleVol);
    layoutData->addLayout(h1);

    layoutData->addWidget(btnFocus);
    layoutData->addStretch(); // 顶上去

    toolBox->addItem(pageData, "数据与视图管理");

    // 连接信号
    connect(btnOpen, &QPushButton::clicked, this, &ControlPanel::sigOpenDicom);
    connect(btnImport, &QPushButton::clicked, this, &ControlPanel::sigImportLabel);
    connect(btnToggleLabel, &QPushButton::clicked, this, &ControlPanel::sigToggleLabel);
    connect(btnToggleVol, &QPushButton::clicked, this, &ControlPanel::sigToggleVolume);
    connect(btnFocus, &QPushButton::clicked, this, &ControlPanel::sigFocusROI);

	// --- 3D 切片显示的复选框 ---
    QGroupBox* grpPlanes = new QGroupBox("3D 切片显示");
    QVBoxLayout* vLayoutPlanes = new QVBoxLayout(grpPlanes);
    QHBoxLayout* hLayoutPlanes = new QHBoxLayout();
    QCheckBox* chkAxial = new QCheckBox("轴(红)");
    QCheckBox* chkCoronal = new QCheckBox("冠(绿)");
    QCheckBox* chkSagittal = new QCheckBox("矢(黄)");
    // 默认都选中 (因为代码里默认 On)
    chkAxial->setChecked(false);
    chkCoronal->setChecked(false);
    chkSagittal->setChecked(false);
    hLayoutPlanes->addWidget(chkAxial);
    hLayoutPlanes->addWidget(chkCoronal);
    hLayoutPlanes->addWidget(chkSagittal);
    vLayoutPlanes->addLayout(hLayoutPlanes);

    QHBoxLayout* hLayoutPlaneOpts = new QHBoxLayout();
    QCheckBox* chkShowArrows = new QCheckBox("箭头");
    QCheckBox* chkShowOrientBox = new QCheckBox("方向框");
    chkShowArrows->setChecked(true);
    chkShowOrientBox->setChecked(true);
    hLayoutPlaneOpts->addWidget(chkShowArrows);
    hLayoutPlaneOpts->addWidget(chkShowOrientBox);
    vLayoutPlanes->addLayout(hLayoutPlaneOpts);

    layoutData->addWidget(grpPlanes); // 加到页面布局里

    // 连接信号
    connect(chkAxial, &QCheckBox::toggled, [this](bool c) { emit sigShowPlane(0, c); });
    connect(chkCoronal, &QCheckBox::toggled, [this](bool c) { emit sigShowPlane(1, c); });
    connect(chkSagittal, &QCheckBox::toggled, [this](bool c) { emit sigShowPlane(2, c); });
    connect(chkShowArrows, &QCheckBox::toggled, this, &ControlPanel::sigShowPlaneArrows);
    connect(chkShowOrientBox, &QCheckBox::toggled, this, &ControlPanel::sigShowOrientationBox);

    // 【新增】
    QPushButton* btnResetPlanes = new QPushButton("复位 3D 切面");
    btnResetPlanes->setToolTip("将所有切面恢复到默认的正交方向和中心位置");
    layoutData->addWidget(btnResetPlanes); // 加到布局
    // 连接信号 (记得去头文件定义 sigResetPlanes)
    connect(btnResetPlanes, &QPushButton::clicked, this, &ControlPanel::sigResetPlanes);

    // ============================================================
    // 页面 2: 3D 渲染微调 (Rendering)
    // ============================================================  
    QWidget* pageRender = new QWidget();
    QVBoxLayout* layoutRender = new QVBoxLayout(pageRender);

    // 1. Shell
    QHBoxLayout* r1 = new QHBoxLayout;
    r1->addWidget(new QLabel("外壳:"));
    sliderShell = new QSlider(Qt::Horizontal); sliderShell->setRange(0, 100); sliderShell->setValue(80);
    //sliderShell = new QSlider(Qt::Horizontal); sliderShell->setRange(-50, 50); sliderShell->setValue(0);
    lblShellVal = new QLabel("0.8"); lblShellVal->setFixedWidth(30);
    r1->addWidget(sliderShell); r1->addWidget(lblShellVal);
    layoutRender->addLayout(r1);

    // 2. Internal
    QHBoxLayout* r2 = new QHBoxLayout;
    r2->addWidget(new QLabel("内部:"));
    sliderInternal = new QSlider(Qt::Horizontal); sliderInternal->setRange(0, 100); sliderInternal->setValue(20);
    //sliderInternal = new QSlider(Qt::Horizontal); sliderInternal->setRange(10, 500); sliderInternal->setValue(100);
    lblInternalVal = new QLabel("0.2"); lblInternalVal->setFixedWidth(30);
    r2->addWidget(sliderInternal); r2->addWidget(lblInternalVal);
    layoutRender->addLayout(r2);

    // 3. Edge
    QHBoxLayout* r3 = new QHBoxLayout;
    r3->addWidget(new QLabel("边缘:"));
    sliderEdge = new QSlider(Qt::Horizontal); sliderEdge->setRange(1, 100); sliderEdge->setValue(10);
    //sliderEdge = new QSlider(Qt::Horizontal); sliderEdge->setRange(0, 100); sliderEdge->setValue(60);
    lblEdgeVal = new QLabel("0.10"); lblEdgeVal->setFixedWidth(30);
    r3->addWidget(sliderEdge); r3->addWidget(lblEdgeVal);
    layoutRender->addLayout(r3);

    // 4. Reset
    QPushButton* btnReset = new QPushButton("重置默认参数");
    layoutRender->addWidget(btnReset);
    layoutRender->addStretch();

    toolBox->addItem(pageRender, "3D 渲染微调");

    // 连接滑块信号到内部槽，统一处理
    connect(sliderShell, &QSlider::valueChanged, this, &ControlPanel::onSliderChanged);
    connect(sliderInternal, &QSlider::valueChanged, this, &ControlPanel::onSliderChanged);
    connect(sliderEdge, &QSlider::valueChanged, this, &ControlPanel::onSliderChanged);
    connect(btnReset, &QPushButton::clicked, this, &ControlPanel::sigReset3DParams);


    // ============================================================
    // 页面 3: 路径规划 (Planning)
    // ============================================================
    QWidget* pagePlan = new QWidget();
    QVBoxLayout* layoutPlan = new QVBoxLayout(pagePlan);

    QPushButton* btnJumpTarget = new QPushButton("跳转: 靶点平面");
    QPushButton* btnJumpEntry = new QPushButton("跳转: 进针点平面");
    btnToggleNeedle = new QPushButton("生成手术针");
    btnToggleNeedle->setStyleSheet("QPushButton { background-color: #E6A23C; color: white; }");

    btnJumpTarget->setFixedHeight(h);
    btnJumpEntry->setFixedHeight(h);
    btnToggleNeedle->setFixedHeight(h);

    layoutPlan->addWidget(btnJumpTarget);
    layoutPlan->addWidget(btnJumpEntry);
    layoutPlan->addWidget(btnToggleNeedle);
    layoutPlan->addStretch();

    btnToggleTrajectoryMode = new QPushButton("开启轨迹对齐视角");
    btnToggleTrajectoryMode->setStyleSheet("QPushButton { background-color: #409EFF; color: white; }");
    btnToggleTrajectoryMode->setFixedHeight(h);
    layoutPlan->addWidget(btnToggleTrajectoryMode);

    // 【新增】回到中心按钮
    btnSnapToNeedle = new QPushButton("回到手术针中心");
    btnSnapToNeedle->setStyleSheet("QPushButton { background-color: #67C23A; color: white; }"); // 绿色按钮
    btnSnapToNeedle->setFixedHeight(h);
    layoutPlan->addWidget(btnSnapToNeedle);

    // 【新增】重置 2D 窗宽窗位按钮
    QPushButton* btnResetWL = new QPushButton("重置窗宽窗位");
    btnResetWL->setStyleSheet("QPushButton { background-color: #909399; color: white; }");
    btnResetWL->setFixedHeight(h);
    layoutPlan->addWidget(btnResetWL);

    // 【调试辅助】填入 FEM 测试参数按钮
    QPushButton* btnFillTest = new QPushButton("🧪 填入 FEM 测试参数");
    btnFillTest->setToolTip(
        "target=(167.114, 235.788, 124.18)\n"
        "Contact 2 阴极 -3V, 60μs\n"
        "用于快速验证 P0 修复效果");
    btnFillTest->setStyleSheet(
        "QPushButton { background-color: #F59E0B; color: white; border-radius: 3px; }"
        "QPushButton:hover { background-color: #FBBF24; }");
    btnFillTest->setFixedHeight(h);
    layoutPlan->addWidget(btnFillTest);

    toolBox->addItem(pagePlan, "路径规划");

    connect(btnJumpTarget, &QPushButton::clicked, this, &ControlPanel::sigJumpTarget);
    connect(btnJumpEntry, &QPushButton::clicked, this, &ControlPanel::sigJumpEntry);
    connect(btnToggleNeedle, &QPushButton::clicked, this, &ControlPanel::sigToggleNeedle);
    connect(btnToggleTrajectoryMode, &QPushButton::clicked, this, &ControlPanel::sigToggleTrajectoryMode);
    connect(btnSnapToNeedle, &QPushButton::clicked, this, &ControlPanel::sigSnapToNeedle);
    connect(btnResetWL, &QPushButton::clicked, this, &ControlPanel::sigResetWindowLevel);
    connect(btnFillTest, &QPushButton::clicked, this, &ControlPanel::sigFillTestParams);

    // ============================================================
    // 页面 4: ROI 裁剪设置
    // ============================================================
    QWidget* pageROI = new QWidget();

    // 1. 这里定义了 layoutROI
    QVBoxLayout* layoutROI = new QVBoxLayout(pageROI);
    layoutROI->setSpacing(15);

    // --- A. 启用/禁用 ROI 裁剪的开关 ---
    QCheckBox* chkEnableROI = new QCheckBox("启用 ROI 裁剪框");
    chkEnableROI->setChecked(false);
    chkEnableROI->setStyleSheet("QCheckBox { font-size: 14px; color: white; }");
    layoutROI->addWidget(chkEnableROI);

    // --- B. 【新增】实体显示开关 (解决你看不到切面纹理的问题) ---
    QCheckBox* chkSolidMode = new QCheckBox("启用实体切面模式 (Solid)");
    chkSolidMode->setChecked(false);
    // 高亮显示，提醒医生这个模式看切面更清楚
    chkSolidMode->setStyleSheet("QCheckBox { color: #87CEFA; font-weight: bold; }");
    layoutROI->addWidget(chkSolidMode);

    // 【新增】
    QCheckBox* chkHideBox = new QCheckBox("隐藏绿色边框 (保留裁剪效果)");
    chkHideBox->setStyleSheet("color: #FFD700;"); // 金色，醒目一点
    layoutROI->addWidget(chkHideBox);

    // --- C. 重置按钮 ---
    QPushButton* btnResetROI = new QPushButton("重置裁剪框范围");
    btnResetROI->setFixedHeight(32);
    layoutROI->addWidget(btnResetROI);

    // --- D. 说明文本 ---
    QLabel* lblTip = new QLabel("提示:\n1. 勾选'实体模式'可清晰观察截面解剖结构\n2. 拖动绿色框壁缩放，拖动中心移动");
    lblTip->setStyleSheet("color: #aaa; font-style: italic; font-size: 12px;");
    lblTip->setWordWrap(true);
    layoutROI->addWidget(lblTip);

    layoutROI->addStretch(); // 顶上去

    // 将页面加入工具箱
    toolBox->addItem(pageROI, "ROI 裁剪设置");

    // ============================================================
    // 连接信号 (Signal Connections)
    // 1. ROI 开关
    connect(chkEnableROI, &QCheckBox::toggled, this, &ControlPanel::sigEnableROI);

    // 2. 实体模式开关 (连接到我们在 control_panel.h 新加的信号)
    connect(chkSolidMode, &QCheckBox::toggled, this, &ControlPanel::sigSetSolidMode);

    // 3. 重置按钮
    connect(btnResetROI, &QPushButton::clicked, this, &ControlPanel::sigResetROI);

    // ============================================================
    // 【新增阶段A】页面 X: 临床效果分析 (Clinical Analysis)
    // ============================================================
    QWidget* pageAnalysis = new QWidget();
    QVBoxLayout* layoutAnalysis = new QVBoxLayout(pageAnalysis);
    layoutAnalysis->setSpacing(10);
    
    // 1. VTA 覆盖率 (3个核团并排)
    QGroupBox* grpCoverage = new QGroupBox("VTA 各核团覆盖率");
    QHBoxLayout* lCoverage = new QHBoxLayout(grpCoverage);
    
    QString labels[3] = {"核团 1", "核团 2", "核团 3"};
    QString colors[3] = {"#F56C6C", "#409EFF", "#67C23A"};
    
    for (int i = 0; i < 3; i++) {
        QVBoxLayout* vBox = new QVBoxLayout;
        QLabel* lblTitle = new QLabel(labels[i]);
        lblTitle->setAlignment(Qt::AlignCenter);
        vBox->addWidget(lblTitle);
        
        barCoverages[i] = new QProgressBar();
        barCoverages[i]->setRange(0, 100);
        barCoverages[i]->setValue(0);
        barCoverages[i]->setTextVisible(true);
        barCoverages[i]->setStyleSheet(QString("QProgressBar { border: 1px solid grey; border-radius: 3px; text-align: center; color: black; font-weight: bold; } "
                                   "QProgressBar::chunk { background-color: %1; width: 5px; }").arg(colors[i]));
        vBox->addWidget(barCoverages[i]);
        lCoverage->addLayout(vBox);
    }
    layoutAnalysis->addWidget(grpCoverage);
    
    // 2. 触点距离探测器
    QGroupBox* grpDistance = new QGroupBox("触点至核团表面最短距离 (几何位置)");
    QFormLayout* lDistance = new QFormLayout(grpDistance);
    for (int i = 0; i < 4; i++) {
        lblDistances[i] = new QLabel("N/A");
        lDistance->addRow(QString("Contact %1:").arg(i), lblDistances[i]);
    }
    layoutAnalysis->addWidget(grpDistance);
    
    // 3. 智能推荐
    lblRecommendation = new QLabel("⭐ 推荐触点: 暂无");
    lblRecommendation->setStyleSheet("QLabel { font-weight: bold; color: #E6A23C; }");
    layoutAnalysis->addWidget(lblRecommendation);
    
    layoutAnalysis->addStretch();
    toolBox->addItem(pageAnalysis, "临床效果分析 (VTA)");

    // ============================================================
    // 页面 5: AC-PC 坐标系标定 (Stereotactic Calibration)
    // ============================================================
    QWidget* pageAcPc = new QWidget();
    QVBoxLayout* layoutAcPc = new QVBoxLayout(pageAcPc);
    layoutAcPc->setSpacing(6);
    layoutAcPc->setContentsMargins(6, 6, 6, 6);

    // --- A. 操作说明 ---
    QLabel* lblAcPcHint = new QLabel(
        "1. 点击鼠标标定 → 移至二维视图 → Ctrl+左键落点\n"
        "2. 或点击键入坐标 → 输入 World XYZ → 确认");
    lblAcPcHint->setStyleSheet("color: #aaa; font-size: 11px;");
    lblAcPcHint->setWordWrap(true);
    layoutAcPc->addWidget(lblAcPcHint);

    // --- B. 三个解剖点，横向三列并排 ---
    const char* pointNames[3] = { "AC", "PC", "MSP" };
    const char* pointColors[3] = { "#00CCFF", "#FF8800", "#FFFF00" };

    QPushButton** calibBtns[3] = { &btnCalibrateAC, &btnCalibratePC, &btnCalibrateMSP };
    QPushButton** inputBtns[3] = { &btnInputAC, &btnInputPC, &btnInputMSP };

    QGroupBox* grpCalib = new QGroupBox("解剖点标定");
    QHBoxLayout* layoutCalib = new QHBoxLayout(grpCalib);
    layoutCalib->setSpacing(4);
    layoutCalib->setContentsMargins(4, 4, 4, 4);

    for (int pi = 0; pi < 3; pi++) {
        // --- 每列一个纵向小面板 ---
        QWidget* col = new QWidget();
        QVBoxLayout* colLayout = new QVBoxLayout(col);
        colLayout->setSpacing(3);
        colLayout->setContentsMargins(2, 2, 2, 2);

        // 点名标签
        QLabel* lblName = new QLabel(pointNames[pi]);
        lblName->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 12px;").arg(pointColors[pi]));
        lblName->setAlignment(Qt::AlignCenter);
        colLayout->addWidget(lblName);

        // 两个按钮横排
        QHBoxLayout* btnRow = new QHBoxLayout();
        btnRow->setSpacing(2);
        *calibBtns[pi] = new QPushButton("标定");
        (*calibBtns[pi])->setStyleSheet(
            "QPushButton { background-color: #409EFF; color: white; border-radius: 3px; padding: 2px 4px; font-size: 10px; }"
            "QPushButton:hover { background-color: #66B1FF; }");
        btnRow->addWidget(*calibBtns[pi]);

        *inputBtns[pi] = new QPushButton("键入");
        (*inputBtns[pi])->setStyleSheet(
            "QPushButton { background-color: #606266; color: white; border-radius: 3px; padding: 2px 4px; font-size: 10px; }"
            "QPushButton:hover { background-color: #909399; }");
        btnRow->addWidget(*inputBtns[pi]);
        colLayout->addLayout(btnRow);

        // --- QStackedWidget: page0=文本显示, page1=输入编辑 ---
        m_pointStack[pi] = new QStackedWidget();

        // Page 0: 文本显示
        QWidget* displayPage = new QWidget();
        QVBoxLayout* dispLayout = new QVBoxLayout(displayPage);
        dispLayout->setSpacing(1);
        dispLayout->setContentsMargins(0, 0, 0, 0);

        m_lblPointWorld[pi] = new QLabel("W: --");
        m_lblPointWorld[pi]->setStyleSheet("color: #C0C0C0; font-size: 10px;");
        m_lblPointWorld[pi]->setAlignment(Qt::AlignCenter);
        m_lblPointAcpc[pi]  = new QLabel("AP: --");
        m_lblPointAcpc[pi]->setStyleSheet("color: #909399; font-size: 10px;");
        m_lblPointAcpc[pi]->setAlignment(Qt::AlignCenter);
        dispLayout->addWidget(m_lblPointWorld[pi]);
        dispLayout->addWidget(m_lblPointAcpc[pi]);
        m_pointStack[pi]->addWidget(displayPage);  // index 0

        // Page 1: 输入编辑
        QWidget* editPage = new QWidget();
        QVBoxLayout* editLayout = new QVBoxLayout(editPage);
        editLayout->setSpacing(2);
        editLayout->setContentsMargins(0, 0, 0, 0);

        QVBoxLayout* spinCol = new QVBoxLayout();
        spinCol->setSpacing(1);
        const char* axisLabels[3] = { "X:", "Y:", "Z:" };
        for (int ai = 0; ai < 3; ai++) {
            QHBoxLayout* axisRow = new QHBoxLayout();
            axisRow->setSpacing(1);
            QLabel* lblAxis = new QLabel(axisLabels[ai]);
            lblAxis->setStyleSheet("font-size: 10px;");
            lblAxis->setFixedWidth(14);
            axisRow->addWidget(lblAxis);
            m_edPoint[pi][ai] = new QDoubleSpinBox();
            m_edPoint[pi][ai]->setRange(-500.0, 500.0);
            m_edPoint[pi][ai]->setDecimals(2);
            m_edPoint[pi][ai]->setSingleStep(0.5);
            m_edPoint[pi][ai]->setMinimumWidth(50);
            m_edPoint[pi][ai]->setStyleSheet("font-size: 10px; padding: 1px 2px;");
            axisRow->addWidget(m_edPoint[pi][ai]);
            spinCol->addLayout(axisRow);
        }
        editLayout->addLayout(spinCol);

        QHBoxLayout* confirmRow = new QHBoxLayout();
        confirmRow->setSpacing(2);
        QPushButton* btnConfirm = new QPushButton("✓");
        btnConfirm->setStyleSheet(
            "QPushButton { background-color: #67C23A; color: white; border-radius: 3px; padding: 1px 6px; font-size: 10px; }");
        QPushButton* btnCancel = new QPushButton("✕");
        btnCancel->setStyleSheet(
            "QPushButton { background-color: #909399; color: white; border-radius: 3px; padding: 1px 6px; font-size: 10px; }");
        confirmRow->addStretch();
        confirmRow->addWidget(btnConfirm);
        confirmRow->addWidget(btnCancel);
        editLayout->addLayout(confirmRow);
        m_pointStack[pi]->addWidget(editPage);  // index 1

        colLayout->addWidget(m_pointStack[pi]);
        layoutCalib->addWidget(col, 1);

        // --- 连接信号（捕获 pi 的值）---
        const int capturedPi = pi;
        connect(*inputBtns[pi], &QPushButton::clicked, [this, capturedPi]() {
            m_pointStack[capturedPi]->setCurrentIndex(1);
        });
        connect(btnCancel, &QPushButton::clicked, [this, capturedPi]() {
            m_pointStack[capturedPi]->setCurrentIndex(0);
        });
        connect(btnConfirm, &QPushButton::clicked, [this, capturedPi]() {
            double x = m_edPoint[capturedPi][0]->value();
            double y = m_edPoint[capturedPi][1]->value();
            double z = m_edPoint[capturedPi][2]->value();
            m_pointStack[capturedPi]->setCurrentIndex(0);
            emit sigConfirmAcPcInput(capturedPi, x, y, z);
        });
    }
    layoutAcPc->addWidget(grpCalib);

    // --- C. 系统状态 + 重置 ---
    QHBoxLayout* statusRow = new QHBoxLayout();
    lblAcPcSystemStatus = new QLabel("坐标系: 未建立");
    lblAcPcSystemStatus->setStyleSheet("font-weight: bold; color: #E6A23C;");
    statusRow->addWidget(lblAcPcSystemStatus, 1);
    QPushButton* btnResetAcPc = new QPushButton("重置");
    btnResetAcPc->setStyleSheet(
        "QPushButton { background-color: #F56C6C; color: white; border-radius: 3px; padding: 3px 8px; }");
    statusRow->addWidget(btnResetAcPc);
    layoutAcPc->addLayout(statusRow);

    // --- C2. 默认 AC-PC 按钮 ---
    QPushButton* btnDefaultAcPc = new QPushButton("📐 填入默认 AC-PC 坐标");
    btnDefaultAcPc->setToolTip(
        "AC: (185, 250, 142)\n"
        "PC: (182, 227, 141)\n"
        "MSP: (184, 238.59, 141.32)");
    btnDefaultAcPc->setStyleSheet(
        "QPushButton { background-color: #8B5CF6; color: white; border-radius: 3px; padding: 4px 8px; font-size: 11px; }"
        "QPushButton:hover { background-color: #A78BFA; }");
    layoutAcPc->addWidget(btnDefaultAcPc);
    connect(btnDefaultAcPc, &QPushButton::clicked, this, &ControlPanel::sigSetDefaultAcPc);

    // --- D. 五点坐标显示区 ---
    QGroupBox* grpCoords = new QGroupBox("坐标实时显示");
    QFormLayout* formCoords = new QFormLayout(grpCoords);
    formCoords->setSpacing(3);
    formCoords->setContentsMargins(6, 4, 6, 4);

    QLabel* lblTW = new QLabel("靶点 World:");
    lblTW->setStyleSheet("color: #FF6666; font-weight: bold; font-size: 11px;");
    lblTargetWorld = new QLabel("N/A");
    lblTargetWorld->setStyleSheet("color: #C0C0C0; font-size: 11px;");
    formCoords->addRow(lblTW, lblTargetWorld);

    QLabel* lblTA = new QLabel("靶点 AC-PC:");
    lblTA->setStyleSheet("color: #FF6666; font-size: 11px;");
    lblTargetAcPc = new QLabel("N/A");
    lblTargetAcPc->setStyleSheet("color: #909399; font-size: 11px;");
    formCoords->addRow(lblTA, lblTargetAcPc);

    QLabel* lblEW = new QLabel("进针点 World:");
    lblEW->setStyleSheet("color: #66FF66; font-weight: bold; font-size: 11px;");
    lblEntryWorld = new QLabel("N/A");
    lblEntryWorld->setStyleSheet("color: #C0C0C0; font-size: 11px;");
    formCoords->addRow(lblEW, lblEntryWorld);

    QLabel* lblEA = new QLabel("进针点 AC-PC:");
    lblEA->setStyleSheet("color: #66FF66; font-size: 11px;");
    lblEntryAcPc = new QLabel("N/A");
    lblEntryAcPc->setStyleSheet("color: #909399; font-size: 11px;");
    formCoords->addRow(lblEA, lblEntryAcPc);

    layoutAcPc->addWidget(grpCoords);

    // --- E. 十字线位置与操作 ---
    m_crosshairGroup = new QGroupBox("十字线位置");
    QVBoxLayout* layoutCH = new QVBoxLayout(m_crosshairGroup);
    layoutCH->setSpacing(3);
    layoutCH->setContentsMargins(6, 4, 6, 4);

    m_lblCrosshairWorld = new QLabel("World: --");
    m_lblCrosshairWorld->setStyleSheet("color: #FFE666; font-size: 11px;");
    layoutCH->addWidget(m_lblCrosshairWorld);

    m_lblCrosshairAcPc = new QLabel("AC-PC: --");
    m_lblCrosshairAcPc->setStyleSheet("color: #C0C0C0; font-size: 11px;");
    layoutCH->addWidget(m_lblCrosshairAcPc);

    QHBoxLayout* chBtnRow = new QHBoxLayout();
    chBtnRow->setSpacing(4);
    m_btnCrosshairSetTarget = new QPushButton("设为靶点");
    m_btnCrosshairSetTarget->setStyleSheet(
        "QPushButton { background-color: #E6A23C; color: white; border-radius: 3px; padding: 3px 6px; font-size: 11px; }"
        "QPushButton:hover { background-color: #F0C060; }");
    m_btnCrosshairSetEntry = new QPushButton("设为进针点");
    m_btnCrosshairSetEntry->setStyleSheet(
        "QPushButton { background-color: #67C23A; color: white; border-radius: 3px; padding: 3px 6px; font-size: 11px; }"
        "QPushButton:hover { background-color: #85CE61; }");
    chBtnRow->addWidget(m_btnCrosshairSetTarget);
    chBtnRow->addWidget(m_btnCrosshairSetEntry);
    layoutCH->addLayout(chBtnRow);

    m_chkCrosshairLink = new QCheckBox("三视图联动");
    m_chkCrosshairLink->setStyleSheet("color: #C0C0C0; font-size: 11px;");
    m_chkCrosshairLink->setChecked(true);
    layoutCH->addWidget(m_chkCrosshairLink);

    m_crosshairGroup->setVisible(false);
    layoutAcPc->addWidget(m_crosshairGroup);

    connect(m_btnCrosshairSetTarget, &QPushButton::clicked, this, &ControlPanel::sigCrosshairSetTarget);
    connect(m_btnCrosshairSetEntry,  &QPushButton::clicked, this, &ControlPanel::sigCrosshairSetEntry);
    connect(m_chkCrosshairLink, &QCheckBox::toggled, this, &ControlPanel::sigCrosshairLinkToggled);

    layoutAcPc->addStretch();

    toolBox->addItem(pageAcPc, "坐标系标定 (AC-PC)");

    // --- 连接鼠标标定信号 ---
    connect(btnCalibrateAC,  &QPushButton::clicked, this, &ControlPanel::sigStartCalibrateAC);
    connect(btnCalibratePC,  &QPushButton::clicked, this, &ControlPanel::sigStartCalibratePC);
    connect(btnCalibrateMSP, &QPushButton::clicked, this, &ControlPanel::sigStartCalibrateMSP);
    connect(btnResetAcPc,    &QPushButton::clicked, this, &ControlPanel::sigResetAcPc);
}

void ControlPanel::onSliderChanged()
{
    double shell = sliderShell->value() / 100.0;
    double internal = sliderInternal->value() / 100.0;
    double edge = sliderEdge->value() / 100.0;

    lblShellVal->setText(QString::number(shell, 'f', 2));
    lblInternalVal->setText(QString::number(internal, 'f', 2));
    lblEdgeVal->setText(QString::number(edge, 'f', 2));

    emit sig3DParamsChanged(shell, internal, edge);
}

void ControlPanel::set3DParamsUI(double shell, double internal, double edge)
{
    sliderShell->blockSignals(true);
    sliderInternal->blockSignals(true);
    sliderEdge->blockSignals(true);

    sliderShell->setValue(shell * 100);
    sliderInternal->setValue(internal * 100);
    sliderEdge->setValue(edge * 100);

    lblShellVal->setText(QString::number(shell, 'f', 2));
    lblInternalVal->setText(QString::number(internal, 'f', 2));
    lblEdgeVal->setText(QString::number(edge, 'f', 2));

    sliderShell->blockSignals(false);
    sliderInternal->blockSignals(false);
    sliderEdge->blockSignals(false);
}

// 更新 AC-PC 标定状态显示
void ControlPanel::updateAcPcStatusUI(bool hasAC, bool hasPC, bool hasMSP, bool acpcValid)
{
    const QString styleCalibrated =
        "QPushButton { background-color: #67C23A; color: white; border-radius: 3px; padding: 3px 6px; font-size: 11px; }"
        "QPushButton:hover { background-color: #85CE61; }";
    const QString styleDefault =
        "QPushButton { background-color: #409EFF; color: white; border-radius: 3px; padding: 3px 6px; font-size: 11px; }"
        "QPushButton:hover { background-color: #66B1FF; }";

    btnCalibrateAC->setStyleSheet(hasAC   ? styleCalibrated : styleDefault);
    btnCalibratePC->setStyleSheet(hasPC   ? styleCalibrated : styleDefault);
    btnCalibrateMSP->setStyleSheet(hasMSP ? styleCalibrated : styleDefault);

    if (acpcValid) {
        lblAcPcSystemStatus->setText("坐标系: ✔ 已建立");
        lblAcPcSystemStatus->setStyleSheet("font-weight: bold; color: #67C23A;");
    } else {
        int count = (hasAC ? 1 : 0) + (hasPC ? 1 : 0) + (hasMSP ? 1 : 0);
        lblAcPcSystemStatus->setText(
            QString("坐标系: 未建立 (%1/3 点已标定)").arg(count));
        lblAcPcSystemStatus->setStyleSheet("font-weight: bold; color: #E6A23C;");
    }
}

// 更新靶点/进针点的世界坐标 + AC-PC 坐标显示
void ControlPanel::updateCoordinateDisplay(
    bool hasTarget, const double targetWorld[3], const double targetAcPc[3],
    bool hasEntry,  const double entryWorld[3],  const double entryAcPc[3],
    bool acpcValid)
{
    if (hasTarget) {
        lblTargetWorld->setText(QString("(%1, %2, %3)")
            .arg(targetWorld[0], 0, 'f', 2)
            .arg(targetWorld[1], 0, 'f', 2)
            .arg(targetWorld[2], 0, 'f', 2));
        if (acpcValid) {
            lblTargetAcPc->setText(QString("L%1  AP%2  Ax%3")
                .arg(targetAcPc[0], 0, 'f', 2)
                .arg(targetAcPc[1], 0, 'f', 2)
                .arg(targetAcPc[2], 0, 'f', 2));
        } else {
            lblTargetAcPc->setText("(需先建立 AC-PC 坐标系)");
        }
    } else {
        lblTargetWorld->setText("N/A");
        lblTargetAcPc->setText("N/A");
    }

    if (hasEntry) {
        lblEntryWorld->setText(QString("(%1, %2, %3)")
            .arg(entryWorld[0], 0, 'f', 2)
            .arg(entryWorld[1], 0, 'f', 2)
            .arg(entryWorld[2], 0, 'f', 2));
        if (acpcValid) {
            lblEntryAcPc->setText(QString("L%1  AP%2  Ax%3")
                .arg(entryAcPc[0], 0, 'f', 2)
                .arg(entryAcPc[1], 0, 'f', 2)
                .arg(entryAcPc[2], 0, 'f', 2));
        } else {
            lblEntryAcPc->setText("(需先建立 AC-PC 坐标系)");
        }
    } else {
        lblEntryWorld->setText("N/A");
        lblEntryAcPc->setText("N/A");
    }
}

void ControlPanel::setAcPcActiveMode(int mode)
{
    // mode: 0=none, 1=AC, 2=PC, 3=MSP
    // Active = orange pulsing border (simulated via background color change)
    const QString styleActive =
        "QPushButton { background-color: #E6A23C; color: white; border-radius: 3px; padding: 3px 6px; font-size: 11px; "
        "border: 2px solid #F5C26B; }";
    const QString styleDefault =
        "QPushButton { background-color: #409EFF; color: white; border-radius: 3px; padding: 3px 6px; font-size: 11px; }"
        "QPushButton:hover { background-color: #66B1FF; }";
    const QString styleCalibrated =
        "QPushButton { background-color: #67C23A; color: white; border-radius: 3px; padding: 3px 6px; font-size: 11px; }"
        "QPushButton:hover { background-color: #85CE61; }";

    // Reset all to their calibrated or default style first, then override active one
    // We don't have "has" state here, so we just switch active vs non-active for the activated button
    if (mode == 1) {
        btnCalibrateAC->setStyleSheet(styleActive);
    } else {
        // leave existing style (set by updateAcPcStatusUI)
    }
    if (mode == 2) {
        btnCalibratePC->setStyleSheet(styleActive);
    }
    if (mode == 3) {
        btnCalibrateMSP->setStyleSheet(styleActive);
    }
}

void ControlPanel::updateAcPcPointWorld(int idx, bool has, const double world[3], bool isPreview)
{
    if (idx < 0 || idx > 2) return;
    if (!has) {
        m_lblPointWorld[idx]->setStyleSheet("color: #606266; font-size: 11px;");
        m_lblPointWorld[idx]->setText("World: --");
        return;
    }
    QString txt = QString("World: (%1, %2, %3)")
        .arg(world[0], 0, 'f', 2)
        .arg(world[1], 0, 'f', 2)
        .arg(world[2], 0, 'f', 2);
    m_lblPointWorld[idx]->setText(txt);
    // isPreview = gray italic; confirmed = white
    if (isPreview)
        m_lblPointWorld[idx]->setStyleSheet("color: #808080; font-style: italic; font-size: 11px;");
    else
        m_lblPointWorld[idx]->setStyleSheet("color: #C0C0C0; font-size: 11px;");
}

void ControlPanel::updateAcPcPointAcpc(int idx, bool has, const double acpc[3])
{
    if (idx < 0 || idx > 2) return;
    if (!has) {
        m_lblPointAcpc[idx]->setText("AC-PC: --");
        m_lblPointAcpc[idx]->setStyleSheet("color: #606266; font-size: 11px;");
        return;
    }
    m_lblPointAcpc[idx]->setText(
        QString("AC-PC: L%1  AP%2  Ax%3")
        .arg(acpc[0], 0, 'f', 2)
        .arg(acpc[1], 0, 'f', 2)
        .arg(acpc[2], 0, 'f', 2));
    m_lblPointAcpc[idx]->setStyleSheet("color: #909399; font-size: 11px;");
}

// 【新增阶段A】提供给主窗口的 UI 更新接口
void ControlPanel::updateAnalysisUI(const std::vector<double>& coverages, const std::vector<double>& distances, int bestContactIndex)
{
    // 更新三个进度条
    for (int i = 0; i < 3 && i < coverages.size(); i++) {
        barCoverages[i]->setValue(static_cast<int>(coverages[i]));
    }

    // 更新距离文本
    for (int i = 0; i < 4 && i < distances.size(); i++) {
        if (distances[i] < 0) {
            lblDistances[i]->setText(QString("<span style='color:green;font-weight:bold;'>Inside (内部)</span>"));
        } else if (distances[i] >= 9990.0) {
            lblDistances[i]->setText("N/A");
        } else {
            lblDistances[i]->setText(QString("%1 mm").arg(distances[i], 0, 'f', 2));
        }
    }

    // 更新推荐触点
    if (bestContactIndex >= 0 && bestContactIndex < 4) {
        lblRecommendation->setText(QString("⭐ 推荐激活: Contact %1").arg(bestContactIndex));
        lblRecommendation->setStyleSheet("QLabel { font-weight: bold; color: #67C23A; }");
    } else {
        lblRecommendation->setText("⭐ 推荐触点: 无法计算");
        lblRecommendation->setStyleSheet("QLabel { font-weight: bold; color: #909399; }");
    }
}

void ControlPanel::updateCrosshairDisplay(bool visible, const double world[3], const double* acpc)
{
    m_crosshairGroup->setVisible(visible);
    if (!visible) return;

    m_lblCrosshairWorld->setText(
        QString("World: (%1, %2, %3)")
            .arg(world[0], 0, 'f', 2)
            .arg(world[1], 0, 'f', 2)
            .arg(world[2], 0, 'f', 2));

    if (acpc) {
        m_lblCrosshairAcPc->setText(
            QString("AC-PC: (%1, %2, %3)")
                .arg(acpc[0], 0, 'f', 2)
                .arg(acpc[1], 0, 'f', 2)
                .arg(acpc[2], 0, 'f', 2));
        m_lblCrosshairAcPc->setVisible(true);
    } else {
        m_lblCrosshairAcPc->setVisible(false);
    }
}
