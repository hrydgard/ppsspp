cmake_minimum_required (VERSION 3.2.0)
project (RCheevos)

set(SRC_DIR ../rcheevos/src)

# format
set(ALL_SOURCE_FILES
	# rapi
	${SRC_DIR}/rapi/rc_api_common.c
	${SRC_DIR}/rapi/rc_api_common.h
	${SRC_DIR}/rapi/rc_api_editor.c
	${SRC_DIR}/rapi/rc_api_info.c
	${SRC_DIR}/rapi/rc_api_runtime.c
	${SRC_DIR}/rapi/rc_api_user.c
	# rcheevos
	${SRC_DIR}/rcheevos/alloc.c
	${SRC_DIR}/rcheevos/condition.c
	${SRC_DIR}/rcheevos/condset.c
	${SRC_DIR}/rcheevos/consoleinfo.c
	${SRC_DIR}/rcheevos/format.c
	${SRC_DIR}/rcheevos/lboard.c
	${SRC_DIR}/rcheevos/memref.c
	${SRC_DIR}/rcheevos/operand.c
	${SRC_DIR}/rc_compat.h
	${SRC_DIR}/rc_compat.c
	${SRC_DIR}/rc_util.c
	${SRC_DIR}/rc_client.c
	${SRC_DIR}/rc_client_internal.h
	${SRC_DIR}/rc_version.h
	${SRC_DIR}/rcheevos/rc_internal.h
	${SRC_DIR}/rcheevos/rc_validate.h
	${SRC_DIR}/rcheevos/rc_validate.c
	${SRC_DIR}/rcheevos/richpresence.c
	${SRC_DIR}/rcheevos/runtime.c
	${SRC_DIR}/rcheevos/runtime_progress.c
	${SRC_DIR}/rcheevos/trigger.c
	${SRC_DIR}/rcheevos/value.c
	# rhash (still need just md5)
	${SRC_DIR}/rhash/md5.c
	${SRC_DIR}/rhash/md5.h
	)

add_library(rcheevos STATIC ${ALL_SOURCE_FILES})

target_compile_definitions(rcheevos PUBLIC RC_DISABLE_LUA)
target_include_directories(rcheevos PUBLIC ../rcheevos/include ../rcheevos/src/rapi)
