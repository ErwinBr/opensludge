# Make the shaders of the SDL_GPU renderer: compile the GLSL in
# source/Engine/shaders/gpu to SPIR-V (for Vulkan) with glslangValidator,
# translate that to MSL (for Metal) with spirv-cross, and write both into
# gpu_shaders.h. We keep gpu_shaders.h in the repository, so that building
# the engine needs neither tool. Run this after changing a shader:
#
#   cmake -P cmake/GenerateGPUShaders.cmake
#
# glslang and spirv-cross are in Homebrew and in most Linux distributions.

set(SHADER_DIR ${CMAKE_CURRENT_LIST_DIR}/../source/Engine/shaders/gpu)
set(OUTPUT ${SHADER_DIR}/gpu_shaders.h)
set(WORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/gpu-shaders)
set(SHADERS quad.vert scale.frag paste.frag texture.frag color.frag yuv.frag)

find_program(GLSLANG glslangValidator REQUIRED)
find_program(SPIRV_CROSS spirv-cross REQUIRED)
file(MAKE_DIRECTORY ${WORK_DIR})

set(content "// Made by cmake/GenerateGPUShaders.cmake from the shaders in this folder.\n")
string(APPEND content "// Don't change it by hand.\n\n")
string(APPEND content "#include <stddef.h>\n\n")
string(APPEND content "struct gpuShaderCode {\n")
string(APPEND content "\tconst char * name;\n")
string(APPEND content "\tconst unsigned char * spirv;\n")
string(APPEND content "\tsize_t spirvSize;\n")
string(APPEND content "\tconst char * msl;	// The entry point is main0\n")
string(APPEND content "};\n\n")

set(table "")
foreach(shader ${SHADERS})
	string(MAKE_C_IDENTIFIER ${shader} id)
	set(spv ${WORK_DIR}/${shader}.spv)
	execute_process(
		COMMAND ${GLSLANG} -V --target-env vulkan1.0 -o ${spv} ${SHADER_DIR}/${shader}
		RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "glslangValidator failed for ${shader}:\n${log}")
	endif()
	execute_process(
		COMMAND ${SPIRV_CROSS} --msl --msl-version 20100 --msl-decoration-binding ${spv}
		RESULT_VARIABLE result OUTPUT_VARIABLE msl ERROR_VARIABLE log)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "spirv-cross failed for ${shader}:\n${log}")
	endif()

	file(READ ${spv} hex HEX)
	string(LENGTH "${hex}" hexLength)
	math(EXPR size "${hexLength} / 2")
	string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
	string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n\t" bytes "${bytes}")
	string(APPEND content "static const unsigned char ${id}_spirv[${size}] = {\n\t${bytes}\n};\n\n")
	string(APPEND content "static const char ${id}_msl[] = R\"sludge_msl(${msl})sludge_msl\";\n\n")
	string(APPEND table "\t{ \"${shader}\", ${id}_spirv, ${size}, ${id}_msl },\n")
endforeach()

string(APPEND content "static const gpuShaderCode gpuShaders[] = {\n${table}\t{ NULL, NULL, 0, NULL }\n};\n")
file(WRITE ${OUTPUT} "${content}")
message(STATUS "Wrote ${OUTPUT}")
