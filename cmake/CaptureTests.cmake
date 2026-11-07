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
set(SLUDGE_EGO_SPRITES "" CACHE PATH
	"The folder of the sprite sheets (Ego_*.png) of CompositeTest, which aren't in this repository (see tests/render/make-ego-banks.py)")
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
#                         [FRAMES <list>] [ENGINE_ARGS <options>] [NO_REFERENCE])
# NO_REFERENCE: no "-reference" variant, for games of features that the
# reference engine doesn't have.
function(sludge_add_capture_test name)
	cmake_parse_arguments(PARSE_ARGV 1 arg "NO_REFERENCE" "GAME;INPUT;MAX_FRAMES;FRAMES;ENGINE_ARGS" "")
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
	if(SLUDGE_CAPTURE_REFERENCE_ENGINE AND NOT arg_NO_REFERENCE)
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
	foreach(file RenderTest.slp render.slu UnloadTest.slp unload.slu LoadTest.slp load.slu FreezeTest.slp freeze.slu
			TimelineTest.slp timeline.slu SealTest.slp seal.slu CompositeTest.slp composite.slu
			big.png hole.png light.png sky.png movie.webm)
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

	# Freezing and snapshots move animations on by a tick, as they always
	# did, at every level of freezing. Every frame is kept, with the number
	# of frame events in the status line, so that any difference in timing
	# leaves an image.
	sludge_compile_game(FreezeTest ${render_project}/FreezeTest.slp)
	sludge_add_capture_test(freeze GAME ${CMAKE_BINARY_DIR}/games/FreezeTest.slg FRAMES 0-260)

	# Clips of newClip (): playback and events apart from drawing, which the
	# game checks itself, and every fifth frame kept. Saving and loading
	# must restore the playback exactly: after loading, the game plays what
	# it played after saving, frame by frame.
	sludge_compile_game(TimelineTest ${render_project}/TimelineTest.slp)
	sludge_add_capture_test(timeline GAME ${CMAKE_BINARY_DIR}/games/TimelineTest.slg FRAMES 0-700/5 NO_REFERENCE)
	add_test(NAME capture-timeline-saveload
		COMMAND ${CMAKE_COMMAND}
			-DENGINE=$<TARGET_FILE:sludge-engine>
			-DGAME=${CMAKE_BINARY_DIR}/games/TimelineTest.slg
			-DOUT=${CMAKE_BINARY_DIR}/capture/timeline-saveload
			"-DENGINE_ARGS=--renderer=gl"
			-P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/saveload.cmake)
	set_tests_properties(capture-timeline-saveload PROPERTIES LABELS capture TIMEOUT 300)

	# A clip that a character uses can't change.
	sludge_compile_game(SealTest ${render_project}/SealTest.slp)
	add_test(NAME capture-seal
		COMMAND ${CMAKE_COMMAND}
			-DENGINE=$<TARGET_FILE:sludge-engine>
			-DGAME=${CMAKE_BINARY_DIR}/games/SealTest.slg
			-DOUT=${CMAKE_BINARY_DIR}/capture/seal
			"-DMESSAGE=already used by a character, costume or cursor"
			-P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/expect-fatal.cmake)
	set_tests_properties(capture-seal PROPERTIES LABELS capture TIMEOUT 300)

	# A character of parts, of HD sprites (about 750 pixels high, drawn at
	# half that), from sheets outside this repository: with
	# SLUDGE_EGO_SPRITES, we make its banks, and it walks, talks, stands in
	# every direction, turns, scales, flips and hides parts, behind the
	# z-buffer and under the light map. Saving and loading must restore it.
	set(composite_lifetimes "")
	if(SLUDGE_EGO_SPRITES)
		find_package(Python3 COMPONENTS Interpreter)
		if(Python3_FOUND)
			set(ego_script ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/make-ego-banks.py)
			set(ego_sheets "")
			set(ego_banks "")
			foreach(part Legs_SOUTH Torso_SOUTH Head_SOUTH Arm1_SOUTH Arm2_SOUTH Legs_EAST Torso_EAST Head_EAST
					Arm1_EAST Arm2_EAST Legs_NORTH Torso_NORTH Head_NORTH Arms_NORTH)
				string(TOLOWER ${part} bank)
				list(APPEND ego_sheets ${SLUDGE_EGO_SPRITES}/Ego_${part}.png)
				list(APPEND ego_banks ${render_project}/hd-ego/${bank}.duc)
			endforeach()
			add_custom_command(
				OUTPUT ${ego_banks}
				COMMAND ${Python3_EXECUTABLE} ${ego_script} ${SLUDGE_EGO_SPRITES} ${render_project}/hd-ego
				DEPENDS ${ego_script} ${ego_sheets}
				COMMENT "Making the sprite banks of CompositeTest"
				VERBATIM)
			sludge_compile_game(CompositeTest ${render_project}/CompositeTest.slp DEPENDS ${ego_banks})
			sludge_add_capture_test(composite GAME ${CMAKE_BINARY_DIR}/games/CompositeTest.slg FRAMES 0-1500/10 NO_REFERENCE)
			add_test(NAME capture-composite-saveload
				COMMAND ${CMAKE_COMMAND}
					-DENGINE=$<TARGET_FILE:sludge-engine>
					-DGAME=${CMAKE_BINARY_DIR}/games/CompositeTest.slg
					-DOUT=${CMAKE_BINARY_DIR}/capture/composite-saveload
					"-DENGINE_ARGS=--renderer=gl"
					-P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/saveload.cmake)
			set_tests_properties(capture-composite-saveload PROPERTIES LABELS capture TIMEOUT 300)
			set(composite_lifetimes -DCOMPOSITE_GAME=${CMAKE_BINARY_DIR}/games/CompositeTest.slg)
		else()
			message(WARNING "SLUDGE_EGO_SPRITES is set, but there is no Python 3 to make CompositeTest's banks with")
		endif()
	endif()

	# What lives after saving and loading, and after loads that fail.
	sludge_compile_game(LoadTest ${render_project}/LoadTest.slp)
	add_test(NAME capture-lifetimes
		COMMAND ${CMAKE_COMMAND}
			-DENGINE=$<TARGET_FILE:sludge-engine>
			-DGAME=${CMAKE_BINARY_DIR}/games/LoadTest.slg
			-DUNLOAD_GAME=${CMAKE_BINARY_DIR}/games/UnloadTest.slg
			-DTIMELINE_GAME=${CMAKE_BINARY_DIR}/games/TimelineTest.slg
			${composite_lifetimes}
			-DOUT=${CMAKE_BINARY_DIR}/capture/lifetimes
			-P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/lifetimes.cmake)
	set_tests_properties(capture-lifetimes PROPERTIES LABELS capture TIMEOUT 900)

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
	# Every 40th frame, and frames 180-200 too: on GitHub's macOS arm64
	# runners, OpenGL rounds frame 190 (the hero raises his arm) a little
	# differently in the second run. A frame without an image has only its
	# CRC, which must match exactly; with its image, framediff allows that.
	sludge_add_capture_test(out-of-order GAME ${SLUDGE_OUT_OF_ORDER}
		INPUT ${CMAKE_CURRENT_SOURCE_DIR}/tests/capture/out-of-order.input FRAMES 0-1200/40,180-200)
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
