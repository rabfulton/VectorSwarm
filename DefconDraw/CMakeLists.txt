cmake_minimum_required(VERSION 3.20)
project(vectorgfx C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(VG_BUILD_EXAMPLES "Build examples" ON)
option(VG_BUILD_SDL_PREVIEW "Build SDL preview example" ON)
option(VG_BUILD_VK_SDL_EXAMPLE "Build Vulkan + SDL example" ON)

add_library(vg STATIC
    src/vg.c
    src/vg_image.c
    src/vg_palette.c
    src/vg_pointer.c
    src/vg_svg.c
    third_party/libtess2/Source/bucketalloc.c
    third_party/libtess2/Source/dict.c
    third_party/libtess2/Source/geom.c
    third_party/libtess2/Source/mesh.c
    third_party/libtess2/Source/priorityq.c
    third_party/libtess2/Source/sweep.c
    third_party/libtess2/Source/tess.c
    src/vg_text_fx.c
    src/vg_text_layout.c
    src/vg_ui.c
    src/vg_ui_ext.c
    src/backends/vulkan/vg_vk.c
    src/fx/vg_fx.c
)

find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    target_compile_definitions(vg PRIVATE VG_HAS_VULKAN=1)
    target_link_libraries(vg PRIVATE Vulkan::Vulkan)

    find_program(GLSLANG_VALIDATOR glslangValidator)
    find_program(XXD_EXECUTABLE xxd)
    if(GLSLANG_VALIDATOR AND XXD_EXECUTABLE)
        set(VG_SHADER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)
        file(MAKE_DIRECTORY ${VG_SHADER_GEN_DIR})

        set(VG_VERT_SPV ${VG_SHADER_GEN_DIR}/line.vert.spv)
        set(VG_FRAG_SPV ${VG_SHADER_GEN_DIR}/line.frag.spv)
        set(VG_VERT_HDR ${VG_SHADER_GEN_DIR}/line_vert_spv.h)
        set(VG_FRAG_HDR ${VG_SHADER_GEN_DIR}/line_frag_spv.h)

        add_custom_command(
            OUTPUT ${VG_VERT_HDR}
            COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/line.vert -o ${VG_VERT_SPV}
            COMMAND ${XXD_EXECUTABLE} -i -n vg_line_vert_spv ${VG_VERT_SPV} > ${VG_VERT_HDR}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/line.vert
            VERBATIM
        )
        add_custom_command(
            OUTPUT ${VG_FRAG_HDR}
            COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/line.frag -o ${VG_FRAG_SPV}
            COMMAND ${XXD_EXECUTABLE} -i -n vg_line_frag_spv ${VG_FRAG_SPV} > ${VG_FRAG_HDR}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan/shaders/line.frag
            VERBATIM
        )

        add_custom_target(vg_vk_shaders DEPENDS ${VG_VERT_HDR} ${VG_FRAG_HDR})
        add_dependencies(vg vg_vk_shaders)
        target_compile_definitions(vg PRIVATE VG_HAS_VK_INTERNAL_PIPELINE=1)
        target_include_directories(vg PRIVATE ${VG_SHADER_GEN_DIR})
    else()
        target_compile_definitions(vg PRIVATE VG_HAS_VK_INTERNAL_PIPELINE=0)
    endif()
else()
    target_compile_definitions(vg PRIVATE VG_HAS_VULKAN=0)
    target_compile_definitions(vg PRIVATE VG_HAS_VK_INTERNAL_PIPELINE=0)
endif()

target_include_directories(vg
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libtess2/Include
)

target_compile_options(vg PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
target_link_libraries(vg PRIVATE m)

if(VG_BUILD_EXAMPLES)
    add_executable(vg_demo examples/demo.c)
    target_link_libraries(vg_demo PRIVATE vg)
endif()

if(VG_BUILD_SDL_PREVIEW)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)

    add_executable(vg_demo_sdl examples/demo_sdl.c)
    target_include_directories(vg_demo_sdl PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(vg_demo_sdl PRIVATE vg ${SDL2_LIBRARIES} m)
    target_compile_options(vg_demo_sdl PRIVATE ${SDL2_CFLAGS_OTHER})
endif()

if(VG_BUILD_VK_SDL_EXAMPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_IMAGE QUIET SDL2_image)
    if(Vulkan_FOUND AND GLSLANG_VALIDATOR AND XXD_EXECUTABLE)
        set(VG_DEMO_SHADER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/demo_shaders)
        file(MAKE_DIRECTORY ${VG_DEMO_SHADER_GEN_DIR})

        set(VG_DEMO_FSQ_SPV ${VG_DEMO_SHADER_GEN_DIR}/fullscreen.vert.spv)
        set(VG_DEMO_BLOOM_SPV ${VG_DEMO_SHADER_GEN_DIR}/bloom.frag.spv)
        set(VG_DEMO_COMP_SPV ${VG_DEMO_SHADER_GEN_DIR}/composite.frag.spv)
        set(VG_DEMO_FSQ_HDR ${VG_DEMO_SHADER_GEN_DIR}/demo_fullscreen_vert_spv.h)
        set(VG_DEMO_BLOOM_HDR ${VG_DEMO_SHADER_GEN_DIR}/demo_bloom_frag_spv.h)
        set(VG_DEMO_COMP_HDR ${VG_DEMO_SHADER_GEN_DIR}/demo_composite_frag_spv.h)

        add_custom_command(
            OUTPUT ${VG_DEMO_FSQ_HDR}
            COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/fullscreen.vert -o ${VG_DEMO_FSQ_SPV}
            COMMAND ${XXD_EXECUTABLE} -i -n demo_fullscreen_vert_spv ${VG_DEMO_FSQ_SPV} > ${VG_DEMO_FSQ_HDR}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/fullscreen.vert
            VERBATIM
        )
        add_custom_command(
            OUTPUT ${VG_DEMO_BLOOM_HDR}
            COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/bloom.frag -o ${VG_DEMO_BLOOM_SPV}
            COMMAND ${XXD_EXECUTABLE} -i -n demo_bloom_frag_spv ${VG_DEMO_BLOOM_SPV} > ${VG_DEMO_BLOOM_HDR}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/bloom.frag
            VERBATIM
        )
        add_custom_command(
            OUTPUT ${VG_DEMO_COMP_HDR}
            COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/composite.frag -o ${VG_DEMO_COMP_SPV}
            COMMAND ${XXD_EXECUTABLE} -i -n demo_composite_frag_spv ${VG_DEMO_COMP_SPV} > ${VG_DEMO_COMP_HDR}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/shaders/composite.frag
            VERBATIM
        )

        add_custom_target(vg_demo_vk_shaders DEPENDS ${VG_DEMO_FSQ_HDR} ${VG_DEMO_BLOOM_HDR} ${VG_DEMO_COMP_HDR})
        add_executable(vg_demo_vk_sdl examples/demo_vk_sdl.c)
        add_dependencies(vg_demo_vk_sdl vg_demo_vk_shaders)
        target_compile_definitions(vg_demo_vk_sdl PRIVATE VG_DEMO_HAS_POST_SHADERS=1)
        if(SDL2_IMAGE_FOUND)
            target_compile_definitions(vg_demo_vk_sdl PRIVATE VG_DEMO_HAS_SDL_IMAGE=1)
        else()
            target_compile_definitions(vg_demo_vk_sdl PRIVATE VG_DEMO_HAS_SDL_IMAGE=0)
        endif()
        target_include_directories(vg_demo_vk_sdl PRIVATE ${VG_DEMO_SHADER_GEN_DIR})
        target_include_directories(vg_demo_vk_sdl PRIVATE ${SDL2_INCLUDE_DIRS})
        if(SDL2_IMAGE_FOUND)
            target_include_directories(vg_demo_vk_sdl PRIVATE ${SDL2_IMAGE_INCLUDE_DIRS})
            target_link_libraries(vg_demo_vk_sdl PRIVATE vg ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES} Vulkan::Vulkan m)
            target_compile_options(vg_demo_vk_sdl PRIVATE ${SDL2_IMAGE_CFLAGS_OTHER})
        else()
            target_link_libraries(vg_demo_vk_sdl PRIVATE vg ${SDL2_LIBRARIES} Vulkan::Vulkan m)
        endif()
        target_compile_options(vg_demo_vk_sdl PRIVATE ${SDL2_CFLAGS_OTHER})
    endif()
endif()
