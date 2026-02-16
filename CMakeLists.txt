cmake_minimum_required(VERSION 3.20)
project(v_type C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_subdirectory(DefconDraw)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_IMAGE QUIET SDL2_image)
find_package(Vulkan REQUIRED)

add_executable(v_type
    src/acoustics_ui_layout.c
    src/game.c
    src/level_editor.c
    src/leveldef.c
    src/main.c
    src/planetarium/planetarium_registry.c
    src/planetarium/planetarium_validate.c
    src/render.c
    src/wavetable_poly_synth_lib.c
)
set_target_properties(v_type PROPERTIES OUTPUT_NAME "VectorSwarm")

target_include_directories(v_type PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(v_type PRIVATE vg ${SDL2_LIBRARIES} Vulkan::Vulkan m)
target_compile_options(v_type PRIVATE ${SDL2_CFLAGS_OTHER})
if(SDL2_IMAGE_FOUND)
    target_compile_definitions(v_type PRIVATE V_TYPE_HAS_SDL_IMAGE=1)
    target_include_directories(v_type PRIVATE ${SDL2_IMAGE_INCLUDE_DIRS})
    target_link_libraries(v_type PRIVATE ${SDL2_IMAGE_LIBRARIES})
    target_compile_options(v_type PRIVATE ${SDL2_IMAGE_CFLAGS_OTHER})
else()
    target_compile_definitions(v_type PRIVATE V_TYPE_HAS_SDL_IMAGE=0)
endif()

if(TARGET vg_demo_vk_shaders)
    add_dependencies(v_type vg_demo_vk_shaders)
    target_compile_definitions(v_type PRIVATE V_TYPE_HAS_POST_SHADERS=1)
    target_include_directories(v_type PRIVATE ${CMAKE_BINARY_DIR}/DefconDraw/generated/demo_shaders)
else()
    target_compile_definitions(v_type PRIVATE V_TYPE_HAS_POST_SHADERS=0)
endif()

find_program(GLSLANG_VALIDATOR glslangValidator)
find_program(XXD_EXECUTABLE xxd)
if(GLSLANG_VALIDATOR AND XXD_EXECUTABLE)
    set(V_TYPE_SHADER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)
    file(MAKE_DIRECTORY ${V_TYPE_SHADER_GEN_DIR})

    set(V_TYPE_TERRAIN_VERT_SPV ${V_TYPE_SHADER_GEN_DIR}/terrain.vert.spv)
    set(V_TYPE_TERRAIN_FRAG_SPV ${V_TYPE_SHADER_GEN_DIR}/terrain.frag.spv)
    set(V_TYPE_TERRAIN_WIRE_VERT_SPV ${V_TYPE_SHADER_GEN_DIR}/terrain_wire.vert.spv)
    set(V_TYPE_TERRAIN_WIRE_FRAG_SPV ${V_TYPE_SHADER_GEN_DIR}/terrain_wire.frag.spv)
    set(V_TYPE_PARTICLE_VERT_SPV ${V_TYPE_SHADER_GEN_DIR}/particle.vert.spv)
    set(V_TYPE_PARTICLE_FRAG_SPV ${V_TYPE_SHADER_GEN_DIR}/particle.frag.spv)
    set(V_TYPE_PARTICLE_BLOOM_FRAG_SPV ${V_TYPE_SHADER_GEN_DIR}/particle_bloom.frag.spv)
    set(V_TYPE_WORMHOLE_LINE_VERT_SPV ${V_TYPE_SHADER_GEN_DIR}/wormhole_line.vert.spv)
    set(V_TYPE_WORMHOLE_LINE_FRAG_SPV ${V_TYPE_SHADER_GEN_DIR}/wormhole_line.frag.spv)
    set(V_TYPE_FOG_VERT_SPV ${V_TYPE_SHADER_GEN_DIR}/fog.vert.spv)
    set(V_TYPE_FOG_FRAG_SPV ${V_TYPE_SHADER_GEN_DIR}/fog.frag.spv)
    set(V_TYPE_TERRAIN_VERT_HDR ${V_TYPE_SHADER_GEN_DIR}/terrain_vert_spv.h)
    set(V_TYPE_TERRAIN_FRAG_HDR ${V_TYPE_SHADER_GEN_DIR}/terrain_frag_spv.h)
    set(V_TYPE_TERRAIN_WIRE_VERT_HDR ${V_TYPE_SHADER_GEN_DIR}/terrain_wire_vert_spv.h)
    set(V_TYPE_TERRAIN_WIRE_FRAG_HDR ${V_TYPE_SHADER_GEN_DIR}/terrain_wire_frag_spv.h)
    set(V_TYPE_PARTICLE_VERT_HDR ${V_TYPE_SHADER_GEN_DIR}/particle_vert_spv.h)
    set(V_TYPE_PARTICLE_FRAG_HDR ${V_TYPE_SHADER_GEN_DIR}/particle_frag_spv.h)
    set(V_TYPE_PARTICLE_BLOOM_FRAG_HDR ${V_TYPE_SHADER_GEN_DIR}/particle_bloom_frag_spv.h)
    set(V_TYPE_WORMHOLE_LINE_VERT_HDR ${V_TYPE_SHADER_GEN_DIR}/wormhole_line_vert_spv.h)
    set(V_TYPE_WORMHOLE_LINE_FRAG_HDR ${V_TYPE_SHADER_GEN_DIR}/wormhole_line_frag_spv.h)
    set(V_TYPE_FOG_VERT_HDR ${V_TYPE_SHADER_GEN_DIR}/fog_vert_spv.h)
    set(V_TYPE_FOG_FRAG_HDR ${V_TYPE_SHADER_GEN_DIR}/fog_frag_spv.h)

    add_custom_command(
        OUTPUT ${V_TYPE_TERRAIN_VERT_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain.vert -o ${V_TYPE_TERRAIN_VERT_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_terrain_vert_spv ${V_TYPE_TERRAIN_VERT_SPV} > ${V_TYPE_TERRAIN_VERT_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain.vert
    )
    add_custom_command(
        OUTPUT ${V_TYPE_TERRAIN_FRAG_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain.frag -o ${V_TYPE_TERRAIN_FRAG_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_terrain_frag_spv ${V_TYPE_TERRAIN_FRAG_SPV} > ${V_TYPE_TERRAIN_FRAG_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain.frag
    )
    add_custom_command(
        OUTPUT ${V_TYPE_TERRAIN_WIRE_VERT_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain_wire.vert -o ${V_TYPE_TERRAIN_WIRE_VERT_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_terrain_wire_vert_spv ${V_TYPE_TERRAIN_WIRE_VERT_SPV} > ${V_TYPE_TERRAIN_WIRE_VERT_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain_wire.vert
    )
    add_custom_command(
        OUTPUT ${V_TYPE_TERRAIN_WIRE_FRAG_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain_wire.frag -o ${V_TYPE_TERRAIN_WIRE_FRAG_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_terrain_wire_frag_spv ${V_TYPE_TERRAIN_WIRE_FRAG_SPV} > ${V_TYPE_TERRAIN_WIRE_FRAG_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/terrain_wire.frag
    )
    add_custom_command(
        OUTPUT ${V_TYPE_PARTICLE_VERT_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/particle.vert -o ${V_TYPE_PARTICLE_VERT_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_particle_vert_spv ${V_TYPE_PARTICLE_VERT_SPV} > ${V_TYPE_PARTICLE_VERT_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/particle.vert
    )
    add_custom_command(
        OUTPUT ${V_TYPE_PARTICLE_FRAG_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/particle.frag -o ${V_TYPE_PARTICLE_FRAG_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_particle_frag_spv ${V_TYPE_PARTICLE_FRAG_SPV} > ${V_TYPE_PARTICLE_FRAG_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/particle.frag
    )
    add_custom_command(
        OUTPUT ${V_TYPE_PARTICLE_BLOOM_FRAG_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/particle_bloom.frag -o ${V_TYPE_PARTICLE_BLOOM_FRAG_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_particle_bloom_frag_spv ${V_TYPE_PARTICLE_BLOOM_FRAG_SPV} > ${V_TYPE_PARTICLE_BLOOM_FRAG_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/particle_bloom.frag
    )
    add_custom_command(
        OUTPUT ${V_TYPE_WORMHOLE_LINE_VERT_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/wormhole_line.vert -o ${V_TYPE_WORMHOLE_LINE_VERT_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_wormhole_line_vert_spv ${V_TYPE_WORMHOLE_LINE_VERT_SPV} > ${V_TYPE_WORMHOLE_LINE_VERT_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/wormhole_line.vert
    )
    add_custom_command(
        OUTPUT ${V_TYPE_WORMHOLE_LINE_FRAG_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/wormhole_line.frag -o ${V_TYPE_WORMHOLE_LINE_FRAG_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_wormhole_line_frag_spv ${V_TYPE_WORMHOLE_LINE_FRAG_SPV} > ${V_TYPE_WORMHOLE_LINE_FRAG_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/wormhole_line.frag
    )
    add_custom_command(
        OUTPUT ${V_TYPE_FOG_VERT_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/fog.vert -o ${V_TYPE_FOG_VERT_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_fog_vert_spv ${V_TYPE_FOG_VERT_SPV} > ${V_TYPE_FOG_VERT_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/fog.vert
    )
    add_custom_command(
        OUTPUT ${V_TYPE_FOG_FRAG_HDR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/fog.frag -o ${V_TYPE_FOG_FRAG_SPV}
        COMMAND ${XXD_EXECUTABLE} -i -n v_type_fog_frag_spv ${V_TYPE_FOG_FRAG_SPV} > ${V_TYPE_FOG_FRAG_HDR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/fog.frag
    )
    add_custom_target(v_type_vk_shaders DEPENDS ${V_TYPE_TERRAIN_VERT_HDR} ${V_TYPE_TERRAIN_FRAG_HDR} ${V_TYPE_TERRAIN_WIRE_VERT_HDR} ${V_TYPE_TERRAIN_WIRE_FRAG_HDR} ${V_TYPE_PARTICLE_VERT_HDR} ${V_TYPE_PARTICLE_FRAG_HDR} ${V_TYPE_PARTICLE_BLOOM_FRAG_HDR} ${V_TYPE_WORMHOLE_LINE_VERT_HDR} ${V_TYPE_WORMHOLE_LINE_FRAG_HDR} ${V_TYPE_FOG_VERT_HDR} ${V_TYPE_FOG_FRAG_HDR})
    add_dependencies(v_type v_type_vk_shaders)
    target_compile_definitions(v_type PRIVATE V_TYPE_HAS_TERRAIN_SHADERS=1)
    target_include_directories(v_type PRIVATE ${V_TYPE_SHADER_GEN_DIR})
else()
    target_compile_definitions(v_type PRIVATE V_TYPE_HAS_TERRAIN_SHADERS=0)
endif()
