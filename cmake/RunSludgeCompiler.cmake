# Compile a SLUDGE project into a game file, for sludge_compile_game in
# SludgeCompileGame.cmake. We run this script with cmake -P and these
# variables:
#
#   COMPILER  the sludge-compiler executable
#   PROJECT   the project file (.slp)
#   WORK_DIR  a folder for a copy of the project folder
#   OUTPUT    the game file to make

get_filename_component(project_dir ${PROJECT} DIRECTORY)
get_filename_component(project_name ${PROJECT} NAME)

file(REMOVE_RECURSE ${WORK_DIR})
file(COPY ${project_dir}/ DESTINATION ${WORK_DIR})

# The compiler names the game file after the finalfile setting of the
# project, and adds " (silent)" with makesilent=Y.
file(STRINGS ${PROJECT} final_file REGEX "^finalfile=" LIMIT_COUNT 1)
string(REGEX REPLACE "^finalfile=" "" final_file "${final_file}")
string(STRIP "${final_file}" final_file)
if(final_file STREQUAL "")
	message(FATAL_ERROR "${PROJECT} has no finalfile setting")
endif()
file(STRINGS ${PROJECT} make_silent REGEX "^makesilent=[Yy]" LIMIT_COUNT 1)
if(make_silent)
	set(game_file "${WORK_DIR}/${final_file} (silent).slg")
else()
	set(game_file "${WORK_DIR}/${final_file}.slg")
endif()

execute_process(
	COMMAND ${COMPILER} ${WORK_DIR}/${project_name}
	RESULT_VARIABLE result)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "sludge-compiler failed on ${PROJECT}")
endif()
if(NOT EXISTS "${game_file}")
	message(FATAL_ERROR "sludge-compiler made no ${game_file}")
endif()

file(COPY_FILE "${game_file}" ${OUTPUT})
file(REMOVE_RECURSE ${WORK_DIR})
