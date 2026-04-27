/**     @file ModelPartList.cpp
 *
 *     EEEE2076 - Software Engineering & VR Project
 *
 *     Template for model part list that will be used to create the trewview.
 *
 *     P Evans 2022
 */

#include "ModelPartList.h"
#include "ModelPart.h"
#include <QColor>
#include <QPixmap>

ModelPartList::ModelPartList(const QString &data, QObject *parent)
    : QAbstractItemModel(parent) {
  /* Have option to specify number of visible properties for each item in tree -
   * the root item acts as the column headers
   */
  rootItem = new ModelPart({tr("Part"), tr("Visible?")});
}

ModelPartList::~ModelPartList() { delete rootItem; }

int ModelPartList::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent);

  return rootItem->columnCount();
}

QVariant ModelPartList::data(const QModelIndex &index, int role) const {
  /* If the item index isnt valid, return a new, empty QVariant (QVariant is
   * generic datatype that could be any valid QT class) */
  if (!index.isValid())
    return QVariant();

  /* Role represents what this data will be used for, we only need deal with the
   * case when QT is asking for data to create and display the treeview. Return
   * a new, empty QVariant if any other request comes through. */
  /* Get a pointer to the item referred to by the QModelIndex */
  ModelPart *item = static_cast<ModelPart *>(index.internalPointer());

  if (role == Qt::DisplayRole) {
    /* Each item in the tree has a number of columns ("Part" and "Visible" in
     * this initial example) return the column requested by the QModelIndex */
    return item->data(index.column());
  } else if (role == Qt::DecorationRole && index.column() == 0) {
    /* Return a small colored square as an icon for the first column */
    QPixmap pixmap(20, 20);
    pixmap.fill(
        QColor(item->getColourR(), item->getColourG(), item->getColourB()));
    return pixmap;
  }

  return QVariant();
}

bool ModelPartList::setData(const QModelIndex &index, const QVariant &value,
                            int role) {
  if (role != Qt::EditRole && role != Qt::DisplayRole)
    return false;

  ModelPart *item = static_cast<ModelPart *>(index.internalPointer());
  item->set(index.column(), value);
  emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
  return true;
}

Qt::ItemFlags ModelPartList::flags(const QModelIndex &index) const {
  if (!index.isValid())
    return Qt::NoItemFlags;

  return QAbstractItemModel::flags(index);
}

QVariant ModelPartList::headerData(int section, Qt::Orientation orientation,
                                   int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    return rootItem->data(section);

  return QVariant();
}

QModelIndex ModelPartList::index(int row, int column,
                                 const QModelIndex &parent) const {
  ModelPart *parentItem;

  if (!parent.isValid() || !hasIndex(row, column, parent))
    parentItem = rootItem; // default to selecting root
  else
    parentItem = static_cast<ModelPart *>(parent.internalPointer());

  ModelPart *childItem = parentItem->child(row);
  if (childItem)
    return createIndex(row, column, childItem);

  return QModelIndex();
}

QModelIndex ModelPartList::parent(const QModelIndex &index) const {
  if (!index.isValid())
    return QModelIndex();

  ModelPart *childItem = static_cast<ModelPart *>(index.internalPointer());
  ModelPart *parentItem = childItem->parentItem();

  if (parentItem == rootItem)
    return QModelIndex();

  return createIndex(parentItem->row(), 0, parentItem);
}

int ModelPartList::rowCount(const QModelIndex &parent) const {
  ModelPart *parentItem;
  if (parent.column() > 0)
    return 0;

  if (!parent.isValid())
    parentItem = rootItem;
  else
    parentItem = static_cast<ModelPart *>(parent.internalPointer());

  return parentItem->childCount();
}

ModelPart *ModelPartList::getRootItem() { return rootItem; }

QModelIndex ModelPartList::appendChild(QModelIndex &parent,
                                       const QList<QVariant> &data) {
  ModelPart *parentPart;

  if (parent.isValid())
    parentPart = static_cast<ModelPart *>(parent.internalPointer());
  else {
    parentPart = rootItem;
    parent = QModelIndex();
  }

  beginInsertRows(parent, rowCount(parent), rowCount(parent));

  ModelPart *childPart = new ModelPart(data, parentPart);

  parentPart->appendChild(childPart);

  QModelIndex child = createIndex(parentPart->childCount() - 1, 0, childPart);

  endInsertRows();

  emit layoutChanged();

  return child;
}

bool ModelPartList::removeItem(const QModelIndex &index) {
  if (!index.isValid())
    return false;

  ModelPart *item = static_cast<ModelPart *>(index.internalPointer());
  if (!item)
    return false;

  ModelPart *parentPart = item->parentItem();
  if (!parentPart)
    return false;

  QModelIndex parentIndex = parent(index);
  int rowToRemove = item->row();

  beginRemoveRows(parentIndex, rowToRemove, rowToRemove);
  parentPart->removeChild(rowToRemove);
  endRemoveRows();

  emit layoutChanged();
  return true;
}
