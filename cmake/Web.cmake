# Browser packaging for the same C++ editor and raster backend as desktop.
target_sources(Genesis PRIVATE src/platform/web/BrowserHost.cpp)
target_link_options(Genesis PRIVATE
    # C++ still compiles at the configuration's optimization level. Avoid the
    # very expensive Binaryen -O3 whole-program passes on the large renderer.
    -O1
    -sJSPI=1
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=2147483648
    -sSTACK_SIZE=8388608 -sASSERTIONS=1 -sFORCE_FILESYSTEM=1
    "-sEXPORTED_RUNTIME_METHODS=['FS','ccall']"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/web/shell.html"
)
set_target_properties(Genesis PROPERTIES SUFFIX ".html" OUTPUT_NAME_DEBUG genesis
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bundle")
add_custom_command(TARGET Genesis POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GENESIS_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:Genesis>/genesis.html"
        "$<TARGET_FILE_DIR:Genesis>/genesis.js"
        "$<TARGET_FILE_DIR:Genesis>/genesis.wasm"
        "$<TARGET_FILE_DIR:Genesis>/genesis.data"
        "${GENESIS_OUTPUT_DIR}"
    VERBATIM
)
set_property(TARGET Genesis APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/web/shell.html")

# Deliberately package the starter project, not the multi-GB desktop asset tree.
set(web_assets python/genesis assets/fonts/inter examples/editor_project)
foreach(model Box BoxTextured DamagedHelmet)
    list(APPEND web_assets "assets/glTF-Sample-Models/2.0/${model}/glTF-Binary")
endforeach()
foreach(lut transmittance scattering single_mie irradiance)
    list(APPEND web_assets "assets/atmosphere/bruneton/${lut}.rgb32f")
endforeach()
foreach(asset IN LISTS web_assets)
    target_link_options(Genesis PRIVATE "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/${asset}@/genesis/${asset}")
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
        file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}/*")
        set_property(TARGET Genesis APPEND PROPERTY LINK_DEPENDS ${asset_files})
    else()
        set_property(TARGET Genesis APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
    endif()
endforeach()
