/**
 * @file mainwindow.h
 * @brief Main window controller for the Group 13 VR CAD Viewer.
 *
 * This class manages the main user interface, including loading STL files,
 * displaying model parts in the tree view, applying visual filters, handling
 * screenshots, and syncing the desktop scene with the VR render thread.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QSet>
#include <QTimer>

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

/**
 * @class MainWindow
 * @brief Main application window for the CAD and VR viewer.
 *
 * MainWindow connects the Qt interface to the VTK rendering scene. It handles
 * file loading, part selection, colour and visibility changes, clip/shrink
 * filters, exploded view controls, opacity controls, screenshots, and VR
 * start/stop/sync actions.
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  /**
   * @brief Constructs the main window and initialises the UI.
   * @param parent Parent widget.
   */
  MainWindow(QWidget *parent = nullptr);

  /**
   * @brief Cleans up UI and stops any running VR thread.
   */
  ~MainWindow();

protected:
  /** Catch the user closing the window so we can shut down the VR
   *  thread cleanly (otherwise SteamVR can hold the compositor and
   *  refuse new sessions until restarted). */
  void closeEvent(QCloseEvent *event) override;

  /** Event filter installed on the vtkWidget so we can detect a
   *  click without interfering with the trackball interactor's
   *  rotate/pan/zoom. We track the press position and only treat
   *  a release as a "pick" if the cursor barely moved - drags fall
   *  through to VTK as usual. */
  bool eventFilter(QObject *watched, QEvent *event) override;

signals:

  /**
   * @brief Updates the status bar with a message.
   * @param message Text to display.
   * @param timeout Duration in milliseconds.
   */
  void statusUpdateMessage(const QString &message, int timeout);

public slots:
  /**
   * @brief Handles selection changes in the part tree.
   *
   * Updates UI controls to reflect the selected model.
   */
  void handleTreeClicked();

  /**
   * @brief Opens the item options dialog for the selected part.
   */
  void openItemOptions();

  /** @name File menu / toolbar actions */
  ///@{
  void on_actionOpen_File_triggered();
  void on_actionOpen_Folder_triggered();
  void on_actionSave_triggered();
  void on_actionPrint_triggered();
  void on_actionExit_triggered();
  ///@}

  /** @name Edit menu / toolbar */
  ///@{
  void on_actionItem_Options_triggered();
  void on_actionAdd_Item_triggered();
  void on_actionRemove_Item_triggered();
  void on_actionChange_Colour_triggered();
  void on_actionToggle_Visibility_triggered();

  /**
   * @brief Captures the current VTK render window and saves it as a PNG.
   */
  void onScreenshotClicked();
  ///@}


  /** @name View menu */
  ///@{
  void on_actionReset_View_triggered();
  void on_actionChange_Background_triggered();

  /**
   * @brief Toggles the selected model's shrink filter to a preset value.
   */
  void on_actionToggle_Shrink_triggered();

  /**
   * @brief Toggles the selected model's clip filter to a preset value.
   */
  void on_actionToggle_Clip_triggered();
  ///@}

  /** @name VR menu / toolbar */
  ///@{
  void on_actionStart_VR_triggered();
  void on_actionStop_VR_triggered();
  void on_actionSync_VR_triggered();
  void on_actionToggle_VR_Rotation_triggered();

  /** @name Help */
  ///@{
  void on_actionAbout_triggered();
  ///@}

  /** @name Right-panel widgets */
  ///@{
  void onResetViewClicked();
  void onChangeColourClicked();
  void onBackgroundColourClicked();
  ///@}

  /** @name Continuous slider filter handlers */
  ///@{
  void onLightIntensityChanged(int value);
  /* Continuous-slider filter handlers, merged from the main branch. */
  void onShrinkSliderChanged(int value);
  void onClipSliderChanged(int value);
  /* Animated one-click explode. The button is checkable: clicking
   * toggles between "fly the parts apart" (target = 1.0) and "bring
   * them back" (target = 0.0). A QTimer drives the transition over
   * ~1s of wall time; onExplodeAnimTick() advances progress and
   * pushes the new offset to every part. The mode dropdown picks
   * which direction (Spherical / X / Y / Z). */
  void onExplodeButtonClicked(bool checked);
  void onExplodeModeChanged(int index);
  void onExplodeAnimTick();

  /* X-ray (global transparency) handlers. Slider drives opacity for
   * every visible part; the Solid button snaps it back to 100% so
   * the user can flip in/out of see-through mode in one click. */
  void onOpacitySliderChanged(int value);
  void onOpacitySolidClicked();
  void onVisibilityToggled(bool checked);
  void onSyncVRClicked();
  void onToggleVRRotation();

private:
  Ui::MainWindow *ui;
  ModelPartList *partList;

  bool darkMode = false;
  QString originalStyleSheet;
  void applyTheme(bool enabled);

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
  /* Diff-based scene sync: walk the tree to compute the desired actor
   * set, walk the renderer's current props, then add only what's new
   * and remove only what's stale. Cheap to call after any tree edit
   * (open file/folder, remove, edit) - O(N) instead of the old version
   * that did RemoveAllViewProps + re-add of every actor on every call.
   * At 4000+ parts the old path was the dominant cost of any tree
   * mutation; this one only touches the deltas. */
  void updateRender();
  void collectTreeActors(const QModelIndex &index,
                         QSet<vtkActor *> &out) const;

  /* Walk the tree and push a fresh actor for every visible part to the
   * VR thread. Used by Start VR and the manual Sync button. Returns
   * the number of actors actually queued so callers can sanity-check
   * that the tree is non-empty before starting VR. */
  int pushTreeActorsToVR(const QModelIndex &index);

  /* Auto-sync is wired into every GUI change that affects what should be
   * visible in VR (colour pick, visibility toggle, slider release,
   * explode end, opacity reset). Each call resets a 200ms debounce;
   * once the user pauses for that long, doVRSync() runs once. Without
   * the debounce, dragging through 50 slider values would rebuild the
   * VR scene 50 times in a second and tank headset framerate.
   *
   * doVRSync() is the immediate path - it's what the manual Sync button
   * and the debounce timer's timeout both call. It does the actual
   * CLEAR_SCENE + pushTreeActorsToVR pair. */
  void scheduleVRSync();
  void doVRSync();
  QTimer *m_vrSyncDebounce = nullptr;

  /* "Apply to all" recursion helpers. Each walks the full tree and
   * stamps the same colour / shrink / clip / light state onto every
   * part that has an STL loaded. Used when the right-panel "Apply to
   * all" check boxes are ticked, so a single slider drag or colour
   * pick affects the whole assembly instead of just the current row. */
  void applyColourToTree(const QModelIndex &index, unsigned char R,
                         unsigned char G, unsigned char B);
  void applyShrinkFactorToTree(const QModelIndex &index, double factor);
  void applyClipSliderToTree(const QModelIndex &index, int sliderValue);
  void applyLightFactorToTree(const QModelIndex &index, double factor);

  /* Toggle the visibility of a tree row AND every descendant. Used
   * when the user (un)checks a folder row in the tree - we want all
   * the STL parts inside it to follow the parent's state, otherwise
   * unchecking a folder does nothing (the folder itself has no
   * actor). The flat onVisibilityToggled() path delegates here so
   * the menu, context menu and right-panel checkbox all behave
   * the same way. */
  void setVisibilityRecursive(const QModelIndex &index, bool visible);

  /* Walk the tree and stamp the same opacity onto every part with
   * an STL loaded. Called both when the slider moves AND right
   * after Open File / Open Folder loads new parts, so freshly
   * loaded geometry inherits the current X-ray level instead of
   * showing up solid in the middle of a transparent scene. */
  void applyOpacityToTree(const QModelIndex &index, double opacity);

  /* Pick the front-most actor under the given (Qt-space) widget
   * coordinate, find which ModelPart owns it, and select that row
   * in the tree. handleTreeClicked() then syncs the right panel
   * (slider states, visibility checkbox, status bar) just like a
   * tree click does. Returns true if something was hit. */
  bool pickPartAt(const QPoint &pos);

  /* Recursively search the tree for the ModelPart whose actor
   * pointer matches target. Used by pickPartAt to translate the
   * VTK-world hit back into a tree QModelIndex. */
  QModelIndex findIndexForActor(const QModelIndex &index,
                                vtkActor *target) const;

  /* Click-vs-drag tracking for the vtkWidget event filter. */
  QPoint m_pressPos;
  bool m_pressTracked = false;

  /* Shift+LeftDrag in the 3D view translates the picked part along the
   * screen plane. State for the drag lives on MainWindow because the
   * eventFilter / start / update / end calls are split across mouse
   * press / move / release events. The translation is stored as the
   * part's explode offset so it propagates into VR via getNewActor()
   * (and so a subsequent explode animation will reset the manual
   * placement, which matches user expectation - explode wins). */
  bool startPartDrag(const QPoint &pos);
  void updatePartDrag(const QPoint &pos);
  bool m_partDragging = false;
  ModelPart *m_dragPart = nullptr;
  QPoint m_dragStartScreen;
  double m_dragStartOffset[3] = {0.0, 0.0, 0.0};
  double m_dragDepth = 0.0;

  /* Exploded view: for each visible part, push it away from the
   * global model centre. progress is 0..1 (0 = collapsed, 1 = fully
   * exploded). mode is one of ExplodeMode (see below); modes 1/2/3
   * confine the spread to a single axis, mode 0 spreads radially.
   * Recomputed from the cached per-part originalBounds so the
   * directions don't drift as parts get translated by previous
   * explode passes. */
  enum ExplodeMode {
    ExplodeSpherical = 0,
    ExplodeX = 1,
    ExplodeY = 2,
    ExplodeZ = 3,
  };
  void applyExplosion(double progress, int mode);

  /* Helpers for applyExplosion: union the originalBounds of every
   * visible part in the tree, and walk-and-translate. */
  void collectExplodeBounds(const QModelIndex &index, double bounds[6],
                            bool &any) const;
  void translatePartsForExplosion(const QModelIndex &index,
                                  const double globalCentre[3],
                                  double diag, double progress, int mode);

  /* Timer + animation state for the one-click animated explode. */
  QTimer *explodeTimer = nullptr;
  QElapsedTimer explodeClock;
  double m_explodeStart = 0.0;   /* progress at the start of this run */
  double m_explodeTarget = 0.0;  /* progress we're heading towards */
  double m_explodeProgress = 0.0; /* current progress (last tick) */
  int m_explodeMode = ExplodeSpherical;
  static constexpr int kExplodeDurationMs = 1000;

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
