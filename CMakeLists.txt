cmake_minimum_required(VERSION 3.25)

project(genesis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SDL remains the engine's platform layer. RTXPT is intentionally built as its
# own project because it owns a complete Donut/NVRHI renderer configuration.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/SDL EXCLUDE_FROM_ALL)

# PocketPy is the embedded scripting runtime used by engine demos and examples.
set(PK_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(PK_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(PK_ENABLE_OS ON CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/pocketpy EXCLUDE_FROM_ALL)

add_executable(genesis src/main.cpp)
target_link_libraries(genesis PRIVATE SDL3::SDL3 pocketpy)
target_compile_definitions(genesis PRIVATE
    GENESIS_DEMO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/demos"
)

if(MSVC)
    target_compile_options(genesis PRIVATE /W4 /permissive-)
else()
    target_compile_options(genesis PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(genesis PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:genesis>"
)
