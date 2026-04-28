include("C:/Users/tonad/test/Group13_Project/week3/.qt/QtDeploySupport-Debug.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ws6-plugins-Debug.cmake" OPTIONAL)
set(__QT_DEPLOY_I18N_CATALOGS "qtbase")

qt6_deploy_runtime_dependencies(
    EXECUTABLE "C:/Users/tonad/test/Group13_Project/week3/Debug/ws6.exe"
    GENERATE_QT_CONF
)
