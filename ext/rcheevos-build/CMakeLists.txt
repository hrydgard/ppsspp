cmake_minimum_required (VERSION 3.10.0)
project (RCheevos)

set(SRC_DIR ../rcheevos/src)

set(ALL_SOURCE_FILES
	# api
	${SRC_DIR}/api/rc_api_common.c
	${SRC_DIR}/api/rc_api_editor.c
	${SRC_DIR}/api/rc_api_info.c
	${SRC_DIR}/api/rc_api_runtime.c
	${SRC_DIR}/api/rc_api_user.c
	${SRC_DIR}/api/rc_api_common.h
	# client
	${SRC_DIR}/client/rc_client.c
	${SRC_DIR}/client/rc_client_async.c
	${SRC_DIR}/client/rc_client_do_frame.c
	${SRC_DIR}/client/rc_client_external.c
	${SRC_DIR}/client/rc_client_game.c
	${SRC_DIR}/client/rc_client_info.c
	${SRC_DIR}/client/rc_client_load.c
	${SRC_DIR}/client/rc_client_raintegration.c
	${SRC_DIR}/client/rc_client_server.c
	${SRC_DIR}/client/rc_client_user.c
	${SRC_DIR}/client/rc_client_async.h
	${SRC_DIR}/client/rc_client_do_frame.h
	${SRC_DIR}/client/rc_client_external.h
	${SRC_DIR}/client/rc_client_external_versions.h
	${SRC_DIR}/client/rc_client_game.h
	${SRC_DIR}/client/rc_client_internal.h
	${SRC_DIR}/client/rc_client_load.h
	${SRC_DIR}/client/rc_client_raintegration_internal.h
	${SRC_DIR}/client/rc_client_server.h
	# runtime
	${SRC_DIR}/runtime/rc_alloc.c
	${SRC_DIR}/runtime/rc_condition.c
	${SRC_DIR}/runtime/rc_condset.c
	${SRC_DIR}/runtime/rc_consoleinfo.c
	${SRC_DIR}/runtime/rc_format.c
	${SRC_DIR}/runtime/rc_lboard.c
	${SRC_DIR}/runtime/rc_memref.c
	${SRC_DIR}/runtime/rc_operand.c
	${SRC_DIR}/runtime/rc_richpresence.c
	${SRC_DIR}/runtime/rc_runtime.c
	${SRC_DIR}/runtime/rc_runtime_progress.c
	${SRC_DIR}/runtime/rc_trigger.c
	${SRC_DIR}/runtime/rc_typed_value.c
	${SRC_DIR}/runtime/rc_value.c
	${SRC_DIR}/runtime/rc_alloc.h
	${SRC_DIR}/runtime/rc_condition.h
	${SRC_DIR}/runtime/rc_condset.h
	${SRC_DIR}/runtime/rc_eval_state.h
	${SRC_DIR}/runtime/rc_format.h
	${SRC_DIR}/runtime/rc_lboard.h
	${SRC_DIR}/runtime/rc_memref.h
	${SRC_DIR}/runtime/rc_modified_memref.h
	${SRC_DIR}/runtime/rc_operand.h
	${SRC_DIR}/runtime/rc_operator.h
	${SRC_DIR}/runtime/rc_parse_state.h
	${SRC_DIR}/runtime/rc_richpresence.h
	${SRC_DIR}/runtime/rc_trigger.h
	${SRC_DIR}/runtime/rc_typed_value.h
	${SRC_DIR}/runtime/rc_validate.h
	${SRC_DIR}/runtime/rc_value.h
	# util
	${SRC_DIR}/util/md5.c
	${SRC_DIR}/util/rc_compat.c
	${SRC_DIR}/util/rc_util.c
	${SRC_DIR}/util/rc_version.c
	${SRC_DIR}/util/md5.h
	${SRC_DIR}/util/rc_compat.h
	${SRC_DIR}/util/rc_version.h
	)

add_library(rcheevos STATIC ${ALL_SOURCE_FILES})

target_compile_definitions(rcheevos PUBLIC RC_DISABLE_LUA)
target_include_directories(rcheevos PUBLIC ../rcheevos/include)
