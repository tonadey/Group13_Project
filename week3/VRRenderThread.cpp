/**     @file VRRenderThread.cpp
 *
 *     EEEE2076 - Software Engineering & VR Project
 *
 *     Group 13 - VR section
 *
 *     Based on the example template at
 *     https://github.com/plevans/EEEE2076/tree/master/group/VRRenderThread,
 *     extended with: thread-safe pending-actor queue, OpenVR pre-flight
 *     check, headlight, scene-clear command for GUI sync, and a floor
 *     so VR isn't a void.
 */

#include "VRRenderThread.h"

#include <cmath>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkHDRReader.h>
#include <vtkLight.h>
#include <vtkOpenVRCamera.h>
#include <vtkOpenVRInteractorStyle.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRRenderer.h>
#include <vtkMapper.h>
#include <vtkPlane.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSkybox.h>
#include <vtkTexture.h>
#include <vtkVRMenuWidget.h>

#include <openvr.h>

bool VRRenderThread::isVRAvailable(QString *reason) {
  if (!vr::VR_IsRuntimeInstalled()) {
    if (reason)
      *reason = QObject::tr(
          "OpenVR/SteamVR runtime is not installed on this machine.");
    return false;
  }
  if (!vr::VR_IsHmdPresent()) {
    if (reason)
      *reason = QObject::tr(
          "No HMD detected. Start SteamVR (or enable its Null Driver) and "
          "make sure a headset is connected before clicking Start VR.");
    return false;
  }

  /* IsHmdPresent only checks USB; VR_Init can still fail (e.g. error 110
   * "Installation path could not be located"). If we let that slip
   * through, vtkOpenVRRenderWindowInteractor::Initialize() will then
   * dereference a null vr::VRInput() and the whole app crashes. So we
   * actually try to start a session here, then immediately tear it down
   * so VTK can re-init it. */
  vr::EVRInitError err = vr::VRInitError_None;
  vr::IVRSystem *sys = vr::VR_Init(&err, vr::VRApplication_Scene);
  if (err != vr::VRInitError_None || sys == nullptr) {
    if (reason) {
      const char *msg = vr::VR_GetVRInitErrorAsEnglishDescription(err);
      *reason = QObject::tr("OpenVR could not start a session: %1 "
                            "(error %2). Make sure SteamVR is fully "
                            "running and the headset is on.")
                    .arg(QString::fromUtf8(msg ? msg : "unknown"))
                    .arg(static_cast<int>(err));
    }
    if (sys != nullptr)
      vr::VR_Shutdown();
    return false;
  }
  vr::VR_Shutdown();
  return true;
}

VRRenderThread::VRRenderThread(QObject *parent)
    : QThread(parent), endRender(false), clearScene(false), rotateX(0.0),
      rotateY(0.0), rotateZ(0.0) {
  /* The actor list MUST be allocated here. If New() returns null (which
   * shouldn't happen but we guard against it) every later AddItem would
   * crash with a null deref - and the GUI would silently push actors
   * into nothing, so VR would launch with an empty scene. */
  pendingActors = vtkSmartPointer<vtkActorCollection>::New();
  if (!pendingActors)
    qCritical() << "VRRenderThread: failed to allocate pendingActors list";
}

int VRRenderThread::pendingActorCount() {
  QMutexLocker lock(&mutex);
  return pendingActors ? pendingActors->GetNumberOfItems() : -1;
}

VRRenderThread::~VRRenderThread() {
  /* If the thread is still spinning, ask it to stop and wait. */
  if (isRunning()) {
    issueCommand(END_RENDER, 0.0);
    wait();
  }
}

void VRRenderThread::addActorOffline(vtkSmartPointer<vtkActor> actor) {
  if (!actor) {
    qWarning() << "addActorOffline: refusing null actor";
    return;
  }
  if (!pendingActors) {
    qCritical() << "addActorOffline: pendingActors list is null";
    return;
  }

  /* Place the model in front of the user with its base on the floor.
   *   - RotateX(-90) converts STL's +Z up convention to VR's +Y up.
   *   - SetScale: kModelScale is the mm->m conversion (see header).
   *   - Y offset of -1.5 m drops the model's CAD ground plane (Z=0,
   *     typical for FS cars) onto the floor 1.5 m below the headset
   *     origin, so the car sits on the studio floor rather than at
   *     chest height.
   *   - Z offset of -2.5 m places the car ~2.5 m in front of the
   *     user, the natural "walk around it" viewing distance in a
   *     small play-space.
   *
   * The actor may already carry a non-zero Position from
   * ModelPart::getNewActor (the spherical-explode offset, expressed
   * in GUI mm units pre-rotated into VR axes). Multiply that by
   * kModelScale before stacking the base offset so the explode keeps
   * its visual proportion at the new world scale. */
  double *p = actor->GetPosition();
  const double explodeX = p[0] * kModelScale;
  const double explodeY = p[1] * kModelScale;
  const double explodeZ = p[2] * kModelScale;

  actor->RotateX(-90);
  actor->SetScale(kModelScale, kModelScale, kModelScale);
  actor->SetPosition(explodeX + 0.0,
                     explodeY - 1.5,
                     explodeZ - 2.5);

  /* Record the post-transform baseline so the run() loop can detect
   * when the OpenVR controller's PositionProp3D action drags this
   * actor away from where we placed it, and emit actorMovedInVR for
   * the GUI to mirror. Keyed by raw pointer (stable while the actor
   * lives in the VR scene). */
  double *bp = actor->GetPosition();
  {
    QMutexLocker lock(&mutex);
    m_actorBaseline.insert(static_cast<void *>(actor.Get()),
                           {bp[0], bp[1], bp[2]});
    pendingActors->AddItem(actor);
  }
  qDebug() << "addActorOffline: queued actor, list size now"
           << pendingActorCount();
}

void VRRenderThread::issueCommand(int cmd, double value) {
  QMutexLocker lock(&mutex);
  switch (cmd) {
  case END_RENDER:
    endRender = true;
    break;
  case ROTATE_X:
    rotateX = value;
    break;
  case ROTATE_Y:
    rotateY = value;
    break;
  case ROTATE_Z:
    rotateZ = value;
    break;
  case CLEAR_SCENE:
    clearScene = true;
    break;
  case ROTATE_BY_Y:
    /* Accumulate so back-to-back nudges from the GUI's 30Hz camera
     * poll collapse into a single per-frame rotation. */
    m_pendingYawDelta += value;
    break;
  case ROTATE_BY_X:
    m_pendingPitchDelta += value;
    break;
  }
}

void VRRenderThread::run() {
  /* --- Build the OpenVR pipeline ON THIS THREAD ---
   * VTK (and OpenGL) are not safe to share across threads, so the renderer,
   * window and interactor must all be created where they are used.
   *
   * Construction ORDER strictly follows the EEEE2076 reference
   * (plevans/EEEE2076/group/VRRenderThread). This order matters:
   *   1. Renderer
   *   2. Background, light, floor, AND user actors all added to the
   *      renderer FIRST (before window creation).
   *   3. Window + Initialize() + AddRenderer.
   *   4. Camera (vtkOpenVRCamera) pinned as the renderer's active camera.
   *   5. Interactor + Initialize.
   *   6. Render.
   * Calling renderer->ResetCamera() / ResetCameraClippingRange() between
   * steps clobbers the OpenVR camera state (which is driven by the HMD
   * pose) and will leave the model out of the clipping range, looking
   * like nothing is loaded. The reference does NOT call them - we follow
   * suit. */
  renderer = vtkSmartPointer<vtkOpenVRRenderer>::New();
  renderer->SetBackground(0.1, 0.1, 0.15); /* fallback if skybox load fails */

  /* --- SkyBox + Image-Based Lighting (PDF: realistic scenery via SkyBox) ---
   * Loads an equirectangular HDRI from `<exeDir>/skybox/garage_2k.hdr` and
   * feeds it into both a vtkSkybox actor (so the user sees a real garage
   * around them in VR) and the renderer's environment texture (so metal
   * parts pick up reflections of that garage instead of a flat colour).
   *
   * The HDR file ships next to the exe via a CMake POST_BUILD step. If
   * it can't be found we fall back to the solid-colour background above
   * rather than aborting - VR should still launch on a fresh checkout. */
  /* Pick whichever .hdr lives in skybox/ - we used to hard-code
   * "garage_2k.hdr" with a fallback, but that meant dropping in a
   * replacement HDRI required either renaming the file or deleting
   * the old one. Just scan the directory and take the first .hdr;
   * the team can swap HDRIs (studio, autoshop, workshop, ...) by
   * dropping a new file in and removing whatever else is there. */
  QString skyDir = QCoreApplication::applicationDirPath() + "/skybox";
  QString skyFile;
  QStringList candidates =
      QDir(skyDir).entryList(QStringList{"*.hdr", "*.HDR"}, QDir::Files);
  if (!candidates.isEmpty())
    skyFile = skyDir + "/" + candidates.first();

  /* Skybox actor is held at function scope so the CLEAR_SCENE handler in
   * the render loop can re-add it after RemoveAllViewProps wipes the
   * scene during a GUI-triggered Sync VR. */
  vtkSmartPointer<vtkSkybox> skybox;

  if (QFileInfo::exists(skyFile)) {
    auto hdr = vtkSmartPointer<vtkHDRReader>::New();
    hdr->SetFileName(skyFile.toUtf8().constData());
    hdr->Update();

    /* Feed the HDRI straight to the texture: vtkHDRReader's output
     * already matches vtkSkybox's expected orientation. We used to
     * pipe through a Y-axis vtkImageFlip here, but that double-
     * flipped the image and dropped the floor texture onto the
     * ceiling. */
    auto envTex = vtkSmartPointer<vtkTexture>::New();
    envTex->SetColorModeToDirectScalars();
    envTex->MipmapOn();
    envTex->InterpolateOn();
    envTex->SetInputConnection(hdr->GetOutputPort());

    skybox = vtkSmartPointer<vtkSkybox>::New();
    skybox->SetTexture(envTex);
    skybox->SetProjection(vtkSkybox::Sphere);
    skybox->PickableOff();
    renderer->AddActor(skybox);

    /* Image-based lighting: metal/specular parts will now reflect the
     * garage. Big visual upgrade for free, and a clear "own ideas"
     * talking point for the viva. */
    renderer->UseImageBasedLightingOn();
    renderer->SetEnvironmentTexture(envTex);
    renderer->UseSphericalHarmonicsOn();

    qDebug() << "VR run(): loaded SkyBox HDRI from" << skyFile;
  } else {
    qWarning() << "VR run(): no HDRI found in" << skyDir
               << "- falling back to solid background";
  }

  /* Headlight on top of IBL so the model is still well-lit even if the
   * HDRI is on the dimmer side. */
  vtkSmartPointer<vtkLight> light = vtkSmartPointer<vtkLight>::New();
  light->SetLightTypeToHeadlight();
  light->SetIntensity(1.0);
  renderer->AddLight(light);

  /* Floor / virtual environment (PDF 3.3.3 fourth bullet). The floor
   * actor is pinned in scene space (NOT added to the rotating pool
   * below) by tagging it with PickableOff; we filter it out of the
   * rotation loop by keeping a direct pointer rather than walking
   * GetActors(). */
  /* Floor sits 1.5 m below the headset origin so a standing user sees
   * the model "on the ground" instead of at chest / head level, and
   * extends 4 m in each direction so the play-space feels open. All
   * values are in metres because vtkOpenVRRenderWindow defaults to
   * PhysicalScale=1 (1 VTK unit = 1 m). Older builds used mm-scale
   * values like -2000 here, which placed the floor 2 km out and
   * outside the camera's far-clip plane, so it never rendered. */
  vtkSmartPointer<vtkPlaneSource> floorSource =
      vtkSmartPointer<vtkPlaneSource>::New();
  floorSource->SetOrigin(-4.0, -1.5, -4.0);
  floorSource->SetPoint1(4.0, -1.5, -4.0);
  floorSource->SetPoint2(-4.0, -1.5, 4.0);
  vtkSmartPointer<vtkPolyDataMapper> floorMapper =
      vtkSmartPointer<vtkPolyDataMapper>::New();
  floorMapper->SetInputConnection(floorSource->GetOutputPort());
  vtkSmartPointer<vtkActor> floorActor = vtkSmartPointer<vtkActor>::New();
  floorActor->SetMapper(floorMapper);
  floorActor->GetProperty()->SetColor(0.35, 0.35, 0.40);
  floorActor->GetProperty()->SetAmbient(0.4);
  floorActor->GetProperty()->SetDiffuse(0.6);
  floorActor->PickableOff();
  renderer->AddActor(floorActor);

  /* Drain pendingActors into the renderer BEFORE the window is created.
   * This is the order the reference uses; doing it later (after window /
   * camera setup) appears to leave the OpenVR pipeline confused and the
   * model invisible even though the actor is "in" the renderer. */
  int drained = 0;
  {
    QMutexLocker lock(&mutex);
    if (!pendingActors) {
      qCritical() << "run(): pendingActors is null - cannot populate VR scene";
    } else {
      pendingActors->InitTraversal();
      vtkActor *a = nullptr;
      while ((a = pendingActors->GetNextActor()) != nullptr) {
        renderer->AddActor(a);

        /* Log each actor's bounds so a wrong-units / wrong-scale model
         * (e.g. STL in metres rather than mm) is obvious in the diagnostic
         * log instead of being silently invisible. */
        double b[6];
        a->GetBounds(b);
        qDebug() << "VR run(): added actor" << a
                 << "bounds X[" << b[0] << b[1] << "] Y[" << b[2] << b[3]
                 << "] Z[" << b[4] << b[5] << "] visibility="
                 << a->GetVisibility();
        ++drained;
      }
      pendingActors->RemoveAllItems();
    }
  }
  m_initialDrainCount = drained;
  qDebug() << "VR run(): drained" << drained
           << "actors into renderer (renderer total actor count ="
           << renderer->GetActors()->GetNumberOfItems() << ")";

  /* Now create window / camera / interactor in the order the reference
   * uses. window->Initialize() before AddRenderer wires up the OpenVR
   * compositor pointer; without it window->Render() crashes with a null
   * read. */
  window = vtkSmartPointer<vtkOpenVRRenderWindow>::New();
  window->Initialize();
  window->AddRenderer(renderer);

  vtkSmartPointer<vtkOpenVRCamera> vrCamera =
      vtkSmartPointer<vtkOpenVRCamera>::New();
  renderer->SetActiveCamera(vrCamera);

  interactor = vtkSmartPointer<vtkOpenVRRenderWindowInteractor>::New();
  interactor->SetRenderWindow(window);

  /* Marksheet (Basic Functionality 20%): "The hand controllers work for
   * dragging parts within VR". The default trackball style only knows
   * about mouse/keyboard - swap in the OpenVR-aware style so the SteamVR
   * controllers actually do something. The mappings come from the
   * vtk_openvr_actions.json + per-controller bindings that CMake's
   * VRBindings target copies next to the exe:
   *   - Trigger    -> Pick3D            (point + pull -> select an actor)
   *   - Grip       -> PositionProp3D    (squeeze -> grab + move actor)
   *   - B / menu   -> ShowMenu          (right-controller button pops the
   *                                       radial menu - Probe / Clipping /
   *                                       X-ray / Shrink / Exit)
   * Floor and skybox are PickableOff()'d above so the user can't
   * accidentally pick up the room. */
  vtkSmartPointer<vtkOpenVRInteractorStyle> vrStyle =
      vtkSmartPointer<vtkOpenVRInteractorStyle>::New();
  interactor->SetInteractorStyle(vrStyle);

  /* The radial menu used to host X-ray / Shrink / Clip / Reset
   * toggles, but the menu widget added more demo flakiness than it
   * was worth (drifting placement, default Probe / Clipping / Grab
   * items wedging on STL meshes with no scalar data). Filters live
   * on the GUI side now - tweak them on the desktop, the auto-sync
   * path pushes the change into VR within 100 ms.
   *
   * The toggleVRXray / toggleVRShrink / toggleVRClip / resetVREffects
   * methods below are left in place but unreferenced; if a future
   * iteration wants to wire them to a controller button instead of
   * a menu, they're ready to use. */

  interactor->Initialize();

  qDebug() << "VR run(): pipeline ready, calling first Render()";
  window->Render();

  t_last = std::chrono::steady_clock::now();

  while (!endRender) {
    /* Pull in any actors queued from the GUI thread since last frame,
     * and honour CLEAR_SCENE if Sync VR was clicked. */
    {
      QMutexLocker lock(&mutex);

      if (clearScene) {
        /* Drop everything except the floor + skybox. RemoveAllViewProps
         * is too coarse - we keep the staging by re-adding the actors
         * we built once at startup. */
        renderer->RemoveAllViewProps();
        renderer->AddActor(floorActor);
        if (skybox)
          renderer->AddActor(skybox);
        /* Stale baselines refer to actors that are no longer in the
         * scene; drop them before the new push so we don't accumulate
         * dead pointers. */
        m_actorBaseline.clear();
        clearScene = false;
      }

      if (pendingActors->GetNumberOfItems() > 0) {
        pendingActors->InitTraversal();
        vtkActor *a = nullptr;
        while ((a = pendingActors->GetNextActor()) != nullptr) {
          renderer->AddActor(a);
        }
        pendingActors->RemoveAllItems();
        /* No ResetCameraClippingRange here - vtkOpenVRCamera derives
         * its clipping from the HMD pose, and a manual reset clobbers
         * that and clips the model out of view. */
      }
    }

    /* One VR frame: pump VR/SteamVR events + render both eyes. */
    interactor->DoOneEvent(window, renderer);

    /* Animation tick (PDF 3.3.3 fifth bullet). Every ~20ms, apply the
     * configured rotation to every model actor. The floor is excluded
     * by checking against floorActor. Reference: plevans EEEE2076
     * VRRenderThread.cpp run() loop. */
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - t_last)
            .count() > 20) {

      double rx, ry, rz;
      {
        QMutexLocker lock(&mutex);
        rx = rotateX;
        ry = rotateY;
        rz = rotateZ;
      }

      /* Drain the pending one-shot yaw / pitch deltas (camera-follow).
       * Done inside the same per-tick block so the shared actor walk
       * also applies them without an extra full pass. */
      double yawDelta, pitchDelta;
      {
        QMutexLocker lock(&mutex);
        yawDelta = m_pendingYawDelta;
        pitchDelta = m_pendingPitchDelta;
        m_pendingYawDelta = 0.0;
        m_pendingPitchDelta = 0.0;
      }

      if (rx != 0.0 || ry != 0.0 || rz != 0.0 || yawDelta != 0.0 ||
          pitchDelta != 0.0) {
        vtkActorCollection *actorList = renderer->GetActors();
        actorList->InitTraversal();
        vtkActor *a = nullptr;
        while ((a = actorList->GetNextActor()) != nullptr) {
          if (a == floorActor.Get())
            continue; /* don't spin the ground */
          if (skybox && a == skybox.Get())
            continue; /* don't spin the world around the user */
          if (rx != 0.0)
            a->RotateX(rx);
          if (ry != 0.0)
            a->RotateY(ry);
          if (rz != 0.0)
            a->RotateZ(rz);
          if (yawDelta != 0.0)
            a->RotateY(yawDelta);
          if (pitchDelta != 0.0)
            a->RotateX(pitchDelta);
        }
      }

      /* VR -> GUI sync: detect controller drags. After every tick we
       * compare each scene actor's current Position to the baseline we
       * stored when it was added. Any drift is the OpenVR
       * PositionProp3D action having moved it; emit the delta to the
       * GUI thread (queued connection) and rebase so the next tick
       * only reports newly-applied drag. */
      {
        vtkActorCollection *actorList = renderer->GetActors();
        actorList->InitTraversal();
        vtkActor *a = nullptr;
        while ((a = actorList->GetNextActor()) != nullptr) {
          if (a == floorActor.Get())
            continue;
          if (skybox && a == skybox.Get())
            continue;
          /* Hold the lock briefly: the GUI thread can mutate
           * m_actorBaseline via addActorOffline / clearScene clear. */
          double *cur = a->GetPosition();
          double dx = 0.0, dy = 0.0, dz = 0.0;
          bool drifted = false;
          {
            QMutexLocker lock(&mutex);
            auto it = m_actorBaseline.find(static_cast<void *>(a));
            if (it != m_actorBaseline.end()) {
              dx = cur[0] - (*it)[0];
              dy = cur[1] - (*it)[1];
              dz = cur[2] - (*it)[2];
              /* 1 mm of VR-space drift dead-band so floating-point
               * jitter doesn't dribble micro-deltas back to the GUI
               * every tick. The GUI's part-drag dead-band on the
               * other side is much coarser (~few mm), so this is
               * fine-grained enough not to lose real drags. */
              if (std::abs(dx) > 1e-3 || std::abs(dy) > 1e-3 ||
                  std::abs(dz) > 1e-3) {
                (*it) = {cur[0], cur[1], cur[2]};
                drifted = true;
              }
            }
          }
          if (drifted)
            emit actorMovedInVR(static_cast<void *>(a), dx, dy, dz);
        }
      }

      t_last = now;
    }
  }

  /* Tear down. The order matters for "Stop VR -> Start VR" to work
   * cleanly without crashing:
   *   1. Drop the renderer's actors first so their mappers are
   *      released BEFORE the GL context goes away. If we Finalize()
   *      the window first, mappers try to free their VBOs against a
   *      destroyed context and the next Start VR session can crash
   *      inside vtkOpenGLPolyDataMapper.
   *   2. Drop the cached floor / skybox / actor-collection refs we
   *      were holding for the menu callbacks - otherwise they keep
   *      mappers alive past the GL context teardown.
   *   3. Window->Finalize() releases VTK's grip on OpenVR.
   *   4. vr::VR_Shutdown() makes SteamVR drop the session for sure.
   *      VTK's Finalize SHOULD do this internally but on at least
   *      one demo box the next vr::VR_Init returned "AlreadyInitialized"
   *      and the second Start VR crashed - the explicit shutdown
   *      stops that. It's safe to call when already shut down (it
   *      just no-ops). */
  if (renderer) {
    renderer->RemoveAllViewProps();
    renderer->RemoveAllLights();
  }
  m_floorActor = nullptr;
  m_skyboxActor = nullptr;
  /* Release menu commands AFTER the menu widget is gone (which
   * happens when the interactor style is destroyed below). Doing
   * this earlier could leave the widget pointing at a freed
   * command if it processes a queued click during teardown. */
  m_xrayCmd = nullptr;
  m_shrinkCmd = nullptr;
  m_clipCmd = nullptr;
  m_resetCmd = nullptr;
  if (pendingActors)
    pendingActors->RemoveAllItems();

  if (window)
    window->Finalize();

  renderer = nullptr;
  interactor = nullptr;
  window = nullptr;

  vr::VR_Shutdown();
}

void VRRenderThread::toggleVRXray() {
  /* Walk every model actor (skip floor + skybox) and flip opacity
   * between solid (1.0) and see-through (0.4). Called on the VR
   * thread from inside the menu callback, so the renderer's actor
   * list is safe to traverse without a mutex. */
  if (!renderer)
    return;
  m_xrayOn = !m_xrayOn;
  const double op = m_xrayOn ? 0.4 : 1.0;
  vtkActorCollection *list = renderer->GetActors();
  list->InitTraversal();
  vtkActor *a = nullptr;
  while ((a = list->GetNextActor()) != nullptr) {
    if (a == m_floorActor.Get())
      continue;
    if (m_skyboxActor && a == m_skyboxActor.Get())
      continue;
    a->GetProperty()->SetOpacity(op);
  }
}

void VRRenderThread::toggleVRShrink() {
  /* Visual shrink: drop every model actor to 70% of its baseline scale
   * (kModelScale) or restore to baseline. Multiplying by the baseline
   * (instead of using SetScale(0.7) raw) is critical - actors are
   * already at kModelScale when added, and SetScale overrides the
   * existing value rather than multiplying, so a naive SetScale(0.7)
   * would shrink the model from 16x to 0.7x and make it disappear.
   *
   * Cheaper than rebuilding each mapper's vtkShrinkFilter chain;
   * visually equivalent for demo purposes. */
  if (!renderer)
    return;
  m_shrinkOn = !m_shrinkOn;
  const double s = kModelScale * (m_shrinkOn ? 0.7 : 1.0);
  vtkActorCollection *list = renderer->GetActors();
  list->InitTraversal();
  vtkActor *a = nullptr;
  while ((a = list->GetNextActor()) != nullptr) {
    if (a == m_floorActor.Get())
      continue;
    if (m_skyboxActor && a == m_skyboxActor.Get())
      continue;
    a->SetScale(s, s, s);
  }
}

void VRRenderThread::toggleVRClip() {
  /* "Cut the model in half" via per-part visibility. Originally we
   * used vtkMapper::AddClippingPlane (GPU plane clip), but on at
   * least one VR driver combo the OFF path didn't reliably restore
   * the geometry - mappers cached the clipped shader state and
   * RemoveAllClippingPlanes / SetClippingPlanes(nullptr) didn't
   * always trigger a recompile. SetVisibility is bulletproof:
   * VTK toggles a flag and the actor either draws or doesn't.
   *
   * Trade-off: parts that straddle the cut plane now disappear
   * entirely instead of being cut surface-clean, but for typical
   * assemblies (e.g. an FS car split into ~10 sub-parts) the
   * cross-section visual is good enough and the toggle is reliable. */
  if (!renderer)
    return;
  m_clipOn = !m_clipOn;

  /* Compute global X centre from loaded model actors. We use the raw
   * geometric bounds (GetBounds), which are independent of the
   * actor's current visibility, so toggling clip on/off doesn't drift
   * the centre between calls. */
  double minX = 1e30, maxX = -1e30;
  vtkActorCollection *list = renderer->GetActors();
  list->InitTraversal();
  vtkActor *a = nullptr;
  while ((a = list->GetNextActor()) != nullptr) {
    if (a == m_floorActor.Get())
      continue;
    if (m_skyboxActor && a == m_skyboxActor.Get())
      continue;
    double b[6];
    a->GetBounds(b);
    if (b[0] < minX)
      minX = b[0];
    if (b[1] > maxX)
      maxX = b[1];
  }
  if (minX > maxX)
    return; /* no model actors to clip */
  const double centerX = 0.5 * (minX + maxX);

  list->InitTraversal();
  while ((a = list->GetNextActor()) != nullptr) {
    if (a == m_floorActor.Get())
      continue;
    if (m_skyboxActor && a == m_skyboxActor.Get())
      continue;
    if (m_clipOn) {
      /* Hide everything whose centre lies on the +X side of the cut.
       * The user sees the inside of the assembly from the -X side. */
      double b[6];
      a->GetBounds(b);
      const double partCx = 0.5 * (b[0] + b[1]);
      a->SetVisibility(partCx < centerX ? 1 : 0);
    } else {
      /* Restore: every model actor visible again. */
      a->SetVisibility(1);
    }
  }
}

void VRRenderThread::resetVREffects() {
  /* Single-click way out of every menu toggle: restore opacity, the
   * baseline scale, and remove any active clip planes. Useful at the
   * start of a demo if a previous session left the scene in an odd
   * state. Doesn't undo controller-driven drags - those are explicit
   * user gestures and would surprise the user if a menu reset moved
   * parts back. */
  if (!renderer)
    return;
  m_xrayOn = false;
  m_shrinkOn = false;
  m_clipOn = false;
  vtkActorCollection *list = renderer->GetActors();
  list->InitTraversal();
  vtkActor *a = nullptr;
  while ((a = list->GetNextActor()) != nullptr) {
    if (a == m_floorActor.Get())
      continue;
    if (m_skyboxActor && a == m_skyboxActor.Get())
      continue;
    a->GetProperty()->SetOpacity(1.0);
    a->SetScale(kModelScale, kModelScale, kModelScale);
    a->SetVisibility(1);
    /* In case an older session had used the GPU clip-plane path,
     * make sure no stale planes are stuck on the mapper. Cheap. */
    if (vtkMapper *mapper = a->GetMapper())
      mapper->RemoveAllClippingPlanes();
  }
}
