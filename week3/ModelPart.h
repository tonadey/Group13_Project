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
