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

#include <array>
#include <chrono>

#include <QHash>
#include <QMutex>
#include <QString>
#include <QThread>

#include <vtkActor.h>
#include <vtkActorCollection.h>
#include <vtkCallbackCommand.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRRenderer.h>
#include <vtkSmartPointer.h>

class VRRenderThread : public QThread {
  Q_OBJECT

public:
  /** Per-actor scale applied at add-time. STL files come from the GUI in
   *  millimetres, but vtkOpenVRRenderWindow treats 1 VTK unit as 1
   *  metre (PhysicalScale=1.0 by default). Without this conversion an
   *  ~3000 mm car ends up 3 km wide in the VR world, way past the
   *  default camera far-clip plane (~1 km), and the user sees only the
   *  skybox. 0.0015 = mm->m with a slight 1.5x hero-scale bump so the
   *  car reads as ~2 m tall rather than the realistic ~1.3 m, which is
   *  easier to walk around in a small play-space.
   *  Exposed at class scope so toggleVRShrink / resetVREffects can use
   *  it as the "100%" baseline; otherwise a shrink toggle would clobber
   *  the scale and the part would either disappear or become enormous. */
  static constexpr double kModelScale = 0.03;

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
    CLEAR_SCENE,
    /* One-shot rotation by `value` degrees around Y - applied once on
     * the next frame, then cleared. Used to drive live camera-follow
     * (the GUI sends the per-tick azimuth delta as the user rotates
     * the desktop trackball, so the VR scene rotates to match). */
    ROTATE_BY_Y,
    /* One-shot pitch nudge around X. Same role as ROTATE_BY_Y but for
     * elevation, so when the user tilts the desktop trackball up/down
     * the VR model tilts to match instead of being locked to a single
     * yaw axis. */
    ROTATE_BY_X
  };

  void issueCommand(int cmd, double value = 0.0);

  /** Pre-flight check before launching the VR thread. Returns false if
   *  the OpenVR runtime is missing or no HMD is reachable, so the GUI
   *  can warn the user instead of dereferencing a null vr::VRInput()
   *  inside vtkOpenVRRenderWindowInteractor::Initialize(). When false,
   *  *reason (if non-null) carries a human-readable explanation. */
  static bool isVRAvailable(QString *reason = nullptr);

  /** Menu callbacks - called from inside the VR thread when the user
   *  picks the corresponding item from the radial menu. All three run
   *  on the VR thread (the menu callback fires there) so they walk
   *  the renderer's actor list directly without a mutex. */
  void toggleVRXray();
  void toggleVRShrink();
  void toggleVRClip();
  void resetVREffects();

  /** Number of actors currently waiting in the pending-actors list,
   *  i.e. queued but not yet drained into the VR renderer. Thread-safe.
   *  Used by the VR diagnostic dialog so the GUI can show whether the
   *  list was actually populated. */
  int pendingActorCount();

  /** Number of actors the run() loop drained from pendingActors into
   *  the VR renderer in its initial drain pass. -1 means run() has not
   *  reached the drain step yet. Used by the VR diagnostic dialog. */
  int initialDrainCount() const { return m_initialDrainCount; }

signals:
  /** Emitted from the VR thread when an actor's world-space position
   *  drifts from the baseline we recorded when it was added. The
   *  drift comes from the OpenVR controller's PositionProp3D action
   *  (grip + drag). The GUI thread connects this with a queued
   *  connection, looks up the corresponding ModelPart, converts the
   *  delta back into GUI-space units (mm) and pre-rotation axes, and
   *  applies it to the part's explode offset so the desktop view
   *  follows the headset move in real time. The delta is in VR world
   *  metres (post-rotation, post-scale frame). */
  void actorMovedInVR(void *actor, double dxVR, double dyVR, double dzVR);

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
  /* Accumulated one-shot Y-rotation (degrees) delivered by ROTATE_BY_Y.
   * The render loop drains this every tick and applies it to every
   * model actor so the VR view tracks the desktop camera's azimuth. */
  double m_pendingYawDelta = 0.0;
  /* Same idea but pitch (around X). Lets the desktop trackball's
   * elevation drag tilt the VR scene up/down. */
  double m_pendingPitchDelta = 0.0;

  /* Per-actor baseline Position recorded when addActorOffline finished
   * applying the VR transforms. After each frame the run() loop
   * compares each scene actor's current position to its baseline; if
   * they differ the user has dragged it via the VR controller, and we
   * emit actorMovedInVR with the delta and re-baseline so the next
   * tick only reports newly-applied drag. Keyed by raw vtkActor* -
   * stable for the lifetime of the actor in the scene. */
  QHash<void *, std::array<double, 3>> m_actorBaseline;

  /* Tick timer for the animation loop. Owned by run(). */
  std::chrono::time_point<std::chrono::steady_clock> t_last;

  /* Bookkeeping for the diagnostic dialog. -1 until run() drains. */
  int m_initialDrainCount = -1;

  /* Toggle state for the VR-side X-ray / Shrink / Clip menu items.
   * The menu callbacks flip these and walk the renderer; we keep
   * them as members so a second click on the same menu item flips
   * back. */
  bool m_xrayOn = false;
  bool m_shrinkOn = false;
  bool m_clipOn = false;

  /* Need access to floor + skybox pointers from the menu toggles so
   * we don't accidentally apply X-ray/shrink to the room itself.
   * run() populates these once it has built the floor and skybox. */
  vtkSmartPointer<vtkActor> m_floorActor;
  vtkSmartPointer<vtkActor> m_skyboxActor;

  /* Menu command objects MUST live as long as the VR thread itself.
   * vtkVRMenuWidget::PushFrontMenuItem stores a raw vtkCommand* and
   * does NOT retain a reference, so binding a local smart pointer
   * means the command is freed the moment run() leaves the if-block
   * that registered it - the next click on the menu item then jumps
   * through a dangling pointer and crashes. Holding them as members
   * keeps them alive until the thread is destroyed (i.e. after the
   * menu widget is gone too). */
  vtkSmartPointer<vtkCallbackCommand> m_xrayCmd;
  vtkSmartPointer<vtkCallbackCommand> m_shrinkCmd;
  vtkSmartPointer<vtkCallbackCommand> m_clipCmd;
  vtkSmartPointer<vtkCallbackCommand> m_resetCmd;
};

#endif // VR_RENDER_THREAD_H
