cmake_minimum_required(VERSION 3.19)
project(FingerprintEvaluator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt: https://www.qt.io/licensing/ (LGPL-3.0, GPL or commercial).
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Torch REQUIRED)
separate_arguments(TORCH_FLAGS NATIVE_COMMAND "${TORCH_CXX_FLAGS}")

add_library(Algorithm STATIC src/Algorithm/Algorithm.cpp src/Algorithm/encoder.cpp)
target_include_directories(Algorithm PUBLIC src/Algorithm)
target_link_libraries(Algorithm PUBLIC "${TORCH_LIBRARIES}" Qt${QT_VERSION_MAJOR}::Core)
target_compile_options(Algorithm PUBLIC ${TORCH_FLAGS})

add_library(Engine STATIC src/Engine/Engine.cpp)
target_include_directories(Engine PUBLIC src/Engine)
target_link_libraries(Engine PUBLIC Algorithm Qt${QT_VERSION_MAJOR}::Gui)

add_executable(FingerprintTrain src/Algorithm/train.cpp src/Algorithm/augment.cpp)
target_link_libraries(FingerprintTrain PRIVATE Engine Qt${QT_VERSION_MAJOR}::Gui)

add_executable(FingerprintGUI WIN32 src/main.cpp src/GUI/GUI.cpp src/GUI/report.cpp)
target_link_libraries(FingerprintGUI PRIVATE Engine Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_definitions(FingerprintGUI PRIVATE FINGERPRINT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_custom_command(TARGET FingerprintGUI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/data/original"
    "$<TARGET_FILE_DIR:FingerprintGUI>/data/original")
if(TARGET Qt5::WinMain)
    target_link_libraries(FingerprintGUI PRIVATE Qt5::WinMain)
endif()
if(MSVC)
    target_compile_options(Algorithm PRIVATE /utf-8)
    target_compile_options(FingerprintTrain PRIVATE /utf-8)
    target_compile_options(FingerprintGUI PRIVATE /utf-8)
endif()
