add_library(genesis_atmosphere STATIC
    src/BrunetonAtmosphere.cpp
    src/Clouds.cpp
)

target_include_directories(genesis_atmosphere PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(genesis_atmosphere PUBLIC cxx_std_20)

if(MSVC)
    set_property(TARGET genesis_atmosphere PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_target_properties(genesis_atmosphere PROPERTIES
    FOLDER "Genesis/Plugins"
)
