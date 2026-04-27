#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "ModelPart.h"
#include "ModelPartList.h"

#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

signals:
  void statusUpdateMessage(const QString &message, int timeout);

public slots:
  void handleButton();
  void handleButton2();
  void handleTreeClicked();
  void on_actionOpen_File_triggered();
  void on_actionItem_Options_triggered();
  void on_actionAdd_Item_triggered();

private:
  Ui::MainWindow *ui;
  ModelPartList *partList;

  vtkSmartPointer<vtkRenderer> renderer;
  vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;

  void updateRender();
  void updateRenderFromTree(const QModelIndex &index);
};
#endif // MAINWINDOW_H
