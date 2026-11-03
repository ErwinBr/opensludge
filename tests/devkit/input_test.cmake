# Edit a document in the Dev Kit with the mouse and the keys of a file
# (sludge-devkit --smoke-input, App/SmokeInput.h), save it, and compare it
# with what we expect. We run this script with cmake -P and these variables:
#
#   DEVKIT    sludge-devkit
#   DOCUMENT  the document to open, if any
#   INPUT     the steps, in which ${OUTPUT} is where to save and
#             ${FIXTURES} is the folder of FIXTURES
#   FIXTURES  the folder of the test files
#   EXPECTED  the file that we expect
#   WORK_DIR  a folder for what we make

file(REMOVE_RECURSE ${WORK_DIR})
file(MAKE_DIRECTORY ${WORK_DIR})
get_filename_component(name ${EXPECTED} NAME)
set(OUTPUT ${WORK_DIR}/${name})
configure_file(${INPUT} ${WORK_DIR}/input.txt)

execute_process(
	COMMAND ${DEVKIT} --smoke=${WORK_DIR}/devkit.png --smoke-input=${WORK_DIR}/input.txt ${DOCUMENT}
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE output)
message("${output}")
if(NOT result EQUAL 0)
	message(FATAL_ERROR "The steps of ${INPUT} didn't work")
endif()

# We compare the bytes, but on Windows, the lines of the text files that we
# save end with \r\n.
file(READ ${OUTPUT} saved HEX)
file(READ ${EXPECTED} expected HEX)
string(REGEX REPLACE "0d0a" "0a" saved_lines "${saved}")
string(REGEX REPLACE "0d0a" "0a" expected_lines "${expected}")
if(NOT saved STREQUAL expected AND NOT saved_lines STREQUAL expected_lines)
	message(FATAL_ERROR "${OUTPUT} isn't as we expect, in ${EXPECTED}")
endif()
