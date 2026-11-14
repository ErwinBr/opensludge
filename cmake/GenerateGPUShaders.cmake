# Make the shaders of the SDL_GPU renderer: compile the GLSL in
# source/Engine/shaders/gpu to SPIR-V (for Vulkan) with glslangValidator,
# translate that to MSL (for Metal) and to HLSL with spirv-cross, compile
# the HLSL to DXIL (for Direct3D 12) with dxc, and write them all into
# gpu_shaders.h. We keep gpu_shaders.h in the repository, so that building
# the engine needs none of these tools. Run this after changing a shader:
#
#   cmake -P cmake/GenerateGPUShaders.cmake
#
# glslang and spirv-cross are in Homebrew and in most Linux distributions.
# dxc comes with the Vulkan SDK, and for Linux and Windows also from
# https://github.com/microsoft/DirectXShaderCompiler/releases. It must be
# DXC 1.8 or later, which signs the DXIL, as Direct3D 12 wants.

set(SHADER_DIR ${CMAKE_CURRENT_LIST_DIR}/../source/Engine/shaders/gpu)
set(OUTPUT ${SHADER_DIR}/gpu_shaders.h)
set(WORK_DIR ${CMAKE_CURRENT_BINARY_DIR}/gpu-shaders)
set(SHADERS quad.vert mesh.vert scale.frag paste.frag texture.frag color.frag yuv.frag vertexcolor.frag
	outline_seed.frag outline_jump.frag outline.frag)

find_program(GLSLANG glslangValidator REQUIRED)
find_program(SPIRV_CROSS spirv-cross REQUIRED)
find_program(DXC dxc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
file(MAKE_DIRECTORY ${WORK_DIR})

set(content "// Made by cmake/GenerateGPUShaders.cmake from the shaders in this folder.\n")
string(APPEND content "// Don't change it by hand.\n\n")
string(APPEND content "#include <stddef.h>\n\n")
string(APPEND content "struct gpuShaderCode {\n")
string(APPEND content "\tconst char * name;\n")
string(APPEND content "\tconst unsigned char * spirv;\n")
string(APPEND content "\tsize_t spirvSize;\n")
string(APPEND content "\tconst char * msl;	// The entry point is main0\n")
string(APPEND content "\tconst unsigned char * dxil;\n")
string(APPEND content "\tsize_t dxilSize;\n")
string(APPEND content "};\n\n")

# The bytes of a file as the elements of a C array, 16 to a line, and their
# number.
function(hex_bytes file bytesVar sizeVar)
	file(READ ${file} hex HEX)
	string(LENGTH "${hex}" hexLength)
	math(EXPR size "${hexLength} / 2")
	string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
	string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n\t" bytes "${bytes}")
	set(${bytesVar} "${bytes}" PARENT_SCOPE)
	set(${sizeVar} ${size} PARENT_SCOPE)
endfunction()

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

	# spirv-cross puts descriptor set n in register space n, so sets 1, 2
	# and 3 become the spaces that SDL_GPU wants on Direct3D 12: the
	# uniforms of the vertex shader in space 1, the textures and samplers
	# of the fragment shader in space 2 and its uniforms in space 3.
	set(hlsl ${WORK_DIR}/${shader}.hlsl)
	set(dxil ${WORK_DIR}/${shader}.dxil)
	execute_process(
		COMMAND ${SPIRV_CROSS} --hlsl --shader-model 60 --output ${hlsl} ${spv}
		RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "spirv-cross failed for ${shader}:\n${log}")
	endif()
	if(shader MATCHES "\\.vert$")
		set(profile vs_6_0)
	else()
		set(profile ps_6_0)
	endif()
	execute_process(
		COMMAND ${DXC} -T ${profile} -E main -Fo ${dxil} ${hlsl}
		RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "dxc failed for ${shader}:\n${log}")
	endif()

	hex_bytes(${spv} bytes size)
	hex_bytes(${dxil} dxilBytes dxilSize)
	string(APPEND content "static const unsigned char ${id}_spirv[${size}] = {\n\t${bytes}\n};\n\n")
	string(APPEND content "static const char ${id}_msl[] = R\"sludge_msl(${msl})sludge_msl\";\n\n")
	string(APPEND content "static const unsigned char ${id}_dxil[${dxilSize}] = {\n\t${dxilBytes}\n};\n\n")
	string(APPEND table "\t{ \"${shader}\", ${id}_spirv, ${size}, ${id}_msl, ${id}_dxil, ${dxilSize} },\n")
endforeach()

string(APPEND content "static const gpuShaderCode gpuShaders[] = {\n${table}\t{ NULL, NULL, 0, NULL, NULL, 0 }\n};\n")
file(WRITE ${OUTPUT} "${content}")
message(STATUS "Wrote ${OUTPUT}")
