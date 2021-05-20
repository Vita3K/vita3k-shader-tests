cmake_minimum_required(VERSION 3.10)
project(ftvg)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_subdirectory(Vita3K)


set(VERTEX_SHADERS
    shaders/va_test_program.cg
)

set(SOURCES
	include/generator.h
	src/generator.cpp
	src/main.cpp
)

foreach(shader ${VERTEX_SHADERS})
	get_filename_component(shader_we ${shader} NAME_WE)
	add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/${shader_we}.gxp"
		COMMAND $ENV{PSP2CGC} -profile sce_vp_psp2 "${CMAKE_SOURCE_DIR}/${shader}"
			-o "${CMAKE_BINARY_DIR}/${shader_we}.gxp"
		DEPENDS ${shader}
	)
	list(APPEND SHADER_GXPS "${CMAKE_BINARY_DIR}/${shader_we}.gxp")
endforeach()

set(SHADER_H "${CMAKE_BINARY_DIR}/include/shaders.h")
file(WRITE ${SHADER_H} "")
foreach(gxp ${SHADER_GXPS})
    get_filename_component(gxp_we ${gxp} NAME_WE)
    file(APPEND ${SHADER_H} "extern \"C\" const unsigned char ${gxp_we}[]; \nextern \"C\" const unsigned ${gxp_we}_size;\n")
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/${shader_we}.c"
		COMMAND ${CMAKE_COMMAND} -Doutput="${CMAKE_BINARY_DIR}/${shader_we}.c" -Dgxp_we=${gxp_we} -Dgxp=${gxp} -P "${CMAKE_SOURCE_DIR}/EmbedFile.cmake"
		DEPENDS ${gxp}
    )
    list(APPEND SOURCES "${CMAKE_BINARY_DIR}/${shader_we}.c")
endforeach()

list(APPEND SOURCES ${SHADER_H})

add_executable(ftvg ${SOURCES})
target_include_directories(ftvg PRIVATE include "${CMAKE_BINARY_DIR}/include")
target_link_libraries(ftvg PUBLIC shader)
target_link_libraries(ftvg PRIVATE CLI11)