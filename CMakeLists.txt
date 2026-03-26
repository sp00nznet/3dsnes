cmake_minimum_required(VERSION 3.16)
project(3dsnes C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies ---
find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# --- glad (OpenGL loader, bundled) ---
add_library(glad STATIC ext/glad/src/glad.c)
target_include_directories(glad PUBLIC ext/glad/include)

# --- LakeSnes (SNES emulation core) ---
file(GLOB LAKESNES_SRC ext/LakeSnes/snes/*.c)
add_library(lakesnes STATIC ${LAKESNES_SRC} ext/LakeSnes/zip/zip.c)
target_include_directories(lakesnes PUBLIC ext/LakeSnes ext/LakeSnes/zip)

# --- ImGui (UI library) ---
add_library(imgui STATIC
    ext/imgui/imgui.cpp
    ext/imgui/imgui_draw.cpp
    ext/imgui/imgui_tables.cpp
    ext/imgui/imgui_widgets.cpp
    ext/imgui/backends/imgui_impl_sdl2.cpp
    ext/imgui/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui PUBLIC ext/imgui ext/imgui/backends)
target_link_libraries(imgui PRIVATE SDL2::SDL2)

# --- 3dSNES executable ---
file(GLOB TDSNES_C_SRC src/*.c)
list(FILTER TDSNES_C_SRC EXCLUDE REGEX "test_emu\\.c$")
add_executable(3dsnes ${TDSNES_C_SRC} src/menu.cpp)
target_include_directories(3dsnes PRIVATE include ext/LakeSnes ext/glad/include ext)
target_link_libraries(3dsnes PRIVATE
    lakesnes
    glad
    imgui
    SDL2::SDL2
    SDL2::SDL2main
    OpenGL::GL
    $<$<PLATFORM_ID:Windows>:comdlg32>
)

# --- Test emulator (minimal, no 3D) ---
add_executable(test_emu src/test_emu.c src/ppu_extract.c src/voxelizer.c src/renderer.c src/camera.c)
target_include_directories(test_emu PRIVATE include ext/LakeSnes ext/glad/include)
target_include_directories(test_emu PRIVATE ext/LakeSnes/zip)
target_link_libraries(test_emu PRIVATE lakesnes glad SDL2::SDL2 SDL2::SDL2main OpenGL::GL)

# Copy SDL2 DLL on Windows
if(WIN32)
    add_custom_command(TARGET 3dsnes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:3dsnes>
    )
endif()
