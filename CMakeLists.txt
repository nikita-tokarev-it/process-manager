cmake_minimum_required(VERSION 3.10)

project(ProcessManager)

set(CMAKE_CXX_STANDARD 17)

add_executable(process_manager
    src/main.cpp
)

if(UNIX)
    target_link_libraries(process_manager pthread)
endif()
