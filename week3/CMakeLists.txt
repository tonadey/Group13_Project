cmake_minimum_required(VERSION 3.19)
project(ws6 LANGUAGES CXX)

# Set ws6 as the default startup project in Visual Studio

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets)
find_package(VTK REQUIRED
    COMPONENTS
    CommonCore
    CommonDataModel
    FiltersCore
    FiltersGeneral
    RenderingCore
    RenderingOpenGL2
    IOGeometry
    InteractionStyle
    GUISupportQt
    RenderingOpenVR
)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    ModelPart.cpp
    ModelPart.h
    ModelPartList.cpp
    ModelPartList.h
    icons.qrc
    optiondialog.cpp
    optiondialog.h
    optiondialog.ui
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
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${VTK_LIBRARIES}
)
vtk_module_autoinit(
    TARGETS ws6
    MODULES ${VTK_LIBRARIES}
)

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

install(DIRECTORY ${CMAKE_SOURCE_DIR}/icons
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/vrbindings
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

include(InstallRequiredSystemLibraries)

if(EXISTS "C:/OpenVr/bin/win64/openvr_api.dll")
    install(FILES "C:/OpenVr/bin/win64/openvr_api.dll"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

foreach(_vtk_lib ${VTK_LIBRARIES})
    if(TARGET ${_vtk_lib})
        get_target_property(_lib_type ${_vtk_lib} TYPE)
        if(_lib_type STREQUAL "SHARED_LIBRARY")
            get_target_property(_lib_loc ${_vtk_lib} IMPORTED_LOCATION_RELEASE)
            if(_lib_loc AND EXISTS "${_lib_loc}")
                install(FILES "${_lib_loc}" DESTINATION ${CMAKE_INSTALL_BINDIR})
            endif()
        endif()
    endif()
endforeach()

# Copy across OpenVR bindings that map controllers
# The program will expect to find these in the build dir when it runs
add_custom_target(VRBindings ALL)
add_custom_command(TARGET VRBindings PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E
    copy_directory ${CMAKE_SOURCE_DIR}/vrbindings ${CMAKE_BINARY_DIR}/
)

set(CPACK_PACKAGE_NAME "ws6")
set(CPACK_PACKAGE_VENDOR "EEEE2076 Group 13")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ws6 Qt VTK CAD Viewer")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ws6")
 
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
else()
    set(CPACK_GENERATOR "ZIP")
endif()
 
set(CPACK_NSIS_DISPLAY_NAME "ws6")
set(CPACK_NSIS_PACKAGE_NAME "ws6")
set(CPACK_NSIS_CONTACT "EEEE2076 Group 13")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
 
# Start menu shortcut for the app
set(CPACK_PACKAGE_EXECUTABLES "ws6" "ws6")
 
# Add uninstaller link to start menu (required by marking rubric)
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Uninstall ws6.lnk' '$INSTDIR\\\\Uninstall.exe'"
)
set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Uninstall ws6.lnk'"
)
 
include(CPack)