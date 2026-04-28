/**     @file VRRenderThread.h
 *
 *     EEEE2076 - Software Engineering & VR Project
 *
 *     Group 13 - VR section
 *
 *     Runs the VTK + OpenVR render loop on a dedicated QThread so the
 *     blocking vtkOpenVRRenderWindowInteractor::Start() does not freeze
 *     the Qt GUI.
 */

#ifndef VR_RENDER_THREAD_H
#define VR_RENDER_THREAD_H

#include <chrono>

#include <QMutex>
#include <QString>
#include <QThread>

#include <vtkActor.h>
#include <vtkActorCollection.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRRenderer.h>
#include <vtkSmartPointer.h>

class VRRenderThread : public QThread {
  Q_OBJECT

public:
  explicit VRRenderThread(QObject *parent = nullptr);
  ~VRRenderThread() override;

  /** Queue an actor to be added to the VR scene the next time the render
   *  loop wakes up. Safe to call from the GUI thread before or while
   *  the thread is running - the queue is drained on the VR thread. */
  void addActorOffline(vtkSmartPointer<vtkActor> actor);

  /** Send a command to the VR thread in a thread-safe way. The thread
   *  applies it on its next frame.
   *
   *  END_RENDER asks the thread to exit run() cleanly.
   *  ROTATE_X / Y / Z set a per-tick rotation in degrees applied to
   *  every actor in the scene every ~20ms (set value to 0 to stop).
   *  CLEAR_SCENE drops all actors currently in the VR renderer; used
   *  by the GUI's Sync button to push a fresh snapshot of the tree
   *  (e.g. after the user changed a filter slider). */
  enum Command {
    END_RENDER = 0,
    ROTATE_X,
    ROTATE_Y,
    ROTATE_Z,
    CLEAR_SCENE
  };

  void issueCommand(int cmd, double value = 0.0);

  /** Pre-flight check before launching the VR thread. Returns false if
   *  the OpenVR runtime is missing or no HMD is reachable, so the GUI
   *  can warn the user instead of dereferencing a null vr::VRInput()
   *  inside vtkOpenVRRenderWindowInteractor::Initialize(). When false,
   *  *reason (if non-null) carries a human-readable explanation. */
  static bool isVRAvailable(QString *reason = nullptr);

  /** Number of actors currently waiting in the pending-actors list,
   *  i.e. queued but not yet drained into the VR renderer. Thread-safe.
   *  Used by the VR diagnostic dialog so the GUI can show whether the
   *  list was actually populated. */
  int pendingActorCount();

  /** Number of actors the run() loop drained from pendingActors into
   *  the VR renderer in its initial drain pass. -1 means run() has not
   *  reached the drain step yet. Used by the VR diagnostic dialog. */
  int initialDrainCount() const { return m_initialDrainCount; }

protected:
  /** QThread entry point. Sets up the OpenVR pipeline, then enters the
   *  render loop, polling for new actors, animation state, and the
   *  END_RENDER command. */
  void run() override;

private:
  /* --- VR pipeline (created inside run() on the VR thread) --- */
  vtkSmartPointer<vtkOpenVRRenderer> renderer;
  vtkSmartPointer<vtkOpenVRRenderWindow> window;
  vtkSmartPointer<vtkOpenVRRenderWindowInteractor> interactor;

  /* --- Cross-thread state, protected by mutex --- */
  QMutex mutex;
  vtkSmartPointer<vtkActorCollection> pendingActors;
  bool endRender;
  bool clearScene;

  /* --- Animation state (read/written under mutex) --- */
  double rotateX;
  double rotateY;
  double rotateZ;

  /* Tick timer for the animation loop. Owned by run(). */
  std::chrono::time_point<std::chrono::steady_clock> t_last;

  /* Bookkeeping for the diagnostic dialog. -1 until run() drains. */
  int m_initialDrainCount = -1;
};

#endif // VR_RENDER_THREAD_H
