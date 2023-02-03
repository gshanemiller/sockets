set(SERVER_TARGET server.tsk)
add_executable(${SERVER_TARGET} "server.cpp")
target_compile_options(${SERVER_TARGET} PUBLIC -g)
target_include_directories(${SERVER_TARGET} PUBLIC .)

set(CLIENT_TARGET client.tsk)
add_executable(${CLIENT_TARGET} "client.cpp")
target_compile_options(${CLIENT_TARGET} PUBLIC -g)
target_include_directories(${CLIENT_TARGET} PUBLIC .)
