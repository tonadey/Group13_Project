/**
 * @file OptionDialog.h
 * @brief Dialog for editing a model part's display and file properties.
 */

#ifndef OPTIONDIALOG_H
#define OPTIONDIALOG_H

#include <QDialog>
#include <QFileDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class OptionDialog;
}
QT_END_NAMESPACE

/**
 * @class OptionDialog
 * @brief Lets the user edit the selected model part's name, colour,
 * visibility, filters, and STL file path.
 */
class OptionDialog : public QDialog {
  Q_OBJECT

public:
  /**
   * @brief Constructs the options dialog.
   * @param parent Optional parent widget.
   */
  explicit OptionDialog(QWidget *parent = nullptr);

  /**
   * @brief Destroys the dialog and its generated UI.
   */
  ~OptionDialog();

  /**
   * @brief Gets the model part name currently entered in the dialog.
   * @return The part name.
   */
  QString getName() const;

  /**
   * @brief Sets the model part name shown in the dialog.
   * @param name Name to display.
   */
  void setName(const QString &name);

  /**
   * @brief Gets the red colour component.
   * @return Red value in the range 0 to 255.
   */
  int getR() const;

  /**
   * @brief Sets the red colour component.
   * @param r Red value in the range 0 to 255.
   */
  void setR(int r);

  /**
   * @brief Gets the green colour component.
   * @return Green value in the range 0 to 255.
   */
  int getG() const;

  /**
   * @brief Sets the green colour component.
   * @param g Green value in the range 0 to 255.
   */
  void setG(int g);

  /**
   * @brief Gets the blue colour component.
   * @return Blue value in the range 0 to 255.
   */
  int getB() const;

  /**
   * @brief Sets the blue colour component.
   * @param b Blue value in the range 0 to 255.
   */
  void setB(int b);

  /**
   * @brief Gets whether the part should be visible.
   * @return true when the part is visible.
   */
  bool getVisible() const;

  /**
   * @brief Sets whether the part should be visible.
   * @param visible true to show the part, false to hide it.
   */
  void setItemVisible(bool visible);

  /**
   * @brief Gets whether clipping is enabled for the part.
   * @return true when the clip filter is enabled.
   */
  bool getClipFilter() const;

  /**
   * @brief Sets whether clipping is enabled for the part.
   * @param enabled true to enable clipping.
   */
  void setClipFilter(bool enabled);

  /**
   * @brief Gets whether shrinking is enabled for the part.
   * @return true when the shrink filter is enabled.
   */
  bool getShrinkFilter() const;

  /**
   * @brief Sets whether shrinking is enabled for the part.
   * @param enabled true to enable shrinking.
   */
  void setShrinkFilter(bool enabled);

  /**
   * @brief Gets the STL file path shown in the dialog.
   * @return Absolute or relative STL path.
   */
  QString getStlPath() const;

  /**
   * @brief Sets the STL file path shown in the dialog.
   * @param path Absolute or relative STL path.
   */
  void setStlPath(const QString &path);

private slots:
  /**
   * @brief Opens a file picker so the user can choose an STL file.
   */
  void on_browseButton_clicked();

private:
  /** Generated Qt Designer UI for this dialog. */
  Ui::OptionDialog *ui;
};

#endif // OPTIONDIALOG_H
