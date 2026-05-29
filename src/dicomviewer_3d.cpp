
#include "dicomviewer_3d.h"

#include <QDebug>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QDir>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QListView>
#include <QRegularExpression>
#include <QStringList>
#include <QTreeView>
#include <QTimer>

#include <vtkAutoInit.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);      // 修复 "no override found for vtkPolyDataMapper"
VTK_MODULE_INIT(vtkInteractionStyle);      // 修复 "Link to vtkInteractionStyle"
VTK_MODULE_INIT(vtkRenderingVolumeOpenGL2); // 修复 体渲染(Volume) 可能出现的类似错误
VTK_MODULE_INIT(vtkRenderingFreeType);     // 修复 文字显示问题

#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkImageData.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolumeProperty.h>
#include <vtkPiecewiseFunction.h>
#include <vtkColorTransferFunction.h>
#include <vtkVolume.h>
#include <vtkResliceImageViewer.h>
#include <vtkImageImport.h>
#include <vtkNIFTIImageReader.h>
#include <vtkInteractorStyleImage.h>
#include <vtkActor.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSphereSource.h>
#include <vtkMath.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkImplicitPolyDataDistance.h>
#include <vtkTransform.h>
#include <vtkType.h>
#include <vtkSmartPointer.h>
#include <vtkObjectFactory.h>
#include <vtkImageReslice.h>
#include <vtkMatrix4x4.h>
#include <vtkMatrix3x3.h>
#include <vtkImageActor.h>
#include <vtkImageProperty.h>
#include <vtkDICOMImageReader.h> 

#include <vtkCamera.h>
#include <vtkResliceCursorWidget.h>
#include <vtkResliceCursorRepresentation.h>
#include <vtkResliceCursor.h>
#include <vtkImageMapper3D.h>

#include <vtkInteractorStyleTrackballCamera.h>
#include <QSplitter>
#include <vtkGPUVolumeRayCastMapper.h>

#include <vtkCallbackCommand.h> // <--- 解决 "不完整类型" 报错
#include <vtkCommand.h>         // <--- 确保 vtkCommand::StartEvent 可用
#include <vtkWidgetEventTranslator.h>
#include <vtkWidgetEvent.h> // 可能需要
#include <vtkPlane.h>
#include <vtkMath.h>
#include <vtkResliceCursorLineRepresentation.h>
#include <vtkResliceCursorActor.h>
#include <vtkProperty.h> // 用于设置 Opacity
#include <vtkOutlineFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkAppendPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyData.h>
#include <vtkPolygon.h>
#include <vtkCoordinate.h>
#include "vtk_interaction_styles.h"
#include "trajectory_manager.h"
#include <vtkLight.h>
#include <vtkCellPicker.h>
#include <vtkPropPicker.h>
#include <vtkObjectFactory.h>
#include <QComboBox>
#include "lead_simulator_widget.h"

// FEM VTA 相关
#include "dbs_fem_types.h"
#include "dbs_mesh_worker.h"
#include "dbs_sim_worker.h"
#include <QThread>
#include <vtkCellDataToPointData.h>
#include <vtkContourFilter.h>
#include <vtkFieldData.h>
#include <vtkDataArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkTriangleFilter.h>
#include <vtkMassProperties.h>
#include <vtkXMLUnstructuredGridWriter.h>

namespace {
QString phaseFProjectRoot()
{
    QStringList starts;
    starts << QDir::currentPath() << QCoreApplication::applicationDirPath();
    for (const QString& start : starts) {
        QDir dir(start);
        while (true) {
            if (dir.exists(".kiro") || dir.exists("CMakeLists.txt")) {
                return dir.absolutePath();
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }
    return QDir::currentPath();
}

QString phaseFResultPath(const QString& presetId)
{
    QDir outDir(phaseFProjectRoot() + "/out/phase_f_project");
    if (!outDir.exists()) {
        outDir.mkpath(".");
    }

    QString safeId = presetId;
    safeId.replace(QRegularExpression("[^A-Za-z0-9_-]+"), "_");
    if (safeId.isEmpty()) {
        safeId = "Manual";
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = QString("%1_%2_result.vtu").arg(safeId, stamp);
    QString path = outDir.filePath(fileName);
    int suffix = 1;
    while (QFileInfo::exists(path)) {
        fileName = QString("%1_%2_%3_result.vtu").arg(safeId, stamp).arg(suffix++);
        path = outDir.filePath(fileName);
    }
    return path;
}

bool writePhaseFResult(vtkUnstructuredGrid* result, const QString& path)
{
    if (!result) {
        return false;
    }
    vtkNew<vtkXMLUnstructuredGridWriter> writer;
    const QByteArray pathBytes = QDir::toNativeSeparators(path).toLocal8Bit();
    writer->SetFileName(pathBytes.constData());
    writer->SetInputData(result);
    writer->SetDataModeToBinary();
    return writer->Write() != 0;
}
}


dicomviewer_3d::dicomviewer_3d(QWidget* parent)
    : QMainWindow(parent)
{
    // 1. 显式初始化数据指针为空
    vtkImage = nullptr;
    vtkLabelImage = nullptr;
    m_currentVolume = nullptr;
    // 【新增初始化】
    m_trajManager = new TrajectoryManager();

    //初始化ROI管理器
    m_roiManager = new VolumeROIManager(this);

    // 2. 初始化 UI
    createUI();

    // 3. 初始化 VTK
    initVtkViews();
    initLookupTable();


}
dicomviewer_3d::~dicomviewer_3d()
{
}

QWidget* dicomviewer_3d::createSliceContainer(int viewIndex, const QString& title, const QString& color)
{
    // 1. 创建最外层容器
    QWidget* container = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0); // 留一点边距
    layout->setSpacing(2);

    // 2. 创建 VTK 显示窗口
    QVTKOpenGLNativeWidget* vtkWidget = new QVTKOpenGLNativeWidget(container);
    // 设置最小尺寸，防止被压扁
    vtkWidget->setMinimumSize(200, 200);
    // 设置焦点策略 (解决“有时候没反应”的问题)
    vtkWidget->setFocusPolicy(Qt::StrongFocus);

    // 3. 创建滑动条 (Slider)
    QSlider* slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(0, 100); // 暂时设个默认值，加载数据后会改
    slider->setEnabled(false); // 没数据时禁用
    // 设置滑动条样式 
    slider->setStyleSheet(sliderStyle);


    // 4. 组装布局
    layout->addWidget(vtkWidget, 1); // 1 表示 VTK 窗口占据主要空间
    layout->addWidget(slider);

    // 6. 【关键】将创建的指针保存到结构体中
    m_views[viewIndex].container = container;
    m_views[viewIndex].widget = vtkWidget;
    m_views[viewIndex].slider = slider;
    m_views[viewIndex].viewIndex = viewIndex;

    // 7. 【预埋】连接 Slider 信号 (UI -> VTK)
   /* connect(slider, &QSlider::valueChanged, [this, viewIndex](int value) {
        if (!m_views[viewIndex].viewer || !vtkImage) return;  

        vtkResliceImageViewer* viewer = m_views[viewIndex].viewer;
        int mode = viewer->GetResliceMode();

        if (mode == vtkResliceImageViewer::RESLICE_OBLIQUE) {
            // ==========================================================
            // 【核心修复：斜切模式下防脱钩】
            // 绝对不能调用 SetSlice(value)！这会毁掉 VTK 底层的斜切矩阵！
            // 正确做法：直接根据滑动条抽出主轴坐标，平移中心点，同时严格约束相机。
            // ==========================================================
            vtkResliceCursor* cursor = viewer->GetResliceCursor();
            if (!cursor) return;

            // 1. 获取斜切面当前的中心和法线
            double center[3], normal[3];
            cursor->GetCenter(center);
            cursor->GetPlane(2)->GetNormal(normal);

            double origin[3], spacing[3];
            vtkImage->GetOrigin(origin);
            vtkImage->GetSpacing(spacing);

            // 2. 根据滑动条的值，更新对应主轴的坐标 (保留另外两个轴不变)
            int orientation = viewer->GetSliceOrientation();
            if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) {
                center[2] = origin[2] + value * spacing[2];
            }
            else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) {
                center[1] = origin[1] + value * spacing[1];
            }
            else {
                center[0] = origin[0] + value * spacing[0];
            }

            // 3. 将完美的中心点同步给二维游标
            cursor->SetCenter(center);

            // 4. 将完美的中心点同步给三维数学平面和线框
            if (m_slicePlanes[viewIndex]) {
                m_slicePlanes[viewIndex]->SetOrigin(center);
            }
            if (m_impWidgets[viewIndex]) {
                auto rep = vtkImplicitPlaneRepresentation::SafeDownCast(m_impWidgets[viewIndex]->GetRepresentation());
                if (rep) {
                    rep->SetOrigin(center);
                    rep->Modified();
                }
            }

            // 5. 【防黑屏杀招】严格按照真实的法线方向，重新锁定二维相机的焦点和位置
            vtkCamera* cam = viewer->GetRenderer()->GetActiveCamera();
            if (cam) {
                double distance = cam->GetDistance();
                if (distance <= 0.0) distance = 100.0;

                // 相机位置 = 中心点 + 法线方向 * 距离
                double newPos[3] = {
                    center[0] + normal[0] * distance,
                    center[1] + normal[1] * distance,
                    center[2] + normal[2] * distance
                };
                cam->SetFocalPoint(center);
                cam->SetPosition(newPos);
            }
            // 刷新渲染
            viewer->GetRenderer()->ResetCameraClippingRange();
            viewer->Render();
            if (vtkWidget3D->isVisible()) vtkWidget3D->GetRenderWindow()->Render();
        }
        else {
            // ==========================================================
            // 【正交模式：走原有的正常逻辑】
            // ==========================================================
            viewer->SetSlice(value);
            updateOverlaySlice(viewIndex);
            viewer->Render();
        }
        });*/

    // 7. 【预埋】连接 Slider 信号 (UI -> VTK)
connect(slider, &QSlider::valueChanged, [this, viewIndex, slider](int value) {
    if (!m_views[viewIndex].viewer || !vtkImage) return;

    int mode = m_views[viewIndex].viewer->GetResliceMode();

    if (mode == vtkResliceImageViewer::RESLICE_OBLIQUE) {
        // 【回滚】：恢复最纯粹的 Delta 相对增量推进
        int lastValue = slider->property("last_val").isValid() ? slider->property("last_val").toInt() : value;
        int delta = value - lastValue;
        slider->setProperty("last_val", value);

        if (delta != 0) {
            slider->blockSignals(true);
            advanceSliceOblique(viewIndex, delta);
            slider->blockSignals(false);
        }
    }
    else {
        // 正交模式：走原有的正常逻辑
        m_views[viewIndex].viewer->SetSlice(value);
        updateOverlaySlice(viewIndex);
        syncCrosshairToSlice(viewIndex);
        m_views[viewIndex].viewer->Render();
    }
    });

    return container;
}

void dicomviewer_3d::createUI()
{
    // 1. 创建控制面板 (它包含了所有按钮、滑块和折叠页)
    m_controlPanel = new ControlPanel(this);
    //m_controlPanel->setFixedWidth(280); // 固定宽度，右侧看起来更整洁

    // ==========================================================
    // 【电极模拟器模块】初始化
    // ==========================================================
    m_leadSimulator = new LeadSimulatorWidget(this);
    m_leadSimulator->hide(); // 默认隐藏

    // 连接模拟器发出的参数改变信号 -> 同步给主 3D 模型
    connect(m_leadSimulator, &LeadSimulatorWidget::sigLeadTypeChanged, [this](int type) {
        if (m_trajManager && m_trajManager->m_dbsLead) {
            m_trajManager->m_dbsLead->setLeadType(type);
            refreshAllViews();
            calculateVTAIntersection();
        }
    });
    connect(m_leadSimulator, &LeadSimulatorWidget::sigAmplitudeChanged, [this](double mA) {
        if (m_trajManager && m_trajManager->m_dbsLead) {
            m_trajManager->m_dbsLead->setAmplitude(mA);
            refreshAllViews();
            calculateVTAIntersection();
        }
    });
    connect(m_leadSimulator, &LeadSimulatorWidget::sigPulseWidthChanged, [this](int us) {
        if (m_trajManager && m_trajManager->m_dbsLead) {
            m_trajManager->m_dbsLead->setPulseWidth(us);
            refreshAllViews();
            calculateVTAIntersection();
        }
    });
    connect(m_leadSimulator, &LeadSimulatorWidget::sigFrequencyChanged, [this](int hz) {
        if (m_trajManager && m_trajManager->m_dbsLead) {
            m_trajManager->m_dbsLead->setFrequency(hz);
        }
    });
    connect(m_leadSimulator, &LeadSimulatorWidget::sigShowVTA, [this](bool show) {
        if (m_trajManager && m_trajManager->m_dbsLead) {
            m_trajManager->m_dbsLead->setShowVTA(show);
            refreshAllViews();
            calculateVTAIntersection();
        }
    });
    connect(m_leadSimulator, &LeadSimulatorWidget::sigVTADisplayPresetChanged, [this](int presetIndex) {
        if (m_trajManager && m_trajManager->m_dbsLead) {
            m_trajManager->m_dbsLead->setVTADisplayPreset(presetIndex);
            refreshAllViews();
        }
    });
    connect(m_leadSimulator, &LeadSimulatorWidget::sigContactPolarityChanged, [this](int idx, int pol) {
        if (m_trajManager && m_trajManager->m_dbsLead) {
            ContactPolarity enumPol = POLARITY_OFF;
            if (pol == -1) enumPol = POLARITY_CATHODE;
            else if (pol == 1) enumPol = POLARITY_ANODE;
            m_trajManager->m_dbsLead->setContactPolarity(idx, enumPol);
            refreshAllViews();
            calculateVTAIntersection();
        }
    });
    connect(m_leadSimulator, &LeadSimulatorWidget::sigDepthChanged, [this](double mm) {
        if (m_trajManager && m_trajManager->m_dbsLead) {
            m_trajManager->m_dbsLead->setDepthOffset(mm);
            refreshAllViews();
            calculateVTAIntersection();
        }
    });
    connect(m_leadSimulator, &LeadSimulatorWidget::sigPhaseFPresetRequested,
            this, &dicomviewer_3d::slot_applyPhaseFPreset);
    connect(m_leadSimulator, &LeadSimulatorWidget::sigComputeRealVTA, this, &dicomviewer_3d::slot_computeRealVTA);
    // ==========================================================

    // 2. 连接信号 (ControlPanel -> dicomviewer_3d),把面板上的操作转发给具体的业务逻辑函数
    // 文件与视图操作
    connect(m_controlPanel, &ControlPanel::sigOpenDicom, this, &dicomviewer_3d::slot_openDicomFile);
    connect(m_controlPanel, &ControlPanel::sigImportLabel, this, &dicomviewer_3d::slot_importLabel);
    connect(m_controlPanel, &ControlPanel::sigToggleLabel, this, &dicomviewer_3d::slot_toggleOverlay);
    connect(m_controlPanel, &ControlPanel::sigToggleVolume, this, &dicomviewer_3d::slot_toggleVolume);
    connect(m_controlPanel, &ControlPanel::sigFocusROI, [this]() { this->focusOnROI(nullptr, 40); });
    // 导航跳转
    connect(m_controlPanel, &ControlPanel::sigJumpTarget, this, &dicomviewer_3d::slot_jumpToTarget);
    connect(m_controlPanel, &ControlPanel::sigJumpEntry, this, &dicomviewer_3d::slot_jumpToEntry);
    connect(m_controlPanel, &ControlPanel::sigToggleTrajectoryMode, this, &dicomviewer_3d::slot_toggleTrajectoryMode);
    // 插针
    connect(m_controlPanel, &ControlPanel::sigToggleNeedle, this, &dicomviewer_3d::slot_toggleNeedle);
    // 3D 参数调节
    connect(m_controlPanel, &ControlPanel::sig3DParamsChanged, this, &dicomviewer_3d::slot_update3DParams);
    connect(m_controlPanel, &ControlPanel::sigReset3DParams, this, &dicomviewer_3d::slot_reset3DParams);
    connect(m_controlPanel, &ControlPanel::sigShowPlaneArrows, this, &dicomviewer_3d::slot_showPlaneArrows);
    connect(m_controlPanel, &ControlPanel::sigShowOrientationBox, this, &dicomviewer_3d::slot_showOrientationBox);
    //ROI
    // 1. ROI 开关信号
    connect(m_controlPanel, &ControlPanel::sigEnableROI, [this](bool enable) {
        this->m_isRoiEnabled = enable; // 只更新状态
        this->updateVolumeState();     // 统一执行
        });

    // 2. 实体/玻璃模式切换信号
    connect(m_controlPanel, &ControlPanel::sigSetSolidMode, [this](bool isSolid) {
        this->m_isSolidMode = isSolid; // 只更新状态
        this->updateVolumeState();     // 统一执行
        });

    // 3. 重置按钮 (这个不需要状态变量，直接调)
    connect(m_controlPanel, &ControlPanel::sigResetROI, [this]() {
        if (m_roiManager && m_isRoiEnabled) { // 只有开启时重置才有意义
            m_roiManager->resetROI();
            vtkWidget3D->GetRenderWindow()->Render();
        }
        });

    connect(m_controlPanel, &ControlPanel::sigHideRoiBox, [this](bool hide) {
        this->m_hideRoiBox = hide;
        this->updateVolumeState(); // 重新走统一更新流程
        });

    connect(m_controlPanel, &ControlPanel::sigShowPlane, this, &dicomviewer_3d::slot_show3DPlane);
    // 【新增】连接复位信号
    connect(m_controlPanel, &ControlPanel::sigResetPlanes, this, &dicomviewer_3d::slot_resetPlanes);
    connect(m_controlPanel, &ControlPanel::sigSnapToNeedle, this, &dicomviewer_3d::slot_snapToNeedle);

    // ===== Sprint 3: 深度微调 + 型号切换信号 (由于已移入模拟器，旧代码删除) =====

    // --- AC-PC 坐标系标定信号连接 ---
    connect(m_controlPanel, &ControlPanel::sigStartCalibrateAC, [this]() {
        clearPreviewActors();
        m_acpcPickMode = ACPC_PICK_AC;
        m_controlPanel->setAcPcActiveMode(1);
    });
    connect(m_controlPanel, &ControlPanel::sigStartCalibratePC, [this]() {
        clearPreviewActors();
        m_acpcPickMode = ACPC_PICK_PC;
        m_controlPanel->setAcPcActiveMode(2);
    });
    connect(m_controlPanel, &ControlPanel::sigStartCalibrateMSP, [this]() {
        clearPreviewActors();
        m_acpcPickMode = ACPC_PICK_MSP;
        m_controlPanel->setAcPcActiveMode(3);
    });
    connect(m_controlPanel, &ControlPanel::sigResetAcPc, [this]() {
        clearPreviewActors();
        m_acpcPickMode = ACPC_NONE;
        m_controlPanel->setAcPcActiveMode(0);
        if (m_trajManager->actorACSphere)  ren3d->RemoveActor(m_trajManager->actorACSphere);
        if (m_trajManager->actorPCSphere)  ren3d->RemoveActor(m_trajManager->actorPCSphere);
        if (m_trajManager->actorMSPSphere) ren3d->RemoveActor(m_trajManager->actorMSPSphere);
        m_trajManager->actorACSphere  = nullptr;
        m_trajManager->actorPCSphere  = nullptr;
        m_trajManager->actorMSPSphere = nullptr;
        for (int i = 0; i < 3; i++) {
            if (m_trajManager->acCrossArr[i])  { m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->acCrossArr[i]);  m_trajManager->acCrossArr[i]  = nullptr; }
            if (m_trajManager->pcCrossArr[i])  { m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->pcCrossArr[i]);  m_trajManager->pcCrossArr[i]  = nullptr; }
            if (m_trajManager->mspCrossArr[i]) { m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->mspCrossArr[i]); m_trajManager->mspCrossArr[i] = nullptr; }
            m_views[i].acSliceIndex = m_views[i].pcSliceIndex = m_views[i].mspSliceIndex = -1;
        }
        m_trajManager->resetAcPc();
        // 清除十字线
        setCrosshairVisible(false);
        for (int v = 0; v < 3; v++) {
            vtkRenderer* ren = m_views[v].viewer->GetRenderer();
            if (ren) {
                if (m_views[v].crosshairH) { ren->RemoveActor(m_views[v].crosshairH); m_views[v].crosshairH = nullptr; }
                if (m_views[v].crosshairV) { ren->RemoveActor(m_views[v].crosshairV); m_views[v].crosshairV = nullptr; }
            }
            m_views[v].crosshairHSrc = nullptr;
            m_views[v].crosshairVSrc = nullptr;
        }
        m_crosshairVisible = false;
        m_controlPanel->updateCrosshairDisplay(false, m_crosshairWorld, nullptr);
        m_controlPanel->updateAcPcStatusUI(false, false, false, false);
        m_controlPanel->updateAcPcPointWorld(0, false, nullptr, false);
        m_controlPanel->updateAcPcPointWorld(1, false, nullptr, false);
        m_controlPanel->updateAcPcPointWorld(2, false, nullptr, false);
        m_controlPanel->updateAcPcPointAcpc(0, false, nullptr);
        m_controlPanel->updateAcPcPointAcpc(1, false, nullptr);
        m_controlPanel->updateAcPcPointAcpc(2, false, nullptr);
        refreshCoordinateDisplay();
        refreshAllViews();
    });
    connect(m_controlPanel, &ControlPanel::sigConfirmAcPcInput,
        [this](int idx, double x, double y, double z) {
            AcPcPickMode which = ACPC_NONE;
            if (idx == 0) which = ACPC_PICK_AC;
            else if (idx == 1) which = ACPC_PICK_PC;
            else if (idx == 2) which = ACPC_PICK_MSP;
            if (which == ACPC_NONE) return;
            double pos[3] = { x, y, z };
            commitAcPcPoint(which, pos);
        });

    // --- 十字线相关信号 ---
    connect(m_controlPanel, &ControlPanel::sigCrosshairSetTarget, [this]() {
        if (m_crosshairVisible) {
            handlePick(m_crosshairWorld);
        }
    });
    connect(m_controlPanel, &ControlPanel::sigCrosshairSetEntry, [this]() {
        if (m_crosshairVisible) {
            handlePick(m_crosshairWorld);
        }
    });
    connect(m_controlPanel, &ControlPanel::sigCrosshairLinkToggled, [this](bool linked) {
        m_crosshairLinked = linked;
    });

    // --- 【调试辅助】默认 AC-PC 坐标 ---
    // AC: (185, 250, 142)  PC: (182, 227, 141)  MSP: (184, 238.59, 141.32)
    connect(m_controlPanel, &ControlPanel::sigSetDefaultAcPc, [this]() {
        double ac[3]  = { 185.00, 250.00, 142.00 };
        double pc[3]  = { 182.00, 227.00, 141.00 };
        double msp[3] = { 184.00, 238.59, 141.32 };
        commitAcPcPoint(ACPC_PICK_AC,  ac);
        commitAcPcPoint(ACPC_PICK_PC,  pc);
        commitAcPcPoint(ACPC_PICK_MSP, msp);
        qDebug() << "[Debug] 已填入默认 AC-PC 坐标";
    });

    // --- 【调试辅助】填入 FEM 测试参数 ---
    // target=(167.114, 235.788, 124.18), Contact 2 阴极 -3V, 60μs
    connect(m_controlPanel, &ControlPanel::sigFillTestParams, [this]() {
        if (!m_trajManager) return;

        m_phaseFPresetId = "Manual";
        m_phaseFUseEncapsulation = true;
        m_phaseFSigmaEncapsulation = 0.115;
        m_phaseFEncapsulationThickness = 0.2;

        // 1. 设置靶点 (target)
        double testTarget[3] = { 167.114, 235.788, 124.18 };
        // 进针点：沿 Z 轴向上偏移 80mm 作为默认进针点
        double testEntry[3]  = { 167.114, 235.788, 204.18 };
        m_trajManager->targetPos[0] = testTarget[0];
        m_trajManager->targetPos[1] = testTarget[1];
        m_trajManager->targetPos[2] = testTarget[2];
        m_trajManager->entryPos[0]  = testEntry[0];
        m_trajManager->entryPos[1]  = testEntry[1];
        m_trajManager->entryPos[2]  = testEntry[2];
        m_trajManager->hasTarget = true;
        m_trajManager->hasEntry  = true;

        // 2. 更新电极模型轨迹
        if (m_trajManager->m_dbsLead) {
            m_trajManager->m_dbsLead->UpdateTrajectory(testEntry, testTarget);
            m_trajManager->m_dbsLead->SetVisibility(true);

            // 3. 设置刺激参数: amplitude=3V, pulseWidth=60μs
            m_trajManager->m_dbsLead->setAmplitude(3.0);
            m_trajManager->m_dbsLead->setPulseWidth(60);

            // 4. 设置触点极性: Contact 2 = 阴极, 其余 = OFF
            m_trajManager->m_dbsLead->setContactPolarity(0, POLARITY_OFF);
            m_trajManager->m_dbsLead->setContactPolarity(1, POLARITY_OFF);
            m_trajManager->m_dbsLead->setContactPolarity(2, POLARITY_CATHODE);
            m_trajManager->m_dbsLead->setContactPolarity(3, POLARITY_OFF);

            // 5. 同步到模拟器 UI
            if (m_leadSimulator) {
                const int testPolarities[4] = { 0, 0, -1, 0 };  // contact2=阴极
                m_leadSimulator->setParameters(
                    m_trajManager->m_dbsLead->getLeadType(),
                    m_trajManager->m_dbsLead->getDepthOffset(),
                    3.0,   // amplitude
                    60,    // pulseWidth
                    130,   // frequency
                    false, // showVTA
                    testPolarities
                );
                m_leadSimulator->setPhaseFPresetIndex(0);
            }
        }

        // 6. 刷新坐标显示
        refreshCoordinateDisplay();
        refreshAllViews();
        qDebug() << "[Debug] 已填入 FEM 测试参数: target=("
                 << testTarget[0] << "," << testTarget[1] << "," << testTarget[2]
                 << ") Contact2=阴极 -3V 60μs";
    });

    // 3. 创建 VTK 视图窗口
    // 3D 窗口
    vtkWidget3D = new QVTKOpenGLNativeWidget(this);
    vtkWidget3D->setMinimumSize(200, 200);
    // 2D 视图容器 (带滑条的封装)
    QWidget* containerA = createSliceContainer(0, "Axial (轴状位)", "#FF6666");
    QWidget* containerC = createSliceContainer(1, "Coronal (冠状位)", "#66FF66");
    QWidget* containerS = createSliceContainer(2, "Sagittal (矢状位)", "#FFFF66");

    // ==========================================================
    // 模拟器顶部控制栏
    // ==========================================================
    QWidget* simTopWidget = new QWidget;
    QHBoxLayout* simTopLayout = new QHBoxLayout(simTopWidget);
    simTopLayout->setContentsMargins(4, 4, 4, 4);

    m_btnToggleSimulator = new QPushButton("🚀 开启电极程控模拟器");
    m_btnToggleSimulator->setStyleSheet("QPushButton { background-color: #67C23A; color: white; font-weight: bold; border-radius: 4px; padding: 6px; }");
    
    m_cmbSingleSliceView = new QComboBox;
    m_cmbSingleSliceView->addItem("Axial (轴状位)");
    m_cmbSingleSliceView->addItem("Coronal (冠状位)");
    m_cmbSingleSliceView->addItem("Sagittal (矢状位)");
    m_cmbSingleSliceView->hide(); // 默认隐藏

    simTopLayout->addWidget(m_btnToggleSimulator);
    simTopLayout->addWidget(m_cmbSingleSliceView);

    connect(m_btnToggleSimulator, &QPushButton::clicked, this, &dicomviewer_3d::toggleSimulator);
    connect(m_cmbSingleSliceView, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &dicomviewer_3d::onSingleSliceViewChanged);

    // 4. 布局组装 (Layout)
    // --- 左侧布局 (包含顶部控制条，3个 2D 视图垂直排列，以及模拟器面板) ---
    m_layoutLeft = new QVBoxLayout;
    m_layoutLeft->setSpacing(2);
    m_layoutLeft->setContentsMargins(0, 0, 0, 0);
    m_layoutLeft->addWidget(simTopWidget, 0); // 顶部控制栏
    m_layoutLeft->addWidget(containerA, 1);
    m_layoutLeft->addWidget(containerC, 1);
    m_layoutLeft->addWidget(containerS, 1);
    m_layoutLeft->addWidget(m_leadSimulator, 2); // 模拟器占两份空间

    // --- 右侧布局 (控制面板 + 3D 窗口) ---
    m_layoutRight = new QVBoxLayout;
    m_layoutRight->setSpacing(5);
    m_layoutRight->setContentsMargins(5, 0, 0, 0);
    m_layoutRight->addWidget(m_controlPanel, 3);
    m_layoutRight->addWidget(vtkWidget3D, 7);

    // 步骤 A: 把 layoutLeft 和 layoutRight 分别装进两个容器 Widget 里
    QWidget* leftContainer = new QWidget(this);
    leftContainer->setLayout(m_layoutLeft);
    QWidget* rightContainer = new QWidget(this);
    rightContainer->setLayout(m_layoutRight);
    // 步骤 B: 创建分割器
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(leftContainer);
    splitter->addWidget(rightContainer);
    // 步骤 C: 设置初始比例 (1 : 3)
    // setStretchFactor(索引, 因子)
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    // 步骤 D: 设置分割器样式 (可选，让分割条明显一点)
    splitter->setStyleSheet("QSplitter::handle { background-color: #3a3a3a; }");
    // 步骤 E: 设置中心窗口
    setCentralWidget(splitter);

    resize(1200, 900);
}

//  初始化四个视图：3D + Axial + Coronal + Sagittal 
void dicomviewer_3d::initVtkViews()
{
    // 1. 初始化 3D 窗口
    vtkNew<vtkGenericOpenGLRenderWindow> rw3d;
    rw3d->SetAlphaBitPlanes(1); // 保持透明支持
    rw3d->SetMultiSamples(16);
    vtkWidget3D->SetRenderWindow(rw3d.Get());

    // 2. 配置底层渲染器 (ren3d) -> 画大脑
    ren3d = vtkSmartPointer<vtkRenderer>::New();
    ren3d->SetBackground(0.1, 0.1, 0.1);

    // 保持深度剥离，这对 Volume 自身的渲染质量有好处
    ren3d->SetUseDepthPeeling(1);
    ren3d->SetMaximumNumberOfPeels(100);
    ren3d->SetOcclusionRatio(0.0);

    // ==========================================================
    // 【核心修复】：开启 FXAA (Fast Approximate Anti-Aliasing)
    // 专门对付高光金属边缘和开启深度剥离后的锯齿！
    // ==========================================================
    // 1. 开启 FXAA (快速近似抗锯齿)
    ren3d->SetUseFXAA(true);

    // 2. 告诉渲染窗口尽可能使用平滑渲染
    rw3d->SetLineSmoothing(true);
    rw3d->SetPolygonSmoothing(true);
    // ==========================================================

    // 4. 添加到窗口
    rw3d->AddRenderer(ren3d);

    // ============================================================
    // 【核心新增】：专业双向侧光灯，摧毁“手电筒发光棒”效应
    // （接在 AddRenderer 之后，设置属于 ren3d 的光照）
    // ============================================================
    ren3d->AutomaticLightCreationOff();
    ren3d->RemoveAllLights(); // 关掉默认的头灯

    //// 主光源（左上方，提供金属质感的强烈高光）
    //vtkNew<vtkLight> mainLight;
    //mainLight->SetLightTypeToCameraLight();
    //mainLight->SetPosition(-1.0, 1.0, 1.0);
    //mainLight->SetIntensity(1.2);
    //ren3d->AddLight(mainLight);
    //// 补光源（右后方，微微照亮暗部，提供立体感）
    //vtkNew<vtkLight> backLight;
    //backLight->SetLightTypeToCameraLight();
    //backLight->SetPosition(1.0, -0.5, -0.5);
    //backLight->SetIntensity(0.3); // 较暗
    //backLight->SetDiffuseColor(0.8, 0.8, 1.0); // 微微偏蓝，模拟冷色环境光
    //ren3d->AddLight(backLight);

    // 1. 主光源 (Key Light) - 左上方，偏一点点暖色(阳光感)，负责主要照明和高光
    vtkNew<vtkLight> keyLight;
    keyLight->SetLightTypeToCameraLight();
    keyLight->SetPosition(-1.0, 1.0, 1.0);
    keyLight->SetIntensity(1.1);
    keyLight->SetDiffuseColor(1.0, 0.98, 0.95); // 微暖白
    ren3d->AddLight(keyLight);

    // 2. 补光源 (Fill Light) - 右下方，偏一点点冷色(天光感)，照亮暗部，避免死黑
    vtkNew<vtkLight> fillLight;
    fillLight->SetLightTypeToCameraLight();
    fillLight->SetPosition(1.0, -0.5, 0.0);
    fillLight->SetIntensity(0.5);
    fillLight->SetDiffuseColor(0.85, 0.9, 1.0); // 偏冷蓝
    ren3d->AddLight(fillLight);

    // 3. 边缘光/轮廓光 (Rim Light) - 正后方偏上，极其锐利，负责在电极边缘勾勒出一条亮线！
    vtkNew<vtkLight> rimLight;
    rimLight->SetLightTypeToCameraLight();
    rimLight->SetPosition(0.0, 0.5, -1.5); // 在物体后方
    rimLight->SetIntensity(0.8);
    rimLight->SetDiffuseColor(1.0, 1.0, 1.0); // 纯白
    ren3d->AddLight(rimLight);

    // ============================================================

    //// ============================================================
    //// 【核心修复】添加相机裁剪范围自动修正
    //// ============================================================
    //// 创建一个回调命令
    //vtkNew<vtkCallbackCommand> clipResetCallback;
    //// 定义回调函数：每次渲染前，强制重置 ren3d 的裁剪范围
    //clipResetCallback->SetCallback([](vtkObject*, unsigned long, void* clientData, void*) {
    //    vtkRenderer* ren = static_cast<vtkRenderer*>(clientData);
    //    if (ren) {
    //        // 这句话的意思是：根据 ren (底层脑子) 的包围盒，
    //        // 重新计算相机的 Near/Far Plane。
    //        // 这样能保证脑子永远在两个平面之间，不会被切掉。
    //        ren->ResetCameraClippingRange();
    //    }
    //    });
    //// 把 ren3d 传进去作为 clientData
    //clipResetCallback->SetClientData(ren3d);
    //// 监听 "StartEvent" (渲染开始事件)
    //// 只要画面一刷新，就执行上面的修正逻辑
    //rw3d->AddObserver(vtkCommand::StartEvent, clipResetCallback);
    //// ============================================================

    // 设置交互样式 (保持不变)
    vtkNew<Slicer3DStyle> style3D;
    style3D->Initialize(this);
    rw3d->GetInteractor()->SetInteractorStyle(style3D);

    // --- 创建假图片 (保留) ---
    vtkSmartPointer<vtkImageData> dummyImage = vtkSmartPointer<vtkImageData>::New();
    dummyImage->SetDimensions(1, 1, 1);
    dummyImage->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
    unsigned char* pixel = static_cast<unsigned char*>(dummyImage->GetScalarPointer(0, 0, 0));
    *pixel = 0;

    // 2. 初始化 2D 三视图 (使用新引擎)
    for (int i = 0; i < 3; i++)
    {
        // 【修改】创建新对象
        m_views[i].viewer = vtkSmartPointer<vtkResliceImageViewer>::New();
        // 绑定窗口
        if (i == 0) m_views[i].viewer->SetRenderWindow(m_views[0].widget->GetRenderWindow());
        else if (i == 1) m_views[i].viewer->SetRenderWindow(m_views[1].widget->GetRenderWindow());
        else m_views[i].viewer->SetRenderWindow(m_views[2].widget->GetRenderWindow());
        // 设置背景
        m_views[i].viewer->GetRenderer()->SetBackground(0, 0, 0);

        // ============================================================
        // 【2D视图抗锯齿增强】
        // 3D视图已有 MSAA+FXAA+平滑，但2D视图之前完全没有抗锯齿，
        // 导致电极在2D切面上的轮廓锯齿非常明显。
        // ============================================================
        auto* rw2d = m_views[i].viewer->GetRenderWindow();
        // FXAA：快速近似抗锯齿 (屏幕空间后处理，不依赖OpenGL MSAA)
        m_views[i].viewer->GetRenderer()->SetUseFXAA(true);
        // MSAA：多重采样 (需要 main.cpp 中的 QSurfaceFormat 配合)
        rw2d->SetMultiSamples(8);
        // OpenGL 硬件平滑
        rw2d->SetLineSmoothing(true);
        rw2d->SetPolygonSmoothing(true);
        rw2d->SetPointSmoothing(true);
        // ============================================================

        // 这里的 Input 暂时设为空，后面 setupSliceView 会设
        // m_views[i].viewer->SetInputData(dummyImage); 
        // 【关键】设置模式：暂时设为轴对齐 (Axis Aligned)，模仿旧行为
        // 这样刚启动时，它和以前一样是正的
        m_views[i].viewer->SetResliceModeToAxisAligned();
        // 安装交互器
        m_views[i].viewer->SetupInteractor(m_views[i].viewer->GetRenderWindow()->GetInteractor());

        // 【核心修复】：关闭 Viewer 自带的滚轮切片拦截，把滚轮控制权还给你的 MyViewStyle！
        m_views[i].viewer->SliceScrollOnMouseWheelOff();

        // 使用自定义交互样式 (MyViewStyle)，处理滚轮同步
        vtkNew<MyViewStyle> style;
        style->Initialize(this, i, m_views[i].viewer); // 正确传递 viewer 指针
        m_views[i].viewer->GetRenderWindow()->GetInteractor()->SetInteractorStyle(style);
        m_views[i].style = style; // 保存 style 指针
    }

}

void dicomviewer_3d::slot_openDicomFile()
{
    // --- 1. 检查是否已有数据 (覆盖提示) ---
    if (this->vtkImage) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "确认",
            "当前已加载图像。是否要覆盖现有图像？\n(这将清除所有当前的分割和设置)",
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            return; // 用户点否，取消操作
        }
    }

    // --- 2. 打开文件对话框 (恢复原生外观) ---
    // 【关键修改】去掉了最后一个参数 QFileDialog::DontUseNativeDialog
    QString fileOrFolderPath = QFileDialog::getOpenFileName(
        this,
        "选择 NIfTI 文件 或 DICOM 文件夹中的任意文件",
        QString(),
        "NIfTI (*.nii *.nii.gz);;DICOM (*.dcm);;所有文件 (*)"
    );

    if (fileOrFolderPath.isEmpty()) return;

    // --- 3. 加载逻辑 ---
    if (fileOrFolderPath.endsWith(".nii") || fileOrFolderPath.endsWith(".nii.gz")) {
        // loadNiftiVTK 内部现在有 DeepCopy，非常安全，可以直接覆盖
        if (loadNiftiVTK(fileOrFolderPath)) {

            // 如果之前有标签，加载新底图后，标签可能尺寸对不上了
            // 建议这里顺手把标签也清空，防止显示错位
            if (vtkLabelImage) {
                vtkLabelImage = nullptr;
                for (int i = 0; i < 3; i++) {
                    if (m_views[i].overlayActor) {
                        m_views[i].viewer->GetRenderer()->RemoveActor(m_views[i].overlayActor);
                        m_views[i].overlayActor = nullptr; // 清空智能指针
                        m_views[i].viewer->Render(); // 刷新视图
                    }
                }
            }
            renderVTK_nii(); // 重新渲染
        }
        else {
            QMessageBox::warning(this, "错误", "加载 NIfTI 失败");
        }
        // 【新增】强制激活主窗口，防止缩回任务栏
        this->activateWindow();
        return;
    }
    // 2. 假设用户选择的是一个 DICOM 文件，我们需要它的父目录
    QFileInfo fileInfo(fileOrFolderPath);
    QString dicomFolder = fileInfo.dir().path();

    // 检查这个目录是否存在，并且其中包含 .dcm 文件
    if (fileInfo.exists() && QDir(dicomFolder).entryList(QStringList() << "*.dcm" << "*.DCM").size() > 0)
    {
        if (loadDicomVTK(dicomFolder)) { // 加载父目录
            renderVTK();
        }
        else {
            QMessageBox::warning(this, "错误", "加载 DICOM 失败");
        }
        // 【新增】强制激活主窗口，防止缩回任务栏
        this->activateWindow();
        return;
    }

    // 3. 如果以上都不是，则提示错误
    QMessageBox::warning(this, "错误", "请选择一个有效的 NIfTI 文件或 DICOM 文件夹中的文件。");
}

bool dicomviewer_3d::loadDicomVTK(const QString& folder)
{
    vtkSmartPointer<vtkDICOMImageReader> reader =
        vtkSmartPointer<vtkDICOMImageReader>::New();
    reader->SetDirectoryName(folder.toStdString().c_str());

    try {
        reader->Update();
    }
    catch (...) {
        qDebug() << "VTK 读取 DICOM 失败";
        return false;
    }

    vtkImage = reader->GetOutput();
    return true;
}

bool dicomviewer_3d::loadNiftiVTK(const QString& file)
{
    // 1. 定义局部 Reader
    vtkSmartPointer<vtkNIFTIImageReader> reader = vtkSmartPointer<vtkNIFTIImageReader>::New();
    qDebug() << "Loading NIfTI:" << file;
    reader->SetFileName(file.toStdString().c_str());

    try {
        reader->Update();
    }
    catch (...) {
        qDebug() << "VTK 读取 NIfTI 失败";
        return false;
    }

    // 2. 定义临时指针
    vtkSmartPointer<vtkImageData> tempImage = nullptr;

    // 3. 处理方向矩阵
    vtkSmartPointer<vtkMatrix4x4> matrix = reader->GetSFormMatrix();
    if (!matrix) matrix = reader->GetQFormMatrix();

    if (matrix)
    {
        vtkNew<vtkImageReslice> reslice;
        reslice->SetInputConnection(reader->GetOutputPort());
        reslice->SetResliceAxes(matrix);
        reslice->SetOutputDimensionality(3);
        reslice->SetInterpolationModeToLinear();
        reslice->AutoCropOutputOn();
        reslice->Update();
        tempImage = reslice->GetOutput();
    }
    else
    {
        tempImage = reader->GetOutput();
    }

    if (!tempImage) return false;

    // ============================================================
    // 【修复核心】：使用 DeepCopy 切断与局部变量的联系
    // ============================================================

    // 创建全新的对象
    this->vtkImage = vtkSmartPointer<vtkImageData>::New();

    // 深拷贝数据：把像素值复制到新内存里
    this->vtkImage->DeepCopy(tempImage);

    // 手动复制元信息 (Origin, Spacing 等)
    this->vtkImage->SetOrigin(tempImage->GetOrigin());
    this->vtkImage->SetSpacing(tempImage->GetSpacing());
    this->vtkImage->SetDimensions(tempImage->GetDimensions());
    this->vtkImage->SetExtent(tempImage->GetExtent());

    return true;
}

bool dicomviewer_3d::loadLabelVTK(const QString& file)
{
    vtkSmartPointer<vtkNIFTIImageReader> reader = vtkSmartPointer<vtkNIFTIImageReader>::New();
    reader->SetFileName(file.toStdString().c_str());

    try {
        reader->Update();
    }
    catch (...) {
        qDebug() << "标签文件读取失败: " << file;
        return false;
    }

    // --- 关键修正：对齐坐标系 ---
    // 如果主图像进行了 RAS 校正，标签也必须进行相同的操作
    // 或者更简单的方法：使用 vtkImageReslice 将标签重采样到主图像的几何空间

    if (!this->vtkImage) {
        this->vtkLabelImage = reader->GetOutput();
        return true;
    }

    qDebug() << "正在将标签对齐到主图像几何空间...";

    vtkNew<vtkImageReslice> aligner;
    aligner->SetInputConnection(reader->GetOutputPort());

    // 关键：如果原始NIfTI读取时用了SForm，这里标签也要考虑。
    // 但更稳健的做法是：强制将标签重采样到 vtkImage 的 grid 上
    // 即使标签和原图的方向矩阵不同，这也能让它们重合。

    // 1. 设置标签自身的变换 (如果标签也有方向矩阵)
    vtkSmartPointer<vtkMatrix4x4> labelMatrix = reader->GetSFormMatrix();
    if (!labelMatrix) labelMatrix = reader->GetQFormMatrix();
    if (labelMatrix) {
        aligner->SetResliceAxes(labelMatrix);
    }

    // 2. 告诉 Reslice 输出必须和 vtkImage 一模一样
    aligner->SetOutputOrigin(this->vtkImage->GetOrigin());
    aligner->SetOutputSpacing(this->vtkImage->GetSpacing());
    aligner->SetOutputExtent(this->vtkImage->GetExtent()); // 强制尺寸一致
    aligner->SetOutputDimensionality(3);

    // 3. 标签必须用最近邻插值，防止出现小数类别
    aligner->SetInterpolationModeToNearestNeighbor();

    // 4. 背景填充为0
    aligner->SetBackgroundLevel(0);

    aligner->Update();
    this->vtkLabelImage = aligner->GetOutput();

    return true;
}

void dicomviewer_3d::renderVTK()
{
    // 1. 智能计算窗宽窗位 (保留你的逻辑)
    double range[2];
    vtkImage->GetScalarRange(range);
    double minVal = range[0];
    double maxVal = range[1];
    double fullWidth = range[1] - range[0];
    double finalWindow = (fullWidth > 2000) ? fullWidth * 0.15 : fullWidth;
    double finalLevel = range[0] + (finalWindow / 2.0);
    if (finalWindow < 1.0) finalWindow = 1.0;
    m_smartWindow = finalWindow;
    m_smartLevel = finalLevel;

    if (!vtkImage) return;

    qDebug() << "NIfTI 数据范围 (RAS校正后): [" << minVal << ", " << maxVal << "]";

    // 2. 设置 2D 视图
    int orientations[3] = {
        vtkImageViewer2::SLICE_ORIENTATION_XY, // Axial
        vtkImageViewer2::SLICE_ORIENTATION_XZ, // Coronal
        vtkImageViewer2::SLICE_ORIENTATION_YZ  // Sagittal
    };

    for (int i = 0; i < 3; i++) {
        m_views[i].viewer->SetInputData(vtkImage);
        m_views[i].viewer->SetSliceOrientation(orientations[i]);

        m_views[i].viewer->SetColorWindow(m_smartWindow);
        m_views[i].viewer->SetColorLevel(m_smartLevel);

        // 【修改】传入 range[0] 和 range[1]
        MyViewStyle* style = MyViewStyle::SafeDownCast(
            m_views[i].viewer->GetRenderWindow()->GetInteractor()->GetInteractorStyle());
        if (style) {
            // 传入：当前窗宽，当前窗位，数据最小值，数据最大值
            style->UpdateResetValues(m_smartWindow, m_smartLevel, range[0], range[1]);
        }

        m_views[i].viewer->GetRenderer()->ResetCamera();
        updateRulerLayout(i);
        m_views[i].viewer->Render();
    }

    // ------------------------------
    // 构建 3D Volume Rendering
    // ------------------------------
    ren3d->RemoveAllViewProps();

    vtkSmartPointer<vtkSmartVolumeMapper> mapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
    mapper->SetInputData(vtkImage);

    vtkSmartPointer<vtkVolumeProperty> prop = vtkSmartPointer<vtkVolumeProperty>::New();
    prop->ShadeOn();
    prop->SetInterpolationTypeToLinear();

    vtkSmartPointer<vtkPiecewiseFunction> opacity = vtkSmartPointer<vtkPiecewiseFunction>::New();
    opacity->AddPoint(-1000, 0.0);
    opacity->AddPoint(0, 0.0);
    opacity->AddPoint(200, 0.1);
    opacity->AddPoint(1000, 0.8);

    vtkSmartPointer<vtkColorTransferFunction> color = vtkSmartPointer<vtkColorTransferFunction>::New();
    color->AddRGBPoint(-1000, 0.0, 0.0, 0.0);
    color->AddRGBPoint(0, 0.8, 0.5, 0.4);
    color->AddRGBPoint(1000, 0.9, 0.9, 0.9);

    prop->SetScalarOpacity(opacity);
    prop->SetColor(color);

    vtkSmartPointer<vtkVolume> volume = vtkSmartPointer<vtkVolume>::New();
    volume->SetMapper(mapper);
    volume->SetProperty(prop);

    ren3d->AddVolume(volume);
    ren3d->ResetCamera();
    vtkWidget3D->GetRenderWindow()->Render();

    qDebug() << "三视图 + 3D 体渲染完成";
}

void dicomviewer_3d::renderVTK_nii()
{
    if (!vtkImage) {
        qDebug() << "Error: vtkImage is null!";
        return;
    }
    m_isRoiEnabled = false;
    m_isSolidMode = false;
    // 1. 清理两个层
    ren3d->RemoveAllViewProps();      // 清理大脑
    m_labelActors.clear();


    // 【重构替换】：用 TrajectoryManager 来统一清理针的残留
    vtkResliceImageViewer* viewers_arr[3] = { m_views[0].viewer, m_views[1].viewer, m_views[2].viewer };
    m_trajManager->clearNeedleActors(ren3d, viewers_arr);
    m_trajManager->hasTarget = false;
    m_trajManager->hasEntry = false;

    // 1b. 【重要】清理 2D 视图上挂的所有附加 actor (ruler/orient/triangle/crosshair/preview)
    //     防止重新导入图像时旧的 actor 残留导致重影
    for (int i = 0; i < 3; i++) {
        cleanupSliceViewAddons(i);
    }
    // 1c. 复位 AC-PC/MSP/十字线状态
    resetAllAcPcAndCrosshair();

    // 2. 智能计算窗宽窗位 
    double range[2];
    vtkImage->GetScalarRange(range);
    double minVal = range[0];
    double maxVal = range[1];
    double fullWidth = range[1] - range[0];
    double finalWindow = (fullWidth > 2000) ? fullWidth * 0.15 : fullWidth;
    double finalLevel = range[0] + (finalWindow / 2.0);
    if (finalWindow < 1.0) finalWindow = 1.0;
    m_smartWindow = finalWindow;
    m_smartLevel = finalLevel;

    if (!vtkImage) return;

    qDebug() << "NIfTI 数据范围 (RAS校正后): [" << minVal << ", " << maxVal << "]";

    // 3. 【核心修改】循环调用封装好的 setupSliceView
    // 这才是正确的框架，把 dirty work 交给子函数
    int orientations[3] = {
        vtkResliceImageViewer::SLICE_ORIENTATION_XY, // Axial (Z轴)
        vtkResliceImageViewer::SLICE_ORIENTATION_XZ, // Coronal (Y轴)
        vtkResliceImageViewer::SLICE_ORIENTATION_YZ  // Sagittal (X轴)
    };

    for (int i = 0; i < 3; i++) {
        // 这里调用了 setupSliceView，所有的共享光标、设置方向都在里面完成
        setupSliceView(i, orientations[i], minVal, maxVal);
    }

    // ============================================================
    // 4. 构建 3D 体渲染 (针对 DBS 手术规划的“玻璃脑”调优)
    // ============================================================

    vtkSmartPointer<vtkSmartVolumeMapper> mapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
    //vtkSmartPointer<vtkGPUVolumeRayCastMapper> mapper = vtkSmartPointer<vtkGPUVolumeRayCastMapper>::New();
    mapper->SetInputData(vtkImage);
    // 降低采样距离可以提高画质，但会增加显卡负担。0.5 是个平衡点。
    // 如果觉得卡，可以改成 1.0
    mapper->SetSampleDistance(0.5);

    //mapper->SetIntermixIntersectingGeometry(1);
    //mapper->SetAutoAdjustSampleDistances(1);

    vtkSmartPointer<vtkVolumeProperty> prop = vtkSmartPointer<vtkVolumeProperty>::New();
    applySolidStyle(prop);

    // 创建 Volume
    m_currentVolume = vtkSmartPointer<vtkVolume>::New(); // 赋值给成员变量
    m_currentVolume->SetMapper(mapper);
    m_currentVolume->SetProperty(prop);
    ren3d->AddVolume(m_currentVolume);

    // ============================================================
    // 【新增】添加数据边界线框 (Outline)
    // ============================================================

    // 1. 清理旧的 (如果有)
    if (m_outlineActor) {
        ren3d->RemoveActor(m_outlineActor);
        m_outlineActor = nullptr;
    }

    // 2. 创建线框滤镜
    vtkNew<vtkOutlineFilter> outlineFilter;
    outlineFilter->SetInputData(vtkImage); // 获取 vtkImage 的 Bounds
    outlineFilter->Update();

    // 3. 创建映射器和 Actor
    vtkNew<vtkPolyDataMapper> outlineMapper;
    outlineMapper->SetInputConnection(outlineFilter->GetOutputPort());

    m_outlineActor = vtkSmartPointer<vtkActor>::New();
    m_outlineActor->SetMapper(outlineMapper);

    // 4. 设置外观 (模仿 Slicer 的紫色框，或者你喜欢的颜色)
    // 颜色: R=0.5, G=0.5, B=1.0 (淡紫/蓝紫色)
    m_outlineActor->GetProperty()->SetColor(0.5, 0.5, 1.0);
    m_outlineActor->GetProperty()->SetLineWidth(2.0); // 线宽

    // 设为不可拾取 (防止鼠标点到它)
    m_outlineActor->PickableOff();

    // 5. 添加到场景
    ren3d->AddActor(m_outlineActor);

    m_outlineActor->SetVisibility(m_showOrientationBox ? 1 : 0);
    updateOrientationMarkers();

    // ============================================================

    // 【新增】初始化 3D 空间中的切片板 (确保不会被清场)
    initPlaneWidgets();

    // 【新增】初始化 ROI 管理器
    m_roiManager->initialize(vtkWidget3D->GetRenderWindow()->GetInteractor(), mapper);
    //m_roiManager->setCapWindowLevel(m_smartWindow, m_smartLevel);
    // 默认先不显示框，等用户点按钮再开
    m_roiManager->enableROI(false);

    // 每次重新渲染时，确保交互样式是 Slicer 风格，防止被重置为默认
    vtkNew<Slicer3DStyle> style3D;
    style3D->Initialize(this); // <--- 关键：这里也要传入 this
    vtkWidget3D->GetRenderWindow()->GetInteractor()->SetInteractorStyle(style3D);

    // 相机设置
    ren3d->GetActiveCamera()->SetFocalPoint(vtkImage->GetCenter());
    ren3d->ResetCamera();
    ren3d->GetActiveCamera()->Zoom(1.2); // 稍微拉近一点

    vtkWidget3D->GetRenderWindow()->Render();

    m_roiManager->enableROI(false);
}

void dicomviewer_3d::initLookupTable()
{
    labelLUT = vtkSmartPointer<vtkLookupTable>::New();
    // 设置一个足够大的范围，比如 0-20，防止后面需要动态调整
    labelLUT->SetNumberOfTableValues(20);
    labelLUT->SetRange(0.0, 19.0);
    labelLUT->Build(); // 这里调用 Build 是安全的，因为还没设颜色

    // 0号背景全透明
    labelLUT->SetTableValue(0, 0.0, 0.0, 0.0, 0.0);

    // 设置核团颜色 (RGBA)
    labelLUT->SetTableValue(1, 1.0, 0.0, 0.0, 1.0); // 1号: 纯红
    labelLUT->SetTableValue(2, 0.0, 0.0, 1.0, 1.0); // 2号: 纯蓝
    labelLUT->SetTableValue(3, 0.0, 1.0, 0.0, 1.0); // 3号: 纯绿
    labelLUT->SetTableValue(4, 1.0, 1.0, 0.0, 1.0); // 4号: 黄色
    labelLUT->SetTableValue(5, 0.0, 1.0, 1.0, 1.0); // 5号: 青色
    labelLUT->SetTableValue(6, 1.0, 0.0, 1.0, 1.0); // 6号: 品红
}

void dicomviewer_3d::slot_importLabel()
{
    // 0. 前置检查：必须先有底图才能导标签
    if (!vtkImage) {
        QMessageBox::warning(this, "提示", "请先导入原始 MRI/CT 图像，再导入标签。");
        return;
    }

    // --- 1. 检查是否已有标签 (覆盖提示) ---
    if (this->vtkLabelImage) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "确认",
            "当前已加载标签。是否要覆盖现有标签？",
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            return;
        }
    }

    // --- 2. 打开文件对话框 (恢复原生外观) ---
    QString file = QFileDialog::getOpenFileName(
        this,
        "导入标签 (NIfTI)",
        QString(),
        "NIfTI (*.nii *.nii.gz)"
    );

    if (file.isEmpty()) return;

    // --- 3. 加载标签 ---
    // loadLabelVTK 内部会做重采样对齐，确保和底图匹配
    if (!loadLabelVTK(file)) {
        QMessageBox::warning(this, "错误", "无法加载标签文件！");
        return;
    }

    // --- 4. 渲染标签 (创建双 Actor) ---

    // 准备颜色映射
    if (!labelLUT) initLookupTable();
    auto mapToColors = vtkSmartPointer<vtkImageMapToColors>::New();
    mapToColors->SetInputData(vtkLabelImage);
    mapToColors->SetLookupTable(labelLUT);
    mapToColors->SetOutputFormatToRGBA();
    mapToColors->Update();

    for (int i = 0; i < 3; i++) {
        // 清理旧的 Actor (如果有)
        if (m_views[i].overlayActor) {
            m_views[i].viewer->GetRenderer()->RemoveActor(m_views[i].overlayActor);
        }

        // 创建新的 Actor
        m_views[i].overlayActor = vtkSmartPointer<vtkImageActor>::New();
        m_views[i].overlayActor->GetMapper()->SetInputConnection(mapToColors->GetOutputPort());

        // 设置属性
        m_views[i].overlayActor->SetOpacity(0.6);
        m_views[i].overlayActor->PickableOff();

        // 添加到渲染器
        m_views[i].viewer->GetRenderer()->AddActor(m_views[i].overlayActor);

        // 同步切片位置
        updateOverlaySlice(i);

        m_views[i].viewer->Render();
    }

    // --- 5. 更新 3D 视图的核团 (面绘制) ---
    update3DLabelMesh();

    // --- 6. 切换原图为玻璃脑模式 (体绘制) ---
    // 【核心修改】直接使用 m_currentVolume，不再去 Renderer 里遍历查找
    if (m_currentVolume) {
        vtkVolumeProperty* prop = m_currentVolume->GetProperty();

        // 获取滑块当前的值
        double shell = 0.8;
        double internal = 0.2;
        double edge = 0.1;

        // 应用玻璃脑样式
        applyGlassStyle(prop, shell, internal, edge);

        vtkWidget3D->GetRenderWindow()->Render();
    }
}

void dicomviewer_3d::slot_toggleOverlay()
{
    for (int i = 0; i < 3; i++) {
        if (m_views[i].overlayActor) {
            bool isVisible = m_views[i].overlayActor->GetVisibility();
            m_views[i].overlayActor->SetVisibility(!isVisible);
            m_views[i].viewer->Render();
        }
    }
    // 2. 【新增】控制 3D 核团
    bool labelVisible = true;
    // 取第一个核团的状态作为参考
    if (!m_labelActors.empty()) {
        labelVisible = !m_labelActors[0]->GetVisibility();
    }

    for (auto actor : m_labelActors) {
        actor->SetVisibility(labelVisible);
    }
    vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::slot_jumpToTarget() {
    if (m_trajManager->hasTarget) jumpToPosition(m_trajManager->targetPos);
    else QMessageBox::information(this, "提示", "尚未设定靶点，请先在 2D 视图中使用 Ctrl+左键 选点。");
}

void dicomviewer_3d::slot_jumpToEntry() {
    if (m_trajManager->hasEntry) jumpToPosition(m_trajManager->entryPos);
    else QMessageBox::information(this, "提示", "尚未设定进针点。");
}

void dicomviewer_3d::slot_toggleNeedle()
{
    if (!m_trajManager->hasEntry || !m_trajManager->hasTarget) {
        QMessageBox::warning(this, "提示", "请先在 2D 视图中选定【靶点】和【进针点】。");
        return;
    }

    vtkResliceImageViewer* viewers[3] = { m_views[0].viewer, m_views[1].viewer, m_views[2].viewer };

    bool isVisible = false;
    if (m_trajManager->m_dbsLead && m_trajManager->m_dbsLead->GetAssembly3D()) {
        isVisible = m_trajManager->m_dbsLead->GetAssembly3D()->GetVisibility();
    }

    if (isVisible) {
        // --- 状态：显示 -> 隐藏 ---
        m_trajManager->clearNeedleActors(ren3d, viewers);
        m_controlPanel->btnToggleNeedle->setText("显示手术针");

        // 【新增】：针隐藏时，重新显示红绿定位球
        if (m_trajManager->actorTargetSphere) m_trajManager->actorTargetSphere->SetVisibility(true);
        if (m_trajManager->actorEntrySphere) m_trajManager->actorEntrySphere->SetVisibility(true);
    }
    else {
        // --- 状态：隐藏 -> 重新显示 ---
        m_trajManager->updateNeedleActor(ren3d, viewers);
        m_controlPanel->btnToggleNeedle->setText("隐藏手术针");

        // 【新增】：针生成后，立刻隐藏红绿定位球，以免遮挡针尖和杆身！
        if (m_trajManager->actorTargetSphere) m_trajManager->actorTargetSphere->SetVisibility(false);
        if (m_trajManager->actorEntrySphere) m_trajManager->actorEntrySphere->SetVisibility(false);
    }

    vtkWidget3D->GetRenderWindow()->Render();
    refreshAllViews();
}

void dicomviewer_3d::slot_toggleVolume()
{
    if (m_currentVolume) {
        // 获取当前状态并取反
        bool isVisible = m_currentVolume->GetVisibility();
        m_currentVolume->SetVisibility(!isVisible);

        // 刷新 3D 窗口
        vtkWidget3D->GetRenderWindow()->Render();
    }
}

void dicomviewer_3d::updateOverlaySlice(int i)
{
    if (!m_views[i].viewer || !m_views[i].viewer->GetInput()) return;

    int currentSlice = m_views[i].viewer->GetSlice();
    int mode = m_views[i].viewer->GetResliceMode();

    // 1. 同步标签 Overlay
    if (m_views[i].overlayActor) {
        int extent[6];
        m_views[i].overlayActor->GetMapper()->GetInput()->GetExtent(extent);
        int orientation = m_views[i].viewer->GetSliceOrientation();
        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) { extent[4] = currentSlice; extent[5] = currentSlice; }
        else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) { extent[2] = currentSlice; extent[3] = currentSlice; }
        else { extent[0] = currentSlice; extent[1] = currentSlice; }
        m_views[i].overlayActor->SetDisplayExtent(extent);
    }

    // 2. 轴对齐模式下的十字准星显隐
    if (m_trajManager->targetCrossArr[i]) {
        bool show = (m_trajManager->hasTarget && currentSlice == m_views[i].targetSliceIndex);
        m_trajManager->targetCrossArr[i]->SetVisibility(show);
    }
    if (m_trajManager->entryCrossArr[i]) {
        bool show = (m_trajManager->hasEntry && currentSlice == m_views[i].entrySliceIndex);
        m_trajManager->entryCrossArr[i]->SetVisibility(show);
    }
    if (m_trajManager->acCrossArr[i]) {
        bool show = (m_trajManager->hasAC && currentSlice == m_views[i].acSliceIndex);
        m_trajManager->acCrossArr[i]->SetVisibility(show);
    }
    if (m_trajManager->pcCrossArr[i]) {
        bool show = (m_trajManager->hasPC && currentSlice == m_views[i].pcSliceIndex);
        m_trajManager->pcCrossArr[i]->SetVisibility(show);
    }
    if (m_trajManager->mspCrossArr[i]) {
        bool show = (m_trajManager->hasMSP && currentSlice == m_views[i].mspSliceIndex);
        m_trajManager->mspCrossArr[i]->SetVisibility(show);
    }

    // ============================================================
    // 【核心同步】：2D 滚轮驱动 3D 平面 (仅限轴对齐)
    // ============================================================
    if (mode == vtkResliceImageViewer::RESLICE_AXIS_ALIGNED) {
        if (m_slicePlanes[i] && vtkImage) {
            double origin[3], spacing[3];
            vtkImage->GetOrigin(origin);
            vtkImage->GetSpacing(spacing);
            int orientation = m_views[i].viewer->GetSliceOrientation();

            double currentCenter[3];
            vtkCamera* cam = m_views[i].viewer->GetRenderer()->GetActiveCamera();
            if (cam) cam->GetFocalPoint(currentCenter);
            else vtkImage->GetCenter(currentCenter);

            double newCenter[3] = { currentCenter[0], currentCenter[1], currentCenter[2] };

            // 从滚轮的切片层数，反算出 3D 绝对高度
            if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) newCenter[2] = origin[2] + currentSlice * spacing[2];
            else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) newCenter[1] = origin[1] + currentSlice * spacing[1];
            else newCenter[0] = origin[0] + currentSlice * spacing[0];

            vtkResliceCursor* cursor = m_views[i].viewer->GetResliceCursor();
            if (cursor) cursor->SetCenter(newCenter);

            // 【关键同步】：把反算出来的高度，喂给 3D 数学平面和彩色交互框！
            m_slicePlanes[i]->SetOrigin(newCenter);
            if (m_impWidgets[i]) {
                vtkImplicitPlaneRepresentation* rep = vtkImplicitPlaneRepresentation::SafeDownCast(m_impWidgets[i]->GetRepresentation());
                if (rep) {
                    rep->SetOrigin(newCenter);
                    rep->SetPlane(m_slicePlanes[i]);
                    rep->Modified();
                }
            }

            if (cam) {
                double viewDir[3]; cam->GetDirectionOfProjection(viewDir);
                double distance = cam->GetDistance();
                if (distance <= 0.0) distance = 100.0;
                double newPos[3] = { newCenter[0] - viewDir[0] * distance, newCenter[1] - viewDir[1] * distance, newCenter[2] - viewDir[2] * distance };
                cam->SetFocalPoint(newCenter);
                cam->SetPosition(newPos);
            }
        }
    }

    // ============================================================
        // 【物理截断手术针】 (所有模式通用) -> 现在兼容高保真电极
        // ============================================================
    if (m_slicePlanes[i] && m_trajManager && m_trajManager->m_dbsLead) {
        double origin[3];
        m_slicePlanes[i]->GetOrigin(origin);

        vtkCamera* cam = m_views[i].viewer->GetRenderer()->GetActiveCamera();
        if (cam) {
            double camDir[3];
            cam->GetDirectionOfProjection(camDir);

            // 正确的法线切割方向 (这段不动，保留你的数学心血！)
            double awayFromCamera[3] = { -camDir[0], -camDir[1], -camDir[2] };
            double towardsCamera[3] = { camDir[0], camDir[1], camDir[2] };

            vtkNew<vtkPlane> planeAbove;
            planeAbove->SetOrigin(origin);
            planeAbove->SetNormal(awayFromCamera);

            vtkNew<vtkPlane> planeBelow;
            planeBelow->SetOrigin(origin);
            planeBelow->SetNormal(towardsCamera);

            // 【关键调用】：一键切断当前视图的 1绝缘 + 4触点 的上下半截
            m_trajManager->m_dbsLead->ApplyClippingPlanes(i, planeAbove, planeBelow);
        }
    }

    // 确保脑部图像 100% 不透明
    if (m_views[i].viewer && m_views[i].viewer->GetImageActor()) {
        m_views[i].viewer->GetImageActor()->GetProperty()->SetOpacity(1.0);
    }

    m_views[i].viewer->GetRenderer()->ResetCameraClippingRange();
    m_views[i].viewer->Render();

    if (vtkWidget3D->isVisible()) {
        vtkWidget3D->GetRenderWindow()->Render();
    }
}

void dicomviewer_3d::updateSliderUI(int viewIndex, int slice)
{
    // 安全检查
    if (viewIndex < 0 || viewIndex > 2) return;
    QSlider* slider = m_views[viewIndex].slider;
    if (!slider) return;

    // 【关键技巧】阻断信号
    // 我们只是想更新滑条的视觉位置，不想让滑条再次发送 valueChanged 信号去调用 SetSlice
    // 否则会形成：滚轮 -> SetSlice -> updateSlider -> setValue -> valueChanged -> SetSlice 的死循环
    slider->blockSignals(true);
    slider->setValue(slice);

    // 【新增】把当前值存入属性，为滑动条拖动做参照
    slider->setProperty("last_val", slice);

    slider->blockSignals(false);
}

void dicomviewer_3d::update3DLabelMesh()
{
    // 1. 清理
    for (auto actor : m_labelActors) {
        // 【修改】从 overlay 层移除
        ren3d->RemoveActor(actor);
    }
    m_labelActors.clear();

    if (!vtkLabelImage) return;

    // 2. 定义颜色 (参考你的 txt 文件)
    struct ColorRGB { double r, g, b; };
    std::vector<ColorRGB> labelColors = {
        {0.98, 0.5, 0.45}, {0.6, 0.98, 0.6}, {0.53, 0.81, 0.98},
        {0.94, 0.9, 0.55}, {0.5, 1.0, 0.83}, {0.87, 0.63, 0.87}
    };

    // 3. 遍历标签值 (标签是 1~6)
    for (int i = 1; i <= 6; ++i)
    {
        // --- A. 面绘制算法 (Marching Cubes) ---
        vtkNew<vtkDiscreteMarchingCubes> extractor;
        extractor->SetInputData(vtkLabelImage);
        extractor->GenerateValues(1, i, i); // 提取第 i 个标签
        extractor->Update();

        // 如果这个标签不存在，跳过
        if (extractor->GetOutput()->GetNumberOfPolys() == 0) continue;

        // --- B. 平滑处理 (让模型不那么方) ---
        vtkNew<vtkWindowedSincPolyDataFilter> smoother;
        smoother->SetInputConnection(extractor->GetOutputPort());
        smoother->SetNumberOfIterations(15);
        smoother->SetPassBand(0.01);
        smoother->BoundarySmoothingOff();
        smoother->FeatureEdgeSmoothingOff();
        smoother->NonManifoldSmoothingOn();
        smoother->NormalizeCoordinatesOn();
        smoother->Update();

        // --- C. 计算法线 (让光照更自然) ---
        vtkNew<vtkPolyDataNormals> normals;
        normals->SetInputConnection(smoother->GetOutputPort());
        normals->ComputePointNormalsOn();
        normals->AutoOrientNormalsOn();

        // --- D. 映射与渲染 ---
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(normals->GetOutputPort());
        mapper->ScalarVisibilityOff(); // 关掉标量颜色，使用 Property 颜色

        vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);

        // 设置颜色
        int colorIndex = (i - 1) % labelColors.size();
        actor->GetProperty()->SetColor(labelColors[colorIndex].r, labelColors[colorIndex].g, labelColors[colorIndex].b);

        actor->GetProperty()->SetOpacity(0.58);
        actor->GetProperty()->SetAmbient(0.32);
        actor->GetProperty()->SetDiffuse(0.68);
        actor->GetProperty()->SetSpecular(0.22);
        actor->GetProperty()->SetSpecularPower(18);

        // 添加到渲染器和列表
        ren3d->AddActor(actor);
        m_labelActors.push_back(actor);
    }

    // 刷新显示
    vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::setupSliceView(int i, int orientation, double minVal, double maxVal)
{
    if (i < 0 || i >= 3) return;
    if (!vtkImage) return;

    vtkResliceImageViewer* viewer = m_views[i].viewer;

    // 1. 设置输入
    viewer->SetInputData(vtkImage);

    // 2. 【核心】直接设置方向
    // 因为不再共享光标，每个 Viewer 都有自己独立的光标
    // SetSliceOrientation 会自动帮我们配置好独立的相机和法向量
    viewer->SetSliceOrientation(orientation);

    // 3. 设为默认模式 (AxisAligned)
    // 初始化时用 AxisAligned 最安全，绝对不会黑屏，也不会有平行报错
    viewer->SetResliceModeToAxisAligned();

    // ============================================================
    // 【最终修正版】消除红绿线的正确逻辑
    // ============================================================
    vtkResliceCursorWidget* w = m_views[i].viewer->GetResliceCursorWidget();
    if (w) {
        // 1. 禁用交互
        w->SetEnabled(0);
        w->ProcessEventsOff();

        // 2. 获取表现层 (Representation)
        vtkResliceCursorRepresentation* rep =
            vtkResliceCursorRepresentation::SafeDownCast(w->GetRepresentation());
        // 3. 强转为 LineRepresentation
        vtkResliceCursorLineRepresentation* lineRep =
            vtkResliceCursorLineRepresentation::SafeDownCast(rep);

        if (lineRep) {
            // 4. 【关键一步】获取 Actor
            vtkResliceCursorActor* actor = lineRep->GetResliceCursorActor();
            if (actor) {
                // 5. 终极隐藏方案：直接让 Actor 不可见
                actor->SetVisibility(0);
                // 如果你不放心，也可以把属性设为透明 (双保险)
                actor->GetCenterlineProperty(0)->SetOpacity(0);
                actor->GetCenterlineProperty(1)->SetOpacity(0);
                actor->GetCenterlineProperty(2)->SetOpacity(0);
            }
        }
    }
    // 6. 刷新
    m_views[i].viewer->GetRenderer()->ResetCamera();
    m_views[i].viewer->Render();


    // 4. 设置窗宽窗位
    viewer->SetColorWindow(m_smartWindow);
    viewer->SetColorLevel(m_smartLevel);

    // 5. UI 配置
    int minSlice = viewer->GetSliceMin();
    int maxSlice = viewer->GetSliceMax();
    int midSlice = (minSlice + maxSlice) / 2;

    if (m_views[i].slider) {
        m_views[i].slider->setEnabled(true);
        m_views[i].slider->setRange(minSlice, maxSlice);
        m_views[i].slider->blockSignals(true);
        m_views[i].slider->setValue(midSlice);
        m_views[i].slider->blockSignals(false);
    }

    // 6. 设置初始切片
    viewer->SetSlice(midSlice);
    updateOverlaySlice(i);

    // ============================================================
    // 【质感升级 1：给二维窗口打一盏 3D 侧光灯】
    // 默认的无影灯会让圆柱体看起来像扁平的长方形。
    // 我们从左上方打一盏灯，人为制造出金属圆柱的高光和阴影过渡！
    // ============================================================
    //vtkRenderer* ren2D = viewer->GetRenderer();
    //ren2D->AutomaticLightCreationOff(); // 关掉默认的无影灯
    //ren2D->RemoveAllLights();           // 清理干净
    //vtkNew<vtkLight> sideLight;
    //sideLight->SetLightTypeToCameraLight(); // 跟着相机走
    //sideLight->SetPosition(-1.0, 1.0, 1.0); // 从左上方 45 度打光
    //sideLight->SetIntensity(1.0);
    //ren2D->AddLight(sideLight);
    // --- 补丁：双光源摄影棚灯光 ---
    vtkRenderer* ren2D = viewer->GetRenderer();
    ren2D->RemoveAllLights();
    // 1. 主灯：左上方，强光，负责产生金属高光
    vtkNew<vtkLight> keyLight;
    keyLight->SetLightTypeToCameraLight(); // 【核心修复】：必须加上这句！让灯光死死绑在相机头上！
    keyLight->SetPosition(-1, 1, 1);
    keyLight->SetIntensity(0.8);
    ren2D->AddLight(keyLight);

    // ============================================================
    // 【质感升级 2：切片半透明透视魔法】
    // 让脑部切片变成 85% 的“深色玻璃”。
    // 这样穿透到切片后方的针不会完全消失，而是呈现出真实的“埋入组织”的发灰暗淡感！
    // ============================================================
    if (viewer->GetImageActor()) {
        viewer->GetImageActor()->GetProperty()->SetOpacity(0.85); // 0.85 是黄金比例
    }

    // 7. 方向标签 (四边居中单字母 vtkTextActor)
    {
        auto makeLabel = [&](const char* txt, double nx, double ny) -> vtkSmartPointer<vtkTextActor> {
            vtkSmartPointer<vtkTextActor> ta = vtkSmartPointer<vtkTextActor>::New();
            ta->SetInput(txt);
            ta->GetPositionCoordinate()->SetCoordinateSystemToNormalizedViewport();
            ta->GetPositionCoordinate()->SetValue(nx, ny);
            ta->GetTextProperty()->SetColor(0.9, 0.9, 0.9);
            ta->GetTextProperty()->SetFontSize(16);
            ta->GetTextProperty()->SetOpacity(0.7);
            ta->GetTextProperty()->BoldOn();
            ta->GetTextProperty()->ShadowOff();
            ta->GetTextProperty()->SetJustificationToCentered();
            ta->GetTextProperty()->SetVerticalJustificationToCentered();
            viewer->GetRenderer()->AddActor2D(ta);
            return ta;
        };
        const char *tTop = "S", *tBot = "I", *tLft = "R", *tRgt = "L";
        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) {
            tTop = "A"; tBot = "P"; tLft = "R"; tRgt = "L";
        } else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) {
            tTop = "S"; tBot = "I"; tLft = "R"; tRgt = "L";
        } else {
            tTop = "S"; tBot = "I"; tLft = "A"; tRgt = "P";
        }
        m_views[i].orientTop    = makeLabel(tTop, 0.50, 0.96);
        m_views[i].orientBottom = makeLabel(tBot, 0.50, 0.04);
        m_views[i].orientLeft   = makeLabel(tLft, 0.03, 0.50);
        m_views[i].orientRight  = makeLabel(tRgt, 0.97, 0.50);
        // 右、下方向字母默认隐藏（避让刻度尺标签）；用中点小三角替代
        m_views[i].orientBottom->SetVisibility(0);
        m_views[i].orientRight ->SetVisibility(0);
    }

    // 8. 智能距离标尺 (vtkAxisActor2D × 2)
    {
        auto makeRuler = [&](bool vertical) -> vtkSmartPointer<vtkAxisActor2D> {
            vtkSmartPointer<vtkAxisActor2D> ax = vtkSmartPointer<vtkAxisActor2D>::New();
            ax->GetPoint1Coordinate()->SetCoordinateSystemToNormalizedViewport();
            ax->GetPoint2Coordinate()->SetCoordinateSystemToNormalizedViewport();
            ax->SetNumberOfLabels(5);
            ax->SetLabelFormat("%.0f");
            ax->SetTickLength(4);
            ax->SetMinorTickLength(2);
            ax->SetNumberOfMinorTicks(1);
            ax->GetProperty()->SetColor(0.75, 0.75, 0.75);
            ax->GetProperty()->SetLineWidth(1.0);
            ax->GetLabelTextProperty()->SetColor(0.75, 0.75, 0.75);
            ax->GetLabelTextProperty()->SetFontSize(10);
            ax->GetLabelTextProperty()->ShadowOff();
            ax->GetLabelTextProperty()->BoldOff();
            ax->GetTitleTextProperty()->SetColor(0.75, 0.75, 0.75);
            ax->GetTitleTextProperty()->SetFontSize(10);
            ax->GetTitleTextProperty()->ShadowOff();
            ax->SetTitle("mm");
            ax->SetTitlePosition(1.0); // "mm" 放在标尺端头而非中间
            viewer->GetRenderer()->AddActor2D(ax);
            return ax;
        };
        m_views[i].rulerV.axis = makeRuler(true);
        m_views[i].rulerH.axis = makeRuler(false);
        // 重置标尺状态（重新加载图像时关键）
        m_views[i].rulerV.manuallyAdjusted   = false;
        m_views[i].rulerH.manuallyAdjusted   = false;
        m_views[i].rulerV.halfLenNormInitial = 0.0;
        m_views[i].rulerH.halfLenNormInitial = 0.0;
        m_views[i].rulerV.centerNorm         = 0.5;
        m_views[i].rulerH.centerNorm         = 0.5;
    }

    // 8b. 视图中心三角标记 (替代右/下方向字母, 标记整个视图中心)
    {
        auto makeTriangle = [&](bool vertical) -> vtkSmartPointer<vtkActor2D> {
            // 使用 NormalizedViewport: 顶点紧贴 ruler 内侧, 朝内指
            // 垂直 ruler 在 x=0.92, 三角朝左, 中心在 (0.92, 0.5)
            // 水平 ruler 在 y=0.06, 三角朝上, 中心在 (0.5, 0.06)
            const double size = 0.018;  // 视口比例尺寸
            vtkSmartPointer<vtkPoints> pts = vtkSmartPointer<vtkPoints>::New();
            if (vertical) {
                // 朝左的等腰三角: 顶点在 (0.92-size, 0.5), 底边在 x=0.92
                pts->InsertNextPoint(0.92 - size, 0.5,        0.0); // tip
                pts->InsertNextPoint(0.92,        0.5 + size, 0.0); // top
                pts->InsertNextPoint(0.92,        0.5 - size, 0.0); // bot
            } else {
                // 朝上的等腰三角: 顶点在 (0.5, 0.06+size), 底边在 y=0.06
                pts->InsertNextPoint(0.5,        0.06 + size, 0.0); // tip
                pts->InsertNextPoint(0.5 - size, 0.06,        0.0); // left
                pts->InsertNextPoint(0.5 + size, 0.06,        0.0); // right
            }
            vtkSmartPointer<vtkPolygon> poly = vtkSmartPointer<vtkPolygon>::New();
            poly->GetPointIds()->SetNumberOfIds(3);
            poly->GetPointIds()->SetId(0, 0);
            poly->GetPointIds()->SetId(1, 1);
            poly->GetPointIds()->SetId(2, 2);
            vtkSmartPointer<vtkCellArray> cells = vtkSmartPointer<vtkCellArray>::New();
            cells->InsertNextCell(poly);
            vtkSmartPointer<vtkPolyData> pd = vtkSmartPointer<vtkPolyData>::New();
            pd->SetPoints(pts);
            pd->SetPolys(cells);

            vtkSmartPointer<vtkPolyDataMapper2D> mapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
            mapper->SetInputData(pd);
            // 让坐标按 NormalizedViewport 解释
            vtkSmartPointer<vtkCoordinate> coord = vtkSmartPointer<vtkCoordinate>::New();
            coord->SetCoordinateSystemToNormalizedViewport();
            mapper->SetTransformCoordinate(coord);

            vtkSmartPointer<vtkActor2D> act = vtkSmartPointer<vtkActor2D>::New();
            act->SetMapper(mapper);
            act->GetProperty()->SetColor(0.75, 0.75, 0.75);
            act->GetProperty()->SetOpacity(0.85);
            act->PickableOff();
            viewer->GetRenderer()->AddActor2D(act);
            return act;
        };
        m_views[i].centerTriV = makeTriangle(true);
        m_views[i].centerTriH = makeTriangle(false);
    }

    // 9. 相机适配
    viewer->GetRenderer()->ResetCamera();
    // 10. 标尺初始布局 (必须在 ResetCamera 之后，相机参数才有效)
    updateRulerLayout(i);
    viewer->Render();
}

void dicomviewer_3d::applySolidStyle(vtkSmartPointer<vtkVolumeProperty> prop)
{
    if (!vtkImage) return;

    // 1. 获取范围
    double range[2];
    vtkImage->GetScalarRange(range);
    double minVal = range[0];
    double maxVal = range[1];
    double diff = maxVal - minVal;

    // 2. 计算直方图 (采样点数)
    int numBins = 3000;
    if (diff > 1.0) {
        numBins = (int)std::ceil(diff);
        if (numBins > 3000) numBins = 3000;
    }

    vtkNew<vtkImageAccumulate> histogram;
    histogram->SetInputData(vtkImage);
    histogram->SetComponentExtent(0, numBins - 1, 0, 0, 0, 0);
    histogram->SetComponentOrigin(minVal, 0, 0);
    histogram->SetComponentSpacing(diff / numBins, 0, 0);
    histogram->Update();

    long long* bins = static_cast<long long*>(histogram->GetOutput()->GetScalarPointer());

    // 3. 智能寻找背景截止点 (去除背景底噪)
    long long maxBinCount = 0;
    int peakBinIndex = 0;
    // 在前 5% 里找背景峰值
    for (int i = 0; i < numBins / 20; ++i) {
        if (bins[i] > maxBinCount) {
            maxBinCount = bins[i];
            peakBinIndex = i;
        }
    }
    // 向右寻找背景结束点
    int startBin = 0;
    for (int i = peakBinIndex; i < numBins / 20; ++i) {
        if (bins[i] < maxBinCount * 0.02 && i > numBins * 0.005) {
            startBin = i;
            break;
        }
    }
    if (startBin == 0) startBin = numBins * 0.05;

    // 计算有效体素总数
    long long validVoxels = 0;
    for (int i = startBin; i < numBins; ++i) validVoxels += bins[i];

    // 4. 构建传输函数
    vtkNew<vtkPiecewiseFunction> opacity;
    vtkNew<vtkColorTransferFunction> color;

    // 背景部分：强制透明
    double startVal = minVal + startBin * (diff / numBins);
    opacity->AddPoint(minVal, 0.0);
    opacity->AddPoint(startVal, 0.0);
    color->AddRGBPoint(minVal, 0.0, 0.0, 0.0);
    color->AddRGBPoint(startVal, 0.0, 0.0, 0.0);

    // 有效部分：CDF 映射
    long long accumulated = 0;
    for (int i = startBin; i < numBins; ++i) {
        accumulated += bins[i];
        double currentVal = minVal + i * (diff / numBins);
        double cdf = (validVoxels > 0) ? (double)accumulated / validVoxels : 0.0;

        // 不透明度映射 (S形曲线)
        double opVal = std::pow(cdf, 1.3);
        if (opVal > 0.95) opVal = 0.95;
        opacity->AddPoint(currentVal, opVal);

        // 颜色映射 (灰度)
        color->AddRGBPoint(currentVal, cdf, cdf, cdf);
    }

    // 5. 设置梯度不透明度 (增强边缘立体感)
    vtkNew<vtkPiecewiseFunction> gradientOpacity;
    gradientOpacity->AddPoint(0, 0.0);
    gradientOpacity->AddPoint(10, 1.0);

    // 6. 应用属性
    prop->SetColor(color);
    prop->SetScalarOpacity(opacity);
    prop->SetGradientOpacity(gradientOpacity);

    prop->ShadeOn();
    prop->SetAmbient(0.1);
    prop->SetDiffuse(0.9);
    prop->SetSpecular(0.2);
    prop->SetSpecularPower(15);
    prop->SetInterpolationTypeToLinear();
}

void dicomviewer_3d::applyGlassStyle(vtkSmartPointer<vtkVolumeProperty> prop, double shellOpacity, double internalOpacity, double edgeThreshold) // 新增参数
{
    if (!vtkImage) return;

    // 显式开启梯度不透明度！
    prop->DisableGradientOpacityOff();

    double minVal = m_smartLevel - (m_smartWindow / 2.0);
    double maxVal = m_smartLevel + (m_smartWindow / 2.0);

    // 1. 颜色 (Color)
    vtkNew<vtkColorTransferFunction> color;
    color->AddRGBPoint(minVal, 0.0, 0.0, 0.0);
    color->AddRGBPoint(m_smartLevel, 0.5, 0.5, 0.5);
    color->AddRGBPoint(maxVal, 0.9, 0.9, 0.9);

    //// 2. 标量不透明度 (Scalar Opacity)
    //vtkNew<vtkPiecewiseFunction> opacity;
    //opacity->AddPoint(minVal, 0.0);
    //opacity->AddPoint(minVal + (m_smartWindow * 0.1), 0.0);
    //opacity->AddPoint(minVal + (m_smartWindow * 0.4), internalOpacity); // 内部雾度
    //opacity->AddPoint(maxVal, shellOpacity); // 外壳强度

    ////---------------------------------
    double lowerBound = m_smartLevel - (m_smartWindow / 2.0);
    double upperBound = m_smartLevel + (m_smartWindow / 2.0);
    double airThreshold = minVal + (m_smartWindow * internalOpacity * 0.5);
    // 保护：防止 lowerBound 小于数据最小值
    double range[2];
    vtkImage->GetScalarRange(range);
    if (lowerBound < range[0]) lowerBound = range[0];
    // 2. 设置标量不透明度 (Scalar Opacity)
    vtkNew<vtkPiecewiseFunction> opacity;
    opacity->AddPoint(lowerBound - 1000, 0.0); // 极小值处透明
    opacity->AddPoint(airThreshold, 0.0);      // 【关键】在这个阈值之前，必须是 0 透明度
    opacity->AddPoint(airThreshold + 1, 0.7);  // 超过阈值，立刻变成不透明 (1.0)
    //opacity->AddPoint(upperBound , 1.0); // 极大值处保持不透明
    opacity->AddPoint(maxVal + 1000, 1.0); // 极大值处保持不透明
    ////-----------------------------------

    // 3. 梯度不透明度 (Gradient Opacity) - 【核心修改】
    vtkNew<vtkPiecewiseFunction> gradientOpacity;
    // 使用 edgeThreshold 来动态计算梯度的分段点
    // edgeThreshold 范围建议 0.0 ~ 2.0 (相对于 Window 的倍数)
    double gradLower = m_smartWindow * edgeThreshold;       // 阈值：低于这个梯度的都被忽略
    double gradUpper = m_smartWindow * (edgeThreshold + 0.2); // 强边界点
    gradientOpacity->AddPoint(0.0, 0.0);
    gradientOpacity->AddPoint(gradLower, 0.0); // 【关键】忽略纹理细节
    // 过渡区
    gradientOpacity->AddPoint(gradLower + (gradUpper - gradLower) * 0.2, shellOpacity * 0.5);
    // 强边界
    gradientOpacity->AddPoint(gradUpper, 1.0);

    // 4. 材质
    prop->ShadeOn();
    //prop->SetAmbient(0.15);
    //prop->SetDiffuse(0.7);
    //prop->SetSpecular(0.5); // 高光强一点
    //prop->SetSpecularPower(40);`
    prop->SetAmbient(0.3);// 环境光：调高一点，避免暗部死黑
    prop->SetDiffuse(0.8);// 漫反射：主要光源
    prop->SetSpecular(0.1); // 高光：调低！切面太亮会反光，影响观察纹理
    prop->SetSpecularPower(10);
    prop->ShadeOff();

    prop->SetColor(color);
    prop->SetScalarOpacity(opacity);
    prop->SetGradientOpacity(gradientOpacity);
    prop->SetInterpolationTypeToLinear();

}
/*void dicomviewer_3d::applyGlassStyle(vtkSmartPointer<vtkVolumeProperty> prop, double shellOpacity, double internalOpacity, double edgeThreshold) // 新增参数
{
    if (!vtkImage) return;

    // 显式开启梯度不透明度！
    prop->DisableGradientOpacityOff();

    double minVal = m_smartLevel - (m_smartWindow / 2.0);
    double maxVal = m_smartLevel + (m_smartWindow / 2.0);

    // 1. 颜色 (Color)
    vtkNew<vtkColorTransferFunction> color;
    color->AddRGBPoint(minVal, 0.0, 0.0, 0.0);
    color->AddRGBPoint(m_smartLevel, 0.5, 0.5, 0.5);
    color->AddRGBPoint(maxVal, 0.9, 0.9, 0.9);

    // 2. 标量不透明度 (Scalar Opacity)
    vtkNew<vtkPiecewiseFunction> opacity;
    opacity->AddPoint(minVal, 0.0);
    opacity->AddPoint(minVal + (m_smartWindow * 0.1), 0.0);
    opacity->AddPoint(minVal + (m_smartWindow * 0.4), internalOpacity); // 内部雾度
    opacity->AddPoint(maxVal, shellOpacity); // 外壳强度

    // 3. 梯度不透明度 (Gradient Opacity) - 【核心修改】
    vtkNew<vtkPiecewiseFunction> gradientOpacity;
    // 使用 edgeThreshold 来动态计算梯度的分段点
    // edgeThreshold 范围建议 0.0 ~ 2.0 (相对于 Window 的倍数)
    double gradLower = m_smartWindow * edgeThreshold;       // 阈值：低于这个梯度的都被忽略
    double gradUpper = m_smartWindow * (edgeThreshold + 0.2); // 强边界点
    gradientOpacity->AddPoint(0.0, 0.0);
    gradientOpacity->AddPoint(gradLower, 0.0); // 【关键】忽略纹理细节
    // 过渡区
    gradientOpacity->AddPoint(gradLower + (gradUpper - gradLower) * 0.2, shellOpacity * 0.5);
    // 强边界
    gradientOpacity->AddPoint(gradUpper, 1.0);

    // 4. 材质
    prop->ShadeOn();
    prop->SetAmbient(0.15);
    prop->SetDiffuse(0.7);
    prop->SetSpecular(0.5); // 高光强一点
    prop->SetSpecularPower(40);

    prop->SetColor(color);
    prop->SetScalarOpacity(opacity);
    prop->SetGradientOpacity(gradientOpacity);
    prop->SetInterpolationTypeToLinear();

}*/

void dicomviewer_3d::slot_update3DParams(double shell, double internal, double edge)
{
    // 1. 更新数据源
    m_glassShell = shell;
    m_glassInternal = internal;
    m_glassEdge = edge;

    // 2. 如果当前是 Solid 模式，拖滑块不应立即生效（或者你可以选择让它切回 Glass 模式）
    // 这里我们选择：只有在 Glass 模式下才实时刷新
    if (!m_isSolidMode) {
        // 调用统一入口，不要自己去调 applyGlassStyle
        updateVolumeState();
    }
}

void dicomviewer_3d::slot_reset3DParams()
{
    // 更新 UI，这会触发 sig3DParamsChanged，进而更新 VTK
    m_controlPanel->set3DParamsUI(0.8, 0.2, 0.1);
}

// 【新增槽函数】响应 UI 复选框
void dicomviewer_3d::slot_setRenderingMode(bool isSolid)
{
    if (!m_currentVolume) return;

    vtkVolumeProperty* prop = m_currentVolume->GetProperty();

    if (isSolid) {
        // 切换到实体模式 (Slicer 风格)
        applySolidCutStyle(prop);
    }
    else {
        // 切换回玻璃模式 (你原本的风格)
        // 获取当前滑块的值 (这里你可以存下来，或者直接读取 UI 默认值)
        // 假设这里用默认值，或者你可以让 ControlPanel 传过来
        double shell = 0.8;
        double internal = 0.2;
        double edge = 0.1;
        applyGlassStyle(prop, shell, internal, edge);
    }

    vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::slot_show3DPlane(int axis, bool show)
{
    if (axis < 0 || axis >= 3) return;

    if (m_impWidgets[axis]) {
        if (show) m_impWidgets[axis]->On();
        else m_impWidgets[axis]->Off();
    }

    if (m_sliceActors[axis]) {
        m_sliceActors[axis]->SetVisibility(show);
    }

    vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::slot_showPlaneArrows(bool show)
{
    m_showPlaneArrows = show;
    for (int i = 0; i < 3; i++) {
        if (!m_impWidgets[i]) continue;
        vtkImplicitPlaneRepresentation* rep =
            vtkImplicitPlaneRepresentation::SafeDownCast(m_impWidgets[i]->GetRepresentation());
        if (!rep) continue;
        rep->GetNormalProperty()->SetOpacity(show ? 1.0 : 0.0);
        rep->GetSelectedNormalProperty()->SetOpacity(show ? 1.0 : 0.0);
        rep->Modified();
    }
    vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::slot_showOrientationBox(bool show)
{
    m_showOrientationBox = show;
    if (m_outlineActor) {
        m_outlineActor->SetVisibility(show ? 1 : 0);
    }
    updateOrientationMarkers();
    vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::slot_toggleTrajectoryMode()
{
    m_isTrajectoryMode = !m_isTrajectoryMode;

    if (m_isTrajectoryMode) {
        if (!m_trajManager->hasEntry || !m_trajManager->hasTarget) {
            QMessageBox::warning(this, "提示", "请先选定靶点和进针点并生成手术针！");
            m_isTrajectoryMode = false;
            return;
        }

        // 计算矩阵！中心点设为靶点，这样一切换就能直接看到核团！
        vtkNew<vtkMatrix4x4> matProbe, matInline1, matInline2;
        if (m_trajManager->getTrajectoryMatrices(m_trajManager->targetPos, matProbe, matInline1, matInline2)) {

            // 视窗 0 (Axial) 变成 Probe's Eye (探针垂直视角)
            applyObliqueMatrixToView(0, matProbe);
            //m_views[0].titleLabel->setText("Probe's Eye (探针视角)");

            // 视窗 1 (Coronal) 变成 Inline 1 (纵切视角)
            applyObliqueMatrixToView(1, matInline1);
            //m_views[1].titleLabel->setText("Inline 1 (纵切视角)");

            // 视窗 2 (Sagittal) 变成 Inline 2 (纵切侧视角)
            applyObliqueMatrixToView(2, matInline2);
            //m_views[2].titleLabel->setText("Inline 2 (纵切视角)");

            m_controlPanel->btnToggleTrajectoryMode->setText("还原标准视图");
            m_controlPanel->btnToggleTrajectoryMode->setStyleSheet("QPushButton { background-color: #E6A23C; color: white; }"); // 变橙色提示

        }
    }
    else {
        // 关闭模式，调用你现成的复位函数！
        slot_resetPlanes();
        //m_views[0].titleLabel->setText("Axial (轴状位)");
        //m_views[1].titleLabel->setText("Coronal (冠状位)");
        //m_views[2].titleLabel->setText("Sagittal (矢状位)");
        m_controlPanel->btnToggleTrajectoryMode->setText("开启轨迹对齐视角");
        m_controlPanel->btnToggleTrajectoryMode->setStyleSheet("QPushButton { background-color: #409EFF; color: white; }");
    }

    if (vtkWidget3D->isVisible()) vtkWidget3D->GetRenderWindow()->Render();
}


void dicomviewer_3d::updateOrientationMarkers()
{
    if (!vtkImage || !ren3d) return;

    if (!m_showOrientationBox) {
        for (auto& label : m_orientationLabels) {
            if (label) {
                label->SetVisibility(false);
            }
        }
        return;
    }

    double bounds[6];
    vtkImage->GetBounds(bounds);
    double center[3] = {
        (bounds[0] + bounds[1]) * 0.5,
        (bounds[2] + bounds[3]) * 0.5,
        (bounds[4] + bounds[5]) * 0.5
    };

    double sizeX = bounds[1] - bounds[0];
    double sizeY = bounds[3] - bounds[2];
    double sizeZ = bounds[5] - bounds[4];
    double padX = (sizeX > 0.0) ? sizeX * 0.05 : 5.0;
    double padY = (sizeY > 0.0) ? sizeY * 0.05 : 5.0;
    double padZ = (sizeZ > 0.0) ? sizeZ * 0.05 : 5.0;

    struct LabelInfo {
        const char* text;
        double pos[3];
    } labels[6] = {
        { "l", { bounds[0] - padX, center[1], center[2] } },
        { "r", { bounds[1] + padX, center[1], center[2] } },
        { "b", { center[0], bounds[2] - padY, center[2] } },
        { "f", { center[0], bounds[3] + padY, center[2] } },
        { "d", { center[0], center[1], bounds[4] - padZ } },
        { "u", { center[0], center[1], bounds[5] + padZ } }
    };

    for (int i = 0; i < 6; i++) {
        if (!m_orientationLabels[i]) {
            m_orientationLabels[i] = vtkSmartPointer<vtkBillboardTextActor3D>::New();
            m_orientationLabels[i]->GetTextProperty()->SetFontSize(18);
            m_orientationLabels[i]->GetTextProperty()->SetColor(0.95, 0.95, 0.95);
            m_orientationLabels[i]->GetTextProperty()->SetBold(1);
            m_orientationLabels[i]->GetTextProperty()->SetShadow(1);
            ren3d->AddActor(m_orientationLabels[i]);
        }

        m_orientationLabels[i]->SetInput(labels[i].text);
        m_orientationLabels[i]->SetPosition(labels[i].pos);
        m_orientationLabels[i]->SetVisibility(true);
    }
}

void dicomviewer_3d::handlePick(double* pos)
{
    if (!vtkImage) return;

    double bounds[6];
    vtkImage->GetBounds(bounds);
    bool inside = (pos[0] >= bounds[0] && pos[0] <= bounds[1] &&
        pos[1] >= bounds[2] && pos[1] <= bounds[3] &&
        pos[2] >= bounds[4] && pos[2] <= bounds[5]);

    if (!inside) { qDebug() << "点击无效：超出图像范围"; return; }

    // -------------------------------------------------------
    // AC-PC 标定模式：优先处理，不进入 target/entry 逻辑
    // -------------------------------------------------------
    if (m_acpcPickMode != ACPC_NONE) {
        commitAcPcPoint(m_acpcPickMode, pos);
        return;
    }

    // -------------------------------------------------------
    // 原有 target/entry 选点逻辑（不变）
    // -------------------------------------------------------
    double color[3];
    bool isTarget = false;

    if (!m_trajManager->hasTarget) {
        std::copy(pos, pos + 3, m_trajManager->targetPos);
        m_trajManager->hasTarget = true;
        color[0] = 1.0; color[1] = 0.0; color[2] = 0.0;
        isTarget = true;
        qDebug() << "【靶点】设定";
    }
    else if (!m_trajManager->hasEntry) {
        std::copy(pos, pos + 3, m_trajManager->entryPos);
        m_trajManager->hasEntry = true;
        color[0] = 0.0; color[1] = 1.0; color[2] = 0.0;
        isTarget = false;
        qDebug() << "【进针点】设定";
    }
    else {
        QMessageBox::information(this, "提示", "已有靶点和进针点。");
        return;
    }

    vtkNew<vtkSphereSource> sphere;
    sphere->SetCenter(pos);
    sphere->SetRadius(1.5);
    vtkNew<vtkPolyDataMapper> sphereMapper;
    sphereMapper->SetInputConnection(sphere->GetOutputPort());

    vtkSmartPointer<vtkActor> sphereActor = vtkSmartPointer<vtkActor>::New();
    sphereActor->SetMapper(sphereMapper);
    sphereActor->GetProperty()->SetColor(color);

    if (isTarget) m_trajManager->actorTargetSphere = sphereActor;
    else m_trajManager->actorEntrySphere = sphereActor;
    ren3d->AddActor(sphereActor);

    double origin2[3], spacing2[3];
    vtkImage->GetOrigin(origin2);
    vtkImage->GetSpacing(spacing2);

    for (int i = 0; i < 3; i++) {
        vtkSmartPointer<vtkActor> cross = m_trajManager->createCrossActor(pos, color);

        int orientation = m_views[i].viewer->GetSliceOrientation();
        int sliceIdx = 0;
        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY)
            sliceIdx = static_cast<int>(std::round((pos[2] - origin2[2]) / spacing2[2]));
        else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ)
            sliceIdx = static_cast<int>(std::round((pos[1] - origin2[1]) / spacing2[1]));
        else
            sliceIdx = static_cast<int>(std::round((pos[0] - origin2[0]) / spacing2[0]));

        if (isTarget) { m_trajManager->targetCrossArr[i] = cross; m_views[i].targetSliceIndex = sliceIdx; }
        else          { m_trajManager->entryCrossArr[i]  = cross; m_views[i].entrySliceIndex  = sliceIdx; }

        m_views[i].viewer->GetRenderer()->AddActor(cross);
    }

    jumpToPosition(pos);
    refreshCoordinateDisplay();
}

void dicomviewer_3d::undoPick()
{
    bool changed = false;

    if (m_trajManager->hasEntry) {
        m_trajManager->hasEntry = false;
        if (m_trajManager->actorEntrySphere) ren3d->RemoveActor(m_trajManager->actorEntrySphere);

        for (int i = 0; i < 3; i++) {
            if (m_trajManager->entryCrossArr[i]) {
                m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->entryCrossArr[i]);
                m_trajManager->entryCrossArr[i] = nullptr;
            }
        }
        m_trajManager->actorEntrySphere = nullptr;
        qDebug() << "【撤销】进针点已清除";
        changed = true;
    }
    else if (m_trajManager->hasTarget) {
        m_trajManager->hasTarget = false;
        if (m_trajManager->actorTargetSphere) ren3d->RemoveActor(m_trajManager->actorTargetSphere);

        for (int i = 0; i < 3; i++) {
            if (m_trajManager->targetCrossArr[i]) {
                m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->targetCrossArr[i]);
                m_trajManager->targetCrossArr[i] = nullptr;
            }
        }
        m_trajManager->actorTargetSphere = nullptr;
        qDebug() << "【撤销】靶点已清除";
        changed = true;
    }

    // --- 适配新架构的隐藏逻辑 ---
    // 只要处于显示状态，一旦撤销了靶点/进针点，就立刻把针藏起来
    if (m_trajManager->m_dbsLead && m_trajManager->m_dbsLead->GetAssembly3D()->GetVisibility()) {
        vtkResliceImageViewer* viewers[3] = { m_views[0].viewer, m_views[1].viewer, m_views[2].viewer };
        m_trajManager->clearNeedleActors(ren3d, viewers);
        m_controlPanel->btnToggleNeedle->setText("生成手术针");
    }

    if (changed) {
        refreshAllViews();
    }
}

void dicomviewer_3d::refreshAllViews()
{
    // 1. 刷新 3 个 2D 视图
    for (int i = 0; i < 3; i++) {
        if (m_views[i].viewer && m_views[i].viewer->GetInput()) {

            // 【关键】在渲染前，先触发一次显隐逻辑检查
            // 确保所有标记（十字、标签）的状态都是最新的
            updateOverlaySlice(i);

            // 【关键】重置相机裁剪范围，防止黑屏
            m_views[i].viewer->GetRenderer()->ResetCameraClippingRange();

            // 执行渲染
            m_views[i].viewer->Render();
        }
    }

    // 2. 刷新 3D 视图
    if (ren3d) {
        ren3d->ResetCameraClippingRange();
        vtkWidget3D->GetRenderWindow()->Render();
    }
}

void dicomviewer_3d::jumpToPosition(double* pos)
{
    if (!vtkImage) return;

    double origin[3], spacing[3];
    vtkImage->GetOrigin(origin);
    vtkImage->GetSpacing(spacing);

    for (int i = 0; i < 3; i++) {
        // 计算该坐标在当前视图下的切片索引
        int sliceIndex = 0;
        int orientation = m_views[i].viewer->GetSliceOrientation();

        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) {
            sliceIndex = std::round((pos[2] - origin[2]) / spacing[2]);
        }
        else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) {
            sliceIndex = std::round((pos[1] - origin[1]) / spacing[1]);
        }
        else {
            sliceIndex = std::round((pos[0] - origin[0]) / spacing[0]);
        }

        // 执行跳转
        m_views[i].viewer->SetSlice(sliceIndex);

        // 同步 UI 和 Overlay
        updateSliderUI(i, sliceIndex);
        updateOverlaySlice(i);
    }

    // 统一刷新
    refreshAllViews();
}

void dicomviewer_3d::applySolidCutStyle(vtkSmartPointer<vtkVolumeProperty> prop)
{
    if (!vtkImage) return;

    // 1. 基于窗宽窗位计算动态范围
    // lowerBound: 低于这个值的认为是背景 (全黑/透明)
    // upperBound: 高于这个值的认为是高亮组织 (全白/不透明)
    double lowerBound = m_smartLevel - (m_smartWindow / 2.0);
    double upperBound = m_smartLevel + (m_smartWindow / 2.0);
    double airThreshold = lowerBound + (m_smartWindow * 0.14);

    // 保护：防止 lowerBound 小于数据最小值
    double range[2];
    vtkImage->GetScalarRange(range);
    if (lowerBound < range[0]) lowerBound = range[0];

    // 2. 设置标量不透明度 (Scalar Opacity)
    vtkNew<vtkPiecewiseFunction> opacity;
    opacity->AddPoint(lowerBound - 1000, 0.0); // 极小值处透明
    opacity->AddPoint(airThreshold, 0.0);      // 【关键】在这个阈值之前，必须是 0 透明度
    opacity->AddPoint(airThreshold + 1, 0.7);  // 超过阈值，立刻变成不透明 (1.0)
    //opacity->AddPoint(upperBound , 1.0); // 极大值处保持不透明
    opacity->AddPoint(upperBound + 1000, 1.0); // 极大值处保持不透明

    prop->SetScalarOpacity(opacity);

    // 3. 设置颜色传输函数 (Color Transfer Function)
    // 模拟灰度图像：从黑到白
    vtkNew<vtkColorTransferFunction> color;
    color->AddRGBPoint(lowerBound, 0.0, 0.0, 0.0); // 黑
    color->AddRGBPoint(upperBound, 1.0, 1.0, 1.0); // 白
    prop->SetColor(color);


    //prop->ShadeOn();// 开启阴影，恢复 3D 立体感
    prop->SetAmbient(0.3);// 环境光：调高一点，避免暗部死黑
    prop->SetDiffuse(0.8);// 漫反射：主要光源
    prop->SetSpecular(0.1); // 高光：调低！切面太亮会反光，影响观察纹理
    prop->SetSpecularPower(10);

    // 4. 【核心中的核心】禁用梯度不透明度
    prop->DisableGradientOpacityOn();
    // 或者用 prop->SetGradientOpacity(nullptr);

    // 5. 关闭阴影 (可选)
    // 对于观察切面纹理，关闭光照阴影通常看得更清楚
    prop->ShadeOff();
    //prop->ShadeOn();
    // 6. 线性插值
    prop->SetInterpolationTypeToLinear();

}
/* void dicomviewer_3d::applySolidCutStyle(vtkSmartPointer<vtkVolumeProperty> prop)
{
    if (!vtkImage) return;

    // 1. 准备阈值
    // 我们不再用那个生硬的 airThreshold，而是定义一个“爬坡区间”
    double lowerBound = m_smartLevel - (m_smartWindow / 2.0);
    double upperBound = m_smartLevel + (m_smartWindow / 2.0);

    // 定义斜坡的起点和终点 (关键调参点)
    // start: 低于此值完全透明 (去除背景)
    // end: 高于此值完全不透明 (脑实质)
    // 经验值：在窗宽下限往上一点开始爬坡，爬坡长度约为窗宽的 10%-15%
    double rampStart = lowerBound + (m_smartWindow * 0.1);
    double rampEnd = rampStart + (m_smartWindow * 0.15);

    // 2. 标量不透明度 (Scalar Opacity) - 使用斜坡(Ramp)而非阶梯(Step)
    vtkNew<vtkPiecewiseFunction> opacity;
    opacity->AddPoint(lowerBound - 1000, 0.0);
    opacity->AddPoint(rampStart, 0.0);  // 起点：透明
    opacity->AddPoint(rampEnd, 0.7);    // 终点：实心 (这就是为什么切面是白的，因为这里是1.0)
    opacity->AddPoint(upperBound + 1000, 1.0);
    prop->SetScalarOpacity(opacity);

    // 3. 颜色 (Color) - 保持线性灰度
    vtkNew<vtkColorTransferFunction> color;
    color->AddRGBPoint(lowerBound, 0.0, 0.0, 0.0);
    color->AddRGBPoint(upperBound, 1.0, 1.0, 1.0);
    prop->SetColor(color);


    //prop->ShadeOn();// 开启阴影，恢复 3D 立体感
    prop->SetAmbient(0.3);// 环境光：调高一点，避免暗部死黑
    prop->SetDiffuse(0.8);// 漫反射：主要光源
    prop->SetSpecular(0.1); // 高光：调低！切面太亮会反光，影响观察纹理
    prop->SetSpecularPower(10);

    // 4. 【核心中的核心】禁用梯度不透明度
    prop->DisableGradientOpacityOn();
    // 或者用 prop->SetGradientOpacity(nullptr);

    // 5. 关闭阴影 (可选)
    // 对于观察切面纹理，关闭光照阴影通常看得更清楚
    prop->ShadeOff();
    //prop->ShadeOn();
    // 6. 线性插值
    prop->SetInterpolationTypeToLinear();

}*/

void dicomviewer_3d::updateVolumeState()
{
    if (!m_currentVolume || !m_roiManager) return;

    // 1. 渲染模式 (Solid vs Glass) - 保持不变
    vtkVolumeProperty* prop = m_currentVolume->GetProperty();
    if (m_isSolidMode) {
        applySolidCutStyle(prop);
    }
    else {
        applyGlassStyle(prop, m_glassShell, m_glassInternal, m_glassEdge);
    }

    // 2. ROI 状态逻辑 (修改这里！)
    if (m_isRoiEnabled) {
        // 用户开启了 ROI 功能：必须开启裁剪
        m_roiManager->setCroppingEnabled(true);

        // 框显示与否，取决于 m_hideRoiBox
        if (m_hideRoiBox) {
            m_roiManager->setBoxVisible(false); // 裁剪开，框关 (这就是你要的效果)
            // 【新增修复】隐藏框后，强制重置相机的裁剪范围，防止相机“穿模”
            ren3d->ResetCameraClippingRange();
        }
        else {
            m_roiManager->setBoxVisible(true);  // 裁剪开，框开 (正常调节模式)
        }
    }
    else {
        // 用户彻底关闭了 ROI：裁剪关，框也关
        m_roiManager->setCroppingEnabled(false);
        m_roiManager->setBoxVisible(false);
        // 【新增修复】关闭 ROI 时也要重置一下，以防万一
        ren3d->ResetCameraClippingRange();
    }

    // 3. 刷新
    vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::initPlaneWidgets()
{
    if (!vtkImage || !ren3d) return;

    vtkRenderWindowInteractor* interactor = vtkWidget3D->GetRenderWindow()->GetInteractor();

    // 定义颜色 (红、绿、黄)
    double colors[3][3] = { {1,0,0}, {0,1,0}, {1,1,0} };

    // 创建回调命令 (复用之前的 callback，稍后会修改它的内容)
    vtkNew<vtkCallbackCommand> interactionCallback;
    interactionCallback->SetCallback(dicomviewer_3d::onPlaneInteraction);
    interactionCallback->SetClientData(this);

    for (int i = 0; i < 3; i++) {
        // --------------------------------------------------------
        // 1. 初始化数据平面 (数学模型)
        // --------------------------------------------------------
        m_slicePlanes[i] = vtkSmartPointer<vtkPlane>::New();

        // 获取当前 2D 视图的位置来初始化
        if (m_views[i].viewer) {
            vtkResliceCursor* cursor = m_views[i].viewer->GetResliceCursor();
            if (cursor) {
                // 映射关系：View0->Plane2, View1->Plane1, View2->Plane0
                int planeId = (i == 0) ? 2 : (i == 1 ? 1 : 0);
                m_slicePlanes[i]->SetNormal(cursor->GetPlane(planeId)->GetNormal());
                m_slicePlanes[i]->SetOrigin(cursor->GetCenter());
            }
        }

        // --------------------------------------------------------
        // 2. 初始化显示管线 (Mapper + Actor)
        // --------------------------------------------------------
        m_sliceMappers[i] = vtkSmartPointer<vtkImageResliceMapper>::New();
        m_sliceMappers[i]->SetInputData(vtkImage);
        m_sliceMappers[i]->SetSlicePlane(m_slicePlanes[i]); // 绑定平面！
        m_sliceMappers[i]->BorderOff(); // 不要Mapper自带的边框，用Widget的

        m_sliceActors[i] = vtkSmartPointer<vtkImageSlice>::New();
        m_sliceActors[i]->SetMapper(m_sliceMappers[i]);
        m_sliceActors[i]->GetProperty()->SetColorWindow(m_smartWindow);
        m_sliceActors[i]->GetProperty()->SetColorLevel(m_smartLevel);
        m_sliceActors[i]->GetProperty()->SetInterpolationTypeToLinear();
        m_sliceActors[i]->SetVisibility(false);

        // 添加到场景 (Layer 0, 和脑子在一起)
        //ren3d->AddActor(m_sliceActors[i]);
        ren3d->AddViewProp(m_sliceActors[i]);

        // --------------------------------------------------------
        // 3. 初始化交互 Widget (那个带箭头的框)
        // --------------------------------------------------------
        m_impWidgets[i] = vtkSmartPointer<vtkImplicitPlaneWidget2>::New();
        m_impWidgets[i]->SetInteractor(interactor);
        m_impWidgets[i]->SetCurrentRenderer(ren3d);

        // 设置表现层 (外观)
        auto rep = vtkSmartPointer<vtkImplicitPlaneRepresentation>::New();
        rep->SetPlaceFactor(1.0); // 框的大小比例
        rep->PlaceWidget(vtkImage->GetBounds()); // 根据图像大小放置框

        // 【关键交互设置】模仿 Slicer
        rep->DrawPlaneOff();       // 不画中间那个半透明的板子 (我们要显示真的切片图)
        rep->OutlineTranslationOff(); // 关闭点框移动 (Slicer通常靠箭头根部移动)
        rep->ScaleEnabledOff();    // 关闭缩放 (切片通常不需要缩放)

        // 开启箭头和外框
        rep->SetNormalToCamera();  // 初始重置
        rep->SetPlane(m_slicePlanes[i]); // 让框的位置和切片对齐

        // 设置颜色 (让箭头和框变成红/绿/黄)
        // Edges: 外框
        rep->GetEdgesProperty()->SetColor(colors[i]);
        rep->GetEdgesProperty()->SetLineWidth(2.0);
        // Normal: 箭头
        rep->GetNormalProperty()->SetColor(colors[i]);
        rep->GetNormalProperty()->SetLineWidth(3.0);
        rep->GetNormalProperty()->SetOpacity(m_showPlaneArrows ? 1.0 : 0.0);
        // Selected: 选中变色 (也可以设为一样以防闪烁，这里暂且保留高亮)
        rep->GetSelectedNormalProperty()->SetColor(1, 0, 1); // 选中变紫
        rep->GetSelectedNormalProperty()->SetOpacity(m_showPlaneArrows ? 1.0 : 0.0);

        m_impWidgets[i]->SetRepresentation(rep);

        // 添加监听
        m_impWidgets[i]->AddObserver(vtkCommand::InteractionEvent, interactionCallback);

        // 默认隐藏，等待复选框开启
        m_impWidgets[i]->Off();
    }
}

void dicomviewer_3d::onPlaneInteraction(vtkObject* caller, unsigned long eventId, void* clientData, void* callData)
{
    dicomviewer_3d* self = static_cast<dicomviewer_3d*>(clientData);
    vtkImplicitPlaneWidget2* widget = static_cast<vtkImplicitPlaneWidget2*>(caller);

    if (!self || !widget) return;

    // 1. 确定视图索引
    int viewIndex = -1;
    if (widget == self->m_impWidgets[0]) viewIndex = 0;
    else if (widget == self->m_impWidgets[1]) viewIndex = 1;
    else if (widget == self->m_impWidgets[2]) viewIndex = 2;

    if (viewIndex == -1) return;

    // 2. 获取 Widget 拖动后的新数据
    vtkImplicitPlaneRepresentation* rep = static_cast<vtkImplicitPlaneRepresentation*>(widget->GetRepresentation());
    double newCenter[3], newNormal[3];
    rep->GetPlane(self->m_slicePlanes[viewIndex]);
    self->m_slicePlanes[viewIndex]->GetOrigin(newCenter);
    self->m_slicePlanes[viewIndex]->GetNormal(newNormal);

    vtkResliceImageViewer* viewer = self->m_views[viewIndex].viewer;
    if (!viewer) return;

    // 3. 提取底层游标和矩阵信息
    vtkResliceCursorWidget* w = viewer->GetResliceCursorWidget();
    vtkResliceCursorRepresentation* r = w ? vtkResliceCursorRepresentation::SafeDownCast(w->GetRepresentation()) : nullptr;
    vtkImageReslice* reslice = r ? vtkImageReslice::SafeDownCast(r->GetReslice()) : nullptr;

    // ========================================================================
    // 【核心修复：计算真实位移量 Shift，保护平移 (Pan) 不被重置】
    // ========================================================================
    double oldCenter[3];
    if (r && r->GetResliceCursor()) r->GetResliceCursor()->GetCenter(oldCenter);
    else std::copy(newCenter, newCenter + 3, oldCenter);

    double shift[3] = { newCenter[0] - oldCenter[0], newCenter[1] - oldCenter[1], newCenter[2] - oldCenter[2] };

    // ========================================================================
    // 逻辑分流
    // ========================================================================
    if (self->m_isTrajectoryMode)
    {
        // --------------------------------------------------------------------
        // A. 轨迹对齐模式：绝对锁定法线，严禁旋转！
        // --------------------------------------------------------------------
        if (reslice) {
            vtkMatrix4x4* mat = reslice->GetResliceAxes();

            // 提取出正确的切面法线 (Z轴) 和 向上方向 (Y轴)
            double fixedNormal[3] = { mat->GetElement(0,2), mat->GetElement(1,2), mat->GetElement(2,2) };
            double viewUp[3] = { mat->GetElement(0,1), mat->GetElement(1,1), mat->GetElement(2,1) };

            // 1. 只更新平移位置
            mat->SetElement(0, 3, newCenter[0]);
            mat->SetElement(1, 3, newCenter[1]);
            mat->SetElement(2, 3, newCenter[2]);
            reslice->SetResliceAxes(mat);

            // 2. 强制把 3D 红框被鼠标拉歪的法线掰正回 fixedNormal
            rep->SetNormal(fixedNormal);
            self->m_slicePlanes[viewIndex]->SetNormal(fixedNormal);

            // 3. 同步游标中心
            if (r->GetResliceCursor()) {
                r->GetResliceCursor()->SetCenter(newCenter);
                r->GetResliceCursor()->GetPlane(2)->SetNormal(fixedNormal);
            }

            // 4. 同步摄像机，防黑屏
            vtkCamera* cam = viewer->GetRenderer()->GetActiveCamera();
            if (cam) {
                double distance = 100.0; // 恢复最普通的 100.0 距离
                double newPos[3] = { newCenter[0] + fixedNormal[0] * distance, newCenter[1] + fixedNormal[1] * distance, newCenter[2] + fixedNormal[2] * distance };
                cam->SetFocalPoint(newCenter);
                cam->SetPosition(newPos);
                cam->SetViewUp(viewUp);
            }
        }
    }
    else
    {
        // --------------------------------------------------------------------
        // B & C. 非轨迹模式：判断是普通斜切 还是 轴对齐
        // --------------------------------------------------------------------
        int orientation = viewer->GetSliceOrientation();
        bool isAxisAligned = false;
        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) isAxisAligned = std::abs(newNormal[2]) > 0.999;
        else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) isAxisAligned = std::abs(newNormal[1]) > 0.999;
        else isAxisAligned = std::abs(newNormal[0]) > 0.999;

        if (!isAxisAligned) {
            // --- B. 普通斜切模式 ---
            double axisX[3], axisY[3], axisZ[3];
            axisZ[0] = newNormal[0]; axisZ[1] = newNormal[1]; axisZ[2] = newNormal[2];
            vtkMath::Normalize(axisZ);

            double viewUp[3] = { 0, 0, 1 };
            if (viewIndex == 0) { viewUp[0] = 0; viewUp[1] = -1; viewUp[2] = 0; }
            else if (viewIndex == 1) { viewUp[0] = 0; viewUp[1] = 0; viewUp[2] = 1; }
            else if (viewIndex == 2) { viewUp[0] = 0; viewUp[1] = 0; viewUp[2] = 1; }

            if (std::abs(vtkMath::Dot(axisZ, viewUp)) > 0.99) { viewUp[0] += 0.1; vtkMath::Normalize(viewUp); }
            vtkMath::Cross(viewUp, axisZ, axisX); vtkMath::Normalize(axisX);
            vtkMath::Cross(axisZ, axisX, axisY);  vtkMath::Normalize(axisY);

            vtkNew<vtkMatrix4x4> matrix;
            matrix->SetElement(0, 0, axisX[0]); matrix->SetElement(1, 0, axisX[1]); matrix->SetElement(2, 0, axisX[2]); matrix->SetElement(3, 0, 0);
            matrix->SetElement(0, 1, axisY[0]); matrix->SetElement(1, 1, axisY[1]); matrix->SetElement(2, 1, axisY[2]); matrix->SetElement(3, 1, 0);
            matrix->SetElement(0, 2, axisZ[0]); matrix->SetElement(1, 2, axisZ[1]); matrix->SetElement(2, 2, axisZ[2]); matrix->SetElement(3, 2, 0);
            matrix->SetElement(0, 3, newCenter[0]); matrix->SetElement(1, 3, newCenter[1]); matrix->SetElement(2, 3, newCenter[2]); matrix->SetElement(3, 3, 1);

            if (reslice) {
                reslice->SetResliceAxes(matrix);
                if (r && r->GetResliceCursor()) {
                    r->GetResliceCursor()->SetCenter(newCenter);
                    r->GetResliceCursor()->GetPlane(2)->SetNormal(newNormal);
                }
            }
            viewer->SetResliceModeToOblique();
            if (self->m_views[viewIndex].style) viewer->GetRenderWindow()->GetInteractor()->SetInteractorStyle(self->m_views[viewIndex].style);

            vtkCamera* cam = viewer->GetRenderer()->GetActiveCamera();
            if (cam) {
                double distance = cam->GetDistance();
                if (distance <= 0.0) distance = 100.0;
                double newPos[3] = { newCenter[0] + axisZ[0] * distance, newCenter[1] + axisZ[1] * distance, newCenter[2] + axisZ[2] * distance };
                cam->SetFocalPoint(newCenter);
                cam->SetPosition(newPos);
                cam->SetViewUp(axisY);
                cam->OrthogonalizeViewUp();
            }
        }
        else {
            // --- C. 轴对齐模式 ---
            if (self->vtkImage) {
                double origin[3], spacing[3];
                self->vtkImage->GetOrigin(origin);
                self->vtkImage->GetSpacing(spacing);

                int sliceIndex = 0;
                if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) {
                    sliceIndex = static_cast<int>(std::round((newCenter[2] - origin[2]) / spacing[2]));
                }
                else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) {
                    sliceIndex = static_cast<int>(std::round((newCenter[1] - origin[1]) / spacing[1]));
                }
                else {
                    sliceIndex = static_cast<int>(std::round((newCenter[0] - origin[0]) / spacing[0]));
                }

                int minSlice = viewer->GetSliceMin();
                int maxSlice = viewer->GetSliceMax();
                if (sliceIndex < minSlice) sliceIndex = minSlice;
                if (sliceIndex > maxSlice) sliceIndex = maxSlice;

                viewer->SetSlice(sliceIndex);
                self->updateSliderUI(viewIndex, sliceIndex);
            }

            // 同步焦点 (没有复杂的 shift)
            vtkCamera* cam = viewer->GetRenderer()->GetActiveCamera();
            if (cam) {
                double viewDir[3]; cam->GetDirectionOfProjection(viewDir);
                double distance = cam->GetDistance();
                if (distance <= 0.0) distance = 100.0;
                double newPos[3] = { newCenter[0] - viewDir[0] * distance, newCenter[1] - viewDir[1] * distance, newCenter[2] - viewDir[2] * distance };
                cam->SetFocalPoint(newCenter);
                cam->SetPosition(newPos);
            }
        }
    }

    viewer->GetRenderer()->ResetCameraClippingRange();
    viewer->Render();
}

void dicomviewer_3d::slot_resetPlanes()
{
    if (!vtkImage) return;

    // 获取图像中心，作为复位的原点
    double center[3];
    vtkImage->GetCenter(center);

    for (int i = 0; i < 3; i++) {
        // 【修正1】变量名改为 m_impWidgets
        if (!m_impWidgets[i]) continue;

        // 记录之前的显隐状态
        int wasEnabled = m_impWidgets[i]->GetEnabled();

        // 【修正2】获取表现层 (Representation) 来设置方向
        vtkImplicitPlaneRepresentation* rep =
            vtkImplicitPlaneRepresentation::SafeDownCast(m_impWidgets[i]->GetRepresentation());

        double normal[3] = { 0.0, 0.0, 0.0 };
        if (i == 0) {
            normal[2] = 1.0; // Axial
        }
        else if (i == 1) {
            normal[1] = 1.0; // Coronal
        }
        else {
            normal[0] = 1.0; // Sagittal
        }

        m_slicePlanes[i]->SetOrigin(center);
        m_slicePlanes[i]->SetNormal(normal);

        if (rep) {
            rep->SetPlane(m_slicePlanes[i]);
            rep->Modified();
        }

        // 恢复显隐状态
        if (wasEnabled) {
            m_impWidgets[i]->On();
        }
        else {
            m_impWidgets[i]->Off();
        }

        if (m_sliceActors[i]) {
            m_sliceActors[i]->SetVisibility(wasEnabled);
        }
    }

    // 2D 视图恢复到轴对齐中心切片
    int orientations[3] = {
        vtkResliceImageViewer::SLICE_ORIENTATION_XY,
        vtkResliceImageViewer::SLICE_ORIENTATION_XZ,
        vtkResliceImageViewer::SLICE_ORIENTATION_YZ
    };

    for (int i = 0; i < 3; i++) {
        vtkResliceImageViewer* viewer = m_views[i].viewer;
        if (!viewer) continue;

        viewer->SetSliceOrientation(orientations[i]);
        viewer->SetResliceModeToAxisAligned();
        // ===== 【新增这 3 行补丁】 =====
        if (m_views[i].style) {
            viewer->GetRenderWindow()->GetInteractor()->SetInteractorStyle(m_views[i].style);
        }
        // ============================

        int minSlice = viewer->GetSliceMin();
        int maxSlice = viewer->GetSliceMax();
        int midSlice = (minSlice + maxSlice) / 2;
        viewer->SetSlice(midSlice);

        //updateSliderUI(i, midSlice);
        // 【新增：把滑动条的范围重置回物理层数】
        if (m_views[i].slider) {
            m_views[i].slider->blockSignals(true);
            m_views[i].slider->setRange(minSlice, maxSlice);
            m_views[i].slider->setValue(midSlice);
            m_views[i].slider->blockSignals(false);
        }

        updateOverlaySlice(i);

        viewer->GetRenderer()->ResetCamera();
        m_views[i].rulerV.manuallyAdjusted = false;
        m_views[i].rulerH.manuallyAdjusted = false;
        m_views[i].rulerV.axis->SetLabelFormat("%.0f");
        m_views[i].rulerH.axis->SetLabelFormat("%.0f");
        updateRulerLayout(i);
        viewer->Render();
    }

    vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::focusOnROI(double* targetPos, double viewSizeMM) {
    if (!vtkImage) return;
    double focusPoint[3];
    if (targetPos) {
        focusPoint[0] = targetPos[0]; focusPoint[1] = targetPos[1]; focusPoint[2] = targetPos[2];
    }
    else if (m_trajManager->hasTarget) {
        focusPoint[0] = m_trajManager->targetPos[0]; focusPoint[1] = m_trajManager->targetPos[1]; focusPoint[2] = m_trajManager->targetPos[2];
    }
    else {
        vtkImage->GetCenter(focusPoint);
    }
    jumpToPosition(focusPoint);
    if (ren3d) {
        vtkCamera* cam = ren3d->GetActiveCamera();
        cam->SetFocalPoint(focusPoint);
        double currentPos[3]; cam->GetPosition(currentPos);
        double direction[3] = { currentPos[0] - focusPoint[0], currentPos[1] - focusPoint[1], currentPos[2] - focusPoint[2] };
        vtkMath::Normalize(direction);
        cam->SetPosition(focusPoint[0] + direction[0] * 150.0, focusPoint[1] + direction[1] * 150.0, focusPoint[2] + direction[2] * 150.0);
        ren3d->ResetCameraClippingRange();
        vtkWidget3D->GetRenderWindow()->Render();
    }
}

void dicomviewer_3d::advanceSliceOblique(int viewIndex, int direction)
{
    vtkResliceImageViewer* viewer = m_views[viewIndex].viewer;
    if (!viewer) return;

    vtkResliceCursor* cursor = viewer->GetResliceCursor();
    if (!cursor) return;

    // 1. 获取当前中心点和法线
    double center[3], normal[3];
    cursor->GetCenter(center);
    cursor->GetPlane(2)->GetNormal(normal);

    // 2. 算步长
    double spacing[3] = { 1.0, 1.0, 1.0 };
    if (vtkImage) vtkImage->GetSpacing(spacing);
    double step = std::min(std::min(spacing[0], spacing[1]), spacing[2]);

    // 3. 沿法线推进
    double newCenter[3];
    for (int i = 0; i < 3; i++) {
        newCenter[i] = center[i] + normal[i] * step * (double)direction;
    }
    cursor->SetCenter(newCenter);

    // 4. 【关键同步】：把推算出的新中心，喂给 3D 里面的数学平面和彩色交互框！
    if (m_slicePlanes[viewIndex]) {
        m_slicePlanes[viewIndex]->SetOrigin(newCenter);
    }
    if (m_impWidgets[viewIndex]) {
        vtkImplicitPlaneRepresentation* rep = vtkImplicitPlaneRepresentation::SafeDownCast(m_impWidgets[viewIndex]->GetRepresentation());
        if (rep) rep->SetOrigin(newCenter);
    }

    // 5. 简单的相机跟随
    vtkCamera* cam = viewer->GetRenderer()->GetActiveCamera();
    if (cam) {
        double viewDir[3];
        cam->GetDirectionOfProjection(viewDir);
        double distance = cam->GetDistance();
        if (distance <= 0.0) distance = 100.0;

        double newPos[3] = { newCenter[0] - viewDir[0] * distance, newCenter[1] - viewDir[1] * distance, newCenter[2] - viewDir[2] * distance };
        cam->SetFocalPoint(newCenter);
        cam->SetPosition(newPos);
    }

    // 6. 估算滑动条位置
    if (vtkImage) {
        double origin[3];
        vtkImage->GetOrigin(origin);
        int approxSlice = 0;
        int orientation = viewer->GetSliceOrientation();
        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) approxSlice = std::round((newCenter[2] - origin[2]) / spacing[2]);
        else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) approxSlice = std::round((newCenter[1] - origin[1]) / spacing[1]);
        else approxSlice = std::round((newCenter[0] - origin[0]) / spacing[0]);

        updateSliderUI(viewIndex, approxSlice);
    }

    viewer->GetRenderer()->ResetCameraClippingRange();
    viewer->Render();
    if (vtkWidget3D->isVisible()) vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::toggleMaximizeView(int viewIndex)
{
    // 如果点击的是已经放大的那个，那就执行“还原” (-1)
    if (m_maximizedViewIndex == viewIndex) {
        viewIndex = -1;
    }

    // 为了防止逻辑错乱，如果我们正在看一个放大的 2D 视图，又双击了左边另一个小 2D 视图
    // 我们先强制还原一次，再放大新的
    if (m_maximizedViewIndex != -1 && viewIndex != -1) {
        toggleMaximizeView(-1);
    }

    if (viewIndex == -1) {
        // ========== 【执行还原】 ==========
        if (m_maximizedViewIndex == -1) return; // 已经是还原状态，啥也不做

        // 1. 获取正霸占着右边大窗口的 2D 视图
        QWidget* currentMax = m_views[m_maximizedViewIndex].container;

        // 2. 把它从右边踢掉，把委屈在左边小格子里的 3D 窗口也踢掉
        m_layoutRight->removeWidget(currentMax);
        m_layoutLeft->removeWidget(vtkWidget3D);

        // 3. 各回各家
        m_layoutLeft->insertWidget(m_maximizedViewIndex, currentMax); // 2D回到左边原来的顺位
        m_layoutRight->addWidget(vtkWidget3D, 7);                     // 3D回到右边(带拉伸因子7)

        m_maximizedViewIndex = -1; // 状态标记为正常
    }
    else {
        // ========== 【执行放大】 ==========
        // 1. 揪出被双击的那个 2D 视图
        QWidget* targetWidget = m_views[viewIndex].container;

        // 2. 把它从左边拔出来，把右边的 3D 窗口也拔出来
        m_layoutLeft->removeWidget(targetWidget);
        m_layoutRight->removeWidget(vtkWidget3D);

        // 3. 交换位置
        m_layoutLeft->insertWidget(viewIndex, vtkWidget3D); // 3D塞进左边空出来的小格子里
        m_layoutRight->addWidget(targetWidget, 7);          // 2D塞进右边大格子里(带拉伸因子7)

        m_maximizedViewIndex = viewIndex; // 记录当前霸主
    }

    // 布局变化后 viewport 尺寸异步更新，延迟刷新所有标尺使其适配新视口
    QTimer::singleShot(50, this, [this]() {
        for (int i = 0; i < 3; i++) updateRulerLayout(i);
        refreshAllViews();
    });
}

void dicomviewer_3d::slot_snapToNeedle()
{
    // 只有在轨迹模式下，且有靶点时才生效
    if (!m_isTrajectoryMode || !m_trajManager || !m_trajManager->hasTarget) return;

    // 1. 重新获取以靶点为绝对中心的矩阵
    vtkNew<vtkMatrix4x4> matProbe, matInline1, matInline2;
    if (m_trajManager->getTrajectoryMatrices(m_trajManager->targetPos, matProbe, matInline1, matInline2)) {

        // 2. 强行将三个视图的切面拉回靶点中心
        //applyObliqueMatrixToView(0, matProbe);
        //applyObliqueMatrixToView(1, matInline1);
        //applyObliqueMatrixToView(2, matInline2);

        // 3. 把三个滑动条的刻度乖乖拉回 1000 中点
        for (int i = 0; i < 3; i++) {
            if (m_views[i].slider) {
                m_views[i].slider->blockSignals(true);
                m_views[i].slider->setValue(1000);
                m_views[i].slider->setProperty("last_val", 1000);
                m_views[i].slider->blockSignals(false);
            }
        }
    }

    // 4. 刷新
    if (vtkWidget3D->isVisible()) vtkWidget3D->GetRenderWindow()->Render();
}

void dicomviewer_3d::applyObliqueMatrixToView(int viewIndex, vtkMatrix4x4* matrix)
{
    vtkResliceImageViewer* viewer = m_views[viewIndex].viewer;
    if (!viewer) return;

    vtkResliceCursorWidget* w = viewer->GetResliceCursorWidget();
    if (w) {
        vtkResliceCursorRepresentation* r = vtkResliceCursorRepresentation::SafeDownCast(w->GetRepresentation());
        if (r) {
            vtkImageReslice* reslice = vtkImageReslice::SafeDownCast(r->GetReslice());
            if (reslice) reslice->SetResliceAxes(matrix);

            // 提取矩阵中的中心点和法线，赋值给游标
            double center[3] = { matrix->GetElement(0,3), matrix->GetElement(1,3), matrix->GetElement(2,3) };
            double normal[3] = { matrix->GetElement(0,2), matrix->GetElement(1,2), matrix->GetElement(2,2) };
            if (r->GetResliceCursor()) {
                r->GetResliceCursor()->SetCenter(center);
                r->GetResliceCursor()->GetPlane(2)->SetNormal(normal);
            }
        }
    }

    // 强行设为斜切模式，并防篡改交互器！
    viewer->SetResliceModeToOblique();
    if (m_views[viewIndex].style) {
        viewer->GetRenderWindow()->GetInteractor()->SetInteractorStyle(m_views[viewIndex].style);
    }

    // 同步给 3D 里面的数学平面
    double center[3] = { matrix->GetElement(0,3), matrix->GetElement(1,3), matrix->GetElement(2,3) };
    double normal[3] = { matrix->GetElement(0,2), matrix->GetElement(1,2), matrix->GetElement(2,2) };
    if (m_slicePlanes[viewIndex]) {
        m_slicePlanes[viewIndex]->SetOrigin(center);
        m_slicePlanes[viewIndex]->SetNormal(normal);
    }

    // 【新增/修改】：强制更新 3D Widget 框的表现层
    if (m_impWidgets[viewIndex]) {
        auto rep = vtkImplicitPlaneRepresentation::SafeDownCast(m_impWidgets[viewIndex]->GetRepresentation());
        if (rep) {
            rep->SetPlane(m_slicePlanes[viewIndex]); // 强制依据数学平面更新
            rep->SetOrigin(center);
            rep->SetNormal(normal);
            rep->Modified();
        }
        m_impWidgets[viewIndex]->Modified(); // 唤醒 Widget
    }

    // 更新相机的朝向，让它死死盯着这个斜面
    vtkCamera* cam = viewer->GetRenderer()->GetActiveCamera();
    if (cam) {
        double viewUp[3] = { matrix->GetElement(0,1), matrix->GetElement(1,1), matrix->GetElement(2,1) };
        double distance = cam->GetDistance();
        //if (distance <= 0.0) distance = 100.0;
        if (distance <= 0.0) distance = 1000.0;

        double newPos[3] = {
            center[0] + normal[0] * distance,
            center[1] + normal[1] * distance,
            center[2] + normal[2] * distance
        };
        cam->SetFocalPoint(center);
        cam->SetPosition(newPos);
        cam->SetViewUp(viewUp);
        cam->OrthogonalizeViewUp();

        // 裁剪范围 (限制厚度，防止 2D 视图太乱)
        //cam->SetClippingRange(distance - 5.0, distance + 5.0);
    }

    // ==========================================================
    // 👇👇👇 【在这里插入：扩大相机景深，防止针被剔除】 👇👇👇
    // ==========================================================
    viewer->GetRenderer()->ResetCameraClippingRange();
    if (cam) {
        double clipRange[2];
        cam->GetClippingRange(clipRange);
        cam->SetClippingRange(0.1, clipRange[1] + 150.0);
    }
    // ==========================================================

    //viewer->GetRenderer()->ResetCameraClippingRange();
    viewer->Render();
}

void dicomviewer_3d::toggleSimulator()
{
    m_isSimulatorOpen = !m_isSimulatorOpen;

    if (m_isSimulatorOpen) {
        m_btnToggleSimulator->setText("❌ 关闭模拟器");
        m_btnToggleSimulator->setStyleSheet("QPushButton { background-color: #F56C6C; color: white; font-weight: bold; border-radius: 4px; padding: 6px; }");
        m_cmbSingleSliceView->show();
        m_leadSimulator->show();
        
        // 自动触发一次切面选择，以隐藏多余的容器
        onSingleSliceViewChanged(m_cmbSingleSliceView->currentIndex());
    } else {
        m_btnToggleSimulator->setText("🚀 开启电极程控模拟器");
        m_btnToggleSimulator->setStyleSheet("QPushButton { background-color: #67C23A; color: white; font-weight: bold; border-radius: 4px; padding: 6px; }");
        m_cmbSingleSliceView->hide();
        m_leadSimulator->hide();

        // 恢复显示所有 3 个 2D 切面容器
        for (int i = 0; i < 3; i++) {
            if (m_views[i].container) m_views[i].container->show();
        }
    }
}
\

void dicomviewer_3d::onSingleSliceViewChanged(int index)
{
    if (!m_isSimulatorOpen) return;

    // 只显示选中的容器，隐藏其他两个
    for (int i = 0; i < 3; i++) {
        if (m_views[i].container) {
            if (i == index) m_views[i].container->show();
            else m_views[i].container->hide();
        }
    }
}

#include <vtkPropPicker.h>
#include <vtkAssemblyPath.h>
#include <vtkAssemblyNode.h>
#include <vtkProp.h>

bool dicomviewer_3d::isMainLeadPicked(vtkPropPicker* picker)
{
    if (!m_trajManager || !m_trajManager->m_dbsLead) return false;
    vtkAssemblyPath* path = picker->GetPath();
    if (!path) return false;

    path->InitTraversal();
    vtkAssemblyNode* node;
    while ((node = path->GetNextNode())) {
        if (node->GetViewProp() == m_trajManager->m_dbsLead->GetAssembly3D().GetPointer()) {
            return true;
        }
    }
    return false;
}

void dicomviewer_3d::dragMainLeadDepth(double offset)
{
    if (!m_trajManager || !m_trajManager->m_dbsLead) return;
    
    double currentDepth = m_trajManager->m_dbsLead->getDepthOffset();
    m_trajManager->m_dbsLead->setDepthOffset(currentDepth - offset);
    
    // 【关键】：更新主视图电极的空间位姿
    m_trajManager->m_dbsLead->UpdateTrajectory(m_trajManager->entryPos, m_trajManager->targetPos);
    
    // 只刷新包含该电极的视图
    if (vtkWidget3D && vtkWidget3D->isVisible()) {
        vtkWidget3D->GetRenderWindow()->Render();
    }
    for (int i = 0; i < 3; i++) {
        if (m_views[i].viewer) {
            m_views[i].viewer->Render();
        }
    }
    calculateVTAIntersection();
}

void dicomviewer_3d::translateMainLead(double offset[3])
{
    if (!m_trajManager || !m_trajManager->m_dbsLead) return;
    
    // 同时平移进针点和靶点，保持电极方向和长度不变
    for (int i=0; i<3; i++) {
        m_trajManager->entryPos[i] += offset[i];
        m_trajManager->targetPos[i] += offset[i];
    }
    
    // 更新电极模型
    m_trajManager->m_dbsLead->UpdateTrajectory(m_trajManager->entryPos, m_trajManager->targetPos);
    
    // 如果存在轨迹球，也一起平移过去
    if (m_trajManager->actorTargetSphere) {
        m_trajManager->actorTargetSphere->SetPosition(m_trajManager->targetPos);
    }
    if (m_trajManager->actorEntrySphere) {
        m_trajManager->actorEntrySphere->SetPosition(m_trajManager->entryPos);
    }
    
    // 只刷新包含该电极的视图
    if (vtkWidget3D && vtkWidget3D->isVisible()) {
        vtkWidget3D->GetRenderWindow()->Render();
    }
    for (int i = 0; i < 3; i++) {
        if (m_views[i].viewer) {
            m_views[i].viewer->Render();
        }
    }
    calculateVTAIntersection();
}

// =====================================================================
// 阶段 A: 临床效果分析 (VTA & 触点距离)
// =====================================================================
void dicomviewer_3d::calculateVTAIntersection()
{
    if (!m_controlPanel || !m_trajManager || !m_trajManager->m_dbsLead) return;

    double vtaCenter[3];
    double vtaRadius = m_trajManager->m_dbsLead->getVTARadius();
    bool hasVTA = (m_trajManager->m_dbsLead->getVTACenterWorld(vtaCenter) && vtaRadius > 0.0);
    bool hasRealVTA = m_trajManager->m_dbsLead->hasRealVTA();

    std::vector<double> coverages = {0.0, 0.0, 0.0};
    std::vector<double> distances(4, 9999.0);
    int bestContact = -1;

    if (!vtkLabelImage) {
        m_controlPanel->updateAnalysisUI(coverages, distances, -1);
        return;
    }

    // ==========================================
    // 1. 体素采样法计算 3 个核团的 VTA 覆盖率
    // ==========================================
    if (hasRealVTA) {
        // 真实 VTA: 用 vtkImplicitPolyDataDistance 判断体素是否在 VTA 内
        auto realPoly = m_trajManager->m_dbsLead->getRealVTAPoly();
        vtkNew<vtkImplicitPolyDataDistance> implicitVTA;
        implicitVTA->SetInput(realPoly);

        double vtaBounds[6];
        realPoly->GetBounds(vtaBounds);

        double spacing[3], origin[3];
        vtkLabelImage->GetSpacing(spacing);
        vtkLabelImage->GetOrigin(origin);
        double voxelVol = spacing[0] * spacing[1] * spacing[2];
        int extents[6];
        vtkLabelImage->GetExtent(extents);

        int minIdx[3], maxIdx[3];
        for (int i = 0; i < 3; i++) {
            minIdx[i] = static_cast<int>(std::floor((vtaBounds[i*2] - origin[i]) / spacing[i]));
            maxIdx[i] = static_cast<int>(std::ceil((vtaBounds[i*2+1] - origin[i]) / spacing[i]));
            if (minIdx[i] < extents[i*2]) minIdx[i] = extents[i*2];
            if (maxIdx[i] > extents[i*2+1]) maxIdx[i] = extents[i*2+1];
        }

        double vtaVoxelCount = 0;
        double overlapVol[3] = {0.0, 0.0, 0.0};
        for (int k = minIdx[2]; k <= maxIdx[2]; k++) {
            for (int j = minIdx[1]; j <= maxIdx[1]; j++) {
                for (int i = minIdx[0]; i <= maxIdx[0]; i++) {
                    double pt[3] = { origin[0] + i*spacing[0], origin[1] + j*spacing[1], origin[2] + k*spacing[2] };
                    double d = implicitVTA->EvaluateFunction(pt);
                    if (d <= 0.0) {  // 内部或表面
                        vtaVoxelCount++;
                        double val = vtkLabelImage->GetScalarComponentAsDouble(i, j, k, 0);
                        int labelInt = static_cast<int>(std::round(val));
                        if (labelInt >= 1 && labelInt <= 3) {
                            overlapVol[labelInt - 1] += voxelVol;
                        }
                    }
                }
            }
        }
        double vtaVol = vtaVoxelCount * voxelVol;
        if (vtaVol > 0) {
            for (int i = 0; i < 3; i++) {
                coverages[i] = (overlapVol[i] / vtaVol) * 100.0;
                if (coverages[i] > 100.0) coverages[i] = 100.0;
            }
        }
    }
    else if (hasVTA) {
        // 球形 VTA 回退
        double spacing[3];
        vtkLabelImage->GetSpacing(spacing);
        double voxelVol = spacing[0] * spacing[1] * spacing[2];
        double vtaVol = (4.0 / 3.0) * vtkMath::Pi() * vtaRadius * vtaRadius * vtaRadius;

        double bounds[6] = {
            vtaCenter[0] - vtaRadius, vtaCenter[0] + vtaRadius,
            vtaCenter[1] - vtaRadius, vtaCenter[1] + vtaRadius,
            vtaCenter[2] - vtaRadius, vtaCenter[2] + vtaRadius
        };

        double origin[3];
        vtkLabelImage->GetOrigin(origin);
        int minIdx[3], maxIdx[3];
        int extents[6];
        vtkLabelImage->GetExtent(extents);

        for (int i=0; i<3; i++) {
            minIdx[i] = static_cast<int>(std::floor((bounds[i*2] - origin[i]) / spacing[i]));
            maxIdx[i] = static_cast<int>(std::ceil((bounds[i*2+1] - origin[i]) / spacing[i]));
            if (minIdx[i] < extents[i*2]) minIdx[i] = extents[i*2];
            if (maxIdx[i] > extents[i*2+1]) maxIdx[i] = extents[i*2+1];
        }

        double overlapVol[3] = {0.0, 0.0, 0.0};
        double r2 = vtaRadius * vtaRadius;

        for (int k = minIdx[2]; k <= maxIdx[2]; k++) {
            for (int j = minIdx[1]; j <= maxIdx[1]; j++) {
                for (int i = minIdx[0]; i <= maxIdx[0]; i++) {
                    double pt[3] = { origin[0] + i*spacing[0], origin[1] + j*spacing[1], origin[2] + k*spacing[2] };
                    double dist2 = vtkMath::Distance2BetweenPoints(pt, vtaCenter);
                    if (dist2 <= r2) {
                        double val = vtkLabelImage->GetScalarComponentAsDouble(i, j, k, 0);
                        int labelInt = static_cast<int>(std::round(val));
                        if (labelInt >= 1 && labelInt <= 3) {
                            overlapVol[labelInt - 1] += voxelVol;
                        }
                    }
                }
            }
        }

        if (vtaVol > 0) {
            for (int i=0; i<3; i++) {
                coverages[i] = (overlapVol[i] / vtaVol) * 100.0;
                if (coverages[i] > 100.0) coverages[i] = 100.0;
            }
        }
    }

    // ==========================================
    // 2. 触点到目标核团(通常是 Label 1 作为主核团，或者综合表面) 的最短距离
    // ==========================================
    // 这里我们生成所有三个靶核 (1,2,3) 的混合表面，以此评估触点距离最近的脑深部核团表面
    vtkNew<vtkDiscreteMarchingCubes> dmc;
    dmc->SetInputData(vtkLabelImage);
    dmc->GenerateValues(3, 1, 3); // 生成 Label 1 到 3 的多面体
    dmc->Update();
    
    vtkPolyData* nucleusPoly = dmc->GetOutput();
    if (nucleusPoly && nucleusPoly->GetNumberOfPoints() > 0) {
        vtkNew<vtkImplicitPolyDataDistance> implicitDist;
        implicitDist->SetInput(nucleusPoly);
        
        double minDist = 9999.0;
        
        for (int i = 0; i < 4; i++) {
            double c[3];
            if (m_trajManager->m_dbsLead->getContactCenterWorld(i, c)) {
                double d = implicitDist->EvaluateFunction(c);
                distances[i] = d;
                
                // 找距离表面最近或者最深入核团内部的作为推荐
                if (d < minDist) {
                    minDist = d;
                    bestContact = i;
                }
            }
        }
    }

    m_controlPanel->updateAnalysisUI(coverages, distances, bestContact);
}

void dicomviewer_3d::refreshCoordinateDisplay()
{
    if (!m_controlPanel) return;

    bool acpcValid = m_trajManager->m_acpcValid;

    double zeroWorld[3] = { 0.0, 0.0, 0.0 };
    double zeroAcPc[3]  = { 0.0, 0.0, 0.0 };

    double targetAcPc[3] = { 0.0, 0.0, 0.0 };
    double entryAcPc[3]  = { 0.0, 0.0, 0.0 };

    if (acpcValid && m_trajManager->hasTarget)
        m_trajManager->worldToAcPc(m_trajManager->targetPos, targetAcPc);
    if (acpcValid && m_trajManager->hasEntry)
        m_trajManager->worldToAcPc(m_trajManager->entryPos, entryAcPc);

    m_controlPanel->updateCoordinateDisplay(
        m_trajManager->hasTarget,
        m_trajManager->hasTarget ? m_trajManager->targetPos : zeroWorld,
        m_trajManager->hasTarget ? targetAcPc : zeroAcPc,
        m_trajManager->hasEntry,
        m_trajManager->hasEntry  ? m_trajManager->entryPos  : zeroWorld,
        m_trajManager->hasEntry  ? entryAcPc  : zeroAcPc,
        acpcValid);

    // --- AC/PC/MSP point labels ---
    const bool hasArr[3]  = { m_trajManager->hasAC,  m_trajManager->hasPC,  m_trajManager->hasMSP  };
    double* worldArr[3]   = { m_trajManager->acPos,   m_trajManager->pcPos,   m_trajManager->mspPos  };
    for (int idx = 0; idx < 3; idx++) {
        m_controlPanel->updateAcPcPointWorld(idx, hasArr[idx], worldArr[idx], false);
        if (hasArr[idx] && acpcValid) {
            double acpc[3] = { 0.0, 0.0, 0.0 };
            m_trajManager->worldToAcPc(worldArr[idx], acpc);
            m_controlPanel->updateAcPcPointAcpc(idx, true, acpc);
        } else {
            m_controlPanel->updateAcPcPointAcpc(idx, false, nullptr);
        }
    }
}

// ============================================================
// commitAcPcPoint: unified submit for AC/PC/MSP (mouse + keyboard)
// ============================================================
void dicomviewer_3d::commitAcPcPoint(AcPcPickMode which, double pos[3])
{
    if (which == ACPC_NONE || !vtkImage) return;

    double color[3] = { 1.0, 1.0, 0.0 };
    vtkSmartPointer<vtkActor>* sphereSlot = nullptr;
    vtkSmartPointer<vtkActor>* crossArr   = nullptr;
    int* sliceIdxArr = nullptr; // pointer to m_views[i].acSliceIndex etc.

    if (which == ACPC_PICK_AC) {
        std::copy(pos, pos + 3, m_trajManager->acPos);
        m_trajManager->hasAC = true;
        color[0] = 0.0; color[1] = 0.8; color[2] = 1.0;
        if (m_trajManager->actorACSphere) ren3d->RemoveActor(m_trajManager->actorACSphere);
        for (int i = 0; i < 3; i++) {
            if (m_trajManager->acCrossArr[i]) m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->acCrossArr[i]);
            m_trajManager->acCrossArr[i] = nullptr;
        }
        sphereSlot = &m_trajManager->actorACSphere;
        crossArr   = m_trajManager->acCrossArr;
    }
    else if (which == ACPC_PICK_PC) {
        std::copy(pos, pos + 3, m_trajManager->pcPos);
        m_trajManager->hasPC = true;
        color[0] = 1.0; color[1] = 0.5; color[2] = 0.0;
        if (m_trajManager->actorPCSphere) ren3d->RemoveActor(m_trajManager->actorPCSphere);
        for (int i = 0; i < 3; i++) {
            if (m_trajManager->pcCrossArr[i]) m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->pcCrossArr[i]);
            m_trajManager->pcCrossArr[i] = nullptr;
        }
        sphereSlot = &m_trajManager->actorPCSphere;
        crossArr   = m_trajManager->pcCrossArr;
    }
    else {
        std::copy(pos, pos + 3, m_trajManager->mspPos);
        m_trajManager->hasMSP = true;
        color[0] = 1.0; color[1] = 1.0; color[2] = 0.0;
        if (m_trajManager->actorMSPSphere) ren3d->RemoveActor(m_trajManager->actorMSPSphere);
        for (int i = 0; i < 3; i++) {
            if (m_trajManager->mspCrossArr[i]) m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->mspCrossArr[i]);
            m_trajManager->mspCrossArr[i] = nullptr;
        }
        sphereSlot = &m_trajManager->actorMSPSphere;
        crossArr   = m_trajManager->mspCrossArr;
    }

    // 3D sphere (radius matched to target/entry = 1.5)
    {
        vtkNew<vtkSphereSource> sphere;
        sphere->SetCenter(pos);
        sphere->SetRadius(1.5);
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(sphere->GetOutputPort());
        vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(color);
        actor->GetProperty()->SetOpacity(0.9);
        *sphereSlot = actor;
        ren3d->AddActor(actor);
    }

    // 2D cross actors + slice indices
    double origin[3], spacing[3];
    vtkImage->GetOrigin(origin);
    vtkImage->GetSpacing(spacing);

    for (int i = 0; i < 3; i++) {
        vtkSmartPointer<vtkActor> cross = m_trajManager->createCrossActor(pos, color);
        int orientation = m_views[i].viewer->GetSliceOrientation();
        int sliceIdx = 0;
        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY)
            sliceIdx = static_cast<int>(std::round((pos[2] - origin[2]) / spacing[2]));
        else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ)
            sliceIdx = static_cast<int>(std::round((pos[1] - origin[1]) / spacing[1]));
        else
            sliceIdx = static_cast<int>(std::round((pos[0] - origin[0]) / spacing[0]));

        crossArr[i] = cross;
        m_views[i].viewer->GetRenderer()->AddActor(cross);

        if (which == ACPC_PICK_AC)  m_views[i].acSliceIndex  = sliceIdx;
        else if (which == ACPC_PICK_PC)  m_views[i].pcSliceIndex  = sliceIdx;
        else                             m_views[i].mspSliceIndex = sliceIdx;
    }

    // clear preview and reset mode
    clearPreviewActors();
    m_acpcPickMode = ACPC_NONE;
    m_hasPreview   = false;
    m_controlPanel->setAcPcActiveMode(0);

    // compute AC-PC matrix if all three points ready
    bool acpcValid = false;
    if (m_trajManager->hasAC && m_trajManager->hasPC && m_trajManager->hasMSP)
        acpcValid = m_trajManager->computeAcPcMatrices();

    m_controlPanel->updateAcPcStatusUI(
        m_trajManager->hasAC, m_trajManager->hasPC, m_trajManager->hasMSP, acpcValid);
    refreshCoordinateDisplay();

    // AC-PC 坐标系建立后，自动初始化全屏十字线
    if (acpcValid) {
        initCrosshairs();  // 内部会 jumpToPosition + refreshAllViews
    } else {
        jumpToPosition(pos);
        refreshAllViews();
    }
}

// ============================================================
// clearPreviewActors: remove temporary hover cross from all 2D views
// ============================================================
void dicomviewer_3d::clearPreviewActors()
{
    for (int i = 0; i < 3; i++) {
        if (m_previewCross2D[i] && m_views[i].viewer) {
            m_views[i].viewer->GetRenderer()->RemoveActor(m_previewCross2D[i]);
            m_previewCross2D[i] = nullptr;
        }
    }
    m_hasPreview = false;
}

// ============================================================
// handleHover: called from MyViewStyle::OnMouseMove when in ACPC pick mode
// Uses DisplayToWorld coordinate unprojection (no VTK picker, no per-frame raycast)
// ============================================================
void dicomviewer_3d::handleHover(int viewIndex, int screenX, int screenY)
{
    if (!vtkImage || m_acpcPickMode == ACPC_NONE) return;

    vtkRenderer* ren = m_views[viewIndex].viewer->GetRenderer();
    if (!ren) return;

    // Unproject screen coords to world via orthographic camera
    ren->SetDisplayPoint(screenX, screenY, 0.5);
    ren->DisplayToWorld();
    double* wp = ren->GetWorldPoint();
    if (wp[3] == 0.0) return;

    double worldPos[3] = { wp[0] / wp[3], wp[1] / wp[3], wp[2] / wp[3] };

    // Fix the out-of-plane coordinate to the current slice position
    double origin[3], spacing[3];
    vtkImage->GetOrigin(origin);
    vtkImage->GetSpacing(spacing);
    int currentSlice = m_views[viewIndex].viewer->GetSlice();
    int orientation  = m_views[viewIndex].viewer->GetSliceOrientation();
    if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY)
        worldPos[2] = origin[2] + currentSlice * spacing[2];
    else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ)
        worldPos[1] = origin[1] + currentSlice * spacing[1];
    else
        worldPos[0] = origin[0] + currentSlice * spacing[0];

    // Bounds check
    double bounds[6];
    vtkImage->GetBounds(bounds);
    if (worldPos[0] < bounds[0] || worldPos[0] > bounds[1] ||
        worldPos[1] < bounds[2] || worldPos[1] > bounds[3] ||
        worldPos[2] < bounds[4] || worldPos[2] > bounds[5]) {
        clearPreviewActors();
        return;
    }

    std::copy(worldPos, worldPos + 3, m_previewPos);
    m_hasPreview = true;

    // Update the label in ControlPanel (preview = italic gray)
    int pointIdx = (m_acpcPickMode == ACPC_PICK_AC) ? 0 : (m_acpcPickMode == ACPC_PICK_PC) ? 1 : 2;
    m_controlPanel->updateAcPcPointWorld(pointIdx, true, worldPos, true);

    // Update or create the preview cross for this view only
    double previewColor[3] = { 1.0, 0.85, 0.0 }; // distinct yellow-orange
    if (!m_previewCross2D[viewIndex]) {
        m_previewCross2D[viewIndex] = m_trajManager->createCrossActor(worldPos, previewColor);
        m_views[viewIndex].viewer->GetRenderer()->AddActor(m_previewCross2D[viewIndex]);
    } else {
        // Move existing cross: recreate at new position (simplest approach)
        m_views[viewIndex].viewer->GetRenderer()->RemoveActor(m_previewCross2D[viewIndex]);
        m_previewCross2D[viewIndex] = m_trajManager->createCrossActor(worldPos, previewColor);
        m_views[viewIndex].viewer->GetRenderer()->AddActor(m_previewCross2D[viewIndex]);
    }

    m_views[viewIndex].viewer->GetRenderer()->ResetCameraClippingRange();
    m_views[viewIndex].viewer->Render();
}

// ============================================================
// initCrosshairs: 在 AC-PC 建立后创建全屏十字线
// ============================================================
void dicomviewer_3d::initCrosshairs()
{
    if (!vtkImage) return;

    // MCP = (AC + PC) / 2  —— AC-PC 坐标原点的世界坐标
    for (int k = 0; k < 3; k++)
        m_crosshairWorld[k] = (m_trajManager->acPos[k] + m_trajManager->pcPos[k]) * 0.5;

    double color[3] = { 1.0, 0.9, 0.3 }; // 淡黄色

    for (int i = 0; i < 3; i++) {
        vtkRenderer* ren = m_views[i].viewer->GetRenderer();
        if (!ren) continue;

        // 移除旧的
        if (m_views[i].crosshairH) ren->RemoveActor(m_views[i].crosshairH);
        if (m_views[i].crosshairV) ren->RemoveActor(m_views[i].crosshairV);

        int orientation = m_views[i].viewer->GetSliceOrientation();

        // out-of-plane 坐标贴合当前切片 + 微偏移，防止被切片遮盖
        double sliceZ = getSliceWorldCoord(i);
        double pos[3];
        std::copy(m_crosshairWorld, m_crosshairWorld + 3, pos);
        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY)
            pos[2] = sliceZ + 0.01;
        else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ)
            pos[1] = sliceZ + 0.01;
        else
            pos[0] = sliceZ + 0.01;

        double p1H[3], p2H[3], p1V[3], p2V[3];
        std::copy(pos, pos + 3, p1H);
        std::copy(pos, pos + 3, p2H);
        std::copy(pos, pos + 3, p1V);
        std::copy(pos, pos + 3, p2V);

        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) {
            p1H[0] = -5000.0; p2H[0] = 5000.0;
            p1V[1] = -5000.0; p2V[1] = 5000.0;
        } else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) {
            p1H[0] = -5000.0; p2H[0] = 5000.0;
            p1V[2] = -5000.0; p2V[2] = 5000.0;
        } else {
            p1H[1] = -5000.0; p2H[1] = 5000.0;
            p1V[2] = -5000.0; p2V[2] = 5000.0;
        }

        // 水平线
        vtkSmartPointer<vtkLineSource> hSrc = vtkSmartPointer<vtkLineSource>::New();
        hSrc->SetPoint1(p1H);
        hSrc->SetPoint2(p2H);
        vtkSmartPointer<vtkPolyDataMapper> hMap = vtkSmartPointer<vtkPolyDataMapper>::New();
        hMap->SetInputConnection(hSrc->GetOutputPort());
        vtkSmartPointer<vtkActor> hAct = vtkSmartPointer<vtkActor>::New();
        hAct->SetMapper(hMap);
        hAct->GetProperty()->SetColor(color);
        hAct->GetProperty()->SetOpacity(0.7);
        hAct->GetProperty()->SetLineWidth(1.2);
        ren->AddActor(hAct);

        // 竖直线
        vtkSmartPointer<vtkLineSource> vSrc = vtkSmartPointer<vtkLineSource>::New();
        vSrc->SetPoint1(p1V);
        vSrc->SetPoint2(p2V);
        vtkSmartPointer<vtkPolyDataMapper> vMap = vtkSmartPointer<vtkPolyDataMapper>::New();
        vMap->SetInputConnection(vSrc->GetOutputPort());
        vtkSmartPointer<vtkActor> vAct = vtkSmartPointer<vtkActor>::New();
        vAct->SetMapper(vMap);
        vAct->GetProperty()->SetColor(color);
        vAct->GetProperty()->SetOpacity(0.7);
        vAct->GetProperty()->SetLineWidth(1.2);
        ren->AddActor(vAct);

        m_views[i].crosshairHSrc = hSrc;
        m_views[i].crosshairVSrc = vSrc;
        m_views[i].crosshairH = hAct;
        m_views[i].crosshairV = vAct;
    }

    m_crosshairVisible = true;
    refreshCrosshairCoordDisplay();
    jumpToPosition(m_crosshairWorld);
    refreshAllViews();
}
// updateCrosshairPosition: 拖动十字线到新的世界坐标
// srcView: 触发拖动的视图索引
// ============================================================
void dicomviewer_3d::updateCrosshairPosition(int srcView, double worldPos[3])
{
    if (!vtkImage || !m_crosshairVisible) return;

    // srcView 的 out-of-plane 锁定到当前切片（用户只想在本切面内移动十字）
    {
        int ori = m_views[srcView].viewer->GetSliceOrientation();
        double sz = getSliceWorldCoord(srcView);
        if (ori == vtkImageViewer2::SLICE_ORIENTATION_XY)
            worldPos[2] = sz;
        else if (ori == vtkImageViewer2::SLICE_ORIENTATION_XZ)
            worldPos[1] = sz;
        else
            worldPos[0] = sz;
    }
    std::copy(worldPos, worldPos + 3, m_crosshairWorld);

    for (int i = 0; i < 3; i++) {
        if (!m_views[i].crosshairHSrc || !m_views[i].crosshairVSrc) continue;

        int orientation = m_views[i].viewer->GetSliceOrientation();

        // 每个视图的 out-of-plane 坐标贴合自身切片 + 微偏移
        double myZ = getSliceWorldCoord(i);
        double pos[3];
        std::copy(m_crosshairWorld, m_crosshairWorld + 3, pos);
        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY)
            pos[2] = myZ + 0.01;
        else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ)
            pos[1] = myZ + 0.01;
        else
            pos[0] = myZ + 0.01;

        double p1H[3], p2H[3], p1V[3], p2V[3];
        std::copy(pos, pos + 3, p1H);
        std::copy(pos, pos + 3, p2H);
        std::copy(pos, pos + 3, p1V);
        std::copy(pos, pos + 3, p2V);

        if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY) {
            p1H[0] = -5000.0; p2H[0] = 5000.0;
            p1V[1] = -5000.0; p2V[1] = 5000.0;
        } else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ) {
            p1H[0] = -5000.0; p2H[0] = 5000.0;
            p1V[2] = -5000.0; p2V[2] = 5000.0;
        } else {
            p1H[1] = -5000.0; p2H[1] = 5000.0;
            p1V[2] = -5000.0; p2V[2] = 5000.0;
        }

        m_views[i].crosshairHSrc->SetPoint1(p1H);
        m_views[i].crosshairHSrc->SetPoint2(p2H);
        m_views[i].crosshairHSrc->Modified();
        m_views[i].crosshairVSrc->SetPoint1(p1V);
        m_views[i].crosshairVSrc->SetPoint2(p2V);
        m_views[i].crosshairVSrc->Modified();
    }

    // 联动模式下，只跳转非源视图到对应切片（srcView 切片不变）
    if (m_crosshairLinked) {
        double origin[3], spacing[3];
        vtkImage->GetOrigin(origin);
        vtkImage->GetSpacing(spacing);
        for (int i = 0; i < 3; i++) {
            if (i == srcView) continue;
            int ori = m_views[i].viewer->GetSliceOrientation();
            int sl = 0;
            if (ori == vtkImageViewer2::SLICE_ORIENTATION_XY)
                sl = std::round((m_crosshairWorld[2] - origin[2]) / spacing[2]);
            else if (ori == vtkImageViewer2::SLICE_ORIENTATION_XZ)
                sl = std::round((m_crosshairWorld[1] - origin[1]) / spacing[1]);
            else
                sl = std::round((m_crosshairWorld[0] - origin[0]) / spacing[0]);
            m_views[i].viewer->SetSlice(sl);
            updateSliderUI(i, sl);
            updateOverlaySlice(i);
        }
    }

    refreshCrosshairCoordDisplay();
    refreshAllViews();
}

// ============================================================
// setCrosshairVisible: 显示/隐藏十字线
// ============================================================
void dicomviewer_3d::setCrosshairVisible(bool v)
{
    m_crosshairVisible = v;
    for (int i = 0; i < 3; i++) {
        if (m_views[i].crosshairH) m_views[i].crosshairH->SetVisibility(v ? 1 : 0);
        if (m_views[i].crosshairV) m_views[i].crosshairV->SetVisibility(v ? 1 : 0);
    }
    refreshAllViews();
}

// ============================================================
// refreshCrosshairCoordDisplay: 更新十字线位置坐标到 UI
// ============================================================
void dicomviewer_3d::refreshCrosshairCoordDisplay()
{
    if (!m_controlPanel || !m_trajManager) return;

    double acpc[3] = { 0.0, 0.0, 0.0 };
    bool acpcValid = m_trajManager->m_acpcValid;
    if (acpcValid) {
        m_trajManager->worldToAcPc(m_crosshairWorld, acpc);
    }
    m_controlPanel->updateCrosshairDisplay(
        m_crosshairVisible, m_crosshairWorld, acpcValid ? acpc : nullptr);
}

// ============================================================
// getSliceWorldCoord: 返回视图当前切片的 out-of-plane 世界坐标
// ============================================================
double dicomviewer_3d::getSliceWorldCoord(int vi)
{
    if (vi < 0 || vi >= 3 || !vtkImage || !m_views[vi].viewer) return 0.0;
    double origin[3], spacing[3];
    vtkImage->GetOrigin(origin);
    vtkImage->GetSpacing(spacing);
    int orientation = m_views[vi].viewer->GetSliceOrientation();
    int slice = m_views[vi].viewer->GetSlice();
    if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY)
        return origin[2] + slice * spacing[2];
    else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ)
        return origin[1] + slice * spacing[1];
    else
        return origin[0] + slice * spacing[0];
}

// ============================================================
// syncCrosshairToSlice: 切片变化后刷新十字线 out-of-plane 坐标
//   联动模式：更新 m_crosshairWorld 并跳转其他两个视图
//   非联动：只刷新该视图十字线的 Z 以贴合新切片
// ============================================================
void dicomviewer_3d::syncCrosshairToSlice(int vi)
{
    if (!m_crosshairVisible || !vtkImage) return;
    if (vi < 0 || vi >= 3) return;

    int orientation = m_views[vi].viewer->GetSliceOrientation();
    double sliceZ = getSliceWorldCoord(vi);

    // 更新全局十字线的 out-of-plane 坐标分量
    if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XY)
        m_crosshairWorld[2] = sliceZ;
    else if (orientation == vtkImageViewer2::SLICE_ORIENTATION_XZ)
        m_crosshairWorld[1] = sliceZ;
    else
        m_crosshairWorld[0] = sliceZ;

    // 刷新所有视图的十字线几何（贴合各自当前切片）
    for (int i = 0; i < 3; i++) {
        if (!m_views[i].crosshairHSrc || !m_views[i].crosshairVSrc) continue;
        int ori = m_views[i].viewer->GetSliceOrientation();
        double myZ = getSliceWorldCoord(i);
        double pos[3];
        std::copy(m_crosshairWorld, m_crosshairWorld + 3, pos);
        if (ori == vtkImageViewer2::SLICE_ORIENTATION_XY)
            pos[2] = myZ + 0.01;
        else if (ori == vtkImageViewer2::SLICE_ORIENTATION_XZ)
            pos[1] = myZ + 0.01;
        else
            pos[0] = myZ + 0.01;

        double p1H[3], p2H[3], p1V[3], p2V[3];
        std::copy(pos, pos + 3, p1H);
        std::copy(pos, pos + 3, p2H);
        std::copy(pos, pos + 3, p1V);
        std::copy(pos, pos + 3, p2V);
        if (ori == vtkImageViewer2::SLICE_ORIENTATION_XY) {
            p1H[0] = -5000.0; p2H[0] = 5000.0;
            p1V[1] = -5000.0; p2V[1] = 5000.0;
        } else if (ori == vtkImageViewer2::SLICE_ORIENTATION_XZ) {
            p1H[0] = -5000.0; p2H[0] = 5000.0;
            p1V[2] = -5000.0; p2V[2] = 5000.0;
        } else {
            p1H[1] = -5000.0; p2H[1] = 5000.0;
            p1V[2] = -5000.0; p2V[2] = 5000.0;
        }
        m_views[i].crosshairHSrc->SetPoint1(p1H);
        m_views[i].crosshairHSrc->SetPoint2(p2H);
        m_views[i].crosshairHSrc->Modified();
        m_views[i].crosshairVSrc->SetPoint1(p1V);
        m_views[i].crosshairVSrc->SetPoint2(p2V);
        m_views[i].crosshairVSrc->Modified();
    }

    // 联动模式：其他两个视图跳到 m_crosshairWorld 对应的切片
    if (m_crosshairLinked) {
        double origin[3], spacing[3];
        vtkImage->GetOrigin(origin);
        vtkImage->GetSpacing(spacing);
        for (int i = 0; i < 3; i++) {
            if (i == vi) continue;
            int ori = m_views[i].viewer->GetSliceOrientation();
            int sl = 0;
            if (ori == vtkImageViewer2::SLICE_ORIENTATION_XY)
                sl = std::round((m_crosshairWorld[2] - origin[2]) / spacing[2]);
            else if (ori == vtkImageViewer2::SLICE_ORIENTATION_XZ)
                sl = std::round((m_crosshairWorld[1] - origin[1]) / spacing[1]);
            else
                sl = std::round((m_crosshairWorld[0] - origin[0]) / spacing[0]);
            m_views[i].viewer->SetSlice(sl);
            updateSliderUI(i, sl);
            updateOverlaySlice(i);
        }
    }

    refreshCrosshairCoordDisplay();
    refreshAllViews();
}

// ============================================================
// updateRulerLayout: 根据当前相机参数计算标尺量程和位置
//   设计：
//   - halfLenNormInitial 为锚（可被手动拉伸更新，双击复位）
//   - 始终：rangeMM = halfLenNormInitial * 2 * viewMM → snap 到真·整十
//           → halfLenNorm = snappedRange / (2 * viewMM)
//   - 永远不覆盖 centerNorm（保留用户拖拽位置）
// ============================================================
void dicomviewer_3d::updateRulerLayout(int vi)
{
    if (vi < 0 || vi >= 3) return;
    vtkResliceImageViewer* viewer = m_views[vi].viewer;
    if (!viewer || !viewer->GetRenderer() || !vtkImage) return;

    vtkRenderer* ren = viewer->GetRenderer();
    vtkCamera* cam = ren->GetActiveCamera();
    if (!cam) return;

    // --- 基本参数 ---
    double parallelScale = cam->GetParallelScale(); // 半高 (world units = mm)
    int* vpSize = ren->GetSize(); // [width, height] in pixels
    if (!vpSize || vpSize[0] <= 0 || vpSize[1] <= 0) return;
    double vpW = vpSize[0];
    double vpH = vpSize[1];

    // 视口的全高/全宽对应的物理距离 (mm)
    double viewHeightMM = 2.0 * parallelScale;
    double viewWidthMM  = viewHeightMM * (vpW / vpH);
    if (viewHeightMM <= 0 || viewWidthMM <= 0) return;

    // 真·整十 snap：≤300 步进 10，≤500 步进 25，再大步进 50
    auto snapTen = [](double mm) -> double {
        if (mm < 1.0) return 1.0;
        double step = 10.0;
        if (mm > 300.0) step = 25.0;
        if (mm > 500.0) step = 50.0;
        double snapped = std::round(mm / step) * step;
        if (snapped < step) snapped = step;
        return snapped;
    };

    auto layoutOne = [&](SliceViewContext::RulerState& rs, double viewMM, bool isVertical) {
        // 初次进入：用一个合理的初始相对比例（≈ 0.15, 约占视口 30%）
        if (rs.halfLenNormInitial <= 0.0) {
            rs.halfLenNormInitial = 0.15;
        }

        // 始终以 halfLenNormInitial（锚定比例）为目标，按当前缩放算量程，snap 到真整十后微调绝对长度
        double idealMM = rs.halfLenNormInitial * 2.0 * viewMM;
        double snapped = snapTen(idealMM);
        rs.rangeMM     = snapped;
        rs.halfLenNorm = snapped / (2.0 * viewMM);

        // axis 端点
        if (isVertical) {
            double x  = 0.92; // 右侧
            double y1 = rs.centerNorm - rs.halfLenNorm;
            double y2 = rs.centerNorm + rs.halfLenNorm;
            rs.axis->GetPoint1Coordinate()->SetValue(x, y1);
            rs.axis->GetPoint2Coordinate()->SetValue(x, y2);
        } else {
            double y  = 0.06; // 底部
            double x1 = rs.centerNorm - rs.halfLenNorm;
            double x2 = rs.centerNorm + rs.halfLenNorm;
            rs.axis->GetPoint1Coordinate()->SetValue(x1, y);
            rs.axis->GetPoint2Coordinate()->SetValue(x2, y);
        }
        rs.axis->SetRange(0.0, rs.rangeMM);

        // 刻度密度：每 10mm 一个主刻度，但限制总数
        int n = std::max(2, (int)std::round(rs.rangeMM / 10.0) + 1);
        if (n > 11) n = 11;
        rs.axis->SetNumberOfLabels(n);
        rs.axis->Modified();
    };

    layoutOne(m_views[vi].rulerV, viewHeightMM, true);
    layoutOne(m_views[vi].rulerH, viewWidthMM,  false);
}

// ============================================================
// hitTestRuler: 检测屏幕坐标是否命中标尺（返回命中区域）
// ============================================================
dicomviewer_3d::RulerHitZone dicomviewer_3d::hitTestRuler(
    int vi, bool vertical, int screenX, int screenY)
{
    if (vi < 0 || vi >= 3) return RULER_NONE;
    vtkRenderer* ren = m_views[vi].viewer ? m_views[vi].viewer->GetRenderer() : nullptr;
    if (!ren) return RULER_NONE;

    int* vpSize = ren->GetSize();
    if (!vpSize || vpSize[0] <= 0 || vpSize[1] <= 0) return RULER_NONE;

    // 转换为 NormalizedViewport 坐标
    double nx = (double)screenX / vpSize[0];
    double ny = (double)screenY / vpSize[1];

    const double hitTol = 0.03;  // 3% viewport tolerance
    const double endTol = 0.04;  // 端点更大的命中区

    SliceViewContext::RulerState& rs = vertical ? m_views[vi].rulerV : m_views[vi].rulerH;

    if (vertical) {
        // 垂直标尺: x 固定在 0.92, y 从 center-half 到 center+half
        double rulerX = 0.92;
        double y1 = rs.centerNorm - rs.halfLenNorm;
        double y2 = rs.centerNorm + rs.halfLenNorm;
        if (std::abs(nx - rulerX) > hitTol) return RULER_NONE;
        if (ny < y1 - endTol || ny > y2 + endTol) return RULER_NONE;
        // 端点 A (bottom)
        if (ny < y1 + endTol) return RULER_END_A;
        // 端点 B (top)
        if (ny > y2 - endTol) return RULER_END_B;
        return RULER_BODY;
    } else {
        // 水平标尺: y 固定在 0.06, x 从 center-half 到 center+half
        double rulerY = 0.06;
        double x1 = rs.centerNorm - rs.halfLenNorm;
        double x2 = rs.centerNorm + rs.halfLenNorm;
        if (std::abs(ny - rulerY) > hitTol) return RULER_NONE;
        if (nx < x1 - endTol || nx > x2 + endTol) return RULER_NONE;
        if (nx < x1 + endTol) return RULER_END_A;
        if (nx > x2 - endTol) return RULER_END_B;
        return RULER_BODY;
    }
}

// ============================================================
// cleanupSliceViewAddons: 重新加载图像前移除一个 2D 视图上挂的所有附加 actor
// 防止重影。仅清空 vtkSmartPointer 本身的引用以及从 renderer 中移除。
// ============================================================
void dicomviewer_3d::cleanupSliceViewAddons(int viewIndex)
{
    if (viewIndex < 0 || viewIndex >= 3) return;
    SliceViewContext& v = m_views[viewIndex];
    if (!v.viewer) return;
    vtkRenderer* ren = v.viewer->GetRenderer();
    if (!ren) return;

    // ruler axes
    if (v.rulerV.axis) { ren->RemoveActor2D(v.rulerV.axis); v.rulerV.axis = nullptr; }
    if (v.rulerH.axis) { ren->RemoveActor2D(v.rulerH.axis); v.rulerH.axis = nullptr; }
    v.rulerV = SliceViewContext::RulerState{};
    v.rulerH = SliceViewContext::RulerState{};

    // orientation labels
    if (v.orientTop)    { ren->RemoveActor2D(v.orientTop);    v.orientTop    = nullptr; }
    if (v.orientBottom) { ren->RemoveActor2D(v.orientBottom); v.orientBottom = nullptr; }
    if (v.orientLeft)   { ren->RemoveActor2D(v.orientLeft);   v.orientLeft   = nullptr; }
    if (v.orientRight)  { ren->RemoveActor2D(v.orientRight);  v.orientRight  = nullptr; }

    // center triangles
    if (v.centerTriV) { ren->RemoveActor2D(v.centerTriV); v.centerTriV = nullptr; }
    if (v.centerTriH) { ren->RemoveActor2D(v.centerTriH); v.centerTriH = nullptr; }

    // crosshair
    if (v.crosshairH) { ren->RemoveActor(v.crosshairH); v.crosshairH = nullptr; }
    if (v.crosshairV) { ren->RemoveActor(v.crosshairV); v.crosshairV = nullptr; }
    v.crosshairHSrc = nullptr;
    v.crosshairVSrc = nullptr;

    // preview cross
    if (m_previewCross2D[viewIndex]) {
        ren->RemoveActor(m_previewCross2D[viewIndex]);
        m_previewCross2D[viewIndex] = nullptr;
    }
}

// ============================================================
// resetAllAcPcAndCrosshair: 重新加载图像时复位所有 AC-PC/MSP/十字线状态
// 等价于触发一次 ControlPanel::sigResetAcPc 的处理逻辑
// ============================================================
void dicomviewer_3d::resetAllAcPcAndCrosshair()
{
    clearPreviewActors();
    m_acpcPickMode = ACPC_NONE;
    if (m_controlPanel) m_controlPanel->setAcPcActiveMode(0);

    if (m_trajManager) {
        if (m_trajManager->actorACSphere)  ren3d->RemoveActor(m_trajManager->actorACSphere);
        if (m_trajManager->actorPCSphere)  ren3d->RemoveActor(m_trajManager->actorPCSphere);
        if (m_trajManager->actorMSPSphere) ren3d->RemoveActor(m_trajManager->actorMSPSphere);
        m_trajManager->actorACSphere  = nullptr;
        m_trajManager->actorPCSphere  = nullptr;
        m_trajManager->actorMSPSphere = nullptr;
        for (int i = 0; i < 3; i++) {
            if (m_trajManager->acCrossArr[i]  && m_views[i].viewer) m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->acCrossArr[i]);
            if (m_trajManager->pcCrossArr[i]  && m_views[i].viewer) m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->pcCrossArr[i]);
            if (m_trajManager->mspCrossArr[i] && m_views[i].viewer) m_views[i].viewer->GetRenderer()->RemoveActor(m_trajManager->mspCrossArr[i]);
            m_trajManager->acCrossArr[i]  = nullptr;
            m_trajManager->pcCrossArr[i]  = nullptr;
            m_trajManager->mspCrossArr[i] = nullptr;
            m_views[i].acSliceIndex = m_views[i].pcSliceIndex = m_views[i].mspSliceIndex = -1;
        }
        m_trajManager->resetAcPc();
    }

    // 十字线（cleanupSliceViewAddons 已经会移除 actor，这里只重置状态）
    m_crosshairVisible = false;
    m_crosshairLinked  = true;
    m_crosshairWorld[0] = m_crosshairWorld[1] = m_crosshairWorld[2] = 0.0;

    if (m_controlPanel) {
        m_controlPanel->updateCrosshairDisplay(false, m_crosshairWorld, nullptr);
        m_controlPanel->updateAcPcStatusUI(false, false, false, false);
        m_controlPanel->updateAcPcPointWorld(0, false, nullptr, false);
        m_controlPanel->updateAcPcPointWorld(1, false, nullptr, false);
        m_controlPanel->updateAcPcPointWorld(2, false, nullptr, false);
        m_controlPanel->updateAcPcPointAcpc(0, false, nullptr);
        m_controlPanel->updateAcPcPointAcpc(1, false, nullptr);
        m_controlPanel->updateAcPcPointAcpc(2, false, nullptr);
    }
    refreshCoordinateDisplay();
}

// ============================================================
// slot_resetWindowLevel: 把 3 个 2D 视图窗宽窗位恢复到智能初始值
// ============================================================
void dicomviewer_3d::slot_resetWindowLevel()
{
    if (!vtkImage) return;
    qDebug() << "slot_resetWindowLevel: W=" << m_smartWindow << " L=" << m_smartLevel;
    for (int i = 0; i < 3; i++) {
        if (!m_views[i].viewer) continue;
        m_views[i].viewer->SetColorWindow(m_smartWindow);
        m_views[i].viewer->SetColorLevel(m_smartLevel);
        m_views[i].viewer->GetRenderWindow()->Render();
    }
}

void dicomviewer_3d::slot_applyPhaseFPreset(int presetIndex)
{
    if (!m_trajManager || !m_trajManager->m_dbsLead) {
        return;
    }

    if (presetIndex <= 0) {
        if (m_leadSimulator) {
            m_leadSimulator->setPhaseFPresetIndex(0);
        }
        m_phaseFPresetId = "Manual";
        qDebug() << "[Phase F] Manual mode selected; keeping current FEM parameters";
        return;
    }

    int polarities[4] = {0, 0, -1, 0};
    double target[3] = {167.114, 235.788, 124.18};
    double entry[3] = {167.114, 235.788, 204.18};
    double amplitude = 3.0;
    int pulseWidth = 60;
    int frequency = 130;
    bool showVTA = false;
    m_phaseFUseEncapsulation = true;
    m_phaseFSigmaEncapsulation = 0.115;
    m_phaseFEncapsulationThickness = 0.2;

    if (presetIndex == 2) {
        m_phaseFPresetId = "F2";
        polarities[0] = -1;
        polarities[2] = 0;
    } else if (presetIndex == 3) {
        m_phaseFPresetId = "F3";
        m_phaseFUseEncapsulation = false;
        m_phaseFEncapsulationThickness = 0.0;
    } else if (presetIndex == 4) {
        m_phaseFPresetId = "F4";
        m_phaseFSigmaEncapsulation = 0.070;
    } else if (presetIndex == 5) {
        m_phaseFPresetId = "VideoDemo";
        polarities[1] = -1;
        polarities[2] = 0;
        target[0] = 174.47;
        target[1] = 238.15;
        target[2] = 129.00;
        entry[0] = 167.00;
        entry[1] = 191.00;
        entry[2] = 82.00;
        amplitude = 4.0;
        pulseWidth = 80;
        showVTA = false;
    } else if (presetIndex == 1) {
        m_phaseFPresetId = "F1";
    }

    for (int i = 0; i < 3; ++i) {
        m_trajManager->targetPos[i] = target[i];
        m_trajManager->entryPos[i] = entry[i];
    }
    m_trajManager->hasTarget = true;
    m_trajManager->hasEntry = true;

    auto* lead = m_trajManager->m_dbsLead;
    lead->UpdateTrajectory(entry, target);
    lead->SetVisibility(true);
    lead->setLeadType(0);
    lead->setDepthOffset(0.0);
    lead->setAmplitude(amplitude);
    lead->setPulseWidth(pulseWidth);
    lead->setFrequency(frequency);
    for (int i = 0; i < 4; ++i) {
        ContactPolarity p = POLARITY_OFF;
        if (polarities[i] == -1) p = POLARITY_CATHODE;
        else if (polarities[i] == 1) p = POLARITY_ANODE;
        lead->setContactPolarity(i, p);
    }
    lead->setShowVTA(showVTA);

    if (m_leadSimulator) {
        m_leadSimulator->setParameters(0, 0.0, amplitude, pulseWidth, frequency, showVTA, polarities);
        m_leadSimulator->setPhaseFPresetIndex(presetIndex);
    }

    refreshCoordinateDisplay();
    refreshAllViews();
    calculateVTAIntersection();

    qDebug() << "[Phase F] Applied preset" << m_phaseFPresetId
             << "target=" << target[0] << target[1] << target[2]
             << "entry=" << entry[0] << entry[1] << entry[2]
             << "activeContact="
             << ((polarities[0] == -1) ? 0 : (polarities[1] == -1) ? 1 : (polarities[2] == -1) ? 2 : (polarities[3] == -1) ? 3 : -1)
             << "amplitude=" << amplitude << "V pulseWidth=" << pulseWidth << "us frequency=" << frequency << "Hz"
             << "useEncapsulation=" << m_phaseFUseEncapsulation
             << "encapThickness_mm=" << m_phaseFEncapsulationThickness
             << "sigmaBrain=0.115S/m sigmaEncapsulation=" << m_phaseFSigmaEncapsulation << "S/m";
}

// ============================================================
// 【FEM】真实 VTA 计算流水线
// ============================================================
void dicomviewer_3d::slot_computeRealVTA()
{
    // 前置检查
    if (!vtkImage) {
        QMessageBox::warning(this, "VTA 计算", "请先加载脑部影像");
        return;
    }
    if (!m_trajManager || !m_trajManager->hasTarget || !m_trajManager->hasEntry) {
        QMessageBox::warning(this, "VTA 计算", "请先设置靶点和入针点");
        return;
    }
    if (!m_trajManager->m_dbsLead) {
        QMessageBox::warning(this, "VTA 计算", "请先创建电极模型");
        return;
    }

    // 检查是否有阴极
    bool hasCathode = false;
    for (int i = 0; i < m_trajManager->m_dbsLead->getNumContacts(); ++i) {
        if (m_trajManager->m_dbsLead->getContactPolarity(i) == POLARITY_CATHODE)
            hasCathode = true;
    }
    if (!hasCathode) {
        QMessageBox::warning(this, "VTA 计算", "请至少设置一个阴极触点");
        return;
    }

    // 收集参数
    dbs_fem::DBSSimSpec spec;
    std::copy(m_trajManager->entryPos, m_trajManager->entryPos + 3, spec.entry);
    std::copy(m_trajManager->targetPos, m_trajManager->targetPos + 3, spec.target);

    auto* lead = m_trajManager->m_dbsLead;
    spec.leadType        = lead->getLeadType();
    spec.depthOffset     = lead->getDepthOffset();
    spec.numContacts     = lead->getNumContacts();
    spec.leadRadius      = lead->getLeadRadius();
    spec.contactLength   = lead->getContactLength();
    spec.contactSpacing  = lead->getContactSpacing();

    for (int i = 0; i < spec.numContacts && i < 4; ++i) {
        ContactPolarity p = lead->getContactPolarity(i);
        spec.contactPolarity[i] = (p == POLARITY_CATHODE) ? -1 : (p == POLARITY_ANODE) ? 1 : 0;
    }

    // 从主电极模型读取刺激参数
    spec.amplitude        = lead->getAmplitude();
    spec.pulseWidth       = lead->getPulseWidth();
    spec.isVoltageControl = true;
    spec.sigmaBrain = 0.115;
    spec.useEncapsulationLayer = m_phaseFUseEncapsulation;
    spec.sigmaEncapsulation = m_phaseFSigmaEncapsulation;
    spec.encapsulationThickness = m_phaseFEncapsulationThickness;

    if (spec.amplitude < 0.01) {
        QMessageBox::warning(this, "VTA 计算", "请先设置刺激幅度 (amplitude > 0)");
        return;
    }

    QString meshPath = QDir::tempPath() + "/dbs_fem_mesh.mesh";
    QString phaseFPresetId = m_phaseFPresetId;
    QString resultPath = phaseFResultPath(phaseFPresetId);

    qRegisterMetaType<vtkSmartPointer<vtkUnstructuredGrid>>("vtkSmartPointer<vtkUnstructuredGrid>");
    qDebug() << "[Phase F] ================= FEM run begin =================";
    qDebug() << "[Phase F] case=" << phaseFPresetId
             << "resultPath=" << resultPath;
    qDebug() << "[Phase F] target=" << spec.target[0] << spec.target[1] << spec.target[2]
             << "entry=" << spec.entry[0] << spec.entry[1] << spec.entry[2];
    qDebug() << "[Phase F] leadType=" << spec.leadType
             << "amplitude=" << spec.amplitude << "V"
             << "pulseWidth=" << spec.pulseWidth << "us"
             << "contactPolarity=" << spec.contactPolarity[0] << spec.contactPolarity[1]
             << spec.contactPolarity[2] << spec.contactPolarity[3];
    qDebug() << "[Phase F] material sigmaBrain=" << spec.sigmaBrain << "S/m"
             << "useEncapsulation=" << spec.useEncapsulationLayer
             << "encapsulationThickness=" << spec.encapsulationThickness << "mm"
             << "sigmaEncapsulation=" << spec.sigmaEncapsulation << "S/m";
    qDebug() << "[VTA] 启动 FEM 计算流水线...";

    // ---- Step 1: 网格化 (在后台线程) ----
    auto* meshWorker = new DBSMeshWorker();
    meshWorker->setSpec(spec);
    meshWorker->setBrainImage(vtkImage);
    meshWorker->setLabelImage(vtkLabelImage);
    meshWorker->setOutputPath(meshPath);

    auto* meshThread = new QThread(this);
    meshWorker->moveToThread(meshThread);

    connect(meshThread, &QThread::started, meshWorker, &DBSMeshWorker::process);
    connect(meshWorker, &DBSMeshWorker::progressUpdated, this, [this](int pct, const QString& msg) {
        qDebug() << "[VTA Mesh]" << pct << "%" << msg;
    });
    connect(meshWorker, &DBSMeshWorker::errorOccurred, this, [this](const QString& err) {
        QMessageBox::warning(this, "VTA 网格化失败", err);
        qDebug() << "[Phase F] ================= FEM run end: mesh error =================";
    });

    // 网格化完成后启动求解
    connect(meshWorker, &DBSMeshWorker::finished, this, [this, spec, phaseFPresetId, resultPath](const QString& path) {
        qDebug() << "[VTA] 网格化完成:" << path;
        qDebug() << "[VTA-Debug] 即将创建 DBSSimWorker...";

        auto* simWorker = new DBSSimWorker();
        qDebug() << "[VTA-Debug] simWorker 已创建, 设置 spec...";
        simWorker->setSpec(spec);
        simWorker->setMeshPath(path);
        qDebug() << "[VTA-Debug] spec/mesh path 已设置, 创建线程...";

        auto* simThread = new QThread(this);
        simWorker->moveToThread(simThread);
        qDebug() << "[VTA-Debug] simWorker 已 moveToThread, 连接信号...";

        connect(simThread, &QThread::started, simWorker, &DBSSimWorker::process);
        connect(simWorker, &DBSSimWorker::progressUpdated, this, [this](int pct, const QString& msg) {
            qDebug() << "[VTA FEM]" << pct << "%" << msg;
        });
        connect(simWorker, &DBSSimWorker::errorOccurred, this, [this](const QString& err) {
            QMessageBox::warning(this, "VTA 求解失败", err);
            qDebug() << "[Phase F] ================= FEM run end: solver error =================";
        });

        // 求解完成 → 提取 VTA 等值面
        connect(simWorker, &DBSSimWorker::finished,
                this, [this, spec, phaseFPresetId, resultPath](vtkSmartPointer<vtkUnstructuredGrid> result) {
            qDebug() << "[VTA] FEM 求解完成，提取等值面...";

            if (!result || result->GetNumberOfCells() == 0) {
                QMessageBox::warning(this, "VTA", "FEM 结果为空");
                qDebug() << "[Phase F] ================= FEM run end: empty result =================";
                return;
            }

            if (writePhaseFResult(result, resultPath)) {
                qDebug() << "[Phase F] FEM VTU saved:" << phaseFPresetId << resultPath;
            } else {
                qWarning() << "[Phase F] Failed to save FEM VTU:" << phaseFPresetId << resultPath;
            }

            // CellData → PointData
            vtkNew<vtkCellDataToPointData> c2p;
            c2p->SetInputData(result);
            c2p->Update();
            auto* c2pOut = c2p->GetOutput();
            if (c2pOut && c2pOut->GetPointData()) {
                c2pOut->GetPointData()->SetActiveScalars("E_mag_Vmm");
            }
            const char* activeScalarName = nullptr;
            if (c2pOut && c2pOut->GetPointData() && c2pOut->GetPointData()->GetScalars()) {
                activeScalarName = c2pOut->GetPointData()->GetScalars()->GetName();
            }
            qDebug() << "[VTA] contour active point scalar ="
                     << (activeScalarName ? activeScalarName : "(null)");

            // MarchingCubes 提取等值面
            double threshold = dbs_fem::getVTAThreshold(spec.pulseWidth);
            qDebug() << "[VTA] 脉宽=" << spec.pulseWidth << "μs, 阈值=" << threshold << "V/mm";

            vtkNew<vtkContourFilter> contour;
            contour->SetInputConnection(c2p->GetOutputPort());
            contour->SetValue(0, threshold);
            contour->Update();

            vtkSmartPointer<vtkPolyData> vtaPoly = contour->GetOutput();
            qDebug() << "[VTA] 等值面: " << vtaPoly->GetNumberOfPoints() << "pts,"
                     << vtaPoly->GetNumberOfCells() << "cells";

            double vtaVolume = 0.0;
            if (vtaPoly && vtaPoly->GetNumberOfCells() > 0) {
                vtkNew<vtkTriangleFilter> vtaTri;
                vtaTri->SetInputData(vtaPoly);
                vtaTri->Update();
                vtkNew<vtkMassProperties> mass;
                mass->SetInputConnection(vtaTri->GetOutputPort());
                mass->Update();
                vtaVolume = mass->GetVolume();
            }
            double vtaEqR = (vtaVolume > 0.0)
                ? std::cbrt(3.0 * vtaVolume / (4.0 * vtkMath::Pi()))
                : 0.0;

            // ===== [VTA SUMMARY] 汇报用最终摘要 =====
            vtkDataArray* diag = result->GetFieldData()
                ? result->GetFieldData()->GetArray("FEMDiagnostics") : nullptr;
            if (diag && diag->GetNumberOfTuples() >= 57) {
                auto dv = [diag](int i) { return diag->GetTuple1(i); };
                double vtaB[6] = {0};
                vtaPoly->GetBounds(vtaB);
                double vtaDx = vtaB[1] - vtaB[0];
                double vtaDy = vtaB[3] - vtaB[2];
                double vtaDz = vtaB[5] - vtaB[4];
                double vtaRx = 0.5 * vtaDx;
                double vtaRy = 0.5 * vtaDy;
                double vtaRz = 0.5 * vtaDz;
                double vtaDiagHalf = 0.5 * std::sqrt(vtaDx*vtaDx + vtaDy*vtaDy + vtaDz*vtaDz);
                int selC = static_cast<int>(dv(3));
                double selSurf = (selC >= 0 && selC < 4) ? dv(25 + selC) : 0.0;
                double selVol  = (selC >= 0 && selC < 4) ? dv(29 + selC) : 0.0;
                double maxE = dv(14);
                double p99  = dv(13);
                double p995 = dv(17);
                double p999 = dv(18);
                double robustMaxE = dv(19);
                double dirPct = dv(10);
                double tinyVolP1 = dv(20);
                double activeVol = dv(21);
                int activeCells = static_cast<int>(dv(22));
                int surfaceNoBrain = static_cast<int>(dv(23));
                int hollowedSurface = static_cast<int>(dv(24));
                double selAllSurf = (selC >= 0 && selC < 4) ? dv(33 + selC) : 0.0;
                double selAllShared = (selC >= 0 && selC < 4) ? dv(37 + selC) : 0.0;
                double selAllNoBrain = (selC >= 0 && selC < 4) ? dv(41 + selC) : 0.0;
                double selActiveShared = (selC >= 0 && selC < 4) ? dv(45 + selC) : 0.0;
                double selActiveNoBrain = (selC >= 0 && selC < 4) ? dv(49 + selC) : 0.0;
                double selEffective = (selC >= 0 && selC < 4) ? dv(53 + selC) : 0.0;
                double selBrainFacingRatio = (diag->GetNumberOfTuples() >= 77 && selC >= 0 && selC < 4) ? dv(57 + selC) : 0.0;
                double selEffectiveRatio = (diag->GetNumberOfTuples() >= 77 && selC >= 0 && selC < 4) ? dv(61 + selC) : 0.0;
                double selFluxMA = (diag->GetNumberOfTuples() >= 77 && selC >= 0 && selC < 4) ? dv(65 + selC) : 0.0;
                double contactFlux0 = (diag->GetNumberOfTuples() >= 77) ? dv(65) : 0.0;
                double contactFlux1 = (diag->GetNumberOfTuples() >= 77) ? dv(66) : 0.0;
                double contactFlux2 = (diag->GetNumberOfTuples() >= 77) ? dv(67) : 0.0;
                double contactFlux3 = (diag->GetNumberOfTuples() >= 77) ? dv(68) : 0.0;
                bool solverOk  = (robustMaxE > 0.2 && robustMaxE < 30.0 && p999 > 0.05 && p999 < 10.0);
                bool contactOk = (selEffectiveRatio >= 0.95);
                bool vtaOk     = (vtaPoly->GetNumberOfPoints() > 0);
                QString bcRoute = "先检查brain-facing分类";
                if (selEffectiveRatio >= 0.95) {
                    bcRoute = "BC耦合已充分";
                } else if (selEffectiveRatio >= 0.80) {
                    bcRoute = "BC耦合可接受";
                }

                qDebug() << "[VTA SUMMARY] ================= 本次 FEM-VTA 汇报摘要 =================";
                qDebug() << "[VTA SUMMARY] 数据: T2 MNI image + nuclei labels(1-6), spacing~1mm";
                qDebug() << "[VTA SUMMARY] 网格: nodes=" << static_cast<int>(dv(0))
                         << " tets="  << static_cast<int>(dv(1))
                         << " tris="  << static_cast<int>(dv(2))
                         << " | 合理: nodes>5000, tets>20000";
                qDebug() << "[VTA SUMMARY] 刺激: UI amplitude=" << dv(5)
                         << " 按电压控制解释为" << dv(4) << "V"
                         << " pulseWidth=" << static_cast<int>(dv(6)) << "us";
                qDebug() << "[VTA SUMMARY] 触点: selected=" << selC
                         << " surfaceNodes=" << selSurf
                         << " volumeNodes="  << selVol
                         << " | 目标: brain-facing effective ratio>=0.95";
                qDebug() << "[VTA SUMMARY] selected contact BC有效性: allSurface="
                         << selAllSurf
                         << " allShared=" << selAllShared
                         << " allNoBrain=" << selAllNoBrain
                         << " activeShared=" << selActiveShared
                         << " activeNoBrain=" << selActiveNoBrain
                         << " effective=" << selEffective
                         << " brainFacingRatio=" << selBrainFacingRatio
                         << " effectiveRatio=" << selEffectiveRatio
                         << " | 判定:" << bcRoute
                         << (contactOk ? " [OK]" : " [NEEDS IMPROVEMENT]");
                qDebug() << "[VTA SUMMARY] contact flux:"
                         << "C0=" << contactFlux0 << "mA"
                         << "C1=" << contactFlux1 << "mA"
                         << "C2=" << contactFlux2 << "mA"
                         << "C3=" << contactFlux3 << "mA"
                         << " | floating目标: inactive≈0";
                qDebug() << "[VTA SUMMARY] 电极表面共享: surfaceNoBrain="
                         << surfaceNoBrain
                         << " hollowedSurface=" << hollowedSurface
                         << " | 目标: hollowedSurface=0";
                qDebug() << "[VTA SUMMARY] Dirichlet: total=" << static_cast<int>(dv(7))
                         << QString("(%1%)").arg(dirPct, 0, 'f', 1)
                         << " contact=" << static_cast<int>(dv(8))
                         << " outer="   << static_cast<int>(dv(9))
                         << " | 小 ROI 下外边界占比偏高是正常现象";
                qDebug() << "[VTA SUMMARY] E-field: p50=" << dv(11)
                         << " p95=" << dv(12)
                         << " p99=" << p99
                         << " p99.5=" << p995
                         << " p99.9=" << p999
                         << " max=" << maxE << "V/mm"
                         << " robustMax=" << robustMaxE
                         << " | 重点看 p99.9/robustMax 而非全局 p99/raw max"
                         << (solverOk ? " [OK]" : " [CHECK]");
                qDebug() << "[VTA SUMMARY] maxE cell: label=" << static_cast<int>(dv(15))
                         << " vol=" << dv(16) << "mm^3"
                         << " tinyVolP1=" << tinyVolP1
                         << " | 合理: raw max 不应只由极小体积单元主导";
                qDebug() << "[VTA SUMMARY] thresholded brain: cells="
                         << activeCells << " volume=" << activeVol << "mm^3";
                qDebug() << "[VTA SUMMARY] VTA: threshold=" << threshold << "V/mm"
                         << " pts=" << vtaPoly->GetNumberOfPoints()
                         << " cells=" << vtaPoly->GetNumberOfCells()
                         << " bounds=" << vtaB[0] << vtaB[1] << vtaB[2]
                         << vtaB[3] << vtaB[4] << vtaB[5]
                         << " halfExtents=" << vtaRx << vtaRy << vtaRz
                         << " diagHalf=" << vtaDiagHalf
                         << " volume=" << vtaVolume
                         << " eqSphereR=" << vtaEqR
                         << " | 文献目标(3V/60us/3389): 半径约2-3mm, 直径4-6mm"
                         << (vtaOk ? " [OK]" : " [EMPTY]");
                qDebug() << "[VTA SUMMARY] 结论: solver="
                         << (solverOk  ? "OK" : "CHECK")
                         << ", VTA extract="
                         << (vtaOk     ? "OK" : "EMPTY")
                         << ", contact meshing="
                         << (contactOk ? "OK" : "NEEDS IMPROVEMENT");
                qDebug() << "[VTA SUMMARY] ========================================================";
            }

            if (vtaPoly->GetNumberOfPoints() == 0) {
                QMessageBox::information(this, "VTA", "在当前阈值下未检测到激活区域");
                qDebug() << "[Phase F] ================= FEM run end: empty VTA =================";
                return;
            }

            // 替换球形 VTA
            if (m_trajManager && m_trajManager->m_dbsLead) {
                m_trajManager->m_dbsLead->setRealVTA(vtaPoly);
                m_trajManager->m_dbsLead->setShowVTA(true);
                refreshAllViews();
                calculateVTAIntersection();
                QMessageBox::information(this, "VTA", "真实 VTA 计算完成！");
            }
            qDebug() << "[Phase F] ================= FEM run end: success =================";
        });

        connect(simWorker, &DBSSimWorker::finished, simThread, &QThread::quit);
        connect(simThread, &QThread::finished, simWorker, &QObject::deleteLater);
        connect(simThread, &QThread::finished, simThread, &QObject::deleteLater);

        qDebug() << "[VTA-Debug] 信号连接完毕, 即将启动 simThread...";
        simThread->start();
        qDebug() << "[VTA-Debug] simThread->start() 已调用";
    });

    connect(meshWorker, &DBSMeshWorker::finished, meshThread, &QThread::quit);
    connect(meshWorker, &DBSMeshWorker::errorOccurred, meshThread, &QThread::quit);
    connect(meshThread, &QThread::finished, meshWorker, &QObject::deleteLater);
    connect(meshThread, &QThread::finished, meshThread, &QObject::deleteLater);

    meshThread->start();
}