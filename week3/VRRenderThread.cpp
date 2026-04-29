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

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

#include <vtkCamera.h>
#include <vtkHDRReader.h>
#include <vtkLight.h>
#include <vtkOpenVRCamera.h>
#include <vtkOpenVRInteractorStyle.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRRenderer.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSkybox.h>
#include <vtkTexture.h>

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

  /* Initial transform from the EEEE2076 reference VRRenderThread
   * (plevans/EEEE2076/group/VRRenderThread): rotate -90 around X and
   * offset Y/Z so the model sits in front of and below the headset
   * origin, instead of clipping the user. STL files commonly come in
   * with +Z up; VR space here uses +Y up. */
  double *ac = actor->GetOrigin();
  actor->RotateX(-90);
  actor->AddPosition(-ac[0] + 0, -ac[1] - 100, -ac[2] - 200);

  QMutexLocker lock(&mutex);
  pendingActors->AddItem(actor);
  qDebug() << "addActorOffline: queued actor, list size now"
           << pendingActors->GetNumberOfItems();
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
  QString skyDir = QCoreApplication::applicationDirPath() + "/skybox";
  QString skyFile = skyDir + "/garage_2k.hdr";
  if (!QFileInfo::exists(skyFile)) {
    /* Fallback: any .hdr in the skybox dir. Lets the team swap HDRIs
     * (e.g. autoshop_01, workshop) without recompiling. */
    QStringList candidates =
        QDir(skyDir).entryList(QStringList{"*.hdr", "*.HDR"}, QDir::Files);
    if (!candidates.isEmpty())
      skyFile = skyDir + "/" + candidates.first();
  }

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
  vtkSmartPointer<vtkPlaneSource> floorSource =
      vtkSmartPointer<vtkPlaneSource>::New();
  floorSource->SetOrigin(-2000.0, -200.0, -2000.0);
  floorSource->SetPoint1(2000.0, -200.0, -2000.0);
  floorSource->SetPoint2(-2000.0, -200.0, 2000.0);
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
   *   - Trackpad   -> teleport / menu   (built into the style)
   * Floor and skybox are PickableOff()'d above so the user can't
   * accidentally pick up the room. */
  vtkSmartPointer<vtkOpenVRInteractorStyle> vrStyle =
      vtkSmartPointer<vtkOpenVRInteractorStyle>::New();
  interactor->SetInteractorStyle(vrStyle);

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

      if (rx != 0.0 || ry != 0.0 || rz != 0.0) {
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
        }
      }

      t_last = now;
    }
  }

  /* Tear down. Releasing the window first stops SteamVR from holding the
   * compositor; the smart pointers will then free everything. */
  window->Finalize();

  renderer = nullptr;
  interactor = nullptr;
  window = nullptr;
}
