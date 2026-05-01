#include "CrashReporter.h"
#include "mainwindow.h"
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#include <QFont>

#include <vtkFileOutputWindow.h>
#include <vtkOutputWindow.h>
#include <vtkSmartPointer.h>

int main(int argc, char *argv[]) {
  /* Install crash reporter first so anything that goes wrong during VTK /
   * Qt setup is captured in the log and shown to the user. */
  CrashReporter::install();

  // Set up VTK OpenGL surface format
  QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

  QApplication a(argc, argv);

  // VTK's OpenVR module loads vtk_openvr_*.json relative to the current
  // working directory. If the exe is launched from a shortcut, cwd may not
  // be the exe folder, so SetActionManifestPath fails and VRInput() returns
  // null. Pin cwd to the exe folder so the bindings are always found.
  QDir::setCurrent(QCoreApplication::applicationDirPath());

  // VTK writes errors to vtk_log.txt in the current working directory by
  // default, which fails on a read-only USB stick and pops a system-modal
  // error box mid-demo. Redirect to %TEMP% so it never bothers the user.
  {
    QString logPath =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
        QStringLiteral("/ws6_vtk_log.txt");
    vtkSmartPointer<vtkFileOutputWindow> fileOut =
        vtkSmartPointer<vtkFileOutputWindow>::New();
    fileOut->SetFileName(logPath.toUtf8().constData());
    fileOut->SetFlush(true);
    vtkOutputWindow::SetInstance(fileOut);
  }

  MainWindow w;
  w.show();
  return a.exec();
}
