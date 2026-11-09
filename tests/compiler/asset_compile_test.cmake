# Compile a project of rigs and clips (.rig and .clp assets) that
# make_asset_project writes, with sludge-compiler --machine, and check what
# the compiler says. We run this script with cmake -P and these variables:
#
#   COMPILER   sludge-compiler
#   MAKER      make_asset_project
#   CASE       the project to make (see make_asset_project.cpp)
#   WORK_DIR   a folder for it
#   BANK       a sprite bank, which the project has as bank.duc
#   SOUND      a sound, which the project has as pop.wav
#   ERROR      a regular expression: the compiler must fail with an error
#              message of the project that matches it
#   WARNING    or a warning that matches it, and succeed
#   RESOURCES  or succeed with no message, and this many files in the game
#   ENGINE     with RESOURCES: an engine to play the game, which must end
#              without an error

foreach(var COMPILER MAKER CASE WORK_DIR BANK SOUND)
	if(NOT DEFINED ${var})
		message(FATAL_ERROR "${var} is not set")
	endif()
endforeach()

file(REMOVE_RECURSE ${WORK_DIR})
file(MAKE_DIRECTORY ${WORK_DIR})
execute_process(COMMAND ${MAKER} ${CASE} ${WORK_DIR} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "make_asset_project ${CASE} failed")
endif()
configure_file(${BANK} ${WORK_DIR}/bank.duc COPYONLY)
configure_file(${SOUND} ${WORK_DIR}/pop.wav COPYONLY)

execute_process(
	COMMAND ${COMPILER} --machine Assets.slp
	WORKING_DIRECTORY ${WORK_DIR}
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE errors)
string(REGEX MATCHALL "(^|\n)message\t[^\n]*" messages "${output}")

if(DEFINED ERROR)
	if(result EQUAL 0 OR NOT output MATCHES "\ndone\t0\t")
		message(FATAL_ERROR "The compiler didn't fail:\n${output}${errors}")
	endif()
	if(NOT output MATCHES "(^|\n)message\terror\t${ERROR}")
		message(FATAL_ERROR "There is no error message that matches \"${ERROR}\":\n${output}")
	endif()
	return()
endif()

if(NOT result EQUAL 0 OR NOT output MATCHES "\ndone\t1\t([^\n]*)\n$")
	message(FATAL_ERROR "The compiler failed:\n${output}${errors}")
endif()
set(game ${CMAKE_MATCH_1})

if(DEFINED WARNING)
	if(NOT output MATCHES "(^|\n)message\twarning\t${WARNING}")
		message(FATAL_ERROR "There is no warning that matches \"${WARNING}\":\n${output}")
	endif()
	return()
endif()

if(messages)
	message(FATAL_ERROR "The project has errors or warnings:\n${output}")
endif()
if(DEFINED RESOURCES)
	# The last stats line: the compiler writes one as it starts, too.
	string(REGEX MATCHALL "stats\t[0-9]+\t[0-9]+\t[0-9]+\t[0-9]+\t[0-9]+" stats "${output}")
	list(GET stats -1 last)
	if(NOT last MATCHES "\t([0-9]+)$")
		message(FATAL_ERROR "There is no stats line:\n${output}")
	endif()
	if(NOT CMAKE_MATCH_1 EQUAL RESOURCES)
		message(FATAL_ERROR "The game has ${CMAKE_MATCH_1} files, not ${RESOURCES}:\n${output}")
	endif()
endif()
if(DEFINED ENGINE)
	execute_process(
		COMMAND ${CMAKE_COMMAND} -E env --unset=SLUDGE_RENDERER
			${ENGINE} --capture=${WORK_DIR}/capture --frames=0 --renderer=gl ${game}
		RESULT_VARIABLE result
		OUTPUT_VARIABLE log
		ERROR_VARIABLE log
		TIMEOUT 120)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "The game stopped with ${result}:\n${log}")
	endif()
endif()
