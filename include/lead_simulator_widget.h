#ifndef LEAD_SIMULATOR_WIDGET_H
#define LEAD_SIMULATOR_WIDGET_H
#pragma execution_character_set("utf-8")

#include <QWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include "QVTKOpenGLNativeWidget.h"
#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkInteractorStyleTrackballCamera.h>

class DBSLeadModel; // 前向声明

class LeadSimulatorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LeadSimulatorWidget(QWidget* parent = nullptr);
    ~LeadSimulatorWidget();

    DBSLeadModel* getLeadModel() const { return m_simLead; }

    // 允许外部同步参数（比如从文件加载）
    void setParameters(int typeIndex, double depth, double amp, int pw, int freq, bool vta, const int polarities[4]);
    void setPhaseFPresetIndex(int index);

    double getDepth() const { return spnDepth->value(); }
    void setDepth(double v) { spnDepth->setValue(v); }

    // 【关键】Qt 安全的渲染请求：通过 update() 走 Qt 事件循环，避免 GL 上下文抢占
    void requestRender() { if (m_vtkWidget) m_vtkWidget->update(); }

public slots:
    void onContactButtonClicked(int index);

signals:
    // 当在模拟器中调整了参数，向主窗口发射信号以同步主模型
    void sigLeadTypeChanged(int typeIndex);
    void sigAmplitudeChanged(double mA);
    void sigPulseWidthChanged(int us);
    void sigFrequencyChanged(int hz);
    void sigDepthChanged(double mm);
    void sigShowVTA(bool show);
    void sigVTADisplayPresetChanged(int presetIndex);
    void sigContactPolarityChanged(int contactIndex, int polarity);
    void sigPhaseFPresetRequested(int presetIndex);
    void sigComputeRealVTA();  // 请求主窗口执行真实 VTA 计算

protected:
    // 【修复黑屏】鼠标进入模拟器区域时，强制刷新 GL 上下文
    void enterEvent(QEvent* event) override {
        QWidget::enterEvent(event);
        requestRender();
    }

private:
    // VTK 核心
    QVTKOpenGLNativeWidget* m_vtkWidget;
    vtkSmartPointer<vtkRenderer> m_renderer;
    
    // 独立的电极模型实例（永远垂直居中）
    DBSLeadModel* m_simLead;

    // UI 控件
    QComboBox* cmbPhaseFPreset;
    QComboBox* cmbVTADisplayPreset;
    QComboBox* cmbLeadType;
    QDoubleSpinBox* spnAmplitude;
    QSpinBox* spnPulseWidth;
    QSpinBox* spnFrequency;
    QDoubleSpinBox* spnDepth;
    QCheckBox* chkShowVTA;
    QPushButton* btnContact[4];
    QPushButton* btnComputeVTA;

    int m_contactPolarity[4] = {0, 0, 0, 0};

    void setupUi();
    void setupVTK();
    void updateContactButtonUI(int index, int polarity);
};

#endif // LEAD_SIMULATOR_WIDGET_H
