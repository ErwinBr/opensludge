# Compile a copy of a project in the Dev Kit, as a user does: open it, and
# compile it, with sludge-devkit --smoke-compile. We run this script with
# cmake -P and these variables:
#
#   DEVKIT    sludge-devkit
#   PROJECT   the project file
#   WORK_DIR  a folder for the copy of the project

get_filename_component(project_dir ${PROJECT} DIRECTORY)
get_filename_component(project_name ${PROJECT} NAME)
file(REMOVE_RECURSE ${WORK_DIR})
file(COPY ${project_dir}/ DESTINATION ${WORK_DIR})

execute_process(
	COMMAND ${DEVKIT} --smoke-compile=${WORK_DIR}/${project_name} --smoke=${WORK_DIR}/devkit.png
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE output)
message("${output}")
if(NOT result EQUAL 0)
	message(FATAL_ERROR "The Dev Kit couldn't compile ${project_name}")
endif()
file(GLOB games ${WORK_DIR}/*.slg)
if(NOT games)
	message(FATAL_ERROR "There is no game in ${WORK_DIR}")
endif()
