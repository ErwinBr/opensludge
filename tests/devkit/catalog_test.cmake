# Check that source/DevKit/Core/ScriptCatalogData.inc is what
# make_script_catalog writes from the help now. We run this script with
# cmake -P and these variables:
#
#   MAKER     make_script_catalog
#   HELP      doc/SLUDGEDevKitHelp
#   EXPECTED  the file in the repository
#   OUTPUT    where to write the new one

execute_process(COMMAND ${MAKER} ${HELP} ${OUTPUT} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "make_script_catalog failed")
endif()
# The same but for the \r of a checkout on Windows.
file(READ ${OUTPUT} made)
file(READ ${EXPECTED} kept)
string(REPLACE "\r" "" made "${made}")
string(REPLACE "\r" "" kept "${kept}")
if(NOT made STREQUAL kept)
	message(FATAL_ERROR "${EXPECTED} isn't what the help says now: run\n  make_script_catalog ${HELP} ${EXPECTED}")
endif()
