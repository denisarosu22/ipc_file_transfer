cmake_minimum_required(VERSION 3.16)

project(IpcFileTransfer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Code shared by both programs:RAII sockets, protocol, logging.
add_library(net_common STATIC
    common/net.cpp
    common/net.h
    common/protocol.h
    common/log.h
)
target_include_directories(net_common PUBLIC common)

if(WIN32)
    target_link_libraries(net_common PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(net_common PUBLIC /W4 /permissive- /utf-8)
else()
    target_compile_options(net_common PUBLIC -Wall -Wextra -Wpedantic)
endif()

add_executable(file_server server/main.cpp)
target_link_libraries(file_server PRIVATE net_common)

add_executable(file_client client/main.cpp)
target_link_libraries(file_client PRIVATE net_common)