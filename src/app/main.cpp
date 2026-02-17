#include "ui/mainwindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Paint.nux");
    app.setApplicationVersion("0.1.0");

    paintnux::MainWindow window;
    window.show();

    return app.exec();
}
