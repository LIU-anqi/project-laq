#include "volume_roi_manager.h"
#include <vtkImageData.h>
#include <QDebug>
#include <vtkCallbackCommand.h>
#include <vtkProperty.h>

#include <vtkPlanes.h>          // 【新增】解决 GetPlanes 报错 (如果有用到)
#include <vtkPolyData.h>        // 【新增】解决 GetPolyData 报错
#include <vtkWidgetEventTranslator.h> 
#include <vtkWidgetEvent.h> 

VolumeROIManager::VolumeROIManager(QObject* parent) : QObject(parent)
{
    m_boxWidget = vtkSmartPointer<vtkBoxWidget2>::New();

    // 创建回调命令
    m_callback = vtkSmartPointer<vtkCallbackCommand>::New();
    m_callback->SetCallback(VolumeROIManager::onInteractionEvent);
    m_callback->SetClientData(this); // 把 this 指针传进去，以便静态函数访问成员
}

VolumeROIManager::~VolumeROIManager()
{
    if (m_boxWidget) {
        m_boxWidget->Off();
    }
}

void VolumeROIManager::initialize(vtkRenderWindowInteractor* interactor, vtkSmartVolumeMapper* mapper)
{
    if (!interactor || !mapper) return;

    m_targetMapper = mapper;

    // 1. 设置交互器
    m_boxWidget->SetInteractor(interactor);

    // 2. 设置外观 (Representation)
    auto rep = vtkSmartPointer<vtkBoxRepresentation>::New();
    rep->SetPlaceFactor(1.0); // 初始大小比例
    rep->HandlesOn();

    // 设置线条颜色为绿色，显眼一点
    rep->GetOutlineProperty()->SetColor(0.0, 1.0, 0.0);
    rep->GetSelectedOutlineProperty()->SetColor(1.0, 0.0, 0.0); // 选中变红

    // 这里的 PlaceWidget 需要依赖 Input 数据，所以先获取数据范围
    if (mapper->GetInput()) {
        rep->PlaceWidget(mapper->GetInput()->GetBounds());
    }
    m_boxWidget->SetRepresentation(rep);

    m_boxWidget->TranslationEnabledOn(); // 【修正】开启平移
    m_boxWidget->ScalingEnabledOn();     // 开启缩放
    m_boxWidget->RotationEnabledOff();    // 禁止旋转

    vtkWidgetEventTranslator* translator = m_boxWidget->GetEventTranslator();
    //1. 清空默认绑定 (屏蔽 Ctrl/Shift/右键)
    translator->ClearEvents();
    // 2. 重新绑定左键
    translator->SetTranslation(vtkCommand::LeftButtonPressEvent, vtkWidgetEvent::Select);
    translator->SetTranslation(vtkCommand::LeftButtonReleaseEvent, vtkWidgetEvent::EndSelect);
    translator->SetTranslation(vtkCommand::MouseMoveEvent,vtkWidgetEvent::Move);

    // 3. 添加监听器 (监听 InteractionEvent)
    m_boxWidget->AddObserver(vtkCommand::InteractionEvent, m_callback.Get());

    // 4. 开启裁剪功能
    // 这是 VTK 体绘制裁剪的开关，必须设为 true
    m_targetMapper->SetCropping(1);
    //初始同步
    doCrop();

    m_isInitialized = true;
}

void VolumeROIManager::enableROI(bool enable)
{
    if (!m_isInitialized) return;

    if (enable) {
        m_boxWidget->On();
        // 开启时，确保裁剪也是开启的
        m_targetMapper->SetCropping(1);
    }
    else {
        m_boxWidget->Off();
        // 关闭时，通常我们希望看到完整的图像，所以关闭裁剪
        // 或者你可以选择保留裁剪效果但隐藏框，看需求。这里先设为关闭裁剪。
        m_targetMapper->SetCropping(0);
    }

    // 触发一次渲染
    if (m_boxWidget->GetInteractor()) {
        m_boxWidget->GetInteractor()->Render();
    }
}

void VolumeROIManager::resetROI()
{
    if (!m_targetMapper || !m_targetMapper->GetInput()) return;

    // 获取原始图像边界
    double bounds[6];
    m_targetMapper->GetInput()->GetBounds();

    // 重置框的位置
    auto rep = vtkBoxRepresentation::SafeDownCast(m_boxWidget->GetRepresentation());
    if (rep) {
        rep->PlaceWidget(bounds);
    }

    // 执行裁剪重置
    doCrop();
}

void VolumeROIManager::onInteractionEvent(vtkObject* caller, unsigned long eventId, void* clientData, void* callData)
{
    // 静态函数转发给实例
    VolumeROIManager* manager = static_cast<VolumeROIManager*>(clientData);
    if (manager) {
        manager->doCrop();
    }
}

void VolumeROIManager::doCrop()
{
    if (!m_targetMapper) return;

    auto rep = vtkBoxRepresentation::SafeDownCast(m_boxWidget->GetRepresentation());
    if (!rep) return;

    // 1. 获取当前框的范围 (PolyData Bounds)
    double planes[24]; // VTK 这里的 Planes 其实是 6 个面的参数，但 SetCroppingRegionPlanes 需要的是 bounds
    double bounds[6];
    rep->GetPlanes(vtkSmartPointer<vtkPlanes>::New()); // 这一步其实是为了更新内部状态

    // 直接获取 Bounds 最简单
    // 注意：vtkBoxRepresentation 的 GetPlanes 比较复杂，用于斜切。
    // 对于轴对齐裁剪 (Axis-Aligned)，我们直接用 GetBounds 即可。
    // 但是 vtkBoxWidget2 的设计是支持旋转的。
    // 如果我们只需要轴对齐裁剪，应该在初始化时禁止旋转。

    // 修正：为了简单起见，我们假设用户只缩放、平移，不旋转盒子。
    // 实际上 vtkBoxWidget2 默认带旋转。我们需要在 initialize 里关掉旋转。

    rep->GetPolyData(vtkSmartPointer<vtkPolyData>::New()); // 刷新
    double* b = rep->GetBounds();

    // 2. 应用裁剪到 Mapper
    // SetCroppingRegionPlanes(xmin, xmax, ymin, ymax, zmin, zmax)
    m_targetMapper->SetCroppingRegionPlanes(b[0], b[1], b[2], b[3], b[4], b[5]);

    // 3. 发送信号给 UI (后续阶段用)
    emit sigROIChanged(b);
}

void VolumeROIManager::getROIBounds(double bounds[6])
{
    auto rep = vtkBoxRepresentation::SafeDownCast(m_boxWidget->GetRepresentation());
    if (rep) {
        rep->GetPolyData(vtkSmartPointer<vtkPolyData>::New());
        double* b = rep->GetBounds();
        for (int i = 0; i < 6; i++) bounds[i] = b[i];
    }
}

void VolumeROIManager::setROIBounds(double bounds[6])
{
    auto rep = vtkBoxRepresentation::SafeDownCast(m_boxWidget->GetRepresentation());
    if (rep) {
        rep->PlaceWidget(bounds);
        doCrop(); // 应用更改
        if (m_boxWidget->GetInteractor()) {
            m_boxWidget->GetInteractor()->Render();
        }
    }
}

// 1. 单独控制裁剪 (Mapper 设置)
void VolumeROIManager::setCroppingEnabled(bool enable)
{
    if (m_targetMapper) {
        m_targetMapper->SetCropping(enable ? 1 : 0);
    }
}

// 2. 单独控制框 (Widget 设置)
void VolumeROIManager::setBoxVisible(bool visible)
{
    if (m_boxWidget) {
        // SetEnabled(1) 会显示框并允许交互
        // SetEnabled(0) 会隐藏框并禁止交互
        m_boxWidget->SetEnabled(visible ? 1 : 0);
    }
}