cmake_minimum_required(VERSION 3.24)
project(DynamicShaderFrameGen LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "DynamicShaderFrameGen requires Windows x64 and MSVC. Use the Windows Actions workflow.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only x64 is supported")
endif()

include(CTest)
set(COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/CommonLibSSE-NG" CACHE PATH "CommonLibSSE-NG source")
set(STREAMLINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/streamline" CACHE PATH "Official Streamline SDK")
set(FFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/FidelityFX-SDK" CACHE PATH "Official FidelityFX SDK")
foreach(_required
    "${COMMONLIB_DIR}/CMakeLists.txt"
    "${STREAMLINE_DIR}/include/sl.h"
    "${STREAMLINE_DIR}/external/ngx-sdk/include/nvsdk_ngx.h"
    "${FFX_DIR}/Kits/FidelityFX/api/include/ffx_api.hpp")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Missing dependency: ${_required}. See docs/SE1597_TESTING.md")
    endif()
endforeach()
# The audited plugin supports SE 1.5.97 and AE 1.6.1170, not VR or future layouts.
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)
set(COMMONLIB_PREBUILT OFF CACHE STRING "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "CommonLib tests" FORCE)

set(CommonLibPath "${COMMONLIB_DIR}")
set(CommonLibName "CommonLibSSE")

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/NativeDlaa.cpp
    src/FrameGen.cpp
    src/DX12SwapChain.cpp
    src/Streamline.cpp
    src/FidelityFX.cpp
    src/ImguiMenu.cpp
    # DLSS5 Neural Rendering (NGX Feature 18, ported from CS feature/dlssnr-vr)
    src/NeuralRendering/Runtime.cpp
    src/NeuralRendering/D3D12Interop.cpp
    src/NeuralRendering/Renderer.cpp
    # v0.7：Dear ImGui 源码（vcpkg 下载被代理拦，直接 extern 编译）
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
    extern/imgui/imgui_demo.cpp
    extern/imgui/backends/imgui_impl_dx11.cpp
    extern/imgui/backends/imgui_impl_win32.cpp
    src/version.rc
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
# CommonLib headers are large; parse them once per target rather than per source.
target_precompile_headers(${PROJECT_NAME} PRIVATE <RE/Skyrim.h> <SKSE/SKSE.h>)

target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN SKYRIM NV_WINDOWS)

# Keep MSVC C++ flags out of the Windows resource compiler invocation.
# /utf-8 preserves the bilingual menu in BOM-less sources.
target_compile_options(${PROJECT_NAME} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/MP;/W4;/utf-8;/wd4200;/wd4819;/wd4996;/wd4471>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/fp:strict;/ZI;/Od;/Gy>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Zi;/fp:fast;/GL;/Gy-;/O2;/Ob2;/Oi;/Ot;/Oy>"
)

# CommonLibSSE-NG 源码
add_subdirectory(${CommonLibPath} ${CommonLibName} EXCLUDE_FROM_ALL)

# 依赖：spdlog / directxtk（CommonLibSSE-NG 需要），从 vcpkg 预装库查找
find_package(spdlog CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)


target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends")
# d3dx12 and d3d12 declarations must come from the same pinned DirectX-Headers
# package; mixing a newer d3dx12 with the runner's older system d3d12.h fails.
find_path(DIRECTX_HEADERS_INCLUDE_DIR directx/d3d12.h REQUIRED)
target_include_directories(${PROJECT_NAME} BEFORE PRIVATE "${DIRECTX_HEADERS_INCLUDE_DIR}/directx")
target_include_directories(${PROJECT_NAME} PRIVATE "${STREAMLINE_DIR}/include")
# DLSS5 Neural Rendering: NGX SDK headers (nvsdk_ngx.h + helpers) ship inside Streamline SDK
target_include_directories(${PROJECT_NAME} PRIVATE "${STREAMLINE_DIR}/external/ngx-sdk/include")
target_include_directories(${PROJECT_NAME} PRIVATE "${FFX_DIR}/Kits")


target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE spdlog::spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE d3d12.lib d3d11.lib dxgi.lib d3dcompiler.lib delayimp.lib)
# v0.7.15：Detours（函数开头 detour，CS 同款）——MenuManagerDrawInterfaceStart hook。
# write_branch 只能重定向现有 jmp/call，不能做函数 detour（原指令会丢 → 崩溃实锤）
find_path(DETOURS_INCLUDE_DIR detours/detours.h REQUIRED)
find_library(DETOURS_LIBRARY NAMES detours REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE "${DETOURS_INCLUDE_DIR}")
target_link_libraries(${PROJECT_NAME} PRIVATE "${DETOURS_LIBRARY}" version wintrust crypt32)

# v0.5.2：d3d11 延迟加载（/DELAYLOAD）——ENB 整合包根因修复：
# 游戏目录有 ENB 的 d3d11.dll 代理，SKSE 加载本插件时若静态解析 d3d11.dll →
# 触发 ENB 代理在插件加载阶段初始化失败 → LoadLibrary 整体失败（skse64.log
# "no version data (handle 0)" 不加载）。delayload 后 SKSE 加载零 d3d11 依赖，
# 运行时首次调用（游戏 D3D 初始化后，ENB 代理已正常加载）才解析。
# v0.5.17：d3d12.dll 恢复静态导入（ENB 不代理 d3d12，无冲突）——SL interposer
# 的 IAT hook 需要拦 D3D12CreateCommandQueue（dlss_g 插件靠它记录 queue；
# delayload 的 d3d12 走 delay-import thunk，IAT hook 拦不到 → 插件 queue 槽
# null → Allocate 时 0xC0000005 崩溃）。
target_link_options(${PROJECT_NAME} PRIVATE "/DELAYLOAD:d3d11.dll")

# 生成后复制到游戏 Data 目录（-DPLUGIN_DEPLOY_DIR 指定）
if(DEFINED PLUGIN_DEPLOY_DIR)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PROJECT_NAME}>
            "${PLUGIN_DEPLOY_DIR}/SKSE/Plugins/DynamicShaderFrameGen.dll"
        # v0.5.4: deploy INI + runtime DLLs too. MO2 rule: mod root == game Data tree,
        # so NO "Data/" prefix anywhere (a Data/ prefix maps to Data/Data/... and
        # the game never finds the files - verified 2026-08-28 "No INI found").
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/SKSE/Plugins/DynamicShaderFrameGen.ini"
            "${PLUGIN_DEPLOY_DIR}/SKSE/Plugins/DynamicShaderFrameGen.ini"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Shaders"
            "${PLUGIN_DEPLOY_DIR}/Shaders"
    )
endif()

# Test-package contains our DLL, shaders and opt-in test profile only. Official
# NVIDIA/AMD runtimes are not redistributed or modified by this build.
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION SKSE/Plugins)
install(FILES profiles/SE1597-Native-DLAA.ini DESTINATION SKSE/Plugins RENAME DynamicShaderFrameGen.ini)
install(DIRECTORY Shaders/ DESTINATION Shaders FILES_MATCHING PATTERN "*.hlsl" PATTERN "*.hlsli")
install(FILES LICENSE EXCEPTIONS.md docs/SE1597_TESTING.md DESTINATION .)

if(BUILD_TESTING)
    add_executable(render_policy_tests tests/render_policy.cpp)
    target_include_directories(render_policy_tests PRIVATE src)
    add_test(NAME render_policy COMMAND render_policy_tests)
endif()
