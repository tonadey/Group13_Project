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
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSTLReader.h>
#include <vtkSmartPointer.h>

ModelPart::ModelPart(const QList<QVariant> &data, ModelPart *parent)
    : m_itemData(data), m_parentItem(parent), isVisible(true), m_stlPath(""),
      m_R(255), m_G(255), m_B(255) {

  /* Synchronise internal flags with the data provided */
  for (int i = 0; i < m_itemData.size(); ++i) {
    set(i, m_itemData.at(i));
  }
}

ModelPart::~ModelPart() { qDeleteAll(m_childItems); }

void ModelPart::appendChild(ModelPart *item) {
  /* Add another model part as a child of this part
   * (it will appear as a sub-branch in the treeview)
   */
  item->m_parentItem = this;
  m_childItems.append(item);
}

ModelPart *ModelPart::child(int row) {
  /* Return pointer to child item in row below this item.
   */
  if (row < 0 || row >= m_childItems.size())
    return nullptr;
  return m_childItems.at(row);
}

int ModelPart::childCount() const {
  /* Count number of child items
   */
  return m_childItems.count();
}

int ModelPart::columnCount() const {
  /* Count number of columns (properties) that this item has.
   */
  return m_itemData.count();
}

QVariant ModelPart::data(int column) const {
  /* Return the data associated with a column of this item
   *  Note on the QVariant type - it is a generic placeholder type
   *  that can take on the type of most Qt classes. It allows each
   *  column or property to store data of an arbitrary type.
   */
  if (column < 0 || column >= m_itemData.size())
    return QVariant();
  return m_itemData.at(column);
}

void ModelPart::set(int column, const QVariant &value) {
  /* Set the data associated with a column of this item
   */
  if (column < 0 || column >= m_itemData.size())
    return;

  m_itemData.replace(column, value);

  /* If the visibility column (1) is changed, update the internal isVisible flag
   */
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
  /* Return the row index of this item, relative to it's parent.
   */
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

void ModelPart::loadSTL(QString fileName) {
  QFileInfo fileInfo(fileName);
  if (!fileInfo.exists()) {
    qWarning() << "File does not exist:" << fileName;
    return;
  }

  m_stlPath = fileName;
  set(0, fileInfo.fileName());

  /* 1. Use the vtkSTLReader class to load the STL file */
  if (!reader)
    reader = vtkSmartPointer<vtkSTLReader>::New();
  reader->SetFileName(fileName.toStdString().c_str());
  reader->Update();

  /* 2. Initialise the part's vtkMapper */
  if (!mapper)
    mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  mapper->SetInputConnection(reader->GetOutputPort());

  /* 3. Initialise the part's vtkActor and link to the mapper */
  if (!actor)
    actor = vtkSmartPointer<vtkActor>::New();
  actor->SetMapper(mapper);
  actor->GetProperty()->SetColor(m_R / 255.0, m_G / 255.0, m_B / 255.0);
  actor->SetVisibility(isVisible);

  qDebug() << "Successfully loaded STL:" << fileName;
}

vtkSmartPointer<vtkActor> ModelPart::getActor() { return actor; }

vtkSmartPointer<vtkActor> ModelPart::getNewActor() {
  /* 1. Create new mapper */
  vtkSmartPointer<vtkPolyDataMapper> newMapper =
      vtkSmartPointer<vtkPolyDataMapper>::New();
  newMapper->SetInputConnection(reader->GetOutputPort());

  /* 2. Create new actor and link to mapper */
  vtkSmartPointer<vtkActor> newActor = vtkSmartPointer<vtkActor>::New();
  newActor->SetMapper(newMapper);

  /* 3. Link the vtkProperties of the original actor to the new actor. */
  if (actor) {
    newActor->SetProperty(actor->GetProperty());
  }

  return newActor;
}
