#include "ui/mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Paint.nux");
    app.setApplicationVersion("0.2.0");
    app.setWindowIcon(QIcon(":/paintnux.svg"));

    paintnux::MainWindow window;
    window.show();

    if (argc > 1)
        window.openFile(QString::fromLocal8Bit(argv[1]));

    return app.exec();
}
