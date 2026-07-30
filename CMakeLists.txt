cmake_minimum_required(VERSION 3.15)
project(Fennec)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(Dependencies/SysMacro)

# Explicitly list the paths based on your screenshot
set(SOURCES 
    fennec/src/lexer.cpp
    fennec/src/parser.cpp
    src/main.cpp
)

add_executable(fennec ${SOURCES})

# Point to both include directories seen in your tree
target_include_directories(fennec PRIVATE fennec/includes includes)

target_link_libraries(fennec PRIVATE sysmacro stdc++exp)