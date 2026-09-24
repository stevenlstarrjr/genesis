cmake_minimum_required(VERSION 3.25)

project(genesis VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(GENESIS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

if(EMSCRIPTEN)
    set(GENESIS_PLATFORM "web")
    # Exceptions are used by the scene loader and retained UI on both platforms.
    # Native Wasm exceptions avoid JS invoke trampolines trying to suspend
    # during static constructors, before main's JSPI promise boundary exists.
    add_compile_options(-fwasm-exceptions -msimd128)
    add_link_options(-fwasm-exceptions -msimd128)
elseif(WIN32)
    set(GENESIS_PLATFORM "microsoft")
elseif(APPLE)
    set(GENESIS_PLATFORM "apple")
elseif(UNIX)
    set(GENESIS_PLATFORM "linux")
else()
    message(FATAL_ERROR "Genesis does not recognize this target platform")
endif()
set(GENESIS_OUTPUT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/build/$<LOWER_CASE:$<CONFIG>>/${GENESIS_PLATFORM}")

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# bgfx is the sole real-time renderer.
set(BX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/bx" CACHE PATH "" FORCE)
set(BIMG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/bimg" CACHE PATH "" FORCE)
set(BGFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/bgfx" CACHE PATH "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_AMALGAMATED ON CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
    set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_TOOLS_SHADER OFF CACHE BOOL "" FORCE)
    set(BGFX_CONFIG_RENDERER_WEBGPU ON CACHE BOOL "" FORCE)
    set(GENESIS_HOST_SHADERC "" CACHE FILEPATH "Native bgfx shaderc executable (not WebAssembly)")
    if(NOT EXISTS "${GENESIS_HOST_SHADERC}")
        message(FATAL_ERROR "Web builds require -DGENESIS_HOST_SHADERC=/path/to/native/shaderc")
    endif()
    add_executable(bgfx::shaderc IMPORTED GLOBAL)
    set_target_properties(bgfx::shaderc PROPERTIES IMPORTED_LOCATION "${GENESIS_HOST_SHADERC}")
endif()
add_subdirectory(thirdparty/bgfx.cmake EXCLUDE_FROM_ALL)
target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_MAX_FRAME_BUFFERS=256 BGFX_CONFIG_MAX_VIEWS=512)
if(EMSCRIPTEN)
    # The vendored CMake option enables the WebGPU link dependency, but does
    # not forward the renderer define. Without it bgfx silently chooses GLES.
    target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_WEBGPU=1 BGFX_CONFIG_RENDERER_OPENGLES=0)
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/SDL EXCLUDE_FROM_ALL)

set(PK_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(PK_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(PK_ENABLE_OS ON CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/pocketpy EXCLUDE_FROM_ALL)
add_subdirectory(thirdparty/entt EXCLUDE_FROM_ALL)
add_subdirectory(thirdparty/yoga/yoga EXCLUDE_FROM_ALL)
include(cmake/ThorVG.cmake)
add_subdirectory(plugins/atmosphere)
add_subdirectory(plugins/color)

# Official Bruneton CPU reference baker. It shares this build tree and produces
# cacheable RGB lookup tables consumed by the bgfx renderer.
if(NOT EMSCRIPTEN)
add_executable(BrunetonBake
    tools/bruneton_bake.cpp
    thirdparty/precomputed_atmospheric_scattering/atmosphere/reference/model.cc
    thirdparty/precomputed_atmospheric_scattering/atmosphere/reference/functions.cc
    thirdparty/precomputed_atmospheric_scattering/external/progress_bar/util/progress_bar.cc
)
set_target_properties(BrunetonBake PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}"
)
target_include_directories(BrunetonBake PRIVATE
    thirdparty/precomputed_atmospheric_scattering
    thirdparty/precomputed_atmospheric_scattering/external/dimensional_types
    thirdparty/precomputed_atmospheric_scattering/external/progress_bar
)
target_compile_definitions(BrunetonBake PRIVATE _USE_MATH_DEFINES)
endif()

include(thirdparty/bgfx.cmake/cmake/bgfxToolUtils.cmake)
set(GENESIS_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bgfx")
set(GENESIS_GENERATED_SHADERS "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
set(GENESIS_SHADER_PROFILE s_5_0)
set(GENESIS_SHADER_VARIANT dxbc)
if(EMSCRIPTEN)
    set(GENESIS_SHADER_PROFILE wgsl)
    set(GENESIS_SHADER_VARIANT wgsl)
endif()
bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS "${GENESIS_SHADER_DIR}/vs_pbr.sc" "${GENESIS_SHADER_DIR}/vs_shadow.sc" "${GENESIS_SHADER_DIR}/vs_fullscreen.sc" "${GENESIS_SHADER_DIR}/vs_light_source.sc" "${GENESIS_SHADER_DIR}/vs_editor_lines.sc" "${GENESIS_SHADER_DIR}/vs_ui.sc" "${GENESIS_SHADER_DIR}/vs_selection_mask.sc"
    VARYING_DEF "${GENESIS_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${GENESIS_GENERATED_SHADERS}"
    INCLUDE_DIRS "${GENESIS_SHADER_DIR}" "${BGFX_DIR}/src"
    PROFILES ${GENESIS_SHADER_PROFILE}
    AS_HEADERS
    OUT_FILES_VAR GENESIS_VERTEX_SHADERS
)
bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS "${GENESIS_SHADER_DIR}/fs_pbr.sc" "${GENESIS_SHADER_DIR}/fs_viewport.sc" "${GENESIS_SHADER_DIR}/fs_shadow.sc" "${GENESIS_SHADER_DIR}/fs_sky.sc" "${GENESIS_SHADER_DIR}/fs_environment_prefilter.sc" "${GENESIS_SHADER_DIR}/fs_probe_prefilter.sc" "${GENESIS_SHADER_DIR}/fs_brdf_lut.sc" "${GENESIS_SHADER_DIR}/fs_ao.sc" "${GENESIS_SHADER_DIR}/fs_volumetric.sc" "${GENESIS_SHADER_DIR}/fs_bloom_prefilter.sc" "${GENESIS_SHADER_DIR}/fs_bloom_upsample.sc" "${GENESIS_SHADER_DIR}/fs_light_source.sc" "${GENESIS_SHADER_DIR}/fs_tonemap.sc" "${GENESIS_SHADER_DIR}/fs_hud.sc" "${GENESIS_SHADER_DIR}/fs_ui.sc" "${GENESIS_SHADER_DIR}/fs_selection_mask.sc" "${GENESIS_SHADER_DIR}/fs_selection_outline.sc"
    VARYING_DEF "${GENESIS_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${GENESIS_GENERATED_SHADERS}"
    INCLUDE_DIRS "${GENESIS_SHADER_DIR}" "${BGFX_DIR}/src"
    PROFILES ${GENESIS_SHADER_PROFILE}
    AS_HEADERS
    OUT_FILES_VAR GENESIS_FRAGMENT_SHADERS
)
bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS "${GENESIS_SHADER_DIR}/fs_editor_lines.sc"
    VARYING_DEF "${GENESIS_SHADER_DIR}/varying.def.sc"
    OUTPUT_DIR "${GENESIS_GENERATED_SHADERS}"
    INCLUDE_DIRS "${GENESIS_SHADER_DIR}" "${BGFX_DIR}/src"
    PROFILES ${GENESIS_SHADER_PROFILE}
    AS_HEADERS
    OUT_FILES_VAR GENESIS_EDITOR_SHADERS
)
bgfx_compile_shaders(
    TYPE COMPUTE
    SHADERS "${GENESIS_SHADER_DIR}/cs_histogram_clear.sc" "${GENESIS_SHADER_DIR}/cs_histogram.sc" "${GENESIS_SHADER_DIR}/cs_auto_exposure.sc"
    OUTPUT_DIR "${GENESIS_GENERATED_SHADERS}"
    INCLUDE_DIRS "${GENESIS_SHADER_DIR}" "${BGFX_DIR}/src"
    PROFILES ${GENESIS_SHADER_PROFILE}
    AS_HEADERS
    OUT_FILES_VAR GENESIS_COMPUTE_SHADERS
)

# Renderer code uses neutral shader names; only the build selects the GPU language.
foreach(shader IN LISTS GENESIS_VERTEX_SHADERS GENESIS_FRAGMENT_SHADERS GENESIS_COMPUTE_SHADERS GENESIS_EDITOR_SHADERS)
    get_filename_component(shaderFile "${shader}" NAME)
    string(REPLACE ".sc.bin.h" "" shaderName "${shaderFile}")
    file(CONFIGURE OUTPUT "${GENESIS_GENERATED_SHADERS}/compiled/${shaderName}.h"
        CONTENT "#pragma once\n#include <${GENESIS_SHADER_VARIANT}/${shaderFile}>\nstatic constexpr auto& ${shaderName}_shader = ${shaderName}_${GENESIS_SHADER_VARIANT};\n")
endforeach()

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" GENESIS_ROOT_NORMALIZED)
if(EMSCRIPTEN)
    set(GENESIS_ROOT_NORMALIZED "/genesis")
endif()
add_library(genesis_ui STATIC src/ui/Toolkit.cpp src/ui/EditorWidgets.cpp src/ui/DockSpace.cpp src/ui/LauncherView.cpp src/ui/RendererHud.cpp)
target_include_directories(genesis_ui PUBLIC src)
target_compile_definitions(genesis_ui PRIVATE GENESIS_ROOT="${GENESIS_ROOT_NORMALIZED}")
target_link_libraries(genesis_ui PRIVATE yogacore thorvg)
target_compile_options(genesis_ui PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
set_target_properties(genesis_ui PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")
add_library(genesis_scene STATIC src/GameplayWorld.cpp src/SceneDocument.cpp src/ProjectLoader.cpp src/ModelValidation.cpp thirdparty/render_deps/jsoncpp.cpp)
target_include_directories(genesis_scene PUBLIC src thirdparty/render_deps)
target_link_libraries(genesis_scene PUBLIC EnTT::EnTT)
target_compile_options(genesis_scene PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
set_target_properties(genesis_scene PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")

option(GENESIS_WITH_PHYSX_PBD "Build Windows NVIDIA PhysX particles" OFF)
if(GENESIS_WITH_PHYSX_PBD)
    if(NOT WIN32)
        message(FATAL_ERROR "PhysX GPU particles currently require Windows")
    endif()
    set(GENESIS_PHYSX_ROOT "" CACHE PATH "Path to PhysX physx source directory")
    set(GENESIS_PHYSX_LIB_DIR "" CACHE PATH "Path to PhysX release static libraries and GPU DLL")
    set(GENESIS_CUDA_ROOT "" CACHE PATH "Path to CUDA toolkit")
    foreach(required GENESIS_PHYSX_ROOT GENESIS_PHYSX_LIB_DIR GENESIS_CUDA_ROOT)
        if(NOT EXISTS "${${required}}")
            message(FATAL_ERROR "${required} must point to an existing directory")
        endif()
    endforeach()
    add_library(genesis_physx STATIC src/physics/PhysxParticles.cpp)
    target_include_directories(genesis_physx PUBLIC src PRIVATE "${GENESIS_PHYSX_ROOT}/include" "${GENESIS_CUDA_ROOT}/include" thirdparty/bx/include)
    target_compile_definitions(genesis_physx PRIVATE PX_PHYSX_STATIC_LIB PX_PUBLIC_RELEASE=1 PX_SUPPORT_OMNI_PVD=0 PX_SUPPORT_PVD=0)
    target_compile_options(genesis_physx PRIVATE /EHsc /Zc:__cplusplus /Zc:preprocessor)
    target_link_libraries(genesis_physx PUBLIC genesis_scene bx
        "${GENESIS_PHYSX_LIB_DIR}/PhysXExtensions_static.lib"
        "${GENESIS_PHYSX_LIB_DIR}/PhysXCooking_static.lib"
        "${GENESIS_PHYSX_LIB_DIR}/PhysX_static.lib"
        "${GENESIS_PHYSX_LIB_DIR}/PhysXCommon_static.lib"
        "${GENESIS_PHYSX_LIB_DIR}/PhysXFoundation_static.lib"
        "${GENESIS_PHYSX_LIB_DIR}/PhysXPvdSDK_static.lib")
endif()
add_library(genesis_editor_ui STATIC tools/editor/UiWorkbench.cpp tools/editor/GameEditor.cpp tools/editor/ViewportSidebar.cpp tools/editor/EditMesh.cpp tools/editor/EditMode.cpp tools/editor/ComponentInspector.cpp tools/editor/WorkspaceLayout.cpp tools/editor/PlaySession.cpp tools/editor/ProjectAssets.cpp tools/editor/AssetThumbnails.cpp tools/editor/ProjectBrowser.cpp)
if(GENESIS_WITH_PHYSX_PBD)
    target_compile_definitions(genesis_editor_ui PUBLIC GENESIS_WITH_PHYSX_PBD=1)
endif()
target_include_directories(genesis_editor_ui PUBLIC tools)
target_link_libraries(genesis_editor_ui PUBLIC genesis_ui genesis_scene)
set_target_properties(genesis_editor_ui PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")

add_executable(Genesis
    src/main.cpp
    src/backends/raster/BgfxRasterBackend.cpp
    src/backends/raster/BloomRenderer.cpp
    src/backends/raster/EditorOverlay.cpp
    src/backends/raster/BgfxUiRenderer.cpp
    src/backends/raster/LightSourceRenderer.cpp
    src/backends/raster/LightPatternLoader.cpp
    src/backends/raster/HdriTexture.cpp
    src/backends/raster/GltfTextures.cpp
    src/lighting/EnvironmentLighting.cpp
    src/RenderScene.cpp
    src/ui/ProjectPrompt.cpp
    src/ui/SdlUiInput.cpp
    tools/editor/UiWorkbenchWindow.cpp
    tools/editor/SdlEditorHost.cpp
    src/ScriptRuntime.cpp
    ${GENESIS_VERTEX_SHADERS}
    ${GENESIS_FRAGMENT_SHADERS}
    ${GENESIS_COMPUTE_SHADERS}
    ${GENESIS_EDITOR_SHADERS}
)
target_compile_definitions(Genesis PRIVATE GENESIS_ROOT="${GENESIS_ROOT_NORMALIZED}" _CRT_SECURE_NO_WARNINGS)
target_include_directories(Genesis PRIVATE src thirdparty/render_deps "${GENESIS_GENERATED_SHADERS}")
target_link_libraries(Genesis PRIVATE bgfx bx bimg bimg_decode bimg_encode SDL3::SDL3-static pocketpy EnTT::EnTT
    genesis_ui genesis_editor_ui genesis_atmosphere genesis_color)
if(GENESIS_WITH_PHYSX_PBD)
    target_link_libraries(Genesis PRIVATE genesis_physx)
    add_custom_command(TARGET Genesis POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GENESIS_PHYSX_LIB_DIR}/PhysXGpu_64.dll" "$<TARGET_FILE_DIR:Genesis>")
endif()
set(GENESIS_RELEASE_BINARY_NAME "genesis" CACHE STRING "Release executable basename")
set_target_properties(Genesis PROPERTIES
    OUTPUT_NAME_DEBUG genesisD
    OUTPUT_NAME_RELEASE "${GENESIS_RELEASE_BINARY_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Genesis)

if(EMSCRIPTEN)
    include(cmake/Web.cmake)
endif()

include(CTest)
if(BUILD_TESTING AND NOT EMSCRIPTEN)
    if(GENESIS_WITH_PHYSX_PBD)
        add_executable(PhysxParticlesTests tests/physx_particles.cpp)
        target_link_libraries(PhysxParticlesTests PRIVATE genesis_physx)
        set_target_properties(PhysxParticlesTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")
        add_custom_command(TARGET PhysxParticlesTests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GENESIS_PHYSX_LIB_DIR}/PhysXGpu_64.dll" "$<TARGET_FILE_DIR:PhysxParticlesTests>")
        add_test(NAME physx_particles COMMAND PhysxParticlesTests)
    endif()
    add_executable(EditorNavigationTests tests/editor_navigation.cpp tools/editor/SdlEditorHost.cpp src/ui/SdlUiInput.cpp)
    target_link_libraries(EditorNavigationTests PRIVATE genesis_editor_ui SDL3::SDL3-static)
    target_compile_definitions(EditorNavigationTests PRIVATE GENESIS_ROOT="${GENESIS_ROOT_NORMALIZED}")
    set_target_properties(EditorNavigationTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")
    add_test(NAME editor_navigation COMMAND EditorNavigationTests)
    add_executable(GameEditorTests tests/game_editor.cpp)
    target_link_libraries(GameEditorTests PRIVATE genesis_editor_ui)
    target_compile_definitions(GameEditorTests PRIVATE GENESIS_ROOT="${GENESIS_ROOT_NORMALIZED}")
    set_target_properties(GameEditorTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")
    add_test(NAME game_editor COMMAND GameEditorTests)
    add_executable(UiToolkitTests tests/ui_toolkit.cpp)
    target_link_libraries(UiToolkitTests PRIVATE genesis_ui genesis_editor_ui)
    target_compile_definitions(UiToolkitTests PRIVATE GENESIS_ROOT="${GENESIS_ROOT_NORMALIZED}")
    set_target_properties(UiToolkitTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")
    add_test(NAME ui_toolkit COMMAND UiToolkitTests)
    add_executable(EnvironmentLightingTests tests/environment_lighting.cpp src/lighting/EnvironmentLighting.cpp)
    target_include_directories(EnvironmentLightingTests PRIVATE src)
    set_target_properties(EnvironmentLightingTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")
    add_test(NAME environment_lighting COMMAND EnvironmentLightingTests)
    add_executable(MaterialOcclusionTests tests/material_occlusion.cpp)
    target_include_directories(MaterialOcclusionTests PRIVATE src thirdparty/render_deps thirdparty/bgfx/include)
    set_target_properties(MaterialOcclusionTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")
    add_test(NAME material_occlusion COMMAND MaterialOcclusionTests)
    add_executable(TextureMipTests tests/texture_mips.cpp)
    target_include_directories(TextureMipTests PRIVATE src)
    set_target_properties(TextureMipTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")
    add_test(NAME texture_mips COMMAND TextureMipTests)
    add_executable(TextureCoordinateTests tests/texture_coordinates.cpp)
    target_include_directories(TextureCoordinateTests PRIVATE src thirdparty/render_deps)
    set_target_properties(TextureCoordinateTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GENESIS_OUTPUT_DIR}")
    add_test(NAME texture_coordinates COMMAND TextureCoordinateTests)
endif()
