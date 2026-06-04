#ifndef DICOMVIEWER_3D_H

#define DICOMVIEWER_3D_H

#pragma execution_character_set("utf-8")



#include <QMainWindow>

#include <QPushButton>

#include <QLabel>

#include <QSlider>

#include <QVBoxLayout>

#include <QHBoxLayout>

#include <QFrame> // 新增：用于标题栏背景





// ===== VTK =====

#include <QVTKOpenGLNativeWidget.h>

#include <vtkSmartPointer.h>

#include <vtkRenderer.h>

#include <vtkImageData.h>

#include <vtkImageViewer2.h>

#include <vtkImageActor.h>

#include <vtkImageMapToColors.h>

#include <vtkLookupTable.h>



// 面绘制算法

#include <vtkDiscreteMarchingCubes.h> // 或者 vtkDiscreteFlyingEdges3D (更快)

#include <vtkWindowedSincPolyDataFilter.h> // 平滑

#include <vtkPolyDataNormals.h> // 法线(光照)

#include <vtkPolyDataMapper.h>

#include <vtkActor.h>

#include <vtkProperty.h>

#include <vtkBillboardTextActor3D.h>

#include <vtkTextProperty.h>

//更细致的体绘制所需

#include <vtkImageAccumulate.h>

#include <vtkVolumeProperty.h>

#include <vtkPiecewiseFunction.h>

#include <vtkColorTransferFunction.h>



#include <QGroupBox>

#include <QFormLayout>



#include <vtkCellPicker.h>

#include <vtkCursor3D.h>   // 用于画十字

#include <vtkSphereSource.h> // 用于画球

#include <vtkLineSource.h>   // 用于画针

#include <vtkTubeFilter.h>   // 用于把线变成管



#include <vtkAxisActor2D.h>
#include <vtkProperty2D.h>
#include <vtkTextActor.h>
#include <vtkActor2D.h>
#include <vtkPolyDataMapper2D.h>

#include "control_panel.h" // 引入新头文件

#include "volume_roi_manager.h"

#include <vtkImagePlaneWidget.h>

#include <vtkResliceImageViewer.h> 

#include <vtkResliceCursorRepresentation.h>

#include <vtkImplicitPlaneWidget2.h>

#include <vtkImplicitPlaneRepresentation.h>

#include <vtkImageResliceMapper.h>

#include <vtkPlane.h>

#include "trajectory_manager.h"
#include "lead_simulator_widget.h"
#include "ai_run_manager.h"
#include "dbs_fem_types.h"


// 【核心结构体】视图上下文

// 把一个视图的所有 UI 控件和 VTK 对象打包在一起

struct SliceViewContext {

    // UI 组件

    QWidget* container = nullptr;              // 外层容器

    QVTKOpenGLNativeWidget* widget = nullptr;  // VTK 显示窗口

    QSlider* slider = nullptr;                 // 底部滑动条

    QLabel* titleLabel = nullptr;              // 标题



    // VTK 逻辑组件

    //vtkSmartPointer<vtkImageViewer2> viewer;

    vtkSmartPointer<vtkResliceImageViewer> viewer; // 改成这个

    vtkSmartPointer<vtkImageActor> overlayActor;



    // 状态

    int viewIndex = -1; // 0, 1, 2



    // 【新增】记录该视图下，靶点和进针点在哪一层

    int targetSliceIndex = -1;

    int entrySliceIndex = -1;

    // AC/PC/MSP 点所在切片
    int acSliceIndex  = -1;
    int pcSliceIndex  = -1;
    int mspSliceIndex = -1;

    // 【新增】交互样式，用于处理滚轮同步
    vtkSmartPointer<vtkInteractorStyleImage> style;

    // 方向标签 (四边居中单字母)
    vtkSmartPointer<vtkTextActor> orientTop;
    vtkSmartPointer<vtkTextActor> orientBottom;
    vtkSmartPointer<vtkTextActor> orientLeft;
    vtkSmartPointer<vtkTextActor> orientRight;

    // 智能距离标尺
    struct RulerState {
        vtkSmartPointer<vtkAxisActor2D> axis;
        double centerNorm        = 0.5;   // 标尺中心在 NormalizedViewport 上的位置
        double halfLenNorm       = 0.30;  // 标尺半长（NormalizedViewport 单位）
        double halfLenNormInitial = 0.0;  // 初始相对比例（首次布局时记录, 0 表示尚未初始化）
        double rangeMM           = 50.0;  // 当前量程 (mm)
        bool   manuallyAdjusted  = false; // 用户是否手动拉伸过
    };
    RulerState rulerV;  // 右侧垂直标尺
    RulerState rulerH;  // 底部水平标尺

    // 视图中心三角标记 (替代右/下方向字母)
    vtkSmartPointer<vtkActor2D> centerTriV; // 垂直标尺中点, 朝左
    vtkSmartPointer<vtkActor2D> centerTriH; // 水平标尺中点, 朝上

    // 全屏十字线
    vtkSmartPointer<vtkLineSource> crosshairHSrc;
    vtkSmartPointer<vtkLineSource> crosshairVSrc;
    vtkSmartPointer<vtkActor>      crosshairH;
    vtkSmartPointer<vtkActor>      crosshairV;
};



class dicomviewer_3d : public QMainWindow

{

    Q_OBJECT

    friend class MyViewStyle;

public:

    explicit dicomviewer_3d(QWidget* parent = nullptr);

    ~dicomviewer_3d();



    void createUI();

    void initVtkViews();

    void initLookupTable();



    // 加载数据

    bool loadNiftiVTK(const QString& file);

    bool loadDicomVTK(const QString& file);

    bool loadLabelVTK(const QString& file);



    // 渲染逻辑

    void renderVTK_nii();

    void renderVTK(); // 保留 DICOM 用的

    //标签面绘制

    void update3DLabelMesh();



    // 同步切片函数

    void updateOverlaySlice(int viewIndex);



    // 聚焦功能

    void focusOnROI(double* targetPos = nullptr, double viewSizeMM = 40.0);



    // 【新增】UI 更新函数：供 MyViewStyle 调用，当滚轮滚动时更新 Slider

    void updateSliderUI(int viewIndex, int slice);



    // 【新增】获取封装好的视图容器 (用于嵌入 MainWidget)

    QWidget* getAxialContainer() const { return m_views[0].container; }

    QWidget* getCoronalContainer() const { return m_views[1].container; }

    QWidget* getSagittalContainer() const { return m_views[2].container; }



    // 3D 窗口目前还是裸的 Widget，照旧返回

    QVTKOpenGLNativeWidget* get3DWidget() const { return vtkWidget3D; }




    // 【新增】处理选点逻辑的核心函数

    void handlePick(double* pos);



    // 【新增】处理撤销逻辑 (Ctrl+右键)

    void undoPick();


    // 【新增】通用跳转函数：将所有视图切换到指定的世界坐标切片
    void jumpToPosition(double* pos);

    //斜切推进
    void advanceSliceOblique(int viewIndex, int direction);

    // 【新增】判断主视图中的电极是否被选中，以及拖拽深度的委托函数
    bool isMainLeadPicked(class vtkPropPicker* picker);
    void dragMainLeadDepth(double offset);

    void translateMainLead(double offset[3]);

    void calculateVTAIntersection();

    bool runAiPlanFile(const QString& planPath, bool autoExit);

    // AC-PC 标定模式枚举
    enum AcPcPickMode {
        ACPC_NONE    = 0,
        ACPC_PICK_AC  = 1,
        ACPC_PICK_PC  = 2,
        ACPC_PICK_MSP = 3
    };

    // supply pick-mode query and hover callback to vtk_interaction_styles
    AcPcPickMode getAcpcPickMode() const { return m_acpcPickMode; }
    void handleHover(int viewIndex, int screenX, int screenY);
    bool isCrosshairVisible() const { return m_crosshairVisible; }
    void updateCrosshairPosition(int srcView, double worldPos[3]);
    void setCrosshairLinked(bool v) { m_crosshairLinked = v; refreshAllViews(); }

    // 标尺相关
    void updateRulerLayout(int viewIndex);     // 缩放后调用，重算量程
    enum RulerHitZone { RULER_NONE = 0, RULER_BODY, RULER_END_A, RULER_END_B };
    RulerHitZone hitTestRuler(int viewIndex, bool vertical, int screenX, int screenY);

public slots:

    void slot_openDicomFile();

    void slot_importLabel();

    // 【新增】响应滑动条拖动的槽函数

    void slot_update3DParams(double shell, double internal, double edge);

    // 【新增】重置参数槽函数

    void slot_reset3DParams();

    void slot_toggleOverlay();
    void slot_toggleVolume(); // 【新增】开关原图体绘制



    // 【新增】跳转槽函数

    void slot_jumpToTarget();

    void slot_jumpToEntry();



    // 【新增】生成/隐藏针的槽函数

    void slot_toggleNeedle();



    // 【新增】切换 3D 渲染模式：true=实体(适合切面), false=玻璃(适合透视)

    void slot_setRenderingMode(bool isSolid);



    // 【新增】响应 UI 复选框，显隐 3D 切片

    void slot_show3DPlane(int axis, bool show);

    void slot_resetPlanes(); // 复位切面

    void slot_showPlaneArrows(bool show);

    void slot_showOrientationBox(bool show);

    // 【新增】视图交换的核心函数
    void toggleMaximizeView(int viewIndex);

    void slot_toggleTrajectoryMode(); // 【新增】

    void slot_snapToNeedle(); // 【新增】

    void slot_resetWindowLevel(); // 【新增】把 3 个 2D 视图窗宽窗位恢复到初始智能值

    void slot_applyPhaseFPreset(int presetIndex);

    void slot_computeRealVTA(); // 【FEM】启动真实 VTA 计算流水线



private:

    // 【新增】工厂函数：创建带滑条的视图容器

    QWidget* createSliceContainer(int viewIndex, const QString& title, const QString& color);



    //体绘制辅助函数

    void applySolidStyle(vtkSmartPointer<vtkVolumeProperty> prop); // 实体模式 (CDF)

    //void applyGlassStyle(vtkSmartPointer<vtkVolumeProperty> prop,

    //    double shellOpacity = 0.6,

    //    double internalOpacity = 0.15);//玻璃模式

    void applyGlassStyle(vtkSmartPointer<vtkVolumeProperty> prop,

        double shellOpacity = 0.8,    // 调高

        double internalOpacity = 0.2, // 调高

        double edgeThreshold = 0.1);  // 调低



    // 【新增】初始化单个切片视图的通用逻辑

    // 参数：视图索引，切片方向，数据最小值，数据最大值

    void setupSliceView(int i, int orientation, double minVal, double maxVal);


    void refreshAllViews();



    // 【新增】应用实体切面样式 (动态计算)

    void applySolidCutStyle(vtkSmartPointer<vtkVolumeProperty> prop);



    // 【新增】统一更新视图状态的函数 (State Machine)

    void updateVolumeState();



    // 【新增】内部静态回调函数，用于处理 VTK 交互事件 (3D -> 2D 同步)

    static void onPlaneInteraction(vtkObject* caller, unsigned long eventId, void* clientData, void* callData);



    // 【新增】初始化切片控件的辅助函数

    void initPlaneWidgets();

    void updateOrientationMarkers();

    // AC-PC 标定模式状态
    AcPcPickMode m_acpcPickMode = ACPC_NONE;

    // hover preview state
    bool   m_hasPreview = false;
    double m_previewPos[3] = { 0.0, 0.0, 0.0 };
    vtkSmartPointer<vtkActor> m_previewCross2D[3];

    // unified commit for AC/PC/MSP (mouse click and keyboard input share this)
    void commitAcPcPoint(AcPcPickMode which, double pos[3]);

    // clear temporary preview cross actors
    void clearPreviewActors();

    // 刷新坐标显示
    void refreshCoordinateDisplay();

    // 全屏十字线
    bool   m_crosshairVisible = false;
    bool   m_crosshairLinked  = true;
    double m_crosshairWorld[3] = { 0.0, 0.0, 0.0 };
    void initCrosshairs();
    void setCrosshairVisible(bool v);
    void refreshCrosshairCoordDisplay();
    void syncCrosshairToSlice(int viewIndex);           // 切片变化后同步十字线 Z
    double getSliceWorldCoord(int viewIndex);            // 当前切片的 out-of-plane 世界坐标

    // 【新增】重新加载图像时的清理辅助
    void cleanupSliceViewAddons(int viewIndex);   // 移除 ruler/orient/triangle/crosshair/preview 等 actor
    void resetAllAcPcAndCrosshair();              // 等价于触发一次 sigResetAcPc

    // UI 控件

    ControlPanel* m_controlPanel;



    //滑块设定

    QString sliderStyle = R"(

        QSlider::groove:horizontal {

            border: 1px solid #3a3a3a;

            height: 6px;

            background: #202020;

            margin: 2px 0;

            border-radius: 3px;

        }

        QSlider::handle:horizontal {

            background: #b0b0b0;

            border: 1px solid #5c5c5c;

            width: 14px;

            height: 14px;

            margin: -5px 0;

            border-radius: 7px;

        }

        QSlider::handle:horizontal:hover {

            background: #ffffff;

        }

        QSlider::sub-page:horizontal {

            background: #00aaff; /* 进度条颜色：亮蓝 */

            border-radius: 3px;

        }

        QSlider::add-page:horizontal {

            background: #333333;

            border-radius: 3px;

        }

    )";





    // 【核心修改】使用结构体数组替代散乱变量

    SliceViewContext m_views[3];

    //vtkResliceCursor* sharedCursor = m_views[0].viewer->GetResliceCursor();



    // 3D 窗口比较特殊，暂时独立保留

    QVTKOpenGLNativeWidget* vtkWidget3D;



    // 存储所有核团的 Actor，方便后续删除或隐藏

    std::vector<vtkSmartPointer<vtkActor>> m_labelActors;



    // 通用 VTK 对象

    vtkSmartPointer<vtkRenderer> ren3d;

    vtkSmartPointer<vtkImageData> vtkImage;

    vtkSmartPointer<vtkImageData> vtkLabelImage;

    vtkSmartPointer<vtkLookupTable> labelLUT;

    vtkSmartPointer<vtkVolume> m_currentVolume;

    //窗宽窗位

    double m_smartWindow = 0;

    double m_smartLevel = 0;

    //相机是否偏转（初始时要偏转）

    bool needFlip = true;


    // 【新增】存储 3D 渲染参数 (单一数据源)

    double m_glassShell = 0.8;

    double m_glassInternal = 0.2;

    double m_glassEdge = 0.1;


    // 【新增】状态记录变量

    bool m_isRoiEnabled = false;   // ROI 框是否开启

    bool m_isSolidMode = false;    // 是否为实体模式


    // 【新增】是否仅显示裁剪结果，不显示框

    bool m_hideRoiBox = false;


    // 【重构】使用独立的轨迹管理器接管所有路径数据和渲染组件
    TrajectoryManager* m_trajManager = nullptr;


    // 【新增】ROI 管理器
    VolumeROIManager* m_roiManager = nullptr;



    // 【新增】交互组件 (负责箭头和框)
    vtkSmartPointer<vtkImplicitPlaneWidget2> m_impWidgets[3];



    // 【新增】显示组件 (负责画切片图)

    vtkSmartPointer<vtkImageResliceMapper> m_sliceMappers[3];

    vtkSmartPointer<vtkImageSlice> m_sliceActors[3];

    vtkSmartPointer<vtkPlane> m_slicePlanes[3]; // 连接交互和显示的数学平面



    // 【新增】线框盒子 Actor

    vtkSmartPointer<vtkActor> m_outlineActor;

    bool m_showPlaneArrows = true;

    bool m_showOrientationBox = true;

    vtkSmartPointer<vtkBillboardTextActor3D> m_orientationLabels[6];



    // 【新增】3个切片控件: 0=Axial(Z轴), 1=Coronal(Y轴), 2=Sagittal(X轴)

    vtkSmartPointer<vtkImagePlaneWidget> m_planeWidgets[3];

    // 【新增】记录当前谁被放大了 (-1 代表没人被放大，正常布局)
    int m_maximizedViewIndex = -1;
    // 【新增】把布局指针提为成员变量，方便随时调换里面的组件
    QVBoxLayout* m_layoutLeft = nullptr;
    QVBoxLayout* m_layoutRight = nullptr;

    bool m_isTrajectoryMode = false; // 记录当前是否处于轨迹视角

    //// 专门用来给某个 2D 视图赋予复杂矩阵的函数
    void applyObliqueMatrixToView(int viewIndex, vtkMatrix4x4* matrix);

    // ==========================================
    // 电极模拟器 (Lead Simulator)
    // ==========================================
    LeadSimulatorWidget* m_leadSimulator = nullptr;
    QPushButton* m_btnToggleSimulator = nullptr;
    QComboBox* m_cmbSingleSliceView = nullptr;
    QWidget* m_simulatorTopContainer = nullptr; // 包含下拉框和顶部 2D 视图的容器
    QWidget* m_bottom2DContainer = nullptr;     // 包含底部的 2 个 2D 视图的容器
    bool m_isSimulatorOpen = false;
    QString m_phaseFPresetId = "Manual";
    bool m_phaseFUseEncapsulation = true;
    double m_phaseFSigmaEncapsulation = 0.115;
    double m_phaseFEncapsulationThickness = 0.2;

    // AI managed runs: command-line plans and optional manual managed output.
    bool m_aiPlanActive = false;
    bool m_aiAutoExit = false;
    bool m_aiManualManagedRun = false;
    AiRunPlan m_aiPlan;
    int m_aiRunIndex = -1;
    QString m_aiPlanPath;
    QString m_aiBatchDir;
    AiRunItem m_aiCurrentRun;
    QString m_aiCurrentRunDir;
    QString m_aiCurrentResultPath;

    bool applyAiRunItem(const AiRunItem& item);
    void startNextAiPlanRun();
    void finishCurrentAiRun(bool success, const QString& resultPath, const QString& reason = QString());
    void startFemSolverFromMeshPath(const dbs_fem::DBSSimSpec& spec,
                                    const QString& phaseFPresetId,
                                    const QString& resultPath,
                                    bool exportFEMOutputs,
                                    const QString& meshPath);
    QJsonObject buildCurrentRunConfigJson(const QString& caseId,
                                          const dbs_fem::DBSSimSpec& spec,
                                          const QString& meshPath,
                                          const QString& resultPath,
                                          bool exportFEMOutputs) const;

    void toggleSimulator();
    void onSingleSliceViewChanged(int index);
};



#endif // DICOMVIEWER_3D_H
