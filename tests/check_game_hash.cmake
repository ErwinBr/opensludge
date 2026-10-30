# Check the SHA-256 hash of a compiled game file against the list in
# tests/games.sha256, which has the format of sha256sum. We run this script
# with cmake -P and these variables:
#
#   GAME    the game file, for example build/games/Welcome.slg
#   HASHES  the list of hashes

get_filename_component(game_name ${GAME} NAME)
if(NOT EXISTS ${GAME})
	message(FATAL_ERROR "${GAME} is missing. Build the project first.")
endif()

file(STRINGS ${HASHES} lines REGEX "^[0-9a-f]+ [ *]${game_name}$")
if(NOT lines)
	message(FATAL_ERROR "${HASHES} has no hash for ${game_name}")
endif()
string(REGEX REPLACE " .*$" "" expected "${lines}")

file(SHA256 ${GAME} actual)
if(NOT actual STREQUAL expected)
	message(FATAL_ERROR "The hash of ${game_name} is\n  ${actual}\nbut ${HASHES} has\n  ${expected}\n"
		"If the compiler changed on purpose, update ${HASHES}.")
endif()
message(STATUS "${game_name}: ${actual}")
