#ifndef MAINWIDGET_H
#define MAINWIDGET_H
#pragma execution_character_set("utf-8")

#include "dicomviewer_3d.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QListWidgetItem>

class MainWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MainWidget(QWidget* parent = nullptr);
    ~MainWidget();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onWorkflowStepClicked(int index);
    void updatePatientHeader(const QString& info);

private:
    void setupUI();
    void setupHeader();
    void setupWorkflowList();
    void setupCenterViewports();
    void setupControlPanel();
    QLabel* titleLabel;
    dicomviewer_3d* dicomViewer_3d; // 用于存储 dicomviewer_3d 实例

    // 中心视图创建函数
    QWidget* createCenterDataManagement();
    QWidget* createCenterSegmentation();
    QWidget* createCenterTrajectoryPlanning();
    QWidget* createCenterElectrodeMapping();
    QWidget* createCenterSimulationOptimization();
    QWidget* createCenterOutputArchive();

    // 控制面板创建函数
    QWidget* createStepDataManagement();
    QWidget* createStepSegmentation();
    QWidget* createStepTrajectoryPlanning();
    QWidget* createStepElectrodeMapping();
    QWidget* createStepSimulationOptimization();
    QWidget* createStepOutputArchive();

    // 成员变量
    QVBoxLayout* mainLayout;
    QPushButton* importMri;

    // 左侧工作流列表
    QListWidget* workflowStepsList;

    // 中心视图堆叠
    QStackedWidget* centerStack;

    // 右侧控制面板堆叠
    QStackedWidget* controlPanelStack;
};

#endif // MAINWIDGET_H
