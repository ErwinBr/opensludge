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

add_executable(sludge-compiler ${DEVKIT_DIR}/Compiler/CompilerMain.cpp)
target_link_libraries(sludge-compiler PRIVATE devkit-compiler)

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
	endif()
endif()
