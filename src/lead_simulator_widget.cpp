#include "lead_simulator_widget.h"
#include "dbs_lead_model.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <vtkCamera.h>
#include <vtkProperty.h>
#include <vtkLight.h>
#include <vtkCellPicker.h>
#include <vtkObjectFactory.h>
#include <vtkAssemblyPath.h>
#include <vtkAssemblyNode.h>

LeadSimulatorWidget::LeadSimulatorWidget(QWidget* parent)
    : QWidget(parent)
{
    m_simLead = new DBSLeadModel();
    setupUi();
    setupVTK();
}

LeadSimulatorWidget::~LeadSimulatorWidget()
{
    delete m_simLead;
}

void LeadSimulatorWidget::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ============================================================
    // 上半部分：VTK 渲染窗口
    // ============================================================
    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // 【核心修复】：必须开启 MouseTracking，否则悬停事件不会被触发
    m_vtkWidget->setMouseTracking(true); 

    mainLayout->addWidget(m_vtkWidget, 3); // 占较大比例

    // ============================================================
    // 下半部分：控制面板
    // ============================================================
    QWidget* controlPanel = new QWidget(this);
    controlPanel->setStyleSheet("background-color: #f0f4f8; color: #333; border-top: 1px solid #ccc;");
    QVBoxLayout* cLayout = new QVBoxLayout(controlPanel);
    
    QHBoxLayout* hPreset = new QHBoxLayout;
    hPreset->addWidget(new QLabel("Phase F Preset:"));
    cmbPhaseFPreset = new QComboBox();
    cmbPhaseFPreset->addItem("Manual / 当前参数");
    cmbPhaseFPreset->addItem("F1: Contact2, Encap σ=0.115");
    cmbPhaseFPreset->addItem("F2: Contact0, Encap σ=0.115");
    cmbPhaseFPreset->addItem("F3: Contact2, No Encap");
    cmbPhaseFPreset->addItem("F4: Contact2, Encap σ=0.070");
    cmbPhaseFPreset->addItem("Video Demo: Contact1, 4.0V/80us");
    hPreset->addWidget(cmbPhaseFPreset, 1);
    cLayout->addLayout(hPreset);

    // 1. 深度与型号
    QHBoxLayout* h1 = new QHBoxLayout;
    h1->addWidget(new QLabel("进针深度(mm):"));
    spnDepth = new QDoubleSpinBox();
    spnDepth->setRange(-5.0, 5.0); spnDepth->setSingleStep(0.1); spnDepth->setValue(0.0);
    h1->addWidget(spnDepth);
    
    h1->addSpacing(20);
    h1->addWidget(new QLabel("电极型号:"));
    cmbLeadType = new QComboBox();
    cmbLeadType->addItem("Medtronic 3389");
    cmbLeadType->addItem("Medtronic 3387");
    cmbLeadType->addItem("Boston Cartesia");
    h1->addWidget(cmbLeadType);
    cLayout->addLayout(h1);

    // 2. 幅度与脉宽
    QHBoxLayout* h2 = new QHBoxLayout;
    h2->addWidget(new QLabel("幅度(mA):"));
    spnAmplitude = new QDoubleSpinBox();
    spnAmplitude->setRange(0.0, 5.0); spnAmplitude->setSingleStep(0.1); spnAmplitude->setValue(0.0);
    h2->addWidget(spnAmplitude);

    h2->addSpacing(20);
    h2->addWidget(new QLabel("脉宽(\xc2\xb5s):"));
    spnPulseWidth = new QSpinBox();
    spnPulseWidth->setRange(20, 450); spnPulseWidth->setSingleStep(10); spnPulseWidth->setValue(60);
    h2->addWidget(spnPulseWidth);
    cLayout->addLayout(h2);

    // 3. 频率与 VTA
    QHBoxLayout* h3 = new QHBoxLayout;
    h3->addWidget(new QLabel("频率(Hz):"));
    spnFrequency = new QSpinBox();
    spnFrequency->setRange(2, 255); spnFrequency->setSingleStep(5); spnFrequency->setValue(130);
    h3->addWidget(spnFrequency);

    h3->addSpacing(20);
    chkShowVTA = new QCheckBox("显示 VTA");
    chkShowVTA->setChecked(false);
    h3->addWidget(chkShowVTA);
    cLayout->addLayout(h3);

    QHBoxLayout* hVtaPreset = new QHBoxLayout;
    hVtaPreset->addWidget(new QLabel("VTA显示风格:"));
    cmbVTADisplayPreset = new QComboBox();
    cmbVTADisplayPreset->addItem("青绿玻璃");
    cmbVTADisplayPreset->addItem("琥珀玻璃");
    cmbVTADisplayPreset->addItem("黄色透明");
    hVtaPreset->addWidget(cmbVTADisplayPreset, 1);
    cLayout->addLayout(hVtaPreset);

    // 4. 触点控制 (网格布局，类似图二的排版)
    QGridLayout* grid = new QGridLayout;
    for (int i = 0; i < 4; ++i) {
        btnContact[i] = new QPushButton(QString("触点 %1").arg(i));
        btnContact[i]->setCheckable(true);
        updateContactButtonUI(i, 0);
        
        // 布局排版：两列两排。左列：0,2；右列：1,3
        int row = (i / 2);
        int col = (i % 2);
        grid->addWidget(new QLabel(QString("触点 %1:").arg(i)), row, col * 2);
        grid->addWidget(btnContact[i], row, col * 2 + 1);

        connect(btnContact[i], &QPushButton::clicked, [this, i]() { onContactButtonClicked(i); });
    }
    cLayout->addLayout(grid);

    // 5. 计算真实 VTA 按钮
    btnComputeVTA = new QPushButton("计算真实 VTA (FEM)");
    btnComputeVTA->setStyleSheet(
        "QPushButton { background-color: #2563eb; color: white; font-weight: bold; "
        "padding: 6px 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #1d4ed8; }"
        "QPushButton:pressed { background-color: #1e40af; }");
    cLayout->addWidget(btnComputeVTA);
    connect(btnComputeVTA, &QPushButton::clicked, this, &LeadSimulatorWidget::sigComputeRealVTA);

    mainLayout->addWidget(controlPanel, 1); // 占较小比例

    // ============================================================
    // 信号连接（自己变 -> 更新视图并往外发）
    // 【修复黑屏】：全部使用 requestRender() 代替直接 Render()
    // ============================================================
    connect(cmbLeadType, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int idx) {
        m_simLead->setLeadType(idx);
        requestRender();
        emit sigLeadTypeChanged(idx);
    });
    connect(spnAmplitude, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double v) {
        m_simLead->setAmplitude(v);
        requestRender();
        emit sigAmplitudeChanged(v);
    });
    connect(spnPulseWidth, QOverload<int>::of(&QSpinBox::valueChanged), [this](int v) {
        m_simLead->setPulseWidth(v);
        requestRender();
        emit sigPulseWidthChanged(v);
    });
    connect(spnFrequency, QOverload<int>::of(&QSpinBox::valueChanged), [this](int v) {
        emit sigFrequencyChanged(v);
    });
    connect(spnDepth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double v) {
        // 在模拟器中，进针深度可能不需要影响居中显示的坐标，但我们可以存下来发给主窗口
        emit sigDepthChanged(v);
    });
    connect(chkShowVTA, &QCheckBox::toggled, [this](bool v) {
        m_simLead->setShowVTA(v);
        requestRender();
        emit sigShowVTA(v);
    });
    connect(cmbVTADisplayPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int idx) {
        m_simLead->setVTADisplayPreset(idx);
        requestRender();
        emit sigVTADisplayPresetChanged(idx);
    });
    connect(cmbPhaseFPreset, QOverload<int>::of(&QComboBox::activated), [this](int idx) {
        emit sigPhaseFPresetRequested(idx);
    });
}

// ============================================================
// 【修复触点交互】自定义交互器
//   - 使用 vtkCellPicker（射线-单元交叉）替代 vtkPropPicker（包围盒）
//   - 所有渲染调用走 requestRender() 防止 GL 上下文抢占
// ============================================================
class LeadSimulatorInteractorStyle : public vtkInteractorStyleTrackballCamera {
public:
    static LeadSimulatorInteractorStyle* New();
    vtkTypeMacro(LeadSimulatorInteractorStyle, vtkInteractorStyleTrackballCamera);

    LeadSimulatorWidget* m_widget = nullptr;
    vtkActor* m_hoveredActor = nullptr;
    double m_originalAmbient = 0.0;
    
    // 拖拽深度状态
    bool m_isDraggingDepth = false;
    int m_lastY = 0;

    // 【关键修复】：用 vtkCellPicker 做精确射线拾取
    vtkActor* GetPickedActor() {
        if (!this->Interactor || !this->DefaultRenderer) return nullptr;
        int* pos = this->Interactor->GetEventPosition();
        vtkNew<vtkCellPicker> picker;
        picker->SetTolerance(0.005);
        picker->Pick(pos[0], pos[1], 0.0, this->DefaultRenderer);
        // vtkCellPicker::GetActor() 能直接返回 Assembly 内部的叶子 Actor
        //return picker->GetActor();

        // 强制提取装配体路径的最后一个叶子节点（真正的子部件 Actor）
        vtkAssemblyPath* path = picker->GetPath();
        if (path) {
            vtkProp* prop = path->GetLastNode()->GetViewProp();
            return vtkActor::SafeDownCast(prop);
        }
        return nullptr;
    }

    void OnLeftButtonDown() override {
        if (!m_widget || !this->Interactor) {
            vtkInteractorStyleTrackballCamera::OnLeftButtonDown();
            return;
        }

        // 1. 检查是否是 Shift + 拖拽深度
        if (this->Interactor->GetShiftKey()) {
            m_isDraggingDepth = true;
            m_lastY = this->Interactor->GetEventPosition()[1];
            m_widget->setCursor(Qt::SizeVerCursor);
            return;
        }

        // 2. 检查是否点击了触点
        vtkActor* pickedActor = GetPickedActor();
        if (pickedActor) {
            int contactIdx = m_widget->getLeadModel()->GetContactIndexFromActor(pickedActor);
            if (contactIdx >= 0) {
                // 点击到了触点，执行极性切换
                m_widget->onContactButtonClicked(contactIdx);
                m_widget->requestRender();
                return; // 消耗掉该事件，不进行旋转
            }
        }
        vtkInteractorStyleTrackballCamera::OnLeftButtonDown();
    }
    
    void OnLeftButtonUp() override {
        if (m_isDraggingDepth) {
            m_isDraggingDepth = false;
            m_widget->setCursor(Qt::ArrowCursor);
            return;
        }
        vtkInteractorStyleTrackballCamera::OnLeftButtonUp();
    }

    void OnMouseMove() override {
        if (!m_widget || !this->Interactor) {
            vtkInteractorStyleTrackballCamera::OnMouseMove();
            return;
        }

        // 1. 如果正在拖拽深度
        if (m_isDraggingDepth) {
            int currentY = this->Interactor->GetEventPosition()[1];
            int dy = currentY - m_lastY;
            m_lastY = currentY;
            if (dy != 0) {
                double currentDepth = m_widget->getDepth();
                double newDepth = currentDepth - dy * 0.05;
                m_widget->setDepth(newDepth);
            }
            return;
        }

        // 2. 悬停检测
        vtkActor* pickedActor = GetPickedActor();
        bool isHoveringContact = false;
        if (pickedActor && m_widget->getLeadModel()->GetContactIndexFromActor(pickedActor) >= 0) {
            isHoveringContact = true;
        }

        if (isHoveringContact) {
            if (m_hoveredActor != pickedActor) {
                // 恢复之前的
                if (m_hoveredActor) {
                    m_hoveredActor->GetProperty()->SetAmbient(m_originalAmbient);
                }
                // 记录并高亮新的
                m_hoveredActor = pickedActor;
                m_originalAmbient = m_hoveredActor->GetProperty()->GetAmbient();
                m_hoveredActor->GetProperty()->SetAmbient(1.0); // 大幅提升环境光以高亮
                // 【修复】：直接调用 Render() 确保高亮立即显示
                if (this->Interactor) this->Interactor->Render();
            }
            m_widget->setCursor(Qt::PointingHandCursor);
        } else {
            if (m_hoveredActor) {
                // 恢复之前的
                m_hoveredActor->GetProperty()->SetAmbient(m_originalAmbient);
                m_hoveredActor = nullptr;
                // 【修复】：直接调用 Render() 确保恢复立即显示
                if (this->Interactor) this->Interactor->Render();
            }
            m_widget->setCursor(Qt::ArrowCursor);
        }
        vtkInteractorStyleTrackballCamera::OnMouseMove();
    }
};
vtkStandardNewMacro(LeadSimulatorInteractorStyle);

void LeadSimulatorWidget::setupVTK()
{
    vtkNew<vtkGenericOpenGLRenderWindow> renderWindow;
    m_vtkWidget->SetRenderWindow(renderWindow);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_vtkWidget->GetRenderWindow()->AddRenderer(m_renderer);

    // 设置背景颜色：柔和的深色（护眼，同时能突出铂铱合金的金属高光）
    m_renderer->SetBackground(0.12, 0.14, 0.18); 

    // 添加独立的模型
    m_renderer->AddActor(m_simLead->GetAssembly3D());

    // ============================================================
    // 【核心修复】：DBSLeadModel 必须给它一个"初始虚拟路径"才会显示实体
    // 并且默认是隐藏的，需要手动将其设为可见
    // ============================================================
    double dummyEntry[3] = {0.0, 15.0, 0.0}; 
    double dummyTarget[3] = {0.0, 0.0, 0.0};  // 靶点在原点
    m_simLead->UpdateTrajectory(dummyEntry, dummyTarget);
    m_simLead->SetVisibility(true);

    // 增加光照以突出金属质感
    vtkNew<vtkLight> light;
    light->SetPosition(10, 10, 10);
    light->SetFocalPoint(0, 0, 0);
    m_renderer->AddLight(light);

    // 设置相机
    vtkCamera* camera = m_renderer->GetActiveCamera();
    camera->SetPosition(0, 0, 15); 
    camera->SetFocalPoint(0, 0, 0);
    camera->SetViewUp(0, 1, 0);
    m_renderer->ResetCamera();

    // 设置交互模式：允许拖拽旋转 + 点击触点
    vtkNew<LeadSimulatorInteractorStyle> style;
    style->SetDefaultRenderer(m_renderer);
    style->m_widget = this;
    m_vtkWidget->GetRenderWindow()->GetInteractor()->SetInteractorStyle(style);
}

void LeadSimulatorWidget::onContactButtonClicked(int index)
{
    int& pol = m_contactPolarity[index];
    if (pol == 0)       pol = -1;
    else if (pol == -1) pol = 1;
    else                pol = 0;

    updateContactButtonUI(index, pol);
    
    // 转换为枚举类型以传递给核心模型
    ContactPolarity enumPol = POLARITY_OFF;
    if (pol == -1) enumPol = POLARITY_CATHODE;
    else if (pol == 1) enumPol = POLARITY_ANODE;

    // 更新本地模型
    m_simLead->setContactPolarity(index, enumPol);
    // 【修复】：点击变色后立即强制渲染，不再等待事件循环
    if (m_vtkWidget && m_vtkWidget->GetRenderWindow()) {
        m_vtkWidget->GetRenderWindow()->Render();
    }

    // 通知外部主窗口
    emit sigContactPolarityChanged(index, pol);
}

void LeadSimulatorWidget::updateContactButtonUI(int index, int polarity)
{
    QString text;
    QString style;
    if (polarity == -1) {
        text = "Cathode (-)";
        style = "QPushButton { background-color: #409EFF; color: white; border-radius: 12px; padding: 4px; }";
    } else if (polarity == 1) {
        text = "Anode (+)";
        style = "QPushButton { background-color: #F56C6C; color: white; border-radius: 12px; padding: 4px; }";
    } else {
        text = "Off";
        style = "QPushButton { background-color: #e0e0e0; color: #555; border-radius: 12px; padding: 4px; }";
    }
    btnContact[index]->setText(text);
    btnContact[index]->setStyleSheet(style);
}

// 外部同步接口（例如从配置文件加载时调用）
void LeadSimulatorWidget::setParameters(int typeIndex, double depth, double amp, int pw, int freq, bool vta, const int polarities[4])
{
    cmbLeadType->blockSignals(true); cmbLeadType->setCurrentIndex(typeIndex); cmbLeadType->blockSignals(false);
    spnDepth->blockSignals(true); spnDepth->setValue(depth); spnDepth->blockSignals(false);
    spnAmplitude->blockSignals(true); spnAmplitude->setValue(amp); spnAmplitude->blockSignals(false);
    spnPulseWidth->blockSignals(true); spnPulseWidth->setValue(pw); spnPulseWidth->blockSignals(false);
    spnFrequency->blockSignals(true); spnFrequency->setValue(freq); spnFrequency->blockSignals(false);
    chkShowVTA->blockSignals(true); chkShowVTA->setChecked(vta); chkShowVTA->blockSignals(false);

    for(int i=0; i<4; i++) {
        m_contactPolarity[i] = polarities[i];
        updateContactButtonUI(i, polarities[i]);
        
        ContactPolarity enumPol = POLARITY_OFF;
        if (polarities[i] == -1) enumPol = POLARITY_CATHODE;
        else if (polarities[i] == 1) enumPol = POLARITY_ANODE;
        m_simLead->setContactPolarity(i, enumPol);
    }
    
    m_simLead->setLeadType(typeIndex);
    m_simLead->setAmplitude(amp);
    m_simLead->setPulseWidth(pw);
    m_simLead->setFrequency(freq);
    m_simLead->setShowVTA(vta);
    // 【修复黑屏】：走 Qt 安全渲染
    requestRender();
}

void LeadSimulatorWidget::setPhaseFPresetIndex(int index)
{
    if (!cmbPhaseFPreset) {
        return;
    }
    cmbPhaseFPreset->blockSignals(true);
    cmbPhaseFPreset->setCurrentIndex(index);
    cmbPhaseFPreset->blockSignals(false);
}
