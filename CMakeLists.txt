cmake_minimum_required(VERSION 3.16)
project(3dsnes C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Dependencies ---
find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# --- glad (OpenGL loader, bundled) ---
add_library(glad STATIC ext/glad/src/glad.c)
target_include_directories(glad PUBLIC ext/glad/include)

# --- LakeSnes (SNES emulation core) ---
file(GLOB LAKESNES_SRC ext/LakeSnes/snes/*.c)
add_library(lakesnes STATIC ${LAKESNES_SRC})
target_include_directories(lakesnes PUBLIC ext/LakeSnes)

# --- 3dSNES executable ---
file(GLOB TDSNES_SRC src/*.c)
list(FILTER TDSNES_SRC EXCLUDE REGEX "test_emu\\.c$")
add_executable(3dsnes ${TDSNES_SRC})
target_include_directories(3dsnes PRIVATE include ext/LakeSnes ext/glad/include)
target_link_libraries(3dsnes PRIVATE
    lakesnes
    glad
    SDL2::SDL2
    SDL2::SDL2main
    OpenGL::GL
)

# --- Test emulator (minimal, no 3D) ---
add_executable(test_emu src/test_emu.c src/ppu_extract.c src/voxelizer.c src/renderer.c src/camera.c)
target_include_directories(test_emu PRIVATE include ext/LakeSnes ext/glad/include)
target_link_libraries(test_emu PRIVATE lakesnes glad SDL2::SDL2 SDL2::SDL2main OpenGL::GL)

# Copy SDL2 DLL on Windows
if(WIN32)
    add_custom_command(TARGET 3dsnes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:3dsnes>
    )
endif()
