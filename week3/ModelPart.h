/** @file ModelPart.h
 *
 *  EEEE2076 - Software Engineering & VR Project
 *
 *  Template for model parts that will be added as treeview items
 *
 *  P Evans 2022
 */

#ifndef VIEWER_MODELPART_H
#define VIEWER_MODELPART_H

#include <QList>
#include <QString>
#include <QVariant>

#include <vtkActor.h>
#include <vtkClipDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkPlane.h>
#include <vtkProperty.h>
#include <vtkSTLReader.h>
#include <vtkShrinkFilter.h>
#include <vtkSmartPointer.h>

class ModelPart {
public:
  ModelPart(const QList<QVariant> &data, ModelPart *parent = nullptr);
  ~ModelPart();

  void appendChild(ModelPart *item);

  /** Remove (and delete) the child at given row. */
  void removeChild(int row);

  ModelPart *child(int row);
  int childCount() const;
  int columnCount() const;
  QVariant data(int column) const;
  void set(int column, const QVariant &value);
  ModelPart *parentItem();
  int row() const;

  void setColour(const unsigned char R, const unsigned char G,
                 const unsigned char B);
  unsigned char getColourR();
  unsigned char getColourG();
  unsigned char getColourB();

  void setVisible(bool isVisible);
  bool visible();

  QString getStlPath() { return m_stlPath; }
  void setStlPath(QString path) { m_stlPath = path; }

  /** Filter accessors */
  void setClipFilter(bool enabled);
  bool getClipFilter() const { return m_clipFilter; }
  void setShrinkFilter(bool enabled);
  bool getShrinkFilter() const { return m_shrinkFilter; }
  void setShrinkFactor(double factor);
  void applyClipping(double actualX);
  double getShrinkFactor() const { return m_shrinkFactor; }


  /** Rebuild the mapper input chain (reader -> [filters] -> mapper)
   *  according to the current filter flags. */
  void refreshFilters();

  void loadSTL(QString fileName);

  vtkSmartPointer<vtkActor> getActor();
  vtkSmartPointer<vtkActor> getNewActor();

private:
  QList<ModelPart *> m_childItems;
  QList<QVariant> m_itemData;
  ModelPart *m_parentItem;

  bool isVisible;
  QString m_stlPath;
  unsigned char m_R, m_G, m_B;

  bool m_clipFilter;
  bool m_shrinkFilter;

  double m_shrinkFactor = 1.0;
  double m_clipX = 0.0;

  vtkSmartPointer<vtkSTLReader> reader;
  vtkSmartPointer<vtkShrinkFilter> shrinkFilter;
  vtkSmartPointer<vtkClipDataSet> clipFilter;
  vtkSmartPointer<vtkPlane> clipPlane;
  vtkSmartPointer<vtkDataSetMapper> mapper;
  vtkSmartPointer<vtkActor> actor;
};

#endif
