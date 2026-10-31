# Capture tests: we run games deterministically and compare their frames
# (see source/Engine/capture.h and tests/capture/run.cmake). They need a
# display, and have the label "capture", so that
# `ctest -L capture` runs only them and `ctest -LE capture` all the others.
#
# Each test runs the engine twice and checks that the frames are the same.
# With SLUDGE_CAPTURE_REFERENCE_ENGINE set to the engine of another build,
# for example of an earlier commit, each test also has a "-reference"
# variant that compares the frames with those of that engine. With
# SLUDGE_GPU_CAPTURE_TESTS, each test also runs with the SDL_GPU renderer
# twice ("-gpu-determinism"), and compares its frames with those of OpenGL
# ("-gpu-vs-gl"). There, a frame may differ in up to 5% of its pixels: where
# nearest filtering samples exactly between texels, and in linearly filtered
# or anti-aliased pixels, the two can round differently. The noise of the
# TVSTATIC transition, stretched across the screen, comes closest.

set(SLUDGE_OUT_OF_ORDER "" CACHE PATH
	"The gamedata file of Out Of Order, for its capture test (see packaging/get-out-of-order.sh)")
set(SLUDGE_CAPTURE_REFERENCE_ENGINE "" CACHE FILEPATH
	"An engine to compare the frames of the capture tests with")
# The SDL_GPU renderer needs Metal, Vulkan or Direct3D 12, which the runners
# of our CI have on macOS only.
if(APPLE)
	set(gpu_tests_default ON)
else()
	set(gpu_tests_default OFF)
endif()
option(SLUDGE_GPU_CAPTURE_TESTS "Also run the capture tests with the SDL_GPU renderer" ${gpu_tests_default})

add_executable(framediff ${CMAKE_CURRENT_SOURCE_DIR}/tests/framediff.cpp)
target_link_libraries(framediff PRIVATE sludge::png)

# sludge_add_capture_test(<name> GAME <file> [INPUT <script>] [MAX_FRAMES <n>]
#                         [FRAMES <list>] [ENGINE_ARGS <options>])
function(sludge_add_capture_test name)
	cmake_parse_arguments(PARSE_ARGV 1 arg "" "GAME;INPUT;MAX_FRAMES;FRAMES;ENGINE_ARGS" "")
	set(defines
		-DENGINE=$<TARGET_FILE:sludge-engine>
		-DFRAMEDIFF=$<TARGET_FILE:framediff>
		-DGAME=${arg_GAME})
	foreach(option INPUT MAX_FRAMES FRAMES ENGINE_ARGS)
		if(DEFINED arg_${option})
			list(APPEND defines "-D${option}=${arg_${option}}")
		endif()
	endforeach()

	set(variants determinism)
	if(SLUDGE_CAPTURE_REFERENCE_ENGINE)
		list(APPEND variants reference)
	endif()
	if(SLUDGE_GPU_CAPTURE_TESTS)
		list(APPEND variants gpu-determinism gpu-vs-gl)
	endif()
	foreach(variant ${variants})
		set(test capture-${name}-${variant})
		set(variant_defines ${defines} -DOUT=${CMAKE_BINARY_DIR}/capture/${name}-${variant})
		if(variant STREQUAL "reference")
			list(APPEND variant_defines -DREFERENCE_ENGINE=${SLUDGE_CAPTURE_REFERENCE_ENGINE})
		elseif(variant MATCHES "^gpu")
			# These replace the ENGINE_ARGS that we added above.
			list(FILTER variant_defines EXCLUDE REGEX "^-DENGINE_ARGS=")
			list(APPEND variant_defines "-DENGINE_ARGS=${arg_ENGINE_ARGS} --renderer=gpu" "-DREQUIRE=Renderer: SDL_GPU")
			if(variant STREQUAL "gpu-vs-gl")
				list(APPEND variant_defines "-DEXPECTED_ARGS=${arg_ENGINE_ARGS} --renderer=gl" -DMAX_BAD=0.05)
			endif()
		endif()
		add_test(NAME ${test}
			COMMAND ${CMAKE_COMMAND} ${variant_defines} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/run.cmake)
		set_tests_properties(${test} PROPERTIES LABELS capture TIMEOUT 900)
	endforeach()
endfunction()

if(TARGET sludge-compiler AND NOT CMAKE_CROSSCOMPILING)
	sludge_add_capture_test(welcome GAME ${CMAKE_BINARY_DIR}/games/Welcome.slg
		FRAMES 0-60/10)
	sludge_add_capture_test(verbcoin GAME ${CMAKE_BINARY_DIR}/games/VerbCoin.slg
		INPUT ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/verbcoin.input FRAMES 0-2100/50)
	# The RenderTest project uses images of the VerbCoin example, so we put
	# the two together in the build folder. With configure_file, CMake runs
	# again when one of the files changes.
	set(render_project ${CMAKE_BINARY_DIR}/rendertest-project)
	set(verbcoin ${CMAKE_CURRENT_SOURCE_DIR}/doc/ExampleProjects/VerbCoin)
	foreach(file demofont.duc ego/alien.duc iface/invback.tga iface/mouse.duc iface/verbcoin.tga
			inside/room.tga outside/duck.duc outside/room.flo outside/room.tga outside/room.zbu)
		configure_file(${verbcoin}/${file} ${render_project}/${file} COPYONLY)
	endforeach()
	foreach(file RenderTest.slp render.slu big.png hole.png light.png sky.png)
		configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/render/${file} ${render_project}/${file} COPYONLY)
	endforeach()
	sludge_compile_game(RenderTest ${render_project}/RenderTest.slp)
	sludge_add_capture_test(rendertest GAME ${CMAKE_BINARY_DIR}/games/RenderTest.slg
		FRAMES 0-1000/10)
	# The same without anti-aliasing, which draws through other shader code.
	sludge_add_capture_test(rendertest-noaa GAME ${CMAKE_BINARY_DIR}/games/RenderTest.slg
		FRAMES 0-1000/10 ENGINE_ARGS -a0)
	# The same with textures of power-of-two sizes, as on OpenGL ES 2.0
	# devices.
	sludge_add_capture_test(rendertest-pot GAME ${CMAKE_BINARY_DIR}/games/RenderTest.slg
		FRAMES 0-1000/10 ENGINE_ARGS --pot-textures)
endif()

if(SLUDGE_OUT_OF_ORDER)
	sludge_add_capture_test(out-of-order GAME ${SLUDGE_OUT_OF_ORDER}
		INPUT ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/out-of-order.input FRAMES 0-1200/40)
endif()
