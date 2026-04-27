#include "mainwindow.h"
#include "OptionDialog.h"
#include "ui_mainwindow.h"
#include <QApplication>
#include <QColorDialog>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStyle>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);

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
  //connect(ui->shrinkFilterCheckBox, &QCheckBox::toggled, this,
      //    &MainWindow::onShrinkFilterToggled);
 // connect(ui->clipFilterCheckBox, &QCheckBox::toggled, this,
    //      &MainWindow::onClipFilterToggled);
  connect(ui->visibilityCheckBox, &QCheckBox::toggled, this,
          &MainWindow::onVisibilityToggled);
  connect(ui->shrinkSlider, &QSlider::valueChanged, this, 
      &MainWindow::onShrinkSliderChanged);
  connect(ui->clipSlider, &QSlider::valueChanged, this, 
      &MainWindow::onClipSliderChanged);

  /* VR buttons */
  connect(ui->startVRButton, &QPushButton::released, this,
          &MainWindow::on_actionStart_VR_triggered);
  connect(ui->stopVRButton, &QPushButton::released, this,
          &MainWindow::on_actionStop_VR_triggered);
  connect(ui->vrSyncButton, &QPushButton::released, this,
          &MainWindow::onSyncVRClicked);

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

  /* Hook VTK render window to the Qt widget */
  renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
  ui->vtkWidget->setRenderWindow(renderWindow);

  renderer = vtkSmartPointer<vtkRenderer>::New();
  renderWindow->AddRenderer(renderer);

  renderer->ResetCamera();
  renderer->GetActiveCamera()->Azimuth(30);
  renderer->GetActiveCamera()->Elevation(30);
  renderer->ResetCameraClippingRange();

  updateRender();

  emit statusUpdateMessage(tr("Ready"), 0);
}

MainWindow::~MainWindow() {
  delete ui;
  delete partList;
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
   * controls */
  //ui->shrinkFilterCheckBox->blockSignals(true);
  //ui->clipFilterCheckBox->blockSignals(true);
  ui->visibilityCheckBox->blockSignals(true);
  //ui->shrinkFilterCheckBox->setChecked(selectedPart->getShrinkFilter());
  //ui->clipFilterCheckBox->setChecked(selectedPart->getClipFilter());
  ui->visibilityCheckBox->setChecked(selectedPart->visible());
  //ui->shrinkFilterCheckBox->blockSignals(false);
  //ui->clipFilterCheckBox->blockSignals(false);
  ui->visibilityCheckBox->blockSignals(false);
  ui->shrinkSlider->blockSignals(true); // Prevent slider move from triggering logic
  ui->clipSlider->blockSignals(true);
  // Assuming shrink is 0.0-1.0 and clip is mapped 0-100
  ui->shrinkSlider->setValue(selectedPart->getShrinkFactor() * 100);

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
    selectedPart->loadSTL(dialog.getStlPath());
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
      this, tr("Open File"), QDir::currentPath(), tr("STL Files (*.stl)"));

  if (fileName.isEmpty()) {
    emit statusUpdateMessage(tr("Open file cancelled"), 0);
    return;
  }

  QList<QVariant> data = {tr("New Part"), tr("Yes")};
  QModelIndex newIndex = partList->appendChild(index, data);

  if (!newIndex.isValid()) {
    emit statusUpdateMessage(tr("Failed to create new tree item"), 0);
    return;
  }

  ModelPart *newPart = static_cast<ModelPart *>(newIndex.internalPointer());
  newPart->loadSTL(fileName);

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
      this, tr("Open Folder of STL Files"), QDir::currentPath());

  if (dirPath.isEmpty()) {
    emit statusUpdateMessage(tr("Open folder cancelled"), 0);
    return;
  }

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
    newPart->loadSTL(fullPath);

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

/*void MainWindow::on_actionToggle_Shrink_triggered() {
  bool on = ui->actionToggle_Shrink->isChecked();
  ui->shrinkFilterCheckBox->setChecked(on);
}

void MainWindow::on_actionToggle_Clip_triggered() {
  bool on = ui->actionToggle_Clip->isChecked();
  ui->clipFilterCheckBox->setChecked(on);
}*/

void MainWindow::on_actionStart_VR_triggered() {
  emit statusUpdateMessage(tr("Start VR: not yet implemented"), 0);
}

void MainWindow::on_actionStop_VR_triggered() {
  emit statusUpdateMessage(tr("Stop VR: not yet implemented"), 0);
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
  QColor chosen =
      QColorDialog::getColor(Qt::white, this, tr("Choose Background Colour"));
  if (!chosen.isValid())
    return;

  renderer->SetBackground(chosen.redF(), chosen.greenF(), chosen.blueF());
  renderWindow->Render();
  emit statusUpdateMessage(tr("Background colour changed"), 0);
}

void MainWindow::onLightIntensityChanged(int value) {
  emit statusUpdateMessage(
      QString("Light intensity: %1 (not yet wired to a vtkLight)").arg(value),
      0);
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
  emit statusUpdateMessage(tr("Sync GUI to VR: not yet implemented"), 0);
}


// Ensure the MainWindow:: prefix is present!
void MainWindow::onShrinkSliderChanged(int value) {
    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) return;

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());

    // Slider 0   -> (100 - 0)   / 100 = 1.0 (No Shrink)
     // Slider 100 -> (100 - 100) / 100 = 0.0 (Max Shrink)
    double factor = (100.0 - value) / 100.0;
    selectedPart->setShrinkFactor(factor);

    renderWindow->Render(); // This will work now if MainWindow:: is used
}

void MainWindow::onClipSliderChanged(int value) {
    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) return;

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
    if (!selectedPart || !selectedPart->getActor()) return;

    double bounds[6];
    selectedPart->getActor()->GetBounds(bounds);

    double minX = bounds[0];
    double maxX = bounds[1];
    double actualX = minX + (double(value) / 100.0) * (maxX - minX);

    selectedPart->applyClipping(actualX);
    renderWindow->Render();
}