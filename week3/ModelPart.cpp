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
    refreshFilters(); // Crucial: Reconnect the pipes
}

void ModelPart::setShrinkFilter(bool enabled) {
    m_shrinkFilter = enabled;
    refreshFilters(); // Crucial: Reconnect the pipes
}


void ModelPart::refreshFilters() {
    if (!reader) return;

    // Start with the source data
    vtkAlgorithmOutput* currentOutput = reader->GetOutputPort();

    // 1. Shrink logic
    if (m_shrinkFilter) {
        if (!shrinkFilter) shrinkFilter = vtkSmartPointer<vtkShrinkFilter>::New();
        shrinkFilter->SetInputConnection(currentOutput);
        shrinkFilter->SetShrinkFactor(m_shrinkFactor);
        currentOutput = shrinkFilter->GetOutputPort();
    }

    // 2. Clipping logic
    if (m_clipFilter) {
        if (!clipFilter) {
            clipFilter = vtkSmartPointer<vtkClipDataSet>::New();
            clipPlane = vtkSmartPointer<vtkPlane>::New();
            clipFilter->SetClipFunction(clipPlane);
        }
        clipPlane->SetOrigin(m_clipX, 0, 0);
        clipPlane->SetNormal(1, 0, 0);
        clipFilter->SetInputConnection(currentOutput);
        currentOutput = clipFilter->GetOutputPort();
    }

    // 3. Update the Mapper
    if (mapper) {
        // We connect the mapper to whatever the "final" output in the chain is
        mapper->SetInputConnection(currentOutput);
    }
}


void ModelPart::loadSTL(QString fileName) {
  QFileInfo fileInfo(fileName);
  if (!fileInfo.exists()) {
    qWarning() << "File does not exist:" << fileName;
    return;
  }

  m_stlPath = fileName;
  set(0, fileInfo.fileName());

  if (!reader)
    reader = vtkSmartPointer<vtkSTLReader>::New();
  reader->SetFileName(fileName.toStdString().c_str());
  reader->Update();

  if (!mapper)
    mapper = vtkSmartPointer<vtkDataSetMapper>::New();
  mapper->SetInputConnection(reader->GetOutputPort());

  if (!actor)
    actor = vtkSmartPointer<vtkActor>::New();
  actor->SetMapper(mapper);
  actor->GetProperty()->SetColor(m_R / 255.0, m_G / 255.0, m_B / 255.0);
  actor->SetVisibility(isVisible);

  refreshFilters();

  qDebug() << "Successfully loaded STL:" << fileName;
}

vtkSmartPointer<vtkActor> ModelPart::getActor() { return actor; }

vtkSmartPointer<vtkActor> ModelPart::getNewActor() {
  vtkSmartPointer<vtkPolyDataMapper> newMapper =
      vtkSmartPointer<vtkPolyDataMapper>::New();
  newMapper->SetInputConnection(reader->GetOutputPort());

  vtkSmartPointer<vtkActor> newActor = vtkSmartPointer<vtkActor>::New();
  newActor->SetMapper(newMapper);

  if (actor) {
    newActor->SetProperty(actor->GetProperty());
  }

  return newActor;
}

void ModelPart::setShrinkFactor(double factor) {
    m_shrinkFactor = factor;
    m_shrinkFilter = (factor < 1.0); // Enable filter if we are shrinking
    refreshFilters();
}

void ModelPart::applyClipping(double actualX) {
    m_clipX = actualX;
    m_clipFilter = true; // Ensure the filter is active

    // 1. Move the physical plane to the new X coordinate
    if (clipPlane) {
        // SetOrigin(X, Y, Z) - we only care about moving the X position
        clipPlane->SetOrigin(actualX, 0.0, 0.0);
    }

    // 2. Re-run the filter pipeline to show the new cut
    refreshFilters();
}
