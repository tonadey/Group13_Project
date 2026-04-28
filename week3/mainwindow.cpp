#include "mainwindow.h"
#include "OptionDialog.h"
#include "ui_mainwindow.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCaptionActor2D.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLight.h>
#include <vtkNew.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTextProperty.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), vrThread(nullptr) {
  ui->setupUi(this);

  /* Use the bundled VR icon as the window/taskbar icon so the demo
   * doesn't show the generic Qt feather. */
  setWindowIcon(QIcon(":/Icons/icons/startVR.png"));
  refreshWindowTitle();

  ui->openFolderButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
  ui->actionOpen_Folder->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));

  /* Tree-side buttons */
  connect(ui->addItemButton, &QPushButton::released, this,
          &MainWindow::on_actionAdd_Item_triggered);
  connect(ui->removeItemButton, &QPushButton::released, this,
          &MainWindow::on_actionRemove_Item_triggered);
  connect(ui->optionsButton, &QPushButton::released, this,
          &MainWindow::openItemOptions);

  /* Load-models buttons */
  connect(ui->openFileButton, &QPushButton::released, this,
          &MainWindow::on_actionOpen_File_triggered);
  connect(ui->openFolderButton, &QPushButton::released, this,
          &MainWindow::on_actionOpen_Folder_triggered);

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
  connect(ui->visibilityCheckBox, &QCheckBox::toggled, this,
          &MainWindow::onVisibilityToggled);

  /* VR buttons */
  connect(ui->startVRButton, &QPushButton::released, this,
          &MainWindow::on_actionStart_VR_triggered);
  connect(ui->stopVRButton, &QPushButton::released, this,
          &MainWindow::on_actionStop_VR_triggered);
  connect(ui->vrSyncButton, &QPushButton::released, this,
          &MainWindow::onSyncVRClicked);

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

  /* Tree clicks */
  connect(ui->treeView, &QTreeView::clicked, this,
          &MainWindow::handleTreeClicked);

  /* Allocate the ModelList */
  this->partList = new ModelPartList("PartsList");
  ui->treeView->setModel(this->partList);

  /* Right-click context menu on the tree */
  ui->treeView->setContextMenuPolicy(Qt::ActionsContextMenu);
  ui->treeView->addAction(ui->actionItem_Options);
  ui->treeView->addAction(ui->actionAdd_Item);
  ui->treeView->addAction(ui->actionRemove_Item);

  /* --- Hook VTK render window to the Qt widget --- */
  renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
  /* Multi-sample anti-aliasing: smooths polygon edges; 8x is well supported. */
  renderWindow->SetMultiSamples(8);
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
    vrThread->issueCommand(VRRenderThread::END_RENDER, 0.0);
    if (!vrThread->wait(3000)) {
      qWarning() << "VR thread did not exit within 3s; terminating.";
      vrThread->terminate();
      vrThread->wait(1000);
    }
    delete vrThread;
    vrThread = nullptr;
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
    selectedPart->getActor()->SetVisibility(dialog.getVisible());
  }

  updateRender();

  emit statusUpdateMessage(QString("Updated item: ") + dialog.getName(), 0);
}

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

  updateRender();
  renderer->ResetCamera();
  renderWindow->Render();

  emit statusUpdateMessage(QString("Opened file: ") + fileInfo.fileName(), 0);
}

void MainWindow::on_actionOpen_Folder_triggered() {
  QModelIndex parentIndex = ui->treeView->currentIndex();

  QString dirPath = QFileDialog::getExistingDirectory(
      this, tr("Open Folder of STL Files"), lastBrowsedDir);

  if (dirPath.isEmpty()) {
    emit statusUpdateMessage(tr("Open folder cancelled"), 0);
    return;
  }
  lastBrowsedDir = dirPath;

  QDir dir(dirPath);
  QStringList stlFiles =
      dir.entryList(QStringList() << "*.stl" << "*.STL", QDir::Files);

  if (stlFiles.isEmpty()) {
    emit statusUpdateMessage(tr("No STL files found in: ") + dir.dirName(), 0);
    QMessageBox::information(this, tr("No STL Files"),
                             tr("The chosen directory contains no .stl files."));
    return;
  }

  QList<QVariant> folderData = {dir.dirName(), tr("Yes")};
  QModelIndex folderIndex = partList->appendChild(parentIndex, folderData);

  QProgressDialog progress(tr("Loading STL files..."), tr("Cancel"), 0,
                           stlFiles.size(), this);
  progress.setWindowTitle(tr("Loading %1").arg(dir.dirName()));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setValue(0);

  int loaded = 0;
  for (const QString &file : stlFiles) {
    if (progress.wasCanceled())
      break;

    progress.setLabelText(tr("Loading %1 (%2 of %3)...")
                              .arg(file)
                              .arg(loaded + 1)
                              .arg(stlFiles.size()));

    QString fullPath = dir.absoluteFilePath(file);
    QList<QVariant> data = {file, tr("Yes")};
    QModelIndex newIndex = partList->appendChild(folderIndex, data);

    if (!newIndex.isValid()) {
      ++loaded;
      progress.setValue(loaded);
      continue;
    }

    ModelPart *newPart = static_cast<ModelPart *>(newIndex.internalPointer());
    QString loadErr;
    if (!newPart->loadSTL(fullPath, &loadErr)) {
      /* Drop the failed row so we don't leave dead entries in the tree.
       * Show a non-modal status update; we don't want a popup per file
       * when the user is loading a folder of dozens of STLs. */
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

  progress.setValue(stlFiles.size());

  ui->treeView->expand(folderIndex);
  if (parentIndex.isValid())
    ui->treeView->expand(parentIndex);

  updateRender();
  renderer->ResetCamera();
  renderWindow->Render();

  if (progress.wasCanceled()) {
    emit statusUpdateMessage(QString("Loading cancelled after %1 of %2 files")
                                 .arg(loaded)
                                 .arg(stlFiles.size()),
                             0);
  } else {
    emit statusUpdateMessage(QString("Loaded %1 STL files from %2")
                                 .arg(loaded)
                                 .arg(dir.dirName()),
                             0);
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

void MainWindow::updateRender() {
  renderer->RemoveAllViewProps();
  updateRenderFromTree(QModelIndex());
  renderWindow->Render();
}

void MainWindow::updateRenderFromTree(const QModelIndex &index) {
  if (index.isValid()) {
    ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
    if (selectedPart && selectedPart->getActor()) {
      vtkProperty *prop = selectedPart->getActor()->GetProperty();
      prop->SetAmbient(0.3);
      prop->SetDiffuse(0.8);
      prop->SetSpecular(0.3);
      prop->SetSpecularPower(20);
      renderer->AddActor(selectedPart->getActor());
    }
  }

  int rows = partList->rowCount(index);
  for (int i = 0; i < rows; i++) {
    updateRenderFromTree(partList->index(i, 0, index));
  }
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

  emit statusUpdateMessage(
      tr("VR started (%1 triangles).")
          .arg(QLocale::system().toString(qlonglong(totalTris))),
      0);
}

void MainWindow::on_actionStop_VR_triggered() {
  if (!vrThread || !vrThread->isRunning()) {
    emit statusUpdateMessage(tr("VR is not running"), 0);
    return;
  }

  vrThread->issueCommand(VRRenderThread::END_RENDER, 0.0);
  vrThread->wait();
  delete vrThread;
  vrThread = nullptr;
  vrRotating = false;
  refreshWindowTitle();

  emit statusUpdateMessage(tr("VR stopped"), 0);
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
  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Cannot change colour: no item selected in the tree."), 0);
    return;
  }

  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
  QColor initial(selectedPart->getColourR(), selectedPart->getColourG(),
                 selectedPart->getColourB());

  QColor chosen = QColorDialog::getColor(initial, this, tr("Choose Part Colour"));
  if (!chosen.isValid())
    return;

  selectedPart->setColour(chosen.red(), chosen.green(), chosen.blue());
  if (selectedPart->getActor()) {
    selectedPart->getActor()->GetProperty()->SetColor(
        chosen.redF(), chosen.greenF(), chosen.blueF());
  }
  renderWindow->Render();
  emit statusUpdateMessage(tr("Changed colour for: ") + selectedPart->data(0).toString(), 0);
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
  /* Slider is 0..100; map to 0..1.2 so the midpoint 50 gives a
   * comfortable 0.6 intensity and 100 a slightly punchy highlight. */
  if (sceneLight) {
    double intensity = value / 100.0 * 1.2;
    sceneLight->SetIntensity(intensity);
    renderWindow->Render();
  }
  emit statusUpdateMessage(QString("Light intensity: %1").arg(value), 0);
}

void MainWindow::setupLighting() {
  sceneLight = vtkSmartPointer<vtkLight>::New();
  sceneLight->SetLightTypeToHeadlight();
  sceneLight->SetIntensity(1.0);
  renderer->AddLight(sceneLight);
  renderer->SetAmbient(0.2, 0.2, 0.2);
}

void MainWindow::onShrinkSliderChanged(int value) {
  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Select an item before adjusting the shrink slider."), 0);
    return;
  }
  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
  if (!selectedPart)
    return;

  /* Slider 0   -> factor 1.0 (no shrink).
   * Slider 100 -> factor 0.0 (max shrink). */
  double factor = (100.0 - value) / 100.0;
  selectedPart->setShrinkFactor(factor);
  renderWindow->Render();
  emit statusUpdateMessage(
      tr("Shrink factor: %1").arg(factor, 0, 'f', 2), 0);
}

void MainWindow::onClipSliderChanged(int value) {
  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Select an item before adjusting the clip slider."), 0);
    return;
  }
  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
  if (!selectedPart || !selectedPart->getActor())
    return;

  /* Use the ORIGINAL bounds (cached at load time), not the actor's current
   * bounds: once the clip filter cuts away part of the geometry the runtime
   * bounds shrink, so a second slider drag would map to a smaller range and
   * the slider would gradually lose effective travel. Fix from origin/Toni. */
  double bounds[6];
  selectedPart->getOriginalBounds(bounds);
  double minX = bounds[0];
  double maxX = bounds[1];
  double width = maxX - minX;

  double actualX;
  if (value == 0) {
    /* Park the clip plane just outside the model on the -X side so nothing
     * is cut. This lets the user "turn the clip off" by dragging the slider
     * back to 0 without us having to actually disable m_clipFilter (which
     * would require rebuilding the filter chain on every toggle). */
    actualX = minX - 0.1 * width;
  } else {
    actualX = minX + (double(value) / 100.0) * width;
  }

  selectedPart->applyClipping(actualX);
  renderWindow->Render();
  emit statusUpdateMessage(tr("Clip X: %1").arg(actualX, 0, 'f', 2), 0);
}

void MainWindow::onVisibilityToggled(bool checked) {
  QModelIndex index = ui->treeView->currentIndex();
  if (!index.isValid()) {
    emit statusUpdateMessage(
        tr("Select an item before toggling visibility."), 0);
    return;
  }
  ModelPart *selectedPart = static_cast<ModelPart *>(index.internalPointer());
  selectedPart->setVisible(checked);
  partList->setData(index.siblingAtColumn(1), checked ? "Yes" : "No",
                    Qt::EditRole);
  if (selectedPart->getActor())
    selectedPart->getActor()->SetVisibility(checked);
  renderWindow->Render();
  emit statusUpdateMessage(
      tr(checked ? "Item visible" : "Item hidden"), 0);
}

void MainWindow::onSyncVRClicked() {
  if (!vrThread || !vrThread->isRunning()) {
    emit statusUpdateMessage(
        tr("Sync VR: VR is not running. Click Start VR first."), 0);
    return;
  }

  /* Drop the current VR scene (keeping the floor) and push a fresh
   * snapshot of the tree. This is what makes filter changes in the
   * GUI actually show up in VR (PDF 3.3.3: "change things in the GUI
   * and the effect is seen in VR, while it is running") - getNewActor
   * rebuilds the filter chain from the current ModelPart state every
   * time it is called. */
  vrThread->issueCommand(VRRenderThread::CLEAR_SCENE, 0.0);
  pushTreeActorsToVR(QModelIndex());

  emit statusUpdateMessage(tr("Synced current parts to VR"), 0);
}

void MainWindow::onToggleVRRotation() {
  if (!vrThread || !vrThread->isRunning()) {
    emit statusUpdateMessage(
        tr("Toggle VR Rotation: VR is not running."), 0);
    return;
  }

  vrRotating = !vrRotating;
  /* 1 degree per ~20ms tick = ~50 deg/s. Comfortable speed for the
   * demo; not so fast it makes anyone seasick in the headset. */
  vrThread->issueCommand(VRRenderThread::ROTATE_Y, vrRotating ? 1.0 : 0.0);
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
    qApp->setStyleSheet(R"(
      QMainWindow, QWidget {
        background-color: #202124;
        color: #e8eaed;
      }

      QMenuBar, QMenu, QStatusBar, QToolBar {
        background-color: #2b2d31;
        color: #e8eaed;
      }

      QMenu::item:selected {
        background-color: #3c4043;
      }

      QPushButton {
        background-color: #3c4043;
        color: #e8eaed;
        border: 1px solid #5f6368;
        border-radius: 4px;
        padding: 4px 8px;
      }

      QPushButton:hover {
        background-color: #4a4d52;
      }

      QPushButton:pressed {
        background-color: #5f6368;
      }

      QTreeView, QTableView, QListView {
        background-color: #1f1f1f;
        color: #e8eaed;
        alternate-background-color: #2b2d31;
        border: 1px solid #5f6368;
      }

      QHeaderView::section {
        background-color: #2b2d31;
        color: #e8eaed;
        border: 1px solid #5f6368;
        padding: 4px;
      }

      QLineEdit, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit {
        background-color: #1f1f1f;
        color: #e8eaed;
        border: 1px solid #5f6368;
        selection-background-color: #4a90e2;
      }

      QGroupBox {
        border: 1px solid #5f6368;
        margin-top: 8px;
        color: #e8eaed;
      }

      QGroupBox::title {
        subcontrol-origin: margin;
        left: 8px;
        padding: 0px 4px;
      }

      QSlider::groove:horizontal {
        height: 6px;
        background: #5f6368;
        border-radius: 3px;
      }

      QSlider::handle:horizontal {
        background: #e8eaed;
        width: 14px;
        margin: -4px 0;
        border-radius: 7px;
      }

      QCheckBox {
        color: #e8eaed;
      }
    )");

    if (renderer) {
      renderer->SetBackground(0.05, 0.06, 0.08);
      renderer->SetBackground2(0.12, 0.14, 0.18);
      renderer->GradientBackgroundOn();
    }

    emit statusUpdateMessage(tr("Night mode enabled"), 0);

  } else {
    qApp->setStyleSheet("");

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
