# Run a capture test: run a game with the engine deterministically and
# compare the frames with framediff (see source/Engine/capture.h).
#
#   cmake -DENGINE=<sludge-engine> -DFRAMEDIFF=<framediff> -DGAME=<game.slg>
#         -DOUT=<folder> [-DINPUT=<script>] [-DMAX_FRAMES=<n>] [-DFRAMES=<list>]
#         [-DENGINE_ARGS=<options>] [-DEXPECTED_ARGS=<options>]
#         [-DREFERENCE_ENGINE=<sludge-engine>] [-DREQUIRE=<regex>]
#         [-DENGINE_ENV=<variable>=<value>] [-DMAX_DIFF=<n>]
#         [-DMAX_BAD=<fraction>] -P run.cmake
#
# Without REFERENCE_ENGINE, we run the engine twice and the runs must be the
# same, which tests that the run is deterministic. With REFERENCE_ENGINE, we
# compare with a run of that engine, for example a build of an earlier
# commit. With EXPECTED_ARGS, the first run has those options instead of
# ENGINE_ARGS, for example to compare two renderers. The second run has
# the environment variables of ENGINE_ENV, and its output must match
# REQUIRE. We unset SLUDGE_RENDERER for both runs. The frames are in
# OUT/expected and OUT/actual, and the images that differ in
# OUT/difference.

foreach(var ENGINE FRAMEDIFF GAME OUT)
	if(NOT DEFINED ${var})
		message(FATAL_ERROR "${var} is not set")
	endif()
endforeach()
if(NOT EXISTS "${GAME}")
	message(FATAL_ERROR "The game ${GAME} doesn't exist")
endif()
if(NOT DEFINED MAX_DIFF)
	set(MAX_DIFF 0)
endif()
if(NOT DEFINED MAX_BAD)
	set(MAX_BAD 0)
endif()

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

function(run_engine engine folder engine_args engine_env)
	set(args "--capture=${OUT}/${folder}")
	if(DEFINED INPUT)
		list(APPEND args "--input=${INPUT}")
	endif()
	if(DEFINED MAX_FRAMES)
		list(APPEND args "--max-frames=${MAX_FRAMES}")
	endif()
	if(DEFINED FRAMES)
		list(APPEND args "--frames=${FRAMES}")
	endif()
	separate_arguments(extra NATIVE_COMMAND "${engine_args}")
	execute_process(
		COMMAND ${CMAKE_COMMAND} -E env --unset=SLUDGE_RENDERER ${engine_env}
			"${engine}" ${args} ${extra} "${GAME}"
		RESULT_VARIABLE result
		OUTPUT_FILE "${OUT}/${folder}.log"
		ERROR_FILE "${OUT}/${folder}.log"
		TIMEOUT 600)
	if(NOT result EQUAL 0)
		file(READ "${OUT}/${folder}.log" log)
		message(FATAL_ERROR "${engine} stopped with ${result}:\n${log}")
	endif()
endfunction()

if(NOT DEFINED EXPECTED_ARGS)
	set(EXPECTED_ARGS "${ENGINE_ARGS}")
endif()
if(DEFINED REFERENCE_ENGINE AND NOT REFERENCE_ENGINE STREQUAL "")
	run_engine("${REFERENCE_ENGINE}" expected "${EXPECTED_ARGS}" "")
else()
	run_engine("${ENGINE}" expected "${EXPECTED_ARGS}" "")
endif()
run_engine("${ENGINE}" actual "${ENGINE_ARGS}" "${ENGINE_ENV}")
if(DEFINED REQUIRE)
	file(READ "${OUT}/actual.log" log)
	if(NOT log MATCHES "${REQUIRE}")
		message(FATAL_ERROR "The engine didn't print \"${REQUIRE}\":\n${log}")
	endif()
endif()

execute_process(
	COMMAND "${FRAMEDIFF}" --max-diff=${MAX_DIFF} --max-bad=${MAX_BAD} "--out=${OUT}/difference"
		"${OUT}/expected" "${OUT}/actual"
	RESULT_VARIABLE result)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "The frames differ. See ${OUT}/difference.")
endif()
