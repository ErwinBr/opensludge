# Compile the project from the old Windows versions of SLUDGE
# (fixtures/OldProject), as on every system: first as it is, which fails
# where its script isn't UTF-8, and then with the script in UTF-8, which
# compiles, with warnings for the names that have another case on disk.
# We run this script with cmake -P and these variables:
#
#   COMPILER  sludge-compiler
#   FIXTURES  tests/devkit/fixtures
#   WORK_DIR  a folder for a copy of the project

file(REMOVE_RECURSE ${WORK_DIR})
file(COPY ${FIXTURES}/OldProject/ DESTINATION ${WORK_DIR})

execute_process(COMMAND ${COMPILER} --machine Old.SLP
	WORKING_DIRECTORY ${WORK_DIR} OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT output MATCHES "message\terror\tinit.slu\t3\tThe file is not UTF-8 encoded")
	message(FATAL_ERROR "The compiler didn't say that line 3 of init.slu isn't UTF-8:\n${output}")
endif()
if(NOT output MATCHES "\ndone\t0")
	message(FATAL_ERROR "The compile didn't fail:\n${output}")
endif()

file(COPY_FILE ${FIXTURES}/init-utf8.slu ${WORK_DIR}/init.slu)
execute_process(COMMAND ${COMPILER} --machine Old.SLP
	WORKING_DIRECTORY ${WORK_DIR} OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT output MATCHES "\ndone\t1\t")
	message(FATAL_ERROR "The compile with the script in UTF-8 failed:\n${output}")
endif()
if(NOT output MATCHES "message\twarning\tinit.slu\t0\tThe project names this file in another case: INIT.SLU")
	message(FATAL_ERROR "No warning that the project names init.slu as INIT.SLU:\n${output}")
endif()
if(NOT output MATCHES "message\twarning\tinit.slu\t2\t'pics\\\\\\\\image.tga' has another case on disk: 'Pics/IMAGE.TGA'")
	message(FATAL_ERROR "No warning that pics\\image.tga is Pics/IMAGE.TGA:\n${output}")
endif()
if(NOT EXISTS ${WORK_DIR}/Old.slg)
	message(FATAL_ERROR "There is no Old.slg")
endif()
