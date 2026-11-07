# What lives, by the engine's lifetime log (SLUDGE_LIFETIME_LOG): animation
# handles, definitions, costumes, characters and sprite banks.
#
# cmake -DENGINE=<sludge-engine> -DGAME=<LoadTest.slg>
#       -DUNLOAD_GAME=<UnloadTest.slg> -DOUT=<folder> -P lifetimes.cmake
#
# 1. UnloadTest lets every costume, animation and character go: at its end,
#    only the mouse cursor's handle lives.
# 2. LoadTest saves and loads twice: what lives after the second load must be
#    what lived after the first, and after the second save, so that loading
#    frees what it replaces.
# 3. Failed loads: LoadTest again, with its save cut short at 64 places
#    (SLUDGE_TEST_CUT_SAVE). Each must end in "This saved game appears to be
#    corrupted", without a crash. With the sanitisers, no error, and no leak
#    of what the loaders made.

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

# Run game in OUT/name, with variables of the environment, and set
# <name>_result and <name>_log.
function(run_game name game)
	set(folder "${OUT}/${name}")
	file(MAKE_DIRECTORY "${folder}")
	execute_process(
		COMMAND ${CMAKE_COMMAND} -E env --unset=SLUDGE_RENDERER
			SLUDGE_LIFETIME_LOG=1 SLUDGE_SPRITEBANK_CACHE_MB=0 ${ARGN}
			"${ENGINE}" "--capture=${folder}" --frames=0 --renderer=gl "${game}"
		RESULT_VARIABLE result
		OUTPUT_FILE "${folder}.log"
		ERROR_FILE "${folder}.log"
		TIMEOUT 300)
	file(READ "${folder}.log" log)
	if(log MATCHES "ERROR: AddressSanitizer|runtime error:")
		message(FATAL_ERROR "${name}: the sanitisers found something:\n${log}")
	endif()
	set(${name}_result "${result}" PARENT_SCOPE)
	set(${name}_log "${log}" PARENT_SCOPE)
endfunction()

# The counts of the lifetime line for when, from log.
function(lifetimes log when out)
	if(NOT log MATCHES "Lifetimes ${when}: ([0-9]+ handles, [0-9]+ definitions, [0-9]+ costumes, [0-9]+ characters, [0-9]+ banks, [0-9]+ rigs)")
		message(FATAL_ERROR "No lifetime line '${when}':\n${log}")
	endif()
	set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

# 1. Letting go
run_game(unload "${UNLOAD_GAME}")
if(NOT unload_result EQUAL 0)
	message(FATAL_ERROR "UnloadTest stopped with ${unload_result}:\n${unload_log}")
endif()
lifetimes("${unload_log}" "at quit" atQuit)
if(NOT atQuit STREQUAL "1 handles, 0 definitions, 0 costumes, 0 characters, 0 banks, 0 rigs")
	message(FATAL_ERROR "UnloadTest ended with ${atQuit} alive, not only the cursor's handle")
endif()
message(STATUS "UnloadTest at quit: ${atQuit}")

# 2. Saving and loading twice
run_game(load "${GAME}")
if(NOT load_result EQUAL 0)
	message(FATAL_ERROR "LoadTest stopped with ${load_result}:\n${load_log}")
endif()
string(REGEX MATCHALL "Lifetimes after (saving|loading): [^\n]*" lines "${load_log}")
list(LENGTH lines count)
if(NOT count EQUAL 4)
	message(FATAL_ERROR "LoadTest should save and load twice:\n${load_log}")
endif()
list(GET lines 1 firstLoad)
list(GET lines 2 secondSave)
list(GET lines 3 secondLoad)
string(REGEX REPLACE "^[^:]*: " "" firstLoad "${firstLoad}")
string(REGEX REPLACE "^[^:]*: " "" secondSave "${secondSave}")
string(REGEX REPLACE "^[^:]*: " "" secondLoad "${secondLoad}")
if(NOT firstLoad STREQUAL secondSave OR NOT firstLoad STREQUAL secondLoad)
	message(FATAL_ERROR "Loading doesn't free what it replaces:\n"
		"  after the first load:  ${firstLoad}\n"
		"  after the second save: ${secondSave}\n"
		"  after the second load: ${secondLoad}")
endif()
message(STATUS "LoadTest after each load: ${firstLoad}")

# 3. Loads that fail
file(GLOB_RECURSE saves "${OUT}/load/*first.sav")
list(LENGTH saves found)
if(NOT found EQUAL 1)
	message(FATAL_ERROR "Can't find LoadTest's first.sav in ${OUT}/load")
endif()
file(SIZE "${saves}" size)
set(cuts 64)
set(fatalCounts "")
foreach(k RANGE 1 ${cuts})
	math(EXPR bytes "${size} * ${k} / (${cuts} + 1)")
	run_game(cut-${k} "${GAME}" SLUDGE_TEST_CUT_SAVE=${bytes})
	set(log "${cut-${k}_log}")
	if(NOT "${cut-${k}_result}" EQUAL 1 OR NOT log MATCHES "This saved game appears to be corrupted")
		message(FATAL_ERROR "LoadTest with its save cut at ${bytes} of ${size} bytes stopped with ${cut-${k}_result}, not as a corrupted save:\n${log}")
	endif()
	# LeakSanitizer (Linux): nothing that the loaders made may leak. Each
	# report is a "Direct leak" or "Indirect leak" line and its stack; one
	# with a loader in its stack is ours. (Others, as of fontconfig, aren't.)
	if(log MATCHES "ERROR: LeakSanitizer")
		string(REGEX MATCHALL "(Direct|Indirect) leak of[^\n]*\n([ \t]+#[^\n]*\n)*" leaks "${log}")
		foreach(leak ${leaks})
			if(leak MATCHES " in (loadGame|loadPeople|loadCostume|loadAnim|loadVariable|loadFunction|loadStack|loadStackRef|loadHSI|loadRegions|loadObjectRef|loadHandlers|loadSpeech|loadStatusBars|loadSounds|restoreSnapshot|blur_loadSettings)\\(")
				message(FATAL_ERROR "LoadTest with its save cut at ${bytes} of ${size} bytes leaks what it loaded:\n${leak}")
			endif()
		endforeach()
	endif()
	lifetimes("${log}" "at a fatal error" atFatal)
	list(APPEND fatalCounts "${bytes}: ${atFatal}")
	file(REMOVE_RECURSE "${OUT}/cut-${k}")
endforeach()
message(STATUS "All ${cuts} cuts of the ${size} bytes of the save failed as corrupted saves. What lived then:")
foreach(line ${fatalCounts})
	message(STATUS "  ${line}")
endforeach()
