# configure_functions.cmake - various functions from ffmpeg's configure script rewritten for cmake

# Note: in order to not break ffmpeg, we need to arrive at the same result as ffmpeg's configure script's checks, so instead of relying solely on cmake modules, I ported ffmpeg's configure script's functions needed below.

# Create a directory to store all of our tests
set(CONFIG_TESTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/configure_checks")
file(MAKE_DIRECTORY ${CONFIG_TESTS_DIR})

include(CheckIncludeFile)
include(CheckIncludeFiles)

# Rewrite of ffmpeg's check_cc function at line 866 of configure script
function(check_cc target ARGUMENTS RESULT_VAR)
	set(EXTRA_LIBS "")
	set(EXTRA_FLAGS "")
	set(ADDL_LIB_DIRS_CLEAN "")
	set(COMPILE_INCLUDES "")

	foreach(arg IN ITEMS ${ARGN} ${ADDL_LIB_DIRS} ${ADDL_INCLUDES})
		# Windows-specific libs
		if(MSVC AND arg MATCHES "^-l(msvcrt|shell32|advapi32|user32|gdi32|kernel32|psapi|ws2_32)")
			string(REGEX REPLACE "^-l" "" clean_lib "${arg}")
			list(APPEND EXTRA_LIBS "${clean_lib}.lib")

		# Standard UNIX linker flags
		elseif(arg MATCHES "^-l")
			list(APPEND EXTRA_LIBS "${arg}")

		# Absolute library paths (.lib, .a, .so, etc.)
		elseif(IS_ABSOLUTE "${arg}" AND arg MATCHES "\\.(lib|a|so|dylib)$")
			list(APPEND EXTRA_LIBS "${arg}")

		# Library Directory flags (Unix: -L, MSVC: /LIBPATH: or -libpath:)
		elseif(arg MATCHES "^-L")
			string(SUBSTRING "${arg}" 2 -1 clean_dir)
			list(APPEND ADDL_LIB_DIRS_CLEAN "${clean_dir}")
		elseif(arg MATCHES "^[-/][Ll][Ii][Bb][Pp][Aa][Tt][Hh]:")
			string(REGEX REPLACE "^[-/][Ll][Ii][Bb][Pp][Aa][Tt][Hh]:" "" clean_dir "${arg}")
			list(APPEND ADDL_LIB_DIRS_CLEAN "${clean_dir}")

		# Include Directory flags (Unix: -I, MSVC: /I or -I) or raw paths from ADDL_INCLUDES
		elseif(arg MATCHES "^[-/]I")
			string(REGEX REPLACE "^[-/]I" "" clean_dir "${arg}")
			list(APPEND COMPILE_INCLUDES "${clean_dir}")

		# Deal with raw paths if they didn't match any of the above patterns
		elseif(IS_DIRECTORY "${arg}")
			if(arg IN_LIST ADDL_INCLUDES)
				list(APPEND COMPILE_INCLUDES "${arg}")
			else()
				list(APPEND ADDL_LIB_DIRS_CLEAN "${arg}")
			endif()

		# Everything else is a compiler option flag
		else()
			list(APPEND EXTRA_FLAGS "${arg}")
		endif()
	endforeach()
	
	# Clean up duplicate paths/extra libs
	foreach(list_var IN ITEMS EXTRA_LIBS ADDL_LIB_DIRS_CLEAN EXTRA_FLAGS COMPILE_INCLUDES)
		if(${list_var})
			list(REMOVE_DUPLICATES ${list_var})
		endif()
	endforeach()
	
	# Check if ARGUMENTS contains a variation of main function definition
	string(REGEX MATCH "int[ \t\r\n]+main[ \t\r\n]*\\(" HAS_MAIN "${ARGUMENTS}")
	if(NOT HAS_MAIN)
		# Append a standard int main() block to the end of the existing source code as a failsafe
		string(APPEND ARGUMENTS "\n\nint main(void) {\n    return 0;\n}\n")
	endif()

	set(TEST_SOURCE "${CONFIG_TESTS_DIR}/${target}.c")
	file(WRITE "${TEST_SOURCE}" "${ARGUMENTS}")
	set(OUTPUT_OBJ "${CONFIG_TESTS_DIR}/${target}.o")
    try_compile(COMPILE_RESULT
		"${CONFIG_TESTS_DIR}"
		"${TEST_SOURCE}"
		COMPILE_DEFINITIONS ${EXTRA_FLAGS}
		LINK_LIBRARIES ${EXTRA_LIBS}
		CMAKE_FLAGS
			"-DINCLUDE_DIRECTORIES:PATH=${COMPILE_INCLUDES}"
			"-DLINK_DIRECTORIES:PATH=${ADDL_LIB_DIRS_CLEAN}"
		COPY_FILE "${OUTPUT_OBJ}"
		OUTPUT_VARIABLE COMPILE_OUTPUT
	)

	if(COMPILE_RESULT)
		message(STATUS "${target} check passed")
		set(${RESULT_VAR} 1 PARENT_SCOPE)
	else()
		message(STATUS "${target} check failed")	# Standard message to not clutter build logs overmuch
#		message(STATUS "${target} check failed. Error:\n${COMPILE_OUTPUT}")
#		message(STATUS "${target} check failed. See ${target}_error.log for details")
#		file(WRITE "${CONFIG_TESTS_DIR}/${target}_error.log" "${COMPILE_OUTPUT}")
	endif()
endfunction()

# Rewrite of fmpeg's check_ld function at line 953 of configure script
function(check_ld target ARGUMENTS EXTERNAL_LIB RESULT_VAR)
	set(ALL_ARGS ${ARGN})
	list(APPEND ALL_ARGS "${EXTERNAL_LIB}")
    check_cc("${target}" "${ARGUMENTS}" CC_RESULT ${ALL_ARGS})
    if(CC_RESULT)
        set(${RESULT_VAR} 1 PARENT_SCOPE)
    endif()
endfunction()

# Rewrite of ffmpeg's check_code function at line 972 of configure script
function(check_code target compiler headers ARGUMENTS RESULT_VAR)
	set(EXTRA_FLAGS "")
	foreach(arg IN ITEMS ${ARGN})
    	# Append to EXTRA_FLAGS
    	if(EXTRA_FLAGS STREQUAL "")
    		set(EXTRA_FLAGS "${arg}")
    	else()
    		list(APPEND EXTRA_FLAGS "${arg}")
    	endif()
	endforeach()
	# Check whether more than 1 header file was passed as a function argument
	list(LENGTH headers len)
	if (len GREATER 1)
		foreach(header IN LISTS headers)
			string(APPEND HEADERS_STRING "#include ${header}\n")
		endforeach()
	elseif (headers STREQUAL "")
		set(HEADERS_STRING "")
	else()
		set(HEADERS_STRING "#include ${headers}")
	endif()
	set(test_func "check_${compiler}")
	if (NOT ${test_func} STREQUAL "check_ld")
		set(TEST_CODE "${HEADERS_STRING}\nint main(void) { ${ARGUMENTS};\nreturn 0; }")
		cmake_language(CALL ${test_func} ${target} "${TEST_CODE}" ${RESULT_VAR} ${EXTRA_FLAGS})
	else()
		set(TEST_CODE "${HEADERS_STRING}\nint main(void) { ${ARGUMENTS};\nreturn 0; }")
		cmake_language(CALL ${test_func} ${target} "${TEST_CODE}" "" ${RESULT_VAR} ${EXTRA_FLAGS})
	endif()
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
endfunction()

# Rewrite of ffmpeg's check_header function at line 1053 of configure script
function(check_header target header flag RESULT_VAR)
	set(Backup_Flags ${CMAKE_REQUIRED_FLAGS})
	if(flag)
		string(APPEND CMAKE_REQUIRED_FLAGS " ${flag}")
	endif()
	if (header MATCHES ";")
		check_include_files("${header}" ${RESULT_VAR})
	else()
		check_include_file(${header} ${RESULT_VAR} "${flag}")
	endif()
	set(CMAKE_REQUIRED_FLAGS ${Backup_Flags})
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
endfunction()

# Rewrite of ffmpeg's check_func function at line 1076 of configure script
function(check_func target func RESULT_VAR)
	set(EXTRA_FLAGS "")
	foreach(arg IN ITEMS ${ARGN})
    	# Append to EXTRA_FLAGS
    	if(EXTRA_FLAGS STREQUAL "")
    		set(EXTRA_FLAGS "${arg}")
    	else()
    		list(APPEND EXTRA_FLAGS "${arg}")
    	endif()
	endforeach()
	set(TEST_CODE "extern int ${func}();\nint main(void){ ${func}(); }")
	check_ld(${target} "${TEST_CODE}" "" ${RESULT_VAR} ${EXTRA_FLAGS})
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
endfunction()

# Rewrite of ffmpeg's check_complexfunc function at line 1087 of configure script
function(check_complexfunc target func RESULT_VAR)
	set(TEST_CODE "#include <complex.h>\n#include <math.h>\nfloat foo(complex float f, complex float g) { return ${func}(f * I); \}\nint main(void){ return (int) foo; \}")
	if(MSVC)
		check_ld(${target} "${TEST_CODE}" "-lmsvcrt" ${RESULT_VAR})
	else()
		check_ld(${target} "${TEST_CODE}" "-lm" ${RESULT_VAR})
	endif()
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
endfunction()

# Rewrite of ffmpeg's check_mathfunc function at line 1102 of configure script
function(check_mathfunc target func lib RESULT_VAR)
	set(args1 "f, g")
	set(args2 "f")
	if ("${target}" MATCHES "atan2f|copysign|hypot|ldexpf|powf")
		set(TEST_CODE "#include <math.h>\nfloat foo(float f, float g) { return ${func}(${args1}); }\nint main(void){ return (int) foo; }")
	else()
		set(TEST_CODE "#include <math.h>\nfloat foo(float f, float g) { return ${func}(${args2}); }\nint main(void){ return (int) foo; }")
	endif()
	check_ld(${target} "${TEST_CODE}" "${lib}" ${RESULT_VAR})
	if (${RESULT_VAR} EQUAL 1)
		set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
	endif()
endfunction()

# Rewrite of ffmpeg's check_func_headers function at line 1116 of configure script
function(check_func_headers target headers funcs lib RESULT_VAR)
	# Check whether more than 1 header file was passed as an argument
	list(LENGTH headers len)
	if (len GREATER 1)
		foreach(header IN LISTS headers)
			string(APPEND HEADERS_STRING "#include ${header}\n")
		endforeach()
	else()
		set(HEADERS_STRING "#include ${headers}")
	endif()
	# Check whether more than 1 function to test was passed as an argument
	list(LENGTH funcs len)
	if (len GREATER 1)
		foreach(func IN LISTS funcs)
			string(APPEND FUNCS_STRING "long check_${func}(void) { return (long) ${func}; }\n")
		endforeach()
	else()
		set(FUNCS_STRING "long check_${funcs}(void) { return (long) ${funcs}; }\n")
	endif()
	set(TEST_CODE "${HEADERS_STRING}\n${FUNCS_STRING}\nint main(void) { return 0; }")
	check_ld(${target} "${TEST_CODE}" "${lib}" ${RESULT_VAR})
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
	# To Do: in ffmpeg's configure script, some results prompt adding additional system libs.
endfunction()

# Rewrite of ffmpeg's check_cpp_condition function at line 1151 of configure script
function(check_cpp_condition target header condition RESULT_VAR)
	set(HEADER_STRING "#include <${header}>")
	set(CONDITION_STRING "#if !\(${condition})\n#error \"unsatisfied condition: ${condition}\"\n#endif")
	set(TEST_CODE "${HEADER_STRING}\n${CONDITION_STRING}\nint x;")
	check_cc(${target} "${TEST_CODE}" ${RESULT_VAR})
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
endfunction()

# Rewrite of ffmpeg's check_lib function at line 1164 of configure script
function(check_lib target header func flag RESULT_VAR)
	set(HEADER_STRING "${header}")
	set(FUNC_STRING "${func}")
	set(HEADER_RESULT_VAR "HEADER_${RESULT_VAR}")
	check_header("${target}_header" "${HEADER_STRING}" "${flag}" ${HEADER_RESULT_VAR})
	if (${HEADER_RESULT_VAR} EQUAL 1)
		check_func(${target} "${func}" ${RESULT_VAR} ${flag})
	endif()
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
endfunction()

# Rewrite of ffmpeg's check_lib2 function at line 1164 of configure script
function(check_lib2 target headers funcs lib RESULT_VAR)
	check_func_headers(${target} "${headers}" "${funcs}" "${lib}" ${RESULT_VAR})
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
endfunction()

# Rewrite of ffmpeg's check_type function at line 1237 of configure script
function(check_type target headers type RESULT_VAR)
	check_code(${target} cc "${headers}" "${type} v" ${RESULT_VAR})
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
endfunction()

# Rewrite of ffmpeg's check_struct function at line 1246 of configure script
function(check_struct target headers struct member RESULT_VAR)
	set(EXTRA_FLAGS "")
	foreach(arg IN ITEMS ${ARGN})
    	# Append to EXTRA_FLAGS
    	if(EXTRA_FLAGS STREQUAL "")
    		set(EXTRA_FLAGS "${arg}")
    	else()
    		list(APPEND EXTRA_FLAGS "${arg}")
    	endif()
	endforeach()
	check_code(${target} cc "${headers}" "const void *p = &((${struct} *)0)->${member}" ${RESULT_VAR} ${EXTRA_FLAGS})
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
endfunction()

# Rewrite of ffmpeg's check_builtin function at line 1257 of configure script
function(check_builtin target headers builtin RESULT_VAR)
	check_code(${target} ld "${headers}" "${builtin}" ${RESULT_VAR})
	set(${RESULT_VAR} "${${RESULT_VAR}}" PARENT_SCOPE)
	# Note: configure actually invokes check_code with an extra argument!
	# Now that we've refactored check_cc to accept optional arguments, we could deal with this here if needed
endfunction()

# convenience function at the end that sets any truthy variables to "1" and any falsy variables to "0"
function(set_disabled_to_zero option)
	if(${option})
		set(${option} 1 PARENT_SCOPE)
	else()
		set(${option} 0 PARENT_SCOPE)
	endif()
endfunction()
