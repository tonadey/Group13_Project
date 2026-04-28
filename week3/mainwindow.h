#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "ModelPart.h"
#include "ModelPartList.h"
#include "VRRenderThread.h"

#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLight.h>
#include <vtkOrientationMarkerWidget.h>
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

protected:
  /** Catch the user closing the window so we can shut down the VR
   *  thread cleanly (otherwise SteamVR can hold the compositor and
   *  refuse new sessions until restarted). */
  void closeEvent(QCloseEvent *event) override;

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
  /* Continuous-slider filter handlers, merged from the main branch. */
  void onShrinkSliderChanged(int value);
  void onClipSliderChanged(int value);
  void onVisibilityToggled(bool checked);
  void onSyncVRClicked();
  void onToggleVRRotation();

private:
  Ui::MainWindow *ui;
  ModelPartList *partList;

  vtkSmartPointer<vtkRenderer> renderer;
  vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;

  /* Headlight that follows the camera. Its intensity is driven by the
   * Light Intensity slider in the right-hand panel. */
  vtkSmartPointer<vtkLight> sceneLight;

  /* Corner axes gizmo so the user always knows which way is X / Y / Z. */
  vtkSmartPointer<vtkOrientationMarkerWidget> orientationWidget;

  /* VR render thread (nullptr when VR is not running). */
  VRRenderThread *vrThread;

  /* Toggles ROTATE_Y on/off while VR is running. The "R" keyboard
   * shortcut and View > Toggle VR Rotation menu both flip this. */
  bool vrRotating = false;

  /* Where File->Open / Open Folder dialogs land. Initialised to the
   * Models/ folder bundled next to the exe (so a USB-stick deployment
   * "just works"), then updated to the last directory the user picked. */
  QString lastBrowsedDir;

  void setupLighting();
  void updateRender();
  void updateRenderFromTree(const QModelIndex &index);

  /* Walk the tree and push a fresh actor for every visible part to the
   * VR thread. Used by Start VR and the manual Sync button. Returns
   * the number of actors actually queued so callers can sanity-check
   * that the tree is non-empty before starting VR. */
  int pushTreeActorsToVR(const QModelIndex &index);

  /* Sum triangles across visible parts in the tree. Used pre-flight
   * before launching VR so we can warn on overly heavy scenes. */
  vtkIdType countVisibleTriangles(const QModelIndex &index) const;

  /* Reflect the current VR state in the window title so it's obvious
   * during a demo whether the headset session is live. */
  void refreshWindowTitle();

  /* Build a copy-pasteable diagnostic of the GUI tree + the VR thread's
   * actor list. Used both by Start VR (when the push count is 0) and by
   * the manual Ctrl+Shift+D shortcut. */
  QString buildVRDiagnostic(const QString &reason) const;
  void showVRDiagnosticDialog(const QString &reason);

  /* Helpers for the diagnostic - count parts in the tree by category. */
  struct TreeStats {
    int totalParts = 0;
    int withStl = 0;
    int visibleWithStl = 0;
    int withActor = 0;
    int withReader = 0;
    QStringList partLines;
  };
  void collectTreeStats(const QModelIndex &index, int depth,
                        TreeStats &out) const;
};
#endif // MAINWINDOW_H
