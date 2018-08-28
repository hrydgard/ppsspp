cmake_minimum_required (VERSION 3.2.0)
project (DiscordRPC)

set(SRC_DIR ../discord-rpc/src)


# format
set(ALL_SOURCE_FILES
	${SRC_DIR}/backoff.h
	${SRC_DIR}/connection.h
	${SRC_DIR}/msg_queue.h
	${SRC_DIR}/rpc_connection.h
	${SRC_DIR}/serialization.h
	${SRC_DIR}/discord_rpc.cpp
	${SRC_DIR}/rpc_connection.cpp
	${SRC_DIR}/serialization.cpp
	)

if(APPLE)
	set(ALL_SOURCE_FILES ${ALL_SOURCE_FILES}
		${SRC_DIR}/discord_register_osx.m
	)
endif()

if(WIN32)
	set(ALL_SOURCE_FILES ${ALL_SOURCE_FILES} ${SRC_DIR}/connection_win.cpp)
	set(ALL_SOURCE_FILES ${ALL_SOURCE_FILES} ${SRC_DIR}/discord_register_win.cpp)
else()
	set(ALL_SOURCE_FILES ${ALL_SOURCE_FILES} ${SRC_DIR}/connection_unix.cpp)
	set(ALL_SOURCE_FILES ${ALL_SOURCE_FILES} ${SRC_DIR}/discord_register_linux.cpp)
endif()

add_library(discord-rpc STATIC ${ALL_SOURCE_FILES})

target_include_directories(discord-rpc PUBLIC ../discord-rpc/src ../discord-rpc/include ../rapidjson/include)
