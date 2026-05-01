/**
 * @file mainwindow.cpp
 * @brief Implements the main Qt window, VTK desktop renderer, and VR controls.
 */

#include "mainwindow.h"
#include "OptionDialog.h"
#include "ui_mainwindow.h"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QScreen>
#include <QListView>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSet>
#include <QStyle>
#include <QTreeView>
#include <QVBoxLayout>
#include <functional>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkActorCollection.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCaptionActor2D.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLight.h>
#include <vtkNew.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPNGWriter.h>
#include <vtkPropPicker.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTextProperty.h>
#include <vtkWindowToImageFilter.h>

/**
 * @brief Builds the UI, connects controls, and initialises the VTK scene.
 * @param parent Optional parent widget.
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), vrThread(nullptr) {
  ui->setupUi(this);

  /* The .ui defaults to 1200x780 which is taller than the work area on
   * a 1366x768 laptop with the Windows taskbar visible - the bottom
   * VR Controls row gets clipped behind the taskbar. Cap to the
   * screen's available geometry (which already excludes taskbars and
   * docks) and centre on it so the user never has to drag the window
   * up to see the buttons. */
  if (QScreen *scr = QGuiApplication::primaryScreen()) {
    QRect avail = scr->availableGeometry();
    QSize def(qMin(1200, avail.width() - 20),
              qMin(780, avail.height() - 20));
    resize(def);
    move(avail.x() + (avail.width() - def.width()) / 2,
         avail.y() + (avail.height() - def.height()) / 2);
  }

  /* Snapshot the .ui-baked light stylesheet so applyTheme() can clear it
   * for dark mode (widget-level styles outrank qApp ones) and restore it
   * when switching back. */
  originalStyleSheet = this->styleSheet();

  /* Use the bundled VR icon as the window/taskbar icon so the demo
   * doesn't show the generic Qt feather. */
  setWindowIcon(QIcon(":/Icons/icons/startVR.png"));
  refreshWindowTitle();

  ui->actionOpen_Folder->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));

  /* Tree-side buttons */
  connect(ui->addItemButton, &QPushButton::released, this,
          &MainWindow::on_actionAdd_Item_triggered);
  connect(ui->removeItemButton, &QPushButton::released, this,
          &MainWindow::on_actionRemove_Item_triggered);
  connect(ui->optionsButton, &QPushButton::released, this,
          &MainWindow::openItemOptions);

  /* Open File / Open Folder live in the File menu and the toolbar - both
   * routes auto-connect to on_actionOpen_File_triggered /
   * on_actionOpen_Folder_triggered via setupUi's connectSlotsByName. */

  /* View-controls widgets */
  connect(ui->resetViewButton, &QPushButton::released, this,
          &MainWindow::onResetViewClicked);
  connect(ui->changeColourButton, &QPushButton::released, this,
          &MainWindow::onChangeColourClicked);
  connect(ui->backgroundColourButton, &QPushButton::released, this,
          &MainWindow::onBackgroundColourClicked);
  connect(ui->lightSlider, &QSlider::valueChanged, this,
          &MainWindow::onLightIntensityChanged);
  connect(ui->shrinkSlider, &QSlider::valueChanged, this,
          &MainWindow::onShrinkSliderChanged);
  connect(ui->clipSlider, &QSlider::valueChanged, this,
          &MainWindow::onClipSliderChanged);
  /* Shrink and clip rebuild every leaf's VTK filter chain when the value
   * changes. With a folder selected and thousands of leaves underneath,
   * tracking made the slider fire 100x per drag and froze the renderer.
   * Drop tracking so valueChanged only fires on release - the slider
   * still moves visually during the drag, but the cascade applies once
   * at the end. Light and opacity stay tracked; they're cheap. */
  ui->shrinkSlider->setTracking(false);
  ui->clipSlider->setTracking(false);
  /* Auto-sync VR after any change. The slider value handlers
   * (onShrinkSliderChanged etc.) call scheduleVRSync() at the end, so
   * every slider tick during a drag schedules a sync. The 100ms
   * debounce coalesces the storm of valueChanged events into one sync
   * the moment the user briefly pauses (or releases the handle), giving
   * a near-live feel inside VR without rebuilding the scene 60 times
   * per second. */
  m_vrSyncDebounce = new QTimer(this);
  m_vrSyncDebounce->setSingleShot(true);
  m_vrSyncDebounce->setInterval(100);
  connect(m_vrSyncDebounce, &QTimer::timeout, this, &MainWindow::doVRSync);

  /* 30Hz polling timer for the desktop trackball -> VR camera-follow.
   * Starts when Start VR fires, stops on Stop VR. Owned by MainWindow
   * so it dies with the window. */
  m_cameraSyncTimer = new QTimer(this);
  m_cameraSyncTimer->setInterval(33);
  connect(m_cameraSyncTimer, &QTimer::timeout, this,
          &MainWindow::syncCameraToVR);
  /* Animated one-click explode + direction dropdown. The QTimer
   * (created lazily on first click) drives a 1-second transition;
   * see onExplodeButtonClicked / onExplodeAnimTick for the loop. */
  connect(ui->explodeButton, &QPushButton::clicked, this,
          &MainWindow::onExplodeButtonClicked);
  connect(ui->explodeModeBox,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::onExplodeModeChanged);

  /* X-ray opacity slider + Solid quick-reset. Slider drives per-tick
   * desktop updates; the VR sync on release is wired earlier with the
   * shrink/clip sliders to keep all the auto-sync triggers in one place. */
  connect(ui->opacitySlider, &QSlider::valueChanged, this,
          &MainWindow::onOpacitySliderChanged);
  connect(ui->opacitySolidButton, &QPushButton::released, this,
          &MainWindow::onOpacitySolidClicked);
  connect(ui->visibilityCheckBox, &QCheckBox::toggled, this,
          &MainWindow::onVisibilityToggled);

  /* VR buttons */
  connect(ui->startVRButton, &QPushButton::released, this,
          &MainWindow::on_actionStart_VR_triggered);
  connect(ui->stopVRButton, &QPushButton::released, this,
          &MainWindow::on_actionStop_VR_triggered);
  connect(ui->vrSyncButton, &QPushButton::released, this,
          &MainWindow::onSyncVRClicked);
  connect(ui->vrRotateButton, &QPushButton::toggled, this,
          [this](bool /*checked*/) { onToggleVRRotation(); });

  /* "R" toggles a Y-axis spin while VR is running (PDF 3.3.3 fifth
   * bullet: "Add some animation: e.g. the rotation hinted at in the
   * renderThread class"). Added programmatically so no .ui edit is
   * required. The action is also added to the main window so the
   * shortcut works regardless of focus. */
  QAction *rotateAction = new QAction(tr("Toggle VR Rotation"), this);
  rotateAction->setShortcut(QKeySequence(Qt::Key_R));
  rotateAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(rotateAction);
  connect(rotateAction, &QAction::triggered, this,
          &MainWindow::onToggleVRRotation);

  /* Ctrl+Shift+D pops a copy-pasteable diagnostic dialog showing the
   * state of the GUI tree and the VR thread's actor list. Useful when
   * VR launches but no model appears - the dialog shows whether each
   * step (tree -> getNewActor -> addActorOffline -> drain) actually
   * happened. */
  QAction *diagAction = new QAction(tr("Show VR Diagnostics"), this);
  QAction *darkModeAction = new QAction(tr("Night Mode"), this);
  darkModeAction->setCheckable(true);
  ui->menuView->addSeparator();
  ui->menuView->addAction(darkModeAction);

  connect(darkModeAction, &QAction::toggled, this, [this](bool checked) {
    applyTheme(checked);
  });
  diagAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  diagAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(diagAction);
  connect(diagAction, &QAction::triggered, this, [this]() {
    showVRDiagnosticDialog(tr("Manual diagnostic (Ctrl+Shift+D)"));
  });

  /* Status bar */
  connect(this, &MainWindow::statusUpdateMessage, ui->statusbar,
          &QStatusBar::showMessage);

  /* Keyboard Shortcuts for main actions.
   * Standard keys (Ctrl+O, Ctrl+S, Del, etc.) are used where possible. */
  ui->actionOpen_File->setShortcut(QKeySequence::Open);
  ui->actionOpen_Folder->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
  ui->actionSave->setShortcut(QKeySequence::Save);
  ui->actionPrint->setShortcut(QKeySequence::Print);
  ui->actionAdd_Item->setShortcut(QKeySequence::New);
  ui->actionRemove_Item->setShortcut(QKeySequence::Delete);
  ui->actionItem_Options->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
  ui->actionChange_Colour->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
  ui->actionToggle_Visibility->setShortcut(QKeySequence(Qt::Key_V));
  ui->actionReset_View->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
  ui->actionStart_VR->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_V));
  ui->actionStop_VR->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
  ui->actionSync_VR->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
  ui->actionAbout->setShortcut(QKeySequence::HelpContents);

  /* Light intensity shortcuts (+/-) */
  QAction *incLightAction = new QAction(tr("Increase Light Intensity"), this);
  incLightAction->setShortcuts({QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal)});
  addAction(incLightAction);
  connect(incLightAction, &QAction::triggered, this, [this]() {
    ui->lightSlider->setValue(ui->lightSlider->value() + 5);
  });

  QAction *decLightAction = new QAction(tr("Decrease Light Intensity"), this);
  decLightAction->setShortcut(QKeySequence(Qt::Key_Minus));
  addAction(decLightAction);
  connect(decLightAction, &QAction::triggered, this, [this]() {
    ui->lightSlider->setValue(ui->lightSlider->value() - 5);
  });

  /* Opacity shortcuts ([/]) */
  QAction *incOpacityAction = new QAction(tr("Increase Opacity"), this);
  incOpacityAction->setShortcut(QKeySequence(Qt::Key_BracketRight));
  addAction(incOpacityAction);
  connect(incOpacityAction, &QAction::triggered, this, [this]() {
    ui->opacitySlider->setValue(ui->opacitySlider->value() + 5);
  });

  QAction *decOpacityAction = new QAction(tr("Decrease Opacity"), this);
  decOpacityAction->setShortcut(QKeySequence(Qt::Key_BracketLeft));
  addAction(decOpacityAction);
  connect(decOpacityAction, &QAction::triggered, this, [this]() {
    ui->opacitySlider->setValue(ui->opacitySlider->value() - 5);
  });

  /* Explode Factor shortcuts (Shift + [/]) */
  QAction *incExplodeAction = new QAction(tr("Increase Explode Factor"), this);
  incExplodeAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BracketRight));
  addAction(incExplodeAction);
  connect(incExplodeAction, &QAction::triggered, this, [this]() {
    ui->shrinkSlider->setValue(ui->shrinkSlider->value() + 5);
  });

  QAction *decExplodeAction = new QAction(tr("Decrease Explode Factor"), this);
  decExplodeAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft));
  addAction(decExplodeAction);
  connect(decExplodeAction, &QAction::triggered, this, [this]() {
    ui->shrinkSlider->setValue(ui->shrinkSlider->value() - 5);
  });

  /* Tree clicks */
  connect(ui->treeView, &QTreeView::clicked, this,
          &MainWindow::handleTreeClicked);

  /* Allocate the ModelList */
  this->partList = new ModelPartList("PartsList");
  ui->treeView->setModel(this->partList);

  /* Right-click context menu on the tree. Marksheet (s75/s89) wants
   * colour and visibility specifically reachable through the context
   * menu, not just the right-panel buttons - so we expose both here. */
  ui->treeView->setContextMenuPolicy(Qt::ActionsContextMenu);
  ui->treeView->addAction(ui->actionItem_Options);
  {
    auto *sep1 = new QAction(this);
    sep1->setSeparator(true);
    ui->treeView->addAction(sep1);
  }
  ui->treeView->addAction(ui->actionChange_Colour);
  ui->treeView->addAction(ui->actionToggle_Visibility);
  {
    auto *sep2 = new QAction(this);
    sep2->setSeparator(true);
    ui->treeView->addAction(sep2);
  }
  ui->treeView->addAction(ui->actionAdd_Item);
  ui->treeView->addAction(ui->actionRemove_Item);

  /* --- Hook VTK render window to the Qt widget --- */
  renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
  /* MSAA is set later, in tandem with depth peeling - the two are
   * mutually exclusive on most drivers, so we pick depth peeling
   * (needed for the X-ray feature) and rely on FXAA below for
   * edge smoothing. */
  ui->vtkWidget->setRenderWindow(renderWindow);

  renderer = vtkSmartPointer<vtkRenderer>::New();
  renderWindow->AddRenderer(renderer);

  /* Gradient sky-style background (top dark blue -> bottom light grey).
   * Looks much nicer than a flat colour for a CAD viewer. */
  renderer->SetBackground(0.74, 0.77, 0.82);  /* bottom (lighter) */
  renderer->SetBackground2(0.18, 0.22, 0.30); /* top    (darker)  */
  renderer->GradientBackgroundOn();

  /* FXAA on top of MSAA gives clean lines on thin geometry / silhouettes. */
  renderer->UseFXAAOn();

  /* Order-independent transparency via depth peeling. Without this,
   * stacking multiple translucent X-ray parts shows obvious sorting
   * artefacts (the part further from the camera bleeds through the
   * one in front). 4 peels is plenty for a CAD assembly with ~10
   * overlapping shells; OcclusionRatio 0.1 lets VTK stop early once
   * the remaining contribution drops below 10%. Requires the
   * renderWindow to use alpha bit planes, which vtkGenericOpenGLRenderWindow
   * does by default. */
  renderWindow->SetAlphaBitPlanes(1);
  renderWindow->SetMultiSamples(0); /* MSAA + depth peeling don't mix */
  renderer->SetUseDepthPeeling(1);
  renderer->SetMaximumNumberOfPeels(4);
  renderer->SetOcclusionRatio(0.1);

  /* Headlight controlled by the right-panel Light Intensity slider. */
  setupLighting();

  /* Corner axes gizmo (bottom-left). Always visible, non-interactive,
   * shows world-space X / Y / Z so explode directions are obvious. */
  vtkSmartPointer<vtkAxesActor> axes = vtkSmartPointer<vtkAxesActor>::New();
  axes->SetXAxisLabelText("X");
  axes->SetYAxisLabelText("Y");
  axes->SetZAxisLabelText("Z");
  axes->GetXAxisCaptionActor2D()->GetCaptionTextProperty()->SetColor(1.0,
                                                                     0.25, 0.25);
  axes->GetYAxisCaptionActor2D()->GetCaptionTextProperty()->SetColor(0.30,
                                                                     0.80, 0.30);
  axes->GetZAxisCaptionActor2D()->GetCaptionTextProperty()->SetColor(0.30,
                                                                     0.50, 1.0);
  for (auto *cap :
       {axes->GetXAxisCaptionActor2D(), axes->GetYAxisCaptionActor2D(),
        axes->GetZAxisCaptionActor2D()}) {
    cap->GetCaptionTextProperty()->BoldOn();
    cap->GetCaptionTextProperty()->ShadowOff();
    cap->GetCaptionTextProperty()->SetFontSize(14);
  }

  orientationWidget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
  orientationWidget->SetOutlineColor(0.93, 0.57, 0.13);
  orientationWidget->SetOrientationMarker(axes);
  orientationWidget->SetInteractor(renderWindow->GetInteractor());
  orientationWidget->SetViewport(0.0, 0.0, 0.18, 0.22);
  orientationWidget->SetEnabled(1);
  orientationWidget->InteractiveOff();

  renderer->ResetCamera();
  renderer->GetActiveCamera()->Azimuth(30);
  renderer->GetActiveCamera()->Elevation(30);
  renderer->ResetCameraClippingRange();

  /* Install ourselves as an event filter on the VTK render widget so
   * we can detect a "click without drag" and turn it into a part
   * selection. Press / release tracking lives in eventFilter(); the
   * actual ray cast is in pickPartAt(). */
  ui->vtkWidget->installEventFilter(this);

  updateRender();

  /* If the exe ships next to a Models/ folder (USB-stick deployment),
   * point the file dialogs there so the demo models are one click away. */
  QString bundledModels =
      QCoreApplication::applicationDirPath() + QStringLiteral("/Models");
  if (QFileInfo(bundledModels).isDir())
    lastBrowsedDir = bundledModels;
  else
    lastBrowsedDir = QDir::currentPath();

  emit statusUpdateMessage(tr("Ready"), 0);
}

/**
 * @brief Stops any running VR thread and releases the generated UI.
 */
MainWindow::~MainWindow() {
  /* Make sure the VR thread is stopped before any of its dependencies
   * (renderer, actors held by ModelParts) get destroyed. */
  if (vrThread) {
    vrThread->issueCommand(VRRenderThread::END_RENDER, 0.0);
    vrThread->wait();
    delete vrThread;
    vrThread = nullptr;
  }

  delete ui;
  delete partList;
}

void MainWindow::closeEvent(QCloseEvent *event) {
  /* If a VR session is live, ask before tearing it down. Otherwise just
   * stop it silently and proceed. Either way, wait with a short timeout
   * so a hung VR thread can't keep the GUI alive forever. */
  if (vrThread && vrThread->isRunning()) {
    auto answer = QMessageBox::question(
        this, tr("VR is running"),
        tr("A VR session is currently active. Close anyway?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      event->ignore();
      return;
    }
    if (m_cameraSyncTimer)
      m_cameraSyncTimer->stop();
    if (m_vrSyncDebounce)
      m_vrSyncDebounce->stop();
    vrThread->issueCommand(VRRenderThread::END_RENDER, 0.0);
    if (!vrThread->wait(3000)) {
      qWarning() << "VR thread did not exit within 3s; terminating.";
      vrThread->terminate();
      vrThread->wait(1000);
    }
    delete vrThread;
    vrThread = nullptr;
    m_vrActorToPart.clear();
  }
  event->accept();
}

void MainWindow::refreshWindowTitle() {
  const QString base = tr("Group 13 - VR CAD Viewer");
  if (vrThread && vrThread->isRunning())
    setWindowTitle(base + tr("  [VR running]"));
  else
    setWindowTitle(base);
}

vtkIdType MainWindow::countVisibleTriangles(const QModelIndex &index) const {
  vtkIdType total = 0;
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && part->visible() && !part->getStlPath().isEmpty())
      total += part->getTriangleCount();
  }
  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    total += countVisibleTriangles(partList->index(i, 0, index));
  return total;
}

void MainWindow::handleTreeClicked() {
  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid())
    return;

  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
  if (!selectedPart)
    return;

  QString text = selectedPart->data(0).toString();

  /* Reflect selected item's filter/visibility state in the right-panel
   * controls. Slider value 0 = no shrink (factor 1.0), slider 100 =
   * max shrink (factor 0.0), matching onShrinkSliderChanged. */
  ui->shrinkSlider->blockSignals(true);
  ui->clipSlider->blockSignals(true);
  ui->visibilityCheckBox->blockSignals(true);
  int shrinkSliderValue =
      static_cast<int>((1.0 - selectedPart->getShrinkFactor()) * 100.0);
  ui->shrinkSlider->setValue(qBound(0, shrinkSliderValue, 100));
  ui->clipSlider->setValue(selectedPart->getClipFilter() ? 50 : 0);
  ui->visibilityCheckBox->setChecked(selectedPart->visible());
  ui->shrinkSlider->blockSignals(false);
  ui->clipSlider->blockSignals(false);
  ui->visibilityCheckBox->blockSignals(false);

  emit statusUpdateMessage(QString("Selected: ") + text, 0);
}

/**
 * @brief Opens the item options dialog and applies accepted edits.
 */
void MainWindow::openItemOptions() {
  QModelIndex index = ui->treeView->currentIndex();

  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Cannot edit options: no item selected in the tree."), 0);
    QMessageBox::warning(this, tr("No Selection"),
                         tr("Please select an item in the tree view first."));
    return;
  }

  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());

  OptionDialog dialog(this);
  dialog.setName(selectedPart->data(0).toString());
  dialog.setItemVisible(selectedPart->data(1).toString() == "Yes");
  dialog.setStlPath(selectedPart->getStlPath());
  dialog.setR(selectedPart->getColourR());
  dialog.setG(selectedPart->getColourG());
  dialog.setB(selectedPart->getColourB());
  dialog.setClipFilter(selectedPart->getClipFilter());
  dialog.setShrinkFilter(selectedPart->getShrinkFilter());

  if (dialog.exec() != QDialog::Accepted)
    return;

  partList->setData(index, dialog.getName(), Qt::EditRole);
  partList->setData(index.siblingAtColumn(1),
                    dialog.getVisible() ? "Yes" : "No", Qt::EditRole);

  bool pathChanged = (selectedPart->getStlPath() != dialog.getStlPath());

  selectedPart->setStlPath(dialog.getStlPath());
  selectedPart->setColour(dialog.getR(), dialog.getG(), dialog.getB());
  selectedPart->setVisible(dialog.getVisible());

  if (pathChanged && !dialog.getStlPath().isEmpty()) {
    QString err;
    if (!selectedPart->loadSTL(dialog.getStlPath(), &err)) {
      QMessageBox::warning(this, tr("Could not load STL"), err);
      emit statusUpdateMessage(tr("Load failed: %1").arg(err), 0);
    }
  }

  selectedPart->setClipFilter(dialog.getClipFilter());
  selectedPart->setShrinkFilter(dialog.getShrinkFilter());

  if (selectedPart->getActor()) {
    selectedPart->getActor()->GetProperty()->SetColor(
        dialog.getR() / 255.0, dialog.getG() / 255.0, dialog.getB() / 255.0);
    /* No SetVisibility call here - selectedPart->setVisible(...) above
     * already pushed the new visibility state into the actor. */
  }

  updateRender();

  emit statusUpdateMessage(QString("Updated item: ") + dialog.getName(), 0);
}

/**
 * @brief Opens one STL file and inserts it below the selected tree item.
 */
void MainWindow::on_actionOpen_File_triggered() {
  QModelIndex index = ui->treeView->currentIndex();

  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open File"), lastBrowsedDir, tr("STL Files (*.stl)"));

  if (fileName.isEmpty()) {
    emit statusUpdateMessage(tr("Open file cancelled"), 0);
    return;
  }
  lastBrowsedDir = QFileInfo(fileName).absolutePath();

  QList<QVariant> data = {tr("New Part"), tr("Yes")};
  QModelIndex newIndex = partList->appendChild(index, data);

  if (!newIndex.isValid()) {
    emit statusUpdateMessage(tr("Failed to create new tree item"), 0);
    return;
  }

  ModelPart *newPart = static_cast<ModelPart *>(newIndex.internalPointer());
  QString loadErr;
  if (!newPart->loadSTL(fileName, &loadErr)) {
    /* Roll back the empty tree row so the user doesn't see a phantom
     * entry that has no actor. */
    partList->removeItem(newIndex);
    QMessageBox::warning(this, tr("Could not load STL"), loadErr);
    emit statusUpdateMessage(tr("Load failed: %1").arg(loadErr), 0);
    return;
  }

  QFileInfo fileInfo(fileName);
  partList->setData(newIndex.siblingAtColumn(0), fileInfo.fileName(),
                    Qt::EditRole);

  ui->treeView->setCurrentIndex(newIndex);
  if (index.isValid())
    ui->treeView->expand(index);

  /* If X-ray mode is currently active, the slider's value is < 100
   * and the existing parts are translucent. A freshly loaded part
   * defaults to opaque, which would stick out as solid amongst the
   * see-through ones. Re-apply the global opacity to the whole
   * tree so the new part inherits it. */
  double currentOpacity = ui->opacitySlider->value() / 100.0;
  if (currentOpacity < 1.0)
    applyOpacityToTree(QModelIndex(), currentOpacity);

  updateRender();
  renderer->ResetCamera();
  renderWindow->Render();

  emit statusUpdateMessage(QString("Opened file: ") + fileInfo.fileName(), 0);
}

/**
 * @brief Opens one or more folders and imports all STL files they contain.
 */
void MainWindow::on_actionOpen_Folder_triggered() {
  QModelIndex parentIndex = ui->treeView->currentIndex();

  /* Multi-folder pick: Qt's getExistingDirectory only returns one
   * folder, and the native Windows directory picker doesn't support
   * multi-select at all. So we drop to a non-native QFileDialog and
   * flip its internal QListView/QTreeView into ExtendedSelection
   * mode - that way Ctrl+click / Shift+click in the dialog picks
   * several folders in a single shot. */
  QStringList chosenPaths;
  {
    QFileDialog dialog(this, tr("Open Folder(s) of STL Files"),
                       lastBrowsedDir);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);

    /* The two children below are the dialog's contents view; their
     * exact object names / classes are stable across Qt 5/6. Setting
     * ExtendedSelection on whichever one is currently shown gives
     * the user the standard Ctrl/Shift multi-select behaviour. */
    if (auto *l = dialog.findChild<QListView *>("listView"))
      l->setSelectionMode(QAbstractItemView::ExtendedSelection);
    if (auto *t = dialog.findChild<QTreeView *>())
      t->setSelectionMode(QAbstractItemView::ExtendedSelection);

    if (dialog.exec() != QDialog::Accepted) {
      emit statusUpdateMessage(tr("Open folder cancelled"), 0);
      return;
    }

    chosenPaths = dialog.selectedFiles();
    /* Drop accidental duplicates (some Qt versions occasionally
     * include the dialog's current directory alongside the
     * actual selection). */
    chosenPaths.removeDuplicates();
  }

  if (chosenPaths.isEmpty()) {
    emit statusUpdateMessage(tr("Open folder cancelled"), 0);
    return;
  }

  /* Anchor the next browse on the first picked folder's parent so
   * the dialog re-opens at the same level the user was working at. */
  lastBrowsedDir = QFileInfo(chosenPaths.first()).absolutePath();

  /* Walk each chosen directory recursively. Empty branches (folders
   * with no STLs anywhere in their subtree) are pruned so the GUI
   * tree mirrors only the parts of the filesystem that actually
   * hold geometry. */
  struct Node {
    QDir dir;
    QStringList files;
    QList<Node> children;
  };

  const QStringList stlFilters{"*.stl", "*.STL"};

  std::function<bool(const QDir &, Node &)> scan = [&](const QDir &d,
                                                       Node &out) -> bool {
    out.dir = d;
    out.files = d.entryList(stlFilters, QDir::Files);

    const QStringList subDirs =
        d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &sub : subDirs) {
      Node child;
      if (scan(QDir(d.absoluteFilePath(sub)), child))
        out.children.append(child);
    }

    return !out.files.isEmpty() || !out.children.isEmpty();
  };

  std::function<int(const Node &)> count = [&](const Node &n) {
    int c = n.files.size();
    for (const Node &child : n.children)
      c += count(child);
    return c;
  };

  /* Build one Node per chosen folder. Skip any that have no STLs
   * anywhere in their subtree so we don't produce empty tree rows. */
  QList<Node> roots;
  QStringList skippedNames;
  for (const QString &path : chosenPaths) {
    Node root;
    if (scan(QDir(path), root))
      roots.append(root);
    else
      skippedNames << QFileInfo(path).fileName();
  }

  if (roots.isEmpty()) {
    emit statusUpdateMessage(tr("No STL files found in selection"), 0);
    QMessageBox::information(
        this, tr("No STL Files"),
        tr("None of the chosen folders contain .stl files (in any "
           "subdirectory)."));
    return;
  }

  int totalFiles = 0;
  for (const Node &n : roots)
    totalFiles += count(n);

  /* One progress dialog spans the whole multi-folder load so the
   * user gets a single "X of N files" rather than back-to-back
   * dialogs flashing past. */
  QProgressDialog progress(tr("Loading STL files..."), tr("Cancel"), 0,
                           totalFiles, this);
  const QString title = (roots.size() == 1)
                            ? tr("Loading %1").arg(roots.first().dir.dirName())
                            : tr("Loading %1 folders").arg(roots.size());
  progress.setWindowTitle(title);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setValue(0);

  int loaded = 0;
  bool cancelled = false;

  std::function<void(const Node &, QModelIndex)> load =
      [&](const Node &node, QModelIndex parent) {
        if (cancelled)
          return;

        QList<QVariant> data = {node.dir.dirName(), tr("Yes")};
        QModelIndex nodeIdx = partList->appendChild(parent, data);

        for (const QString &file : node.files) {
          if (progress.wasCanceled()) {
            cancelled = true;
            return;
          }

          progress.setLabelText(tr("Loading %1/%2 (%3 of %4)...")
                                    .arg(node.dir.dirName())
                                    .arg(file)
                                    .arg(loaded + 1)
                                    .arg(totalFiles));

          QString fullPath = node.dir.absoluteFilePath(file);
          QList<QVariant> fileData = {file, tr("Yes")};
          QModelIndex newIndex = partList->appendChild(nodeIdx, fileData);

          if (!newIndex.isValid()) {
            ++loaded;
            progress.setValue(loaded);
            continue;
          }

          ModelPart *newPart =
              static_cast<ModelPart *>(newIndex.internalPointer());
          QString loadErr;
          if (!newPart->loadSTL(fullPath, &loadErr)) {
            /* Drop the failed row so we don't leave dead entries in
             * the tree. Status-only feedback; per-file popups would
             * be unusable across hundreds of STLs. */
            partList->removeItem(newIndex);
            qWarning() << "Skipped STL:" << loadErr;
            ++loaded;
            progress.setValue(loaded);
            continue;
          }

          partList->setData(newIndex.siblingAtColumn(0), file, Qt::EditRole);

          ++loaded;
          progress.setValue(loaded);
        }

        for (const Node &child : node.children) {
          if (cancelled)
            return;
          load(child, nodeIdx);
        }

        ui->treeView->expand(nodeIdx);
      };

  /* Drop each root underneath the currently-selected tree row (or
   * top-level if nothing is selected, same as before). */
  for (const Node &root : roots) {
    if (cancelled)
      break;
    load(root, parentIndex);
  }

  progress.setValue(totalFiles);

  if (parentIndex.isValid())
    ui->treeView->expand(parentIndex);

  /* Inherit the current X-ray level on every freshly loaded part
   * (see Open File handler for the rationale). */
  double currentOpacity = ui->opacitySlider->value() / 100.0;
  if (currentOpacity < 1.0)
    applyOpacityToTree(QModelIndex(), currentOpacity);

  updateRender();
  renderer->ResetCamera();
  renderWindow->Render();

  /* Compose a status message that lists the loaded folders, plus a
   * note about any picked folders that were empty so the user
   * notices their drag selection accidentally caught a non-STL
   * directory. */
  QStringList rootNames;
  for (const Node &n : roots)
    rootNames << n.dir.dirName();
  const QString summary = rootNames.join(QStringLiteral(", "));

  if (cancelled) {
    emit statusUpdateMessage(QString("Loading cancelled after %1 of %2 files")
                                 .arg(loaded)
                                 .arg(totalFiles),
                             0);
  } else {
    QString msg = QString("Loaded %1 STL files from: %2")
                      .arg(loaded)
                      .arg(summary);
    if (!skippedNames.isEmpty())
      msg += QString("  (skipped empty: %1)")
                 .arg(skippedNames.join(QStringLiteral(", ")));
    emit statusUpdateMessage(msg, 0);
  }
}

void MainWindow::on_actionSave_triggered() {
  emit statusUpdateMessage(tr("Save: not yet implemented"), 0);
}

void MainWindow::on_actionPrint_triggered() {
  emit statusUpdateMessage(tr("Print: not yet implemented"), 0);
}

void MainWindow::on_actionExit_triggered() {
  QApplication::quit();
}

/**
 * @brief Diffs the tree actors against the renderer and updates the scene.
 */
void MainWindow::updateRender() {
  /* Build the desired set of actors from the current tree. Folder rows
   * have no actor (loadSTL was never called), so they're naturally
   * skipped by the null check in collectTreeActors. */
  QSet<vtkActor *> desired;
  collectTreeActors(QModelIndex(), desired);

  /* Snapshot the renderer's current actors. We can't mutate the
   * collection while iterating it, so we collect first, diff second. */
  QSet<vtkActor *> current;
  if (vtkActorCollection *actors = renderer->GetActors()) {
    vtkCollectionSimpleIterator it;
    actors->InitTraversal(it);
    while (vtkActor *a = actors->GetNextActor(it))
      current.insert(a);
  }

  /* Remove actors that left the tree (e.g. on Remove Item). */
  for (vtkActor *a : current) {
    if (!desired.contains(a))
      renderer->RemoveActor(a);
  }

  /* Add actors that just appeared (e.g. on Open File / Open Folder).
   * Material props are stamped only on first add - they don't change
   * across renders, so re-stamping every frame (as the old recursive
   * version did) was wasted work. */
  for (vtkActor *a : desired) {
    if (!current.contains(a)) {
      vtkProperty *prop = a->GetProperty();
      prop->SetAmbient(0.3);
      prop->SetDiffuse(0.8);
      prop->SetSpecular(0.3);
      prop->SetSpecularPower(20);
      renderer->AddActor(a);
    }
  }

  renderWindow->Render();
}

void MainWindow::collectTreeActors(const QModelIndex &index,
                                   QSet<vtkActor *> &out) const {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && part->getActor())
      out.insert(part->getActor().Get());
  }

  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; i++)
    collectTreeActors(partList->index(i, 0, index), out);
}

void MainWindow::on_actionItem_Options_triggered() { openItemOptions(); }

void MainWindow::on_actionAdd_Item_triggered() {
  QModelIndex index = ui->treeView->currentIndex();

  QList<QVariant> data = {tr("New Part"), tr("Yes")};
  QModelIndex newIndex = partList->appendChild(index, data);

  if (!newIndex.isValid()) {
    emit statusUpdateMessage(tr("Failed to add new item"), 0);
    return;
  }

  ui->treeView->setCurrentIndex(newIndex);
  if (index.isValid())
    ui->treeView->expand(index);

  emit statusUpdateMessage(tr("Added new item"), 0);
}

void MainWindow::on_actionRemove_Item_triggered() {
  QModelIndex index = ui->treeView->currentIndex();

  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Cannot remove: no item selected in the tree."), 0);
    QMessageBox::warning(this, tr("No Selection"),
                         tr("Please select an item to remove."));
    return;
  }

  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
  QString name =
      selectedPart ? selectedPart->data(0).toString() : tr("(unknown)");

  if (partList->removeItem(index)) {
    updateRender();
    emit statusUpdateMessage(tr("Removed item: ") + name, 0);
  } else {
    emit statusUpdateMessage(tr("Failed to remove item"), 0);
  }
}

void MainWindow::on_actionReset_View_triggered() { onResetViewClicked(); }

void MainWindow::on_actionChange_Background_triggered() {
  onBackgroundColourClicked();
}

void MainWindow::on_actionToggle_Shrink_triggered() {
  /* Menu/toolbar quick-toggle now flips the slider between full size
   * (0) and the typical 80% shrink (20). Lets users keep the keyboard
   * shortcut while we still ship the continuous slider. */
  bool on = ui->actionToggle_Shrink->isChecked();
  ui->shrinkSlider->setValue(on ? 20 : 0);
}

void MainWindow::on_actionToggle_Clip_triggered() {
  bool on = ui->actionToggle_Clip->isChecked();
  ui->clipSlider->setValue(on ? 50 : 0);
}

/**
 * @brief Validates the scene and starts the OpenVR render thread.
 */
void MainWindow::on_actionStart_VR_triggered() {
  if (vrThread && vrThread->isRunning()) {
    emit statusUpdateMessage(tr("VR is already running"), 0);
    return;
  }

  /* Pre-flight: vtkOpenVRRenderWindowInteractor::Initialize() will
   * dereference vr::VRInput() unconditionally; if SteamVR is not running
   * or no HMD is present that pointer is null and the whole app crashes
   * with a read access violation. Catch it here instead. */
  QString reason;
  if (!VRRenderThread::isVRAvailable(&reason)) {
    QMessageBox::warning(this, tr("Cannot start VR"), reason);
    emit statusUpdateMessage(tr("Cannot start VR: %1").arg(reason), 0);
    return;
  }

  /* Empty-scene check: launching VR with no actors just shows a black
   * room and confuses the demo. Tell the user to load something first. */
  vtkIdType totalTris = countVisibleTriangles(QModelIndex());
  if (totalTris == 0) {
    QMessageBox::information(this, tr("No models loaded"),
                             tr("Load at least one STL part (or a folder "
                                "of parts) and make it visible before "
                                "starting VR."));
    emit statusUpdateMessage(tr("Start VR aborted: scene is empty"), 0);
    return;
  }

  /* Heavy-scene warning: the OpenVR compositor really suffers above ~2M
   * triangles on integrated / mobile GPUs, which is what most lab
   * machines run. Let the user bail before sending the headset to 11fps. */
  const vtkIdType heavyLimit = 2'000'000;
  if (totalTris > heavyLimit) {
    QString msg =
        tr("This scene has %1 triangles, which may run very slowly in VR "
           "(target machines often hit < 30 fps above ~2M). Continue?")
            .arg(QLocale::system().toString(qlonglong(totalTris)));
    auto answer =
        QMessageBox::warning(this, tr("Heavy scene"), msg,
                             QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      emit statusUpdateMessage(tr("Start VR aborted: scene too heavy"), 0);
      return;
    }
  }

  /* Recreate the thread each session so a previous Stop+Start cycle gets
   * a clean OpenVR pipeline. */
  if (vrThread) {
    delete vrThread;
    vrThread = nullptr;
  }
  vrThread = new VRRenderThread(this);

  /* Push a fresh actor for every visible loaded part. We use getNewActor()
   * so the GUI's renderer and the VR renderer each own their own actor /
   * mapper - VTK actors must not be shared across two render windows.
   *
   * If pushTreeActorsToVR returns 0 the list is empty and starting VR
   * would just show the floor + fog. Bail out with a clear message
   * instead of letting the user wonder why the headset is empty. */
  int pushed = pushTreeActorsToVR(QModelIndex());
  qDebug() << "Start VR: pushed" << pushed
           << "actors to the VR thread's pending list";

  if (pushed == 0) {
    showVRDiagnosticDialog(
        tr("Start VR aborted: walked the tree but pushed 0 actors. "
           "The model never reached the VR list. The breakdown below "
           "shows where in the tree -> actor -> list pipeline the "
           "models were lost."));
    delete vrThread;
    vrThread = nullptr;
    emit statusUpdateMessage(
        tr("Start VR aborted: actor list was empty after walking tree"), 0);
    return;
  }

  vrThread->start();
  refreshWindowTitle();

  /* VR -> GUI feedback loop: VRRenderThread emits actorMovedInVR
   * whenever the OpenVR controller drags an actor. Use a queued
   * connection so the slot runs on the GUI thread; the signal is
   * fired from the VR worker thread. */
  connect(vrThread, &VRRenderThread::actorMovedInVR, this,
          &MainWindow::onVRActorMoved, Qt::QueuedConnection);

  /* Desktop trackball -> VR rotation follow. Start polling the camera
   * pose now that the VR thread is alive; syncCameraToVR is the no-op
   * when the thread isn't running, but the explicit start/stop here
   * keeps timer activity scoped to the VR session. */
  if (m_cameraSyncTimer) {
    m_lastSyncedAzimuth = computeCameraAzimuth();
    m_lastSyncedElevation = computeCameraElevation();
    m_cameraSyncTimer->start();
  }

  emit statusUpdateMessage(
      tr("VR started (%1 triangles).")
          .arg(QLocale::system().toString(qlonglong(totalTris))),
      0);
}

/**
 * @brief Stops and deletes the active VR render thread.
 */
void MainWindow::on_actionStop_VR_triggered() {
  if (!vrThread || !vrThread->isRunning()) {
    emit statusUpdateMessage(tr("VR is not running"), 0);
    return;
  }

  /* Stop the camera-follow + VR-sync timers BEFORE tearing down the
   * thread so neither timer can fire one last tick into a half-
   * destructed VR thread and crash the next Start VR. */
  if (m_cameraSyncTimer)
    m_cameraSyncTimer->stop();
  if (m_vrSyncDebounce)
    m_vrSyncDebounce->stop();

  vrThread->issueCommand(VRRenderThread::END_RENDER, 0.0);
  vrThread->wait();
  delete vrThread;
  vrThread = nullptr;
  /* Stale actor* pointers from the dead VR session - drop them so the
   * next Start VR's pushTreeActorsToVR fills the map fresh. */
  m_vrActorToPart.clear();
  vrRotating = false;
  ui->vrRotateButton->blockSignals(true);
  ui->vrRotateButton->setChecked(false);
  ui->vrRotateButton->setText(tr("Start Rotation (R)"));
  ui->vrRotateButton->blockSignals(false);
  refreshWindowTitle();

  emit statusUpdateMessage(tr("VR stopped"), 0);
}

void MainWindow::on_actionChange_Colour_triggered() { onChangeColourClicked(); }

void MainWindow::on_actionToggle_Visibility_triggered() {
  /* Marksheet: "Visibility can be applied to selected parts through use
   * of context menu". Flips the current part's visible flag and routes
   * through the existing checkbox handler so VR auto-sync, status bar
   * and tree column all stay in sync. */
  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Select an item before toggling visibility."), 0);
    QMessageBox::warning(this, tr("No Selection"),
                         tr("Please select an item in the tree view first."));
    return;
  }
  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
  if (!selectedPart)
    return;
  bool now = !selectedPart->visible();
  ui->visibilityCheckBox->blockSignals(true);
  ui->visibilityCheckBox->setChecked(now);
  ui->visibilityCheckBox->blockSignals(false);
  onVisibilityToggled(now);
}

void MainWindow::on_actionSync_VR_triggered() { onSyncVRClicked(); }

void MainWindow::on_actionToggle_VR_Rotation_triggered() {
  onToggleVRRotation();
}

void MainWindow::on_actionAbout_triggered() {
  QMessageBox::about(
      this, tr("About"),
      tr("Group 13 - CAD Viewer\n\nEEEE2076 Software Development Group "
         "Project\nQt + VTK based STL viewer with VR support."));
  emit statusUpdateMessage(tr("About"), 0);
}

void MainWindow::onResetViewClicked() {
  renderer->ResetCamera();
  renderer->GetActiveCamera()->Azimuth(30);
  renderer->GetActiveCamera()->Elevation(30);
  renderer->ResetCameraClippingRange();
  renderWindow->Render();
  emit statusUpdateMessage(tr("View reset"), 0);
}

void MainWindow::onChangeColourClicked() {
  /* Cascade semantics:
   *   - "Apply to all" checkbox ticked: walk the entire tree from the
   *     root (shortcut for "stamp every loaded part" without having to
   *     select the root row first).
   *   - Otherwise: walk the selected subtree. applyColourToTree() touches
   *     the index AND every descendant, skipping rows with no STL loaded.
   *     So selecting a folder cascades the colour onto every leaf
   *     underneath; selecting a leaf affects only that leaf. */
  const bool applyToAll = ui->applyAllColourCheckBox &&
                          ui->applyAllColourCheckBox->isChecked();

  QModelIndex index = ui->treeView->currentIndex();
  ModelPart *selectedPart =
      index.isValid() ? static_cast<ModelPart *>(index.internalPointer())
                      : nullptr;

  if (!applyToAll && !selectedPart) {
    emit statusUpdateMessage(
        tr("Cannot change colour: no item selected in the tree."), 0);
    return;
  }

  /* Seed the picker from the selected part if it has STL data; otherwise
   * white (folder selections / Apply-to-all). */
  QColor initial = (selectedPart && !selectedPart->getStlPath().isEmpty())
                       ? QColor(selectedPart->getColourR(),
                                selectedPart->getColourG(),
                                selectedPart->getColourB())
                       : QColor(Qt::white);

  QColor chosen =
      QColorDialog::getColor(initial, this, tr("Choose Part Colour"));
  if (!chosen.isValid())
    return;

  if (applyToAll) {
    applyColourToTree(QModelIndex(), chosen.red(), chosen.green(),
                      chosen.blue());
    emit statusUpdateMessage(tr("Changed colour for ALL parts"), 0);
  } else {
    applyColourToTree(index, chosen.red(), chosen.green(), chosen.blue());
    emit statusUpdateMessage(
        tr("Changed colour for: ") + selectedPart->data(0).toString(), 0);
  }
  renderWindow->Render();
  /* Marksheet: "VR updates colour in real-time (no need to restart)". */
  scheduleVRSync();
}

void MainWindow::onBackgroundColourClicked() {
  QColor initial = darkMode ? QColor(20, 22, 28) : Qt::white;

  QColor chosen =
      QColorDialog::getColor(initial, this, tr("Choose Background Colour"));
  if (!chosen.isValid())
    return;

  renderer->SetBackground(chosen.redF(), chosen.greenF(), chosen.blueF());
  renderWindow->Render();
  emit statusUpdateMessage(tr("Background colour changed"), 0);
}

void MainWindow::onLightIntensityChanged(int value) {
  /* Slider 0..100 -> per-part light factor 0..2 (value/50). The factor
   * scales each part's ambient + diffuse coefficients via
   * ModelPart::setLightFactor, which lets the user spotlight or dim
   * individual parts instead of moving the single scene light.
   *
   * Cascade semantics:
   *   - "Per-Part Light" ticked: strict-leaf mode - only the selected
   *     row gets the new factor, even if it's a folder. Folders without
   *     STL data are no-ops in this mode.
   *   - Otherwise (default): walk the selected subtree via
   *     applyLightFactorToTree, so picking a folder cascades the same
   *     factor onto every leaf underneath.
   *
   * The scene light kept by setupLighting() stays at its default 1.0
   * so the renderer still has illumination if no part has had a factor
   * set yet. */
  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Select an item before adjusting the light slider."), 0);
    return;
  }
  ModelPart *selectedPart =
      static_cast<ModelPart *>(index.internalPointer());
  const double factor = value / 50.0; /* 0..2.0 */

  const bool strictLeaf =
      ui->perPartLightCheckBox && ui->perPartLightCheckBox->isChecked();

  if (strictLeaf) {
    if (!selectedPart || selectedPart->getStlPath().isEmpty()) {
      emit statusUpdateMessage(
          tr("Per-Part Light: select a leaf (STL part), not a folder."), 0);
      return;
    }
    selectedPart->setLightFactor(factor);
    emit statusUpdateMessage(
        tr("Light (leaf %1): %2")
            .arg(selectedPart->data(0).toString())
            .arg(factor, 0, 'f', 2),
        0);
  } else {
    applyLightFactorToTree(index, factor);
    emit statusUpdateMessage(
        tr("Light (subtree of %1): %2")
            .arg(selectedPart ? selectedPart->data(0).toString()
                              : QStringLiteral("(root)"))
            .arg(factor, 0, 'f', 2),
        0);
  }
  renderWindow->Render();
  scheduleVRSync();
}

void MainWindow::setupLighting() {
  sceneLight = vtkSmartPointer<vtkLight>::New();
  sceneLight->SetLightTypeToHeadlight();
  sceneLight->SetIntensity(1.0);
  renderer->AddLight(sceneLight);
  renderer->SetAmbient(0.2, 0.2, 0.2);
}

void MainWindow::onShrinkSliderChanged(int value) {
  /* Slider 0   -> factor 1.0 (no shrink).
   * Slider 100 -> factor 0.0 (max shrink). */
  double factor = (100.0 - value) / 100.0;

  /* Cascade semantics:
   *   - "Apply to all" ticked: walk from the root (every loaded part).
   *   - Otherwise: walk the selected subtree. applyShrinkFactorToTree
   *     touches the index AND every descendant, skipping non-STL rows -
   *     so picking a folder shrinks every leaf underneath, picking a
   *     leaf shrinks only that leaf. */
  const bool applyToAll = ui->applyAllShrinkCheckBox &&
                          ui->applyAllShrinkCheckBox->isChecked();
  if (applyToAll) {
    applyShrinkFactorToTree(QModelIndex(), factor);
    renderWindow->Render();
    scheduleVRSync();
    emit statusUpdateMessage(
        tr("Shrink (all parts): %1").arg(factor, 0, 'f', 2), 0);
    return;
  }

  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Select an item before adjusting the shrink slider."), 0);
    return;
  }

  applyShrinkFactorToTree(index, factor);
  renderWindow->Render();
  scheduleVRSync();
  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
  emit statusUpdateMessage(
      tr("Shrink %1: %2")
          .arg(selectedPart ? selectedPart->data(0).toString()
                            : QStringLiteral("(subtree)"))
          .arg(factor, 0, 'f', 2),
      0);
}

void MainWindow::onClipSliderChanged(int value) {
  /* Cascade semantics:
   *   - "Apply to all" ticked: walk from the root (every loaded part).
   *   - Otherwise: walk the selected subtree via applyClipSliderToTree.
   *     That helper recomputes each part's actualX from its OWN
   *     original X bounds (cached at load time, not the live actor
   *     bounds which shrink as soon as the clip filter cuts geometry),
   *     so the slider value maps to the right local cut on every leaf.
   *     Folder selection clips every leaf underneath; leaf selection
   *     clips only that leaf. */
  const bool applyToAll =
      ui->applyAllClipCheckBox && ui->applyAllClipCheckBox->isChecked();
  if (applyToAll) {
    applyClipSliderToTree(QModelIndex(), value);
    renderWindow->Render();
    scheduleVRSync();
    emit statusUpdateMessage(tr("Clip (all parts): slider=%1").arg(value), 0);
    return;
  }

  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Select an item before adjusting the clip slider."), 0);
    return;
  }

  applyClipSliderToTree(index, value);
  renderWindow->Render();
  scheduleVRSync();
  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
  emit statusUpdateMessage(
      tr("Clip %1: slider=%2")
          .arg(selectedPart ? selectedPart->data(0).toString()
                            : QStringLiteral("(subtree)"))
          .arg(value),
      0);
}

void MainWindow::setVisibilityRecursive(const QModelIndex &index,
                                        bool visible) {
  if (!index.isValid())
    return;
  ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
  if (!part)
    return;

  /* Route through ModelPartList::setData so the tree view repaints
   * the "Visible?" column. setData internally calls part->set(1, ...)
   * which keeps m_itemData and the isVisible flag in sync, then
   * emits dataChanged. We follow up with part->setVisible(visible)
   * because set() only flips the flag - it doesn't touch the actor.
   * setVisible() is the single place that calls SetVisibility on the
   * vtkActor, so calling it explicitly here is what actually hides
   * the geometry in the desktop renderer. */
  partList->setData(index.siblingAtColumn(1), visible ? "Yes" : "No",
                    Qt::EditRole);
  part->setVisible(visible);

  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    setVisibilityRecursive(partList->index(i, 0, index), visible);
}

void MainWindow::onVisibilityToggled(bool checked) {
  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Select an item before toggling visibility."), 0);
    return;
  }
  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());

  /* Cascade the toggle into every descendant. This makes folder
   * rows useful (un-tick "Models/Engine" and the whole assembly
   * disappears) and keeps individual STL toggles working as
   * before because leaves have no children to recurse into. */
  setVisibilityRecursive(index, checked);

  renderWindow->Render();
  /* Marksheet: real-time VR update on visibility change. */
  scheduleVRSync();

  const QString name =
      selectedPart ? selectedPart->data(0).toString() : tr("(unknown)");
  emit statusUpdateMessage(checked ? tr("Showing: %1").arg(name)
                                   : tr("Hiding: %1").arg(name),
                           0);
}

void MainWindow::onSyncVRClicked() {
  if (!vrThread || !vrThread->isRunning()) {
    emit statusUpdateMessage(
        tr("Sync VR: VR is not running. Click Start VR first."), 0);
    return;
  }

  /* Manual button: skip the debounce, sync right now. */
  doVRSync();
  emit statusUpdateMessage(tr("Synced current parts to VR"), 0);
}

void MainWindow::scheduleVRSync() {
  /* No-op when VR is not running - we never auto-start a session for
   * the user, only push into a session they explicitly opened. */
  if (!vrThread || !vrThread->isRunning())
    return;
  /* Restart the timer. start() on an active QTimer just resets the
   * countdown, so a stream of GUI changes (slider drags, multiple
   * visibility toggles) collapses into one sync once the user pauses. */
  if (m_vrSyncDebounce)
    m_vrSyncDebounce->start();
}

void MainWindow::doVRSync() {
  if (!vrThread || !vrThread->isRunning())
    return;
  /* CLEAR_SCENE drops every model actor in the VR thread (the floor and
   * skybox are re-added inside the thread), then pushTreeActorsToVR
   * walks the GUI tree and queues a fresh actor per visible part.
   * getNewActor() rebuilds the filter chain from the current ModelPart
   * state on every call - that's what carries colour, shrink, clip,
   * opacity, and visibility into VR. */
  vrThread->issueCommand(VRRenderThread::CLEAR_SCENE, 0.0);
  /* Stale actor pointers from the previous push are about to be
   * destroyed VR-side. Wipe the actor->part map so the next
   * actorMovedInVR doesn't dereference a dead actor. */
  m_vrActorToPart.clear();
  pushTreeActorsToVR(QModelIndex());
}

double MainWindow::computeCameraAzimuth() const {
  if (!renderer)
    return 0.0;
  vtkCamera *cam = renderer->GetActiveCamera();
  if (!cam)
    return 0.0;
  double pos[3], focal[3];
  cam->GetPosition(pos);
  cam->GetFocalPoint(focal);
  /* Azimuth = horizontal angle of the camera around the focal point.
   * Project the (camera - focal) vector onto the XZ plane and read its
   * angle. atan2(x, z) means angle 0 looks down +Z and positive rotates
   * toward +X (clockwise when viewed from +Y). The sign here matches
   * what the trackball produces, so a horizontal drag of the desktop
   * view spins the VR model the same direction. */
  const double vx = pos[0] - focal[0];
  const double vz = pos[2] - focal[2];
  return std::atan2(vx, vz) * 180.0 / 3.14159265358979323846;
}

double MainWindow::computeCameraElevation() const {
  if (!renderer)
    return 0.0;
  vtkCamera *cam = renderer->GetActiveCamera();
  if (!cam)
    return 0.0;
  double pos[3], focal[3];
  cam->GetPosition(pos);
  cam->GetFocalPoint(focal);
  /* Elevation = vertical angle of the camera vector. Compare the Y
   * component to the XZ plane radius. We send this as a pitch nudge
   * to the VR thread so tilting the desktop trackball up/down also
   * tilts the VR model up/down. */
  const double vx = pos[0] - focal[0];
  const double vy = pos[1] - focal[1];
  const double vz = pos[2] - focal[2];
  const double rxz = std::sqrt(vx * vx + vz * vz);
  return std::atan2(vy, rxz) * 180.0 / 3.14159265358979323846;
}

void MainWindow::syncCameraToVR() {
  /* Belt and braces: the timer is stopped on Stop VR / close, but if
   * the VR thread crashed mid-session the isRunning() check still
   * keeps us from sending into a dead thread. */
  if (!vrThread || !vrThread->isRunning())
    return;

  double az = computeCameraAzimuth();
  double yawDelta = az - m_lastSyncedAzimuth;
  /* Wrap the delta into (-180, 180] so spinning past the +/-180
   * boundary doesn't make the VR scene snap 360 degrees the wrong
   * way. */
  while (yawDelta > 180.0)
    yawDelta -= 360.0;
  while (yawDelta < -180.0)
    yawDelta += 360.0;

  /* 0.1 deg dead-band: ignore rounding noise from atan2 when the
   * camera isn't actually moving, so we don't dribble RotateY(0.001)s
   * into the VR thread every tick. */
  if (std::abs(yawDelta) > 0.1) {
    vrThread->issueCommand(VRRenderThread::ROTATE_BY_Y, yawDelta);
    m_lastSyncedAzimuth = az;
  }

  double el = computeCameraElevation();
  double pitchDelta = el - m_lastSyncedElevation;
  /* Elevation is bounded to (-90, 90) so no 180 wrap needed. */
  if (std::abs(pitchDelta) > 0.1) {
    vrThread->issueCommand(VRRenderThread::ROTATE_BY_X, pitchDelta);
    m_lastSyncedElevation = el;
  }
}

void MainWindow::onVRActorMoved(void *actor, double dxVR, double dyVR,
                                double dzVR) {
  /* The VR thread saw a controller drag move this actor by (dxVR,
   * dyVR, dzVR) metres in the VR world frame (post-rotation,
   * post-scale). Mirror it back to the desktop side so the user
   * doesn't see the part lock to the headset and snap back to its
   * old desktop position the next time something triggers a sync.
   *
   * Two coordinate transforms to undo:
   *   1. addActorOffline applies SetScale(kModelScale) - so 1 VR
   *      metre corresponds to (1 / kModelScale) GUI mm.
   *   2. addActorOffline applies RotateX(-90), which maps GUI vector
   *      (x, y, z) -> VR vector (x, z, -y). The inverse is
   *      VR (x_v, y_v, z_v) -> GUI (x_v, -z_v, y_v). */
  auto it = m_vrActorToPart.find(actor);
  if (it == m_vrActorToPart.end())
    return;
  ModelPart *part = it.value();
  if (!part)
    return;

  const double inv = 1.0 / VRRenderThread::kModelScale;
  const double dxGui = dxVR * inv;
  const double dyGui = -dzVR * inv;
  const double dzGui = dyVR * inv;

  double curOff[3];
  part->getExplodeOffset(curOff);
  /* setExplodeOffset both updates m_explodeOffset and re-positions
   * the GUI actor, so the desktop view follows the drag without an
   * explicit ResetCamera or Render-from-here. */
  part->setExplodeOffset(curOff[0] + dxGui, curOff[1] + dyGui,
                         curOff[2] + dzGui);
  renderWindow->Render();
  /* Deliberately NO scheduleVRSync() here: the VR actor is already
   * at the new position (that's what fired the signal); a sync
   * would CLEAR_SCENE + push fresh actors during an ongoing
   * controller interaction, causing a visible flicker and possibly
   * dropping the user's grip mid-drag. */
}

void MainWindow::onToggleVRRotation() {
  if (!vrThread || !vrThread->isRunning()) {
    /* Keep the toggle button visually consistent with the actual state. */
    ui->vrRotateButton->blockSignals(true);
    ui->vrRotateButton->setChecked(false);
    ui->vrRotateButton->setText(tr("Start Rotation (R)"));
    ui->vrRotateButton->blockSignals(false);
    emit statusUpdateMessage(
        tr("Toggle VR Rotation: VR is not running."), 0);
    return;
  }

  vrRotating = !vrRotating;
  /* 1 degree per ~20ms tick = ~50 deg/s. Comfortable speed for the
   * demo; not so fast it makes anyone seasick in the headset. */
  vrThread->issueCommand(VRRenderThread::ROTATE_Y, vrRotating ? 1.0 : 0.0);

  /* Keep button state synced when triggered from menu / keyboard. */
  ui->vrRotateButton->blockSignals(true);
  ui->vrRotateButton->setChecked(vrRotating);
  ui->vrRotateButton->setText(vrRotating ? tr("Stop Rotation (R)")
                                         : tr("Start Rotation (R)"));
  ui->vrRotateButton->blockSignals(false);

  emit statusUpdateMessage(
      vrRotating ? tr("VR rotation: ON (Y axis)")
                 : tr("VR rotation: OFF"),
      0);
}

int MainWindow::pushTreeActorsToVR(const QModelIndex &index) {
  if (!vrThread)
    return 0;

  int pushed = 0;

  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && part->visible() && !part->getStlPath().isEmpty()) {
      vtkSmartPointer<vtkActor> vrActor = part->getNewActor();
      if (vrActor) {
        vrThread->addActorOffline(vrActor);
        /* Remember actor->part so VRRenderThread::actorMovedInVR
         * (emitted when the user grip-drags this actor with a VR
         * controller) can find the matching ModelPart and translate
         * the drag back into GUI coordinates. */
        m_vrActorToPart.insert(static_cast<void *>(vrActor.Get()), part);
        ++pushed;
      } else {
        qWarning() << "pushTreeActorsToVR: getNewActor() returned null for"
                   << part->data(0).toString()
                   << "- STL may not have loaded; skipping.";
      }
    }
  }

  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i) {
    pushed += pushTreeActorsToVR(partList->index(i, 0, index));
  }
  return pushed;
}

void MainWindow::collectTreeStats(const QModelIndex &index, int depth,
                                  TreeStats &out) const {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part) {
      ++out.totalParts;
      const QString name = part->data(0).toString();
      const QString stl = part->getStlPath();
      const bool hasStl = !stl.isEmpty();
      const bool vis = part->visible();
      const bool hasActor = (part->getActor() != nullptr);
      const vtkIdType tris = part->getTriangleCount();
      if (hasStl)
        ++out.withStl;
      if (hasStl && vis)
        ++out.visibleWithStl;
      if (hasActor)
        ++out.withActor;
      if (tris > 0)
        ++out.withReader;

      QString line = QString("  ").repeated(depth);
      line += QStringLiteral("- \"%1\"  visible=%2  STL=%3  actor=%4  tris=%5")
                  .arg(name)
                  .arg(vis ? "Y" : "N")
                  .arg(hasStl ? "set" : "(empty)")
                  .arg(hasActor ? "set" : "(null)")
                  .arg(qlonglong(tris));
      if (hasStl)
        line += QStringLiteral("\n%1   path: %2")
                    .arg(QString("  ").repeated(depth))
                    .arg(stl);
      out.partLines << line;
    }
  }

  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    collectTreeStats(partList->index(i, 0, index), depth + 1, out);
}

QString MainWindow::buildVRDiagnostic(const QString &reason) const {
  QString r;
  r += QStringLiteral("=== VR Pipeline Diagnostic ===\n");
  r += QStringLiteral("Time: %1\n")
           .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
  r += QStringLiteral("Reason: %1\n\n").arg(reason);

  /* --- GUI tree state --- */
  TreeStats stats;
  collectTreeStats(QModelIndex(), 0, stats);

  r += QStringLiteral("[GUI Tree]\n");
  r += QStringLiteral("  Total parts in tree         : %1\n").arg(stats.totalParts);
  r += QStringLiteral("  Parts with STL path set     : %1\n").arg(stats.withStl);
  r += QStringLiteral("  Parts visible AND with STL  : %1\n").arg(stats.visibleWithStl);
  r += QStringLiteral("  Parts with vtkActor created : %1\n").arg(stats.withActor);
  r += QStringLiteral("  Parts with reader/triangles : %1\n\n").arg(stats.withReader);

  if (stats.partLines.isEmpty())
    r += QStringLiteral("  (tree is empty - no parts loaded)\n\n");
  else {
    r += QStringLiteral("[Tree Walk]\n");
    for (const QString &line : stats.partLines)
      r += line + QChar('\n');
    r += QChar('\n');
  }

  /* --- VR thread / list state --- */
  r += QStringLiteral("[VR Thread]\n");
  if (!vrThread) {
    r += QStringLiteral("  vrThread                    : NOT ALLOCATED\n");
    r += QStringLiteral("  pendingActors list          : N/A\n");
  } else {
    r += QStringLiteral("  vrThread                    : allocated (%1)\n")
             .arg(reinterpret_cast<quintptr>(vrThread), 0, 16);
    r += QStringLiteral("  vrThread running            : %1\n")
             .arg(vrThread->isRunning() ? "yes" : "no");
    r += QStringLiteral("  pendingActors items waiting : %1  %2\n")
             .arg(vrThread->pendingActorCount())
             .arg(vrThread->pendingActorCount() == -1
                      ? "(list pointer is NULL - bug)"
                      : "");
    int drained = vrThread->initialDrainCount();
    r += QStringLiteral("  initial drain count         : %1\n")
             .arg(drained == -1
                      ? QStringLiteral("(run() has not drained yet)")
                      : QString::number(drained));
  }
  r += QChar('\n');

  /* --- Interpretation hints --- */
  r += QStringLiteral("[Where it broke]\n");
  if (stats.totalParts == 0)
    r += QStringLiteral("  * No parts in the tree at all. Use File > Open File / "
                       "Open Folder to load STL(s).\n");
  else if (stats.withStl == 0)
    r += QStringLiteral("  * Parts exist but none have an STL path. The tree "
                       "has folder/group rows only. Add STLs to a folder.\n");
  else if (stats.visibleWithStl == 0)
    r += QStringLiteral("  * STL parts exist but every one is hidden (visible=N). "
                       "Toggle their visibility checkbox in the tree.\n");
  else if (stats.withReader == 0)
    r += QStringLiteral("  * Visible STL parts exist but none have a populated "
                       "reader/triangles. loadSTL() probably failed silently. "
                       "Re-open the file and watch the status bar / log.\n");
  else if (vrThread && vrThread->pendingActorCount() == 0 &&
           vrThread->initialDrainCount() == -1)
    r += QStringLiteral("  * Tree is fine but pendingActors is still empty. "
                       "pushTreeActorsToVR walked the tree without pushing "
                       "anything - check getNewActor() returned non-null.\n");
  else
    r += QStringLiteral("  * Tree and list look populated. If the model still "
                       "isn't visible in VR, the issue is downstream of the "
                       "list (camera position, actor scale, or the renderer "
                       "didn't get the actor). Check VTK log and crash.log.\n");

  r += QChar('\n');
  r += QStringLiteral("[Logs]\n");
  r += QStringLiteral("  crash.log : %LOCALAPPDATA%\\ws6\\crash.log\n");
  r += QStringLiteral("  vtk_log   : %TEMP%\\ws6_vtk_log.txt\n");

  return r;
}

void MainWindow::showVRDiagnosticDialog(const QString &reason) {
  const QString report = buildVRDiagnostic(reason);

  QDialog dlg(this);
  dlg.setWindowTitle(tr("VR Pipeline Diagnostic"));
  dlg.resize(820, 560);

  auto *layout = new QVBoxLayout(&dlg);

  auto *header = new QLabel(
      tr("<b>VR could not load the model into its actor list.</b><br>"
         "Below is the full state of the tree and the list. Click "
         "<b>Copy to Clipboard</b> to paste it in a chat / email."),
      &dlg);
  header->setWordWrap(true);
  layout->addWidget(header);

  auto *view = new QPlainTextEdit(&dlg);
  view->setReadOnly(true);
  view->setLineWrapMode(QPlainTextEdit::NoWrap);
  view->setPlainText(report);
  /* Monospace so the column-aligned report stays readable. */
  QFont mono(QStringLiteral("Consolas"));
  mono.setStyleHint(QFont::TypeWriter);
  view->setFont(mono);
  layout->addWidget(view, 1);

  auto *buttons = new QDialogButtonBox(&dlg);
  auto *copyBtn =
      buttons->addButton(tr("Copy to Clipboard"), QDialogButtonBox::ActionRole);
  auto *saveBtn =
      buttons->addButton(tr("Save to File..."), QDialogButtonBox::ActionRole);
  buttons->addButton(QDialogButtonBox::Close);

  connect(copyBtn, &QPushButton::clicked, this, [report, copyBtn]() {
    QApplication::clipboard()->setText(report);
    copyBtn->setText(tr("Copied!"));
  });
  connect(saveBtn, &QPushButton::clicked, this, [this, report]() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save VR Diagnostic"),
        QStringLiteral("vr_diagnostic_%1.txt")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        tr("Text Files (*.txt)"));
    if (path.isEmpty())
      return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      f.write(report.toUtf8());
      emit statusUpdateMessage(tr("Saved diagnostic to %1").arg(path), 0);
    } else {
      QMessageBox::warning(this, tr("Save failed"),
                           tr("Could not write to %1").arg(path));
    }
  });
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::close);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::close);
  layout->addWidget(buttons);

  dlg.exec();
}
void MainWindow::applyTheme(bool enabled) {
  darkMode = enabled;

  if (enabled) {
    /* Drop the .ui-baked light stylesheet from the QMainWindow itself.
     * It cascades to every child and outranks qApp's stylesheet, so
     * leaving it in place would keep the menu bar / buttons / sliders
     * looking light even in night mode. */
    this->setStyleSheet("");

    qApp->setStyleSheet(R"(
      QMainWindow, QWidget, QDialog {
        background-color: #202124;
        color: #e8eaed;
      }

      QMenuBar {
        background-color: #2b2d31;
        color: #e8eaed;
        border-bottom: 1px solid #3c4043;
      }
      QMenuBar::item {
        background: transparent;
        padding: 4px 10px;
      }
      QMenuBar::item:selected {
        background-color: #3c4043;
      }

      QMenu {
        background-color: #2b2d31;
        color: #e8eaed;
        border: 1px solid #3c4043;
      }
      QMenu::item:selected {
        background-color: #4a90e2;
        color: #ffffff;
      }
      QMenu::separator {
        height: 1px;
        background: #3c4043;
        margin: 4px 8px;
      }

      QStatusBar {
        background-color: #2b2d31;
        color: #e8eaed;
      }
      QStatusBar::item { border: none; }

      QToolBar {
        background-color: #2b2d31;
        border-bottom: 1px solid #3c4043;
        spacing: 2px;
        padding: 2px;
      }

      QPushButton {
        background-color: #3c4043;
        color: #e8eaed;
        border: 1px solid #5f6368;
        border-radius: 4px;
        padding: 4px 12px;
        min-height: 22px;
      }
      QPushButton:hover {
        background-color: #4a4d52;
        border-color: #4a90e2;
      }
      QPushButton:pressed {
        background-color: #5f6368;
      }
      QPushButton:disabled {
        background-color: #2a2b2e;
        color: #6b6f74;
        border-color: #3c4043;
      }

      QTreeView, QTableView, QListView {
        background-color: #1f1f1f;
        color: #e8eaed;
        alternate-background-color: #2b2d31;
        border: 1px solid #5f6368;
        selection-background-color: #4a90e2;
        selection-color: #ffffff;
      }
      QTreeView::item { padding: 2px 0; }

      QHeaderView::section {
        background-color: #2b2d31;
        color: #e8eaed;
        border: 1px solid #5f6368;
        padding: 4px;
      }

      QLineEdit, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit, QComboBox {
        background-color: #1f1f1f;
        color: #e8eaed;
        border: 1px solid #5f6368;
        selection-background-color: #4a90e2;
        padding: 1px 2px;
      }

      QGroupBox {
        font-weight: 600;
        color: #e8eaed;
        border: 1px solid #5f6368;
        border-radius: 6px;
        margin-top: 9px;
        padding: 8px 6px 5px 6px;
        background-color: #2b2d31;
      }
      QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        left: 8px;
        padding: 0 5px;
        color: #87bce7;
      }

      QSlider::groove:horizontal {
        height: 5px;
        background: #5f6368;
        border-radius: 2px;
      }
      QSlider::sub-page:horizontal {
        background: #4a90e2;
        border-radius: 2px;
      }
      QSlider::handle:horizontal {
        background: #e8eaed;
        width: 13px;
        margin: -5px 0;
        border: 1px solid #4a90e2;
        border-radius: 6px;
      }
      QSlider::handle:horizontal:hover {
        background: #87bce7;
      }

      QCheckBox, QRadioButton, QLabel {
        color: #e8eaed;
        background: transparent;
      }

      QScrollBar:vertical, QScrollBar:horizontal {
        background: #2b2d31;
        border: none;
      }
      QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
        background: #5f6368;
        border-radius: 3px;
      }
      QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
        background: #7a7e84;
      }
      QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }

      QSplitter::handle { background: #3c4043; }

      QToolTip {
        background-color: #2b2d31;
        color: #e8eaed;
        border: 1px solid #5f6368;
      }
    )");

    if (renderer) {
      renderer->SetBackground(0.05, 0.06, 0.08);
      renderer->SetBackground2(0.12, 0.14, 0.18);
      renderer->GradientBackgroundOn();
    }

    emit statusUpdateMessage(tr("Night mode enabled"), 0);

  } else {
    /* Drop the global dark stylesheet, then put the original .ui light
     * theme back on the QMainWindow so children re-inherit it. */
    qApp->setStyleSheet("");
    this->setStyleSheet(originalStyleSheet);

    if (renderer) {
      renderer->SetBackground(0.74, 0.77, 0.82);
      renderer->SetBackground2(0.18, 0.22, 0.30);
      renderer->GradientBackgroundOn();
    }

    emit statusUpdateMessage(tr("Night mode disabled"), 0);
  }

  if (renderWindow) {
    renderWindow->Render();
  }
}

void MainWindow::onExplodeButtonClicked(bool checked) {
  /* Lazy-allocate the animation timer the first time the user clicks
   * the button. 30Hz (~33ms) is plenty smooth for a translation
   * animation and keeps the VTK render cost off the critical path. */
  if (!explodeTimer) {
    explodeTimer = new QTimer(this);
    explodeTimer->setInterval(33);
    connect(explodeTimer, &QTimer::timeout, this,
            &MainWindow::onExplodeAnimTick);
  }

  /* Re-target the animation. If the user re-clicks mid-animation, we
   * pick up from the current progress rather than snapping, which
   * makes the explode/collapse feel responsive. */
  m_explodeStart = m_explodeProgress;
  m_explodeTarget = checked ? 1.0 : 0.0;
  explodeClock.restart();
  ui->explodeButton->setText(checked ? tr("Collapse") : tr("Explode"));

  if (!explodeTimer->isActive())
    explodeTimer->start();

  emit statusUpdateMessage(checked ? tr("Exploding parts...")
                                   : tr("Collapsing parts..."),
                           0);
}

void MainWindow::onExplodeModeChanged(int index) {
  /* Switching axis mid-state: re-apply current progress under the
   * new mode so the visual snaps to whatever the new direction
   * means. (E.g. exploded spherical -> X-axis collapses Y/Z.) */
  m_explodeMode = index;
  applyExplosion(m_explodeProgress, m_explodeMode);
  renderWindow->Render();

  /* Push to VR only if we're not in the middle of an animation -
   * the per-tick handler does its own end-of-anim VR sync. */
  if (!explodeTimer || !explodeTimer->isActive())
    scheduleVRSync();
  emit statusUpdateMessage(
      tr("Explode mode: %1").arg(ui->explodeModeBox->currentText()), 0);
}

void MainWindow::onExplodeAnimTick() {
  /* Linearly interpolate (with a smoothstep ease) from m_explodeStart
   * to m_explodeTarget over kExplodeDurationMs of wall time. Using
   * elapsed wall time rather than tick count means the animation
   * still finishes in ~1s even if the GUI is briefly busy and a
   * tick or two is dropped. */
  qint64 elapsed = explodeClock.elapsed();
  double t = double(elapsed) / double(kExplodeDurationMs);
  if (t >= 1.0)
    t = 1.0;

  /* Smoothstep easing: t' = t^2 * (3 - 2t). Slows in/out so the
   * parts don't snap on takeoff/landing. */
  double eased = t * t * (3.0 - 2.0 * t);
  double progress =
      m_explodeStart + (m_explodeTarget - m_explodeStart) * eased;

  m_explodeProgress = progress;
  applyExplosion(progress, m_explodeMode);
  renderWindow->Render();

  if (t >= 1.0) {
    /* Animation done - stop the timer and push the final state to
     * VR in one go. Doing this every tick would thrash the VR
     * thread's actor list. */
    explodeTimer->stop();
    scheduleVRSync();
    emit statusUpdateMessage(
        m_explodeTarget > 0.5 ? tr("Exploded view ready (%1)")
                                    .arg(ui->explodeModeBox->currentText())
                              : tr("Parts back in place"),
        0);
  }
}

void MainWindow::collectExplodeBounds(const QModelIndex &index,
                                      double bounds[6], bool &any) const {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && part->visible() && !part->getStlPath().isEmpty()) {
      double pb[6];
      part->getOriginalBounds(pb);
      /* Skip parts whose bounds were never populated (loadSTL never
       * succeeded). pb stays at the default {0,0,0,0,0,0} which is
       * a degenerate point at the origin and would corrupt the union. */
      bool nonDegenerate = (pb[1] > pb[0]) || (pb[3] > pb[2]) ||
                           (pb[5] > pb[4]);
      if (nonDegenerate) {
        if (!any) {
          for (int i = 0; i < 6; ++i)
            bounds[i] = pb[i];
          any = true;
        } else {
          bounds[0] = std::min(bounds[0], pb[0]);
          bounds[1] = std::max(bounds[1], pb[1]);
          bounds[2] = std::min(bounds[2], pb[2]);
          bounds[3] = std::max(bounds[3], pb[3]);
          bounds[4] = std::min(bounds[4], pb[4]);
          bounds[5] = std::max(bounds[5], pb[5]);
        }
      }
    }
  }
  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    collectExplodeBounds(partList->index(i, 0, index), bounds, any);
}

void MainWindow::translatePartsForExplosion(const QModelIndex &index,
                                            const double globalCentre[3],
                                            double diag, double progress,
                                            int mode) {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && !part->getStlPath().isEmpty()) {
      double pb[6];
      part->getOriginalBounds(pb);
      bool nonDegenerate = (pb[1] > pb[0]) || (pb[3] > pb[2]) ||
                           (pb[5] > pb[4]);
      if (nonDegenerate) {
        double cx = 0.5 * (pb[0] + pb[1]);
        double cy = 0.5 * (pb[2] + pb[3]);
        double cz = 0.5 * (pb[4] + pb[5]);
        double dx = cx - globalCentre[0];
        double dy = cy - globalCentre[1];
        double dz = cz - globalCentre[2];

        double offX = 0.0, offY = 0.0, offZ = 0.0;

        switch (mode) {
        case ExplodeSpherical: {
          /* Each part flies along the unit vector from the global
           * centre to its own centre, by progress * diagonal. Using
           * the diagonal as the scale keeps the spread proportional
           * to the model size so it doesn't fly off-screen on big
           * assemblies or look pointless on small ones. */
          double len = std::sqrt(dx * dx + dy * dy + dz * dz);
          if (len < 1e-9) {
            /* Part sits exactly on the centre (single-part scene
             * or symmetric layout). Pick a stable direction so it
             * still moves visibly. */
            dx = 1.0;
            dy = 0.0;
            dz = 0.0;
            len = 1.0;
          }
          double scale = progress * diag;
          offX = (dx / len) * scale;
          offY = (dy / len) * scale;
          offZ = (dz / len) * scale;
          break;
        }
        case ExplodeX:
          /* Spread along X only; (P.x - C.x) * progress means at
           * progress 1 each part is twice as far from centre as
           * before. Y/Z stay put. */
          offX = dx * progress;
          break;
        case ExplodeY:
          offY = dy * progress;
          break;
        case ExplodeZ:
          offZ = dz * progress;
          break;
        }

        part->setExplodeOffset(offX, offY, offZ);
      }
    }
  }
  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    translatePartsForExplosion(partList->index(i, 0, index), globalCentre,
                               diag, progress, mode);
}

void MainWindow::applyExplosion(double progress, int mode) {
  /* Compute the union of every visible part's original bounds. We
   * use the cached pre-filter bounds (NOT the actor's runtime
   * bounds) so that previous explosion offsets and any active
   * shrink/clip filters don't drift the centre between calls. */
  double bounds[6] = {0, 0, 0, 0, 0, 0};
  bool any = false;
  collectExplodeBounds(QModelIndex(), bounds, any);

  if (!any) {
    /* Tree is empty / no STL bounds available - nothing to explode. */
    return;
  }

  double centre[3] = {0.5 * (bounds[0] + bounds[1]),
                      0.5 * (bounds[2] + bounds[3]),
                      0.5 * (bounds[4] + bounds[5])};

  double dx = bounds[1] - bounds[0];
  double dy = bounds[3] - bounds[2];
  double dz = bounds[5] - bounds[4];
  double diag = std::sqrt(dx * dx + dy * dy + dz * dz);

  translatePartsForExplosion(QModelIndex(), centre, diag, progress, mode);
}

void MainWindow::applyOpacityToTree(const QModelIndex &index,
                                    double opacity) {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && !part->getStlPath().isEmpty())
      part->setOpacity(opacity);
  }
  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    applyOpacityToTree(partList->index(i, 0, index), opacity);
}

void MainWindow::applyColourToTree(const QModelIndex &index, unsigned char R,
                                   unsigned char G, unsigned char B) {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && !part->getStlPath().isEmpty()) {
      part->setColour(R, G, B);
      if (part->getActor())
        part->getActor()->GetProperty()->SetColor(R / 255.0, G / 255.0,
                                                  B / 255.0);
    }
  }
  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    applyColourToTree(partList->index(i, 0, index), R, G, B);
}

void MainWindow::applyShrinkFactorToTree(const QModelIndex &index,
                                         double factor) {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && !part->getStlPath().isEmpty())
      part->setShrinkFactor(factor);
  }
  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    applyShrinkFactorToTree(partList->index(i, 0, index), factor);
}

void MainWindow::applyClipSliderToTree(const QModelIndex &index,
                                       int sliderValue) {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && !part->getStlPath().isEmpty() && part->getActor()) {
      /* Each part has its own original X bounds, so we can't share one
       * actualX value across the tree - recompute per-part exactly the
       * way onClipSliderChanged does for a single part. */
      double bounds[6];
      part->getOriginalBounds(bounds);
      double minX = bounds[0];
      double maxX = bounds[1];
      double width = maxX - minX;
      double actualX;
      if (sliderValue == 0)
        actualX = minX - 0.1 * width;
      else
        actualX = minX + (double(sliderValue) / 100.0) * width;
      part->applyClipping(actualX);
    }
  }
  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    applyClipSliderToTree(partList->index(i, 0, index), sliderValue);
}

void MainWindow::applyLightFactorToTree(const QModelIndex &index,
                                        double factor) {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && !part->getStlPath().isEmpty())
      part->setLightFactor(factor);
  }
  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i)
    applyLightFactorToTree(partList->index(i, 0, index), factor);
}

void MainWindow::onOpacitySliderChanged(int value) {
  /* Slider 0..100 maps to 0.0..1.0. Below ~5% the parts vanish into
   * the background and look like a render bug, so clamp the lower
   * end to 0.05 - the slider can still go to 0 but visually it stops
   * at "barely visible" rather than "gone". */
  double opacity = value / 100.0;
  if (opacity < 0.05 && value > 0)
    opacity = 0.05;
  applyOpacityToTree(QModelIndex(), opacity);
  renderWindow->Render();
  scheduleVRSync();
  emit statusUpdateMessage(
      value >= 100 ? tr("X-ray off (solid)")
                   : tr("X-ray opacity: %1%").arg(value),
      0);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  /* We only care about mouse events on the VTK render widget. Other
   * events (or events on other widgets) pass through unchanged. */
  if (watched == ui->vtkWidget) {
    if (event->type() == QEvent::MouseButtonPress) {
      QMouseEvent *me = static_cast<QMouseEvent *>(event);
      if (me->button() == Qt::LeftButton) {
        /* Plain LMB on the currently-selected part = "move this part"
         * (auto-discoverable: select via tree row OR by clicking the
         * part once, then press-and-drag to translate). If the cursor
         * is over an unselected part - or over empty space - fall
         * through to the camera trackball + click-to-pick logic.
         *
         * Shift+LMB is a one-step "grab anything under the cursor
         * without pre-selecting" shortcut. */
        const bool shiftHeld = me->modifiers() & Qt::ShiftModifier;
        if (startPartDrag(me->pos(), /*requireSelected=*/!shiftHeld)) {
          return true;
        }
        /* Don't consume the press - VTK's interactor still needs it
         * to start a rotate/pan if the user goes on to drag. We just
         * remember where the press happened so we can decide on
         * release whether this was a click or a drag. */
        m_pressPos = me->pos();
        m_pressTracked = true;
      }
    } else if (event->type() == QEvent::MouseMove) {
      if (m_partDragging) {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        updatePartDrag(me->pos());
        return true; /* consume so VTK doesn't also pan/rotate */
      }
    } else if (event->type() == QEvent::MouseButtonRelease) {
      QMouseEvent *me = static_cast<QMouseEvent *>(event);
      if (me->button() == Qt::LeftButton) {
        if (m_partDragging) {
          /* Drop the part where the user let go and propagate the
           * new position into VR. */
          m_partDragging = false;
          QString name = m_dragPart ? m_dragPart->data(0).toString()
                                    : QStringLiteral("(unknown)");
          m_dragPart = nullptr;
          scheduleVRSync();
          emit statusUpdateMessage(
              tr("Moved: %1 (drag the same part again to move further)").arg(name), 0);
          return true;
        }
        if (m_pressTracked) {
          m_pressTracked = false;
          /* 5px slack: anything more than that is treated as a drag,
           * which we leave alone (VTK's trackball already rotated
           * the camera during the move). */
          QPoint delta = me->pos() - m_pressPos;
          if (delta.manhattanLength() < 5)
            pickPartAt(me->pos());
        }
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::startPartDrag(const QPoint &pos, bool requireSelected) {
  /* Same coord transform as pickPartAt: VTK uses bottom-left + physical
   * pixels, Qt gives top-left + logical pixels. */
  const double dpr = ui->vtkWidget->devicePixelRatioF();
  const int widgetH = ui->vtkWidget->height();
  const int x = static_cast<int>(pos.x() * dpr);
  const int y = static_cast<int>((widgetH - pos.y() - 1) * dpr);

  vtkSmartPointer<vtkPropPicker> picker =
      vtkSmartPointer<vtkPropPicker>::New();
  if (!picker->Pick(x, y, 0, renderer))
    return false;
  vtkActor *picked = picker->GetActor();
  if (!picked)
    return false;
  QModelIndex idx = findIndexForActor(QModelIndex(), picked);
  if (!idx.isValid())
    return false;
  ModelPart *part = static_cast<ModelPart *>(idx.internalPointer());
  if (!part || !part->getActor())
    return false;

  /* Plain-LMB drag is gated to the already-selected part so a stray
   * left-click on an unselected part (or one that overlaps the camera
   * the user wants to rotate) doesn't accidentally pick it up. */
  if (requireSelected) {
    QModelIndex curr = ui->treeView->currentIndex();
    if (curr != idx)
      return false;
  }

  /* Mirror pickPartAt: select the picked part so the right panel and
   * the rest of the toolbox track what the user is dragging. */
  ui->treeView->setCurrentIndex(idx);

  m_partDragging = true;
  m_dragPart = part;
  m_dragStartScreen = pos;
  part->getExplodeOffset(m_dragStartOffset);

  /* Compute the depth (display-space Z) of the part's centre. We pin
   * the drag to that depth so screen movement maps to a translation on
   * a plane parallel to the camera, not into / out of the screen. The
   * actor's centre = original-bounds centre + current explode offset. */
  double bounds[6];
  part->getOriginalBounds(bounds);
  double cx = (bounds[0] + bounds[1]) / 2.0 + m_dragStartOffset[0];
  double cy = (bounds[2] + bounds[3]) / 2.0 + m_dragStartOffset[1];
  double cz = (bounds[4] + bounds[5]) / 2.0 + m_dragStartOffset[2];
  renderer->SetWorldPoint(cx, cy, cz, 1.0);
  renderer->WorldToDisplay();
  double display[3];
  renderer->GetDisplayPoint(display);
  m_dragDepth = display[2];

  emit statusUpdateMessage(
      tr("Dragging %1 - release the mouse to drop").arg(part->data(0).toString()),
      0);
  return true;
}

void MainWindow::updatePartDrag(const QPoint &pos) {
  if (!m_partDragging || !m_dragPart)
    return;

  const double dpr = ui->vtkWidget->devicePixelRatioF();
  const int widgetH = ui->vtkWidget->height();

  auto displayToWorld = [&](const QPoint &p, double out[3]) {
    double sx = p.x() * dpr;
    double sy = (widgetH - p.y() - 1) * dpr;
    renderer->SetDisplayPoint(sx, sy, m_dragDepth);
    renderer->DisplayToWorld();
    double w[4];
    renderer->GetWorldPoint(w);
    /* DisplayToWorld returns homogeneous coords; normalise. */
    if (w[3] != 0.0) {
      out[0] = w[0] / w[3];
      out[1] = w[1] / w[3];
      out[2] = w[2] / w[3];
    } else {
      out[0] = w[0];
      out[1] = w[1];
      out[2] = w[2];
    }
  };

  double w0[3], wc[3];
  displayToWorld(m_dragStartScreen, w0);
  displayToWorld(pos, wc);

  /* Apply the world-space delta on top of the offset captured when the
   * drag started. setExplodeOffset both stores the value on the part
   * (so the next VR sync picks it up via getNewActor) and immediately
   * positions the GUI actor for the live preview. */
  m_dragPart->setExplodeOffset(m_dragStartOffset[0] + (wc[0] - w0[0]),
                               m_dragStartOffset[1] + (wc[1] - w0[1]),
                               m_dragStartOffset[2] + (wc[2] - w0[2]));
  renderWindow->Render();
}

QModelIndex MainWindow::findIndexForActor(const QModelIndex &index,
                                          vtkActor *target) const {
  if (index.isValid()) {
    ModelPart *part = static_cast<ModelPart *>(index.internalPointer());
    if (part && part->getActor().Get() == target)
      return index;
  }
  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; ++i) {
    QModelIndex found =
        findIndexForActor(partList->index(i, 0, index), target);
    if (found.isValid())
      return found;
  }
  return QModelIndex();
}

bool MainWindow::pickPartAt(const QPoint &pos) {
  /* VTK uses bottom-left origin and works in physical pixels;
   * Qt's QPoint is top-left origin in logical pixels. Translate
   * before handing the coords to vtkPropPicker, otherwise picking
   * is offset on HiDPI displays. */
  const double dpr = ui->vtkWidget->devicePixelRatioF();
  const int widgetH = ui->vtkWidget->height();
  const int x = static_cast<int>(pos.x() * dpr);
  const int y = static_cast<int>((widgetH - pos.y() - 1) * dpr);

  vtkSmartPointer<vtkPropPicker> picker =
      vtkSmartPointer<vtkPropPicker>::New();
  /* PropPicker hits the first actor whose bounding rectangle the
   * ray crosses - that's plenty for "which part did I click on"
   * and far cheaper than vtkCellPicker (which walks every triangle).
   * The third arg 0 is z; ignored for screen-space picks. */
  int hit = picker->Pick(x, y, 0, renderer);
  if (!hit) {
    emit statusUpdateMessage(tr("No part under cursor"), 0);
    return false;
  }

  vtkActor *picked = picker->GetActor();
  if (!picked) {
    emit statusUpdateMessage(tr("Picked something non-pickable"), 0);
    return false;
  }

  QModelIndex idx = findIndexForActor(QModelIndex(), picked);
  if (!idx.isValid()) {
    /* Could be the orientation gizmo, the floor (VR), or any other
     * scene actor that doesn't correspond to a tree row. */
    emit statusUpdateMessage(tr("Picked actor is not a tree part"), 0);
    return false;
  }

  /* Drive the same code path a tree click takes - sets currentIndex
   * AND calls handleTreeClicked so the right panel (sliders,
   * visibility checkbox, status bar) reflects the new selection.
   * Change Colour, Item Options, Toggle Visibility etc. all read
   * currentIndex(), so they immediately operate on the picked part. */
  ui->treeView->setCurrentIndex(idx);
  ui->treeView->scrollTo(idx);
  handleTreeClicked();

  ModelPart *part = static_cast<ModelPart *>(idx.internalPointer());
  emit statusUpdateMessage(
      tr("Picked: %1").arg(part ? part->data(0).toString()
                                  : QStringLiteral("(unknown)")),
      0);
  return true;
}

void MainWindow::onOpacitySolidClicked() {
  /* Snap to fully opaque. setValue() updates the GUI but does NOT emit
   * sliderReleased, so we apply the opacity directly and schedule the
   * VR sync ourselves. The "already at 100" branch covers the case
   * where a part was loaded mid-X-ray and the slider is at 100 but
   * the new actor inherited a fractional opacity. */
  if (ui->opacitySlider->value() == 100)
    applyOpacityToTree(QModelIndex(), 1.0);
  else
    ui->opacitySlider->setValue(100);
  renderWindow->Render();
  scheduleVRSync();
  emit statusUpdateMessage(tr("Opacity reset to solid"), 0);
}

/**
 * @brief Menu/toolbar entry point for saving a viewport screenshot.
 */
void MainWindow::on_actionScreenshot_triggered()
{
    onScreenshotClicked();
}

/**
 * @brief Captures the VTK render window and writes it as a PNG file.
 */
void MainWindow::onScreenshotClicked()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save Screenshot"),
        QDir::homePath() + "/cad_view_screenshot.png",
        tr("PNG Images (*.png)")
    );

    if (fileName.isEmpty()) {
        emit statusUpdateMessage(tr("Screenshot cancelled"), 0);
        return;
    }

    if (!fileName.endsWith(".png", Qt::CaseInsensitive)) {
        fileName += ".png";
    }

    renderWindow->Render();

    vtkSmartPointer<vtkWindowToImageFilter> windowToImage =
        vtkSmartPointer<vtkWindowToImageFilter>::New();

    windowToImage->SetInput(renderWindow);
    windowToImage->SetInputBufferTypeToRGBA();
    windowToImage->ReadFrontBufferOff();
    windowToImage->Update();

    vtkSmartPointer<vtkPNGWriter> writer =
        vtkSmartPointer<vtkPNGWriter>::New();

    writer->SetFileName(fileName.toStdString().c_str());
    writer->SetInputConnection(windowToImage->GetOutputPort());
    writer->Write();

    emit statusUpdateMessage(tr("Screenshot saved: %1").arg(fileName), 0);
}
