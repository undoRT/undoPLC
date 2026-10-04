cmake_minimum_required(VERSION 3.16)
project(undoPLC_test VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
#  Include directories
# ============================================================================
set(UNDOPLC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

# ============================================================================
#  Compiler flags
# ============================================================================
set(UNDOPLC_COMPILE_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -pthread
)

# ============================================================================
#  Test executable
#
#  UndoLatch is header only, so this test needs neither libundoPLC nor Boost.
# ============================================================================
add_executable(test_undoLatch
    main.cpp
)

target_include_directories(test_undoLatch PRIVATE
    ${UNDOPLC_INCLUDE_DIR}
)

target_compile_options(test_undoLatch PRIVATE
    ${UNDOPLC_COMPILE_FLAGS}
)

target_link_libraries(test_undoLatch
    pthread
)

# ============================================================================
#  CTest integration
# ============================================================================
enable_testing()
add_test(NAME test_undoLatch COMMAND test_undoLatch)
