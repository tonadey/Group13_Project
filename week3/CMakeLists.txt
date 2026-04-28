cmake_minimum_required(VERSION 3.19)
project(ws6 LANGUAGES CXX)

# Set ws6 as the default startup project in Visual Studio
if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ws6)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
find_package(VTK REQUIRED
    COMPONENTS
    CommonCore
    CommonDataModel
    FiltersCore
    FiltersGeneral
    RenderingCore
    RenderingOpenGL2
    RenderingAnnotation
    IOGeometry
    InteractionStyle
    InteractionWidgets
    GUISupportQt
    RenderingOpenVR
)

set(CMAKE_AUTORCC on)
set(CMAKE_AUTOMOC on)
set(CMAKE_AUTOUIC on)

qt_standard_project_setup()

qt_add_executable(ws6
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    ModelPart.cpp
    ModelPart.h
    ModelPartList.cpp
    ModelPartList.h
    icons.qrc
    OptionDialog.cpp
    OptionDialog.h
    OptionDialog.ui
    VRRenderThread.cpp
    VRRenderThread.h
    CrashReporter.cpp
    CrashReporter.h
)

# --- Automatic DLL Deployment ---
# This part copies the required Qt DLLs to your output folder (e.g., ws6/Debug)
# so the program can find them when you click "Run" in Visual Studio.
if(MSVC)
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ws6 POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --debug "$<TARGET_FILE:ws6>"
            COMMENT "Copying Qt DLLs for workspace debugging..."
        )
    endif()
endif()

target_link_libraries(ws6
    PRIVATE
    Qt::Core
    Qt::Widgets
    ${VTK_LIBRARIES}
)

# We call vr::VR_IsRuntimeInstalled / vr::VR_IsHmdPresent in
# VRRenderThread.cpp to fail fast when SteamVR is not available, instead
# of crashing inside vtkOpenVRRenderWindowInteractor::Initialize() when
# vr::VRInput() returns nullptr. Those symbols live in openvr_api, which
# VTK's RenderingOpenVR module already discovered - reuse its variables.
if(DEFINED OpenVR_INCLUDE_DIR AND DEFINED OpenVR_LIBRARY)
    target_include_directories(ws6 PRIVATE ${OpenVR_INCLUDE_DIR})
    target_link_libraries(ws6 PRIVATE ${OpenVR_LIBRARY})
endif()

include(GNUInstallDirs)

install(TARGETS ws6
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET ws6
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# Copy across OpenVR bindings that map controllers
# The program will expect to find these in the build dir when it runs
add_custom_target(VRBindings ALL)
add_custom_command(TARGET VRBindings PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E
    copy_directory ${CMAKE_SOURCE_DIR}/vrbindings ${CMAKE_BINARY_DIR}/
)

