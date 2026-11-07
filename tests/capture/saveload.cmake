# A game that saves, plays a while, loads that save and plays the same again
# must show the same frames both times: loading restores everything that the
# frames show, the playback of clips too.
#
# cmake -DENGINE=<sludge-engine> -DGAME=<game.slg> -DOUT=<folder>
#       [-DENGINE_ARGS=<options>] [-DWINDOW=<frames>] -P saveload.cmake
#
# The game must end soon after it plays the second time. We take the CRCs of
# WINDOW frames from near the end (frames.tsv, see source/Engine/capture.h),
# which must differ among themselves (something moves), and find the same
# run of frames earlier on.

foreach(var ENGINE GAME OUT)
	if(NOT DEFINED ${var})
		message(FATAL_ERROR "${var} is not set")
	endif()
endforeach()
if(NOT DEFINED WINDOW)
	set(WINDOW 20)
endif()
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

separate_arguments(extra NATIVE_COMMAND "${ENGINE_ARGS}")
execute_process(
	COMMAND ${CMAKE_COMMAND} -E env --unset=SLUDGE_RENDERER
		"${ENGINE}" "--capture=${OUT}/run" --frames=0 ${extra} "${GAME}"
	RESULT_VARIABLE result
	OUTPUT_FILE "${OUT}/run.log"
	ERROR_FILE "${OUT}/run.log"
	TIMEOUT 600)
file(READ "${OUT}/run.log" log)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "${ENGINE} stopped with ${result}:\n${log}")
endif()
if(log MATCHES "ERROR: AddressSanitizer|runtime error:")
	message(FATAL_ERROR "The sanitisers found something:\n${log}")
endif()

file(STRINGS "${OUT}/run/frames.tsv" lines)
set(crcs "")
foreach(line ${lines})
	string(REGEX REPLACE "^.*\t" "" crc "${line}")
	list(APPEND crcs "${crc}")
endforeach()
list(LENGTH crcs count)
math(EXPR start "${count} - ${WINDOW} - 5")
if(start LESS WINDOW)
	message(FATAL_ERROR "Only ${count} frames: too few")
endif()
list(SUBLIST crcs ${start} ${WINDOW} tail)
set(distinct ${tail})
list(REMOVE_DUPLICATES distinct)
list(LENGTH distinct distinctCount)
math(EXPR enough "${WINDOW} / 2")
if(distinctCount LESS enough)
	message(FATAL_ERROR "The last frames hardly change (${distinctCount} different of ${WINDOW}), so they prove nothing")
endif()

string(JOIN "," wanted ${tail})
math(EXPR last "${start} - ${WINDOW}")
set(found -1)
foreach(s RANGE 0 ${last})
	list(SUBLIST crcs ${s} ${WINDOW} run)
	string(JOIN "," got ${run})
	if(got STREQUAL wanted)
		set(found ${s})
		break()
	endif()
endforeach()
if(found LESS 0)
	message(FATAL_ERROR "Frames ${start} to ${start}+${WINDOW} after loading don't match any run of frames before it: loading didn't restore what was saved. Frames: ${OUT}/run")
endif()
message(STATUS "Frames ${start}-${WINDOW} after loading are frames ${found}-${WINDOW} before")
