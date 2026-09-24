set(THORVG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/thorvg")
set(THORVG_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/thorvg")
file(MAKE_DIRECTORY "${THORVG_GENERATED_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/thorvg_config.h.in"
    "${THORVG_GENERATED_DIR}/config.h"
    @ONLY
)

# ThorVG uses Meson upstream. Genesis builds the pinned software-raster subset
# directly so its normal CMake build remains the only required build tool.
file(GLOB THORVG_SOURCES CONFIGURE_DEPENDS
    "${THORVG_ROOT}/src/common/*.cpp"
    "${THORVG_ROOT}/src/renderer/*.cpp"
    "${THORVG_ROOT}/src/renderer/sw_engine/*.cpp"
    "${THORVG_ROOT}/src/loaders/raw/*.cpp"
    "${THORVG_ROOT}/src/loaders/svg/*.cpp"
    "${THORVG_ROOT}/src/loaders/ttf/*.cpp"
)
add_library(thorvg STATIC ${THORVG_SOURCES})
target_compile_definitions(thorvg PUBLIC TVG_STATIC PRIVATE NOMINMAX)
target_include_directories(thorvg
    PUBLIC "${THORVG_ROOT}/inc"
    PRIVATE
        "${THORVG_GENERATED_DIR}"
        "${THORVG_ROOT}/src/common"
        "${THORVG_ROOT}/src/renderer"
        "${THORVG_ROOT}/src/renderer/sw_engine"
        "${THORVG_ROOT}/src/loaders"
        "${THORVG_ROOT}/src/loaders/raw"
        "${THORVG_ROOT}/src/loaders/svg"
        "${THORVG_ROOT}/src/loaders/ttf"
)
