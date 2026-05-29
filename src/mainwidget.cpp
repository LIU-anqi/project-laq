#include "mainwidget.h"

#include <QDebug>
#include <QLineEdit>
#include <QApplication>
#include <QStyleFactory>
#include <QSpinBox>
#include <QCloseEvent>
#include <QMessageBox>

MainWidget::MainWidget(QWidget* parent) : QWidget(parent)
{
    // 设置固定的深色风格
    qApp->setStyle(QStyleFactory::create("Fusion"));

    // 主布局
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    mainLayout->setSpacing(0);

    dicomViewer_3d = new dicomviewer_3d(this); //实例化

    setupUI();
    setStyleSheet(
        "QWidget { "
        "   background-color: #0f172a; " 
        "   color: #f8fafc; "           
        "   font-family: Inter, Microsoft YaHei, SimHei; "
        "   font-size: 14px; "         
        "}"

        // 普通按钮样式
        "QPushButton { "
        "   background-color: #3b82f6; " 
        "   border: none; "
        "   color: white; "
        "   padding: 10px 15px; " // 增大内边距
        "   border-radius: 6px; " // 略微增大圆角
        "   font-size: 14px;"
        "   font-weight: 500;"
        "}"
        "QPushButton:hover { background-color: #2563eb; }" 
        "QPushButton:pressed { "
        "   background-color: #1d4ed8; "
        "   padding: 11px 14px 9px 16px; " 
        "}"

        // 输入框/旋转框
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { " // 统一处理 QComboBox
        "   background-color: #1e293b; " 
        "   border: 1px solid #475569; " // 
        "   padding: 6px; " // 增大内边距
        "   border-radius: 4px; "
        "   font-size: 14px;"
        "   color: #f8fafc; " // 确保文字颜色高对比
        "}"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { "
        "   border: 1px solid #3b82f6; " 
        "}"

        // 分组框 (QGroupBox)
        "QGroupBox { "
        "   border: 1px solid #475569; " // 边框
        "   margin-top: 15px; " // 增大顶部边距以容纳标题
        "   padding: 10px; "
        "   border-radius: 6px; "
        "   font-size: 14px;"
        "   color: #f8fafc;" // 组框内文本颜色继承
        "}"
        "QGroupBox::title { "
        "   subcontrol-origin: margin; "
        "   subcontrol-position: top left; "
        "   padding: 0 5px; "
        "   font-size: 15px;" // 组框标题略大
        "   font-weight: bold;"
        "   color: #60a5fa;" // 更亮的标题颜色 (之前是 #90cdf4)
        "}"

        // Header按钮样式 
        "#HeaderButton { "
        "   background-color: #4f46e5; " 
        "   border-radius: 6px; "
        "   padding: 10px 16px; " // 统一内边距
        "   font-size: 14px;"
        "}"
        "#HeaderButton:hover { background-color: #4338ca; }"
        "#HeaderButton:pressed { "
        "   background-color: #3730a3; "
        "   padding: 11px 15px 9px 17px; "
        "}"


        // 标签字体大小 
        "#controlPanelStack QLabel, QWidget QLabel { "
        "   font-size: 14px;" // 统一到基础大小，如果有需要特殊放大的标题，再单独设置
        "}"

        // 标题栏字体 
        "QLabel#StepTitleLabel { "
        "   font-size: 18px; "
        "   font-weight: bold; "
        "   color: #60a5fa;"
        "   margin-bottom: 10px;"
        "}"

        // 视图区域标题 
        "#centerStack QLabel { "
        "   font-size: 16px; font-weight: bold; margin-bottom: 5px; color: #60a5fa;" 
        "}"

        // 视口占位符 
        "QWidget[class=\"viewport\"] QLabel { "
        "   color: #94a3b8; font-size: 11px;" 
        "}"
    );

    // 连接打开文件按钮
    QPushButton* importMri = findChild<QPushButton*>("ImportMriButton"); 
    if (importMri && dicomViewer_3d) {
        connect(importMri, &QPushButton::clicked, dicomViewer_3d, &dicomviewer_3d::slot_openDicomFile);
    }
    else {
        qDebug() << "Connection failed: Button or Viewer is NULL.";
    }

    setWindowTitle(tr("DBS 智能规划系统"));
    resize(1200, 800);
}

MainWidget::~MainWidget()
{
}

void MainWidget::setupUI()
{
    // 标题（顶部）
    setupHeader();

    // 主要内容区（左、中、右）
    QHBoxLayout* contentLayout = new QHBoxLayout;
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // 左侧边栏（工作流步骤）
    setupWorkflowList();
    contentLayout->addWidget(workflowStepsList, 1); // 1/10th width

    // 中心区域（视口）
    setupCenterViewports();
    contentLayout->addWidget(centerStack, 9); // 7/10th width

    // 右侧边栏（控制面板）
    setupControlPanel();
    contentLayout->addWidget(controlPanelStack, 2); // 2/10th width

    // 将内容布局添加到垂直主布局，并赋予伸展系数，使其占据剩余空间
    mainLayout->addLayout(contentLayout, 1);

    // 默认初选
    workflowStepsList->setCurrentRow(0);
}

void MainWidget::setupHeader()
{
    QWidget* headerWidget = new QWidget;
    headerWidget->setFixedHeight(50);
    headerWidget->setStyleSheet("background-color: #2d3748; border-bottom: 1px solid #4a5568;");

    QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(10, 0, 10, 0);

    titleLabel = new QLabel(tr("DBS 智能规划系统"));
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #63b3ed;");

    QWidget* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QPushButton* finalizeButton = new QPushButton(tr("选择最终方案"));
    finalizeButton->setObjectName("HeaderButton");

    QPushButton* exportButton = new QPushButton(tr("导出/输出 (报告/导航)"));
    exportButton->setObjectName("HeaderButton");

    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(spacer);
    headerLayout->addWidget(finalizeButton);
    headerLayout->addWidget(exportButton);

    mainLayout->insertWidget(0, headerWidget);
}

void MainWidget::updatePatientHeader(const QString& info)
{
    // 接收患者信息，更新 titleLabel
    QString baseTitle = tr("DBS 智能规划系统");
    if (info.isEmpty()) {
        titleLabel->setText(baseTitle);
    }
    else {
        // 使用实际的患者信息替换默认的 ID
        titleLabel->setText(QString("%1 - [%2]").arg(baseTitle).arg(info));
    }
}

void MainWidget::setupWorkflowList()
{
    workflowStepsList = new QListWidget;
    workflowStepsList->setFixedWidth(250);

    // 设置列表项的样式
    workflowStepsList->setStyleSheet(
        "QListWidget {"
        "   background-color: #2d3748;"
        "   border: none;"
        "   outline: none;"
        "}"
        "QListWidget::item {"
        "   background-color: #303C4E;"
        "   color: #e2e8f0;"
        "   padding: 15px 10px;"
        "   margin: 3px 5px;"
        "   border-left: 4px solid #4299e1;"
        "   border-radius: 6px;"
        "   font-size: 16px;"
        "   font-weight: 500;"
        "}"
        "QListWidget::item:hover {"
        "   background-color: #374151;"
        "   border-left: 4px solid #4299e1;"
        "}"
        "QListWidget::item:selected {"
        "   background-color: #4a5568;"
        "   border-left: 4px solid #4299e1;"
        "   font-weight: bold;"
        "   color: #90cdf4;"
        "}"
    );

    QStringList steps = {
        tr("数据导入与配准"),
        tr("目标结构分割"),
        tr("轨迹规划"),
        tr("电极模型定位"),
        tr("激活体积优化"),
        tr("结果输出存档")
    };

    // 添加步骤项，不设置固定高度
    for (const QString& step : steps) {
        QListWidgetItem* item = new QListWidgetItem(step);
        // 不设置固定高度，让项高度自适应
        workflowStepsList->addItem(item);
    }

    // 设置列表的字体
    QFont listFont("Microsoft YaHei", 13);
    workflowStepsList->setFont(listFont);

    // 连接信号以切换右侧面板和中心视图内容
    connect(workflowStepsList, &QListWidget::currentRowChanged, this, &MainWidget::onWorkflowStepClicked);

    // 设置列表大小策略，使其可以扩展
    workflowStepsList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 设置垂直滚动条策略为始终关闭，确保所有项可见
    workflowStepsList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void MainWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // 计算每个列表项的高度
    if (workflowStepsList && workflowStepsList->count() > 0) {
        int itemCount = workflowStepsList->count();
        int listHeight = workflowStepsList->height();

        // 计算每个项的理想高度（考虑边框和边距）
        int itemHeight = (listHeight - (itemCount) * 1) / itemCount;

        // 设置最小和最大高度限制
        itemHeight = qMax(60, qMin(itemHeight, 120));

        // 为每个项设置高度
        for (int i = 0; i < itemCount; ++i) {
            QListWidgetItem* item = workflowStepsList->item(i);
            item->setSizeHint(QSize(item->sizeHint().width(), itemHeight));
        }
    }
}

void MainWidget::setupCenterViewports()
{
    centerStack = new QStackedWidget;
    centerStack->setStyleSheet("background-color: #1a202c;");

    // 为每个步骤添加中心视图
    centerStack->addWidget(createCenterDataManagement());     // Index 0
    centerStack->addWidget(createCenterSegmentation());       // Index 1
    centerStack->addWidget(createCenterTrajectoryPlanning()); // Index 2
    centerStack->addWidget(createCenterElectrodeMapping());   // Index 3
    centerStack->addWidget(createCenterSimulationOptimization()); // Index 4
    centerStack->addWidget(createCenterOutputArchive());      // Index 5
}

void MainWidget::onWorkflowStepClicked(int index)
{
    if (index >= 0 && index < controlPanelStack->count()) {
        controlPanelStack->setCurrentIndex(index);
        centerStack->setCurrentIndex(index);
    }

}

// 辅助函数：将任何 QWidget* 包装在一个带有标题和边框的容器中
QWidget* wrapViewerWithTitleAndBorder(const QString& titleText, QWidget* viewerContent)
{
    // 外部容器 QWidget，用于边框和布局
    QWidget* viewportContainer = new QWidget;

    // 应用样式：边框和背景色
    viewportContainer->setStyleSheet(
        "background-color: #2d3748;"
        "border: 1px solid #4a5568;"
        "border-radius: 8px;"
    );

    // 视口的主垂直布局
    QVBoxLayout* vbox = new QVBoxLayout(viewportContainer);
    // 移除布局边距，让内容更靠近边框
    vbox->setContentsMargins(5, 5, 5, 5);
    vbox->setSpacing(5); // 标题和内容之间的间距

    // 1. 视图名称 (在左上角)
    QLabel* title = new QLabel(titleText);
    title->setStyleSheet("color: #90cdf4; font-size: 12px;");
    vbox->addWidget(title);
    vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);

    // 2. 实际内容 (来自 dicomViewer_3d 的视图)
    // 确保 viewerContent 能够扩展填充剩余空间
    if (viewerContent) {

        // 设置圆角半径，但好像没效果
        viewerContent->setStyleSheet(  
            "QWidget {"
            "   border-radius: 10px; "
            "}");

        viewerContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        vbox->addWidget(viewerContent);
    }
    else {
        // 如果内容为空，添加一个占位符
        QLabel* placeholder = new QLabel(QObject::tr("加载失败或内容为空"));
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");
        vbox->addWidget(placeholder);
    }

    return viewportContainer;
}

// 步骤特定中心视图小部件
QWidget* MainWidget::createCenterDataManagement()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    QLabel* title = new QLabel(tr("数据导入与配准 - 多模态图像视图"));
    title->setStyleSheet("font-size: 18px; font-weight: bold; margin-bottom: 5px; color: #63b3ed;");
    layout->addWidget(title);

    QGridLayout* viewportGrid = new QGridLayout;
    viewportGrid->setSpacing(10);

    // 检查 dicomViewer 是否已实例化 & dicomviewer_3d 的所有核心功能（加载、渲染、交互）都在视图中
    if (dicomViewer_3d) {

        // 1. 轴状位 (Axial)
        // 获取带滑条的容器，然后用 MainWidget 的风格包装它
        QWidget* axialWrapper = wrapViewerWithTitleAndBorder(tr("轴状位"), dicomViewer_3d->getAxialContainer());
        viewportGrid->addWidget(axialWrapper, 0, 0);

        // 2. 冠状位 (Coronal)
        QWidget* coronalWrapper = wrapViewerWithTitleAndBorder(tr("冠状位"), dicomViewer_3d->getCoronalContainer());
        viewportGrid->addWidget(coronalWrapper, 0, 1);

        // 3. 矢状位 (Sagittal)
        QWidget* sagittalWrapper = wrapViewerWithTitleAndBorder(tr("矢状位"), dicomViewer_3d->getSagittalContainer());
        viewportGrid->addWidget(sagittalWrapper, 1, 0);

        // 4. 三维可视化 (3D) - 这个还是裸的，需要 MainWidget 帮忙包装一下
        // 依然调用 wrapViewerWithTitleAndBorder
        QWidget* volumeWrapper = wrapViewerWithTitleAndBorder(tr("三维可视化"), dicomViewer_3d->get3DWidget());
        viewportGrid->addWidget(volumeWrapper, 1, 1);

        // 为了让 VTK 视图占据整个网格空间，需要设置伸缩因子
        viewportGrid->setRowStretch(0, 1);
        viewportGrid->setRowStretch(1, 1);
        viewportGrid->setColumnStretch(0, 1);
        viewportGrid->setColumnStretch(1, 1);

    }
    else {
        auto createViewport = [](const QString& titleText, const QString& placeholderText) -> QWidget* {
            QWidget* viewport = new QWidget;
            viewport->setMinimumSize(250, 250);
            viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

            QVBoxLayout* vbox = new QVBoxLayout(viewport);
            QLabel* title = new QLabel(titleText);
            title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

            QLabel* placeholder = new QLabel(placeholderText);
            placeholder->setAlignment(Qt::AlignCenter);
            placeholder->setStyleSheet("color: #718096; font-size: 10px;");

            vbox->addWidget(title);
            vbox->addWidget(placeholder);
            vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
            vbox->setAlignment(placeholder, Qt::AlignCenter);

            return viewport;
            };

        // 分割特定的视口
        viewportGrid->addWidget(createViewport(tr("轴状位"), tr("轴状位数据")), 0, 0);
        viewportGrid->addWidget(createViewport(tr("冠状位"), tr("冠状位数据")), 0, 1);
        viewportGrid->addWidget(createViewport(tr("矢状位"), tr("矢状位数据")), 1, 0);
        viewportGrid->addWidget(createViewport(tr("三维可视化"), tr("三维空间对齐检查")), 1, 1);
    }

    layout->addLayout(viewportGrid, 1);
    return widget;
}

QWidget* MainWidget::createCenterSegmentation()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    QLabel* title = new QLabel(tr("目标结构分割 - 解剖结构可视化"));
    title->setStyleSheet("font-size: 18px; font-weight: bold; margin-bottom: 5px; color: #63b3ed;");
    layout->addWidget(title);

    QGridLayout* viewportGrid = new QGridLayout;
    viewportGrid->setSpacing(10);

    auto createViewport = [](const QString& titleText, const QString& placeholderText) -> QWidget* {
        QWidget* viewport = new QWidget;
        viewport->setMinimumSize(250, 250);
        viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

        QVBoxLayout* vbox = new QVBoxLayout(viewport);
        QLabel* title = new QLabel(titleText);
        title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

        QLabel* placeholder = new QLabel(placeholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");

        vbox->addWidget(title);
        vbox->addWidget(placeholder);
        vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
        vbox->setAlignment(placeholder, Qt::AlignCenter);

        return viewport;
        };

    // 分割特定的视口
    viewportGrid->addWidget(createViewport(tr("轴状位"), tr("轴状位数据")), 0, 0);
    viewportGrid->addWidget(createViewport(tr("冠状位"), tr("冠状位数据")), 0, 1);
    viewportGrid->addWidget(createViewport(tr("矢状位"), tr("矢状位数据")), 1, 0);
    viewportGrid->addWidget(createViewport(tr("三维可视化"), tr("三维空间对齐检查")), 1, 1);

    layout->addLayout(viewportGrid, 1);
    return widget;
}

QWidget* MainWidget::createCenterTrajectoryPlanning()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    QLabel* title = new QLabel(tr("轨迹/通道规划 - 手术路径设计"));
    title->setStyleSheet("font-size: 18px; font-weight: bold; margin-bottom: 5px; color: #63b3ed;");
    layout->addWidget(title);

    // 主网格布局
    QGridLayout* viewportGrid = new QGridLayout;
    viewportGrid->setSpacing(10);

    auto createViewport = [](const QString& titleText, const QString& placeholderText, int minWidth = 0, int minHeight = 250) -> QWidget* {
        QWidget* viewport = new QWidget;
        if (minWidth > 0) viewport->setMinimumWidth(minWidth);
        viewport->setMinimumHeight(minHeight);
        viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

        QVBoxLayout* vbox = new QVBoxLayout(viewport);
        QLabel* title = new QLabel(titleText);
        title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

        QLabel* placeholder = new QLabel(placeholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");

        vbox->addWidget(title);
        vbox->addWidget(placeholder);
        vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
        vbox->setAlignment(placeholder, Qt::AlignCenter);

        return viewport;
        };

    // 左侧视图 - 轨迹安全性评估和3D轨迹视图（垂直排列）
    viewportGrid->addWidget(createViewport(tr("局部细节"), tr("局部细节图")), 0, 0);
    viewportGrid->addWidget(createViewport(tr("3D 细节"), tr("3D 细节图")), 1, 0);

    // 右侧视图 - 多轨迹方案对比和手术模拟
    // 创建一个容器来放置两个水平排列的视图
    QWidget* rightContainer = new QWidget;
    QHBoxLayout* rightLayout = new QHBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    // 创建长条状视图
    auto createHorizontalViewport = [](const QString& titleText, const QString& placeholderText) -> QWidget* {
        QWidget* viewport = new QWidget;
        viewport->setMinimumWidth(100);  // 设置较小的宽度
        viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

        QVBoxLayout* vbox = new QVBoxLayout(viewport);
        QLabel* title = new QLabel(titleText);
        title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

        QLabel* placeholder = new QLabel(placeholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");

        vbox->addWidget(title);
        vbox->addWidget(placeholder);
        vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
        vbox->setAlignment(placeholder, Qt::AlignCenter);

        return viewport;
        };

    // 添加水平排列的长条状视图
    rightLayout->addWidget(createHorizontalViewport(tr("单针细节"), tr("单针细节图")));
    rightLayout->addWidget(createHorizontalViewport(tr("单针细节"), tr("单针细节图")));

    // 将右侧容器添加到网格布局中，占据右侧一整列
    viewportGrid->addWidget(rightContainer, 0, 1, 2, 1); // 跨两行，占一列

    layout->addLayout(viewportGrid, 1);
    return widget;
}

QWidget* MainWidget::createCenterElectrodeMapping()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    QLabel* title = new QLabel(tr("电极模型定位 - 植入位置规划"));
    title->setStyleSheet("font-size: 18px; font-weight: bold; margin-bottom: 5px; color: #63b3ed;");
    layout->addWidget(title);

    // 主网格布局
    QGridLayout* viewportGrid = new QGridLayout;
    viewportGrid->setSpacing(10);

    auto createViewport = [](const QString& titleText, const QString& placeholderText, int minWidth = 0, int minHeight = 250) -> QWidget* {
        QWidget* viewport = new QWidget;
        if (minWidth > 0) viewport->setMinimumWidth(minWidth);
        viewport->setMinimumHeight(minHeight);
        viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

        QVBoxLayout* vbox = new QVBoxLayout(viewport);
        QLabel* title = new QLabel(titleText);
        title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

        QLabel* placeholder = new QLabel(placeholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");

        vbox->addWidget(title);
        vbox->addWidget(placeholder);
        vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
        vbox->setAlignment(placeholder, Qt::AlignCenter);

        return viewport;
        };

    // 左侧视图 - 轨迹安全性评估和3D轨迹视图（垂直排列）
    viewportGrid->addWidget(createViewport(tr("局部细节"), tr("局部细节图")), 0, 0);
    viewportGrid->addWidget(createViewport(tr("3D 细节"), tr("3D 细节图")), 1, 0);

    // 右侧视图 - 多轨迹方案对比和手术模拟
    // 创建一个容器来放置两个水平排列的视图
    QWidget* rightContainer = new QWidget;
    QHBoxLayout* rightLayout = new QHBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    // 创建长条状视图
    auto createHorizontalViewport = [](const QString& titleText, const QString& placeholderText) -> QWidget* {
        QWidget* viewport = new QWidget;
        viewport->setMinimumWidth(100);  // 设置较小的宽度
        viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

        QVBoxLayout* vbox = new QVBoxLayout(viewport);
        QLabel* title = new QLabel(titleText);
        title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

        QLabel* placeholder = new QLabel(placeholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");

        vbox->addWidget(title);
        vbox->addWidget(placeholder);
        vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
        vbox->setAlignment(placeholder, Qt::AlignCenter);

        return viewport;
        };

    // 添加水平排列的长条状视图
    rightLayout->addWidget(createHorizontalViewport(tr("单针细节"), tr("单针细节图")));
    rightLayout->addWidget(createHorizontalViewport(tr("单针细节"), tr("单针细节图")));

    // 将右侧容器添加到网格布局中，占据右侧一整列
    viewportGrid->addWidget(rightContainer, 0, 1, 2, 1); // 跨两行，占一列

    layout->addLayout(viewportGrid, 1);
    return widget;
}

QWidget* MainWidget::createCenterSimulationOptimization()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    QLabel* title = new QLabel(tr("激活体积与优化 - VTA 模拟分析"));
    title->setStyleSheet("font-size: 18px; font-weight: bold; margin-bottom: 5px; color: #63b3ed;");
    layout->addWidget(title);

    // 主网格布局
    QGridLayout* viewportGrid = new QGridLayout;
    viewportGrid->setSpacing(10);

    auto createViewport = [](const QString& titleText, const QString& placeholderText, int minWidth = 0, int minHeight = 250) -> QWidget* {
        QWidget* viewport = new QWidget;
        if (minWidth > 0) viewport->setMinimumWidth(minWidth);
        viewport->setMinimumHeight(minHeight);
        viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

        QVBoxLayout* vbox = new QVBoxLayout(viewport);
        QLabel* title = new QLabel(titleText);
        title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

        QLabel* placeholder = new QLabel(placeholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");

        vbox->addWidget(title);
        vbox->addWidget(placeholder);
        vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
        vbox->setAlignment(placeholder, Qt::AlignCenter);

        return viewport;
        };

    // 左侧视图 - 轨迹安全性评估和3D轨迹视图（垂直排列）
    viewportGrid->addWidget(createViewport(tr("局部细节"), tr("局部细节图")), 0, 0);
    viewportGrid->addWidget(createViewport(tr("3D 细节"), tr("3D 细节图")), 1, 0);

    // 右侧视图 - 多轨迹方案对比和手术模拟
    // 创建一个容器来放置两个水平排列的视图
    QWidget* rightContainer = new QWidget;
    QHBoxLayout* rightLayout = new QHBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    // 创建长条状视图
    auto createHorizontalViewport = [](const QString& titleText, const QString& placeholderText) -> QWidget* {
        QWidget* viewport = new QWidget;
        viewport->setMinimumWidth(100);  // 设置较小的宽度
        viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

        QVBoxLayout* vbox = new QVBoxLayout(viewport);
        QLabel* title = new QLabel(titleText);
        title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

        QLabel* placeholder = new QLabel(placeholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");

        vbox->addWidget(title);
        vbox->addWidget(placeholder);
        vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
        vbox->setAlignment(placeholder, Qt::AlignCenter);

        return viewport;
        };

    // 添加水平排列的长条状视图
    rightLayout->addWidget(createHorizontalViewport(tr("放电区域选择"), tr("放电区域选择")));
    //    rightLayout->addWidget(createHorizontalViewport(tr("单针细节"), tr("单针细节图")));

        // 将右侧容器添加到网格布局中，占据右侧一整列
    viewportGrid->addWidget(rightContainer, 0, 1, 2, 1); // 跨两行，占一列

    layout->addLayout(viewportGrid, 1);
    return widget;
}

QWidget* MainWidget::createCenterOutputArchive()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    QLabel* title = new QLabel(tr("结果输出与存档 - 最终方案展示"));
    title->setStyleSheet("font-size: 18px; font-weight: bold; margin-bottom: 5px; color: #63b3ed;");
    layout->addWidget(title);

    // 主网格布局
    QGridLayout* viewportGrid = new QGridLayout;
    viewportGrid->setSpacing(10);

    auto createViewport = [](const QString& titleText, const QString& placeholderText, int minWidth = 0, int minHeight = 250) -> QWidget* {
        QWidget* viewport = new QWidget;
        if (minWidth > 0) viewport->setMinimumWidth(minWidth);
        viewport->setMinimumHeight(minHeight);
        viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

        QVBoxLayout* vbox = new QVBoxLayout(viewport);
        QLabel* title = new QLabel(titleText);
        title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

        QLabel* placeholder = new QLabel(placeholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");

        vbox->addWidget(title);
        vbox->addWidget(placeholder);
        vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
        vbox->setAlignment(placeholder, Qt::AlignCenter);

        return viewport;
        };

    // 左侧视图 - 轨迹安全性评估和3D轨迹视图（垂直排列）
    viewportGrid->addWidget(createViewport(tr("局部细节"), tr("局部细节图")), 0, 0);
    viewportGrid->addWidget(createViewport(tr("3D 细节"), tr("3D 细节图")), 1, 0);

    // 右侧视图 - 多轨迹方案对比和手术模拟
    // 创建一个容器来放置两个水平排列的视图
    QWidget* rightContainer = new QWidget;
    QHBoxLayout* rightLayout = new QHBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    // 创建长条状视图
    auto createHorizontalViewport = [](const QString& titleText, const QString& placeholderText) -> QWidget* {
        QWidget* viewport = new QWidget;
        viewport->setMinimumWidth(100);  // 设置较小的宽度
        viewport->setStyleSheet("background-color: #2d3748; border: 1px solid #4a5568; border-radius: 8px;");

        QVBoxLayout* vbox = new QVBoxLayout(viewport);
        QLabel* title = new QLabel(titleText);
        title->setStyleSheet("color: #90cdf4; font-size: 12px; margin-bottom: 5px;");

        QLabel* placeholder = new QLabel(placeholderText);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #718096; font-size: 10px;");

        vbox->addWidget(title);
        vbox->addWidget(placeholder);
        vbox->setAlignment(title, Qt::AlignTop | Qt::AlignLeft);
        vbox->setAlignment(placeholder, Qt::AlignCenter);

        return viewport;
        };

    // 添加水平排列的长条状视图
    rightLayout->addWidget(createHorizontalViewport(tr("单针细节"), tr("单针细节图")));
    rightLayout->addWidget(createHorizontalViewport(tr("单针细节"), tr("单针细节图")));

    // 将右侧容器添加到网格布局中，占据右侧一整列
    viewportGrid->addWidget(rightContainer, 0, 1, 2, 1); // 跨两行，占一列

    layout->addLayout(viewportGrid, 1);
    return widget;
}

void MainWidget::setupControlPanel()
{
    controlPanelStack = new QStackedWidget;
    controlPanelStack->setFixedWidth(300);
    controlPanelStack->setStyleSheet("background-color: #2d3748; border-left: 1px solid #4a5568;");

    // 为每个步骤添加小部件
    controlPanelStack->addWidget(createStepDataManagement()); // Index 0
    controlPanelStack->addWidget(createStepSegmentation()); // Index 1
    controlPanelStack->addWidget(createStepTrajectoryPlanning()); // Index 2
    controlPanelStack->addWidget(createStepElectrodeMapping()); // Index 3
    controlPanelStack->addWidget(createStepSimulationOptimization()); // Index 4
    controlPanelStack->addWidget(createStepOutputArchive()); // Index 5
}

// 步骤特定控制面板小部件
QWidget* MainWidget::createStepDataManagement()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(15);

    QLabel* title = new QLabel(tr("当前：数据管理"));
    title->setObjectName("StepTitleLabel");
    layout->addWidget(title);

    QPushButton* importMri = new QPushButton(tr("导入 MRI/CT 图像"));
    importMri->setStyleSheet("font-size: 14px;margin-bottom: 10px;");
    importMri->setObjectName("ImportMriButton");
    layout->addWidget(importMri);

    QPushButton* importAtlas = new QPushButton(tr("导入 脑图谱数据"));
    importAtlas->setObjectName("QPushButton");
    layout->addWidget(importAtlas);

    QGroupBox* regBox = new QGroupBox(tr("图像配准"));
    //regBox->setStyleSheet("font-weight: bold; margin-bottom: 10px;");
    QVBoxLayout* regLayout = new QVBoxLayout(regBox);

    QLabel* alignLabel = new QLabel(tr("对齐模态："));
    //alignLabel->setStyleSheet("font-size: 14px;margin-bottom: 10px;");
    QComboBox* alignSelect = new QComboBox;
    //alignSelect->setStyleSheet("font-size: 16px;");
    alignSelect->addItem(tr("MRI -> CT"));
    alignSelect->addItem(tr("MRI -> Atlas"));

    QPushButton* runReg = new QPushButton(tr("运行配准"));
    runReg->setStyleSheet("font-size: 14px;margin-bottom: 10px;");

    regLayout->addWidget(alignLabel);
    regLayout->addWidget(alignSelect);
    regLayout->addWidget(runReg);

    layout->addWidget(regBox);
    layout->addStretch();
    return widget;
}

QWidget* MainWidget::createStepSegmentation()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(15, 15, 15, 15);

    QLabel* title = new QLabel(tr("当前：目标结构分割"));
    title->setStyleSheet("font-size: 14px; font-weight: bold; color: #63b3ed; margin-bottom: 10px;");
    layout->addWidget(title);

    QPushButton* autoSeg = new QPushButton(tr("自动分割"));
    autoSeg->setObjectName("HeaderButton");
    layout->addWidget(autoSeg);

    QGroupBox* refineBox = new QGroupBox(tr("微调工具"));
    refineBox->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");
    QVBoxLayout* refineLayout = new QVBoxLayout(refineBox);

    //    QPushButton *AddTarget = new QPushButton(tr("添加目标"));

    QPushButton* Correctiontarget = new QPushButton(tr("分割修正"));
    Correctiontarget->setStyleSheet("color: #48bb78; margin-top: 12px;");


    QPushButton* fiberStatus = new QPushButton(tr("纤维束重建"));
    fiberStatus->setStyleSheet("color: #48bb78; margin-top: 12px;");

    //    refineLayout->addWidget(AddTarget);
    refineLayout->addWidget(Correctiontarget);
    refineLayout->addWidget(fiberStatus);

    layout->addWidget(refineBox);
    layout->addStretch();
    return widget;
}

QWidget* MainWidget::createStepTrajectoryPlanning()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(15, 15, 15, 15);

    QLabel* title = new QLabel(tr("当前：轨迹规划"));
    title->setStyleSheet("font-size: 14px; font-weight: bold; color: #63b3ed; margin-bottom: 10px;");
    layout->addWidget(title);

    // 刺激参数输入
    QGroupBox* paramBox = new QGroupBox(tr("选择入口点"));
    paramBox->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");
    QGridLayout* paramLayout = new QGridLayout(paramBox);
    paramLayout->setSpacing(8);


    QPushButton* targetPointLabel1 = new QPushButton("靶点", this);
    QLineEdit* targetPointLineEdit1 = new QLineEdit("0,0,0", this);
    targetPointLineEdit1->setStyleSheet("background-color: #2d3748; border-left: 1px solid #4a5568;");
    targetPointLineEdit1->setFixedWidth(60);
    //    QPushButton *deleteTargetPointButton1 = new QPushButton("删除", this);

    QPushButton* entryPointLabel1 = new QPushButton("进针点", this);
    QLineEdit* entryPointLineEdit1 = new QLineEdit("0,0,0", this);
    entryPointLineEdit1->setStyleSheet("background-color: #2d3748; border-left: 1px solid #4a5568;");
    entryPointLineEdit1->setFixedWidth(60);
    QPushButton* confirmEntryPointButton1 = new QPushButton("确认", this);

    paramLayout->addWidget(targetPointLabel1, 1, 0);
    paramLayout->addWidget(targetPointLineEdit1, 1, 1);
    paramLayout->addWidget(entryPointLabel1, 2, 0);
    paramLayout->addWidget(entryPointLineEdit1, 2, 1);
    //    paramLayout->addWidget(deleteTargetPointButton1, 3, 0, 1, 2);
    paramLayout->addWidget(confirmEntryPointButton1, 4, 0, 1, 2);
    layout->addWidget(paramBox);

    QPushButton* autoPath = new QPushButton(tr("自动路径寻优"));
    autoPath->setStyleSheet("color: #48bb78; margin-top: 12px;");
    layout->addWidget(autoPath);

    layout->addStretch();
    return widget;
}

QWidget* MainWidget::createStepElectrodeMapping()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(15, 15, 15, 15);

    QLabel* title = new QLabel(tr("当前：电极定位"));
    title->setStyleSheet("font-size: 14px; font-weight: bold; color: #63b3ed; margin-bottom: 10px;");
    layout->addWidget(title);

    QLabel* modelLabel = new QLabel(tr("选择电极型号："));
    modelLabel->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");
    QComboBox* modelSelect = new QComboBox;
    modelSelect->setStyleSheet("font-size: 16px;");
    modelSelect->addItem(tr("Boston Vercise PC"));
    modelSelect->addItem(tr("Medtronic Activa PC"));

    QPushButton* fineTune = new QPushButton(tr("微调位置/旋转"));
    fineTune->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");

    layout->addWidget(modelLabel);
    layout->addWidget(modelSelect);
    layout->addWidget(fineTune);

    layout->addStretch();
    return widget;
}

QWidget* MainWidget::createStepSimulationOptimization()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(15, 15, 15, 15);

    QLabel* title = new QLabel(tr("当前：激活体积模拟"));
    title->setStyleSheet("font-size: 14px; font-weight: bold; color: #63b3ed; margin-bottom: 10px;");
    layout->addWidget(title);

    // 刺激参数输入
    QGroupBox* paramBox = new QGroupBox(tr("电刺激参数"));
    paramBox->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");
    QGridLayout* paramLayout = new QGridLayout(paramBox);
    paramLayout->setSpacing(8);

    QLabel* pwLabel = new QLabel(tr("脉冲宽度 (μs):"));
    QSpinBox* pwInput = new QSpinBox;
    pwInput->setRange(20, 450);
    pwInput->setValue(60);

    QLabel* freqLabel = new QLabel(tr("频率 (Hz):"));
    QSpinBox* freqInput = new QSpinBox;
    freqInput->setRange(100, 200);
    freqInput->setValue(130);

    QLabel* ampLabel = new QLabel(tr("电流 (mA):"));
    QDoubleSpinBox* ampInput = new QDoubleSpinBox;
    ampInput->setRange(0.5, 10.0);
    ampInput->setSingleStep(0.1);
    ampInput->setValue(2.5);

    QLabel* StrLabel = new QLabel(tr("步长 :"));
    QSpinBox* StrInput = new QSpinBox;
    StrInput->setRange(1, 5);
    StrInput->setValue(2);

    paramLayout->addWidget(pwLabel, 0, 0);
    paramLayout->addWidget(pwInput, 0, 1);
    paramLayout->addWidget(freqLabel, 1, 0);
    paramLayout->addWidget(freqInput, 1, 1);
    paramLayout->addWidget(ampLabel, 2, 0);
    paramLayout->addWidget(ampInput, 2, 1);
    paramLayout->addWidget(StrLabel, 3, 0);
    paramLayout->addWidget(StrInput, 3, 1);


    QPushButton* runSim = new QPushButton(tr("运行激活体积模拟"));
    runSim->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");
    paramLayout->addWidget(runSim, 4, 0, 1, 2);

    layout->addWidget(paramBox);

    // 方案对比与优化建议
    QLabel* optTitle = new QLabel(tr("方案对比与优化"));
    optTitle->setStyleSheet("font-weight: bold; color: #f6e05e; margin-top: 10px; font-size: 14px;");
    layout->addWidget(optTitle);

    QLabel* schemeA = new QLabel(tr("方案 A (原始)"));
    schemeA->setStyleSheet("background-color: #4a5568; padding: 5px; border-radius: 4px; color: #fc8181; font-size: 14px;");
    layout->addWidget(schemeA);

    QLabel* schemeB = new QLabel(tr("方案 B (优化)"));
    schemeB->setStyleSheet("background-color: #4a5568; padding: 5px; border-radius: 4px; color: #68d391; font-size: 14px;");
    layout->addWidget(schemeB);

    QPushButton* getOptim = new QPushButton(tr("获取优化建议"));
    getOptim->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");
    layout->addWidget(getOptim);

    layout->addStretch();
    return widget;
}

QWidget* MainWidget::createStepOutputArchive()
{
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(15, 15, 15, 15);

    QLabel* title = new QLabel(tr("当前：结果输出"));
    title->setStyleSheet("font-size: 14px; font-weight: bold; color: #63b3ed; margin-bottom: 10px;");
    layout->addWidget(title);

    QPushButton* exportPdf = new QPushButton(tr("生成 PDF 报告"));
    exportPdf->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");
    layout->addWidget(exportPdf);

    QPushButton* exportNav = new QPushButton(tr("导出手术导航文件"));
    exportNav->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");
    layout->addWidget(exportNav);

    QPushButton* archive = new QPushButton(tr("存档病例"));
    archive->setStyleSheet("font-size: 14px; font-weight: bold; margin-bottom: 10px;");
    layout->addWidget(archive);

    layout->addStretch();
    return widget;
}
