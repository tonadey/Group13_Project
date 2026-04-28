/**     @file ModelPart.cpp
 *
 *     EEEE2076 - Software Engineering & VR Project
 *
 *     Template for model parts that will be added as treeview items
 *
 *     P Evans 2022
 */

#include "ModelPart.h"
#include <QDebug>
#include <QFileInfo>

#include <vtkActor.h>
#include <vtkClipDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkPlane.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSTLReader.h>
#include <vtkShrinkFilter.h>
#include <vtkSmartPointer.h>

ModelPart::ModelPart(const QList<QVariant> &data, ModelPart *parent)
    : m_itemData(data), m_parentItem(parent), isVisible(true), m_stlPath(""),
      m_R(255), m_G(255), m_B(255), m_clipFilter(false),
      m_shrinkFilter(false) {

  /* Synchronise internal flags with the data provided */
  for (int i = 0; i < m_itemData.size(); ++i) {
    set(i, m_itemData.at(i));
  }
}

ModelPart::~ModelPart() { qDeleteAll(m_childItems); }

void ModelPart::appendChild(ModelPart *item) {
  item->m_parentItem = this;
  m_childItems.append(item);
}

void ModelPart::removeChild(int row) {
  if (row < 0 || row >= m_childItems.size())
    return;
  delete m_childItems.takeAt(row);
}

ModelPart *ModelPart::child(int row) {
  if (row < 0 || row >= m_childItems.size())
    return nullptr;
  return m_childItems.at(row);
}

int ModelPart::childCount() const { return m_childItems.count(); }

int ModelPart::columnCount() const { return m_itemData.count(); }

QVariant ModelPart::data(int column) const {
  if (column < 0 || column >= m_itemData.size())
    return QVariant();
  return m_itemData.at(column);
}

void ModelPart::set(int column, const QVariant &value) {
  if (column < 0 || column >= m_itemData.size())
    return;

  m_itemData.replace(column, value);

  if (column == 1) {
    if (value.toString().toLower() == "yes" ||
        value.toString().toLower() == "true") {
      isVisible = true;
    } else {
      isVisible = false;
    }
  }
}

ModelPart *ModelPart::parentItem() { return m_parentItem; }

int ModelPart::row() const {
  if (m_parentItem)
    return m_parentItem->m_childItems.indexOf(const_cast<ModelPart *>(this));
  return 0;
}

void ModelPart::setColour(const unsigned char R, const unsigned char G,
                          const unsigned char B) {
  m_R = R;
  m_G = G;
  m_B = B;
}

unsigned char ModelPart::getColourR() { return m_R; }
unsigned char ModelPart::getColourG() { return m_G; }
unsigned char ModelPart::getColourB() { return m_B; }

void ModelPart::setVisible(bool isVisible) {
  this->isVisible = isVisible;
  set(1, isVisible ? "Yes" : "No");
}

bool ModelPart::visible() { return isVisible; }

void ModelPart::setClipFilter(bool enabled) {
  m_clipFilter = enabled;
  refreshFilters();
}

void ModelPart::setShrinkFilter(bool enabled) {
  m_shrinkFilter = enabled;
  refreshFilters();
}

void ModelPart::refreshFilters() {
  if (!reader || !mapper)
    return;

  /* Build a chain: reader -> [shrink] -> [clip] -> mapper.
   * vtkDataSetMapper accepts any vtkDataSet, so the unstructured grid
   * output of clip/shrink can feed it directly.
   *
   * Slider values (m_shrinkFactor / m_clipX) come from the merged main
   * branch - the GUI's shrink and clip sliders write to those before
   * calling refreshFilters(). */
  vtkAlgorithmOutput *currentOutput = reader->GetOutputPort();

  if (m_shrinkFilter) {
    if (!shrinkFilter)
      shrinkFilter = vtkSmartPointer<vtkShrinkFilter>::New();
    shrinkFilter->SetInputConnection(currentOutput);
    shrinkFilter->SetShrinkFactor(m_shrinkFactor);
    currentOutput = shrinkFilter->GetOutputPort();
  }

  if (m_clipFilter) {
    if (!clipPlane) {
      clipPlane = vtkSmartPointer<vtkPlane>::New();
      clipPlane->SetNormal(1.0, 0.0, 0.0);
    }
    clipPlane->SetOrigin(m_clipX, 0.0, 0.0);
    if (!clipFilter)
      clipFilter = vtkSmartPointer<vtkClipDataSet>::New();
    clipFilter->SetInputConnection(currentOutput);
    clipFilter->SetClipFunction(clipPlane);
    currentOutput = clipFilter->GetOutputPort();
  }

  mapper->SetInputConnection(currentOutput);
}

void ModelPart::setShrinkFactor(double factor) {
  m_shrinkFactor = factor;
  /* The shrink filter is only enabled when the slider is < 1.0, so the
   * geometry returns to its full size when the slider is dragged back
   * to "no shrink". */
  m_shrinkFilter = (factor < 1.0);
  refreshFilters();
}

void ModelPart::applyClipping(double actualX) {
  m_clipX = actualX;
  m_clipFilter = true;
  refreshFilters();
}

bool ModelPart::loadSTL(QString fileName, QString *errorMsg) {
  QFileInfo fileInfo(fileName);
  if (!fileInfo.exists()) {
    if (errorMsg)
      *errorMsg = QString("File does not exist: %1").arg(fileName);
    qWarning() << "File does not exist:" << fileName;
    return false;
  }
  if (fileInfo.size() == 0) {
    if (errorMsg)
      *errorMsg = QString("File is empty: %1").arg(fileInfo.fileName());
    return false;
  }

  m_stlPath = fileName;
  set(0, fileInfo.fileName());

  if (!reader)
    reader = vtkSmartPointer<vtkSTLReader>::New();
  reader->SetFileName(fileName.toStdString().c_str());
  reader->Update();

  /* vtkSTLReader sets ErrorCode on parse / IO failure but does not throw,
   * so we have to check explicitly - otherwise a corrupt STL silently
   * produces a 0-triangle actor and the user sees nothing in the scene. */
  if (reader->GetErrorCode() != 0) {
    if (errorMsg)
      *errorMsg = QString("STL reader failed (VTK error %1): %2")
                      .arg(reader->GetErrorCode())
                      .arg(fileInfo.fileName());
    return false;
  }

  vtkPolyData *poly = reader->GetOutput();
  if (!poly || poly->GetNumberOfCells() == 0) {
    if (errorMsg)
      *errorMsg = QString("STL contains no triangles: %1")
                      .arg(fileInfo.fileName());
    return false;
  }

  if (!mapper)
    mapper = vtkSmartPointer<vtkDataSetMapper>::New();
  mapper->SetInputConnection(reader->GetOutputPort());

  if (!actor)
    actor = vtkSmartPointer<vtkActor>::New();
  actor->SetMapper(mapper);
  /* Cache the unfiltered bounds so the clip slider can map 0..100 to
   * the original X range, even after the shrink/clip filter changes
   * the actor's runtime bounds. (From origin/Toni clip-fix.) */
  actor->GetBounds(originalBounds);
  actor->GetProperty()->SetColor(m_R / 255.0, m_G / 255.0, m_B / 255.0);
  actor->SetVisibility(isVisible);

  refreshFilters();

  qDebug() << "Successfully loaded STL:" << fileName << "tris="
           << poly->GetNumberOfCells();
  return true;
}

vtkIdType ModelPart::getTriangleCount() const {
  if (!reader)
    return 0;
  vtkPolyData *poly = reader->GetOutput();
  return poly ? poly->GetNumberOfCells() : 0;
}

vtkSmartPointer<vtkActor> ModelPart::getActor() { return actor; }

vtkSmartPointer<vtkActor> ModelPart::getNewActor() {
  if (!reader)
    return nullptr;

  /* PDF 2.1 / 3.3.3: the VR actor must be a fresh actor with a fresh
   * mapper - VTK actors and mappers cannot be shared across two render
   * windows. We also have to mirror the GUI's filter chain here, so
   * that clip + shrink show up in VR (PDF 3.3.3 first bullet) and the
   * GUI's slider changes are reflected the next time the user clicks
   * Sync VR.
   *
   * vtkDataSetMapper is used (not vtkPolyDataMapper) because the
   * outputs of vtkClipDataSet / vtkShrinkFilter are vtkUnstructuredGrid,
   * which vtkPolyDataMapper would refuse. Source stays as the same
   * vtkSTLReader so we don't re-parse the STL file. */
  vtkSmartPointer<vtkDataSetMapper> newMapper =
      vtkSmartPointer<vtkDataSetMapper>::New();

  vtkAlgorithmOutput *currentOutput = reader->GetOutputPort();

  if (m_shrinkFilter) {
    vtkSmartPointer<vtkShrinkFilter> sf =
        vtkSmartPointer<vtkShrinkFilter>::New();
    sf->SetInputConnection(currentOutput);
    sf->SetShrinkFactor(m_shrinkFactor);
    currentOutput = sf->GetOutputPort();
    /* sf is kept alive by the mapper's input connection holding a
     * reference to the algorithm via VTK's pipeline executive. */
  }

  if (m_clipFilter) {
    vtkSmartPointer<vtkPlane> plane = vtkSmartPointer<vtkPlane>::New();
    plane->SetOrigin(m_clipX, 0.0, 0.0);
    plane->SetNormal(1.0, 0.0, 0.0);

    vtkSmartPointer<vtkClipDataSet> cf =
        vtkSmartPointer<vtkClipDataSet>::New();
    cf->SetInputConnection(currentOutput);
    cf->SetClipFunction(plane);
    currentOutput = cf->GetOutputPort();
  }

  newMapper->SetInputConnection(currentOutput);

  vtkSmartPointer<vtkActor> newActor = vtkSmartPointer<vtkActor>::New();
  newActor->SetMapper(newMapper);

  /* Each actor must own its own vtkProperty. Sharing the GUI actor's
   * property across two render windows is a data race - the GUI thread
   * could mutate colour/visibility while the VR thread renders, and VTK
   * is not thread-safe. The Sync VR button rebuilds VR actors from the
   * current ModelPart state, which is how GUI changes propagate. */
  vtkProperty *prop = newActor->GetProperty();
  prop->SetColor(m_R / 255.0, m_G / 255.0, m_B / 255.0);
  prop->SetAmbient(0.3);
  prop->SetDiffuse(0.8);
  prop->SetSpecular(0.3);
  prop->SetSpecularPower(20);
  newActor->SetVisibility(isVisible);

  return newActor;
}

void ModelPart::getOriginalBounds(double bounds[6]) const {
  for (int i = 0; i < 6; ++i)
    bounds[i] = originalBounds[i];
}
