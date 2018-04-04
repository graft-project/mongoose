cmake_minimum_required(VERSION 3.5)

Project(cpp_server_test)

find_package (Threads)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug)

add_definitions(-DGN_ENABLE_EVENTFD=1)

add_executable(cpp_server
	graft_server_example.cpp
	../mongoose.c
)

target_include_directories(cpp_server PRIVATE 
	..
)

target_link_libraries(cpp_server
	m
	${CMAKE_THREAD_LIBS_INIT}
)

