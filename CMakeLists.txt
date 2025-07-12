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
add_subdirectory(SDL)
add_subdirectory(glm)
add_executable(fluid_simulation WIN32
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_impl_sdl3.cpp
    imgui/imgui_impl_sdlgpu3.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    main.cpp
    mesh.cpp
    pipeline.cpp
    rw_texture.cpp
    shader.cpp
)
set_target_properties(fluid_simulation PROPERTIES CXX_STANDARD 23)
target_include_directories(fluid_simulation PRIVATE imgui)
target_link_libraries(fluid_simulation PRIVATE SDL3::SDL3 glm)

function(add_shader FILE)
    set(DEPENDS ${ARGN})
    set(GLSL ${CMAKE_SOURCE_DIR}/${FILE})
    set(SPV ${CMAKE_SOURCE_DIR}/bin/${FILE}.spv)
    set(DXIL ${CMAKE_SOURCE_DIR}/bin/${FILE}.dxil)
    set(MSL ${CMAKE_SOURCE_DIR}/bin/${FILE}.msl)
    set(JSON ${CMAKE_SOURCE_DIR}/bin/${FILE}.json)
    function(compile PROGRAM SOURCE OUTPUT)
        add_custom_command(
            OUTPUT ${OUTPUT}
            COMMAND ${PROGRAM} ${SOURCE} -o ${OUTPUT}
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
    if (MSVC)
        set(SHADERCROSS SDL_shadercross/msvc/shadercross.exe)
        compile(glslc ${GLSL} ${SPV})
        compile(${SHADERCROSS} ${SPV} ${DXIL})
        compile(${SHADERCROSS} ${SPV} ${MSL})
        compile(${SHADERCROSS} ${SPV} ${JSON})
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
    if(WIN32)
        package(${DXIL})
    elseif(APPLE)
        package(${MSL})
    else()
        package(${SPV})
    endif()
    package(${JSON})
endfunction()
add_shader(add.comp)
add_shader(clear.comp config.hpp)
add_shader(diffuse.comp config.hpp shader.glsl)
add_shader(outline.frag)
add_shader(outline.vert)
add_shader(voxel.frag)
add_shader(voxel.vert)

configure_file(LICENSE.txt ${BINARY_DIR} COPYONLY)
configure_file(README.md ${BINARY_DIR} COPYONLY)