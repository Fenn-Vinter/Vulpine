cmake_minimum_required(VERSION 3.20)
project(Fennec LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Find LLVM with standard system fallbacks (Linux & MSYS2 Windows)
find_package(LLVM REQUIRED CONFIG 
    HINTS 
        C:/msys64/ucrt64/lib/cmake/llvm
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
        C:/msys64/ucrt64/lib/cmake/mlir
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

# --- PRE-BUILD STEP: Close running fennec process before building ---
if(WIN32)
    add_custom_command(TARGET fennec PRE_BUILD
        COMMAND cmd.exe /c "taskkill /F /IM fennec.exe >nul 2>&1 || exit 0"
        COMMENT "Closing running fennec instances if active..."
    )
else()
    add_custom_command(TARGET fennec PRE_BUILD
        COMMAND sh -c "killall -q -9 fennec || true"
        COMMENT "Closing running fennec instances if active..."
    )
endif()

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

if(WIN32)
    # Get the directory where GCC/Clang lives (C:/msys64/ucrt64/bin)
    get_filename_component(COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

    add_custom_command(TARGET fennec POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:fennec>
            $<TARGET_FILE_DIR:fennec>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${COMPILER_BIN_DIR}/libwinpthread-1.dll"
            "${COMPILER_BIN_DIR}/libstdc++-6.dll"
            "${COMPILER_BIN_DIR}/libgcc_s_seh-1.dll"
            $<TARGET_FILE_DIR:fennec>
        COMMAND_EXPAND_LISTS
    )
endif()