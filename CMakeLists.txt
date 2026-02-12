cmake_minimum_required(VERSION 3.20)
project(v_type C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_subdirectory(DefconDraw)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
find_package(Vulkan REQUIRED)

add_executable(v_type
    src/game.c
    src/main.c
    src/render.c
    src/wavetable_poly_synth_lib.c
)
set_target_properties(v_type PROPERTIES OUTPUT_NAME "VectorSwarm")

target_include_directories(v_type PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(v_type PRIVATE vg ${SDL2_LIBRARIES} Vulkan::Vulkan m)
target_compile_options(v_type PRIVATE ${SDL2_CFLAGS_OTHER})

if(TARGET vg_demo_vk_shaders)
    add_dependencies(v_type vg_demo_vk_shaders)
    target_compile_definitions(v_type PRIVATE V_TYPE_HAS_POST_SHADERS=1)
    target_include_directories(v_type PRIVATE ${CMAKE_BINARY_DIR}/DefconDraw/generated/demo_shaders)
else()
    target_compile_definitions(v_type PRIVATE V_TYPE_HAS_POST_SHADERS=0)
endif()
