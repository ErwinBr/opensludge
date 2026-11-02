# Compile a project with sludge-compiler --machine, as the Dev Kit does, and
# check what it says: the progress, a "done" line with the game file, and
# that the game has its hash in tests/games.sha256. We run this script with
# cmake -P and these variables:
#
#   COMPILER  sludge-compiler
#   PROJECT   the project file
#   WORK_DIR  a folder for a copy of the project
#   HASHES    the list of hashes
#   NAME      the name of the game in the list, without .slg

get_filename_component(project_dir ${PROJECT} DIRECTORY)
get_filename_component(project_name ${PROJECT} NAME)
file(REMOVE_RECURSE ${WORK_DIR})
file(COPY ${project_dir}/ DESTINATION ${WORK_DIR})

execute_process(
	COMMAND ${COMPILER} --machine ${project_name}
	WORKING_DIRECTORY ${WORK_DIR}
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "sludge-compiler --machine failed:\n${output}${errors}")
endif()

foreach(kind task progress stats)
	if(NOT output MATCHES "(^|\n)${kind}\t")
		message(FATAL_ERROR "There is no ${kind} line in:\n${output}")
	endif()
endforeach()
if(output MATCHES "(^|\n)message\t")
	message(FATAL_ERROR "The project has errors or warnings:\n${output}")
endif()
if(NOT output MATCHES "\ndone\t1\t([^\n]*)\n$")
	message(FATAL_ERROR "The last line isn't a successful done line:\n${output}")
endif()
set(game ${CMAKE_MATCH_1})
if(NOT EXISTS ${game})
	message(FATAL_ERROR "The done line names ${game}, which doesn't exist")
endif()

# The list names the game after its project.
configure_file(${game} ${WORK_DIR}/${NAME}.slg COPYONLY)
set(GAME ${WORK_DIR}/${NAME}.slg)
include(${CMAKE_CURRENT_LIST_DIR}/check_game_hash.cmake)
