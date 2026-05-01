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
#include <vtkGeometryFilter.h>
#include <vtkPlane.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSTLReader.h>
#include <vtkShrinkFilter.h>
#include <vtkSmartPointer.h>

/**
 * @class ModelPart
 * @brief Tree item and render data for one CAD model part or folder node.
 *
 * A ModelPart stores the data shown in the Qt tree model as well as the VTK
 * pipeline used to render an STL file. Folder nodes can have children without
 * owning an actor; leaf nodes usually hold the STL reader, mapper, actor, and
 * filter state.
 */
class ModelPart {
public:
  /**
   * @brief Creates a model tree item.
   * @param data Column data displayed by the tree view.
   * @param parent Parent item, or nullptr for the root item.
   */
  ModelPart(const QList<QVariant> &data, ModelPart *parent = nullptr);

  /**
   * @brief Deletes the item and all child items.
   */
  ~ModelPart();

  /**
   * @brief Appends a child item under this item.
   * @param item Child item to take ownership of.
   */
  void appendChild(ModelPart *item);

  /**
   * @brief Removes and deletes the child at the given row.
   * @param row Child row to remove.
   */
  void removeChild(int row);

  /**
   * @brief Gets a child by row.
   * @param row Child row.
   * @return Child item, or nullptr if the row is invalid.
   */
  ModelPart *child(int row);

  /**
   * @brief Gets the number of child items.
   * @return Child count.
   */
  int childCount() const;

  /**
   * @brief Gets the number of columns exposed to the tree model.
   * @return Column count.
   */
  int columnCount() const;

  /**
   * @brief Gets data for a tree column.
   * @param column Column index.
   * @return Value stored for the column.
   */
  QVariant data(int column) const;

  /**
   * @brief Sets data for a tree column.
   * @param column Column index.
   * @param value New value.
   */
  void set(int column, const QVariant &value);

  /**
   * @brief Gets this item's parent.
   * @return Parent item, or nullptr for the root.
   */
  ModelPart *parentItem();

  /**
   * @brief Gets this item's row within its parent.
   * @return Row index.
   */
  int row() const;

  /**
   * @brief Sets the actor colour.
   * @param R Red value in the range 0 to 255.
   * @param G Green value in the range 0 to 255.
   * @param B Blue value in the range 0 to 255.
   */
  void setColour(const unsigned char R, const unsigned char G,
                 const unsigned char B);

  /** @return Red colour component in the range 0 to 255. */
  unsigned char getColourR();

  /** @return Green colour component in the range 0 to 255. */
  unsigned char getColourG();

  /** @return Blue colour component in the range 0 to 255. */
  unsigned char getColourB();

  /**
   * @brief Shows or hides this part's actor.
   * @param isVisible true to show the part.
   */
  void setVisible(bool isVisible);

  /**
   * @brief Gets whether this part is marked visible.
   * @return true when visible.
   */
  bool visible();

  /** @return STL path associated with this part. */
  QString getStlPath() { return m_stlPath; }

  /**
   * @brief Stores the STL path associated with this part.
   * @param path STL file path.
   */
  void setStlPath(QString path) { m_stlPath = path; }

  /**
   * @brief Enables or disables the clip filter.
   * @param enabled true to enable clipping.
   */
  void setClipFilter(bool enabled);
  bool getClipFilter() const { return m_clipFilter; }
  void setShrinkFilter(bool enabled);
  bool getShrinkFilter() const { return m_shrinkFilter; }

  /** Continuous-slider filter controls (merged from main branch).
   *  setShrinkFactor enables shrink iff factor < 1.0 and uses the value
   *  as the vtkShrinkFilter factor; applyClipping enables clipping and
   *  positions the clip plane at world-space x. */
  void setShrinkFactor(double factor);
  void applyClipping(double actualX);
  double getShrinkFactor() const { return m_shrinkFactor; }
  double getClipX() const { return m_clipX; }

  /** Bounds captured the moment the STL is loaded, before any clip /
   *  shrink filter runs. The clip slider needs these so it can map
   *  0..100 to a stable X range - the actor's runtime bounds shrink
   *  as soon as the clip filter cuts the mesh, which would otherwise
   *  cause the slider to lose travel after the first drag. */
  void getOriginalBounds(double bounds[6]) const;

  /** 360 / spherical explode offset. Stored on the part so that
   *  getNewActor() (used to push a fresh actor into the VR thread)
   *  can apply the same translation that the GUI actor already has,
   *  keeping the explosion view in sync between desktop and headset. */
  void setExplodeOffset(double dx, double dy, double dz);
  void getExplodeOffset(double offset[3]) const;

  /** X-ray / per-part opacity in [0..1]. 1.0 = fully opaque (default),
   *  0.0 = fully transparent. The value is cached on the part so any
   *  fresh actor (loadSTL or getNewActor for VR) inherits it instead
   *  of resetting to opaque every time the user reloads / re-syncs. */
  void setOpacity(double opacity);
  double getOpacity() const { return m_opacity; }

  /** Per-part light intensity factor. Modulates this part's ambient and
   *  diffuse lighting coefficients so the user can highlight or dim a
   *  single part without touching the scene-level light. 1.0 = default,
   *  0.0 = pitch dark, 2.0 = saturated bright. */
  void setLightFactor(double factor);
  double getLightFactor() const { return m_lightFactor; }

  /** Rebuild the mapper input chain (reader -> [filters] -> mapper)
   *  according to the current filter flags. */
  void refreshFilters();

  /** Load an STL file. Returns true on success. On failure, *errorMsg
   *  (if non-null) carries a human-readable reason so the GUI can tell
   *  the user instead of silently leaving an empty actor in the tree. */
  bool loadSTL(QString fileName, QString *errorMsg = nullptr);

  /** Triangle count of the loaded mesh, or 0 if nothing has been loaded.
   *  Used by the GUI to warn before pushing a huge scene into VR. */
  vtkIdType getTriangleCount() const;

  /** Triangle count of the loaded mesh, or 0 if nothing has been loaded.
   *  Used by the GUI to warn before pushing a huge scene into VR. */
  vtkIdType getTriangleCount() const;

  /**
   * @brief Gets the actor used by the desktop renderer.
   * @return Current actor, or nullptr if no STL has been loaded.
   */
  vtkSmartPointer<vtkActor> getActor();

  /**
   * @brief Builds a fresh actor copy for the VR renderer.
   * @return New actor with the same mesh, colour, opacity, and transform state.
   */
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

  /* Slider state from the merged main branch. */
  double m_shrinkFactor = 1.0;
  double m_clipX = 0.0;

  /* Pre-filter bounds, captured in loadSTL so getOriginalBounds() can
   * hand them to the clip slider. */
  double originalBounds[6] = {0, 0, 0, 0, 0, 0};

  /* 360-degree explosion translation, in the GUI scene's coordinate
   * frame. (0,0,0) means no explosion. Driven by the explodeSlider
   * in MainWindow. */
  double m_explodeOffset[3] = {0.0, 0.0, 0.0};

  /* X-ray opacity in [0..1]. Driven by the Opacity slider in
   * MainWindow's right-hand panel. 1.0 = solid (default). */
  double m_opacity = 1.0;

  /* Per-part light factor; 1.0 = default lighting. */
  double m_lightFactor = 1.0;

  vtkSmartPointer<vtkSTLReader> reader;
  vtkSmartPointer<vtkPolyDataMapper> mapper;
  vtkSmartPointer<vtkActor> actor;

  /* Filter pipeline objects, kept as members so they remain alive while
   * the mapper references them. */
  vtkSmartPointer<vtkShrinkFilter> shrinkFilter;
  vtkSmartPointer<vtkClipDataSet> clipFilter;
  vtkSmartPointer<vtkPlane> clipPlane;
  /* Tail of the filter chain when shrink/clip are active: their output
   * is vtkUnstructuredGrid, but we render through vtkPolyDataMapper
   * (much faster than vtkDataSetMapper for static STL meshes), so we
   * convert back to polydata here before feeding the mapper. */
  vtkSmartPointer<vtkGeometryFilter> geometryFilter;
};

#endif
