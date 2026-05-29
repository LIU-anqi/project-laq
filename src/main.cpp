#include "mainwidget.h"
#include "dicomviewer_3d.h"
#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    dicomviewer_3d w;
    w.show();
    return a.exec();
}
