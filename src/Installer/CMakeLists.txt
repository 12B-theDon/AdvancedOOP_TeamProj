cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FingerprintInstallerPackaging LANGUAGES NONE)
endif()
if(NOT WIN32)
    message(FATAL_ERROR "The installation wizard must be packaged on Windows with Inno Setup 6.3+.")
endif()
get_filename_component(installer_source_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(FINGERPRINT_INSTALLER_PAYLOAD "${CMAKE_BINARY_DIR}/installer-payload" CACHE PATH "Clean runtime payload produced by the package targets")
set(FINGERPRINT_INSTALLER_OUTPUT "${installer_source_root}/dist/FingerprintSetup" CACHE PATH "Installation wizard output folder")
find_program(FINGERPRINT_ISCC NAMES ISCC.exe ISCC
    HINTS "$ENV{ProgramFiles}/Inno Setup 6" "$ENV{ProgramFiles\(x86\)}/Inno Setup 6")
set(installer_dependencies "")
set(uninstall_exe "${FINGERPRINT_INSTALLER_PAYLOAD}/Uninstall.exe")
if(CMAKE_CXX_COMPILER_LOADED)
    add_executable(FingerprintUninstall WIN32 uninstall.cpp)
    set_target_properties(FingerprintUninstall PROPERTIES OUTPUT_NAME Uninstall
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(FingerprintUninstall PRIVATE advapi32 shell32 user32 ole32)
    set(uninstall_exe "$<TARGET_FILE:FingerprintUninstall>")
    list(APPEND installer_dependencies FingerprintUninstall)
endif()
if(TARGET FingerprintAlgorithmPackage AND TARGET FingerprintEvaluatorPackage)
    list(APPEND installer_dependencies FingerprintAlgorithmPackage FingerprintEvaluatorPackage)
endif()
add_custom_target(FingerprintInstaller
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE=${installer_source_root}"
        "-DPAYLOAD=${FINGERPRINT_INSTALLER_PAYLOAD}"
        "-DOUTPUT=${FINGERPRINT_INSTALLER_OUTPUT}"
        "-DISCC=${FINGERPRINT_ISCC}"
        "-DUNINSTALL_EXE=${uninstall_exe}"
        "-DRUNNING_INSTALLER=$ENV{FINGERPRINT_RUNNING_INSTALLER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/PackageInstaller.cmake"
    DEPENDS ${installer_dependencies} VERBATIM)
if(TARGET Algorithm)
    add_executable(FingerprintDeploymentCheck EXCLUDE_FROM_ALL deployment_check.cpp)
    target_link_libraries(FingerprintDeploymentCheck PRIVATE Qt${QT_VERSION_MAJOR}::Gui psapi)
endif()
