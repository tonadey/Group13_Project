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
#include <vtkGeometryFilter.h>
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
  /* Single source of truth for visibility:
   *   - flip the internal flag
   *   - mirror it into column 1 of the tree row so the "Visible?"
   *     column stays accurate (the model still has to emit
   *     dataChanged separately - that's done in MainWindow via
   *     ModelPartList::setData)
   *   - push the same state into the GUI's vtkActor so the model
   *     actually disappears from the desktop view in real time.
   * Folder rows have no actor (loadSTL was never called), so we
   * just gate the SetVisibility call on actor != nullptr. */
  this->isVisible = isVisible;
  set(1, isVisible ? "Yes" : "No");
  if (actor)
    actor->SetVisibility(isVisible ? 1 : 0);
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

  /* Build a chain: reader -> [shrink] -> [clip] -> [geometry] -> mapper.
   * The mapper is vtkPolyDataMapper (faster than vtkDataSetMapper for
   * static STL meshes), so when shrink/clip are active we tail the chain
   * with vtkGeometryFilter to convert their unstructured-grid output
   * back to polydata.
   *
   * Slider values (m_shrinkFactor / m_clipX) come from the merged main
   * branch - the GUI's shrink and clip sliders write to those before
   * calling refreshFilters(). */
  vtkAlgorithmOutput *currentOutput = reader->GetOutputPort();
  bool anyFilter = false;

  if (m_shrinkFilter) {
    if (!shrinkFilter)
      shrinkFilter = vtkSmartPointer<vtkShrinkFilter>::New();
    shrinkFilter->SetInputConnection(currentOutput);
    shrinkFilter->SetShrinkFactor(m_shrinkFactor);
    currentOutput = shrinkFilter->GetOutputPort();
    anyFilter = true;
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
    anyFilter = true;
  }

  if (anyFilter) {
    if (!geometryFilter)
      geometryFilter = vtkSmartPointer<vtkGeometryFilter>::New();
    geometryFilter->SetInputConnection(currentOutput);
    currentOutput = geometryFilter->GetOutputPort();
    /* Filter output mutates whenever the slider moves, so the mapper
     * must re-upload VBOs each render. */
    mapper->SetStatic(0);
  } else {
    /* Pure STL chain: geometry never changes, so let VTK cache the
     * VBOs once and skip the per-frame upload. Big win at 4000+ parts. */
    mapper->SetStatic(1);
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
    mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  mapper->SetInputConnection(reader->GetOutputPort());

  if (!actor)
    actor = vtkSmartPointer<vtkActor>::New();
  actor->SetMapper(mapper);
  /* Cache the unfiltered bounds so the clip slider can map 0..100 to
   * the original X range, even after the shrink/clip filter changes
   * the actor's runtime bounds. (From origin/Toni clip-fix.) */
  actor->GetBounds(originalBounds);
  actor->GetProperty()->SetColor(m_R / 255.0, m_G / 255.0, m_B / 255.0);
  actor->GetProperty()->SetOpacity(m_opacity);
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
   * Mapper is vtkPolyDataMapper for speed; when shrink/clip are active
   * (their output is vtkUnstructuredGrid) we tail the chain with
   * vtkGeometryFilter to convert back to polydata. Source stays as the
   * same vtkSTLReader so we don't re-parse the STL file. */
  vtkSmartPointer<vtkPolyDataMapper> newMapper =
      vtkSmartPointer<vtkPolyDataMapper>::New();

  /* Hoist the filter smart pointers out of the if-blocks so they stay
   * alive until the function returns. vtkAlgorithmOutput* (currentOutput)
   * is a raw pointer owned by its producer; the producer is only kept
   * alive by the next filter/mapper's SetInputConnection AFTER that
   * call returns. So if we let sf/cf go out of scope before connecting
   * the next stage, the producer is destroyed and currentOutput dangles
   * - which is what crashed VR sync on heavy scenes (3M+ tri parts with
   * shrink/clip enabled). */
  vtkSmartPointer<vtkShrinkFilter> sf;
  vtkSmartPointer<vtkPlane> plane;
  vtkSmartPointer<vtkClipDataSet> cf;
  vtkSmartPointer<vtkGeometryFilter> gf;

  vtkAlgorithmOutput *currentOutput = reader->GetOutputPort();
  bool anyFilter = false;

  if (m_shrinkFilter) {
    sf = vtkSmartPointer<vtkShrinkFilter>::New();
    sf->SetInputConnection(currentOutput);
    sf->SetShrinkFactor(m_shrinkFactor);
    currentOutput = sf->GetOutputPort();
    anyFilter = true;
  }

  if (m_clipFilter) {
    plane = vtkSmartPointer<vtkPlane>::New();
    plane->SetOrigin(m_clipX, 0.0, 0.0);
    plane->SetNormal(1.0, 0.0, 0.0);

    cf = vtkSmartPointer<vtkClipDataSet>::New();
    cf->SetInputConnection(currentOutput);
    cf->SetClipFunction(plane);
    currentOutput = cf->GetOutputPort();
    anyFilter = true;
  }

  if (anyFilter) {
    gf = vtkSmartPointer<vtkGeometryFilter>::New();
    gf->SetInputConnection(currentOutput);
    currentOutput = gf->GetOutputPort();
  } else {
    /* No filters: STL geometry is immutable for the lifetime of this
     * VR actor, so let the mapper cache its VBOs. */
    newMapper->SetStatic(1);
  }

  newMapper->SetInputConnection(currentOutput);
  /* Once SetInputConnection has been called, newMapper's executive holds
   * a strong reference to the chain (gf -> cf -> sf -> reader), so the
   * function-scope smart pointers above are safe to destruct on return:
   * the chain survives via the mapper -> actor reference graph. */

  vtkSmartPointer<vtkActor> newActor = vtkSmartPointer<vtkActor>::New();
  newActor->SetMapper(newMapper);

  /* Mirror the GUI's spherical-explode translation onto the VR actor.
   * addActorOffline() calls RotateX(-90) before AddPosition()ing the
   * VR base offset, which rotates a GUI-space vector (x, y, z) into
   * (x, z, -y) in VR space. Apply that same rotation to the explode
   * offset so the radial directions stay consistent between the two
   * views. */
  double vrExplodeX = m_explodeOffset[0];
  double vrExplodeY = m_explodeOffset[2];
  double vrExplodeZ = -m_explodeOffset[1];
  newActor->SetPosition(vrExplodeX, vrExplodeY, vrExplodeZ);

  /* Each actor must own its own vtkProperty. Sharing the GUI actor's
   * property across two render windows is a data race - the GUI thread
   * could mutate colour/visibility while the VR thread renders, and VTK
   * is not thread-safe. The Sync VR button rebuilds VR actors from the
   * current ModelPart state, which is how GUI changes propagate. */
  vtkProperty *prop = newActor->GetProperty();
  prop->SetColor(m_R / 255.0, m_G / 255.0, m_B / 255.0);
  prop->SetOpacity(m_opacity);
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

void ModelPart::setExplodeOffset(double dx, double dy, double dz) {
  m_explodeOffset[0] = dx;
  m_explodeOffset[1] = dy;
  m_explodeOffset[2] = dz;
  if (actor)
    actor->SetPosition(dx, dy, dz);
}

void ModelPart::getExplodeOffset(double offset[3]) const {
  offset[0] = m_explodeOffset[0];
  offset[1] = m_explodeOffset[1];
  offset[2] = m_explodeOffset[2];
}

void ModelPart::setOpacity(double opacity) {
  /* Clamp to [0..1] - VTK accepts > 1 silently but it has no visual
   * effect and just papers over GUI bugs. Clamping here means
   * MainWindow can pass slider/100.0 directly without worrying. */
  if (opacity < 0.0)
    opacity = 0.0;
  else if (opacity > 1.0)
    opacity = 1.0;
  m_opacity = opacity;
  if (actor)
    actor->GetProperty()->SetOpacity(opacity);
}
