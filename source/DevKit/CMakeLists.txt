# The SLUDGE compiler, which turns a project of scripts and resources into a
# game data file (.slg). It runs without a display and needs only libpng,
# so we build it on every platform. The GTK 3 tools in GTK_Dev_Kit use the
# libraries here too.

set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/source)
set(DEVKIT_DIR ${SOURCE_DIR}/DevKit)

# The code that the tools share, in static libraries.
add_library(devkit-common STATIC
	${DEVKIT_DIR}/Common/moreio.cpp
	${DEVKIT_DIR}/Common/helpers.cpp
	${DEVKIT_DIR}/Common/stringarray.cpp
	${SOURCE_DIR}/CommonCode/tga.cpp
	${SOURCE_DIR}/CommonCode/utf8.cpp
)
target_include_directories(devkit-common PUBLIC
	${SOURCE_DIR}/CommonCode
	${DEVKIT_DIR}/Common
)

add_library(devkit-compiler STATIC
	${DEVKIT_DIR}/ProjectManager/project.cpp
	${DEVKIT_DIR}/ProjectManager/settings.cpp
	${DEVKIT_DIR}/ProjectManager/messbox.cpp
	${DEVKIT_DIR}/Compiler/compiler.cpp
	${DEVKIT_DIR}/Compiler/compilerinfo.cpp
	${DEVKIT_DIR}/Compiler/allknown.cpp
	${DEVKIT_DIR}/Compiler/checkused.cpp
	${DEVKIT_DIR}/Compiler/dumpfiles.cpp
	${DEVKIT_DIR}/Compiler/floor.cpp
	${DEVKIT_DIR}/Compiler/hsi.cpp
	${DEVKIT_DIR}/Compiler/linker.cpp
	${DEVKIT_DIR}/Compiler/objtype.cpp
	${DEVKIT_DIR}/Compiler/preproc.cpp
	${DEVKIT_DIR}/Compiler/realproc.cpp
	${DEVKIT_DIR}/Compiler/sludge_functions.cpp
	${DEVKIT_DIR}/Compiler/splitter.cpp
	${DEVKIT_DIR}/Compiler/tokens.cpp
	${DEVKIT_DIR}/Compiler/translation.cpp
)
target_include_directories(devkit-compiler PUBLIC
	${DEVKIT_DIR}/Compiler
	${DEVKIT_DIR}/ProjectManager
)
target_link_libraries(devkit-compiler PUBLIC devkit-common sludge::png)

# The models of the floor maker, the z-buffer maker and the sprite bank
# editor, and the drawing of their views through draw2d.h, which knows
# nothing of how we draw.
if(SLUDGE_DEVKIT OR SLUDGE_DEVKIT_GTK)
	add_library(devkit-graphics STATIC
		${DEVKIT_DIR}/GraphicalTools/sprites.cpp
		${DEVKIT_DIR}/GraphicalTools/floormaker.cpp
		${DEVKIT_DIR}/GraphicalTools/zbuffer.cpp
	)
	target_include_directories(devkit-graphics PUBLIC ${DEVKIT_DIR}/GraphicalTools)
	target_link_libraries(devkit-graphics PUBLIC devkit-common sludge::png)
endif()

add_executable(sludge-compiler ${DEVKIT_DIR}/Compiler/CompilerMain.cpp)
target_link_libraries(sludge-compiler PRIVATE devkit-compiler)
if(MINGW)
	# As the engine, the compiler runs without the MinGW DLLs.
	target_link_options(sludge-compiler PRIVATE -static -static-libgcc -static-libstdc++)
endif()

install(TARGETS sludge-compiler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# We compile the example projects into ${CMAKE_BINARY_DIR}/games. We can't
# run the compiler when we cross-compile it.
if(NOT CMAKE_CROSSCOMPILING)
	include(${PROJECT_SOURCE_DIR}/cmake/SludgeCompileGame.cmake)
	sludge_compile_game(Welcome ${PROJECT_SOURCE_DIR}/doc/ExampleProjects/Welcome/Welcome.slp)
	sludge_compile_game(VerbCoin ${PROJECT_SOURCE_DIR}/doc/ExampleProjects/VerbCoin/VerbCoin.slp)

	if(SLUDGE_TESTS)
		add_test(NAME compiler_help COMMAND sludge-compiler --help)
		set_tests_properties(compiler_help PROPERTIES PASS_REGULAR_EXPRESSION "usage: sludge-compiler")

		# The compiler must make the same game files on every platform, so we
		# compare them with the hashes in tests/games.sha256.
		foreach(game Welcome VerbCoin)
			add_test(NAME game_${game}
				COMMAND ${CMAKE_COMMAND} -DGAME=${CMAKE_BINARY_DIR}/games/${game}.slg
					-DHASHES=${PROJECT_SOURCE_DIR}/tests/games.sha256
					-P ${PROJECT_SOURCE_DIR}/tests/check_game_hash.cmake)
		endforeach()

		# A project from the old Windows versions of SLUDGE, with text that
		# isn't UTF-8 and names in another case than on disk.
		add_test(NAME compiler_old_project
			COMMAND ${CMAKE_COMMAND} -DCOMPILER=$<TARGET_FILE:sludge-compiler>
				-DFIXTURES=${PROJECT_SOURCE_DIR}/tests/devkit/fixtures
				-DWORK_DIR=${CMAKE_BINARY_DIR}/old_project
				-P ${PROJECT_SOURCE_DIR}/tests/devkit/old_project_test.cmake)

		# The same with --machine, which the Dev Kit reads.
		foreach(game Welcome VerbCoin)
			add_test(NAME compiler_machine_${game}
				COMMAND ${CMAKE_COMMAND} -DCOMPILER=$<TARGET_FILE:sludge-compiler>
					-DPROJECT=${PROJECT_SOURCE_DIR}/doc/ExampleProjects/${game}/${game}.slp
					-DWORK_DIR=${CMAKE_BINARY_DIR}/machine/${game}
					-DNAME=${game}
					-DHASHES=${PROJECT_SOURCE_DIR}/tests/games.sha256
					-P ${PROJECT_SOURCE_DIR}/tests/check_machine_compile.cmake)
		endforeach()
	endif()
endif()

# The Dev Kit, with SDL 3 and Dear ImGui. Core is the state and the work of
# the editor, which knows nothing of SDL or ImGui; App is the program.
if(SLUDGE_DEVKIT)
	add_library(devkit-core STATIC
		${DEVKIT_DIR}/Core/CommandStack.cpp
		${DEVKIT_DIR}/Core/CompileJob.cpp
		${DEVKIT_DIR}/Core/Document.cpp
		${DEVKIT_DIR}/Core/DocumentManager.cpp
		${DEVKIT_DIR}/Core/FileMemory.cpp
		${DEVKIT_DIR}/Core/FloorDocument.cpp
		${DEVKIT_DIR}/Core/Launch.cpp
		${DEVKIT_DIR}/Core/Log.cpp
		${DEVKIT_DIR}/Core/Preferences.cpp
		${DEVKIT_DIR}/Core/ProjectCheck.cpp
		${DEVKIT_DIR}/Core/ProjectDocument.cpp
		${DEVKIT_DIR}/Core/RecentFiles.cpp
		${DEVKIT_DIR}/Core/SpriteBankDocument.cpp
		${DEVKIT_DIR}/Core/ZBufferDocument.cpp
	)
	target_include_directories(devkit-core PUBLIC ${DEVKIT_DIR}/Core)
	# The compile job reads the compiler on a thread of its own.
	find_package(Threads REQUIRED)
	# The floors and the other pictures are the models of GraphicalTools.
	# Those call the functions of Common/interface.h, which
	# Core/InterfacePlatform.cpp has: the programs take that in themselves,
	# as a library after devkit-graphics would come too early for GNU ld.
	target_link_libraries(devkit-core PUBLIC devkit-graphics Threads::Threads)

	# The drawing of the views with the renderers of the engine.
	add_library(devkit-view STATIC ${DEVKIT_DIR}/GraphicalTools/draw2d_gfx.cpp)
	target_link_libraries(devkit-view PUBLIC devkit-graphics sludge-renderer)

	add_executable(sludge-devkit
		${DEVKIT_DIR}/App/main.cpp
		${DEVKIT_DIR}/App/BuildPanel.cpp
		${DEVKIT_DIR}/App/CanvasView.cpp
		${DEVKIT_DIR}/App/FileCheckWindow.cpp
		${DEVKIT_DIR}/App/FloorView.cpp
		${DEVKIT_DIR}/Core/InterfacePlatform.cpp
		${DEVKIT_DIR}/App/ProjectView.cpp
		${DEVKIT_DIR}/App/imgui_impl_gfx.cpp
		${DEVKIT_DIR}/App/PlatformSDL.cpp
		${DEVKIT_DIR}/App/SmokeInput.cpp
		${DEVKIT_DIR}/App/SpriteBankView.cpp
		${DEVKIT_DIR}/App/Workspace.cpp
		${DEVKIT_DIR}/App/ZBufferView.cpp
	)
	target_include_directories(sludge-devkit PRIVATE ${DEVKIT_DIR}/App ${SOURCE_DIR}/CommonCode)
	# Where the Dev Kit finds the engine and the compiler when it runs from
	# the build folder.
	target_compile_definitions(sludge-devkit PRIVATE SLUDGE_BUILD_FOLDER="${CMAKE_BINARY_DIR}/")
	target_link_libraries(sludge-devkit PRIVATE devkit-core devkit-view sludge-renderer sludge::imgui sludge::png)
	if(WIN32)
		set_target_properties(sludge-devkit PROPERTIES WIN32_EXECUTABLE TRUE)
		if(MINGW)
			target_link_options(sludge-devkit PRIVATE -static -static-libgcc -static-libstdc++)
		endif()
	endif()
	install(TARGETS sludge-devkit RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

	if(SLUDGE_TESTS)
		# It needs a display, as the capture tests.
		add_test(NAME capture-devkit-smoke COMMAND sludge-devkit --smoke=${CMAKE_BINARY_DIR}/devkit-smoke.png)
		set_tests_properties(capture-devkit-smoke PROPERTIES LABELS capture)
		# Open a copy of VerbCoin, compile it with sludge-compiler as a
		# program of its own, and check that it compiled.
		add_test(NAME capture-devkit-compile
			COMMAND ${CMAKE_COMMAND} -DDEVKIT=$<TARGET_FILE:sludge-devkit>
				-DPROJECT=${PROJECT_SOURCE_DIR}/doc/ExampleProjects/VerbCoin/VerbCoin.slp
				-DWORK_DIR=${CMAKE_BINARY_DIR}/devkit-compile
				-P ${PROJECT_SOURCE_DIR}/tests/devkit/compile_test.cmake)
		set_tests_properties(capture-devkit-compile PROPERTIES LABELS capture DEPENDS compiler_help)

		# The floor editor with the mouse and the keys, as a user edits.
		add_test(NAME capture-devkit-floor
			COMMAND ${CMAKE_COMMAND} -DDEVKIT=$<TARGET_FILE:sludge-devkit>
				-DDOCUMENT=${PROJECT_SOURCE_DIR}/tests/devkit/fixtures/floor.flo
				-DFIXTURES=${PROJECT_SOURCE_DIR}/tests/devkit/fixtures
				-DINPUT=${PROJECT_SOURCE_DIR}/tests/devkit/floor_input.txt
				-DEXPECTED=${PROJECT_SOURCE_DIR}/tests/devkit/fixtures/floor-edited.flo
				-DWORK_DIR=${CMAKE_BINARY_DIR}/devkit-floor
				-P ${PROJECT_SOURCE_DIR}/tests/devkit/input_test.cmake)
		set_tests_properties(capture-devkit-floor PROPERTIES LABELS capture)
		# The same for a z-buffer, which the steps make from a TGA picture.
		add_test(NAME capture-devkit-zbuffer
			COMMAND ${CMAKE_COMMAND} -DDEVKIT=$<TARGET_FILE:sludge-devkit>
				-DFIXTURES=${PROJECT_SOURCE_DIR}/tests/devkit/fixtures
				-DINPUT=${PROJECT_SOURCE_DIR}/tests/devkit/zbuffer_input.txt
				-DEXPECTED=${PROJECT_SOURCE_DIR}/tests/devkit/fixtures/zbuffer-edited.zbu
				-DWORK_DIR=${CMAKE_BINARY_DIR}/devkit-zbuffer
				-P ${PROJECT_SOURCE_DIR}/tests/devkit/input_test.cmake)
		set_tests_properties(capture-devkit-zbuffer PROPERTIES LABELS capture)
		# And for a sprite bank: VerbCoin's alien, which has a palette, so
		# that what we save doesn't depend on the version of zlib.
		add_test(NAME capture-devkit-sprites
			COMMAND ${CMAKE_COMMAND} -DDEVKIT=$<TARGET_FILE:sludge-devkit>
				-DDOCUMENT=${PROJECT_SOURCE_DIR}/doc/ExampleProjects/VerbCoin/ego/alien.duc
				-DFIXTURES=${PROJECT_SOURCE_DIR}/tests/devkit/fixtures
				-DINPUT=${PROJECT_SOURCE_DIR}/tests/devkit/sprites_input.txt
				-DEXPECTED=${PROJECT_SOURCE_DIR}/tests/devkit/fixtures/alien-edited.duc
				-DWORK_DIR=${CMAKE_BINARY_DIR}/devkit-sprites
				-P ${PROJECT_SOURCE_DIR}/tests/devkit/input_test.cmake)
		set_tests_properties(capture-devkit-sprites PROPERTIES LABELS capture)

		# The core of the Dev Kit, without a display.
		add_executable(devkit_core_test ${PROJECT_SOURCE_DIR}/tests/devkit/core_test.cpp ${DEVKIT_DIR}/Core/InterfacePlatform.cpp)
		target_link_libraries(devkit_core_test PRIVATE devkit-core)
		file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/core_test)
		add_test(NAME devkit_core COMMAND devkit_core_test ${CMAKE_CURRENT_BINARY_DIR}/core_test
			${PROJECT_SOURCE_DIR}/doc/ExampleProjects ${PROJECT_SOURCE_DIR}/tests/devkit/fixtures)

		# The views of the editors, with both renderers, against the same
		# reference images as the GTK tools (tests/devkit/view_test.cpp).
		add_executable(view_test ${PROJECT_SOURCE_DIR}/tests/devkit/view_test.cpp)
		target_compile_definitions(view_test PRIVATE VIEW_TEST_FOLDER="${PROJECT_SOURCE_DIR}/tests/devkit")
		target_link_libraries(view_test PRIVATE devkit-view)
		set(view_renderers gl)
		if(SLUDGE_GPU_CAPTURE_TESTS)
			list(APPEND view_renderers gpu)
		endif()
		foreach(renderer ${view_renderers})
			add_test(NAME capture-devkit-views-${renderer} COMMAND view_test ${renderer})
			set_tests_properties(capture-devkit-views-${renderer} PROPERTIES LABELS capture)
		endforeach()
	endif()
endif()
