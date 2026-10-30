# sludge_compile_game(<name> <project.slp>)
#
# Compile a SLUDGE project with sludge-compiler when we build, into
# ${CMAKE_BINARY_DIR}/games/<name>.slg, with a target named game-<name>.
#
# The compiler writes the game file next to the project file, so we compile
# a copy of the project folder in the build folder, and leave the source
# folder as it is.

set(SLUDGE_RUN_COMPILER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/RunSludgeCompiler.cmake)

function(sludge_compile_game name project)
	get_filename_component(project ${project} ABSOLUTE)
	get_filename_component(project_dir ${project} DIRECTORY)
	set(games_dir ${CMAKE_BINARY_DIR}/games)
	set(output ${games_dir}/${name}.slg)

	# We compile again when any file in the project folder changes.
	file(GLOB_RECURSE project_files CONFIGURE_DEPENDS ${project_dir}/*)

	add_custom_command(
		OUTPUT ${output}
		COMMAND ${CMAKE_COMMAND}
			-DCOMPILER=$<TARGET_FILE:sludge-compiler>
			-DPROJECT=${project}
			-DWORK_DIR=${games_dir}/${name}.project
			-DOUTPUT=${output}
			-P ${SLUDGE_RUN_COMPILER_SCRIPT}
		DEPENDS sludge-compiler ${project_files} ${SLUDGE_RUN_COMPILER_SCRIPT}
		COMMENT "Compiling the SLUDGE game ${name}"
		VERBATIM)
	add_custom_target(game-${name} ALL DEPENDS ${output})
endfunction()
