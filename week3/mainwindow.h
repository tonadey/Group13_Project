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
  void handleTreeClicked();
  void openItemOptions();

  /* File menu / toolbar */
  void on_actionOpen_File_triggered();
  void on_actionOpen_Folder_triggered();
  void on_actionSave_triggered();
  void on_actionPrint_triggered();
  void on_actionExit_triggered();

  /* Edit menu / toolbar */
  void on_actionItem_Options_triggered();
  void on_actionAdd_Item_triggered();
  void on_actionRemove_Item_triggered();

  /* View menu */
  void on_actionReset_View_triggered();
  void on_actionChange_Background_triggered();
  void on_actionToggle_Shrink_triggered();
  void on_actionToggle_Clip_triggered();

  /* VR menu / toolbar */
  void on_actionStart_VR_triggered();
  void on_actionStop_VR_triggered();

  /* Help */
  void on_actionAbout_triggered();

  /* Right-panel widgets */
  void onResetViewClicked();
  void onChangeColourClicked();
  void onBackgroundColourClicked();
  void onLightIntensityChanged(int value);
  void onShrinkFilterToggled(bool checked);
  void onClipFilterToggled(bool checked);
  void onVisibilityToggled(bool checked);
  void onSyncVRClicked();

private:
  Ui::MainWindow *ui;
  ModelPartList *partList;

  vtkSmartPointer<vtkRenderer> renderer;
  vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;

  void updateRender();
  void updateRenderFromTree(const QModelIndex &index);
};
#endif // MAINWINDOW_H
