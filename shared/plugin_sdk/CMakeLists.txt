cmake_minimum_required(VERSION 3.16)
project(PluginSDK VERSION 1.0.0 LANGUAGES CXX)

# ── SDK static library ────────────────────────────────────────────────────────
# Customers: add_subdirectory(path/to/shared/plugin_sdk)
#            target_link_libraries(MyApp PRIVATE PluginSDK)
#
# Host-side: PluginHostInterface + PluginHostLauncher
# Plugin-side: PluginInterface
# All three are compiled into the same library; use only what you link against.
# ─────────────────────────────────────────────────────────────────────────────

add_library(PluginSDK STATIC
    PluginHostInterface.cpp
    PluginHostLauncher.cpp
    PluginInterface.cpp
)

# Public headers — inherited by any target that links PluginSDK
# SharedMemLayout.h lives in the SDK directory itself (self-contained)
target_include_directories(PluginSDK
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(PluginSDK PUBLIC cxx_std_11)

if(MSVC)
    target_compile_options(PluginSDK PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(PluginSDK PRIVATE user32 kernel32)
endif()

# Optional: install rules (skip if not building as standalone SDK package)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    install(TARGETS PluginSDK ARCHIVE DESTINATION lib)
    install(FILES
        PluginHostInterface.h
        PluginHostInterfaceExt.h
        PluginHostLauncher.h
        PluginInterface.h
        DESTINATION include/plugin_sdk
    )
endif()
