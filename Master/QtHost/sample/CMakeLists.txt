# Standalone build for the Qt host sample (VisionLab SDK, out-of-process integration).
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/qt6/6.9.1/msvc2022_64"
#   cmake --build build --config Release
#
# Needs: Qt6 (Core) + OpenCV. Produces qt_host_sample.
cmake_minimum_required(VERSION 3.16)
project(qt_host_sample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)              # IpcClient is a QObject (needs moc)

find_package(Qt6 COMPONENTS Core REQUIRED)
find_package(OpenCV REQUIRED)

add_executable(qt_host_sample
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../IpcClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../IpcClient.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../PlacementExpand.h)

target_include_directories(qt_host_sample PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..                      # IpcClient.h / PlacementExpand.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../shared/plugin_sdk
    ${OpenCV_INCLUDE_DIRS})

target_link_libraries(qt_host_sample PRIVATE Qt6::Core ${OpenCV_LIBS})
if(WIN32)
    target_link_libraries(qt_host_sample PRIVATE user32)
endif()
