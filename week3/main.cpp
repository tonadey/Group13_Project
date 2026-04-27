#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>

int main(int argc, char *argv[]) {
  // Set up VTK OpenGL surface format
  QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

  QApplication a(argc, argv);
  MainWindow w;
  w.show();
  return a.exec();
}
