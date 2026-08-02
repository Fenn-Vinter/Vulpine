cmake_minimum_required(VERSION 3.20)
# Enable C alongside CXX so LLVM's C-based checks can run
project(Fennec LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Fetch LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

# 2. Convert LLVM_DEFINITIONS string into a proper CMake list
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")

# 3. Add SysMacro dependency
add_subdirectory(Dependencies/SysMacro)

# 4. Explicitly list source files
set(SOURCES 
    fennec/src/lexer.cpp
    fennec/src/parser.cpp
    fennec/src/emitter.cpp
    src/main.cpp
)

add_executable(fennec ${SOURCES})

# 5. Attach include directories and compile definitions to the target
target_include_directories(fennec PRIVATE 
    ${LLVM_INCLUDE_DIRS}
    fennec/includes 
    includes
)

target_compile_definitions(fennec PRIVATE ${LLVM_DEFINITIONS_LIST})

# 6. Link LLVM libraries and SysMacro
llvm_map_components_to_libnames(llvm_libs core support native)
target_link_libraries(fennec PRIVATE ${llvm_libs} sysmacro)