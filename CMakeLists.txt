cmake_minimum_required(VERSION 3.24)
project(fluid_simulation)

set(BINARY_DIR ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${BINARY_DIR})
make_directory(${BINARY_DIR})

set(GLM_BUILD_LIBRARY OFF)
add_subdirectory(lib/SDL)
add_subdirectory(lib/glm)
add_subdirectory(lib/json)
add_executable(fluid_simulation WIN32
    lib/imgui/imgui.cpp
    lib/imgui/imgui_demo.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_impl_sdl3.cpp
    lib/imgui/imgui_impl_sdlgpu3.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
    src/helpers.cpp
    src/main.cpp
    src/texture.cpp
)
set_target_properties(fluid_simulation PROPERTIES CXX_STANDARD 23)
target_include_directories(fluid_simulation PRIVATE lib/imgui)
target_link_libraries(fluid_simulation PRIVATE SDL3::SDL3 glm nlohmann_json)

find_program(GLSLC glslc)
find_program(SHADERCROSS shadercross)
function(add_shader FILE)
    set(DEPENDS ${ARGN})
    set(GLSL ${CMAKE_SOURCE_DIR}/shaders/${FILE})
    set(SPV ${CMAKE_SOURCE_DIR}/shaders/bin/${FILE}.spv)
    set(MSL ${CMAKE_SOURCE_DIR}/shaders/bin/${FILE}.msl)
    set(JSON ${CMAKE_SOURCE_DIR}/shaders/bin/${FILE}.json)
    function(compile PROGRAM SOURCE OUTPUT)
        add_custom_command(
            OUTPUT ${OUTPUT}
            COMMAND ${PROGRAM} ${SOURCE} -o ${OUTPUT} -I src
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS ${SOURCE} ${DEPENDS}
            COMMENT ${OUTPUT}
        )
        get_filename_component(NAME ${OUTPUT} NAME)
        string(REPLACE . _ NAME ${NAME})
        set(NAME compile_${NAME})
        add_custom_target(${NAME} DEPENDS ${OUTPUT})
        add_dependencies(fluid_simulation ${NAME})
    endfunction()
    if (EXISTS ${GLSLC} AND EXISTS ${SHADERCROSS})
        compile(${GLSLC} ${GLSL} ${SPV})
        compile(${SHADERCROSS} ${SPV} ${MSL})
        compile(${SHADERCROSS} ${SPV} ${JSON})
    else()
        message("Using prebuilts since SDL_shadercross is missing")
    endif()
    function(package OUTPUT)
        get_filename_component(NAME ${OUTPUT} NAME)
        set(BINARY ${BINARY_DIR}/${NAME})
        add_custom_command(
            OUTPUT ${BINARY}
            COMMAND ${CMAKE_COMMAND} -E copy ${OUTPUT} ${BINARY}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS ${OUTPUT}
            COMMENT ${BINARY}
        )
        string(REPLACE . _ NAME ${NAME})
        set(NAME package_${NAME})
        add_custom_target(${NAME} DEPENDS ${BINARY})
        add_dependencies(fluid_simulation ${NAME})
    endfunction()
    package(${JSON})
    package(${MSL})
    package(${SPV})
endfunction()
add_shader(add1.comp)
add_shader(add2.comp src/config.hpp)
add_shader(advect1.comp src/config.hpp shaders/shader.glsl)
add_shader(advect2.comp src/config.hpp shaders/shader.glsl)
add_shader(bnd1.comp src/config.hpp)
add_shader(bnd2.comp src/config.hpp)
add_shader(bnd3.comp src/config.hpp)
add_shader(bnd4.comp src/config.hpp)
add_shader(bnd5.comp src/config.hpp)
add_shader(clear.comp src/config.hpp)
add_shader(composite.comp shaders/shader.glsl src/config.hpp)
add_shader(diffuse.comp src/config.hpp shaders/shader.glsl)
add_shader(project1.comp src/config.hpp)
add_shader(project2.comp src/config.hpp shaders/shader.glsl)
add_shader(project3.comp src/config.hpp)
add_shader(single.comp shaders/shader.glsl src/config.hpp)

configure_file(LICENSE.txt ${BINARY_DIR} COPYONLY)
configure_file(README.md ${BINARY_DIR} COPYONLY)