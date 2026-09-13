# 示例插件 pin_hole_plugin —— 编译成一个插件 DLL。
# 客户把本工程拷走替换算法主体即可（保留 register/run 两个导出）。
#
# 依赖：OpenCV（core/imgproc）+ nlohmann/json（本仓库 shared/json.hpp 单头文件）。
# 在宿主工程内：由 shared/plugin_sdk/algo_plugin 的
#   CIRCLE_QT_BUILD_PLUGIN_EXAMPLES=ON 引入（OpenCV 变量来自根工程）。
# 独立构建：先 find_package(OpenCV REQUIRED) 并把 json.hpp 放到 include 路径。

add_library(pin_hole_plugin SHARED pin_hole_plugin.cpp)

target_compile_definitions(pin_hole_plugin PRIVATE CIRCLE_QT_PLUGIN_BUILD)

target_include_directories(pin_hole_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..          # circle_qt_plugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../..    # shared/json.hpp
    ${OpenCV_INCLUDE_DIRS}
)

if(MSVC)
    target_compile_options(pin_hole_plugin PRIVATE /utf-8)
endif()

# 在宿主工程内 OpenCV 已 find；独立构建时这里兜底。
find_package(OpenCV QUIET COMPONENTS core imgproc)
if(OpenCV_FOUND)
    target_link_libraries(pin_hole_plugin PRIVATE ${OpenCV_LIBS})
else()
    message(WARNING "OpenCV not found — pin_hole_plugin will not link OpenCV")
endif()

# Windows 下 DLL 不带 lib 前缀；输出到 build/Release 与宿主一致。
set_target_properties(pin_hole_plugin PROPERTIES PREFIX "")
