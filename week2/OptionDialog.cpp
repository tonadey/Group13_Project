#include "OptionDialog.h"
#include "ui_OptionDialog.h"
#include <QFileInfo>

OptionDialog::OptionDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::OptionDialog) {
  ui->setupUi(this);
}

OptionDialog::~OptionDialog() { delete ui; }

QString OptionDialog::getName() const { return ui->nameLineEdit->text(); }

void OptionDialog::setName(const QString &name) {
  ui->nameLineEdit->setText(name);
}

int OptionDialog::getR() const { return ui->rSpinBox->value(); }

void OptionDialog::setR(int r) { ui->rSpinBox->setValue(r); }

int OptionDialog::getG() const { return ui->gSpinBox->value(); }

void OptionDialog::setG(int g) { ui->gSpinBox->setValue(g); }

int OptionDialog::getB() const { return ui->bSpinBox->value(); }

void OptionDialog::setB(int b) { ui->bSpinBox->setValue(b); }

bool OptionDialog::getVisible() const {
  return ui->visibleCheckBox->isChecked();
}

void OptionDialog::setItemVisible(bool visible) {
  ui->visibleCheckBox->setChecked(visible);
}

QString OptionDialog::getStlPath() const { return ui->stlLineEdit->text(); }

void OptionDialog::setStlPath(const QString &path) {
  ui->stlLineEdit->setText(path);
}

void OptionDialog::on_browseButton_clicked() {
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Open STL File"), QDir::currentPath(), tr("STL Files (*.stl)"));

  if (!fileName.isEmpty()) {
    ui->stlLineEdit->setText(fileName);
    QFileInfo fileInfo(fileName);
    ui->nameLineEdit->setText(fileInfo.baseName());
  }
}
