/**
 * @file ModelPartList.h
 * @brief Qt item model that exposes the ModelPart tree to the QTreeView.
 */

#ifndef VIEWER_MODELPARTLIST_H
#define VIEWER_MODELPARTLIST_H

#include "ModelPart.h"

#include <QAbstractItemModel>
#include <QList>
#include <QModelIndex>
#include <QString>
#include <QVariant>


class ModelPart;

/**
 * @class ModelPartList
 * @brief QAbstractItemModel wrapper around the hierarchical ModelPart tree.
 *
 * The model provides the data, flags, editing, parent/child indexes, and row
 * management needed by the main window's tree view.
 */
class ModelPartList : public QAbstractItemModel {
  Q_OBJECT

public:
  /**
   * @brief Creates a model part list with a root item.
   * @param data Initial root label or setup data.
   * @param parent Optional QObject parent.
   */
  ModelPartList(const QString &data, QObject *parent = NULL);

  /**
   * @brief Destroys the model and root item.
   */
  ~ModelPartList();

  /**
   * @brief Returns the number of columns shown in the tree.
   * @param parent Parent index, unused because all rows share the same columns.
   * @return Number of columns.
   */
  int columnCount(const QModelIndex &parent) const;

  /**
   * @brief Supplies data for a tree cell.
   * @param index Model index for the requested cell.
   * @param role Qt data role being requested.
   * @return Cell value for the role, or an invalid QVariant when not available.
   */
  QVariant data(const QModelIndex &index, int role) const;

  /**
   * @brief Updates data for a tree cell.
   * @param index Model index for the edited cell.
   * @param value New value.
   * @param role Edit role.
   * @return true if the value was accepted and stored.
   */
  bool setData(const QModelIndex &index, const QVariant &value,
               int role = Qt::EditRole);

  /**
   * @brief Returns the interaction flags for an item.
   * @param index Model index to query.
   * @return Qt item flags controlling selection, editing, and checkability.
   */
  Qt::ItemFlags flags(const QModelIndex &index) const;

  /**
   * @brief Supplies header text for the tree view.
   * @param section Header section.
   * @param orientation Header orientation.
   * @param role Qt data role being requested.
   * @return Header value for the role, or an invalid QVariant.
   */
  QVariant headerData(int section, Qt::Orientation orientation, int role) const;

  /**
   * @brief Creates an index for a child item.
   * @param row Child row.
   * @param column Child column.
   * @param parent Parent index; invalid means the root item.
   * @return Model index for the requested child, or an invalid index.
   */
  QModelIndex index(int row, int column, const QModelIndex &parent) const;

  /**
   * @brief Gets the parent index for an item.
   * @param index Child index.
   * @return Parent model index, or invalid for top-level items.
   */
  QModelIndex parent(const QModelIndex &index) const;

  /**
   * @brief Gets the number of child rows under a parent.
   * @param parent Parent index; invalid means the root item.
   * @return Number of child rows.
   */
  int rowCount(const QModelIndex &parent) const;

  /**
   * @brief Gets the root ModelPart item.
   * @return Root item pointer.
   */
  ModelPart *getRootItem();

  /**
   * @brief Appends a child under a parent item.
   * @param parent Parent index that will receive the child.
   * @param data Column data for the new child.
   * @return Model index for the inserted child.
   */
  QModelIndex appendChild(QModelIndex &parent, const QList<QVariant> &data);

  /**
   * @brief Removes the row at a given index from the tree.
   * @param index Index of the item to remove.
   * @return true if the row was removed.
   */
  bool removeItem(const QModelIndex &index);

private:
  /** Root item at the base of the tree. */
  ModelPart *rootItem;
};
#endif
