/**
 * @file OptionDialog.cpp
 * @brief Implements the model part options dialog.
 */

#include "OptionDialog.h"
#include "ui_OptionDialog.h"
#include <QFileInfo>

/**
 * @brief Creates the dialog and initialises the Qt Designer UI.
 * @param parent Optional parent widget.
 */
OptionDialog::OptionDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::OptionDialog) {
  ui->setupUi(this);
}

/**
 * @brief Releases the generated dialog UI.
 */
OptionDialog::~OptionDialog() { delete ui; }

/** @return The part name entered by the user. */
QString OptionDialog::getName() const { return ui->nameLineEdit->text(); }

/**
 * @brief Updates the part name field.
 * @param name Name to show in the dialog.
 */
void OptionDialog::setName(const QString &name) {
  ui->nameLineEdit->setText(name);
}

/** @return Red colour component from the dialog. */
int OptionDialog::getR() const { return ui->rSpinBox->value(); }

/** @param r Red colour component to show. */
void OptionDialog::setR(int r) { ui->rSpinBox->setValue(r); }

/** @return Green colour component from the dialog. */
int OptionDialog::getG() const { return ui->gSpinBox->value(); }

/** @param g Green colour component to show. */
void OptionDialog::setG(int g) { ui->gSpinBox->setValue(g); }

/** @return Blue colour component from the dialog. */
int OptionDialog::getB() const { return ui->bSpinBox->value(); }

/** @param b Blue colour component to show. */
void OptionDialog::setB(int b) { ui->bSpinBox->setValue(b); }

/** @return true when the visible checkbox is checked. */
bool OptionDialog::getVisible() const {
  return ui->visibleCheckBox->isChecked();
}

/**
 * @brief Updates the visible checkbox.
 * @param visible true to mark the part visible.
 */
void OptionDialog::setItemVisible(bool visible) {
  ui->visibleCheckBox->setChecked(visible);
}

/** @return true when the clip filter checkbox is checked. */
bool OptionDialog::getClipFilter() const {
  return ui->clipCheckBox->isChecked();
}

/**
 * @brief Updates the clip filter checkbox.
 * @param enabled true to enable clipping.
 */
void OptionDialog::setClipFilter(bool enabled) {
  ui->clipCheckBox->setChecked(enabled);
}

/** @return true when the shrink filter checkbox is checked. */
bool OptionDialog::getShrinkFilter() const {
  return ui->shrinkCheckBox->isChecked();
}

/**
 * @brief Updates the shrink filter checkbox.
 * @param enabled true to enable shrinking.
 */
void OptionDialog::setShrinkFilter(bool enabled) {
  ui->shrinkCheckBox->setChecked(enabled);
}

/** @return STL path entered in the dialog. */
QString OptionDialog::getStlPath() const { return ui->stlLineEdit->text(); }

/**
 * @brief Updates the STL path field.
 * @param path STL file path to show.
 */
void OptionDialog::setStlPath(const QString &path) {
  ui->stlLineEdit->setText(path);
}

/**
 * @brief Lets the user pick an STL file and copies the base name into the name field.
 */
void OptionDialog::on_browseButton_clicked() {
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open STL File"), QDir::currentPath(), tr("STL Files (*.stl)"));

  if (!fileName.isEmpty()) {
    ui->stlLineEdit->setText(fileName);
    QFileInfo fileInfo(fileName);
    ui->nameLineEdit->setText(fileInfo.baseName());
  }
}
