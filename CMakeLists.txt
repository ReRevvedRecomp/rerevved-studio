cmake_minimum_required(VERSION 3.25)
project(rerevved_studio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(CTest)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG a74efa0d5628b74adc0426af4c5710e287fa7c2c
)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG fd13a1e8923a0a7077b404fc36fd063b25a0c0b5
)
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG 5690d4671d7ad07ae6021756d7222eb159745f06
)
FetchContent_MakeAvailable(glfw imgui dr_libs)

find_package(OpenGL REQUIRED)

add_library(rerevved_studio_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(rerevved_studio_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(rerevved_studio_imgui PUBLIC glfw OpenGL::GL)

add_library(rerevved_studio_dr_mp3 STATIC src/third_party/dr_mp3.cpp)
target_include_directories(rerevved_studio_dr_mp3 SYSTEM PRIVATE ${dr_libs_SOURCE_DIR})

add_library(rerevved_studio_core STATIC
    src/core/dds_document.cpp
    src/core/dds_metadata.cpp
    src/core/file_inspection.cpp
    src/core/file_kind.cpp
    src/core/fpk_document.cpp
    src/core/fpk_extraction.cpp
    src/core/fpk_index.cpp
    src/core/gfx_document.cpp
    src/core/map_document.cpp
    src/core/mp3_document.cpp
    src/core/nif_document.cpp
)
target_include_directories(rerevved_studio_core PUBLIC api)
target_include_directories(rerevved_studio_core SYSTEM PRIVATE ${dr_libs_SOURCE_DIR})
target_link_libraries(rerevved_studio_core PRIVATE rerevved_studio_dr_mp3)

add_executable(rerevved-studio
    src/app/application.cpp
    src/app/archive_explorer.cpp
    src/app/dds_viewer.cpp
    src/app/gfx_inspector.cpp
    src/app/inspection_format.cpp
    src/app/map_preview.cpp
    src/app/main.cpp
    src/app/mp3_preview.cpp
    src/app/nif_inspector.cpp
)
target_include_directories(rerevved-studio PRIVATE src/app)
target_link_libraries(rerevved-studio PRIVATE
    rerevved_studio_core
    rerevved_studio_imgui
)
target_compile_definitions(rerevved-studio PRIVATE
    REREVVED_STUDIO_VERSION="${PROJECT_VERSION}"
)

function(rerevved_studio_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

rerevved_studio_enable_warnings(rerevved_studio_core)
rerevved_studio_enable_warnings(rerevved-studio)

if(BUILD_TESTING)
    add_executable(rerevved_studio_tests
        src/app/inspection_format.cpp
        tests/studio_tests.cpp
    )
    target_include_directories(rerevved_studio_tests PRIVATE src/app)
    target_link_libraries(rerevved_studio_tests PRIVATE rerevved_studio_core)
    rerevved_studio_enable_warnings(rerevved_studio_tests)
    add_test(NAME rerevved_studio COMMAND rerevved_studio_tests)
endif()
