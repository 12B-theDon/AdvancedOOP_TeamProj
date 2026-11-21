cmake_minimum_required(VERSION 3.21)
project(FingerprintEvaluator VERSION 1.0 LANGUAGES CXX)
if(NOT WIN32)
    message(FATAL_ERROR "FingerprintEvaluator supports Windows only.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "FingerprintEvaluator requires MSVC.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
find_package(Threads REQUIRED)
option(FINGERPRINT_BUILD_ALGORITHM "Build the DeepPrint implementation" ON)
option(FINGERPRINT_BUILD_TRAINING "Build training and native validation" ON)
option(FINGERPRINT_BUILD_GUI "Build the seven-step evaluator GUI" ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Gui)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui)
if(FINGERPRINT_BUILD_GUI)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
endif()
add_compile_options(/utf-8 /W4)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

add_library(Engine STATIC src/Engine/Engine.cpp src/Engine/kisaDll.cpp)
target_include_directories(Engine PUBLIC src/Engine)
target_link_libraries(Engine PUBLIC Qt${QT_VERSION_MAJOR}::Gui Threads::Threads)
# Engine and GUI have no Torch/encoder link or template-format knowledge.
if(FINGERPRINT_BUILD_GUI)
    add_executable(FingerprintGUI WIN32 src/main.cpp src/GUI/GUI.cpp src/GUI/report.cpp src/GUI/startup.cpp)
    target_link_libraries(FingerprintGUI PRIVATE Engine Qt${QT_VERSION_MAJOR}::Widgets)
    if(FINGERPRINT_BUILD_ALGORITHM AND FINGERPRINT_BUILD_TRAINING)
        target_compile_definitions(FingerprintGUI PRIVATE FINGERPRINT_AUTO_PREPARE_MODELS=1)
    endif()
    if(TARGET Qt5::WinMain)
        target_link_libraries(FingerprintGUI PRIVATE Qt5::WinMain)
    endif()
endif()
if(FINGERPRINT_BUILD_ALGORITHM OR FINGERPRINT_BUILD_TRAINING)
    find_package(Torch REQUIRED)
    separate_arguments(TORCH_FLAGS NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
    add_library(Algorithm STATIC src/Algorithm/encoder.cpp)
    target_include_directories(Algorithm PUBLIC src/Algorithm)
    target_link_libraries(Algorithm PUBLIC ${TORCH_LIBRARIES} Qt${QT_VERSION_MAJOR}::Core)
    target_compile_options(Algorithm PUBLIC ${TORCH_FLAGS})
endif()
if(FINGERPRINT_BUILD_TRAINING)
    add_executable(FingerprintTrain src/Training/train.cpp src/Training/validation.cpp
        src/Training/augment.cpp src/Training/ValidationEngine.cpp)
    target_include_directories(FingerprintTrain PRIVATE src/Training)
    target_link_libraries(FingerprintTrain PRIVATE Algorithm Qt${QT_VERSION_MAJOR}::Gui)
endif()
if(FINGERPRINT_BUILD_ALGORITHM)
    add_library(FingerprintAlgorithmCore SHARED src/Kisa/core.cpp)
    target_link_libraries(FingerprintAlgorithmCore PRIVATE Algorithm)
endif()
add_executable(FingerprintWorker src/Worker/worker.cpp)
set_target_properties(FingerprintWorker PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(FINGERPRINT_BUILD_ALGORITHM)
    add_library(FingerprintAlgorithm SHARED src/Kisa/BioApi.cpp src/Kisa/FingerprintAlgorithm.def)
    target_compile_definitions(FingerprintAlgorithm PRIVATE KISA_EXPORTS)
    set_target_properties(FingerprintAlgorithm PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_dependencies(FingerprintAlgorithm FingerprintAlgorithmCore)
    set(FINGERPRINT_PACKAGE_MODELS "${CMAKE_SOURCE_DIR}/models/trained_result" CACHE PATH "Existing four production weight files")
    set(FINGERPRINT_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/dist/FingerprintAlgorithm" CACHE PATH "Algorithm deployment destination")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
    add_custom_target(FingerprintAlgorithmPackage
        COMMAND ${CMAKE_COMMAND}
            "-DALGORITHM_DLL=$<TARGET_FILE:FingerprintAlgorithm>"
            "-DCORE_DLL=$<TARGET_FILE:FingerprintAlgorithmCore>"
            "-DWORKER_EXE=$<TARGET_FILE:FingerprintWorker>"
            "-DQT_CORE=$<TARGET_FILE:Qt${QT_VERSION_MAJOR}::Core>"
            "-DTORCH_ROOT=${TORCH_INSTALL_PREFIX}"
            "-DCRT_FILES=${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}"
            "-DMODELS=${FINGERPRINT_PACKAGE_MODELS}"
            "-DDESTINATION=${FINGERPRINT_PACKAGE_DIR}"
            "-DSOURCE=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageAlgorithm.cmake"
        DEPENDS FingerprintAlgorithm FingerprintAlgorithmCore FingerprintWorker VERBATIM)
endif()
if(FINGERPRINT_BUILD_GUI)
    get_target_property(QMAKE_EXE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN "${QMAKE_EXE}" DIRECTORY)
    find_program(WINDEPLOYQT windeployqt HINTS "${QT_BIN}" REQUIRED)
    add_custom_target(FingerprintEvaluatorPackage
        COMMAND ${CMAKE_COMMAND}
            "-DGUI_EXE=$<TARGET_FILE:FingerprintGUI>"
            "-DWORKER_EXE=$<TARGET_FILE:FingerprintWorker>"
            "-DWINDEPLOYQT=${WINDEPLOYQT}"
            "-DDESTINATION=${CMAKE_SOURCE_DIR}/dist/FingerprintEvaluator"
            "-DSOURCE=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageEvaluator.cmake"
        DEPENDS FingerprintGUI FingerprintWorker VERBATIM)
endif()
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
