# A game that must stop with a fatal error: the engine must exit with 1 and
# print MESSAGE (a regular expression), without a crash or a sanitiser's
# report.
#
# cmake -DENGINE=<sludge-engine> -DGAME=<game.slg> -DOUT=<folder>
#       -DMESSAGE=<regex> -P expect-fatal.cmake

foreach(var ENGINE GAME OUT MESSAGE)
	if(NOT DEFINED ${var})
		message(FATAL_ERROR "${var} is not set")
	endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
execute_process(
	COMMAND ${CMAKE_COMMAND} -E env --unset=SLUDGE_RENDERER
		"${ENGINE}" "--capture=${OUT}/run" --frames=0 --renderer=gl "${GAME}"
	RESULT_VARIABLE result
	OUTPUT_FILE "${OUT}/run.log"
	ERROR_FILE "${OUT}/run.log"
	TIMEOUT 300)
file(READ "${OUT}/run.log" log)
if(log MATCHES "ERROR: AddressSanitizer|runtime error:")
	message(FATAL_ERROR "The sanitisers found something:\n${log}")
endif()
if(NOT result EQUAL 1 OR NOT log MATCHES "${MESSAGE}")
	message(FATAL_ERROR "The game stopped with ${result}, not with \"${MESSAGE}\":\n${log}")
endif()
