#ifndef VOLUME_ROI_MANAGER_H
#define VOLUME_ROI_MANAGER_H

#include <QObject>
#include <vtkSmartPointer.h>
#include <vtkBoxWidget2.h>
#include <vtkBoxRepresentation.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkCommand.h>
#include <QCheckBox>

class VolumeROIManager : public QObject
{
    Q_OBJECT

public:
    explicit VolumeROIManager(QObject* parent = nullptr);
    ~VolumeROIManager();

    // 初始化：传入交互器(用于响应鼠标) 和 Mapper(用于执行裁剪)
    void initialize(vtkRenderWindowInteractor* interactor, vtkSmartVolumeMapper* mapper);

    // 开关 ROI 框
    void enableROI(bool enable);
    // 【新增】单独控制裁剪功能的开关 (只切图，不关框)
    void setCroppingEnabled(bool enable);
    // 【新增】单独控制框的可见性 (只隐框，不关裁剪)
    void setBoxVisible(bool visible);

    // 重置 ROI 框到图像边界
    void resetROI();

    // 获取当前 ROI 的范围 (xmin, xmax, ymin, ymax, zmin, zmax)
    // 供后续 UI 同步使用
    void getROIBounds(double bounds[6]);

    // 设置 ROI 范围 (供后续 UI 滑块调用)
    void setROIBounds(double bounds[6]);

signals:
    void sigROIChanged(double bounds[6]);// 当用户在 3D 窗口拖动框时，发送此信号通知 UI 更新数值


private:
    // 内部回调函数：处理 VTK 的交互事件
    static void onInteractionEvent(vtkObject* caller, unsigned long eventId, void* clientData, void* callData);

    // 执行裁剪的核心逻辑
    void doCrop();

private:
    vtkSmartPointer<vtkBoxWidget2> m_boxWidget;
    vtkSmartVolumeMapper* m_targetMapper = nullptr; // 不拥有所有权，只引用
    vtkSmartPointer<vtkCallbackCommand> m_callback;

    bool m_isInitialized = false;

};

#endif // VOLUME_ROI_MANAGER_H