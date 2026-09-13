# 进程内算法插件 ABI（in-process C ABI）— 见 docs/plugin_abi.md。
# 与 shared/plugin_sdk（独立进程插件宿主）互补：这里定义进程内 C 头文件 +
# 示例插件，插件算法直接复用宿主的 AlgorithmRegistry / 参数表单 / 配方系统。
#
# 核心交付：circle_qt_plugin.h（纯 C、零依赖，直接拷给客户）。
# 示例插件默认不编译；需要时 -DCIRCLE_QT_BUILD_PLUGIN_EXAMPLES=ON。
option(CIRCLE_QT_BUILD_PLUGIN_EXAMPLES
       "Build the in-process algo plugin examples" OFF)

if(CIRCLE_QT_BUILD_PLUGIN_EXAMPLES)
    add_subdirectory(examples/pin_hole)
endif()
