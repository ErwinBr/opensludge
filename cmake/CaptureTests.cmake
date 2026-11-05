# Capture tests: we run games deterministically and compare their frames
# (see source/Engine/capture.h and tests/capture/run.cmake). They need a
# display, and have the label "capture", so that
# `ctest -L capture` runs only them and `ctest -LE capture` all the others.
#
# Each test runs the engine twice with OpenGL and checks that the frames
# are the same, up to a difference of 1 in a colour channel: the OpenGL of
# the virtual GPU of GitHub's macOS runners sometimes rounds a few pixels
# differently the first time that it uses a shader, such as in the first
# frame of a movie. Real nondeterminism in the engine differs by more.
# With SLUDGE_CAPTURE_REFERENCE_ENGINE set to the engine of another build,
# for example of an earlier commit, each test also has a "-reference"
# variant that compares the frames with those of that engine. With
# SLUDGE_GPU_CAPTURE_TESTS, each test also runs with the SDL_GPU renderer
# twice ("-gpu-determinism"), and compares its frames with those of OpenGL
# ("-gpu-vs-gl"). There, a frame may differ in up to 5% of its pixels: where
# nearest filtering samples exactly between texels, and in linearly filtered
# or anti-aliased pixels, the two can round differently. On a Mac's own GPU,
# the thumbnail of a saved game comes closest, at 2%; with the virtual GPU
# of GitHub's macOS runners, OpenGL rounds more pixels differently.
#
# The "capture-renderer-" tests check how the engine chooses its renderer.

set(SLUDGE_OUT_OF_ORDER "" CACHE PATH
	"The gamedata file of Out Of Order, for its capture test (see packaging/get-out-of-order.sh)")
set(SLUDGE_CAPTURE_REFERENCE_ENGINE "" CACHE FILEPATH
	"An engine to compare the frames of the capture tests with")
# The SDL_GPU renderer needs Metal, Vulkan or Direct3D 12. Every Mac has
# Metal, and Windows has Direct3D 12, at least through WARP, its software
# driver; on Linux, turn the tests on where Vulkan works, as our Linux CI
# does with Mesa's software Vulkan, lavapipe (mesa-vulkan-drivers).
if((APPLE OR WIN32) AND SLUDGE_RENDERER_GPU)
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
	foreach(option INPUT MAX_FRAMES FRAMES)
		if(DEFINED arg_${option})
			list(APPEND defines "-D${option}=${arg_${option}}")
		endif()
	endforeach()
	set(gl_args "${arg_ENGINE_ARGS} --renderer=gl")
	set(gpu_args "${arg_ENGINE_ARGS} --renderer=gpu")

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
		if(variant MATCHES "^gpu")
			list(APPEND variant_defines "-DENGINE_ARGS=${gpu_args}" "-DREQUIRE=Renderer: SDL_GPU")
			if(variant STREQUAL "gpu-vs-gl")
				list(APPEND variant_defines "-DEXPECTED_ARGS=${gl_args}" -DMAX_BAD=0.05)
			endif()
		else()
			list(APPEND variant_defines "-DENGINE_ARGS=${gl_args}" "-DREQUIRE=Renderer: OpenGL")
			if(variant STREQUAL "reference")
				list(APPEND variant_defines -DREFERENCE_ENGINE=${SLUDGE_CAPTURE_REFERENCE_ENGINE})
			else()
				list(APPEND variant_defines -DMAX_DIFF=1)
			endif()
		endif()
		add_test(NAME ${test}
			COMMAND ${CMAKE_COMMAND} ${variant_defines} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/run.cmake)
		set_tests_properties(${test} PROPERTIES LABELS capture TIMEOUT 900)
	endforeach()
endfunction()

# sludge_add_renderer_test(<name> ENGINE_ARGS <options>
#                          [ENGINE_ENV "<variable>=<value> ..."] EXPECTED <gl|gpu>)
# Plays the start of Welcome with ENGINE_ARGS and ENGINE_ENV, which must
# draw with the EXPECTED renderer, and with that renderer named explicitly;
# the frames must be the same.
function(sludge_add_renderer_test name)
	cmake_parse_arguments(PARSE_ARGV 1 arg "" "ENGINE_ARGS;ENGINE_ENV;EXPECTED" "")
	if(arg_EXPECTED STREQUAL "gpu")
		set(require "Renderer: SDL_GPU")
	else()
		set(require "Renderer: OpenGL")
	endif()
	set(test capture-renderer-${name})
	add_test(NAME ${test}
		COMMAND ${CMAKE_COMMAND}
			-DENGINE=$<TARGET_FILE:sludge-engine>
			-DFRAMEDIFF=$<TARGET_FILE:framediff>
			-DGAME=${CMAKE_BINARY_DIR}/games/Welcome.slg
			-DFRAMES=0-60/10
			-DOUT=${CMAKE_BINARY_DIR}/capture/renderer-${name}
			"-DENGINE_ARGS=${arg_ENGINE_ARGS}"
			"-DENGINE_ENV=${arg_ENGINE_ENV}"
			"-DEXPECTED_ARGS=--renderer=${arg_EXPECTED}"
			"-DREQUIRE=${require}"
			-P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/run.cmake)
	set_tests_properties(${test} PROPERTIES LABELS capture TIMEOUT 300)
endfunction()

if(TARGET sludge-compiler AND NOT CMAKE_CROSSCOMPILING)
	sludge_add_capture_test(welcome GAME ${CMAKE_BINARY_DIR}/games/Welcome.slg
		FRAMES 0-60/10)

	# By default, we draw with SDL_GPU, where we build it (see
	# createGraphicsWindow in source/Engine/graphics.cpp).
	if(SLUDGE_RENDERER_GPU)
		set(default_renderer gpu)
		set(other_renderer gl)
	else()
		set(default_renderer gl)
		set(other_renderer gpu)
	endif()
	if(SLUDGE_GPU_CAPTURE_TESTS OR default_renderer STREQUAL "gl")
		sludge_add_renderer_test(auto ENGINE_ARGS --renderer=auto EXPECTED ${default_renderer})
	endif()
	if(SLUDGE_GPU_CAPTURE_TESTS)
		# SLUDGE_RENDERER chooses, and --renderer wins over it.
		sludge_add_renderer_test(environment
			ENGINE_ENV SLUDGE_RENDERER=${other_renderer} EXPECTED ${other_renderer})
		sludge_add_renderer_test(option-before-environment ENGINE_ARGS --renderer=${other_renderer}
			ENGINE_ENV SLUDGE_RENDERER=${default_renderer} EXPECTED ${other_renderer})
		# Without OpenGL, we draw with SDL_GPU. On Wayland, SDL loads OpenGL
		# through EGL, so we take that away too.
		sludge_add_renderer_test(gl-fallback ENGINE_ARGS --renderer=gl
			ENGINE_ENV "SDL_OPENGL_LIBRARY=no-such-library SDL_EGL_LIBRARY=no-such-library"
			EXPECTED gpu)
	endif()
	if(SLUDGE_RENDERER_GPU)
		# Without SDL_GPU, we draw with OpenGL.
		sludge_add_renderer_test(gpu-fallback ENGINE_ARGS "--renderer=gpu --gpu-driver=no-such-driver"
			EXPECTED gl)
	endif()
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
	foreach(file RenderTest.slp render.slu UnloadTest.slp unload.slu big.png hole.png light.png sky.png movie.webm)
		configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/render/${file} ${render_project}/${file} COPYONLY)
	endforeach()
	sludge_compile_game(RenderTest ${render_project}/RenderTest.slp)
	# Every tenth frame, and each frame of the movie at the end (961-980)
	# and the one after it, so that a difference there leaves its images.
	set(render_frames 0-1020/10,961-981)
	sludge_add_capture_test(rendertest GAME ${CMAKE_BINARY_DIR}/games/RenderTest.slg
		FRAMES ${render_frames})
	# The same without anti-aliasing, which draws through other shader code.
	sludge_add_capture_test(rendertest-noaa GAME ${CMAKE_BINARY_DIR}/games/RenderTest.slg
		FRAMES ${render_frames} ENGINE_ARGS -a0)
	# The same with textures of power-of-two sizes, as on OpenGL ES 2.0
	# devices.
	sludge_add_capture_test(rendertest-pot GAME ${CMAKE_BINARY_DIR}/games/RenderTest.slg
		FRAMES ${render_frames} ENGINE_ARGS --pot-textures)

	# Costumes and animations that go again must take their sprite banks with
	# them: with no room for unused banks, the engine ends with none loaded.
	sludge_compile_game(UnloadTest ${render_project}/UnloadTest.slp)
	add_test(NAME capture-unload
		COMMAND ${CMAKE_COMMAND}
			-DENGINE=$<TARGET_FILE:sludge-engine>
			-DFRAMEDIFF=$<TARGET_FILE:framediff>
			-DGAME=${CMAKE_BINARY_DIR}/games/UnloadTest.slg
			-DFRAMES=0
			-DOUT=${CMAKE_BINARY_DIR}/capture/unload
			"-DENGINE_ARGS=--renderer=gl"
			"-DENGINE_ENV=SLUDGE_SPRITEBANK_CACHE_MB=0 SLUDGE_SPRITEBANK_LOG=1"
			"-DREQUIRE=unloaded [0-9]+; 0 loaded, 0 in use"
			-P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/run.cmake)
	set_tests_properties(capture-unload PROPERTIES LABELS capture TIMEOUT 300)

	# Extruding the edges of sprites into their gutters (SPRITEEDGES, or the
	# project's "Clean edges") changes only what filtering reads beside a
	# sprite: with linear filtering, a few pixels at the edges of sprites, by
	# a little (at most 3 in a channel in RenderTest). The default frames
	# don't change.
	add_test(NAME capture-rendertest-extrude
		COMMAND ${CMAKE_COMMAND}
			-DENGINE=$<TARGET_FILE:sludge-engine>
			-DFRAMEDIFF=$<TARGET_FILE:framediff>
			-DGAME=${CMAKE_BINARY_DIR}/games/RenderTest.slg
			-DFRAMES=${render_frames}
			-DOUT=${CMAKE_BINARY_DIR}/capture/rendertest-extrude
			"-DENGINE_ARGS=--renderer=gl -a-1"
			"-DENGINE_ENV=SLUDGE_SPRITE_EDGES=1"
			# Frames without images differ in their CRC; MAX_BAD stops
			# framediff from comparing those, and allows 3 pixels a frame.
			-DMAX_DIFF=8 -DMAX_BAD=0.00001
			-P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/run.cmake)
	set_tests_properties(capture-rendertest-extrude PROPERTIES LABELS capture TIMEOUT 900)
endif()

if(SLUDGE_OUT_OF_ORDER)
	sludge_add_capture_test(out-of-order GAME ${SLUDGE_OUT_OF_ORDER}
		INPUT ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/out-of-order.input FRAMES 0-1200/40)
endif()

# The meshes of the renderers, which the Dev Kit draws with.
add_executable(renderer_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/renderer_test.cpp)
target_link_libraries(renderer_test PRIVATE sludge-renderer)
add_test(NAME capture-renderer-meshes-gl COMMAND renderer_test gl)
set_tests_properties(capture-renderer-meshes-gl PROPERTIES LABELS capture)
if(SLUDGE_GPU_CAPTURE_TESTS)
	add_test(NAME capture-renderer-meshes-gpu COMMAND renderer_test gpu)
	set_tests_properties(capture-renderer-meshes-gpu PROPERTIES LABELS capture)
endif()
