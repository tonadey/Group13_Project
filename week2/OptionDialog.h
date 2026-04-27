#ifndef OPTIONDIALOG_H
#define OPTIONDIALOG_H

#include <QDialog>
#include <QFileDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class OptionDialog;
}
QT_END_NAMESPACE

class OptionDialog : public QDialog {
  Q_OBJECT

public:
  explicit OptionDialog(QWidget *parent = nullptr);
  ~OptionDialog();

  QString getName() const;
  void setName(const QString &name);

  int getR() const;
  void setR(int r);

  int getG() const;
  void setG(int g);

  int getB() const;
  void setB(int b);

  bool getVisible() const;
  void setItemVisible(bool visible);

  QString getStlPath() const;
  void setStlPath(const QString &path);

private slots:
  void on_browseButton_clicked();

private:
  Ui::OptionDialog *ui;
};

#endif // OPTIONDIALOG_H
