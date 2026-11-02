cmake_minimum_required(VERSION 3.24)

project(OpenSLUDGE VERSION 2.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SLUDGE_GLES2 "Render with OpenGL ES 2.0 instead of desktop OpenGL" OFF)
set(SLUDGE_DEVICE "" CACHE STRING "Build for a device: OpenPandora, RaspberryPi or ODROID")
set_property(CACHE SLUDGE_DEVICE PROPERTY STRINGS "" OpenPandora RaspberryPi ODROID)
# Besides OpenGL, the engine can draw with SDL_GPU, through Metal, Vulkan or
# Direct3D 12. On the devices, we draw with OpenGL ES 2.0 only.
if(SLUDGE_DEVICE)
	set(SLUDGE_GLES2 ON)
endif()
if(SLUDGE_GLES2)
	set(RENDERER_GPU_DEFAULT OFF)
else()
	set(RENDERER_GPU_DEFAULT ON)
endif()
option(SLUDGE_RENDERER_GPU "Draw with SDL_GPU (Metal, Vulkan, Direct3D 12) where it works" ${RENDERER_GPU_DEFAULT})
option(SLUDGE_MOVIES "Support playing WebM movies (needs libvpx)" ON)
option(SLUDGE_BUNDLED_DEPS "Build all dependencies from source and link them statically" OFF)
option(SLUDGE_TESTS "Build the tests" ON)

option(SLUDGE_ENGINE "Build the SLUDGE engine" ON)
# The compiler is headless and builds everywhere. The Dev Kit needs it.
option(SLUDGE_COMPILER "Build the headless sludge-compiler" ON)
# The new Dev Kit uses SDL 3 and Dear ImGui, and draws with the renderers
# of the engine. Until it does all that the old one does, we keep the old
# one too, which uses GTK 3, and which we build only on Linux and other Unix
# systems.
option(SLUDGE_DEVKIT "Build the SLUDGE Dev Kit (SDL 3 and Dear ImGui; in progress)" OFF)
if(UNIX AND NOT APPLE)
	set(DEVKIT_GTK_DEFAULT ON)
else()
	set(DEVKIT_GTK_DEFAULT OFF)
endif()
option(SLUDGE_DEVKIT_GTK "Build the old SLUDGE Dev Kit, which uses GTK 3" ${DEVKIT_GTK_DEFAULT})
option(SLUDGE_DOC "Install the SLUDGE documentation" OFF)

include(GNUInstallDirs)
set(SLUDGE_DOCDIR ${CMAKE_INSTALL_DATAROOTDIR}/doc/sludge)

if(SLUDGE_TESTS)
	enable_testing()
endif()

install(FILES AUTHORS README INSTALL DESTINATION ${SLUDGE_DOCDIR})

if((SLUDGE_DEVKIT OR SLUDGE_DEVKIT_GTK) AND NOT SLUDGE_COMPILER)
	message(STATUS "The Dev Kit needs the compiler, so we build it too")
	set(SLUDGE_COMPILER ON)
endif()

if(SLUDGE_ENGINE OR SLUDGE_COMPILER OR SLUDGE_DEVKIT)
	include(cmake/Dependencies.cmake)
endif()

if(SLUDGE_COMPILER)
	add_subdirectory(source/DevKit)
endif()
if(SLUDGE_DEVKIT_GTK)
	add_subdirectory(GTK_Dev_Kit)
endif()
if(SLUDGE_DOC)
	add_subdirectory(doc)
endif()

# The renderers, which the engine and the Dev Kit draw with: OpenGL (or
# OpenGL ES 2.0), and SDL_GPU. They know nothing of the engine. A program
# that links them defines debugOut (source/Engine/debug.h).
if(SLUDGE_ENGINE OR SLUDGE_DEVKIT)
	set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source/Engine)
	set(RENDERER_SOURCES renderer.cpp renderer_gl.cpp renderer_window.cpp shaders.cpp sludge_gl.cpp)
	if(SLUDGE_RENDERER_GPU)
		list(APPEND RENDERER_SOURCES renderer_gpu.cpp)
	endif()
	if(WIN32 AND NOT SLUDGE_GLES2)
		list(APPEND RENDERER_SOURCES dxgipresent.cpp)
	endif()
	list(TRANSFORM RENDERER_SOURCES PREPEND ${ENGINE_DIR}/)

	set(SHADERS
		fixScaleSprite.frag fixScaleSprite.vert
		scale.frag scale_noaa.frag scale.vert
		texture.frag texture.vert
		color.frag color.vert
		yuv.frag yuv.vert
		vertexcolor.frag vertexcolor.vert
	)
	list(TRANSFORM SHADERS PREPEND ${ENGINE_DIR}/)

	# We build the shaders of OpenGL into the library.
	set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
	add_custom_command(
		OUTPUT ${GENERATED_DIR}/shaders_embedded.h
		COMMAND ${CMAKE_COMMAND} -DOUTPUT=${GENERATED_DIR}/shaders_embedded.h "-DSHADERS=${SHADERS}"
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake
		DEPENDS ${SHADERS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake
		COMMENT "Embedding shaders"
		VERBATIM)

	add_library(sludge-renderer STATIC ${RENDERER_SOURCES} ${GENERATED_DIR}/shaders_embedded.h)
	target_include_directories(sludge-renderer PUBLIC ${ENGINE_DIR} PRIVATE ${GENERATED_DIR})
	target_link_libraries(sludge-renderer PUBLIC sludge::sdl3)
	if(SLUDGE_RENDERER_GPU)
		target_compile_definitions(sludge-renderer PUBLIC SLUDGE_RENDERER_GPU)
	endif()
	# The graphics driver of the OpenPandora ignores the swap interval, so
	# we wait for the vertical sync of the framebuffer ourselves.
	if(SLUDGE_DEVICE STREQUAL "OpenPandora")
		target_compile_definitions(sludge-renderer PRIVATE SLUDGE_FRAMEBUFFER_VSYNC)
	endif()
	if(SLUDGE_GLES2)
		target_compile_definitions(sludge-renderer PUBLIC HAVE_GLES2)
		target_link_libraries(sludge-renderer PUBLIC GLESv2)
	else()
		find_package(OpenGL REQUIRED)
		target_link_libraries(sludge-renderer PUBLIC OpenGL::GL)
	endif()
endif()

if(SLUDGE_ENGINE)
	# On every device, we draw with OpenGL ES 2.0. On the OpenPandora, we always
	# show the game in full screen, and we wait for the vertical sync of the
	# framebuffer ourselves, because the graphics driver ignores the swap
	# interval.
	if(SLUDGE_DEVICE STREQUAL "OpenPandora")
		set(DEVICE_DEFINITIONS SLUDGE_ALWAYS_FULLSCREEN SLUDGE_FRAMEBUFFER_VSYNC)
		set(DEVICE_LIBRARIES iconv)
	elseif(SLUDGE_DEVICE STREQUAL "RaspberryPi")
		# SDL 3 draws through the Broadcom driver of the Raspberry Pi when SDL
		# is built with bcm_host, and through Mesa otherwise.
	elseif(SLUDGE_DEVICE STREQUAL "ODROID")
	elseif(NOT SLUDGE_DEVICE STREQUAL "")
		message(FATAL_ERROR "SLUDGE_DEVICE must be OpenPandora, RaspberryPi or ODROID")
	endif()

	set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source/Engine)
	set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source/CommonCode)

	set(ENGINE_SOURCES
		backdrop.cpp bg_effects.cpp builtin.cpp capture.cpp controllermap.cpp cp1250.cpp controllersettings.cpp cursors.cpp debug.cpp
		fileset.cpp floor.cpp fonttext.cpp freeze.cpp gamepad.cpp graphics.cpp helpers.cpp keys.cpp
		language.cpp line.cpp loadsave.cpp main.cpp pointer.cpp
		moreio.cpp newfatal.cpp objtypes.cpp people.cpp
		region.cpp savedata.cpp sludger.cpp sprbanks.cpp
		sprites.cpp statusba.cpp stringy.cpp talk.cpp
		thumbnail.cpp timing.cpp transition.cpp variable.cpp
		zbuffer.cpp sound_sdl.cpp
	)
	if(SLUDGE_MOVIES)
		list(APPEND ENGINE_SOURCES movie.cpp libwebm/mkvreader.cpp)
	else()
		list(APPEND ENGINE_SOURCES movie_none.cpp)
	endif()
	list(TRANSFORM ENGINE_SOURCES PREPEND ${ENGINE_DIR}/)
	list(APPEND ENGINE_SOURCES ${COMMON_DIR}/utf8.cpp)

	if(WIN32)
		# The resource script contains the setup dialog, the cursors and the icons.
		enable_language(RC)
		list(APPEND ENGINE_SOURCES ${ENGINE_DIR}/winstuff.cpp ${CMAKE_CURRENT_SOURCE_DIR}/images/SLUDGE.RC)
	else()
		list(APPEND ENGINE_SOURCES ${ENGINE_DIR}/posixstuff.cpp)
	endif()

	add_executable(sludge-engine ${ENGINE_SOURCES})

	target_include_directories(sludge-engine PRIVATE ${COMMON_DIR})
	target_link_libraries(sludge-engine PRIVATE
		sludge-renderer sludge::sdl3 sludge::vorbis sludge::png sludge::dumb)

	if(SLUDGE_GLES2)
		target_compile_definitions(sludge-engine PRIVATE ${DEVICE_DEFINITIONS})
		target_link_libraries(sludge-engine PRIVATE ${DEVICE_LIBRARIES})
	endif()

	if(SLUDGE_MOVIES)
		target_link_libraries(sludge-engine PRIVATE sludge::vpx sludge::webm)
		if(TARGET libvpx)
			add_dependencies(sludge-engine libvpx)
		endif()
	endif()

	if(WIN32)
		set_target_properties(sludge-engine PROPERTIES WIN32_EXECUTABLE TRUE)
		target_include_directories(sludge-engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/images)
		target_compile_definitions(sludge-engine PRIVATE UNICODE _UNICODE)
		target_link_libraries(sludge-engine PRIVATE comdlg32 shell32 ole32)
		if(MINGW)
			# SDL provides wWinMain in Unicode builds, which requires -municode.
			# We link the C and C++ runtime libraries statically, so that the
			# engine runs without the MinGW DLLs.
			target_link_options(sludge-engine PRIVATE -municode -static -static-libgcc -static-libstdc++)
		endif()
	endif()

	# On Linux, iconv is part of the C library. On macOS, it is a separate
	# library. On Windows, we convert text with the Windows API instead.
	include(CheckFunctionExists)
	check_function_exists(iconv_open HAVE_ICONV_IN_LIBC)
	if(NOT WIN32 AND NOT HAVE_ICONV_IN_LIBC)
		find_package(Iconv REQUIRED)
		target_link_libraries(sludge-engine PRIVATE Iconv::Iconv)
		set(ICONV_LIBRARIES Iconv::Iconv)
	endif()

	install(TARGETS sludge-engine RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

	if(SLUDGE_TESTS)
		add_executable(gamepad_test tests/gamepad_test.cpp ${ENGINE_DIR}/gamepad.cpp
			${ENGINE_DIR}/controllermap.cpp ${ENGINE_DIR}/keys.cpp)
		target_include_directories(gamepad_test PRIVATE ${ENGINE_DIR} ${COMMON_DIR})
		target_link_libraries(gamepad_test PRIVATE sludge::sdl3)
		add_test(NAME gamepad COMMAND gamepad_test)

		add_executable(cp1250_test tests/cp1250_test.cpp ${ENGINE_DIR}/cp1250.cpp)
		target_include_directories(cp1250_test PRIVATE ${ENGINE_DIR})
		target_link_libraries(cp1250_test PRIVATE ${ICONV_LIBRARIES})
		add_test(NAME cp1250 COMMAND cp1250_test)

		include(cmake/CaptureTests.cmake)
	endif()
endif()
