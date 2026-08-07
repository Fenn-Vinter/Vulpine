cmake_minimum_required(VERSION 3.20)
project(Fennec LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Find LLVM with standard system fallbacks
find_package(LLVM REQUIRED CONFIG 
    HINTS 
        /usr/lib64/cmake/llvm
        /usr/lib64/llvm/lib/cmake/llvm
        /usr/share/llvm/cmake
)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

# 2. Add LLVM CMake modules to search path so macros work
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# 3. Find MLIR using LLVM_DIR as primary hint, followed by distro paths
find_package(MLIR REQUIRED CONFIG 
    HINTS 
        ${LLVM_DIR}/../mlir 
        ${LLVM_DIR}
        /usr/lib64/cmake/mlir
        /usr/lib64/llvm/lib/cmake/mlir
)
message(STATUS "Found MLIR ${MLIR_PACKAGE_VERSION} at ${MLIR_DIR}")

# Add MLIR CMake modules to search path
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
include(AddMLIR)

# 4. Handle LLVM definitions
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")

# 5. Add internal dependencies
add_subdirectory(Dependencies/SysMacro)

# 6. Source files definition
set(SOURCES 
    fennec/src/lexer.cpp
    fennec/src/parser.cpp
    fennec/src/emitter.cpp
    src/main.cpp
)

add_executable(fennec ${SOURCES})

# 7. Include directories
target_include_directories(fennec PRIVATE 
    ${LLVM_INCLUDE_DIRS}
    ${MLIR_INCLUDE_DIRS}
    fennec/includes 
    includes
)

target_compile_definitions(fennec PRIVATE ${LLVM_DEFINITIONS_LIST})

# 8. Link LLVM and MLIR libraries
llvm_map_components_to_libnames(llvm_libs core support native)

# Core MLIR target libraries available on Fedora/Nobara and Windows
set(mlir_libs
    MLIRIR
    MLIRSupport
    MLIRBytecodeReader
    MLIRBytecodeWriter
)

target_link_libraries(fennec PRIVATE 
    ${llvm_libs} 
    ${mlir_libs} 
    sysmacro
)