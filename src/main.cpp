#include "mainwidget.h"
#include "dicomviewer_3d.h"
#include "ai_run_manager.h"
#include <QtWidgets/QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    AiRunManager::installMessageHandler();

    QString aiPlanPath;
    bool autoExit = false;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--ai-plan" && i + 1 < args.size()) {
            aiPlanPath = args[++i];
        } else if (args[i].startsWith("--ai-plan=")) {
            aiPlanPath = args[i].mid(QString("--ai-plan=").size());
        } else if (args[i] == "--auto-exit") {
            autoExit = true;
        }
    }

    dicomviewer_3d w;
    w.show();

    if (!aiPlanPath.isEmpty()) {
        QTimer::singleShot(0, [&w, aiPlanPath, autoExit]() {
            w.runAiPlanFile(aiPlanPath, autoExit);
        });
    }

    return a.exec();
}
