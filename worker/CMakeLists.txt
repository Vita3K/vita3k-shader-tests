cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()


project(ftvgworker)
set(TITLE_ID "FTVGWK000")
set(TITLE_NAME "Fuzz the Vita GPU")

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "-Wl,-q -Wall -O2")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11 -fno-rtti -fno-exceptions")

link_directories(
	${CMAKE_BINARY_DIR}
)

set(SOURCES
	main.cpp
)

set(FRAGMENT_SHADERS
    shaders/output_vertex0.cg
    shaders/output_vertex1.cg
    shaders/output_vertex2.cg
)

# foreach(shader ${VERTEX_SHADERS})
# 	get_filename_component(shader_we ${shader} NAME_WE)
# 	add_custom_command(OUTPUT "${shader_we}.gxp"
# 		COMMAND $ENV{PSP2CGC} -profile sce_vp_psp2 "../${shader}"
# 			-o "${CMAKE_BINARY_DIR}/${shader_we}.gxp"
# 		DEPENDS ${shader}
# 		COMMENT "Compiling ${shader} to ${shader_we}.gxp"
# 	)
# 	list(APPEND SHADER_GXPS "${shader_we}.gxp")
# endforeach()

foreach(shader ${FRAGMENT_SHADERS})
	get_filename_component(shader_we ${shader} NAME_WE)
	add_custom_command(OUTPUT "${shader_we}.gxp"
		COMMAND $ENV{PSP2CGC} -profile sce_fp_psp2 "../${shader}"
			-o "${shader_we}.gxp"
		DEPENDS ${shader}
		COMMENT "Compiling ${shader} to ${shader_we}.gxp"
	)
	list(APPEND SHADER_GXPS "${shader_we}.gxp")
endforeach()

foreach(gxp ${SHADER_GXPS})
	get_filename_component(gxp_we ${gxp} NAME_WE)
	add_custom_command(OUTPUT "${gxp_we}_gxp.o"
		COMMAND arm-vita-eabi-objcopy --input binary --output elf32-littlearm
			--binary-architecture arm "${gxp}"
			"${CMAKE_BINARY_DIR}/${gxp_we}_gxp.o"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
		DEPENDS ${gxp}
		COMMENT "Objcopying ${gxp} to ${gxp_we}_gxp.o"
	)
	list(APPEND SHADER_OBJECTS "${gxp_we}_gxp.o")
endforeach()

add_executable(${PROJECT_NAME}
	${SOURCES}
	${SHADER_OBJECTS}
)

target_link_libraries(${PROJECT_NAME}
	SceDisplay_stub
	SceGxm_stub
	SceCtrl_stub
	SceSysmodule_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})