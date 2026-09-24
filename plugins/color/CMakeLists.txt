add_library(genesis_color STATIC src/ColorPipeline.cpp)
target_include_directories(genesis_color PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(genesis_color PUBLIC cxx_std_20)

if(MSVC)
    set_property(TARGET genesis_color PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_target_properties(genesis_color PROPERTIES FOLDER "Genesis/Plugins")
